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

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef FUZZING
#include <sys/time.h>
#endif

#define klib_unused

#ifndef PARTICLE
#include <ifaddrs.h>
#include <krng.h>
#include <net/if.h>
#else
#include <rng_hal.h>

#define kr_srand_r(x, y)
#define kr_rand_r(x)                                                           \
    (uint64_t)(HAL_RNG_GetRandomNumber()) << 32 | HAL_RNG_GetRandomNumber()
#endif

#include <khash.h>
#include <warpcore/warpcore.h>

#ifdef HAVE_ASAN
#include <sanitizer/asan_interface.h>
#endif

#include "backend.h"

#ifdef WITH_NETMAP
#include "eth.h"
#include "ip.h"
#include "udp.h"
#endif

#ifdef PARTICLE
typedef struct if_list if_list;

#include <ifapi.h>
#include <system_network.h>

#define getifaddrs if_get_if_addrs
#define freeifaddrs if_free_if_addrs
#define ifa_next next
#define ifa_name ifname
#define ifa_flags ifflags
#define ifa_addr if_addr->addr
#define ifa_netmask if_addr->netmask

#include <spark_wiring_ticks.h>
#define sleep(sec) delay((sec)*MSECS_PER_SEC)
#endif


#ifndef PARTICLE
// w_init() must initialize this so that it is not all zero
static krng_t w_rand_state;
#endif

/// A global list of netmap engines that have been initialized for different
/// interfaces.
///
struct w_engines engines = sl_head_initializer(engines);


/// Get the socket bound to the given four-tuple <source IP, source port,
/// destination IP, destination port>.
///
/// @param      w      Backend engine.
/// @param[in]  sip    The source IP address.
/// @param[in]  sport  The source port.
/// @param[in]  dip    The destination IP address.
/// @param[in]  dport  The destination port.
///
/// @return     The w_sock bound to the given four-tuple.
///
struct w_sock * w_get_sock(struct w_engine * const w,
                           const uint32_t sip,
                           const uint16_t sport,
                           const uint32_t dip,
                           const uint16_t dport)
{
    khash_t(sock) * const sock = w->sock;
    const khiter_t k =
        kh_get(sock, sock,
               (&(struct w_tuple){
                   .sip = sip, .dip = dip, .sport = sport, .dport = dport}));
    return unlikely(k == kh_end(sock)) ? 0 : kh_val(sock, k);
}


static void __attribute__((nonnull))
ins_sock(struct w_engine * const w, struct w_sock * const s)
{
    khash_t(sock) * const sock = w->sock;
    int ret;
    const khiter_t k = kh_put(sock, sock, &s->tup, &ret);
    ensure(ret >= 1, "inserted is %d", ret);
    kh_val(sock, k) = s;
}


static void __attribute__((nonnull))
rem_sock(struct w_engine * const w, struct w_sock * const s)
{
    khash_t(sock) * const sock = w->sock;
    const khiter_t k = kh_get(sock, sock, &s->tup);
    ensure(k != kh_end(sock), "found");
    kh_del(sock, sock, k);
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
#ifdef WITH_NETMAP
        const size_t diff = sizeof(struct eth_hdr) + sizeof(struct ip_hdr) +
                            sizeof(struct udp_hdr);
        v->buf += diff;
        v->len -= diff;
#endif
        ensure(off == 0 || off <= v->len, "off %u > v->len %u", off, v->len);
        v->buf += off;
        v->len -= off;
        ensure(len <= v->len, "len %u > v->len %u", len, v->len);
        v->len = len ? len : v->len;
        // warn(DBG, "alloc w_iov off %u len %u", v->buf - v->base, v->len);
    }
    return v;
}


