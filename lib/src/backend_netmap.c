// SPDX-License-Identifier: BSD-2-Clause
//
// Copyright (c) 2014-2022, NetApp, Inc.
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

#ifdef __FreeBSD__
#include <netinet/in.h>
#endif

#include <fcntl.h>
#include <net/if.h>
#include <net/netmap_user.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <net/netmap.h>
#include <net/netmap_legacy.h>
#include <warpcore/warpcore.h>

#ifdef HAVE_ASAN
#include <sanitizer/asan_interface.h>
#endif

#include "backend.h"
#include "eth.h"
#include "ifaddr.h"
#include "neighbor.h"
#include "udp.h"


static void __attribute__((nonnull)) ins_sock(struct w_sock * const s)
{
    int ret;
    const khiter_t k = kh_put(sock, &s->w->b->sock, &s->tup, &ret);
    assure(ret >= 1, "inserted is %d", ret);
    kh_val(&s->w->b->sock, k) = s;
}


static void __attribute__((nonnull)) rem_sock(struct w_sock * const s)
{
    const khiter_t k = kh_get(sock, &s->w->b->sock, &s->tup);
    assure(k != kh_end(&s->w->b->sock), "found");
    kh_del(sock, &s->w->b->sock, k);
}


/// Set the socket options.
///
/// @param      s     The w_sock to change options for.
/// @param[in]  opt   Socket options for this socket.
///
void w_set_sockopt(struct w_sock * const s, const struct w_sockopt * const opt)
{
    s->opt = *opt;
}


