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

#include <arpa/inet.h>
#include <netinet/ip.h>
#include <stdbool.h>
#include <stdint.h>

struct w_engine;
struct netmap_slot;
struct w_iov;


#define IP_ADDR_LEN 4 ///< Length of an IPv4 address in bytes. Four.

/// String length of the representation we use for IPv4 strings. The format is
/// "xxx.xxx.xxx.xxx\0", including a final zero byte.
///
#define IP_ADDR_STRLEN 16

#define IP_ANY 0x00000000   ///< IPv4 "any" address.
#define IP_BCAST 0xffffffff ///< IPv4 broadcast address.


/// An IPv4 header representation; see
/// [RFC791](https://tools.ietf.org/html/rfc791.)
///
struct ip_hdr {
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

#define IP_P_ICMP 1 ///< IP protocol number for ICMP
#define IP_P_UDP 17 ///< IP protocol number for UDP


/// Extract the IP version out of an ip_hdr::vhl field.
///
/// @param      ip    Pointer to an ip_hdr.
///
/// @return     IP version number.
///
static inline uint8_t __attribute__((always_inline, nonnull))
ip_v(const struct ip_hdr * const ip)
{
    return (ip->vhl & 0xf0) >> 4;
}


/// Extract the IP header length out of an ip_hdr::vhl field.
///
/// @param      ip    Pointer to an ip_hdr.
///
/// @return     IP header length in bytes.
///
static inline uint8_t __attribute__((always_inline,
                                     nonnull
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 8)
                                     ,
                                     no_sanitize("alignment")
#endif
                                         ))
ip_hl(const struct ip_hdr * const ip)
{
    const uint8_t hl = (ip->vhl & 0x0f) * 4;
    return hl ? hl : sizeof(*ip);
}


/// Extract the DSCP out of an ip_hdr::tos field.
///
/// @param      ip    Pointer to an ip_hdr.
///
/// @return     DSCP.
///
static inline uint8_t __attribute__((always_inline,
                                     nonnull
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 8)
                                     ,
                                     no_sanitize("alignment")
#endif
                                         ))
ip_dscp(const struct ip_hdr * const ip)
{
    return (ip->tos & 0xfc) >> 2;
}


/// Extract the ECN bits out of an ip_hdr::tos field.
///
/// @param      ip    Pointer to an ip_hdr.
///
/// @return     ECN bits.
///
static inline uint8_t __attribute__((always_inline,
                                     nonnull

#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 8)
                                     ,
                                     no_sanitize("alignment")
#endif
                                         ))
ip_ecn(const struct ip_hdr * const ip)
{
    return ip->tos & 0x02;
}

#include "eth.h" // IWYU pragma: keep

/// Return a pointer to the payload data of the IPv4 packet in a buffer. It is
/// up to the caller to ensure that the packet buffer does in fact contain an
/// IPv4 packet.
///
/// @param      buf   The buffer.
///
/// @return     Pointer to the first payload byte.
///
static inline uint8_t * __attribute__((always_inline, nonnull))
ip_data(uint8_t * const buf)
{
    uint8_t * const ed = eth_data(buf);
    return ed + ip_hl((struct ip_hdr *)(void *)ed);
}


/// Calculates the length of the payload data for the given IPv4 header @p ip.
///
/// @param      ip    An ip_hdr.
///
/// @return     { description_of_the_return_value }
///
static inline uint16_t __attribute__((always_inline, nonnull))
ip_data_len(const struct ip_hdr * const ip)
{
    return ntohs(ip->len) - ip_hl(ip);
}


/// Initialize the static fields in an IPv4 ip_hdr header.
///
/// @param      ip    Pointer to the ip_hdr to initialize.
///
static inline void __attribute__((always_inline,
                                  nonnull
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 8)
                                  ,
                                  no_sanitize("alignment")
#endif
                                      )) ip_hdr_init(struct ip_hdr * const ip)
{
    ip->vhl = (4 << 4) + 5;
    ip->off = htons(IP_DF);
    ip->ttl = 64; /* XXX this should be configurable */
    ip->p = IP_P_UDP;
    ip->cksum = 0;
}


extern void __attribute__((nonnull)) ip_tx_with_rx_buf(struct w_engine * w,
                                                       const uint8_t p,
                                                       void * const buf,
                                                       const uint16_t len);

extern void __attribute__((nonnull)) ip_rx(struct w_engine * const w,
                                           struct netmap_slot * const s,
                                           uint8_t * const buf);

extern bool __attribute__((nonnull))
ip_tx(struct w_engine * const w, struct w_iov * const v, const uint16_t len);
