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
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include <warpcore/warpcore.h>

#include "arp.h"
#include "backend.h"
#include "eth.h"
#include "ip.h"


/// Find the ARP cache entry associated with IPv4 address @p ip.
///
/// @param      w     Backend engine.
/// @param[in]  ip    IPv4 address to look up in ARP cache.
///
/// @return     Pointer to arp_entry of @p ip, or zero.
///
static struct arp_entry * __attribute__((nonnull))
arp_cache_find(struct w_engine * w, const uint32_t ip)
{
    const khiter_t k = kh_get(arp_cache, &w->b->arp_cache, ip);
    if (unlikely(k == kh_end(&w->b->arp_cache)))
        return 0;
    return kh_val(&w->b->arp_cache, k);
}


/// Update the MAC address associated with IPv4 address @p ip in the ARP cache.
///
/// @param      w     Backend engine.
/// @param[in]  ip    IPv4 address to update the ARP cache for.
/// @param[in]  mac   New Ethernet MAC address of @p ip.
///
void arp_cache_update(struct w_engine * w,
                      const uint32_t ip,
                      const struct ether_addr mac)
{
    struct arp_entry * a = arp_cache_find(w, ip);
    if (unlikely(a == 0)) {
        a = calloc(1, sizeof(*a));
        ensure(a, "cannot allocate arp_entry");
        int ret;
        const khiter_t k = kh_put(arp_cache, &w->b->arp_cache, ip, &ret);
        ensure(ret >= 1, "inserted");
        kh_val(&w->b->arp_cache, k) = a;
    }
    a->mac = mac;
#ifndef NDEBUG
    char ip_str[IP4_ADDR_STRLEN];
    inet_ntop(AF_INET, &ip, ip_str, IP4_ADDR_STRLEN);
    warn(INF, "ARP cache entry: %s is at %s", ip_str, ether_ntoa(&mac));
#endif
}


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
    struct arp_hdr * const req = (void *)eth_data(buf);

    reply->hrd = bswap16(ARP_HRD_ETHER);
    reply->pro = ETH_TYPE_IP;
    reply->hln = ETHER_ADDR_LEN;
    reply->pln = IP4_ADDR_LEN;
    reply->op = bswap16(ARP_OP_REPLY);
    reply->sha = w->mac;
    reply->spa = ip;
    reply->tha = req->sha;
    reply->tpa = req->spa;

#ifndef NDEBUG
    char ip_str[IP4_ADDR_STRLEN];
    inet_ntop(AF_INET, &reply->spa, ip_str, IP4_ADDR_STRLEN);
    warn(NTE, "ARP reply %s is at %s", ip_str, ether_ntoa(&reply->sha));
#endif

    // send the Ethernet packet
    struct eth_hdr * const eth = (void *)v->base;
    eth->dst = req->sha;
    eth->src = w->mac;
    eth->type = ETH_TYPE_ARP;

    // now send the packet, and make sure it went out before returning it
    const uint32_t orig_idx = v->idx;
    eth_tx(v, sizeof(*reply));
    do {
#ifndef FUZZING
        w_nanosleep(100 * NS_PER_US);
#endif
        w_nic_tx(w);
    } while (v->idx != orig_idx);
    sq_insert_head(&w->iov, v, next);
}


/// Return the Ethernet MAC address for target IP address @p dip. If there is no
/// entry in the ARP cache for the Ethernet MAC address corresponding to IPv4
/// address @p dip, this function will block while attempting to resolve the
/// address.
///
/// @param      w     Backend engine.
/// @param[in]  dip   IP address that is the target of the ARP request
///
/// @return     Ethernet MAC address of @p dip.
///
struct ether_addr
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 8)
    __attribute__((no_sanitize("alignment")))