/// Initialize the warpcore netmap backend for engine @p w. This switches the
/// interface to netmap mode, maps the underlying buffers into memory and locks
/// it there, and sets up the extra buffers.
///
/// @param      w      Backend engine.
/// @param[in]  nbufs  Number of packet buffers to allocate.
///
void backend_init(struct w_engine * const w, const uint32_t nbufs)
{
    struct w_backend * const b = w->b;

    backend_addr_config(w);

    // open /dev/netmap
    ensure((b->fd = open("/dev/netmap", O_RDWR | O_CLOEXEC)) != -1,
           "cannot open /dev/netmap");
    w->backend_name = "netmap";
    w->backend_variant =
        w->is_loopback
            ? (w->is_right_pipe ? "right loopback pipe" : "left loopback pipe")
            : "default";

    // switch interface to netmap mode
    ensure((b->req = calloc(1, sizeof(*b->req))) != 0, "cannot allocate nmreq");
    b->req->nr_name[sizeof b->req->nr_name - 1] = 0;
    b->req->nr_version = NETMAP_API;
    b->req->nr_ringid &= ~NETMAP_RING_MASK;
    b->req->nr_ringid |= NETMAP_NO_TX_POLL; // don't always transmit on poll
    b->req->nr_flags = NR_REG_ALL_NIC;

    // if the interface is a netmap pipe, restore its name
    if (w->is_loopback) {
        warn(NTE, "%s is a loopback, using %s netmap pipe", w->ifname,
             w->is_right_pipe ? "right" : "left");

        // loopback has typically 64KB MTU, but netmap uses 2048-byte buffers
        w->mtu = 2048 - sizeof(struct eth_hdr);

        snprintf(b->req->nr_name, IFNAMSIZ, "w-%.*s", IFNAMSIZ - 3, w->ifname);
        b->req->nr_flags =
            w->is_right_pipe ? NR_REG_PIPE_MASTER : NR_REG_PIPE_SLAVE;
        b->req->nr_ringid = 1 & NETMAP_RING_MASK;

        // preload ARP cache
        for (uint16_t idx = 0; idx < w->addr_cnt; idx++)
            neighbor_update(w, &w->ifaddr[idx].addr,
                            (struct eth_addr){ETH_ADDR_NONE});
    } else {
        strncpy(b->req->nr_name, w->ifname, sizeof b->req->nr_name);
        b->req->nr_name[sizeof b->req->nr_name - 1] = 0;
    }

    b->req->nr_arg3 = nbufs; // request extra buffers
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverflow"
    ensure(ioctl(b->fd, NIOCREGIF, b->req) != -1,
           "%s: cannot put interface into netmap mode", w->ifname);
#pragma GCC diagnostic pop

    // mmap the buffer region
    const int flags = PLAT_MMFLAGS;
    ensure((w->mem = mmap(0, b->req->nr_memsize, PROT_WRITE | PROT_READ,
                          MAP_SHARED | flags, b->fd, 0)) != MAP_FAILED,
           "cannot mmap netmap memory");

    // direct pointer to the netmap interface struct for convenience
    b->nif = NETMAP_IF(w->mem, b->req->nr_offset);

    // allocate space for tails and slot w_iov pointers
    ensure((b->tail = calloc(b->nif->ni_tx_rings, sizeof(*b->tail))) != 0,
           "cannot allocate tail");
    ensure(b->slot_buf = calloc(b->nif->ni_tx_rings, sizeof(*b->slot_buf)),
           "cannot allocate slot w_iov pointers");
    for (uint32_t ri = 0; likely(ri < b->nif->ni_tx_rings); ri++) {
        const struct netmap_ring * const r = NETMAP_TXRING(b->nif, ri);
        // allocate slot pointers
        ensure(b->slot_buf[ri] = calloc(r->num_slots, sizeof(struct w_iov *)),
               "cannot allocate slot w_iov pointers");
        // initialize tails
        b->tail[ri] = r->tail;
        warn(INF, "tx ring %d has %d slots (%d-%d)", ri, r->num_slots,
             r->slot[0].buf_idx, r->slot[r->num_slots - 1].buf_idx);
    }

#ifndef NDEBUG
    for (uint32_t ri = 0; likely(ri < b->nif->ni_rx_rings); ri++) {
        const struct netmap_ring * const r = NETMAP_RXRING(b->nif, ri);
        warn(INF, "rx ring %d has %d slots (%d-%d)", ri, r->num_slots,
             r->slot[0].buf_idx, r->slot[r->num_slots - 1].buf_idx);
    }
#endif

    // save the indices of the extra buffers in the warpcore structure
    w->bufs = calloc(b->req->nr_arg3, sizeof(*w->bufs));
    ensure(w->bufs != 0, "cannot allocate w_iov");

    uint32_t i = b->nif->ni_bufs_head;
    for (uint32_t n = 0; likely(n < b->req->nr_arg3); n++) {
        init_iov(w, &w->bufs[n], i);
        sq_insert_head(&w->iov, &w->bufs[n], next);
        memcpy(&i, w->bufs[n].buf, sizeof(i));
        ASAN_POISON_MEMORY_REGION(w->bufs[n].buf, max_buf_len(w));
    }

    if (b->req->nr_arg3 != nbufs)
        warn(WRN, "can only allocate %d/%d extra buffers", b->req->nr_arg3,
             nbufs);
    ensure(b->req->nr_arg3 != 0, "got some extra buffers");

    // lock memory
    ensure(mlockall(MCL_CURRENT | MCL_FUTURE) != -1, "mlockall");
}


/// Shut a warpcore netmap engine down cleanly. This function returns all
/// w_iov structures associated the engine to netmap.
///
/// @param      w     Backend engine.
///
void backend_cleanup(struct w_engine * const w)
{
    // close all sockets
    struct w_sock * s;
    kh_foreach_value(&w->b->sock, s, { w_close(s); });
    kh_release(sock, &w->b->sock);

    // free ARP cache
    free_neighbor(w);

    // re-construct the extra bufs list, so netmap can free the memory
    for (uint32_t n = 0; likely(n < sq_len(&w->iov)); n++) {
        uint32_t * const buf = (void *)idx_to_buf(w, w->bufs[n].idx);
        ASAN_UNPOISON_MEMORY_REGION(buf, max_buf_len(w));
        if (likely(n < sq_len(&w->iov) - 1))
            *buf = w->bufs[n + 1].idx;
        else
            *buf = 0;
    }
    w->b->nif->ni_bufs_head = w->bufs[0].idx;

    // free slot w_iov pointers
    for (uint32_t ri = 0; likely(ri < w->b->nif->ni_tx_rings); ri++)
        free(w->b->slot_buf[ri]);
    free(w->b->slot_buf);

    ensure(munmap(w->mem, w->b->req->nr_memsize) != -1,
           "cannot munmap netmap memory");

    ensure(close(w->b->fd) != -1, "cannot close /dev/netmap");
    free(w->bufs);
    free(w->b->req);
    free(w->b->tail);
}


