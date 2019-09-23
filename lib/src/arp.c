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


#ifdef __FreeBSD__
#include <sys/socket.h> // IWYU pragma: keep
#include <sys/types.h>  // IWYU pragma: keep
#endif

#include <arpa/inet.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/socket.h>

#include <warpcore/warpcore.h>

#include "arp.h"
#include "backend.h"
#include "eth.h"
#include "ip4.h"
#include "neighbor.h"


/// Modifies the ARP request in @p buf into a corresponding ARP reply, and sends
/// it. Helper function called by arp_rx().
///
/// @param      w     Backend engine.
/// @param      buf   Buffer containing an incoming ARP request inside an
///                   Ethernet frame
/// @param[in]  ip    IPv4 address to send ARP reply for.
///
static void __attribute__((nonnull
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 8)
                           ,
                           no_sanitize("alignment")
#endif
                               ))
arp_is_at(struct w_engine * const w, uint8_t * const buf, const uint32_t ip)
{
    // grab iov for reply
    struct w_iov * const v = w_alloc_iov_base(w);
    if (unlikely(v == 0)) {
        warn(CRT, "no more bufs; ARP reply not sent");
        return;
    }
    struct arp_hdr * const reply = (void *)eth_data(v->base);

    // construct ARP header
    const struct arp_hdr * const req = (void *)eth_data(buf);

    reply->hrd = ARP_HRD_ETHER;
    reply->pro = ETH_TYPE_IP4;
    reply->hln = ETH_LEN;
    reply->pln = IP4_LEN;
    reply->op = ARP_OP_REPLY;
    reply->sha = w->mac;
    reply->spa = ip;
    reply->tha = req->sha;
    reply->tpa = req->spa;

    warn(NTE, "ARP reply %s is at %s",
         inet_ntop(AF_INET, &reply->spa, (char[IP4_STRLEN]){""}, IP4_STRLEN),
         eth_ntoa(&reply->sha, (char[ETH_STRLEN]){""}));

    // send the Ethernet packet
    struct eth_hdr * const eth = (void *)v->base;
    eth->dst = req->sha;
    eth->src = w->mac;
    eth->type = ETH_TYPE_ARP;

    // now send the packet, and make sure it went out before returning
    const uint32_t orig_idx = v->idx;
    v->len = sizeof(*reply);
    eth_tx(v);
    do {
        w_nanosleep(100 * NS_PER_US);
        w_nic_tx(w);
    } while (v->idx != orig_idx);
    sq_insert_head(&w->iov, v, next);
}


void
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 8)
    __attribute__((no_sanitize("alignment")))
#endif
    arp_who_has(struct w_engine * const w, const uint32_t addr)
{
    // grab a spare buffer
    struct w_iov * const v = w_alloc_iov_base(w);
    if (unlikely(v == 0)) {
        warn(CRT, "no more bufs; neighbor request not sent");
        return;
    }

    // pointers to the start of the various headers
    struct eth_hdr * const eth = (void *)v->base;
    struct arp_hdr * const arp = (void *)eth_data(v->base);

    // set Ethernet header fields
    eth->dst = (struct eth_addr){ETH_ADDR_BCAST};
    eth->src = w->mac;
    eth->type = ETH_TYPE_ARP;

    // set ARP header fields
    arp->hrd = ARP_HRD_ETHER;
    arp->pro = ETH_TYPE_IP4;
    arp->hln = ETH_LEN;
    arp->pln = IP4_LEN;
    arp->op = ARP_OP_REQUEST;
    arp->sha = w->mac;
    arp->spa = w->ifaddr[w->addr4_pos].addr.ip4;
    arp->tha = (struct eth_addr){ETH_ADDR_NONE};
    arp->tpa = addr;

    warn(NTE, "ARP request who has %s tell %s",
         inet_ntop(AF_INET, &arp->tpa, (char[IP4_STRLEN]){""}, IP4_STRLEN),
         inet_ntop(AF_INET, &arp->spa, (char[IP4_STRLEN]){""}, IP4_STRLEN));

    // now send the packet, and make sure it went out before returning
    const uint32_t orig_idx = v->idx;
    v->len = sizeof(*arp);
    eth_tx(v);
    do {
        w_nanosleep(100 * NS_PER_US);
        w_nic_tx(w);
    } while (v->idx != orig_idx);
    sq_insert_head(&w->iov, v, next);
}


