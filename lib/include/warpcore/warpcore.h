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
#include <sys/param.h>
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


#define IP6_LEN 16 ///< Length of an IPv4 address in bytes. Sixteen.
#define IP6_STRLEN INET6_ADDRSTRLEN


#define IP4_LEN 4 ///< Length of an IPv4 address in bytes. Four.
#define IP4_STRLEN INET_ADDRSTRLEN

#define IP_STRLEN MAX(IP4_STRLEN, IP6_STRLEN)

#define af_len(af) (uint8_t)((af) == AF_INET ? IP4_LEN : IP6_LEN)

#define ip_hdr_len(af) (uint8_t)((af) == AF_INET ? 20 : 40)


struct w_addr {
    sa_family_t af; ///< Address family.
    union {
        uint32_t ip4;  ///< IPv4 address (when w_addr::af is AF_INET).
        uint128_t ip6; ///< IPv6 address (when w_addr::af is AF_INET6).
    };
};

struct w_ifaddr {
    struct w_addr addr; ///< Interface address.
    union {
        uint32_t net4;  ///< IPv4 netmask (when w_ifaddr::addr::af is AF_INET).
        uint128_t net6; ///< IPv6 netmask (when w_ifaddr::addr::af is AF_INET6).
    };
    union {
        uint32_t bcast4; ///< IPv4 broadcast address (when w_ifaddr::addr::af is
                         ///< AF_INET).
        uint128_t bcast6; ///< IPv6 broadcast address (when w_ifaddr::addr::af
                          ///< is AF_INET6).
    };
    uint8_t prefix; ///< Prefix length.
};

struct w_sockaddr {
    uint16_t port;      ///< Port number.
    struct w_addr addr; ///< IP address.
};


struct w_socktuple {
    struct w_sockaddr local;  ///< Local address and port.
    struct w_sockaddr remote; ///< Remote address and port.
};


extern khint_t __attribute__((nonnull
#if defined(__clang__)
                              ,
                              no_sanitize("unsigned-integer-overflow")
#endif
                                  ))
w_socktuple_hash(const struct w_socktuple * const tup);

extern khint_t __attribute__((nonnull))
w_socktuple_cmp(const struct w_socktuple * const a,
                const struct w_socktuple * const b);

KHASH_INIT(sock,
           struct w_socktuple *,
           struct w_sock *,
           1,
           w_socktuple_hash,
           w_socktuple_cmp)


#define ETH_LEN 6
#define ETH_STRLEN (ETH_LEN * 3 + 1)

struct eth_addr {
    uint8_t addr[ETH_LEN];
};


/// A warpcore backend engine.
///
struct w_engine {
    void * mem;           ///< Pointer to netmap or socket buffer memory region.
    struct w_iov * bufs;  ///< Pointer to w_iov buffers.
    struct w_backend * b; ///< Backend.
    uint16_t mtu;         ///< MTU of this interface.
    uint32_t mbps;        ///< Link speed of this interface in Mb/s.
    struct eth_addr mac;  ///< Local Ethernet MAC address of the interface.
    struct eth_addr rip;  ///< Ethernet MAC address of the next-hop router.

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
    uint8_t have_ip4 : 1;
    uint8_t have_ip6 : 1;
    uint8_t : 6;
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

    struct w_socktuple tup; ///< Socket four-tuple.
    struct eth_addr dmac;   ///< Destination MAC address.
    struct w_sockopt opt;   ///< Socket options.

    int fd; ///< Socket descriptor underlying the engine.

    struct w_iov_sq iv; ///< Tail queue containing incoming unread data.

    sl_entry(w_sock) next_rx; ///< Next socket with unread data.
};


#define ws_af tup.local.addr.af
#define ws_laddr tup.local.addr
#define ws_lport tup.local.port
#define ws_raddr tup.remote.addr
#define ws_rport tup.remote.port


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


#define wv_af saddr.addr.af
#define wv_ip4 saddr.addr.ip4
#define wv_ip6 saddr.addr.ip6
#define wv_addr saddr.addr


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
                                                 const int af,
                                                 struct w_iov_sq * const q,
                                                 const uint_t qlen,
                                                 const uint16_t len,
                                                 const uint16_t off);

extern void __attribute__((nonnull)) w_alloc_cnt(struct w_engine * const w,
                                                 const int af,
                                                 struct w_iov_sq * const q,
                                                 const uint_t count,
                                                 const uint16_t len,
                                                 const uint16_t off);

extern struct w_iov * __attribute__((nonnull))
w_alloc_iov(struct w_engine * const w,
            const int af,
            const uint16_t len,
            const uint16_t off);

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
w_max_iov_len(const struct w_iov * const v, const uint16_t af);

extern void __attribute__((nonnull)) w_free(struct w_iov_sq * const q);

extern void __attribute__((nonnull)) w_free_iov(struct w_iov * const v);

extern struct w_sock * __attribute__((nonnull(1, 2)))
w_get_sock(struct w_engine * const w,
           const struct w_sockaddr * const local,
           const struct w_sockaddr * const remote);

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
    return s->tup.remote.port;
}


/// Compare two w_addr structs for equality.
///
/// @param[in]  a     First struct.
/// @param[in]  b     Second struct.
///
/// @return     True if equal, false otherwise.
///
static inline bool __attribute__((nonnull))
w_addr_cmp(const struct w_addr * const a, const struct w_addr * const b)
{
    return a->af == b->af &&
           (a->af == AF_INET ? (a->ip4 == b->ip4) : (a->ip6 == b->ip6));
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


/// Initialize w_addr @p wa based on sockaddr @p sa.
///
/// @param      wa    The w_addr struct to initialize.
/// @param[in]  sa    The sockaddr struct to initialize based on.
///
/// @return     True if the initialization succeeded.
///
extern bool __attribute__((nonnull))
w_to_waddr(struct w_addr * const wa, const struct sockaddr * const sa);


#ifdef __cplusplus
}
#endif
