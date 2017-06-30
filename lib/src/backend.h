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

#pragma once

#include <warpcore/warpcore.h>

#include "arp.h"
#include "eth.h"
#include "ip.h"
#include "udp.h"


/// For a given buffer index, get a pointer to its beginning.
///
/// Since netmap uses a macro for this, we also need to use a macro for the
/// socket backend.
///
/// @param      w     Backend engine.
/// @param      i     Buffer index.
///
/// @return     Memory region associated with buffer @p i.
///
#ifdef WITH_NETMAP
#define IDX2BUF(w, i) ((uint8_t *)NETMAP_BUF(NETMAP_TXRING((w)->nif, 0), (i)))
#else
#define IDX2BUF(w, i) (((uint8_t *)w->mem + (i * w->mtu)))
#endif


/// A warpcore template packet header structure.
///
struct w_hdr {
    struct eth_hdr eth;                       ///< Ethernet header.
    struct ip_hdr ip __attribute__((packed)); ///< IPv4 header.
    struct udp_hdr udp;                       ///< UDP header.
};


extern int16_t __attribute__((nonnull))
w_sock_cmp(const struct w_sock * const a, const struct w_sock * const b);

SPLAY_HEAD(sock, w_sock);
SPLAY_PROTOTYPE(sock, w_sock, next, w_sock_cmp)


/// A warpcore backend engine.
///
struct w_engine {
    struct sock sock;         ///< List of open (bound) w_sock sockets.
    STAILQ_HEAD(, w_iov) iov; ///< Tail queue of w_iov buffers available.
    uint32_t ip;              ///< Local IPv4 address used on this interface.
    uint32_t mask;            ///< IPv4 netmask of this interface.
    uint16_t mtu;             ///< MTU of this interface.
    struct ether_addr mac;    ///< Local Ethernet MAC address of the interface.
    void * mem;   ///< Pointer to netmap or socket buffer memory region.
    uint32_t rip; ///< Our default IPv4 router IP address.
#ifdef WITH_NETMAP
    int fd;                     ///< Netmap file descriptor.
    struct netmap_if * nif;     ///< Netmap interface.
    struct nmreq * req;         ///< Netmap request structure.
    struct arp_cache arp_cache; ///< The ARP cache.
    uint32_t cur_txr;           ///< Index of the TX ring currently active.
    uint16_t next_eph;          ///< State for random port number generation.
    /// @cond
    uint8_t _unused[2]; ///< @internal Padding.
    /// @endcond
    uint32_t * tail; ///< TX ring tails after last NIOCTXSYNC call.
#else
#if defined(HAVE_KQUEUE)
    int kq;
#elif defined(HAVE_EPOLL)
    int ep;
#else
    /// @cond
    uint8_t _unused_2[4]; ///< @internal Padding.
    /// @endcond
#endif
    char * ifname; ///< Name of the interface of this engine.
#endif
    const char * backend; ///< Name of the warpcore backend used by the engine.
    SLIST_ENTRY(w_engine) next; ///< Pointer to next engine.
    struct w_iov * bufs;
};


/// Global list of initialized warpcore engines.
///
extern SLIST_HEAD(w_engines, w_engine) engines;


/// Compute the IPv4 broadcast address for the given IPv4 address and netmask.
///
/// @param      ip    The IPv4 address to compute the broadcast address for.
/// @param      mask  The netmask associated with @p ip.
///
/// @return     The IPv4 broadcast address associated with @p ip and @p mask.
///
#define mk_bcast(ip, mask) ((ip) | (~mask))


/// The IPv4 network prefix for the given IPv4 address and netmask.
///
/// @param      ip    The IPv4 address to compute the prefix for.
/// @param      mask  The netmask associated with @p ip.
///
/// @return     The IPv4 prefix associated with @p ip and @p mask.
///
#define mk_net(ip, mask) ((ip) & (mask))


extern struct w_sock * __attribute__((nonnull))
get_sock(struct w_engine * const w, const uint16_t port);

extern void __attribute__((nonnull)) backend_bind(struct w_sock * const s);

extern void __attribute__((nonnull)) backend_close(struct w_sock * const s);

extern void __attribute__((nonnull)) backend_connect(struct w_sock * const s);

extern void __attribute__((nonnull)) backend_init(struct w_engine * const w,
                                                  const char * const ifname,
                                                  const uint32_t nbufs);

extern void __attribute__((nonnull)) backend_cleanup(struct w_engine * const w);