#endif
    arp_who_has(struct w_engine * const w, const uint32_t dip)
{
    struct arp_entry * a = arp_cache_find(w, dip);
    while (unlikely(a == 0)) {
#ifndef NDEBUG
        char ip_str[IP4_ADDR_STRLEN];
        inet_ntop(AF_INET, &dip, ip_str, IP4_ADDR_STRLEN);
        warn(INF, "no ARP entry for %s, sending query", ip_str);
#endif

        // grab a spare buffer
        struct w_iov * const v = w_alloc_iov_base(w);
        if (unlikely(v == 0)) {
            warn(CRT, "no more bufs; ARP request not sent");
            return (struct ether_addr){{0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
        }

        // pointers to the start of the various headers
        struct eth_hdr * const eth = (void *)v->base;
        struct arp_hdr * const arp = (void *)eth_data(v->base);

        // set Ethernet header fields
        eth->dst = (struct ether_addr){{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
        eth->src = w->mac;
        eth->type = ETH_TYPE_ARP;

        // set ARP header fields
        arp->hrd = bswap16(ARP_HRD_ETHER);
        arp->pro = ETH_TYPE_IP;
        arp->hln = ETHER_ADDR_LEN;
        arp->pln = IP4_ADDR_LEN;
        arp->op = bswap16(ARP_OP_REQUEST);
        arp->sha = w->mac;
        arp->spa = ((struct sockaddr_in *)&w->addr[w->addr4_pos].addr)
                       ->sin_addr.s_addr;
        memset(&arp->tha, 0, ETHER_ADDR_LEN);
        arp->tpa = dip;

#ifndef NDEBUG
        char tpa[IP4_ADDR_STRLEN];
        char spa[IP4_ADDR_STRLEN];
        inet_ntop(AF_INET, &arp->tpa, tpa, IP4_ADDR_STRLEN);
        inet_ntop(AF_INET, &arp->spa, spa, IP4_ADDR_STRLEN);
        warn(NTE, "ARP request who has %s tell %s", tpa, spa);
#endif

        // now send the packet, and make sure it went out before returning it
        const uint32_t orig_idx = v->idx;
        eth_tx(v, sizeof(*arp));
        do {
#ifndef FUZZING
            w_nanosleep(100 * NS_PER_US);
#endif
            w_nic_tx(w);
        } while (v->idx != orig_idx);
        sq_insert_head(&w->iov, v, next);

        // wait until packets have been received, then handle them
        w_nic_rx(w, 1 * NS_PER_S);

        // check if we can now resolve dip
        a = arp_cache_find(w, dip);
    }
    return a->mac;
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
    const uint16_t hrd = bswap16(arp->hrd);

    if (hrd != ARP_HRD_ETHER || arp->hln != ETHER_ADDR_LEN) {
#ifndef FUZZING
        warn(INF, "unhandled ARP hardware format %d with len %d", hrd,
             arp->hln);
#endif
        return;
    }

    if (arp->pro != ETH_TYPE_IP || arp->pln != IP4_ADDR_LEN) {
#ifndef FUZZING
        warn(INF, "unhandled ARP protocol format %d with len %d",
             bswap16(arp->pro), arp->pln);
#endif
        return;
    }

    const uint16_t op = bswap16(arp->op);
    switch (op) {
    case ARP_OP_REQUEST: {
#ifndef NDEBUG
        char tpa[IP4_ADDR_STRLEN];
        char spa[IP4_ADDR_STRLEN];
        inet_ntop(AF_INET, &arp->tpa, tpa, IP4_ADDR_STRLEN);
        inet_ntop(AF_INET, &arp->spa, spa, IP4_ADDR_STRLEN);
        warn(NTE, "ARP request who has %s tell %s", tpa, spa);
#endif
        if (ip4_addr_idx(w, arp->tpa) >= 0)
            arp_is_at(w, buf, arp->tpa);
        else
            warn(WRN, "ignoring ARP request not asking for us");

        // opportunistically store the ARP mapping
        arp_cache_update(w, arp->spa, arp->sha);
        break;
    }

    case ARP_OP_REPLY: {
#ifndef NDEBUG
        char ip_str[IP4_ADDR_STRLEN];
        warn(NTE, "ARP reply %s is at %s",
             inet_ntop(AF_INET, &arp->spa, ip_str, IP4_ADDR_STRLEN),
             ether_ntoa(&arp->sha));
#endif
        arp_cache_update(w, arp->spa, arp->sha);

        // check if any socket has an IP address matching this ARP
        // reply, and if so, change its destination MAC
        struct w_sock * s;
        kh_foreach_value(&w->sock, s, {
            const struct w_ifaddr * const wa = &w->addr[s->tup.src_idx];

            if (wa->addr.ss_family != AF_INET)
                continue;

            const uint32_t dip =
                ((const struct sockaddr_in *)&wa->addr)->sin_addr.s_addr;
            const uint32_t mask =
                ((const struct sockaddr_in *)&wa->mask)->sin_addr.s_addr;

            if ( // is local-net socket and ARP src IP matches its dst
                ((mk_net(arp->tpa, mask) == mk_net(dip, mask) &&
                  arp->spa == dip)) ||
                // or non-local socket and ARP src IP matches router
                (memcmp(&s->w->rip, &arp->sha, sizeof(arp->sha)) == 0)) {
                warn(NTE, "updating socket on local port %u with %s for %s",
                     bswap16(s->tup.sport), ether_ntoa(&arp->sha),
                     inet_ntop(AF_INET, &arp->spa, ip_str, IP4_ADDR_STRLEN));
                s->dmac = arp->sha;
            }
        });
        break;
    }

    default:
        warn(INF, "unhandled ARP operation %d", op);
    }
}


/// Free the ARP cache entries associated with engine @p w.
///
/// @param[in]  w     Backend engine.
///
void free_arp_cache(struct w_engine * const w)
{
    struct arp_entry * a;
    kh_foreach_value(&w->b->arp_cache, a, { free(a); });
    kh_release(arp_cache, &w->b->arp_cache);
}