/// Allocate a w_iov tail queue for @p plen payload bytes, for eventual use with
/// w_tx(). The tail queue must be later returned to warpcore w_free(). If a @p
/// len length is specified, limit the length of each buffer to the minimum of
/// the MTU and this value. If a @p off offset is specified, leave this much
/// extra space before @p buf in each w_iov. This is meant for upper-level
/// protocols that wish to reserve space for their headers.
///
/// If there aren't enough buffers available to fulfill the request, @p q will
/// be shorter than requested. It is up to the caller to check this.
///
/// @param      w     Backend engine.
/// @param[out] q     Tail queue of w_iov structs.
/// @param[in]  qlen  Amount of payload bytes in the returned tail queue.
/// @param[in]  len   The length of each @p buf.
/// @param[in]  off   Additional offset for @p buf.
///
void w_alloc_len(struct w_engine * const w,
                 struct w_iov_sq * const q,
                 const uint64_t qlen,
                 const uint16_t len,
                 const uint16_t off)
{
    uint64_t needed = qlen;
    while (likely(needed)) {
        struct w_iov * const v = w_alloc_iov(w, len, off);
        if (unlikely(v == 0))
            return;
        if (likely(needed > v->len))
            needed -= v->len;
        else {
            // warn(DBG, "adjust last to %u", needed);
            v->len = (uint16_t)needed;
            needed = 0;
        }
        sq_insert_tail(q, v, next);
    }
}


/// Allocate a w_iov tail queue of @p count packets, for eventual use with
/// w_tx(). The tail queue must be later returned to warpcore w_free(). If a @p
/// len length is specified, limit the length of each buffer to the minimum of
/// the MTU and this value. If a @p off offset is specified, leave this much
/// extra space before @p buf in each w_iov. This is meant for upper-level
/// protocols that wish to reserve space for their headers.
///
/// If there aren't enough buffers available to fulfill the request, @p q will
/// be shorter than requested. It is up to the caller to check this.
///
/// @param      w      Backend engine.
/// @param[out] q      Tail queue of w_iov structs.
/// @param[in]  count  Number of packets in the returned tail queue.
/// @param[in]  len    The length of each @p buf.
/// @param[in]  off    Additional offset for @p buf.
///
void w_alloc_cnt(struct w_engine * const w,
                 struct w_iov_sq * const q,
                 const uint64_t count,
                 const uint16_t len,
                 const uint16_t off)
{
    for (uint64_t needed = 0; likely(needed < count); needed++) {
        struct w_iov * const v = w_alloc_iov(w, len, off);
        if (unlikely(v == 0))
            return;
        sq_insert_tail(q, v, next);
    }
}


/// Return the total payload length of w_iov tail queue @p c.
///
/// @param[in]  q     The w_iov tail queue to compute the payload length of.
///
/// @return     Sum of the payload lengths of the w_iov structs in @p q.
///
uint64_t w_iov_sq_len(const struct w_iov_sq * const q)
{
    uint64_t l = 0;
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
/// enqueue payload data towards that destination.
///
/// @param      s     w_sock to connect.
/// @param[in]  peer  The peer to connect to.
///
/// @return     Zero on success, @p errno otherwise.
///
int w_connect(struct w_sock * const s, const struct sockaddr * const peer)
{
    if (unlikely(s->tup.dip || s->tup.dport)) {
        warn(ERR, "socket already connected");
        return EADDRINUSE;
    }

    if (unlikely(peer->sa_family != AF_INET)) {
        warn(ERR, "peer address is not IPv4");
        return EAFNOSUPPORT;
    }

    rem_sock(s->w, s);
    const struct sockaddr_in * const addr4 =
        (const struct sockaddr_in *)(const void *)peer;
    s->tup.dip = addr4->sin_addr.s_addr;
    s->tup.dport = addr4->sin_port;
    const int e = backend_connect(s);
    if (unlikely(e)) {
        s->tup.dip = s->tup.dport = 0;
        ins_sock(s->w, s);
        return e;
    }
    ins_sock(s->w, s);

#ifndef NDEBUG
    char str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &s->tup.dip, str, INET_ADDRSTRLEN);
    warn(DBG, "socket connected to %s port %d", str, bswap16(s->tup.dport));
#endif

    return 0;
}


