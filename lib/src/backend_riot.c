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


#include "backend.h"

#include <fmt.h>
#include <stdint.h>
#include <sys/select.h>


void w_set_sockopt(struct w_sock * const s, const struct w_sockopt * const opt)
{
    s->opt = *opt;
}


uint16_t backend_addr_cnt(void)
{
    gnrc_netif_t * iface = 0;
    while ((iface = gnrc_netif_iter(iface))) {
        netopt_enable_t link = NETOPT_ENABLE;
        const int ret =
            netif_get_opt(&iface->netif, NETOPT_LINK, 0, &link, sizeof(link));
        if (ret < 0 || link == NETOPT_DISABLE)
            continue;

        ipv6_addr_t addr[CONFIG_GNRC_NETIF_IPV6_ADDRS_NUMOF];
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

    ipv6_addr_t addr[CONFIG_GNRC_NETIF_IPV6_ADDRS_NUMOF];
    size_t idx;
    while ((w->b->nif = gnrc_netif_iter(w->b->nif))) {
        const int n = gnrc_netif_ipv6_addrs_get(w->b->nif, addr, sizeof(addr));
        if (n < 0)
            continue;
        for (idx = 0; idx < n / sizeof(ipv6_addr_t); idx++) {
            // take the first interface with a valid config
            goto done;
        }
    }

done:
    ensure(w->b->nif, "iface not found");
    netif_get_name((netif_t *)w->b->nif, w->ifname);

    w->have_ip6 = true;
    w->mtu = w->b->nif->ipv6.mtu;
    w->mbps = UINT32_MAX;
    memcpy(&w->mac, w->b->nif->l2addr, ETH_LEN);

    struct w_ifaddr * ia = &w->ifaddr[0];
    ia->addr.af = AF_INET6;
    ia->scope_id = netif_get_id((netif_t *)w->b->nif);
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
    s->fd = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (unlikely(s->fd < 0))
        return errno;

    struct sockaddr_storage ss;
    to_sockaddr((struct sockaddr *)&ss, &s->ws_laddr, s->ws_lport, s->ws_scope);
    if (bind(s->fd, (struct sockaddr *)&ss, sa_len(s->ws_af)) < 0)
        return errno;

    // RIOT only assigns a local port on connect, if passed a zero port

    sl_insert_head(&s->w->b->socks, s, __next);
    return 0;
}


/// Close a RIOT socket.
///
/// @param      s     The w_sock to close.
///
void backend_close(struct w_sock * const s)
{
    close(s->fd);
    sl_remove(&s->w->b->socks, s, w_sock, __next);
}


void backend_preconnect(struct w_sock * const s __attribute__((unused))) {}


/// Connect the given w_sock, using the RIOT backend.
///
/// @param      s     w_sock to connect.
///
/// @return     Zero on success, @p errno otherwise.
///
int backend_connect(struct w_sock * const s)
{
    struct sockaddr_storage ss;
    to_sockaddr((struct sockaddr *)&ss, &s->ws_raddr, s->ws_rport, s->ws_scope);
    if (unlikely(connect(s->fd, (struct sockaddr *)&ss, sa_len(ss.ss_family)) !=
                 0))
        return errno;

    // if we're now bound to a random port, find out what it is
    if (s->ws_lport == 0) {
        socklen_t len = sizeof(ss);
        ensure(getsockname(s->fd, (struct sockaddr *)&ss, &len) >= 0,
               "getsockname");
        s->ws_lport = sa_port(&ss);
    }

    return 0;
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
    struct w_iov * v = w_alloc_iov(s->w, s->ws_af, 0, 0);
    if (unlikely(v == 0))
        return;

    struct sockaddr_storage sa;
    socklen_t sa_len = sizeof(sa);
    v->len =
        recvfrom(s->fd, v->buf, v->len, 0, (struct sockaddr *)&sa, &sa_len);

    if (likely(v->len > 0)) {
        v->wv_port = sa_port(&sa);
        w_to_waddr(&v->wv_addr, (struct sockaddr *)&sa);
        sq_insert_tail(i, v, next);
    } else
        w_free_iov(v);
}


/// Loops over the w_iov structures in the w_iov_sq @p o, sending them all
/// over w_sock @p s.
///
/// @param      s     w_sock socket to transmit over.
/// @param      o     w_iov_sq to send.
///
void w_tx(struct w_sock * const s, struct w_iov_sq * const o)
{
    const bool is_connected = w_connected(s);

    struct w_iov * v = sq_first(o);
    while (v) {
        struct sockaddr_storage ss;
        if (is_connected == false)
            to_sockaddr((struct sockaddr *)&ss, &v->wv_addr, v->wv_port,
                        s->ws_scope);

        if (unlikely(sendto(s->fd, v->buf, v->len, 0,
                            is_connected ? 0 : (struct sockaddr *)&ss,
                            is_connected ? 0 : sa_len(s->ws_af)) != v->len))
            warn(ERR, "sendto returned %d (%s)", errno, strerror(errno));
        v = sq_next(v, next);
    };
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
    struct w_backend * const b = w->b;
    FD_ZERO(&b->fds);
    struct w_sock * s;
    sl_foreach (s, &b->socks, __next)
        FD_SET(s->fd, &b->fds);

    const time_t sec = NS_TO_S(nsec);
    const time_t usec = NS_TO_US(nsec - sec * NS_PER_S);
    struct timeval to = {.tv_sec = sec, .tv_usec = usec};

    b->n = select(MIN(FD_SETSIZE, VFS_MAX_OPEN_FILES) - 1, &b->fds, 0, 0,
                  nsec == -1 ? 0 : &to);
    return b->n > 0;
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
    uint32_t i = 0;
    struct w_sock * s;
    sl_foreach (s, &w->b->socks, __next)
        if (FD_ISSET(s->fd, &w->b->fds)) {
            sl_insert_head(sl, s, next);
            i++;
        }

    return i;
}
