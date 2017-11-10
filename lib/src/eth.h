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

#include <arpa/inet.h>
#include <net/ethernet.h>
#include <stdbool.h>
#include <stdint.h>

#include <warpcore/warpcore.h> // IWYU pragma: keep


struct netmap_ring;

#define ETH_TYPE_IP htons(0x0800)  ///< EtherType for IPv4.
#define ETH_TYPE_ARP htons(0x0806) ///< EtherType for ARP.

#define ETH_ADDR_STRLEN ETHER_ADDR_LEN * 3


/// An [Ethernet II MAC
/// header](https://en.wikipedia.org/wiki/Ethernet_frame#Ethernet_II).
///
struct eth_hdr {
    struct ether_addr dst; ///< Destination MAC address.
    struct ether_addr src; ///< Source MAC address.
    uint16_t type;         ///< EtherType of the payload data.
};


/// Return a pointer to the first data byte inside the Ethernet frame in @p buf.
///
/// @param      buf   The buffer to find data in.
///
/// @return     Pointer to the first data byte inside @p buf.
///
#define eth_data(buf) ((buf) + sizeof(struct eth_hdr))


extern void __attribute__((nonnull))
eth_tx_rx_cur(struct w_engine * w, void * const buf, const uint16_t len);

extern void __attribute__((nonnull))
eth_rx(struct w_engine * const w, struct netmap_ring * const r);

extern bool __attribute__((nonnull))
eth_tx(struct w_engine * const w, struct w_iov * const v, const uint16_t len);

#ifndef HAVE_ETHER_NTOA_R
extern char * __attribute__((nonnull))
ether_ntoa_r(const struct ether_addr * const addr, char * const buf);
#endif