/// Bind a w_sock to the given local UDP port number.
///
/// @param      w     The w_sock to bind.
/// @param[in]  port  The local port number to bind to, in network byte order.
///                   If port is zero, a random local port will be chosen.
/// @param[in]  opt   Socket options for this socket. Can be zero.
///
/// @return     Pointer to a bound w_sock.
///
struct w_sock * w_bind(struct w_engine * const w,
                       const uint16_t port,
                       const struct w_sockopt * const opt)
{
    struct w_sock * s = w_get_sock(w, w->ip, port, 0, 0);
    if (unlikely(s)) {
        warn(INF, "UDP source port %d already in bound", bswap16(port));
        // do not free, just return
        return 0;
    }

    if (unlikely(s = calloc(1, sizeof(*s))) == 0)
        goto fail;

    s->tup.sip = w->ip;
    s->tup.sport = port;
    // s->tup.dip and s->tup.dport are set on w_connect()
    s->w = w;
    sq_init(&s->iv);

    if (unlikely(backend_bind(s, opt) != 0))
        goto fail;

#ifndef FUZZING
    warn(NTE, "socket bound to port %d", bswap16(s->tup.sport));
#endif

    ins_sock(w, s);
    return s;

fail:
    if (s)
        free(s);
    return 0;
}


/// Close a warpcore socket.
///
/// @param      s     w_sock to close.
///
void w_close(struct w_sock * const s)
{
    backend_close(s);

    // remove the socket from list of sockets
    rem_sock(s->w, s);

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
    struct w_sock * s;
    kh_foreach_value((khash_t(sock) *)w->sock, s, { w_close(s); });
    kh_destroy(sock, (khash_t(sock) *)w->sock);

    backend_cleanup(w);
    sl_remove(&engines, w, w_engine, next);
    free(w->ifname);
    free(w->drvname);
    free(w->b);
    free(w);
}


/// Init state for w_rand() and w_rand_uniform(). This **MUST** be called once
/// prior to calling any of these functions!
///
void w_init_rand(void)
{
    // init state for w_rand()
#if !defined(FUZZING) && !defined(PARTICLE)
    struct timeval now;
    gettimeofday(&now, 0);
    const uint64_t seed = fnv1a_64(&now, sizeof(now));
    kr_srand_r(&w_rand_state, seed);
#else
    kr_srand_r(&w_rand_state, 0);
#endif
}


