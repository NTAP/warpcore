// SPDX-License-Identifier: BSD-2-Clause
//
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

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <unistd.h>

// IWYU pragma: no_include <net/netmap.h>
#ifdef WITH_NETMAP
#include <net/netmap_user.h> // IWYU pragma: keep
#endif

// IWYU pragma: no_include <sys/queue.h>
#include <warpcore/warpcore.h>

#ifdef HAVE_ASAN
#include <sanitizer/asan_interface.h>
#endif

#if !defined(NDEBUG) && DLEVEL >= NTE
#ifdef __linux__
#include <netinet/ether.h>
#else
#include <net/ethernet.h>
#endif
#endif

#include "backend.h"
#include "eth.h"
#include "ip.h"
#include "udp.h"


int16_t w_sock_cmp(const struct w_sock * const a, const struct w_sock * const b)
{
    return (int16_t)a->hdr->udp.sport - (int16_t)b->hdr->udp.sport;
}


SPLAY_GENERATE(sock, w_sock, next, w_sock_cmp)


/// A global list of netmap engines that have been initialized for different
/// interfaces.
///
struct w_engines engines = sl_head_initializer(engines);


/// Get the socket bound to local port @p port.
///
/// @param      w     Backend engine.
/// @param[in]  port  The port number.
///
/// @return     The w_sock bound to @p port.
///
struct w_sock * get_sock(struct w_engine * const w, const uint16_t port)
{
    struct w_hdr h = {.udp.sport = port};
    struct w_sock s = {.hdr = &h};
    return splay_find(sock, &w->sock, &s);
}


/// Return a spare w_iov from the pool of the given warpcore engine. Needs to be
/// returned to w->iov via sq_insert_head() or sq_concat().
///
/// @param      w     Backend engine.
/// @param[in]  len   The length of each @p buf.
/// @param[in]  off   Additional offset into the buffer.
///
/// @return     Spare w_iov.
///
struct w_iov *
w_alloc_iov(struct w_engine * const w, const uint16_t len, uint16_t off)
{
    struct w_iov * const v = w_alloc_iov_base(w);
    if (likely(v)) {
        v->buf += off;
        v->len = len ? MIN(len, v->len) : v->len;
        ensure(v->len >= off, "offset larger than length");
        v->len -= off;
#ifdef WITH_NETMAP
        v->buf += sizeof(struct w_hdr);
        ensure(v->len >= sizeof(struct w_hdr), "offset larger than headers");
        v->len -= sizeof(struct w_hdr);
#endif
        ASAN_UNPOISON_MEMORY_REGION(IDX2BUF(w, v->idx), v->len + off);
        // warn(DBG, "idx %u base %p buf %p baselen %u off %u len %u", v->idx,
        //      (void *)IDX2BUF(w, v->idx), (void *)v->buf,
        //      (len == 0 ? w->mtu : MIN(w->mtu, len)), off, v->len);
    }
    return v;
}


/// Helper function for w_alloc_size and w_alloc_cnt.
///
/// @param      w         Backend engine.
/// @param[out] q         Tail queue of w_iov structs.
/// @param[in]  count     Number of w_iov structs to allocate.
/// @param[in]  len       The length of each @p buf.
/// @param[in]  off       Additional offset for each buffer.
/// @param[in]  adj_last  Amount to reduce the length of the last w_iov by.
///
static inline void alloc_cnt(struct w_engine * const w,
                             struct w_iov_sq * const q,
                             const uint32_t count,
                             const uint16_t len,
                             uint16_t off,
                             const uint16_t adj_last)
{
    sq_init(q);
    struct w_iov * v = 0;
#ifndef NDEBUG
    uint32_t qlen = 0;
#endif
    for (uint32_t i = 0; likely(i < count); i++) {
        v = w_alloc_iov(w, len, off);
        if (unlikely(v == 0)) {
            // free partial allocation and return
            w_free(w, q);
            warn(CRT, "ran out of w_iov bufs trying to allocate %u", count);
            return;
        }
        sq_insert_tail(q, v, next);
#ifndef NDEBUG
        qlen += v->len;
#endif
    }
    if (v) {
        v->len -= adj_last;
#ifndef NDEBUG
        qlen -= adj_last;
#endif
    }
    warn(DBG, "allocated w_iov_sq of len %u byte%s (%d w_iov%s w/off %u)", qlen,
         plural(qlen), count, plural(count), off);
}


