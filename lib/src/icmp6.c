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

#include <warpcore/warpcore.h>

#include <arpa/inet.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/param.h>
#include <sys/socket.h>

// IWYU pragma: no_include <net/netmap.h>
#include <net/netmap_user.h> // IWYU pragma: keep

#include "backend.h"
#include "eth.h"
#include "icmp6.h"
#include "in_cksum.h"
#include "ip4.h"
#include "ip6.h"
#include "neighbor.h"
#include "udp.h"


static void __attribute__((nonnull
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 8)
                           ,
                           no_sanitize("alignment")
#endif
                               )) mk_icmp6_pkt_hdrs(struct w_iov * const v)
{
    struct eth_hdr * const eth = (void *)v->base;
    struct ip6_hdr * const ip = (void *)eth_data(v->base);
    struct icmp6_hdr * const icmp = (void *)(eth_data(v->base) + sizeof(*ip));

    // set common bits of IPv6 header
    v->saddr.addr.af = AF_INET6;
    ip->next_hdr = IP_P_ICMP6;
    mk_ip6_hdr(v, 0); // adds sizeof(*ip) to v->len

    // calculate the new ICMPv6 checksum
    icmp->cksum = 0;
    icmp->cksum = payload_cksum(ip, v->len);

    // set common bits of the Ethernet header
    eth->src = v->w->mac;
    eth->type = ETH_TYPE_IP6;
}


void
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 8)
    __attribute__((no_sanitize("alignment")))
#endif
    icmp6_nsol(struct w_engine * const w, const uint128_t addr)
{
    // grab a spare buffer
    struct w_iov * const v = w_alloc_iov_base(w);
    if (unlikely(v == 0)) {
        warn(CRT, "no more bufs; neighbor request not sent");
        return;
    }

    // pointers to the start of the various headers
    struct eth_hdr * const eth = (void *)v->base;
    struct icmp6_hdr * const icmp =
        (void *)(eth_data(v->base) + sizeof(struct ip6_hdr));

    icmp->type = ICMP6_TYPE_NSOL;
    icmp->code = 0;
    icmp->id = 0x60; // Solicited+Override flags
    icmp->seq = 0;

    // we format the response here
    uint8_t * resp = (uint8_t *)icmp + sizeof(*icmp);
    memcpy(resp, &addr, sizeof(addr));
    resp += sizeof(uint128_t);
    *(resp++) = 0x01; // source link-layer address
    *(resp++) = 1;    // len in units of eight octets
    memcpy(resp, &w->mac, sizeof(w->mac));
    resp += sizeof(w->mac);

    warn(NTE, "neighbor solicitation, who has %s tell %s",
         inet_ntop(AF_INET6, &addr, (char[IP6_STRLEN]){""}, IP6_STRLEN),
         eth_ntoa(&w->mac, (char[ETH_STRLEN]){""}));

    v->len = (uint16_t)(resp - (uint8_t *)icmp);
    v->saddr.addr.ip6 = snmap_prefix | (addr & snmap_mask);
    mk_icmp6_pkt_hdrs(v);

    // set missing bits of the Ethernet header
    eth->dst = (struct eth_addr){ETH_ADDR_MCAST6};
    *(uint32_t *)(void *)(&eth->dst.addr[2]) = addr >> 96;

    eth_tx_and_free(v);
}


/// Make an ICMPv6 message with the given @p type and @p code based on the
/// received packet in @p buf.
///
/// @param      w     Backend engine.
/// @param[in]  type  The ICMPv6 type to send.
/// @param[in]  code  The ICMPv6 code to send.
/// @param[in]  buf   The received packet to send the ICMPv6 message for.
///
void
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 8)
    __attribute__((no_sanitize("alignment")))