/// Initialize a warpcore engine on the given interface. Ethernet and IPv4
/// source addresses and related information, such as the netmask, are taken
/// from the active OS configuration of the interface. A default router,
/// however, needs to be specified with @p rip, if communication over a WAN is
/// desired. @p nbufs controls how many packet buffers the engine will attempt
/// to allocate.
///
/// @param[in]  ifname  The OS name of the interface (e.g., "eth0").
/// @param[in]  rip     The default router to be used for non-local
///                     destinations. Can be zero.
/// @param[in]  nbufs   Number of extra packet buffers to allocate.
///
/// @return     Initialized warpcore engine.
///
struct w_engine *
w_init(const char * const ifname, const uint32_t rip, const uint64_t nbufs)
{
#ifdef PARTICLE
    warn(WRN, "no I/O support for 64-bit types - expect log weirdness");
#endif

    w_init_rand();

    // allocate engine struct
    struct w_engine * w;
    ensure((w = calloc(1, sizeof(*w))) != 0, "cannot allocate struct w_engine");

    // initialize lists of sockets and iovs
    w->sock = kh_init(sock);
    sq_init(&w->iov);

#ifndef PARTICLE
    // construct interface name of a netmap pipe for this interface
    char pipe[IFNAMSIZ];
    snprintf(pipe, IFNAMSIZ, "warp-%s", ifname);
#endif

    // we mostly loop here because the link may be down
    bool link_up = false;
    bool is_loopback = false;
    bool have_pipe = false;
    while (link_up == false || w->mtu == 0 || w->ip == 0 || w->mask == 0 ||
           w->mbps == 0) {

        // get interface config
        struct ifaddrs * ifap;
        ensure(getifaddrs(&ifap) != -1, "%s: cannot get interface info",
               ifname);

        struct ifaddrs * i;
        for (i = ifap; i; i = i->ifa_next) {
#ifndef PARTICLE
            if (strcmp(i->ifa_name, pipe) == 0)
                have_pipe = true;
#endif
            if (strcmp(i->ifa_name, ifname) != 0)
                continue;

            is_loopback = i->ifa_flags & IFF_LOOPBACK;

            switch (i->ifa_addr->sa_family) {
            case AF_LINK:
                plat_get_mac(&w->mac, i);
                w->mtu = plat_get_mtu(i);
                link_up = plat_get_link(i);
                // mpbs can be zero on generic platforms and loopback interfaces
                w->mbps = plat_get_mbps(i);
                char drvname[32];
                plat_get_iface_driver(i, drvname, sizeof(drvname));
                w->drvname = strdup(drvname);
#ifndef NDEBUG
                char mac[ETH_ADDR_STRLEN];
                ether_ntoa_r(&w->mac, mac);
                warn(NTE, "%s addr %s, MTU %d, speed %" PRIu32 "G, link %s",
                     i->ifa_name, mac, w->mtu, w->mbps / 1000,
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

        if (link_up == false || w->mtu == 0 || w->ip == 0 || w->mask == 0 ||
            w->mbps == 0) {
            // sleep for a bit, so we don't burn the CPU when link is down
            warn(WRN,
                 "%s: could not obtain required interface "
                 "information, retrying",
                 ifname);
            sleep(1);
        }
        freeifaddrs(ifap);
    }

#ifndef NDEBUG
    char ip_str[INET_ADDRSTRLEN];
    char mask_str[INET_ADDRSTRLEN];
    char rip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &w->ip, ip_str, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &w->mask, mask_str, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &w->rip, rip_str, INET_ADDRSTRLEN);
    warn(NTE, "%s has IP addr %s/%s%s%s", ifname, ip_str, mask_str,
         rip ? ", router " : "", rip ? rip_str : "");
#endif

    w->ifname = strndup(ifname, IFNAMSIZ);
    ensure(w->ifname, "could not strndup");

    // set the IP address of our default router
    w->rip = rip;

#ifndef PARTICLE
    // some interfaces can have huge MTUs, so cap to something more sensible
    w->mtu = MIN(w->mtu, (uint16_t)getpagesize() / 2);
#endif

    // backend-specific init
    w->b = calloc(1, sizeof(*w->b));
    ensure(w->b, "cannot alloc backend");
    ensure(nbufs <= UINT32_MAX, "too many nbufs %" PRIu64, nbufs);
    backend_init(w, (uint32_t)nbufs, is_loopback, !have_pipe);

    // store the initialized engine in our global list
    sl_insert_head(&engines, w, next);

    warn(INF, "%s/%s %s using %" PRIu64 " %u-byte bufs on %s", warpcore_name,
         w->backend_name, warpcore_version, sq_len(&w->iov), w->mtu, w->ifname);
    return w;
}


/// Return the maximum size a given w_iov may have for the given engine.
/// Basically, subtracts the header space and any offset specified when
/// allocating the w_iov from the MTU.
///
/// @param[in]  v     The w_iov in question.
///
/// @return     Maximum length of the data in a w_iov for this engine.
///
uint16_t w_iov_max_len(const struct w_iov * const v)
{
    const uint16_t offset = (const uint16_t)(v->buf - v->base);
    return v->w->mtu - offset;
}


/// Return a w_iov tail queue obtained via w_alloc_len(), w_alloc_cnt() or
/// w_rx() back to warpcore.
///
/// @param      q     Tail queue of w_iov structs to return.
///
void w_free(struct w_iov_sq * const q)
{
    if (unlikely(sq_empty(q)))
        return;
    struct w_engine * const w = sq_first(q)->w;
    sq_concat(&w->iov, q);
#ifndef NDEBUG
    struct w_iov * v;
    sq_foreach (v, q, next)
        ASAN_POISON_MEMORY_REGION(v->base, w->mtu);
#endif
}


/// Return a single w_iov obtained via w_alloc_len(), w_alloc_cnt() or w_rx()
/// back to warpcore.
///
/// @param      v     w_iov struct to return.
///
void w_free_iov(struct w_iov * const v)
{
    sq_insert_head(&v->w->iov, v, next);
    ASAN_POISON_MEMORY_REGION(v->base, v->w->mtu);
}


/// Return a 64-bit random number. Fast, but not cryptographically secure.
/// Implements xoroshiro128+; see https://en.wikipedia.org/wiki/Xoroshiro128%2B.
///
/// @return     Random number.
///
uint64_t w_rand64(void)
{
    return kr_rand_r(&w_rand_state);
}


/// Return a 32-bit random number. Fast, but not cryptographically secure.
/// Truncates w_rand64() to 32 bits.
///
/// @return     Random number.
///
uint32_t w_rand32(void)
{
#ifndef PARTICLE
    return (uint32_t)kr_rand_r(&w_rand_state);
#else
    return HAL_RNG_GetRandomNumber();
#endif
}


/// Calculate a uniformly distributed random number in [0, upper_bound) avoiding
/// "modulo bias".
///
/// @param[in]  upper_bound  The upper bound
///
/// @return     Random number.
///
uint64_t w_rand_uniform64(const uint64_t upper_bound)
{
    if (unlikely(upper_bound < 2))
        return 0;

    // 2**64 % x == (2**64 - x) % x
    const uint64_t min = (UINT64_MAX - upper_bound) % upper_bound;

    // This could theoretically loop forever but each retry has p > 0.5 (worst
    // case, usually far better) of selecting a number inside the range we
    // need, so it should rarely need to re-roll.
    uint64_t r;
    for (;;) {
        r = kr_rand_r(&w_rand_state);
        if (likely(r >= min))
            break;
    }

    return r % upper_bound;
}


/// Calculate a uniformly distributed random number in [0, upper_bound) avoiding
/// "modulo bias". This can be faster on platforms with crappy 64-bit math.
///
/// @param[in]  upper_bound  The upper bound
///
/// @return     Random number.
///
uint32_t w_rand_uniform32(const uint32_t upper_bound)
{
    if (unlikely(upper_bound < 2))
        return 0;

    // 2**32 % x == (2**32 - x) % x
    const uint32_t min = (UINT32_MAX - upper_bound) % upper_bound;

    // This could theoretically loop forever but each retry has p > 0.5 (worst
    // case, usually far better) of selecting a number inside the range we
    // need, so it should rarely need to re-roll.
    uint32_t r;
    for (;;) {
        r = w_rand32();
        if (likely(r >= min))
            break;
    }

    return r % upper_bound;
}


/// Return the local or peer IPv4 address and port for a w_sock.
///
/// @param[in]  s      Pointer to w_sock.
/// @param[in]  local  If true, return local IPv4 and port, else the peer's.
///
/// @return     Local or remote IPv4 address and port, or zero if unbound or
/// disconnected.
///
const struct sockaddr * w_get_addr(const struct w_sock * const s,
                                   const bool local)
{
    if ((local && s->tup.sip == 0) || (!local && s->tup.dip == 0))
        return 0;

    static struct sockaddr_storage addr = {.ss_family = AF_INET};
    struct sockaddr_in * const sin = (struct sockaddr_in *)&addr;
    sin->sin_port = local ? s->tup.sport : s->tup.dport;
    sin->sin_addr.s_addr = local ? s->tup.sip : s->tup.dip;
    return (struct sockaddr *)&addr;
}


void init_iov(struct w_engine * const w, struct w_iov * const v)
{
    v->w = w;
    if (unlikely(v->base == 0))
        v->base = idx_to_buf(w, v->idx);
    v->buf = v->base;
    v->len = w->mtu;
    v->o = 0;
    sq_next(v, next) = 0;
}


struct w_iov * w_alloc_iov_base(struct w_engine * const w)
{
    struct w_iov * const v = sq_first(&w->iov);
    if (likely(v)) {
        sq_remove_head(&w->iov, next);
        init_iov(w, v);
        ASAN_UNPOISON_MEMORY_REGION(v->base, w->mtu);
    }
    return v;
}
