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

#include <stdbool.h>
#include <stdint.h>

// IWYU pragma: no_include <net/netmap.h>
#include <net/netmap_user.h> // IWYU pragma: keep

#include <warpcore/warpcore.h>

#include "eth.h"


/// Solicited-node multicast address prefix and mask
static const uint128_t snmap_prefix = (uint128_t)0x2ff | (uint128_t)0xff01
                                                             << 88;
static const uint128_t snmap_mask = (uint128_t)0x00ffffff << 104;


/// An IPv6 header representation; see
/// [RFC3542](https://tools.ietf.org/html/rfc3542.)
///
struct ip6_hdr {
    union {
        struct {
            uint32_t vtcecnfl; ///< Version, traffic class, flow label.
            uint16_t len;      ///< Payload length of the IPv6 packet.
            uint8_t next_hdr;  ///< Next header.
            uint8_t hlim;      ///< Hop limit.
        };
        uint8_t vfc; ///< Version, partial traffic class.
    };
    uint8_t src[16]; ///< Source IPv6 address.
    uint8_t dst[16]; ///< Destination IPv6 address.
} __attribute__((aligned(1)));


/// Extract the traffic class out of an ip6_hdr::vtcecnfl field.
///
/// @param[in]  vtcecnfl  An ip6_hdr::vtcecnfl field.
///
/// @return     Traffic class.
///
static inline uint8_t __attribute__((always_inline, const))
ip6_tc(const uint32_t vtcecnfl)
{
    return ((uint8_t)(vtcecnfl & 0x0000f000) >> 12) |
           (uint8_t)((vtcecnfl & 0x0000000f) << 4);
}


/// Extract the ECN bits out of an ip6_hdr::vtcecnfl field.
///
/// @param[in]  vtcecnfl  An ip6_hdr::vtcecnfl field.
///
/// @return     ECN bits.
///
static inline uint8_t __attribute__((always_inline, const))
ip6_ecn(const uint32_t vtcecnfl)
{
    return (vtcecnfl & 0x00003000) >> 12;
}


/// Extract the flow label out of an ip6_hdr::vtcecnfl field.
///
/// @param[in]  vtcecnfl  An ip6_hdr::vtcecnfl field.
///
/// @return     Flow label in network byte-order.
///
static inline uint32_t __attribute__((always_inline, const))
ip6_flow_label(const uint32_t vtcecnfl)
{
    return vtcecnfl & 0xffff0f00;
}


/// Return a pointer to the payload data of the IPv6 packet in a buffer.
///
/// @param      buf   The buffer.
///
/// @return     Pointer to the first payload byte.
///
static inline uint8_t * __attribute__((always_inline, nonnull))
ip6_data(uint8_t * const buf)
{
    return buf + sizeof(struct eth_hdr) + sizeof(struct ip6_hdr);
}


extern bool __attribute__((nonnull)) ip6_rx(struct w_engine * const w,
                                            struct netmap_slot * const s,
                                            uint8_t * const buf);

extern void __attribute__((nonnull(1)))
mk_ip6_hdr(struct w_iov * const v, const struct w_sock * const s);


extern uint16_t __attribute__((nonnull))
is_my_ip6(const struct w_engine * const w,
          const uint128_t ip,
          const bool match_mcast);