#endif
    icmp6_tx(struct w_engine * const w,
             const uint8_t type,
             const uint8_t code,
             uint8_t * const buf)
{
    struct w_iov * const v = w_alloc_iov_base(w);
    if (unlikely(v == 0)) {
        warn(CRT, "no more bufs; ICMPv6 not sent (type %d, code %d)", type,
             code);
        return;
    }

    // construct an ICMPv6 header and set the fields
    struct icmp6_hdr * const dst_icmp = (void *)ip6_data(v->base);
    dst_icmp->type = type;
    dst_icmp->code = code;
    rwarn(INF, 10, "sending ICMPv6 type %d, code %d", type, code);

    const struct ip6_hdr * const src_ip = (void *)eth_data(buf);
    const uint8_t * data = ip6_data(buf);
    uint16_t data_len = MIN(bswap16(src_ip->len), w->mtu - sizeof(*src_ip));

    const struct eth_addr * sla = 0;
    switch (type) {
    case ICMP6_TYPE_NADV:
        dst_icmp->id = 0x60; // Solicited+Override flags
        dst_icmp->seq = 0;

        // check if there is a source link-address option
        if (data_len >= sizeof(*dst_icmp) + 8 && *(data++) == 1 &&
            *(data++) == 1)
            sla = (const struct eth_addr *)data;

        // we format the response here
        data = 0;
        uint8_t * resp = (uint8_t *)dst_icmp + sizeof(*dst_icmp);
        memcpy(resp, ip6_data(buf) + sizeof(*dst_icmp), sizeof(uint128_t));
        resp += sizeof(uint128_t);
        *(resp++) = 0x02; // target link-layer address
        *(resp++) = 1;    // len in units of eight octets
        memcpy(resp, &w->mac, sizeof(w->mac));
        resp += sizeof(w->mac);
        data_len = (uint16_t)(resp - (uint8_t *)dst_icmp -
                              (uint16_t)sizeof(*dst_icmp));

        warn(NTE, "neighbor advertisement, %s is at %s",
             inet_ntop(AF_INET6, ip6_data(buf) + sizeof(*dst_icmp),
                       (char[IP6_STRLEN]){""}, IP6_STRLEN),
             eth_ntoa(&w->mac, (char[ETH_STRLEN]){""}));
        break;

    case ICMP6_TYPE_ECHOREPLY:;
        const struct icmp6_hdr * const src_icmp = (const void *)data;
        dst_icmp->id = src_icmp->id;
        dst_icmp->seq = src_icmp->seq;

        // copy payload data from echo request
        data += sizeof(*src_icmp);
        data_len -= sizeof(*src_icmp);
        break;

    case ICMP6_TYPE_UNREACH:
        // TODO: implement RFC4884
        dst_icmp->id = dst_icmp->seq = 0;

        // copy IP hdr + 64 bits of the original IP packet as the ICMPv6 payload
        data_len = sizeof(*src_ip) + 8;
        break;

    default:
        die("don't know how to send ICMPv6 type %d", type);
    }

    if (data)
        // copy any required data to the reply
        memcpy((uint8_t *)dst_icmp + sizeof(*dst_icmp), data, data_len);

    v->len = sizeof(*dst_icmp) + data_len;
    memcpy(&v->saddr.addr.ip6, src_ip->src, sizeof(v->saddr.addr.ip6));
    mk_icmp6_pkt_hdrs(v);

    // set missing bits of the Ethernet header
    const struct eth_hdr * const src_eth = (const void *)buf;
    struct eth_hdr * const dst_eth = (void *)v->base;
    dst_eth->dst = sla ? *sla : src_eth->src;

    eth_tx_and_free(v);
}


/// Analyze an inbound ICMPv6 packet and react to it. Called from ip_rx() for
/// all inbound ICMPv6 packets.
///
/// Currently only responds to ICMPv6 echo packets.
///
/// The Ethernet frame to operate on is in the current netmap lot of the
/// indicated RX ring.
///
/// @param      w     Backend engine.
/// @param      s     Currently active netmap RX slot.
/// @param      buf   Incoming packet.
///
void
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 8)
    __attribute__((no_sanitize("alignment")))