/// Netmap-specific code to bind a warpcore socket. Only computes a random
/// port number if the socket is not bound to a specific port yet.
///
/// @param      s     The w_sock to bind.
/// @param[in]  opt   Socket options for this socket. Can be zero.
///
/// @return     Zero on success, @p errno otherwise.
///
int backend_bind(struct w_sock * const s, const struct w_sockopt * const opt)
{
    if (unlikely(w_get_sock(s->w, &s->ws_loc, 0))) {
        warn(INF, "UDP source port %d already in bound", bswap16(s->ws_lport));
        return 0;
    }

    if (opt)
        w_set_sockopt(s, opt);

    if (likely(s->ws_lport == 0))
        s->ws_lport = pick_local_port();

    ins_sock(s);
    return 0;
}


/// The netmap backend performs no operation here.
///
/// @param      s     The w_sock to close.
///
void backend_close(struct w_sock * const s)
{
    // remove the socket from list of sockets
    rem_sock(s);
}


void backend_preconnect(struct w_sock * const s)
{
    // remove the socket from list of sockets
    rem_sock(s);
}


/// Connect the given w_sock, using the netmap backend. If the Ethernet MAC
/// address of the destination (or the default router towards it) is not
/// known, it will block trying to look it up via ARP.
///
/// @param      s     w_sock to connect.
///
/// @return     Zero on success, @p errno otherwise.
///
int backend_connect(struct w_sock * const s)
{
    // // find the Ethernet MAC address of the destination or the default
    // router,
    // // and update the template header
    // const uint32_t ip = s->w->rip && (mk_net(s->tup.dip, s->w->mask) !=
    //                                   mk_net(s->tup.sip, s->w->mask))
    //                         ? s->w->rip
    //                         : s->tup.dip;
    s->dmac = who_has(s->w, &s->ws_raddr);

    // see if we need to update the sport
    uint8_t n = 200;
    while (n--) {
        if (likely(w_get_sock(s->w, &s->ws_loc, &s->ws_rem) == 0))
            break;
        // four-tuple exists, reroll sport
        s->ws_lport = pick_local_port();
    }

    if (likely(n))
        ins_sock(s);

    return n == 0;
}


/// Return any new data that has been received on a socket by appending it
/// to the w_iov tail queue @p i. The tail queue must eventually be returned
/// to warpcore via w_free().
///
/// @param      s     w_sock for which the application would like to receive
/// new
///                   data.
/// @param      i     w_iov tail queue to append new data to.
///
void w_rx(struct w_sock * const s, struct w_iov_sq * const i)
{
    sq_concat(i, &s->iv);
}


/// Loops over the w_iov structures in the w_iov_sq @p o, sending them all
/// over w_sock @p s. Places the payloads into IPv4 UDP packets, and
/// attempts to move them into TX rings. Will force a NIC TX if all rings
/// are full, retry the failed w_iovs. The (last batch of) packets are not
/// send yet; w_nic_tx() needs to be called (again) for that. This is, so
/// that an application has control over exactly when to schedule packet
/// I/O.
///
/// @param      s     w_sock socket to transmit over.
/// @param      o     w_iov_sq to send.
///
void w_tx(struct w_sock * const s, struct w_iov_sq * const o)
{
    struct w_iov * v;
    sq_foreach (v, o, next) {
        const uint16_t len = v->len;
        while (unlikely(udp_tx(s, v) == false)) {
            w_nic_tx(s->w);
            v->len = len;
        }
    }
}


