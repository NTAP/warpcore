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

// IWYU pragma: no_include <net/netmap.h>
#include <arpa/inet.h>
#include <net/netmap_user.h> // IWYU pragma: keep
#include <stdint.h>
#include <string.h>
#include <sys/param.h>
#include <unistd.h>

#ifndef __linux__
#include <netinet/in.h>
#endif

#include <warpcore/warpcore.h>

#include "eth.h"
#include "icmp.h"
#include "ip.h"
#include "udp.h"


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
    struct w_iov * const v = w_alloc_iov(w, 0);
    if (unlikely(v == 0)) {
        warn(crit, "no more bufs; ICMP not sent (type %d, code %d)", type,
             code);
        return;
    }

    // construct an ICMP header and set the fields
    struct icmp_hdr * const dst_icmp = (void *)ip_data(v->buf);
    dst_icmp->type = type;
    dst_icmp->code = code;
    warn(notice, "ICMP type %d, code %d", type, code);

    const struct ip_hdr * const src_ip = (const void *)eth_data(buf);
    uint8_t * data = eth_data(buf);
    uint16_t data_len = MIN(ntohs(src_ip->len),
                            w_iov_max_len(w, v) - sizeof(struct eth_hdr) -
                                sizeof(struct ip_hdr));

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
    dst_icmp->cksum = in_cksum(dst_icmp, sizeof(*dst_icmp) + data_len);

    // construct an IPv4 header
    struct ip_hdr * const dst_ip = (void *)eth_data(v->buf);
    ip_hdr_init(dst_ip);
    dst_ip->src = w->ip;
    dst_ip->dst = src_ip->src;
    dst_ip->p = IP_P_ICMP;

    // set the Ethernet header
    const struct eth_hdr * const src_eth = (const void *)buf;
    struct eth_hdr * const dst_eth = (void *)v->buf;
    dst_eth->dst = src_eth->src;
    dst_eth->src = w->mac;
    dst_eth->type = ETH_TYPE_IP;

    // now send the packet, and make sure it went out before returning it
    const uint32_t orig_idx = v->idx;
    ip_tx(w, v, sizeof(*dst_icmp) + data_len);
    while (v->idx != orig_idx) {
        usleep(100);
        w_nic_tx(w);
    }
    STAILQ_INSERT_HEAD(&w->iov, v, next);
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
/// @param      r     Currently active netmap RX ring.
///
void icmp_rx(struct w_engine * const w, struct netmap_ring * const r)
{
    uint8_t * const buf = (void *)NETMAP_BUF(r, r->slot[r->cur].buf_idx);
    struct icmp_hdr * const icmp = (void *)ip_data(buf);
    warn(notice, "ICMP type %d, code %d", icmp->type, icmp->code);

    // validate the ICMP checksum
    const struct ip_hdr * const ip = (const void *)eth_data(buf);
    const uint16_t icmp_len = MIN(ip_data_len(ip),
                                  r->slot[r->cur].len - sizeof(struct eth_hdr) -
                                      sizeof(struct ip_hdr));

    if (in_cksum(icmp, icmp_len) != 0) {
        warn(warn, "invalid ICMP checksum, received 0x%04x",
             ntohs(icmp->cksum));
        return;
    }

    switch (icmp->type) {
    case ICMP_TYPE_ECHO:
        // send an echo reply
        icmp_tx(w, ICMP_TYPE_ECHOREPLY, 0, buf);
        break;
    case ICMP_TYPE_UNREACH: {
#ifndef NDEBUG
        const struct ip_hdr * const payload_ip =
            (const void *)(ip_data(buf) + sizeof(*icmp) + 4);
#endif
        switch (icmp->code) {
        case ICMP_UNREACH_PROTOCOL:
            warn(warn, "ICMP protocol %d unreachable", payload_ip->p);
            break;
        case ICMP_UNREACH_PORT: {
#ifndef NDEBUG
            const struct udp_hdr * const payload_udp =
                (const void *)((const uint8_t *)ip + ip_hl(ip));
            warn(warn, "ICMP IP proto %d port %d unreachable", payload_ip->p,
                 ntohs(payload_udp->dport));
#endif
            break;
        }
        default:
            die("unhandled ICMP code %d", icmp->code);
            break;
        }
        break;
    }
    default:
        die("unhandled ICMP type %d", icmp->type);
        break;
    }
}
