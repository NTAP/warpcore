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

// IWYU pragma: no_include <net/netmap.h>

#include <arpa/inet.h>
#include <net/netmap_user.h>
#include <string.h>

#ifdef __APPLE__
#include <sys/types.h>
#endif

#include <warpcore/warpcore.h>

#include "arp.h"
#include "eth.h"
#include "ip.h"


#ifndef HAVE_ETHER_NTOA_R
#include <stdio.h>

char * ether_ntoa_r(const struct ether_addr * const addr, char * const buf)
{
    sprintf(buf, "%x:%x:%x:%x:%x:%x", addr->ether_addr_octet[0],
            addr->ether_addr_octet[1], addr->ether_addr_octet[2],
            addr->ether_addr_octet[3], addr->ether_addr_octet[4],
            addr->ether_addr_octet[5]);
    return buf;
}
#endif


/// Receive an Ethernet frame. This is the lowest-level RX function, called for
/// each new inbound frame from w_rx(). Dispatches the frame to either ip_rx()
/// or arp_rx(), based on its EtherType.
///
/// @param      w     Backend engine.
/// @param      s     Currently active netmap RX slot.
/// @param      buf   Incoming packet.
///
void eth_rx(struct w_engine * const w,
            struct netmap_slot * const s,
            uint8_t * const buf)
{
    struct eth_hdr * const eth = (void *)buf;
    if (unlikely(s->len < ETHER_ADDR_LEN)) {
#ifndef FUZZING
        warn(ERR, "buf len %u < eth hdr len", s->len);
#endif
        return;
    }

#if !defined(NDEBUG) && !defined(FUZZING)
    char src[ETH_ADDR_STRLEN];
    char dst[ETH_ADDR_STRLEN];
    warn(DBG, "Eth %s -> %s, type %d, len %d", ether_ntoa_r(&eth->src, src),
         ether_ntoa_r(&eth->dst, dst), ntohs(eth->type), s->len);
#endif

    // make sure the packet is for us (or broadcast)
    if (unlikely((memcmp(&eth->dst, &w->mac, ETHER_ADDR_LEN) != 0) &&
                 (memcmp(&eth->dst, "\xff\xff\xff\xff\xff\xff",
                         ETHER_ADDR_LEN) != 0))) {
#ifndef FUZZING
        warn(INF, "Ethernet packet to %s not destined to us (%s); ignoring",
             ether_ntoa_r(&eth->dst, dst), ether_ntoa_r(&w->mac, src));
#endif
        return;
    }
    if (likely(eth->type == ETH_TYPE_IP))
        ip_rx(w, s, buf);
    else if (eth->type == ETH_TYPE_ARP)
        arp_rx(w, buf);
#ifndef FUZZING
    else
        warn(INF, "unhandled ethertype 0x%04x", ntohs(eth->type));
#endif
}
