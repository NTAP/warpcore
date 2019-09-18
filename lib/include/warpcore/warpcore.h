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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <ifaddrs.h>
#include <netinet/in.h>
#include <stdint.h>
#include <sys/time.h>

#define klib_unused

#include <warpcore/config.h> // IWYU pragma: export
#include <warpcore/khash.h>  // IWYU pragma: export
#include <warpcore/plat.h>   // IWYU pragma: export
#include <warpcore/queue.h>  // IWYU pragma: export
#include <warpcore/util.h>   // IWYU pragma: export

#ifndef PARTICLE
#include <sys/socket.h>
#endif


/// A tail queue of w_iov I/O vectors. Also contains a counter that (on TX)
/// tracks how many w_iovs have not yet been transmitted by the NIC.
///
struct w_iov_sq {
    __extension__ sq_head(, w_iov); ///< Head of the w_iov tail queue.
    uint32_t tx_pending; ///< Counter of untransmitted w_iovs. Only valid on TX.
};


/// Initializer for struct w_iov_sq.
///
/// @param      q     A struct w_iov_sq.
///
/// @return     Empty w_iov_sq, to be assigned to @p q.
///
#define w_iov_sq_initializer(q)                                                \
    {                                                                          \
        sq_head_initializer(q), 0                                              \
    }


#define af_len(x) ((x) == AF_INET ? 32 / 8 : 128 / 8)

struct w_addr {
    union {
        uint32_t ip4;
        uint128_t ip6;
    };
    uint8_t af; ///< Address family.
};

struct w_ifaddr {
    struct w_addr addr;
    uint8_t prefix; ///< Prefix length.
};

struct w_sockaddr {
    struct w_addr addr;
    uint16_t port;
};


// #define sa_set_port(s, p)                                                      \
//     do {                                                                       \
//         _Pragma("clang diagnostic push")                                       \
//             _Pragma("clang diagnostic ignored \"-Wcast-align\"") if (          \
//                 ((struct sockaddr *)(s))->sa_family ==                         \
//                 AF_INET)((struct sockaddr_in *)(s))                            \
//                 ->sin_port = (p);                                              \
//         else((struct sockaddr_in6 *)(s))->sin6_port = (p);                     \
//         _Pragma("clang diagnostic pop")                                        \
//     } while (0)


#define sa_get_port(s)                                                         \
    _Pragma("clang diagnostic push")                                           \
                _Pragma("clang diagnostic ignored \"-Wcast-align\"")(          \
                    (const struct sockaddr *)(s))                              \
                    ->sa_family == AF_INET                                     \
        ? ((const struct sockaddr_in *)(s))->sin_port                          \
        : ((const struct sockaddr_in6 *)(s))                                   \
              ->sin6_port _Pragma("clang diagnostic pop")


#define sa_addr(s)                                                             \
    _Pragma("clang diagnostic push")                                           \
        _Pragma("clang diagnostic ignored \"-Wcast-align\"")(                  \
            ((const struct sockaddr *)(s))->sa_family == AF_INET               \
                ? (const struct sockaddr *)&((const struct sockaddr_in *)(s))  \
                      ->sin_addr                                               \
                : (const struct sockaddr *)&((const struct sockaddr_in6 *)(s)) \
                      ->sin6_addr) _Pragma("clang diagnostic pop")


struct w_tuple {
    uint16_t src_idx;      ///< Index of source address.
    uint16_t src_port;     ///< Source port.
    struct w_sockaddr dst; ///< Destination address and port.
};


extern khint_t __attribute__((nonnull))
tuple_hash(const struct w_tuple * const tup);

extern khint_t __attribute__((nonnull))
tuple_equal(const struct w_tuple * const a, const struct w_tuple * const b);

KHASH_INIT(sock, struct w_tuple *, struct w_sock *, 1, tuple_hash, tuple_equal)


/// A warpcore backend engine.
///
struct w_engine {
    void * mem;           ///< Pointer to netmap or socket buffer memory region.
    struct w_iov * bufs;  ///< Pointer to w_iov buffers.
    struct w_backend * b; ///< Backend.
    uint16_t mtu;         ///< MTU of this interface.
    uint32_t mbps;        ///< Link speed of this interface in Mb/s.
    struct ether_addr mac; ///< Local Ethernet MAC address of the interface.
    struct ether_addr rip; ///< Ethernet MAC address of the next-hop router.

    khash_t(sock) sock;  ///< List of open (bound) w_sock sockets.
    struct w_iov_sq iov; ///< Tail queue of w_iov buffers available.

    sl_entry(w_engine) next;      ///< Pointer to next engine.
    char ifname[8];               ///< Name of the interface of this engine.
    char drvname[8];              ///< Name of the driver of this interface.
    const char * backend_name;    ///< Name of the backend in @p b.
    const char * backend_variant; ///< Name of the backend variant in @p b.

