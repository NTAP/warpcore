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

#include <warpcore/warpcore.h>

// IWYU pragma: no_include <net/netmap.h>
#include <arpa/inet.h>
#ifdef __FreeBSD__
#include <netinet/in.h>
#endif
#include <net/netmap_user.h> // IWYU pragma: keep
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/param.h>
#include <sys/types.h>

#include "arp.h"
#include "backend.h"
#include "eth.h"
#include "icmp.h"
#include "ip.h"
#include "udp.h"


#ifndef NDEBUG
/// Print a summary of the udp_hdr @p udp.
///
/// @param      udp   The udp_hdr to print.
///
#define udp_log(udp)                                                           \
    do {                                                                       \
        warn(DBG, "UDP :%d -> :%d, cksum 0x%04x, len %u", ntohs((udp)->sport), \
             ntohs((udp)->dport), ntohs((udp)->cksum), ntohs((udp)->len));     \
    } while (0)
#else
#define udp_log(udp)                                                           \
    do {                                                                       \
    } while (0)
#endif


// Receive a UDP packet.

/// Receive a UDP packet. Validates the UDP checksum and appends the payload
/// data to the corresponding w_sock. Also makes the receive timestamp and IPv4
/// flags available, via w_iov::ts and w_iov::flags, respectively.
///
/// The Ethernet frame to operate on is in the current netmap lot of the
/// indicated RX ring.
///
/// @param      w     Backend engine.
/// @param      r     Currently active netmap RX ring.
///
void udp_rx(struct w_engine * const w, struct netmap_ring * const r)
{
    uint8_t * const buf = (uint8_t *)NETMAP_BUF(r, r->slot[r->cur].buf_idx);
    const struct ip_hdr * const ip = (const void *)eth_data(buf);
    struct udp_hdr * const udp = (void *)ip_data(buf);
    const uint16_t udp_len =
        MIN(ntohs(udp->len), r->slot[r->cur].len - sizeof(struct eth_hdr) -
                                 sizeof(struct ip_hdr));
    udp_log(udp);

    if (udp->cksum) {
        // validate the checksum
        const uint16_t orig = udp->cksum;
        udp->cksum = in_pseudo(ip->src, ip->dst, htons(udp_len + ip->p));
        uint16_t cksum = in_cksum(udp, udp_len);
        cksum = cksum ? cksum : 0xffff; // make all ones; see RFC786
        udp->cksum = orig;
        if (unlikely(orig != cksum)) {
            warn(WRN, "invalid UDP checksum, received 0x%04x != 0x%04x",
                 ntohs(orig), ntohs(cksum));
            return;
        }
    }

    struct w_sock * const s = get_sock(w, udp->dport);
    if (unlikely(s == 0)) {
        // nobody bound to this port locally
        // send an ICMP unreachable reply, if this was not a broadcast
        if (ip->src)
            icmp_tx(w, ICMP_TYPE_UNREACH, ICMP_UNREACH_PORT, buf);
        return;
    }

    // grab an unused iov for the data in this packet
    //
    // XXX w_alloc_iov() does some (in this case) unneeded initialization;
    // determine if that overhead is a problem
    struct w_iov * const i = w_alloc_iov(w, 0, 0);
    if (unlikely(i == 0)) {
        warn(CRT, "no more bufs; UDP packet RX failed");
        return;
    }
    struct netmap_slot * const rxs = &r->slot[r->cur];

    warn(DBG, "swapping rx ring %u slot %d (buf %d) and spare buf %u",
         r->ringid, r->cur, rxs->buf_idx, i->idx);

    // remember index of this buffer
    const uint32_t tmp_idx = i->idx;

    // adjust the buffer offset to the received data into the iov
    i->buf = ip_data(buf) + sizeof(*udp);
    i->len = udp_len - sizeof(*udp);
    i->idx = rxs->buf_idx;

    // tag the iov with sender information and metadata
    i->ip = ip->src;
    i->port = udp->sport;
    i->flags = ip->tos;

    // append the iov to the socket
    sq_insert_tail(&s->iv, i, next);

    // put the original buffer of the iov into the receive ring
    rxs->buf_idx = tmp_idx;
    rxs->flags = NS_BUF_CHANGED;
}


/// Sends a payload contained in a w_sock::ov via UDP. For a connected w_sock,
/// prepends the template header from w_sock::hdr, computes the UDP length and
/// checksum, and hands the packet off to ip_tx(). For a disconnected w_sock,
/// uses the destination IP and port information in the w_iov for TX.
///
/// @param      s     The w_sock to transmit over.
/// @param      v     The w_iov to transmit.
///
/// @return     True if the payloads was sent, false otherwise.
///
bool udp_tx(const struct w_sock * const s, struct w_iov * const v)
{
    // copy template header into buffer and fill in remaining fields
    uint8_t * const buf = IDX2BUF(s->w, v->idx);
    memcpy(buf, s->hdr, sizeof(*s->hdr));

    struct ip_hdr * const ip = (void *)eth_data(buf);
    struct udp_hdr * const udp = (void *)ip_data(buf);
    const uint16_t len = v->len + sizeof(*udp);
    udp->len = htons(len);

    // if w_sock is disconnected, use destination IP and port from w_iov
    // instead of the one in the template header
    if (!w_connected(s)) {
        struct eth_hdr * const e = (void *)buf;
        e->dst = arp_who_has(s->w, v->ip);
        ip->dst = v->ip;
        udp->dport = v->port;
    }

    // compute the checksum, unless disabled by a socket option
    if ((s->flags & W_ZERO_CHKSUM) == 0) {
        udp->cksum = in_pseudo(s->w->ip, ip->dst, htons(len + IP_P_UDP));
        udp->cksum = in_cksum(udp, len);
    }

    udp_log(udp);

    return ip_tx(s->w, v, len);
}
