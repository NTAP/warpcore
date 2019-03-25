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
#include <netinet/in.h>
#include <stdint.h>
#include <string.h>
#include <sys/param.h>
#include <sys/socket.h>

#ifndef FUZZING
#include <unistd.h>
#endif

// IWYU pragma: no_include <net/netmap.h>
#include <net/netmap_user.h> // IWYU pragma: keep

#include "backend.h"
#include "eth.h"
#include "icmp.h"
#include "in_cksum.h"
#include "ip.h"
#if !defined(NDEBUG) && DLEVEL >= WRN
#include "udp.h"
#endif


/// Make an ICMP message with the given @p type and @p code based on the
/// received packet in @p buf.
///
/// @param      w     Backend engine.
/// @param[in]  type  The ICMP type to send.
/// @param[in]  code  The ICMP code to send.
/// @param[in]  buf   The received packet to send the ICMP message for.
///
void icmp_tx(struct w_engine * const w,
             const uint8_t type,
             const uint8_t code,
             uint8_t * const buf)
{
    struct w_iov * const v = w_alloc_iov_base(w);
    if (unlikely(v == 0)) {
#ifndef FUZZING
        warn(CRT, "no more bufs; ICMP not sent (type %d, code %d)", type, code);
#endif
        return;
    }

    // construct an ICMP header and set the fields
    struct icmp_hdr * const dst_icmp = (void *)ip_data(v->base);
    dst_icmp->type = type;
    dst_icmp->code = code;
    rwarn(INF, 10, "sending ICMP type %d, code %d", type, code);

    struct ip_hdr * const src_ip = (void *)eth_data(buf);
    uint8_t * data = eth_data(buf);
    uint16_t data_len =
        MIN(ntohs(src_ip->len),
            w_iov_max_len(v) - sizeof(struct eth_hdr) - sizeof(struct ip_hdr));

    switch (type) {
    case ICMP_TYPE_ECHOREPLY: {
        const struct icmp_hdr * const src_icmp = (const void *)ip_data(buf);
        dst_icmp->id = src_icmp->id;
        dst_icmp->seq = src_icmp->seq;

        // copy payload data from echo request
        const uint16_t hlen = sizeof(*src_ip) + sizeof(*src_icmp);
        data += hlen;
        data_len -= hlen;
        break;
    }

    case ICMP_TYPE_UNREACH:
        // TODO: implement RFC4884
        dst_icmp->id = dst_icmp->seq = 0;

        // copy IP hdr + 64 bits of the original IP packet as the ICMP payload
        data_len = ip_hl(src_ip) + 8;
        break;

    default:
        die("don't know how to send ICMP type %d", type);
    }

    // copy the required data to the reply
    memcpy((uint8_t *)dst_icmp + sizeof(*dst_icmp), data, data_len);

    // calculate the new ICMP checksum
    dst_icmp->cksum = 0;
    dst_icmp->cksum = ip_cksum(dst_icmp, sizeof(*dst_icmp) + data_len);

    // construct an IPv4 header
    struct ip_hdr * const dst_ip = (void *)eth_data(v->base);
    struct sockaddr_in * const addr4 = (struct sockaddr_in *)&v->addr;
    addr4->sin_family = AF_INET;
    addr4->sin_addr.s_addr = src_ip->src;
    dst_ip->p = IP_P_ICMP;
    mk_ip_hdr(v, sizeof(*dst_icmp) + data_len, 0);

    // set the Ethernet header
    const struct eth_hdr * const src_eth = (const void *)buf;
    struct eth_hdr * const dst_eth = (void *)v->base;
    dst_eth->dst = src_eth->src;
    dst_eth->src = w->mac;
    dst_eth->type = ETH_TYPE_IP;

    // now send the packet, and make sure it went out before returning it
    const uint32_t orig_idx = v->idx;
    eth_tx(v, sizeof(*dst_ip) + sizeof(*dst_icmp) + data_len);
    do {
#ifndef FUZZING
        usleep(100);
#endif
        w_nic_tx(w);
    } while (v->idx != orig_idx);
    sq_insert_head(&w->iov, v, next);
}


/// Analyze an inbound ICMP packet and react to it. Called from ip_rx() for all
/// inbound ICMP packets.
///
/// Currently only responds to ICMP echo packets.
///
/// The Ethernet frame to operate on is in the current netmap lot of the
/// indicated RX ring.
///
/// @param      w     Backend engine.
/// @param      s     Currently active netmap RX slot.
/// @param      buf   Incoming packet.
///
void icmp_rx(struct w_engine * const w,
             struct netmap_slot * const s
#ifdef FUZZING
             __attribute__((unused
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 8)
                            ,
                            no_sanitize("alignment")
#endif
                                ))
#endif
             ,
             uint8_t * const buf)
{
    struct icmp_hdr * const icmp = (void *)ip_data(buf);
#ifndef FUZZING
    rwarn(DBG, 10, "received ICMP type %d, code %d", icmp->type, icmp->code);
#endif

#ifndef FUZZING
    // validate the ICMP checksum
    struct ip_hdr * const ip = (void *)eth_data(buf);
    const uint16_t icmp_len =
        MIN(ip_data_len(ip),
            s->len - sizeof(struct eth_hdr) - sizeof(struct ip_hdr));

    if (ip_cksum(icmp, icmp_len) != 0) {
        warn(WRN, "invalid ICMP checksum, received 0x%04x", ntohs(icmp->cksum));
        return;
    }
#endif

    switch (icmp->type) {
    case ICMP_TYPE_ECHO:
        // send an echo reply
        icmp_tx(w, ICMP_TYPE_ECHOREPLY, 0, buf);
        break;
    case ICMP_TYPE_UNREACH: {
#if !defined(NDEBUG) && DLEVEL >= WRN
        const struct ip_hdr * const payload_ip =
            (const void *)((uint8_t *)icmp + sizeof(*icmp));
#endif
        switch (icmp->code) {
        case ICMP_UNREACH_PROTOCOL:
#if !defined(NDEBUG) && DLEVEL >= WRN
            rwarn(WRN, 10, "received ICMP protocol %d unreachable",
                  payload_ip->p);
#endif
            break;
        case ICMP_UNREACH_PORT: {
#if !defined(NDEBUG) && DLEVEL >= WRN
            const struct udp_hdr * const payload_udp =
                (const void *)((const uint8_t *)payload_ip +
                               sizeof(*payload_ip));
            rwarn(WRN, 10, "received ICMP IP proto %d port %d unreachable",
                  payload_ip->p, ntohs(payload_udp->dport));
#endif
            break;
        }
        default:
#ifndef FUZZING
            rwarn(WRN, 10, "unhandled ICMP code %d", icmp->code)
#endif
                ;
        }
        break;
    }
    default:
#ifndef FUZZING
        rwarn(WRN, 10, "unhandled ICMP type %d", icmp->type)
#endif
            ;
    }
}