    /// Pointer to generic user data (not used by warpcore.)
    void * data;

    uint16_t addr_cnt;
    uint16_t addr4_pos;
    struct w_ifaddr ifaddr[];
};


/// A chain of w_sock socket.
///
sl_head(w_sock_slist, w_sock);


/// Initializer for struct w_sock_slist.
///
/// @param      l     A struct w_sock_slist.
///
/// @return     Empty w_sock_slist, to be assigned to @p l.
///
#define w_sock_slist_initializer(l) sl_head_initializer(l)


/// Socket options.
///
struct w_sockopt {
    /// Do not compute a UDP checksum for outgoing packets.
    uint32_t enable_udp_zero_checksums : 1;
    /// Enable ECN, by setting ECT(0) on all packets.
    uint32_t enable_ecn : 1;
    uint32_t : 30;
};


/// A warpcore socket.
///
struct w_sock {
    /// Pointer back to the warpcore instance associated with this w_sock.
    struct w_engine * w;

    struct w_tuple tup;     ///< Socket four-tuple.
    struct ether_addr dmac; ///< Destination MAC address.
    struct w_sockopt opt;   ///< Socket options.

    int fd; ///< Socket descriptor underlying the engine.

    struct w_iov_sq iv; ///< Tail queue containing incoming unread data.

    sl_entry(w_sock) next_rx; ///< Next socket with unread data.
};


/// The I/O vector structure that warpcore uses at the center of its API. It is
/// mostly a pointer to the first UDP payload byte contained in a netmap packet
/// buffer, together with some associated meta data.
///
/// The meta data consists of the length of the payload data, the sender IP
/// address and port number, the DSCP and ECN bits associated with the IP
/// packet in which the payload arrived, and the netmap arrival timestamp.
///
/// The w_iov structure also contains a pointer to the next I/O vector, which
/// can be used to chain together longer data items for use with w_rx() and
/// w_tx().
///
struct w_iov {
    /// Pointer back to the warpcore instance associated with this w_iov.
    struct w_engine * w;

    uint8_t * base;       ///< Absolute start of buffer.
    uint8_t * buf;        ///< Start of payload data.
    sq_entry(w_iov) next; ///< Next w_iov in a w_iov_sq.
    uint32_t idx;         ///< Index of netmap buffer.
    uint16_t len;         ///< Length of payload data.

    /// Sender IP address and port on RX. Destination IP address and port on TX
    /// on a disconnected w_sock. Ignored on TX on a connected w_sock.
    struct w_sockaddr saddr;

    /// DSCP + ECN of the received IP packet on RX, DSCP + ECN to use for the
    /// to-be-transmitted IP packet on TX.
    uint8_t flags;

    /// Can be used by application to maintain arbitrary data. Not used by
    /// warpcore.
    uint16_t user_data;

    /// @cond
    uint8_t _unused; ///< @internal Padding.
    /// @endcond

    ///< Pointer to the w_iov_sq this w_iov resides in. Only valid on TX.
    struct w_iov_sq * o;
};


/// Return the index of w_iov @p v.
///
/// @param      v     A w_iov.
///
/// @return     Index between 0-nfbus.
///
static inline uint32_t __attribute__((nonnull))
w_iov_idx(const struct w_iov * const v)
{
    return v - v->w->bufs;
}


/// Return a pointer to the w_iov with index @p i.
///
/// @param      w     Warpcore engine.
/// @param      i     Index.
///
/// @return     Pointer to w_iov.
///
static inline struct w_iov * __attribute__((nonnull))
w_iov(const struct w_engine * const w, const uint32_t i)
{
    return &w->bufs[i];
}


extern struct w_engine * __attribute__((nonnull))
w_init(const char * const ifname, const uint32_t rip, const uint_t nbufs);

extern void __attribute__((nonnull)) w_cleanup(struct w_engine * const w);

extern struct w_sock * __attribute__((nonnull(1)))
w_bind(struct w_engine * const w,
       const uint16_t addr_idx,
       const uint16_t port,
       const struct w_sockopt * const opt);

extern int __attribute__((nonnull))
w_connect(struct w_sock * const s, const struct sockaddr * const peer);

extern void __attribute__((nonnull)) w_close(struct w_sock * const s);

extern void __attribute__((nonnull)) w_alloc_len(struct w_engine * const w,
                                                 struct w_iov_sq * const q,
                                                 const uint_t qlen,
                                                 const uint16_t len,
                                                 const uint16_t off);

extern void __attribute__((nonnull)) w_alloc_cnt(struct w_engine * const w,
                                                 struct w_iov_sq * const q,
                                                 const uint_t count,
                                                 const uint16_t len,
                                                 const uint16_t off);

extern struct w_iov * __attribute__((nonnull))
w_alloc_iov(struct w_engine * const w, const uint16_t len, const uint16_t off);

extern void __attribute__((nonnull))
w_tx(const struct w_sock * const s, struct w_iov_sq * const o);

