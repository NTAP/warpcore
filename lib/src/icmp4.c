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

#include <warpcore/warpcore.h>

#include <stdint.h>
#include <string.h>
#include <sys/param.h>
#include <sys/socket.h>

#include <net/netmap.h>

#include "backend.h"
#include "eth.h"
#include "icmp4.h"
#include "in_cksum.h"
#include "ip4.h"

#ifndef NDEBUG
#include "udp.h"
#endif


/// Make an ICMPv4 message with the given @p type and @p code based on the
/// received packet in @p buf.
///
/// @param      w     Backend engine.
/// @param[in]  type  The ICMPv4 type to send.
/// @param[in]  code  The ICMPv4 code to send.
/// @param[in]  buf   The received packet to send the ICMPv4 message for.
///
void
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 8)
    __attribute__((no_sanitize("alignment")))
#endif
    icmp4_tx(struct w_engine * const w,
             const uint8_t type,
             const uint8_t code,
             uint8_t * const buf)
{
    struct w_iov * const v = w_alloc_iov_base(w);
    if (unlikely(v == 0)) {
        warn(CRT, "no more bufs; ICMPv4 not sent (type %d, code %d)", type,
             code);
        return;
    }

    // construct an ICMPv4 header and set the fields
    struct icmp4_hdr * const dst_icmp = (void *)ip4_data(v->base);
    dst_icmp->type = type;
    dst_icmp->code = code;
    rwarn(INF, 10, "sending ICMPv4 type %d, code %d", type, code);

    struct ip4_hdr * const src_ip = (void *)eth_data(buf);
    uint8_t * data = eth_data(buf);
    uint16_t data_len = MIN(bswap16(src_ip->len), w->mtu);

    switch (type) {
    case ICMP4_TYPE_ECHOREPLY:;
        const struct icmp4_hdr * const src_icmp = (const void *)ip4_data(buf);
        dst_icmp->id = src_icmp->id;
        dst_icmp->seq = src_icmp->seq;

        // copy payload data from echo request
        const uint16_t hlen = ip4_hl(src_ip->vhl) + sizeof(*src_icmp);
        data += hlen;
        data_len -= hlen;
        break;

    case ICMP4_TYPE_UNREACH:
        // TODO: implement RFC4884
        dst_icmp->id = dst_icmp->seq = 0;

        // copy IP hdr + 64 bits of the original IP packet as the ICMPv4 payload
        data_len = ip4_hl(src_ip->vhl) + 8;
        break;

    default:
        die("don't know how to send ICMPv4 type %d", type);
    }

    // copy the required data to the reply
    memcpy((uint8_t *)dst_icmp + sizeof(*dst_icmp), data, data_len);

    // calculate the new ICMPv4 checksum
    dst_icmp->cksum = 0;
    dst_icmp->cksum = ip_cksum(dst_icmp, sizeof(*dst_icmp) + data_len);

    // construct an IPv4 header
    struct ip4_hdr * const dst_ip = (void *)eth_data(v->base);
    v->wv_af = AF_INET;
    v->wv_ip4 = src_ip->src;
    v->len = sizeof(*dst_icmp) + data_len;
    dst_ip->p = IP_P_ICMP;
    mk_ip4_hdr(v, 0);

    // set the Ethernet header
    const struct eth_hdr * const src_eth = (const void *)buf;
    struct eth_hdr * const dst_eth = (void *)v->base;
    dst_eth->dst = src_eth->src;
    dst_eth->src = w->mac;
    dst_eth->type = ETH_TYPE_IP4;

    eth_tx_and_free(v);
}


/// Analyze an inbound ICMPv4 packet and react to it. Called from ip_rx() for
/// all inbound ICMPv4 packets.
///
/// Currently only responds to ICMPv4 echo packets.
///
/// The Ethernet frame to operate on is in the current netmap lot of the
/// indicated RX ring.
///
/// @param      w     Backend engine.
/// @param      s     Currently active netmap RX slot.
/// @param      buf   Incoming packet.
///
void icmp4_rx(struct w_engine * const w,
              struct netmap_slot * const s,
              uint8_t * const buf)
{
    const struct icmp4_hdr * const icmp = (void *)ip4_data(buf);
    rwarn(DBG, 10, "received ICMPv4 type %d, code %d", icmp->type, icmp->code);

    // validate the ICMPv4 checksum
    struct ip4_hdr * const ip = (void *)eth_data(buf);
    const uint16_t icmp4_len = MIN(
        ip4_data_len(ip), s->len - sizeof(struct eth_hdr) - ip4_hl(ip->vhl));

    if (ip_cksum(icmp, icmp4_len) != 0) {
        warn(WRN, "invalid ICMPv4 checksum, received 0x%04x",
             bswap16(icmp->cksum));
        return;
    }

    switch (icmp->type) {
    case ICMP4_TYPE_ECHO:
        // send an echo reply
        icmp4_tx(w, ICMP4_TYPE_ECHOREPLY, 0, buf);
        break;
    case ICMP4_TYPE_UNREACH:
#ifndef NDEBUG
        switch (icmp->code) {
        case ICMP4_UNREACH_PROTOCOL:
            rwarn(WRN, 10, "received ICMPv4 protocol %d unreachable",
                  ((const struct ip4_hdr * const)(
                       const void *)((const uint8_t *)icmp + sizeof(*icmp)))
                      ->p);
            break;
        case ICMP4_UNREACH_PORT:
            rwarn(WRN, 10, "received ICMPv4 IP proto %d port %d unreachable",
                  ((const struct ip4_hdr * const)(
                       const void *)((const uint8_t *)icmp + sizeof(*icmp)))
                      ->p,
                  bswap16(((const struct udp_hdr * const)(
                               const void *)((const uint8_t *)icmp +
                                             sizeof(*icmp) + ip4_hl(ip->vhl)))
                              ->dport));
            break;

        default:
            rwarn(WRN, 10, "unhandled ICMPv4 code %d", icmp->code);
        }
#endif
        break;

    default:
        rwarn(WRN, 10, "unhandled ICMPv4 type %d", icmp->type);
    }
}
