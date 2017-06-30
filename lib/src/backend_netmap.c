// Copyright (c) 2014-2017, NetApp, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.


// IWYU pragma: no_include <net/netmap.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <net/if.h>
#include <net/netmap_user.h> // IWYU pragma: keep
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <warpcore/warpcore.h>

#include "arp.h"
#include "backend.h"
#include "eth.h"
#include "ip.h"
#include "udp.h"


/// The backend name.
///
static char backend_name[] = "netmap";


/// Initialize the warpcore netmap backend for engine @p w. This switches the
/// interface to netmap mode, maps the underlying buffers into memory and locks
/// it there, and sets up the extra buffers.
///
/// @param      w       Backend engine.
/// @param[in]  ifname  The OS name of the interface (e.g., "eth0").
/// @param[in]  nbufs   Number of packet buffers to allocate.
///
void backend_init(struct w_engine * w,
                  const char * const ifname,
                  const uint32_t nbufs)
{
    struct w_engine * ww;
    SLIST_FOREACH (ww, &engines, next)
        ensure(strncmp(ifname, ww->b->nif->ni_name, IFNAMSIZ),
               "can only have one warpcore engine active on %s", ifname);

    // open /dev/netmap
    ensure((w->b->fd = open("/dev/netmap", O_RDWR)) != -1,
           "cannot open /dev/netmap");

    // switch interface to netmap mode
    ensure((w->b->req = calloc(1, sizeof(*w->b->req))) != 0,
           "cannot allocate nmreq");
    strncpy(w->b->req->nr_name, ifname, sizeof w->b->req->nr_name);
    w->b->req->nr_name[sizeof w->b->req->nr_name - 1] = '\0';
    w->b->req->nr_version = NETMAP_API;
    w->b->req->nr_ringid &= ~NETMAP_RING_MASK;
    // don't always transmit on poll
    w->b->req->nr_ringid |= NETMAP_NO_TX_POLL;
    w->b->req->nr_flags = NR_REG_ALL_NIC;
    w->b->req->nr_arg3 = nbufs; // request extra buffers
    ensure(ioctl(w->b->fd, NIOCREGIF, w->b->req) != -1,
           "%s: cannot put interface into netmap mode", ifname);

    // mmap the buffer region
    const int flags = PLAT_MMFLAGS;
    ensure((w->mem = mmap(0, w->b->req->nr_memsize, PROT_WRITE | PROT_READ,
                          MAP_SHARED | flags, w->b->fd, 0)) != MAP_FAILED,
           "cannot mmap netmap memory");

    // direct pointer to the netmap interface struct for convenience
    w->b->nif = NETMAP_IF(w->mem, w->b->req->nr_offset);

    // allocate space for tails
    ensure((w->b->tail = calloc(w->b->nif->ni_tx_rings, sizeof(*w->b->tail))) !=
               0,
           "cannot allocate tail");
    for (uint32_t ri = 0; likely(ri < w->b->nif->ni_tx_rings); ri++) {
        const struct netmap_ring * const r = NETMAP_TXRING(w->b->nif, ri);
        // initialize tails
        w->b->tail[ri] = r->tail;
        warn(info, "tx ring %d has %d slots (%d-%d)", ri, r->num_slots,
             r->slot[0].buf_idx, r->slot[r->num_slots - 1].buf_idx);
    }
#ifndef NDEBUG
    for (uint32_t ri = 0; likely(ri < w->b->nif->ni_rx_rings); ri++) {
        const struct netmap_ring * const r = NETMAP_RXRING(w->b->nif, ri);
        warn(info, "rx ring %d has %d slots (%d-%d)", ri, r->num_slots,
             r->slot[0].buf_idx, r->slot[r->num_slots - 1].buf_idx);
    }
#endif

    // save the indices of the extra buffers in the warpcore structure
    w->bufs = calloc(w->b->req->nr_arg3, sizeof(*w->bufs));
    ensure(w->bufs != 0, "cannot allocate w_iov");
    for (uint32_t n = 0, i = w->b->nif->ni_bufs_head;
         likely(n < w->b->req->nr_arg3); n++) {
        w->bufs[n].buf = IDX2BUF(w, i);
        w->bufs[n].idx = i;
        STAILQ_INSERT_HEAD(&w->iov, &w->bufs[n], next);
        memcpy(&i, w->bufs[n].buf, sizeof(i));
    }

    if (w->b->req->nr_arg3 != nbufs)
        die("can only allocate %d/%d extra buffers", w->b->req->nr_arg3, nbufs);
    else
        warn(notice, "allocated %d extra buffers", w->b->req->nr_arg3);

    // lock memory
    ensure(mlockall(MCL_CURRENT | MCL_FUTURE) != -1, "mlockall");

    // initialize random port number generation state
    w->b->next_eph = (uint16_t)plat_random();

    w->backend = backend_name;
    SPLAY_INIT(&w->b->arp_cache);
}


