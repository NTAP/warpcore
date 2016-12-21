// Copyright (c) 2014-2016, NetApp, Inc.
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


#include <fcntl.h>
// clang-format off
// because these includes need to be in-order
#include <net/if.h> // IWYU pragma: keep
#include <stdint.h> // IWYU pragma: keep
#include <net/netmap.h>
// clang-format on
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/queue.h>
#include <unistd.h>

#ifdef __FreeBSD__
#include <sys/types.h>
#endif

#include <warpcore.h>

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
/// @param      w       Warpcore engine.
/// @param[in]  ifname  The OS name of the interface (e.g., "eth0").
///
void backend_init(struct warpcore * w, const char * const ifname)
{
    struct warpcore * ww;
    SLIST_FOREACH (ww, &engines, next)
        ensure(strncmp(ifname, ww->nif->ni_name, IFNAMSIZ),
               "can only have one warpcore engine active on %s", ifname);

    // open /dev/netmap
    ensure((w->fd = open("/dev/netmap", O_RDWR)) != -1,
           "cannot open /dev/netmap");

    w->req = calloc(1, sizeof(*w->req));
    ensure(w->req != 0, "cannot allocate nmreq");

    // switch interface to netmap mode
    strncpy(w->req->nr_name, ifname, sizeof w->req->nr_name);
    w->req->nr_name[sizeof w->req->nr_name - 1] = '\0';
    w->req->nr_version = NETMAP_API;
    w->req->nr_ringid &= ~NETMAP_RING_MASK;
    // don't always transmit on poll
    w->req->nr_ringid |= NETMAP_NO_TX_POLL;
    w->req->nr_flags = NR_REG_ALL_NIC;
    w->req->nr_arg3 = NUM_BUFS; // request extra buffers
    ensure(ioctl(w->fd, NIOCREGIF, w->req) != -1,
           "%s: cannot put interface into netmap mode", ifname);

    // mmap the buffer region
    const int flags = PLAT_MMFLAGS;
    ensure((w->mem = mmap(0, w->req->nr_memsize, PROT_WRITE | PROT_READ,
                          MAP_SHARED | flags, w->fd, 0)) != MAP_FAILED,
           "cannot mmap netmap memory");

    // direct pointer to the netmap interface struct for convenience
    w->nif = NETMAP_IF(w->mem, w->req->nr_offset);

#ifndef NDEBUG
    // print some info about our rings
    for (uint32_t ri = 0; ri < w->nif->ni_tx_rings; ri++) {
        const struct netmap_ring * const r = NETMAP_TXRING(w->nif, ri);
        warn(info, "tx ring %d has %d slots (%d-%d)", ri, r->num_slots,
             r->slot[0].buf_idx, r->slot[r->num_slots - 1].buf_idx);
    }
    for (uint32_t ri = 0; ri < w->nif->ni_rx_rings; ri++) {
        const struct netmap_ring * const r = NETMAP_RXRING(w->nif, ri);
        warn(info, "rx ring %d has %d slots (%d-%d)", ri, r->num_slots,
             r->slot[0].buf_idx, r->slot[r->num_slots - 1].buf_idx);
    }
#endif

    // save the indices of the extra buffers in the warpcore structure
    STAILQ_INIT(&w->iov);
    w->bufs = calloc(w->req->nr_arg3, sizeof(*w->bufs));
    ensure(w->bufs != 0, "cannot allocate w_iov");
    for (uint32_t n = 0, i = w->nif->ni_bufs_head; n < w->req->nr_arg3; n++) {
        w->bufs[n].buf = IDX2BUF(w, i);
        w->bufs[n].idx = i;
        STAILQ_INSERT_HEAD(&w->iov, &w->bufs[n], next);
        i = *(uint32_t *)w->bufs[n].buf;
    }

    if (w->req->nr_arg3 != NUM_BUFS)
        die("can only allocate %d/%d extra buffers", w->req->nr_arg3, NUM_BUFS);
    else
        warn(notice, "allocated %d extra buffers", w->req->nr_arg3);

    // lock memory
    ensure(mlockall(MCL_CURRENT | MCL_FUTURE) != -1, "mlockall");

    w->backend = backend_name;
    SLIST_INIT(&w->arp_cache);
    SLIST_INIT(&w->tx_pending);
}


