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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

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

#if !defined(PARTICLE) && !defined(RIOT_VERSION)
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/socket.h>
#endif

#ifdef RIOT_VERSION
#include "net/netif.h"
#include "net/sock/udp.h"
#endif


/// A tail queue of w_iov I/O vectors. Also contains a counter that (on TX)
/// tracks how many w_iovs have not yet been transmitted by the NIC.
///
sq_head(w_iov_sq, w_iov); ///< Head of the w_iov tail queue.


/// Initializer for struct w_iov_sq.
///
/// @param      q     A struct w_iov_sq.
///
/// @return     Empty w_iov_sq, to be assigned to @p q.
///
#define w_iov_sq_initializer(q) sq_head_initializer(q)


#define IP6_LEN 16 ///< Length of an IPv4 address in bytes. Sixteen.
#define IP6_STRLEN INET6_ADDRSTRLEN

/// Initializer for temporary string for holding an IPv6 address.
#define ip6_tmp                                                                \
    (char[IP6_STRLEN])                                                         \
    {                                                                          \
        ""                                                                     \
    }

#define IP4_LEN 4 ///< Length of an IPv4 address in bytes. Four.
#define IP4_STRLEN INET_ADDRSTRLEN

/// Initializer for temporary string for holding an IPv4 address.
#define ip4_tmp                                                                \
    (char[IP4_STRLEN])                                                         \
    {                                                                          \
        ""                                                                     \
    }

#define IP_STRLEN MAX(IP4_STRLEN, IP6_STRLEN)

/// Initializer for temporary string for holding an IPv4 or IPv6 address.
#define ip_tmp                                                                 \
    (char[IP_STRLEN])                                                          \
    {                                                                          \
        ""                                                                     \
    }


/// Return the length of the IP address of address family @p af.
///
/// @param      af    AF_INET or AF_INET6.
///
/// @return     Length of an IP address of the given family.
///
#define af_len(af) (uint8_t)((af) == AF_INET ? IP4_LEN : IP6_LEN)


/// Return the length of an IP header (without options) of address family @p af.
///
/// @param      af    AF_INET or AF_INET6.
///
/// @return     Length of an IP header of the given family.
///
#define ip_hdr_len(af) (uint8_t)((af) == AF_INET ? 20 : 40)


/// Compare two IPv6 addresses for equality.
///
/// @param      a     An IPv6 address.
/// @param      b     Another IPv6 address.
///
/// @return     True if equal, false otherwise.
///
#define ip6_eql(a, b) (memcmp((a), (b), IP6_LEN) == 0)


struct w_addr {
    sa_family_t af; ///< Address family.
    union {
        uint32_t ip4;         ///< IPv4 address (when w_addr::af is AF_INET).
        uint8_t ip6[IP6_LEN]; ///< IPv6 address (when w_addr::af is AF_INET6).
    };
};


struct w_ifaddr {
    struct w_addr addr; ///< Interface address.

    union {
        /// IPv4 broadcast address (when w_ifaddr::addr::af is AF_INET).
        uint32_t bcast4;

        /// IPv6 broadcast address (when w_ifaddr::addr::af is AF_INET6).
        uint8_t bcast6[IP6_LEN];
    };

    union {
        /// IPv4 solicited-node multicast address (when w_ifaddr::addr::af is
        /// AF_INET). (Not currently in use.)
        uint32_t snma4;

        /// IPv6 solicited-node multicast address (when w_ifaddr::addr::af is
        /// AF_INET6).
        uint8_t snma6[IP6_LEN];
    };

    uint32_t scope_id; ///< IPv6 scope ID.
    uint8_t prefix;    ///< Prefix length.
};


struct w_sockaddr {
    struct w_addr addr; ///< IP address.
    uint16_t port;      ///< Port number.
};


struct w_socktuple {
    struct w_sockaddr local;  ///< Local address and port.
    struct w_sockaddr remote; ///< Remote address and port.
    uint32_t scope_id;        ///< IPv6 scope ID.
};