/// Trigger netmap to make new received data available to w_rx(). Iterates over
/// any new data in the RX rings, calling eth_rx() for each.
///
/// @param[in]  w     Backend engine.
/// @param[in]  nsec  Timeout in nanoseconds. Pass zero for immediate return, -1
///                   for infinite wait.
///
/// @return     Whether any data is ready for reading.
///
bool w_nic_rx(struct w_engine * const w, const int64_t nsec)
{
    struct pollfd fds = {.fd = w->b->fd, .events = POLLIN};
again:
    if (poll(&fds, 1, nsec < 0 ? -1 : (int)(nsec / NS_PER_MS)) == 0)
        return false;

    // loop over all rx rings
    bool rx = false;
    for (uint32_t i = 0; likely(i < w->b->nif->ni_rx_rings); i++) {
        struct netmap_ring * const r = NETMAP_RXRING(w->b->nif, i);
        while (likely(!nm_ring_empty(r))) {
#if 0
            warn(DBG, "rx idx %u from ring %u slot %u", r->slot[r->cur].buf_idx,
                 i, r->cur);
#endif
            // process the current slot
            rx = eth_rx(w, &r->slot[r->cur],
                        (uint8_t *)NETMAP_BUF(r, r->slot[r->cur].buf_idx));
            r->head = r->cur = nm_ring_next(r, r->cur);
        }
    }

    if (rx == false && nsec == -1)
        goto again;

    return rx;
}


/// Push data placed in the TX rings via udp_tx() and similar methods out
/// onto the link. Also move any transmitted data back into the original
/// w_iovs.
///
/// @param[in]  w     Backend engine.
///
void w_nic_tx(struct w_engine * const w)
{
    ensure(ioctl(w->b->fd, NIOCTXSYNC, 0) != -1, "cannot kick tx ring");

    if (unlikely(is_pipe(w)))
        return;

    // grab the transmitted data out of the NIC rings and place it back into
    // the original w_iov_sqs, so it's not lost to the app
    for (uint32_t i = 0; likely(i < w->b->nif->ni_tx_rings); i++) {
        struct netmap_ring * const r = NETMAP_TXRING(w->b->nif, i);
#if 0
        rwarn(WRN, 10, "tx ring %u: old tail %u, tail %u, cur %u, head %u", i,
              w->b->tail[i], r->tail, r->cur, r->head);
#endif

        // XXX we need to abuse the netmap API here by touching tail until a
        // fix is included upstream
        for (uint32_t j = nm_ring_next(r, w->b->tail[i]);
             likely(j != nm_ring_next(r, r->tail)); j = nm_ring_next(r, j)) {
            struct netmap_slot * const s = &r->slot[j];
            struct w_iov * const v = w->b->slot_buf[r->ringid][j];
#if 0
            warn(DBG, "move idx %u from ring %u slot %u to w_iov (swap w/%u)",
                 s->buf_idx, i, j, v->idx);
#endif
            const uint32_t slot_idx = s->buf_idx;
            s->buf_idx = v->idx;
            v->idx = slot_idx;
            s->flags = NS_BUF_CHANGED;
            w->b->slot_buf[i][j] = 0;
        }

        // remember current tail
        w->b->tail[i] = r->tail;
    }
}


/// Fill a w_sock_slist with pointers to some sockets with pending inbound
/// data. Data can be obtained via w_rx() on each w_sock in the list. Call
/// can optionally block to wait for at least one ready connection. Will
/// return the number of ready connections, or zero if none are ready. When
/// the return value is not zero, a repeated call may return additional
/// ready sockets.
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
    kh_foreach_value(&w->b->sock, s, {
        if (!sq_empty(&s->iv)) {
            sl_insert_head(sl, s, next);
            n++;
        }
    });
    return n;
}


/// Get the socket bound to the given four-tuple <source IP, source port,
/// destination IP, destination port>.
///
/// @param      w       Backend engine.
/// @param[in]  local   The local IP address and port.
/// @param[in]  remote  The remote IP address and port.
///
/// @return     The w_sock bound to the given four-tuple.
///
struct w_sock * w_get_sock(struct w_engine * const w,
                           const struct w_sockaddr * const local,
                           const struct w_sockaddr * const remote)
{
    struct w_socktuple tup = {.local = *local};
    if (remote)
        tup.remote = *remote;
    const khiter_t k = kh_get(sock, &w->b->sock, &tup);
    return unlikely(k == kh_end(&w->b->sock)) ? 0 : kh_val(&w->b->sock, k);
}
