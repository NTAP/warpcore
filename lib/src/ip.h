// SPDX-License-Identifier: BSD-2-Clause
//
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

#include <stdbool.h>
#include <stdint.h>

#ifndef __linux__
#include <arpa/inet.h>
#endif

#include "eth.h"

struct w_iov;
struct w_engine;
struct netmap_ring;

#define IP_ECN_NOT_ECT 0x00 ///< ECN was not enabled.
#define IP_ECN_ECT_1 0x01   ///< ECN capable packet.
#define IP_ECN_ECT_0 0x02   ///< ECN capable packet.
#define IP_ECN_CE 0x03      ///< ECN congestion.
#define IP_ECN_MASK 0x03    ///< Mask of ECN bits.

#define IP_OFF_MASK 0x1fff ///< Bit mask for extracting the fragment offset.
#define IP_RF 0x8000       ///< "Reserved" flag.
#define IP_DF 0x4000       ///< "Don't fragment" flag.
#define IP_MF 0x2000       ///< "More fragments" flag.

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
};

#define IP_P_ICMP 1 ///< IP protocol number for ICMP
#define IP_P_UDP 17 ///< IP protocol number for UDP


/// Extract the IP version out of an ip_hdr::vhl field.
///
/// @param      ip    Pointer to an ip_hdr.
///
/// @return     IP version number.
///
#define ip_v(ip) (uint8_t)((((const struct ip_hdr *)(ip))->vhl & 0xf0) >> 4)


/// Extract the IP header length out of an ip_hdr::vhl field.
///
/// @param      ip    Pointer to an ip_hdr.
///
/// @return     IP header length in bytes.
///
#define ip_hl(ip)                                                              \
    (uint8_t)((((const struct ip_hdr *)(const void *)(ip))->vhl & 0x0f) * 4)


/// Extract the DSCP out of an ip_hdr::tos field.
///
/// @param      ip    Pointer to an ip_hdr.
///
/// @return     DSCP.
///
#define ip_dscp(ip) (uint8_t)((((const struct ip_hdr *)(ip))->tos & 0xfc) >> 2)


/// Extract the ECN bits out of an ip_hdr::tos field.
///
/// @param      ip    Pointer to an ip_hdr.
///
/// @return     ECN bits.
///
#define ip_ecn(ip) (uint8_t)(((const struct ip_hdr *)(ip))->tos & 0x02)


/// Return a pointer to the payload data of the IPv4 packet in a buffer. It is
/// up to the caller to ensure that the packet buffer does in fact contain an
/// IPv4 packet.
///
/// @param      buf   The buffer.
///
/// @return     Pointer to the first payload byte.
///
#define ip_data(buf) (eth_data(buf) + ip_hl((buf) + sizeof(struct eth_hdr)))


/// Calculates the length of the payload data for the given IPv4 header @p ip.
///
/// @param      ip    An ip_hdr.
///
/// @return     { description_of_the_return_value }
///
#define ip_data_len(ip) ((ntohs((ip)->len) - ip_hl(ip)))


/// Initialize the static fields in an IPv4 ip_hdr header.
///
/// @param      ip    Pointer to the ip_hdr to initialize.
///
#define ip_hdr_init(ip)                                                        \
    do {                                                                       \
        (ip)->vhl = (4 << 4) + 5;                                              \
        (ip)->tos = IP_ECN_ECT_0;                                              \
        (ip)->off = htons(IP_DF);                                              \
        (ip)->ttl = 64; /* XXX this should be configurable */                  \
        (ip)->p = IP_P_UDP;                                                    \
        (ip)->cksum = 0;                                                       \
    } while (0)


extern void __attribute__((nonnull)) ip_tx_with_rx_buf(struct w_engine * w,
                                                       const uint8_t p,
                                                       void * const buf,
                                                       const uint16_t len);

extern void __attribute__((nonnull))
ip_rx(struct w_engine * const w, struct netmap_ring * const r);

extern bool __attribute__((nonnull))
ip_tx(struct w_engine * const w, struct w_iov * const v, const uint16_t len);
