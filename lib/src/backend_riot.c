// SPDX-License-Identifier: BSD-2-Clause
//
// Copyright (c) 2014-2019, NetApp, Inc.
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


#include <stdint.h>

#include "backend.h"


static void __attribute__((nonnull))
to_sock_udp_ep_t(sock_udp_ep_t * const suet,
                 const struct w_addr * const addr,
                 const uint16_t port,
                 const kernel_pid_t id)
{
    suet->family = addr->af;
    suet->port = bswap16(port);
    suet->netif = id;
    if (unlikely(addr->af == AF_INET))
        memcpy(&suet->addr.ipv4_u32, &addr->ip4, IP4_LEN);
    else
        memcpy(&suet->addr.ipv6, addr->ip6, IP6_LEN);
}


void w_set_sockopt(struct w_sock * const s, const struct w_sockopt * const opt)
{
}


uint16_t backend_addr_cnt(void)
{
    const gnrc_netif_t * iface = 0;
    while ((iface = gnrc_netif_iter(iface))) {
        uint8_t link = 1;
        const int ret = netif_get_opt(iface->pid, NETOPT_LINK_CONNECTED, 0,
                                      &link, sizeof(link));
        if (ret < 0 || link == NETOPT_DISABLE)
            continue;

        ipv6_addr_t addr[GNRC_NETIF_IPV6_ADDRS_NUMOF];
        const int n = gnrc_netif_ipv6_addrs_get(iface, addr, sizeof(addr));
        if (n < 0)
            continue;
        for (size_t idx = 0; idx < n / sizeof(ipv6_addr_t); idx++)
            // take the first interface with a valid config
            return 1;
    }
    return 0;
}


/// Initialize the warpcore RIOT backend for engine @p w.
///
/// @param      w        Backend engine.
/// @param[in]  nbufs    Number of packet buffers to allocate.
///
void backend_init(struct w_engine * const w, const uint32_t nbufs)
{
    w->backend_name = "riot";
    w->backend_variant = "gnrc";

    ipv6_addr_t addr[GNRC_NETIF_IPV6_ADDRS_NUMOF];
    size_t idx;
    const gnrc_netif_t * iface = 0;
    while ((iface = gnrc_netif_iter(iface))) {
        const int n = gnrc_netif_ipv6_addrs_get(iface, addr, sizeof(addr));
        if (n < 0)
            continue;
        for (idx = 0; idx < n / sizeof(ipv6_addr_t); idx++) {
            // take the first interface with a valid config
            goto done;
        }
    }

done:
    ensure(iface, "iface not found");
    netif_get_name(iface->pid, w->ifname);

    w->have_ip6 = true;
    w->mtu = iface->ipv6.mtu;
    w->mbps = UINT32_MAX;
    memcpy(&w->mac, iface->l2addr, ETH_LEN);

    struct w_ifaddr * ia = &w->ifaddr[0];
    ia->addr.af = AF_INET6;
    memcpy(ia->addr.ip6, &addr[idx], IP6_LEN);
    if (ipv6_addr_is_link_local(&addr[idx]))
        ip6_config(ia, (const uint8_t[]){0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                         0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
                                         0x00, 0x00, 0x00, 0x00});
    else
        die("TODO: handle non-link-local");

    // TODO: shouldn't there a way to use the underlying packet buffers?

    ensure((w->mem = calloc(nbufs, max_buf_len(w))) != 0,
           "cannot alloc %" PRIu32 " * %u buf mem", nbufs, max_buf_len(w));
    ensure((w->bufs = calloc(nbufs, sizeof(*w->bufs))) != 0,
           "cannot alloc bufs");

    for (uint32_t i = 0; i < nbufs; i++) {
        init_iov(w, &w->bufs[i], i);
        sq_insert_head(&w->iov, &w->bufs[i], next);
    }
}


/// Shut a warpcore RIOT engine down cleanly.
///
/// @param      w     Backend engine.
///
void backend_cleanup(struct w_engine * const w)
{
    struct w_sock * s;
    sl_foreach (s, &w->b->socks, __next)
        w_close(s);
    free(w->mem);
    free(w->bufs);
}


/// RIOT-specific code to bind a warpcore socket.
///
/// @param      s     The w_sock to bind.
/// @param[in]  opt   Socket options for this socket. Can be zero.
///
/// @return     Zero on success, @p errno otherwise.
///
int backend_bind(struct w_sock * const s, const struct w_sockopt * const opt)
{
    // TODO: socket options
    sock_udp_ep_t local;
    to_sock_udp_ep_t(&local, &s->ws_laddr, s->ws_lport, s->w->b->id);
    s->fd = (intptr_t)malloc(sizeof(sock_udp_t));
    if (unlikely(s->fd == 0))
        return EDESTADDRREQ; // not quite right
    const int ret = sock_udp_create((sock_udp_t *)s->fd, &local, 0, 0);
    if (unlikely(ret < 0)) {
        free((void *)s->fd);
        return ret;
    }

    // if we're binding to a random port, find out what it is
    if (s->ws_lport == 0) {
        ensure(sock_udp_get_local((sock_udp_t *)s->fd, &local) >= 0,
               "sock_udp_get_local");
        s->ws_lport = bswap16(local.port);
    }
    sl_insert_head(&s->w->b->socks, s, __next);
    return ret;
}