#endif
    icmp6_rx(struct w_engine * const w,
             struct netmap_slot * const s,
             uint8_t * const buf)
{
    const struct icmp6_hdr * const icmp = (void *)ip6_data(buf);
    rwarn(DBG, 10, "received ICMPv6 type %d, code %d", icmp->type, icmp->code);

    // validate the ICMPv6 checksum
    const struct ip6_hdr * const ip = (void *)eth_data(buf);
    const uint16_t icmp_len =
        MIN(bswap16(ip->len),
            s->len - sizeof(struct eth_hdr) - sizeof(struct ip6_hdr));

    if (payload_cksum(ip, icmp_len + sizeof(*ip)) != 0) {
        warn(WRN, "invalid ICMPv6 checksum, received 0x%04x",
             bswap16(icmp->cksum));
        return;
    }

    switch (icmp->type) {
    case ICMP6_TYPE_NADV:;
        const uint8_t * data = (const uint8_t *)icmp + sizeof(*icmp);
        uint16_t data_len =
            MIN(bswap16(ip->len),
                s->len - sizeof(struct eth_hdr) - sizeof(*ip) - sizeof(*icmp));

        uint128_t target;
        memcpy(&target, data, sizeof(target));
        data += sizeof(target);

        // check if there is a source link-address option
        const struct eth_addr * sla = 0;
        if (data_len >= sizeof(target) + 8 && *(data++) == 1 && *(data++) == 1)
            sla = (const void *)data;

        const struct eth_hdr * src_eth = (const void *)buf;
        const struct w_addr addr = {.af = AF_INET6, .ip6 = target};
        warn(NTE, "neighbor advertisement, %s is at %s",
             w_ntop(&addr, (char[IP6_STRLEN]){""}, IP6_STRLEN),
             eth_ntoa(sla ? sla : &src_eth->src, (char[ETH_STRLEN]){""}));
        neighbor_update(w, &addr, sla ? *sla : src_eth->src);

        break;

    case ICMP6_TYPE_NSOL:
        data = (const uint8_t *)icmp + sizeof(*icmp);
        data_len = MIN(bswap16(ip->len), s->len - sizeof(struct eth_hdr) -
                                             sizeof(*ip) - sizeof(*icmp));

        memcpy(&target, data, sizeof(target));
        data += sizeof(target);

        // check if there is a source link-address option
        sla = 0;
        if (data_len >= sizeof(target) + 8 && *(data++) == 1 && *(data++) == 1)
            sla = (const void *)data;

        const struct w_addr t = {.af = AF_INET6, .ip6 = target};
        if (sla)
            warn(NTE, "neighbor solicitation, who has %s tell %s",
                 w_ntop(&t, (char[IP6_STRLEN]){""}, IP6_STRLEN),
                 eth_ntoa(sla, (char[ETH_STRLEN]){""}));
        else
            warn(NTE, "neighbor solicitation, who has %s",
                 w_ntop(&t, (char[IP6_STRLEN]){""}, IP6_STRLEN));

        if (is_my_ip6(w, target, false) != UINT16_MAX) {
            icmp6_tx(w, ICMP6_TYPE_NADV, 0, buf);

            // opportunistically store the ND mapping
            src_eth = (const void *)buf;
            memcpy(&target, &ip->src, sizeof(target)); // reuse target
            neighbor_update(w, &t, sla ? *sla : src_eth->src);
        } else
            rwarn(WRN, 10,
                  "received ICMPv6 neighbor solicitation for unknown address");
        break;

    case ICMP6_TYPE_ECHO:
        // send an echo reply
        icmp6_tx(w, ICMP6_TYPE_ECHOREPLY, 0, buf);
        break;

    case ICMP6_TYPE_UNREACH:
        switch (icmp->code) {
        case ICMP6_UNREACH_PORT:
            rwarn(WRN, 10, "received ICMPv6 IP proto %d port %d unreachable",
                  ((const struct ip6_hdr * const)(
                       const void *)((const uint8_t *)icmp + sizeof(*icmp)))
                      ->next_hdr,
                  bswap16(((const struct udp_hdr * const)(
                               const void *)((const uint8_t *)icmp +
                                             sizeof(*icmp) + sizeof(*ip)))
                              ->dport));
            break;

        default:
            rwarn(WRN, 10, "unhandled ICMPv6 code %d", icmp->code);
        }
        break;

    default:
        rwarn(WRN, 10, "unhandled ICMPv6 type %d", icmp->type);
    }
}
