// SPDX-License-Identifier: BSD-2-Clause
//
// Copyright (c) 2014-2022, NetApp, Inc.
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

#include <stdint.h>

#include <warpcore/warpcore.h>


/// A representation of an ARP header; see
/// [RFC826](https://tools.ietf.org/html/rfc826).
/// This representation is limited to making requests for IPv4 over Ethernet,
/// which is sufficient for warpcore.
///
struct arp_hdr {
    /// Format of the hardware address. Will always be ARP_HRD_ETHER for
    /// Ethernet.
    uint16_t hrd;

    /// Format of the protocol address. Will always be ETH_TYPE_IP for IPv4
    /// in warpcore.
    uint16_t pro;

    /// Length of the hardware address. Will always be ETH_LEN for
    /// Ethernet.
    uint8_t hln;

    /// Length of the protocol address. Will always be IP_LEN for IPv4
    /// in warpcore.
    uint8_t pln;

    /// ARP operation. Either ARP_OP_REQUEST or ARP_OP_REPLY.
    uint16_t op;

    /// The sender hardware (i.e., Ethernet) address of the ARP operation.
    ///
    struct eth_addr sha;

    /// The sender protocol (i.e., IPv4) address of the ARP operation.
    ///
    uint32_t spa __attribute__((packed));

    /// The target hardware (i.e., Ethernet) address of the ARP operation.
    ///
    struct eth_addr tha;

    /// The target protocol (i.e., IPv4) address of the ARP operation.
    ///
    uint32_t tpa;
} __attribute__((aligned(1)));


#define ARP_HRD_ETHER 0x0100  ///< Ethernet hardware address format.
#define ARP_OP_REQUEST 0x0100 ///< ARP operation, request to resolve address.
#define ARP_OP_REPLY 0x0200   ///< ARP operation, response to request.


extern void __attribute__((nonnull))
arp_rx(struct w_engine * w, uint8_t * const buf);

extern void __attribute__((nonnull))
arp_who_has(struct w_engine * const w, const uint32_t addr);
