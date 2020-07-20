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
#include <sys/socket.h>

#include <warpcore/warpcore.h>

#ifdef WITH_NETMAP
struct netmap_slot;
#endif


/// An [Ethernet II MAC
/// header](https://en.wikipedia.org/wiki/Ethernet_frame#Ethernet_II).
///
struct eth_hdr {
    struct eth_addr dst; ///< Destination MAC address.
    struct eth_addr src; ///< Source MAC address.
    uint16_t type;       ///< EtherType of the payload data.
} __attribute__((aligned(1)));


#define ETH_TYPE_ARP 0x0608 ///< EtherType for ARP (network byte-order).
#define ETH_TYPE_IP4 0x0008 ///< EtherType for IPv4 (network byte-order).
#define ETH_TYPE_IP6 0xdd86 ///< EtherType for IPv6 (network byte-order).

#define ETH_ADDR_BCAST "\xff\xff\xff\xff\xff\xff"  ///< Broadcast MAC address.
#define ETH_ADDR_NONE "\x00\x00\x00\x00\x00\x00"   ///< Unset MAC address.
#define ETH_ADDR_MCAST6 "\x33\x33\x00\x00\x00\x00" ///< IPv6 multicast.


/// Return a pointer to the first data byte inside the Ethernet frame in @p buf.
///
/// @param      buf   The buffer to find data in.
///
/// @return     Pointer to the first data byte inside @p buf.
///
static inline __attribute__((always_inline, nonnull)) uint8_t *
eth_data(uint8_t * const buf)
{
    return buf + sizeof(struct eth_hdr);
}


#ifdef WITH_NETMAP
#include <net/netmap_user.h>

#include "neighbor.h"


extern bool __attribute__((nonnull)) eth_rx(struct w_engine * const w,
                                            struct netmap_slot * const s,
                                            uint8_t * const buf);

extern bool __attribute__((nonnull)) eth_tx(struct w_iov * const v);

extern void __attribute__((nonnull)) eth_tx_and_free(struct w_iov * const v);


static inline void __attribute__((nonnull))
mk_eth_hdr(const struct w_sock * const s, struct w_iov * const v)
{
    struct eth_hdr * const eth = (void *)v->base;

    if (w_connected(s))
        eth->dst = s->dmac;
    else {
        static struct w_addr last_addr = {0};
        static struct eth_addr last_mac = {{0}};

        if (likely(w_addr_cmp(&last_addr, &v->wv_addr)))
            eth->dst = last_mac;
        else {
            last_addr = v->wv_addr;
            eth->dst = last_mac = who_has(s->w, &v->wv_addr);
        }
    }

    eth->src = v->w->mac;
    eth->type = s->ws_af == AF_INET ? ETH_TYPE_IP4 : ETH_TYPE_IP6;
}

#endif