/// Shut a warpcore netmap engine down cleanly. This function returns all w_iov
/// structures associated the engine to netmap.
///
/// @param      w     Warpcore engine.
///
void backend_cleanup(struct warpcore * const w)
{
    // free ARP cache
    free_arp_cache(w);

    // re-construct the extra bufs list, so netmap can free the memory
    for (uint32_t n = 0; n < w->req->nr_arg3; n++) {
        uint32_t * const buf = (void *)IDX2BUF(w, w->bufs[n].idx);
        if (n < w->req->nr_arg3 - 1)
            *buf = w->bufs[n + 1].idx;
        else
            *buf = 0;
    }
    w->nif->ni_bufs_head = w->bufs[0].idx;

    ensure(munmap(w->mem, w->req->nr_memsize) != -1,
           "cannot munmap netmap memory");

    ensure(close(w->fd) != -1, "cannot close /dev/netmap");
    free(w->req);
}


/// Netmap-specific code to bind a warpcore socket. Does nothing currently.
///
/// @param      s     The w_sock to bind.
///
void backend_bind(struct w_sock * s __attribute__((unused)))
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
    const uint8_t * const mac = arp_who_has(s->w, ip);
    memcpy(s->hdr->eth.dst, mac, ETH_ADDR_LEN);
}


/// Return the file descriptor associated with a w_sock. For the netmap backend,
/// this is the per-interface netmap file descriptor. It can be used for poll()
/// or with event-loop libraries in the application.
///
/// @param      s     w_sock socket for which to get the underlying descriptor.
///
/// @return     A file descriptor.
///
int w_fd(struct w_sock * const s)
{
    return s->w->fd;
}


/// Places the payload of @p v into an IPv4 UDP packet, and attempts to move it
/// onto a TX ring. Will force a NIC TX if all rings are full, retry the failed
/// w_iov and continue with the chain. The (last batch of) packets are not send
/// yet; w_nic_tx() needs to be called (again) for that. This is, so that an
/// application has control over exactly when to schedule packet I/O.
///
/// @param      s     w_sock socket to transmit over.
/// @param      v     w_iov to transmit.
///
void backend_tx(const struct w_sock * const s, struct w_iov * const v)
{
    while (udp_tx(s, v) == false) {
        warn(notice, "all rings seem full, forcing TX");
        w_nic_tx(s->w);
    }
}


/// Trigger netmap to make new received data available to w_rx(). Iterates over
/// any new data in the RX rings, calling eth_rx() for each.
///
/// @param[in]  w     Warpcore engine.
///
void w_nic_rx(struct warpcore * const w)
{
    ensure(ioctl(w->fd, NIOCRXSYNC, 0) != -1, "cannot kick rx ring");

    // loop over all rx rings starting with cur_rxr and wrapping around
    for (uint32_t i = 0; likely(i < w->nif->ni_rx_rings); i++) {
        struct netmap_ring * const r = NETMAP_RXRING(w->nif, i);
        while (!nm_ring_empty(r)) {
            // prefetch the next slot into the cache
            __builtin_prefetch(
                NETMAP_BUF(r, r->slot[nm_ring_next(r, r->cur)].buf_idx));

            // process the current slot
            eth_rx(w, r);
            r->head = r->cur = nm_ring_next(r, r->cur);
        }
    }
}


/// Push data placed in the TX rings via udp_tx() and similar methods out onto
/// the link.
///
/// @param[in]  w     Warpcore engine.
///
void w_nic_tx(struct warpcore * const w)
{
    ensure(ioctl(w->fd, NIOCTXSYNC, 0) != -1, "cannot kick tx ring");

    // grab the transmitted data out of the NIC rings and place it back into the
    // original w_iov_chains, so it's not lost to the app
    while (!SLIST_EMPTY(&w->tx_pending)) {
        struct w_iov * const v = SLIST_FIRST(&w->tx_pending);
        SLIST_REMOVE_HEAD(&w->tx_pending, next_tx);

        // place the ring buf back into the w_iov
        warn(debug, "moving idx %d from ring %d back into w_iov after tx",
             v->slot->buf_idx, w->cur_txr);
        const uint32_t tmp_idx = v->slot->buf_idx;
        v->slot->buf_idx = v->idx;
        v->slot->flags = NS_BUF_CHANGED;
        v->idx = tmp_idx;
        // v->buf and v->len are unchanged after NIC TX, no need to update
    }
}