/// Allocate a w_iov tail queue for @p plen payload bytes, for eventual use with
/// w_tx(). The tail queue must be later returned to warpcore w_free(). If a @p
/// len length is specified, limit the length of each buffer to the minimum of
/// the MTU and this value. If a @p off offset is specified, leave this much
/// extra space before @p buf in each w_iov. This is meant for upper-level
/// protocols that wish to reserve space for their headers.
///
/// @param      w     Backend engine.
/// @param[out] q     Tail queue of w_iov structs.
/// @param[in]  plen  Amount of payload bytes in the returned tail queue.
/// @param[in]  len   The length of each @p buf.
/// @param[in]  off   Additional offset for @p buf.
///
void w_alloc_len(struct w_engine * const w,
                 struct w_iov_sq * const q,
                 const uint32_t plen,
                 const uint16_t len,
                 const uint16_t off)
{
    const uint32_t space = (len == 0 ? w->mtu : MIN(w->mtu, len)) - off
#ifdef WITH_NETMAP
                           - sizeof(struct w_hdr)
#endif
        ;
    const uint32_t count = plen / space + (plen % space != 0);
    alloc_cnt(w, q, count, len, off, (uint16_t)(space * count - plen));
}


/// Allocate a w_iov tail queue of @p count packets, for eventual use with
/// w_tx(). The tail queue must be later returned to warpcore w_free(). If a @p
/// len length is specified, limit the length of each buffer to the minimum of
/// the MTU and this value. If a @p off offset is specified, leave this much
/// extra space before @p buf in each w_iov. This is meant for upper-level
/// protocols that wish to reserve space for their headers.
///
/// @param      w      Backend engine.
/// @param[out] q      Tail queue of w_iov structs.
/// @param[in]  count  Number of packets in the returned tail queue.
/// @param[in]  len    The length of each @p buf.
/// @param[in]  off    Additional offset for @p buf.
///
void w_alloc_cnt(struct w_engine * const w,
                 struct w_iov_sq * const q,
                 const uint32_t count,
                 const uint16_t len,
                 const uint16_t off)
{
    alloc_cnt(w, q, count,
              len
#ifdef WITH_NETMAP
                  + sizeof(struct w_hdr)
#endif
                  ,
              off, 0);
}


/// Return the total payload length of w_iov tail queue @p c.
///
/// @param[in]  q     The w_iov tail queue to compute the payload length of.
///
/// @return     Sum of the payload lengths of the w_iov structs in @p q.
///
uint32_t w_iov_sq_len(const struct w_iov_sq * const q)
{
    uint32_t l = 0;
    const struct w_iov * v;
    sq_foreach (v, q, next)
        l += v->len;
    return l;
}


/// Connect a bound socket to a remote IP address and port. Depending on the
/// backend, this function may block until a MAC address has been resolved with
/// ARP.
///
/// Calling w_connect() will make subsequent w_tx() operations on the w_sock
/// enqueue payload data towards that destination. Unlike with the Socket API,
/// w_connect() can be called several times, which will re-bind a connected
/// w_sock and allows a server application to send data to multiple peers over a
/// w_sock.
///
/// @param      s     w_sock to connect.
/// @param[in]  ip    Destination IPv4 address to bind to.
/// @param[in]  port  Destination UDP port to bind to.
///
void w_connect(struct w_sock * const s, const uint32_t ip, const uint16_t port)
{
    s->hdr->ip.dst = ip;
    s->hdr->udp.dport = port;
    backend_connect(s);

#if !defined(NDEBUG) && DLEVEL >= NTE
    char str[INET_ADDRSTRLEN];
    warn(NTE, "socket connected to %s port %d",
         inet_ntop(AF_INET, &ip, str, INET_ADDRSTRLEN), ntohs(port));
#endif
}


void w_disconnect(struct w_sock * const s)
{
    s->hdr->ip.dst = 0;
    s->hdr->udp.dport = 0;

    warn(NTE, "socket disconnected");
}


