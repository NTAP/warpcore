// SPDX-License-Identifier: BSD-2-Clause
//
// Copyright (c) 2014-2020, NetApp, Inc.
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
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#ifndef NDEBUG
#include <inttypes.h>
#endif

#include <warpcore/warpcore.h>

#ifdef HAVE_ASAN
#include <sanitizer/asan_interface.h>
#endif

// #define DEBUG_BUFFERS

#ifdef DEBUG_BUFFERS
#include <stdio.h>
#endif

#include "backend.h"
#include "ifaddr.h"
#include "ip6.h"
#include "neighbor.h"


#if !defined(PARTICLE) && !defined(RIOT_VERSION)
#include <net/if.h>

/// A global list of netmap engines that have been initialized for different
/// interfaces.
///
static sl_head(w_engines, w_engine) engines = sl_head_initializer(engines);
#else
#define strerror(...) ""
#endif


#if defined(DEBUG_BUFFERS) && !defined(NDEBUG)
static void __attribute__((nonnull))
dump_bufs(const char * const label, const struct w_iov_sq * const q)
{
    char line[400] = "";
    int pos = 0;
    struct w_iov * v;
    uint32_t cnt = 0;
    sq_foreach (v, q, next) {
        cnt++;
        pos += snprintf(&line[pos], sizeof(line) - (size_t)pos, "%s%" PRIu32,
                        pos ? ", " : "", v->idx);
        if ((size_t)pos >= sizeof(line))
            break;
    }
    warn(DBG, "%s: %" PRIu " bufs: %s", label, w_iov_sq_cnt(q), line);
    ensure((size_t)pos >= sizeof(line) || cnt == w_iov_sq_cnt(q),
           "cnt mismatch");
}
#else
#define dump_bufs(...)
#endif