/// Receive an ARP packet, and react to it. This function parses an incoming ARP
/// packet contained in an Ethernet frame. For incoming ARP requests for the
/// local interface, respond appropriately. For incoming ARP replies, updates
/// the information in the w_sock structures of all open connections, as needed.
///
/// @param      w     Backend engine.
/// @param      buf   Incoming packet.
///
void
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 8)
    __attribute__((no_sanitize("alignment")))
#endif
    arp_rx(struct w_engine * const w, uint8_t * const buf)
{
    const struct arp_hdr * const arp = (const void *)eth_data(buf);

    if (arp->hrd != ARP_HRD_ETHER || arp->hln != ETH_LEN) {
        warn(INF, "unhandled ARP hardware format %d with len %d",
             bswap16(arp->hrd), arp->hln);
        return;
    }

    if (arp->pro != ETH_TYPE_IP4 || arp->pln != IP4_LEN) {
        warn(INF, "unhandled ARP protocol format %d with len %d",
             bswap16(arp->pro), arp->pln);
        return;
    }

    switch (arp->op) {
    case ARP_OP_REQUEST:
        warn(NTE, "ARP request who has %s tell %s",
             inet_ntop(AF_INET, &arp->tpa, (char[IP4_STRLEN]){""}, IP4_STRLEN),
             inet_ntop(AF_INET, &arp->spa, (char[IP4_STRLEN]){""}, IP4_STRLEN));

        if (likely(is_my_ip4(w, arp->tpa, false) != UINT16_MAX))
            arp_is_at(w, buf, arp->tpa);
        else
            warn(WRN, "ignoring ARP request not asking for us");
        break;

    case ARP_OP_REPLY:;
        warn(NTE, "ARP reply %s is at %s",
             inet_ntop(AF_INET, &arp->spa, (char[IP4_STRLEN]){""}, IP4_STRLEN),
             eth_ntoa(&arp->sha, (char[ETH_STRLEN]){""}));

        // // check if any socket has an IP address matching this ARP
        // // reply, and if so, change its destination MAC
        // const struct w_addr tpa_mask =
        // struct w_sock * s;
        // kh_foreach_value(&w->sock, s, {
        //     const struct w_ifaddr * const wa = &w->ifaddr[s->tup.src_idx];

        //     if (wa->addr.af != AF_INET)
        //         continue;

        //     const w_addr mask =
        //         ((const struct sockaddr_in *)&wa->mask)->sin_addr.s_addr;

        //     if ( // is local-net socket and ARP src IP matches its dst
        //         ((mk_net(arp->tpa, mask) == mk_net(wa->addr.ip4, mask) &&
        //           arp->spa == wa->addr.ip4)) ||
        //         // or non-local socket and ARP src IP matches router
        //         (memcmp(&s->w->rip, &arp->sha, sizeof(arp->sha)) == 0)) {
        //         warn(NTE, "updating socket on local port %u with %s for %s",
        //              bswap16(s->tup.sport), eth_ntoa(&arp->sha,
        //              (char[ETH_STRLEN]){""})), inet_ntop(AF_INET,
        //              &arp->spa, ip_str, IP4_STRLEN));
        //         s->dmac = arp->sha;
        //     }
        // });
        break;

    default:
        warn(INF, "unhandled ARP operation %d", bswap16(arp->op));
        return;
    }

    neighbor_update(w, &(struct w_addr){.af = AF_INET, .ip4 = arp->spa},
                    arp->sha);
}