/// Shut a warpcore netmap engine down cleanly. This function returns all w_iov
/// structures associated the engine to netmap.
///
/// @param      w     Backend engine.
///
void backend_cleanup(struct w_engine * const w)
{
    // free ARP cache
    free_arp_cache(w);

    // re-construct the extra bufs list, so netmap can free the memory
    for (uint32_t n = 0; likely(n < w->b->req->nr_arg3); n++) {
        uint32_t * const buf = (void *)IDX2BUF(w, w->bufs[n].idx);
        if (likely(n < w->b->req->nr_arg3 - 1))
            *buf = w->bufs[n + 1].idx;
        else
            *buf = 0;
    }
    w->b->nif->ni_bufs_head = w->bufs[0].idx;

    ensure(munmap(w->mem, w->b->req->nr_memsize) != -1,
           "cannot munmap netmap memory");

    ensure(close(w->b->fd) != -1, "cannot close /dev/netmap");
    free(w->b->req);
    free(w->b->tail);
}


/// Netmap-specific code to bind a warpcore socket. Only computes a random port
/// number per RFC 6056, if the socket is not binding to a specific port.
///
/// @param      s     The w_sock to bind.
///
void backend_bind(struct w_sock * s)
{
    if (s->hdr->udp.sport)
        return;

    // compute a random local port number per RFC 6056, Section 3.3.5
    const uint16_t N = 500;
    const uint16_t min_eph = 1024;
    const uint16_t max_eph = UINT16_MAX;
    uint16_t num_eph = max_eph - min_eph + 1;
    uint16_t count = num_eph;
    do {
        s->w->b->next_eph += (plat_random() % N) + 1;
        const uint16_t port = htons(min_eph + (s->w->b->next_eph % num_eph));
        if (get_sock(s->w, port) == 0) {
            s->hdr->udp.sport = port;
            return;
        }
    } while (--count > 0);

    die("could not allocate suitable random local port");
}


/// The netmap backend performs no operation here.
///
/// @param      s     The w_sock to close.
///
void backend_close(struct w_sock * const s __attribute__((unused)))
{
}


/// Connect the given w_sock, using the netmap backend. If the Ethernet MAC
/// address of the destination (or the default router towards it) is not known,
/// it will block trying to look it up via ARP.
///
/// @param      s     w_sock to connect.
///
void backend_connect(struct w_sock * const s)
{
    // find the Ethernet MAC address of the destination or the default router,
    // and update the template header
    const uint32_t ip = s->w->rip && (mk_net(s->hdr->ip.dst, s->w->mask) !=
                                      mk_net(s->hdr->ip.src, s->w->mask))
                            ? s->w->rip
                            : s->hdr->ip.dst;
    s->hdr->eth.dst = arp_who_has(s->w, ip);
}


/// Return the file descriptor associated with a w_sock. For the netmap backend,
/// this is the per-interface netmap file descriptor. It can be used for poll()
/// or with event-loop libraries in the application.
///
/// @param      s     w_sock socket for which to get the underlying descriptor.
///
/// @return     A file descriptor.
///
int w_fd(const struct w_sock * const s)
{
    return s->w->b->fd;
}


/// Return any new data that has been received on a socket by appending it to
/// the w_iov tail queue @p i. The tail queue must eventually be returned to
/// warpcore via w_free().
///
/// @param      s     w_sock for which the application would like to receive new
///                   data.
/// @param      i     w_iov tail queue to append new data to.
///
void w_rx(struct w_sock * const s, struct w_iov_stailq * const i)
{
    STAILQ_CONCAT(i, &s->iv);
}


/// Loops over the w_iov structures in the w_iov_stailq @p o, sending them all
/// over w_sock @p s. Places the payloads into IPv4 UDP packets, and attempts to
/// move them into TX rings. Will force a NIC TX if all rings are full, retry
/// the failed w_iovs. The (last batch of) packets are not send yet; w_nic_tx()
/// needs to be called (again) for that. This is, so that an application has
/// control over exactly when to schedule packet I/O.
///
/// @param      s     w_sock socket to transmit over.
/// @param      o     w_iov_stailq to send.
///
void w_tx(const struct w_sock * const s, struct w_iov_stailq * const o)
{
    struct w_iov * v;
    o->tx_pending = 0;
    STAILQ_FOREACH (v, o, next) {
        o->tx_pending++;
        v->o = o;
        ensure(w_connected(s) || v->ip && v->port,
               "no destination information");
        while (unlikely(udp_tx(s, v) == false))
            w_nic_tx(s->w);
    }
}