extern khint_t __attribute__((nonnull))
w_socktuple_hash(const struct w_socktuple * const tup);

extern khint_t __attribute__((nonnull))
w_socktuple_cmp(const struct w_socktuple * const a,
                const struct w_socktuple * const b);


#define ETH_LEN 6
#define ETH_STRLEN (ETH_LEN * 3 + 1)

#define eth_tmp                                                                \
    (char[ETH_STRLEN])                                                         \
    {                                                                          \
        ""                                                                     \
    }


struct eth_addr {
    uint8_t addr[ETH_LEN];
};


#ifdef RIOT_VERSION
#define IFNAMSIZ CONFIG_NETIF_NAMELENMAX
#endif


/// A warpcore backend engine.
///
struct w_engine {
    void * mem;           ///< Pointer to netmap or socket buffer memory region.
    struct w_iov * bufs;  ///< Pointer to w_iov buffers.
    struct w_backend * b; ///< Backend.
    uint16_t mtu;         ///< MTU of this interface.
    uint32_t mbps;        ///< Link speed of this interface in Mb/s.
    struct eth_addr mac;  ///< Local Ethernet MAC address of the interface.
    // struct eth_addr rip;  ///< Ethernet MAC address of the next-hop router.

    struct w_iov_sq iov; ///< Tail queue of w_iov buffers available.

    sl_entry(w_engine) next;      ///< Pointer to next engine.
    char ifname[IFNAMSIZ];        ///< Name of the interface of this engine.
    char drvname[IFNAMSIZ];       ///< Name of the driver of this interface.
    const char * backend_name;    ///< Name of the backend in @p b.
    const char * backend_variant; ///< Name of the backend variant in @p b.

    /// Pointer to generic user data (not used by warpcore.)
    void * data;

    uint16_t addr_cnt;
    uint16_t addr4_pos;
    uint8_t have_ip4 : 1;
    uint8_t have_ip6 : 1;
    uint8_t is_loopback : 1;
    uint8_t is_right_pipe : 1;
    uint8_t : 4;
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

    /// Pointer to generic user data (not used by warpcore.)
    void * data;

    struct w_socktuple tup; ///< Socket four-tuple.
    struct eth_addr dmac;   ///< Destination MAC address.
    struct w_sockopt opt;   ///< Socket options.
    intptr_t fd;            ///< Socket descriptor underlying the engine.
    struct w_iov_sq iv;     ///< Tail queue containing incoming unread data.

    sl_entry(w_sock) next; ///< Next socket.

#if !defined(HAVE_KQUEUE) && !defined(HAVE_EPOLL)
    sl_entry(w_sock) __next; ///< Internal use.
#endif
};


#define ws_af tup.local.addr.af
#define ws_loc tup.local
#define ws_laddr tup.local.addr
#define ws_lport tup.local.port
#define ws_rem tup.remote
#define ws_raddr tup.remote.addr
#define ws_rport tup.remote.port
#define ws_scope tup.scope_id


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

    /// Sender IP address and port on RX. Destination IP address and port on TX
    /// on a disconnected w_sock. Ignored on TX on a connected w_sock.
    struct w_sockaddr saddr;

    uint8_t * base;       ///< Absolute start of buffer.
    uint8_t * buf;        ///< Start of payload data.
    sq_entry(w_iov) next; ///< Next w_iov in a w_iov_sq.
    uint32_t idx;         ///< Index of netmap buffer.
    uint16_t len;         ///< Length of payload data.

    /// DSCP + ECN of the received IP packet on RX, DSCP + ECN to use for the
    /// to-be-transmitted IP packet on TX.
    uint8_t flags;

    /// TTL of received IP packets.
    uint8_t ttl;

    /// Can be used by application to maintain arbitrary data. Not used by
    /// warpcore.
    uint16_t user_data;
};


#define wv_port saddr.port
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
static inline uint32_t __attribute__((nonnull, no_instrument_function))
w_iov_idx(const struct w_iov * const v)
{
    return (uint32_t)(v - v->w->bufs);
}