/// Bind a w_sock to the given local UDP port number.
///
/// @param      w      The w_sock to bind.
/// @param[in]  port   The local port number to bind to, in network byte order.
///                    If port is zero, a random local port will be chosen.
/// @param[in]  flags  Flags for this socket.
///
/// @return     Pointer to a bound w_sock.
///
struct w_sock *
w_bind(struct w_engine * const w, const uint16_t port, const uint8_t flags)
{
    struct w_sock * s = get_sock(w, port);
    if (unlikely(s)) {
        warn(WRN, "UDP source port %d already in bound", ntohs(port));
        return 0;
    }

    ensure((s = calloc(1, sizeof(*s))) != 0, "cannot allocate w_sock");
    ensure((s->hdr = calloc(1, sizeof(*s->hdr))) != 0, "cannot allocate w_hdr");

    // initialize flags
    s->flags = flags;

    // initialize the non-zero fields of outgoing template header
    s->hdr->eth.type = ETH_TYPE_IP;
    s->hdr->eth.src = w->mac;
    // s->hdr->eth.dst is set on w_connect()

    ip_hdr_init(&s->hdr->ip);
    s->hdr->ip.src = w->ip;
    s->hdr->udp.sport = port;
    // s->hdr->ip.dst is set on w_connect()

    s->w = w;
    splay_insert(sock, &w->sock, s);
    sq_init(&s->iv);

    backend_bind(s);

    warn(NTE, "socket bound to port %d", ntohs(s->hdr->udp.sport));

    return s;
}


/// Close a warpcore socket. This dequeues all data from w_sock::iv.
///
/// @param      s     w_sock to close.
///
void w_close(struct w_sock * const s)
{
    backend_close(s);

    // make iovs of the socket available again
    w_free(s->w, &s->iv);

    // remove the socket from list of sockets
    splay_remove(sock, &s->w->sock, s);

    // free the template header
    free(s->hdr);

    // free the socket
    free(s);
}


/// Shut a warpcore engine down cleanly. In addition to calling into the
/// backend-specific cleanup function, it frees up the extra buffers and other
/// memory structures.
///
/// @param      w     Backend engine.
///
void w_cleanup(struct w_engine * const w)
{
    warn(NTE, "warpcore shutting down");

    // close all sockets
    struct w_sock *s, *tmp;
    for (s = splay_min(sock, &w->sock); s != 0; s = tmp) {
        tmp = splay_next(sock, &w->sock, s);
        w_close(s);
    }

    backend_cleanup(w);
    sl_remove(&engines, w, w_engine, next);
    free(w->ifname);
    free(w->b);
    free(w);
}