/// Trigger netmap to make new received data available to w_rx(). Iterates over
/// any new data in the RX rings, calling eth_rx() for each.
///
/// @param[in]  w     Backend engine.
/// @param[in]  msec  Timeout in milliseconds. Pass zero for immediate return,
///                   -1 for infinite wait.
///
/// @return     Whether any data is ready for reading.
///
bool w_nic_rx(struct w_engine * const w, const int32_t msec)
{
    struct pollfd fds = {.fd = w->b->fd, .events = POLLIN};
    const int n = poll(&fds, 1, msec) > 0;
    if (n <= 0)
        return false;

    // loop over all rx rings starting with cur_rxr and wrapping around
    for (uint32_t i = 0; likely(i < w->b->nif->ni_rx_rings); i++) {
        struct netmap_ring * const r = NETMAP_RXRING(w->b->nif, i);
        while (likely(!nm_ring_empty(r))) {
            // prefetch the next slot into the cache
            __builtin_prefetch(
                NETMAP_BUF(r, r->slot[nm_ring_next(r, r->cur)].buf_idx));

            // process the current slot
            warn(debug, "rx idx %u from ring %u slot %u",
                 r->slot[r->cur].buf_idx, i, r->cur);
            eth_rx(w, r);
            r->head = r->cur = nm_ring_next(r, r->cur);
        }
    }
    return true;
}


/// Push data placed in the TX rings via udp_tx() and similar methods out onto
/// the link. Also move any transmitted data back into the original w_iovs.
///
/// @param[in]  w     Backend engine.
///
void w_nic_tx(struct w_engine * const w)
{
    ensure(ioctl(w->b->fd, NIOCTXSYNC, 0) != -1, "cannot kick tx ring");

    // grab the transmitted data out of the NIC rings and place it back into
    // the original w_iov_stailqs, so it's not lost to the app
    for (uint32_t i = 0; likely(i < w->b->nif->ni_tx_rings); i++) {
        struct netmap_ring * const r = NETMAP_TXRING(w->b->nif, i);
        // warn(warn, "tx ring %u: tail %u, cur %u, head %u", i, r->tail,
        //      r->cur, r->head);

        // XXX we need to abuse the netmap API here by touching tail until a fix
        // is included upstream
        for (uint32_t j = nm_ring_next(r, w->b->tail[i]);
             likely(j != nm_ring_next(r, r->tail)); j = nm_ring_next(r, j)) {
            struct netmap_slot * const s = &r->slot[j];
            struct w_iov * const v = (struct w_iov *)s->ptr;
            if (likely(v)) {
                warn(debug, "moving idx %u from ring %u slot %u back into "
                            "w_iov after tx (swap with %u)",
                     s->buf_idx, i, j, v->idx);
                const uint32_t slot_idx = s->buf_idx;
                s->buf_idx = v->idx;
                v->idx = slot_idx;
                s->flags = NS_BUF_CHANGED;
                s->ptr = 0;

                // update tx_pending
                if (likely(v->o))
                    v->o->tx_pending--;
            } else
                warn(warn, "no w_iov in ring %u slot %u, ignoring", i, j);
        }

        // remember current tail
        w->b->tail[i] = r->tail;
    }
}


/// Fill a w_sock_slist with pointers to some sockets with pending inbound data.
/// Data can be obtained via w_rx() on each w_sock in the list. Call can
/// optionally block to wait for at least one ready connection. Will return the
/// number of ready connections, or zero if none are ready. When the return
/// value is not zero, a repeated call may return additional ready sockets.
///
/// @param[in]  w     Backend engine.
/// @param      sl    Empty and initialized w_sock_slist.
///
/// @return     Number of connections that are ready for reading.
///
uint32_t w_rx_ready(struct w_engine * const w, struct w_sock_slist * const sl)
{
    // insert all sockets with pending inbound data
    struct w_sock * s;
    uint32_t n = 0;
    SPLAY_FOREACH (s, sock, &w->sock)
        if (!STAILQ_EMPTY(&s->iv)) {
            SLIST_INSERT_HEAD(sl, s, next_rx);
            n++;
        }

    return n;
}
