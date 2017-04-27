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

#ifdef WITH_NETMAP
#include <net/netmap_user.h> // IWYU pragma: export
#endif

#include "eth.h"
#include "ip.h"
#include "udp.h"
#include "warpcore.h"


#ifndef WITH_NETMAP
/// Length of a buffer. Same as netmap uses.
///
#define IOV_BUF_LEN 2048
#endif


/// For a given buffer index, get a pointer to its beginning.
///
/// Since netmap uses a macro for this, we also need to use a macro for the shim
/// backend.
///
/// @param      w     Backend engine.
/// @param      i     Buffer index.
///
/// @return     Memory region associated with buffer @p i.
///
#ifdef WITH_NETMAP
#define IDX2BUF(w, i) NETMAP_BUF(NETMAP_TXRING((w)->nif, 0), (i))
#else
#define IDX2BUF(w, i) (((uint8_t *)w->mem + (i * IOV_BUF_LEN)))
#endif


/// The number of extra buffers to allocate from netmap. Extra buffers are
/// buffers that are not used to support TX or RX rings, but instead are used
/// for the warpcore w_sock::iv and w_sock::ov socket buffers, as well as for
/// maintaining packetized data inside an application using warpcore.
///
#define NUM_BUFS 900000 // XXX this should become configurable


/// A warpcore template packet header structure.
///
struct w_hdr {
    struct eth_hdr eth;                       ///< Ethernet header.
    struct ip_hdr ip __attribute__((packed)); ///< IPv4 header.
    struct udp_hdr udp;                       ///< UDP header.
};


struct arp_entry;

/// A warpcore backend engine.
///
struct w_engine {
    SLIST_HEAD(, w_sock) sock; ///< List of open (bound) w_sock sockets.
    STAILQ_HEAD(, w_iov) iov;  ///< Tail queue of w_iov buffers available.
    uint32_t ip;               ///< Local IPv4 address used on this interface.
    uint32_t mask;             ///< IPv4 netmask of this interface.
    uint16_t mtu;              ///< MTU of this interface.
    uint8_t mac[ETH_ADDR_LEN]; ///< Local Ethernet MAC address of the interface.
    void * mem;   ///< Pointer to netmap or shim buffer memory region.
    uint32_t rip; ///< Our default IPv4 router IP address.
#ifdef WITH_NETMAP
    int fd;                            ///< Netmap file descriptor.
    struct netmap_if * nif;            ///< Netmap interface.
    struct nmreq * req;                ///< Netmap request structure.
    SLIST_HEAD(, arp_entry) arp_cache; ///< The ARP cache.
    uint32_t cur_txr; ///< Index of the TX ring currently active.
    /// @cond
    /// @internal Padding.
    uint8_t _unused2[4];
    /// @endcond
    uint32_t * tail; ///< TX ring tails after last NIOCTXSYNC call.
#else
    /// @cond
    /// @internal Padding.
    uint8_t _unused1[4];
    /// @endcond
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


struct w_iov * __attribute__((nonnull)) alloc_iov(struct w_engine * const w);

struct w_sock * __attribute__((nonnull))
get_sock(struct w_engine * const w, const uint16_t port);

extern void __attribute__((nonnull)) backend_bind(struct w_sock * s);

extern void __attribute__((nonnull)) backend_connect(struct w_sock * const s);

extern void __attribute__((nonnull))
backend_init(struct w_engine * w, const char * const ifname);

extern void __attribute__((nonnull)) backend_cleanup(struct w_engine * const w);