/// Return a spare w_iov from the pool of the given warpcore engine. Needs to be
/// returned to w->iov via sq_insert_head() or sq_concat().
///
/// @param      w     Backend engine.
/// @param[in]  af    Address family to allocate packet buffers.
/// @param[in]  len   The length of each @p buf.
/// @param[in]  off   Additional offset into the buffer.
///
/// @return     Spare w_iov.
///
struct w_iov * __attribute__((no_instrument_function))
w_alloc_iov(struct w_engine * const w,
            const int af
#if defined(NDEBUG) && !defined(WITH_NETMAP)
            __attribute__((unused))
#endif
            ,
            const uint16_t len,
            const uint16_t off)
{
#ifdef DEBUG_BUFFERS
    warn(DBG, "w_alloc_iov len %u, off %u", len, off);
#endif
    assure(af == AF_INET || af == AF_INET6, "unknown address family");
    struct w_iov * const v = w_alloc_iov_base(w);
    if (likely(v)) {
        const uint16_t hdr_space = iov_off(w, af);
        v->buf += off + hdr_space;
        v->len = len ? len : v->len - (off + hdr_space);
#ifdef DEBUG_BUFFERS
        warn(DBG, "alloc w_iov off %u len %u", (uint16_t)(v->buf - v->base),
             v->len);
#endif
    }
    dump_bufs(__func__, &w->iov);
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
/// @param[in]  af    Address family to allocate packet buffers.
/// @param[out] q     Tail queue of w_iov structs.
/// @param[in]  qlen  Amount of payload bytes in the returned tail queue.
/// @param[in]  len   The length of each @p buf.
/// @param[in]  off   Additional offset for @p buf.
///
void w_alloc_len(struct w_engine * const w,
                 const int af,
                 struct w_iov_sq * const q,
                 const uint_t qlen,
                 const uint16_t len,
                 const uint16_t off)
{
#ifdef DEBUG_BUFFERS
    warn(DBG, "w_alloc_len qlen %" PRIu ", len %u, off %u", qlen, len, off);
    assure(sq_empty(q), "q not empty");
#endif
    uint_t needed = qlen;
    while (likely(needed)) {
        struct w_iov * const v = w_alloc_iov(w, af, len, off);
        if (unlikely(v == 0))
            return;
        if (likely(needed > v->len))
            needed -= v->len;
        else {
#ifdef DEBUG_BUFFERS
            warn(DBG, "adjust last (%u) to %" PRIu, v->idx, needed);
#endif
            v->len = (uint16_t)needed;
            needed = 0;
        }
        sq_insert_tail(q, v, next);
    }
    dump_bufs(__func__, &w->iov);
    dump_bufs("allocated chain", q);
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
/// @param[in]  af     Address family to allocate packet buffers.
/// @param[out] q      Tail queue of w_iov structs.
/// @param[in]  count  Number of packets in the returned tail queue.
/// @param[in]  len    The length of each @p buf.
/// @param[in]  off    Additional offset for @p buf.
///
void w_alloc_cnt(struct w_engine * const w,
                 const int af,
                 struct w_iov_sq * const q,
                 const uint_t count,
                 const uint16_t len,
                 const uint16_t off)
{
#ifdef DEBUG_BUFFERS
    warn(DBG, "w_alloc_cnt count %" PRIu ", len %u, off %u", count, len, off);
    assure(sq_empty(q), "q not empty");
#endif
    for (uint_t needed = 0; likely(needed < count); needed++) {
        struct w_iov * const v = w_alloc_iov(w, af, len, off);
        if (unlikely(v == 0))
            return;
        sq_insert_tail(q, v, next);
    }
    dump_bufs(__func__, &w->iov);
    dump_bufs("allocated chain", q);
}


/// Return the total payload length of w_iov tail queue @p c.
///
/// @param[in]  q     The w_iov tail queue to compute the payload length of.
///
/// @return     Sum of the payload lengths of the w_iov structs in @p q.
///
uint_t w_iov_sq_len(const struct w_iov_sq * const q)
{
    uint_t l = 0;
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
    if (unlikely(w_connected(s))) {
        warn(ERR, "socket already connected");
        return EADDRINUSE;
    }

    backend_preconnect(s);
    int e = 0;
    if (unlikely(w_to_waddr(&s->ws_raddr, peer) == false)) {
        warn(ERR, "peer has unknown address family");
        e = EAFNOSUPPORT;
    } else {
        s->ws_rport = sa_port(peer);
        e = backend_connect(s);
    }

    if (unlikely(e))
        memset(&s->ws_rem, 0, sizeof(s->ws_rem));

    warn(e ? ERR : DBG, "socket %sconnected to %s:%d %s%s%s", e ? "not " : "",
         w_ntop(&s->ws_raddr, ip_tmp), bswap16(s->ws_rport), e ? "(" : "",
         strerror(e), e ? ")" : "");

    return e;
}


/// Bind a w_sock to the given local UDP port number.
///
/// @param      w         The w_sock to bind.
/// @param[in]  addr_idx  Index of the IP address to bind to.
/// @param[in]  port      The local port number to bind to, in network byte
///                       order. If port is zero, a random local port will be
///                       chosen.
/// @param[in]  opt       Socket options for this socket. Can be zero.
///
/// @return     Pointer to a bound w_sock.
///
struct w_sock * w_bind(struct w_engine * const w,
                       const uint16_t addr_idx,
                       const uint16_t port,
                       const struct w_sockopt * const opt)
{
    struct w_sock * const s = calloc(1, sizeof(*s));
    if (unlikely(s == 0))
        goto fail;

    s->ws_loc =
        (struct w_sockaddr){.addr = w->ifaddr[addr_idx].addr, .port = port};
    s->ws_scope = w->ifaddr[addr_idx].scope_id;
    s->w = w;
    sq_init(&s->iv);

    if (unlikely(backend_bind(s, opt) != 0)) {
        warn(ERR, "w_bind failed on %s:%u (%s)", w_ntop(&s->ws_laddr, ip_tmp),
             bswap16(s->ws_lport), strerror(errno));
        goto fail;
    }

    warn(NTE, "socket bound to %s:%d", w_ntop(&s->ws_laddr, ip_tmp),
         bswap16(s->ws_lport));

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
    backend_cleanup(w);
#if !defined(PARTICLE) && !defined(RIOT_VERSION)
    sl_remove(&engines, w, w_engine, next);
#endif
    free(w->b);
    free(w);
}


uint8_t contig_mask_len(const int af, const uint8_t * const mask)
{
    uint8_t mask_len = 0;
    uint8_t i = 0;
    while (i < af_len(af) && mask[i] == 0xff) {
        mask_len += 8;
        i++;
    }

    if (i < af_len(af)) {
        uint8_t val = mask[i];
        while (val) {
            mask_len++;
            val = (uint8_t)(val << 1);
        }
    }
    return mask_len;
}


const char * w_ntop(const struct w_addr * const addr, char * const dst)
{
    // we simply assume that dst is long enough
    return inet_ntop(addr->af,
                     (addr->af == AF_INET ? (const void *)&addr->ip4
                                          : (const void *)addr->ip6),
                     dst, IP_STRLEN);
}


void ip6_config(struct w_ifaddr * const ia, const uint8_t * const mask)
{
    ia->prefix = contig_mask_len(ia->addr.af, mask);

    uint8_t tmp6[IP6_LEN];
    ip6_invert(tmp6, mask);
    ip6_or(ia->bcast6, ia->addr.ip6, tmp6);
    ip6_mk_snma(ia->snma6, ia->addr.ip6);
}


/// Initialize a warpcore engine on the given interface. Ethernet and IP
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
struct w_engine * w_init(const char * const ifname,
                         const uint32_t rip __attribute__((unused)),
                         const uint_t nbufs)
{
#if !defined(PARTICLE) && !defined(RIOT_VERSION)
    struct w_engine * e;
    sl_foreach (e, &engines, next)
        if (strncmp(ifname, e->ifname, IFNAMSIZ) == 0 &&
            e->is_loopback == false) {
            warn(ERR, "can only have one warpcore engine active on %s", ifname);
            return 0;
        }
#endif

    w_init_rand();

    // we mostly loop here because the link may be down
    uint16_t addr_cnt;
    while ((addr_cnt = backend_addr_cnt(ifname)) == 0) {
        // sleep for a bit, so we don't burn the CPU when link is down
        warn(WRN,
             "%s: could not obtain required interface information, retrying",
             ifname);
        w_nanosleep(1 * NS_PER_S);
    }

    // allocate engine struct with room for addresses
    struct w_engine * w;
    ensure((w = calloc(1, sizeof(*w) + addr_cnt * sizeof(w->ifaddr[0]))) != 0,
           "cannot allocate struct w_engine");
    w->addr_cnt = addr_cnt;
    if (*ifname) {
        strncpy(w->ifname, ifname, sizeof(w->ifname));
        w->ifname[sizeof(w->ifname) - 1] = 0;
    }
    sq_init(&w->iov);

    // backend-specific init
    w->b = calloc(1, sizeof(*w->b));
    ensure(w->b, "cannot alloc backend");
    ensure(nbufs <= UINT32_MAX, "too many nbufs %" PRIu, nbufs);
    backend_init(w, (uint32_t)nbufs);

#ifndef NDEBUG
    warn(NTE, "%s MAC addr %s, MTU %d, speed %" PRIu32 "G", w->ifname,
         eth_ntoa(&w->mac, eth_tmp, ETH_STRLEN), w->mtu, w->mbps / 1000);
    for (uint16_t idx = 0; idx < w->addr_cnt; idx++) {
        struct w_ifaddr * const ia = &w->ifaddr[idx];
        warn(NTE, "%s IPv%d addr %s/%u", w->ifname,
             ia->addr.af == AF_INET ? 4 : 6, w_ntop(&ia->addr, ip_tmp),
             ia->prefix);
    }
#endif

#if !defined(PARTICLE) && !defined(RIOT_VERSION)
    // store the initialized engine in our global list
    sl_insert_head(&engines, w, next);
#endif

    warn(INF, "%s/%s (%s) %s using %" PRIu " %u-byte bufs on %s", warpcore_name,
         w->backend_name, w->backend_variant, warpcore_version,
         w_iov_sq_cnt(&w->iov), w->mtu, w->ifname);
    return w;
}


/// Return the maximum IP payload a given w_iov may have for the given IP
/// address family. Basically, subtracts the header space and any offset
/// specified when allocating the w_iov from the MTU.
///
/// @param[in]  v     The w_iov in question.
/// @param[in]  af    IP address family.
///
/// @return     Maximum length of the data in a w_iov for this address
/// family.
///
uint16_t w_max_iov_len(const struct w_iov * const v, const uint16_t af)
{
    const uint16_t offset = (const uint16_t)(v->buf - v->base);
    return v->w->mtu - offset - ip_hdr_len(af);
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
#ifndef NDEBUG
    struct w_iov * v;
    sq_foreach (v, q, next) {
#ifdef DEBUG_BUFFERS
        warn(DBG, "w_free idx %" PRIu32, v->idx);
#endif
        ASAN_POISON_MEMORY_REGION(v->base, max_buf_len(w));
    }
#endif
    sq_concat(&w->iov, q);
    dump_bufs(__func__, &w->iov);
}


/// Return a single w_iov obtained via w_alloc_len(), w_alloc_cnt() or
/// w_rx() back to warpcore.
///
/// @param      v     w_iov struct to return.
///
void __attribute__((no_instrument_function)) w_free_iov(struct w_iov * const v)
{
#ifdef DEBUG_BUFFERS
    warn(DBG, "w_free_iov idx %" PRIu32, v->idx);
#endif
    assure(sq_next(v, next) == 0,
           "idx %" PRIu32 " still linked to idx %" PRIu32, v->idx,
           sq_next(v, next)->idx);
    dump_bufs(__func__, &v->w->iov);
    sq_insert_head(&v->w->iov, v, next);
    ASAN_POISON_MEMORY_REGION(v->base, max_buf_len(v->w));
    dump_bufs(__func__, &v->w->iov);
}


/// Calculate a uniformly distributed random number in [0, upper_bound)
/// avoiding "modulo bias".
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

    // This could theoretically loop forever but each retry has p > 0.5
    // (worst case, usually far better) of selecting a number inside the
    // range we need, so it should rarely need to re-roll.
    uint64_t r;
    for (;;) {
        r = w_rand64();
        if (likely(r >= min))
            break;
    }

    return r % upper_bound;
}


/// Calculate a uniformly distributed random number in [0, upper_bound)
/// avoiding "modulo bias". This can be faster on platforms with crappy
/// 64-bit math.
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

    // This could theoretically loop forever but each retry has p > 0.5
    // (worst case, usually far better) of selecting a number inside the
    // range we need, so it should rarely need to re-roll.
    uint32_t r;
    for (;;) {
        r = w_rand32();
        if (likely(r >= min))
            break;
    }

    return r % upper_bound;
}


