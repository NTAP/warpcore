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

#include <stdbool.h>
#include <stdint.h>

#include <warpcore/warpcore.h>

#include "eth.h"

#ifdef WITH_NETMAP
struct netmap_slot;
#endif


#define IP_P_ICMP 1   ///< IP protocol number for ICMP
#define IP_P_UDP 17   ///< IP protocol number for UDP
#define IP_P_ICMP6 58 ///< IP protocol number for ICMPv6

#define IP4_RF 0x80        ///< Reserved fragment flag (network byte-order.)
#define IP4_DF 0x40        ///< Dont fragment flag (network byte-order.)
#define IP4_MF 0x20        ///< More fragments flag (network byte-order.)
#define IP4_OFFMASK 0xff1f ///< Mask for fragmenting bits (network byte-order.)


/// An IPv4 header representation; see
/// [RFC791](https://tools.ietf.org/html/rfc791.)
///
struct ip4_hdr {
    uint8_t vhl;    ///< Header length/version byte.
    uint8_t tos;    ///< DSCP/ECN byte.
    uint16_t len;   ///< Total length of the IP packet.
    uint16_t id;    ///< IPv4 identification field.
    uint16_t off;   ///< Flags & fragment offset field.
    uint8_t ttl;    ///< Time-to-live field.
    uint8_t p;      ///< IP protocol number of payload.
    uint16_t cksum; ///< IP checksum.
    uint32_t src;   ///< Source IPv4 address.
    uint32_t dst;   ///< Destination IPv4 address.
} __attribute__((aligned(1)));


/// Extract the IP version out of the first byte of an IPv4 or IPv6 header.
///
/// @param[in]  v_byte  The first byte of an IP header.
///
/// @return     IP version number.
///
static inline uint8_t __attribute__((always_inline, const))
ip_v(const uint8_t v_byte)
{
    return (v_byte & 0xf0) >> 4;
}


/// Extract the IP header length out of an ip4_hdr::vhl field.
///
/// @param[in]  vhl  The first byte of an IPv4 header.
///
/// @return     IPv4 header length in bytes.
///
static inline uint8_t __attribute__((always_inline, const))
ip4_hl(const uint8_t vhl)
{
    return (vhl & 0x0f) * 4;
}


/// Extract the DSCP out of an ip4_hdr::tos field.
///
/// @param[in]  tos   The TOS byte of an IPv4 header.
///
/// @return     DSCP.
///
static inline uint8_t __attribute__((always_inline, const))
ip4_dscp(const uint8_t tos)
{
    return (tos & 0xfc) >> 2;
}


/// Extract the ECN bits out of an ip4_hdr::tos field.
///
/// @param[in]  tos   The TOS byte of an IPv4 header.
///
/// @return     ECN bits.
///
static inline uint8_t __attribute__((always_inline, const))
ip4_ecn(const uint8_t tos)
{
    return tos & 0x02;
}


/// Return a pointer to the payload data of the IPv4 packet in a buffer.
///
/// @param      buf   The buffer.
///
/// @return     Pointer to the first payload byte.
///
static inline uint8_t * __attribute__((always_inline, nonnull))
ip4_data(uint8_t * const buf)
{
    return buf + sizeof(struct eth_hdr) + ip4_hl(*eth_data(buf));
}


/// Calculates the length of the payload data for the given IPv4 header @p ip.
///
/// @param      ip    An ip4_hdr.
///
/// @return     { description_of_the_return_value }
///
static inline uint16_t __attribute__((always_inline, nonnull))
ip4_data_len(const struct ip4_hdr * const ip)
{
    return bswap16(ip->len) - ip4_hl(ip->vhl);
}


#ifdef WITH_NETMAP

extern bool __attribute__((nonnull)) ip4_rx(struct w_engine * const w,
                                            struct netmap_slot * const s,
                                            uint8_t * const buf);

extern void __attribute__((nonnull(1)))
mk_ip4_hdr(struct w_iov * const v, const struct w_sock * const s);


extern uint16_t __attribute__((nonnull))
is_my_ip4(const struct w_engine * const w,
          const uint32_t ip,
          const bool match_bcast);

#endif