/// Close a RIOT socket.
///
/// @param      s     The w_sock to close.
///
void backend_close(struct w_sock * const s)
{
    sock_udp_close((sock_udp_t *)s->fd);
    sl_remove(&s->w->b->socks, s, w_sock, __next);
    free((void *)s->fd);
}


/// Connect the given w_sock, using the RIOT backend.
///
/// @param      s     w_sock to connect.
///
/// @return     Zero on success, @p errno otherwise.
///
int backend_connect(struct w_sock * const s)
{
    // TODO: can we connect an already-bound socket?
    sock_udp_close((sock_udp_t *)s->fd);
    sock_udp_ep_t local;
    sock_udp_ep_t remote;
    to_sock_udp_ep_t(&local, &s->ws_laddr, s->ws_lport, s->w->b->id);
    to_sock_udp_ep_t(&remote, &s->ws_raddr, s->ws_rport, s->w->b->id);
    return sock_udp_create((sock_udp_t *)s->fd, &local, &remote,
                           SOCK_FLAGS_REUSE_EP);
}


/// Return any new data that has been received on a socket by appending it to
/// the w_iov tail queue @p i. The tail queue must eventually be returned to
/// warpcore via w_free().
///
/// @param      s     w_sock for which the application would like to receive new
///                   data.
/// @param      i     w_iov tail queue to append new data to.
///
void w_rx(struct w_sock * const s, struct w_iov_sq * const i)
{
    sq_concat(i, &s->iv);
}


/// Loops over the w_iov structures in the w_iov_sq @p o, sending them all
/// over w_sock @p s.
///
/// @param      s     w_sock socket to transmit over.
/// @param      o     w_iov_sq to send.
///
void w_tx(struct w_sock * const s, struct w_iov_sq * const o)
{
    struct w_iov * v = sq_first(o);
    while (v) {
        sock_udp_ep_t dst;
        if (w_connected(s) == false)
            to_sock_udp_ep_t(&dst, &v->wv_addr, v->wv_port, s->w->b->id);

        if (unlikely(sock_udp_send((sock_udp_t *)s->fd, v->buf, v->len,
                                   w_connected(s) ? 0 : &dst) != v->len))
            warn(ERR, "sock_udp_send returned %d (%s)", errno, strerror(errno));
        v = sq_next(v, next);
    };
    o->tx_pending = 0; // blocking I/O, no need to update o->tx_pending
}


/// Trigger RIOT to make new received data available to w_rx().
///
/// @param[in]  w     Backend engine.
/// @param[in]  nsec  Timeout in nanoseconds. Pass zero for immediate return, -1
///                   for infinite wait.
///
/// @return     Whether any data is ready for reading.
///
bool w_nic_rx(struct w_engine * const w, const int64_t nsec)
{
    // FIXME: this is a super-ugly hack to work around missing poll & select
    bool rxed = false;
    const uint32_t usec = nsec == -1 ? SOCK_NO_TIMEOUT : nsec / NS_PER_US;

    struct w_sock * s;
    sl_foreach (s, &w->b->socks, __next) {
        struct w_iov * const v = w_alloc_iov(w, s->ws_af, 0, 0);
        if (unlikely(v == 0))
            break;
        sock_udp_ep_t remote;
        const ssize_t n =
            sock_udp_recv((sock_udp_t *)s->fd, v->buf, v->len, usec, &remote);
        if (n > 0) {
            v->len = n;
            v->wv_port = bswap16(remote.port);
            v->wv_af = remote.family;
            if (unlikely(remote.family == AF_INET))
                memcpy(&v->wv_ip4, &remote.addr.ipv4_u32, IP4_LEN);
            else
                memcpy(v->wv_ip6, &remote.addr.ipv6, IP6_LEN);
            sq_insert_tail(&s->iv, v, next);
            rxed = true;
        } else
            w_free_iov(v);
    }
    return rxed;
}


/// Push data placed in the TX rings via udp_tx() and similar methods out
/// onto the link. Also move any transmitted data back into the original
/// w_iovs.
///
/// @param[in]  w     Backend engine.
///
void w_nic_tx(struct w_engine * const w) {}


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
    // FIXME: this is a super-ugly hack to work around missing poll & select
    uint32_t n = 0;
    struct w_sock * s;
    sl_foreach (s, &w->b->socks, __next) {
        if (!sq_empty(&s->iv)) {
            sl_insert_head(sl, s, next);
            n++;
        }
    }
    return n;
}