static void __attribute__((no_instrument_function, nonnull))
reinit_iov(struct w_iov * const v)
{
    v->buf = v->base;
    v->len = max_buf_len(v->w);
    v->flags = v->ttl = 0;
    sq_next(v, next) = 0;
}


void __attribute__((no_instrument_function))
init_iov(struct w_engine * const w, struct w_iov * const v, const uint32_t idx)
{
    v->w = w;
    v->idx = idx;
    v->base = idx_to_buf(w, v->idx);
    reinit_iov(v);
}


struct w_iov * w_alloc_iov_base(struct w_engine * const w)
{
    struct w_iov * const v = sq_first(&w->iov);
    if (likely(v)) {
        sq_remove_head(&w->iov, next);
        reinit_iov(v);
        ASAN_UNPOISON_MEMORY_REGION(v->base, v->len);
#ifdef DEBUG_BUFFERS
        warn(DBG, "w_alloc_iov_base idx %" PRIu32, v ? v->idx : UINT32_MAX);
#endif
    }
    return v;
}


khint_t
#if defined(__clang__)
    __attribute__((no_sanitize("unsigned-integer-overflow")))
#endif
    w_socktuple_hash(const struct w_socktuple * const tup)
{
    return w_addr_hash(&tup->local.addr) +
           fnv1a_32(&tup->local.port, sizeof(tup->local.port)) +
           (tup->remote.addr.af
                ? (w_addr_hash(&tup->remote.addr) +
                   fnv1a_32(&tup->local.port, sizeof(tup->local.port)))
                : 0);
}