/// Return a pointer to the w_iov with index @p i.
///
/// @param      w     Warpcore engine.
/// @param      i     Index.
///
/// @return     Pointer to w_iov.
///
static inline struct w_iov * __attribute__((nonnull, no_instrument_function))
w_iov(const struct w_engine * const w, const uint32_t i)
{
    return &w->bufs[i];
}


/// Return warpcore engine serving w_sock @p s.
///
/// @param[in]  s     A w_sock.
///
/// @return     The warpcore engine for w_sock @p s.
///
static inline struct w_engine * __attribute__((nonnull, no_instrument_function))
w_engine(const struct w_sock * const s)
{
    return s->w;
}


/// Return the maximum UDP payload for a given socket.
///
/// @param[in]  s     A w_sock.
///
/// @return     Maximum UDP payload.
///
static inline uint16_t __attribute__((nonnull, no_instrument_function))
w_max_udp_payload(const struct w_sock * const s)
{
    return s->w->mtu - ip_hdr_len(s->ws_af) - 8; // 8 = sizeof(struct udp_hdr)
}


/// Return the number of w_iov structures in the w_iov tail queue @p c.
///
/// @param[in]  q     The w_iov tail queue to compute the payload length of.
///
/// @return     Number of w_iov structs in @p q.
///
static inline uint_t __attribute__((nonnull, no_instrument_function))
w_iov_sq_cnt(const struct w_iov_sq * const q)
{
    return sq_len(q);
}


/// Return the current socket options.
///
/// @param[in]  s     A w_sock.
///
/// @return     The socket options for w_sock @p s.
///
static inline const struct w_sockopt * __attribute__((nonnull,
                                                      no_instrument_function))
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
static inline bool __attribute__((nonnull, no_instrument_function))
w_connected(const struct w_sock * const s)
{
    return s->ws_rport;
}


static inline bool __attribute__((nonnull))
w_is_linklocal(const struct w_addr * const a)
{
    if (a->af == AF_INET)
        return (a->ip4 & 0x0000ffff) == 0x0000fea9; // 169.254.0.0/16
    else
        return a->ip6[0] == 0xfe && (a->ip6[1] & 0xc0) == 0x80;
}


static inline bool __attribute__((nonnull))
w_is_private(const struct w_addr * const a)
{
    if (a->af == AF_INET)
        return (a->ip4 & 0x000000ff) == 0x0000000a || // 10.0.0.0/8
               (a->ip4 & 0x0000f0ff) == 0x000010ac || // 172.16.0.0/12
               (a->ip4 & 0x0000ffff) == 0x0000a8c0;   // 192.168.0.0/16
    else
        return a->ip6[0] == 0xfe && (a->ip6[1] & 0xc0) == 0x80;
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
w_tx(struct w_sock * const s, struct w_iov_sq * const o);

extern uint_t w_iov_sq_len(const struct w_iov_sq * const q);

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

extern const char * __attribute__((nonnull))
w_ntop(const struct w_addr * const addr, char * const dst);

extern void w_init_rand(void);

extern uint64_t w_rand64(void);

extern uint64_t w_rand_uniform64(const uint64_t upper_bound);

extern uint32_t w_rand32(void);

extern uint32_t w_rand_uniform32(const uint32_t upper_bound);

extern bool __attribute__((nonnull))
w_addr_cmp(const struct w_addr * const a, const struct w_addr * const b);

extern bool __attribute__((nonnull))
w_sockaddr_cmp(const struct w_sockaddr * const a,
               const struct w_sockaddr * const b);

extern void __attribute__((nonnull))
w_set_sockopt(struct w_sock * const s, const struct w_sockopt * const opt);

extern uint64_t w_now(const clockid_t clock);

extern void w_nanosleep(const uint64_t ns);

extern bool __attribute__((nonnull))
w_to_waddr(struct w_addr * const wa, const struct sockaddr * const sa);

#ifdef __cplusplus
}
#endif