extern uint_t w_iov_sq_len(const struct w_iov_sq * const q);

extern int __attribute__((nonnull)) w_fd(const struct w_sock * const s);

extern void __attribute__((nonnull))
w_rx(struct w_sock * const s, struct w_iov_sq * const i);

extern void __attribute__((nonnull)) w_nic_tx(struct w_engine * const w);

extern bool __attribute__((nonnull))
w_nic_rx(struct w_engine * const w, const int64_t nsec);

extern uint32_t __attribute__((nonnull))
w_rx_ready(struct w_engine * const w, struct w_sock_slist * sl);

extern uint16_t __attribute__((nonnull))
w_iov_max_len(const struct w_iov * const v);

extern void __attribute__((nonnull)) w_free(struct w_iov_sq * const q);

extern void __attribute__((nonnull)) w_free_iov(struct w_iov * const v);

extern struct w_sock * __attribute__((nonnull(1)))
w_get_sock(struct w_engine * const w,
           const uint16_t src_idx,
           const uint16_t src_port,
           const struct sockaddr * const dst);

extern const struct w_sockaddr * __attribute__((nonnull))
w_get_sockaddr(const struct w_sock * const s, const bool local);

extern const char * __attribute__((nonnull))
w_ntop(const struct w_addr * const addr,
       char * const dst,
       const size_t dst_len);

extern void w_init_rand(void);

extern uint64_t w_rand64(void);

extern uint64_t w_rand_uniform64(const uint64_t upper_bound);

extern uint32_t w_rand32(void);

extern uint32_t w_rand_uniform32(const uint32_t upper_bound);

/// Return the number of w_iov structs in @p q that are still waiting for
/// transmission. Only valid after w_tx() has been called on @p p.
///
/// @param      q     A tail queue of w_iov structs.
///
/// @return     Number of w_iov structs not yet transmitted.
///
static inline uint32_t __attribute__((nonnull))
w_tx_pending(const struct w_iov_sq * const q)
{
    return q->tx_pending;
}


/// Return warpcore engine serving w_sock @p s.
///
/// @param[in]  s     A w_sock.
///
/// @return     The warpcore engine for w_sock @p s.
///
static inline struct w_engine * __attribute__((nonnull))
w_engine(const struct w_sock * const s)
{
    return s->w;
}


/// Return name of interface associated with a warpcore engine.
///
/// @param      w     Backend engine.
///
/// @return     Interface name.
///
static inline const char * __attribute__((nonnull))
w_ifname(const struct w_engine * const w)
{
    return w->ifname;
}


/// Return MTU of w_engine @p w.
///
/// @param[in]  w     Backend engine.
///
/// @return     MTU value in use by engine @p w.
///
static inline uint16_t __attribute__((nonnull))
w_mtu(const struct w_engine * const w)
{
    return w->mtu;
}


/// Return the number of w_iov structures in the w_iov tail queue @p c.
///
/// @param[in]  q     The w_iov tail queue to compute the payload length of.
///
/// @return     Number of w_iov structs in @p q.
///
static inline uint_t __attribute__((nonnull))
w_iov_sq_cnt(const struct w_iov_sq * const q)
{
    return sq_len(q);
}


/// Return link speed of w_engine @p w in Mb/s.
///
/// @param[in]  w     Backend engine.
///
/// @return     Link speed of @p w.
///
static inline uint32_t __attribute__((nonnull))
w_mbps(const struct w_engine * const w)
{
    return w->mbps;
}


/// Return name of the driver associated with the interface of a warpcore
/// engine.
///
/// @param[in]  w     Backend engine.
///
/// @return     Driver name @p w.
///
static inline const char * __attribute__((nonnull))
w_drvname(const struct w_engine * const w)
{
    return w->drvname;
}


/// Return the current socket options.
///
/// @param[in]  s     A w_sock.
///
/// @return     The socket options for w_sock @p s.
///
static inline const struct w_sockopt * __attribute__((nonnull))
w_get_sockopt(const struct w_sock * const s)
{
    return &s->opt;
}


/// Return whether a socket is connected (i.e., w_connect() has been called on
/// it) or not.
///
/// @param[in]  s     Connection.
///
/// @return     True when connected, zero otherwise.
///
static inline bool __attribute__((nonnull))
w_connected(const struct w_sock * const s)
{
    return s->tup.dst.port;
}


/// Set the socket options.
///
/// @param[in]  s     { parameter_description }
///
extern void __attribute__((nonnull))
w_set_sockopt(struct w_sock * const s, const struct w_sockopt * const opt);


/// Return the relative time in nanoseconds since an undefined epoch.
///
extern uint64_t w_now(void);


/// Sleep for a number of nanoseconds.
///
/// @param[in]  ns    Sleep time in nanoseconds.
///
extern void w_nanosleep(const uint64_t ns);

#ifdef __cplusplus
}
#endif