khint_t w_socktuple_cmp(const struct w_socktuple * const a,
                        const struct w_socktuple * const b)
{
    return w_sockaddr_cmp(&a->local, &b->local) &&
           w_sockaddr_cmp(&a->remote, &b->remote);
}


/// Compare two w_addr structs for equality.
///
/// @param[in]  a     First struct.
/// @param[in]  b     Second struct.
///
/// @return     True if equal, false otherwise.
///
bool w_addr_cmp(const struct w_addr * const a, const struct w_addr * const b)
{
    return a->af == b->af &&
           (a->af == AF_INET ? (a->ip4 == b->ip4) : ip6_eql(a->ip6, b->ip6));
}


/// Compare two w_sockaddr structs for equality.
///
/// @param[in]  a     First struct.
/// @param[in]  b     Second struct.
///
/// @return     True if equal, false otherwise.
///
bool w_sockaddr_cmp(const struct w_sockaddr * const a,
                    const struct w_sockaddr * const b)
{
    return a->port == b->port && w_addr_cmp(&a->addr, &b->addr);
}


/// Initialize w_addr @p wa based on sockaddr @p sa.
///
/// @param      wa    The w_addr struct to initialize.
/// @param[in]  sa    The sockaddr struct to initialize based on.
///
/// @return     True if the initialization succeeded.
///
bool w_to_waddr(struct w_addr * const wa, const struct sockaddr * const sa)
{
    if (unlikely(sa->sa_family != AF_INET && sa->sa_family != AF_INET6))
        return false;

    wa->af = sa->sa_family;
    if (wa->af == AF_INET)
        memcpy(&wa->ip4,
               &((const struct sockaddr_in *)(const void *)sa)->sin_addr,
               sizeof(wa->ip4));
    else
        memcpy(wa->ip6,
               &((const struct sockaddr_in6 *)(const void *)sa)->sin6_addr,
               sizeof(wa->ip6));
    return true;
}


void to_sockaddr(struct sockaddr * const sa,
                 const struct w_addr * const addr,
                 const uint16_t port,
                 const uint32_t scope_id)
{
    if (addr->af == AF_INET) {
        struct sockaddr_in * const sin = (struct sockaddr_in *)(void *)sa;
        sin->sin_family = AF_INET;
        sin->sin_port = port;
        memcpy(&sin->sin_addr, &addr->ip4, IP4_LEN);
    } else {
        struct sockaddr_in6 * const sin6 = (struct sockaddr_in6 *)(void *)sa;
        sin6->sin6_family = AF_INET6;
        sin6->sin6_port = port;
        memcpy(&sin6->sin6_addr, addr->ip6, IP6_LEN);
        sin6->sin6_scope_id = scope_id;
    }
}