/// Initialize a warpcore engine on the given interface. Ethernet and IPv4
/// source addresses and related information, such as the netmask, are taken
/// from the active OS configuration of the interface. A default router,
/// however, needs to be specified with @p rip, if communication over a WAN is
/// desired. @p nbufs controls how many packet buffers the engine will attempt
/// to allocate.
///
/// Since warpcore relies on random() to generate random values, the caller
/// should also set an initial seed with srandom() or srandomdev(). Warpcore
/// does not do this, to allow the application control over the seed.
///
/// @param[in]  ifname  The OS name of the interface (e.g., "eth0").
/// @param[in]  rip     The default router to be used for non-local
///                     destinations. Can be zero.
/// @param[in]  nbufs   Number of packet buffers to allocate.
///
/// @return     Initialized warpcore engine.
///
struct w_engine *
w_init(const char * const ifname, const uint32_t rip, const uint32_t nbufs)
{
    // allocate engine struct
    struct w_engine * w;
    ensure((w = calloc(1, sizeof(*w))) != 0, "cannot allocate struct w_engine");

    // initialize lists of sockets and iovs
    splay_init(&w->sock);
    sq_init(&w->iov);

    // construct interface name of a netmap pipe for this interface
    char pipe[IFNAMSIZ];
    snprintf(pipe, IFNAMSIZ, "warp-%s", ifname);

    // get interface config
    struct ifaddrs * ifap;
    ensure(getifaddrs(&ifap) != -1, "%s: cannot get interface info", ifname);

    // we mostly loop here because the link may be down
    bool link_up = false, is_loopback = false, have_pipe = false;
    while (link_up == false || w->mtu == 0 || w->ip == 0 || w->mask == 0) {
        struct ifaddrs * i;
        for (i = ifap; i; i = i->ifa_next) {
            if (strcmp(i->ifa_name, pipe) == 0)
                have_pipe = true;

            if (strcmp(i->ifa_name, ifname) != 0)
                continue;

            is_loopback = i->ifa_flags & IFF_LOOPBACK;

            switch (i->ifa_addr->sa_family) {
            case AF_LINK:
                plat_get_mac(&w->mac, i);
                w->mtu = plat_get_mtu(i);
                link_up = plat_get_link(i);
#if !defined(NDEBUG) && DLEVEL >= NTE
                // mpbs can be zero on generic platforms and loopback interfaces
                const uint32_t mbps = plat_get_mbps(i);
                warn(NTE, "%s addr %s, MTU %d, speed %uG, link %s", i->ifa_name,
                     ether_ntoa(&w->mac), w->mtu, mbps / 1000,
                     link_up ? "up" : "down");
#endif
                break;
            case AF_INET:
                // get IP addr and netmask
                if (w->ip == 0) {
                    w->ip = ((struct sockaddr_in *)(void *)i->ifa_addr)
                                ->sin_addr.s_addr;
                    w->mask = ((struct sockaddr_in *)(void *)i->ifa_netmask)
                                  ->sin_addr.s_addr;
                }
                break;
            case AF_INET6:
                break;
            default:
                warn(NTE, "ignoring unknown addr family %d on %s",
                     i->ifa_addr->sa_family, i->ifa_name);
                break;
            }
        }
        // ensure(i, "unknown interface %s", ifname);

        if (link_up == false || w->mtu == 0 || w->ip == 0 || w->mask == 0) {
            // sleep for a bit, so we don't burn the CPU when link is down
            warn(WRN,
                 "%s: could not obtain required interface "
                 "information, retrying",
                 ifname);
            sleep(1);
        }
    }
    freeifaddrs(ifap);

#if !defined(NDEBUG) && DLEVEL >= NTE
    char ip_str[INET_ADDRSTRLEN];
    char mask_str[INET_ADDRSTRLEN];
    char rip_str[INET_ADDRSTRLEN];
    warn(NTE, "%s has IP addr %s/%s%s%s", ifname,
         inet_ntop(AF_INET, &w->ip, ip_str, INET_ADDRSTRLEN),
         inet_ntop(AF_INET, &w->mask, mask_str, INET_ADDRSTRLEN),
         rip ? ", router " : "",
         rip ? inet_ntop(AF_INET, &w->rip, rip_str, INET_ADDRSTRLEN) : "");
#endif

    w->ifname = strndup(ifname, IFNAMSIZ);
    ensure(w->ifname, "could not strndup");

    // set the IP address of our default router
    w->rip = rip;

    // loopback interfaces can have huge MTUs, so cap to something more sensible
    w->mtu = MIN(w->mtu, (uint16_t)getpagesize() / 2);

    // backend-specific init
    w->b = calloc(1, sizeof(*w->b));
    ensure(w->b, "cannot alloc backend");
    w->max_buf_idx = backend_init(w, nbufs, is_loopback, !have_pipe);

    // store the initialized engine in our global list
    sl_insert_head(&engines, w, next);

    warn(INF, "%s/%s %s using %u %u-byte buffers on %s", warpcore_name,
         w->backend_name, warpcore_version, sq_len(&w->iov), w->mtu, w->ifname);
    return w;
}


/// Return the maximum size a given w_iov may have for the given engine.
/// Basically, subtracts the header space and any offset specified when
/// allocating the w_iov from the MTU.
///
/// @param[in]  w     Backend engine.
/// @param[in]  v     The w_iov in question.
///
/// @return     Maximum length of the data in a w_iov for this engine.
///
uint16_t w_iov_max_len(const struct w_engine * const w,
                       const struct w_iov * const v)
{
    const uint16_t offset = (const uint16_t)(
        (const uint8_t *)v->buf - (const uint8_t *)IDX2BUF(w, v->idx));
    return w->mtu - offset;
}


/// Return whether a socket is connected (i.e., w_connect() has been called on
/// it) or not.
///
/// @param[in]  s     Connection.
///
/// @return     True when connected, zero otherwise.
///
bool w_connected(const struct w_sock * const s)
{
    return s->hdr->ip.dst;
}

#ifndef NDEBUG

void w_free(struct w_engine * const w, struct w_iov_sq * const q)
{
    sq_concat(&w->iov, q);
    struct w_iov * v;
    sq_foreach (v, q, next)
        ASAN_POISON_MEMORY_REGION(IDX2BUF(w, v->idx), w->mtu);
}


void w_free_iov(struct w_engine * const w, struct w_iov * const v)
{
    sq_insert_head(&w->iov, v, next);
    ASAN_POISON_MEMORY_REGION(IDX2BUF(w, v->idx), w->mtu);
}

#endif
