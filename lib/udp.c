// Copyright (c) 2014-2016, NetApp, Inc.
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

#include <arpa/inet.h>
#include <poll.h>

#ifdef __linux__
#include <netinet/ether.h>
#else
// clang-format off
// because these includes need to be in-order
#include <sys/types.h>
#include <net/ethernet.h>
// clang-format on
#endif

#include "arp.h"
#include "backend.h"
#include "icmp.h"


#ifndef NDEBUG
/// Print a summary of the udp_hdr @p udp.
///
/// @param      udp   The udp_hdr to print.
///
#define udp_log(udp)                                                           \
    do {                                                                       \
        warn(info, "UDP :%d -> :%d, cksum 0x%04x, len %u", ntohs(udp->sport),  \
             ntohs(udp->dport), ntohs(udp->cksum), ntohs(udp->len));           \
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
/// @param      w     Warpcore engine.
/// @param      buf   Buffer containing the Ethernet frame to receive from.
/// @param[in]  src   The IPv4 source address of the sender of this packet.
///
void udp_rx(struct warpcore * const w, void * const buf, const uint32_t src)
{
    const struct ip_hdr * const ip = eth_data(buf);
    struct udp_hdr * const udp = ip_data(buf);
    const uint16_t len = ntohs(udp->len);

    udp_log(udp);

    // validate the checksum
    const uint16_t orig = udp->cksum;
    udp->cksum = in_pseudo(ip->src, ip->dst, htons(len + ip->p));
    const uint16_t cksum = in_cksum(udp, len);
    udp->cksum = orig;
    if (unlikely(orig != cksum)) {
        warn(warn, "invalid UDP checksum, received 0x%04x != 0x%04x",
             ntohs(orig), ntohs(cksum));
        return;
    }

    struct w_sock * const s = w->udp[udp->dport];
    if (unlikely(s == 0)) {
        // nobody bound to this port locally
        // send an ICMP unreachable reply, if this was not a broadcast
        if (ip->src)
            icmp_tx_unreach(w, ICMP_UNREACH_PORT, buf);
        return;
    }

    // grab an unused iov for the data in this packet
    struct w_iov * const i = STAILQ_FIRST(&w->iov);
    assert(i != 0, "out of spare bufs");
    struct netmap_ring * const rxr = NETMAP_RXRING(w->nif, w->cur_rxr);
    struct netmap_slot * const rxs = &rxr->slot[rxr->cur];
    STAILQ_REMOVE_HEAD(&w->iov, next);

    warn(debug, "swapping rx ring %u slot %d (buf %d) and spare buf %u",
         w->cur_rxr, rxr->cur, rxs->buf_idx, i->idx);

    // remember index of this buffer
    const uint32_t tmp_idx = i->idx;

    // adjust the buffer offset to the received data into the iov
    i->buf = (char *)ip_data(buf) + sizeof(struct udp_hdr);
    i->len = len - sizeof(struct udp_hdr);
    i->idx = rxs->buf_idx;

    // tag the iov with sender information and metadata
    i->ip = src;
    i->port = udp->sport;
    i->flags = ip->tos;
    memcpy(&i->ts, &rxr->ts, sizeof(i->ts));

    // append the iov to the socket
    STAILQ_INSERT_TAIL(&s->iv, i, next);

    // put the original buffer of the iov into the receive ring
    rxs->buf_idx = tmp_idx;
    rxs->flags = NS_BUF_CHANGED;
}


/// Sends w_iov payloads contained in a w_sock::ov via UDP. For a connected
/// w_sock, prepends the template header from w_sock::hdr, computes the UDP
/// length and checksum, and hands the packet off to ip_tx(). For a disconnected
/// w_sock, uses the destination IP and port information in each w_iov for TX.
/// Stops processing packets from @p v if ip_tx() indicates that the TX rings
/// are full.
///
/// @param      s     The w_sock to transmit over.
/// @param      v     w_iov chain to transmit.
///
void udp_tx(const struct w_sock * const s, struct w_iov * const v)
{
    uint32_t n = 0, l = 0;
    // packetize bufs and place in tx ring
    for (struct w_iov * o = v; o; o = STAILQ_NEXT(o, next)) {

        // copy template header into buffer and fill in remaining fields
        void * const buf = IDX2BUF(s->w, o->idx);
        memcpy(buf, &s->hdr, sizeof(s->hdr));

        struct ip_hdr * const ip = eth_data(buf);
        struct udp_hdr * const udp = ip_data(buf);
        const uint16_t len = o->len + sizeof(struct udp_hdr);
        udp->len = htons(len);

        // if w_sock is disconnected, use destination IP and port from w_iov
        // instead of the one in the template header
        if (s->hdr.ip.dst == 0 && s->hdr.udp.dport == 0) {
            assert(o->ip && o->port, "no destination information");
            struct eth_hdr * const e = buf;
            memcpy(&e->dst, arp_who_has(s->w, o->ip), ETH_ADDR_LEN);
            ip->dst = o->ip;
            udp->dport = o->port;
            // warn(debug, "w_sock disconnected, sending to %s:%d at %s",
            //      inet_ntoa(*(const struct in_addr * const) & o->ip),
            //      ntohs(o->port), ether_ntoa((const struct ether_addr *
            //      const)e->dst));
        }

        // compute the checksum
        udp->cksum = in_pseudo(s->w->ip, ip->dst, htons(len + IP_P_UDP));
        udp->cksum = in_cksum(udp, len);

        udp_log(udp);

        // do IP transmit preparation
        if (ip_tx(s->w, o, len)) {
            n++;
            l += o->len;
        } else
            // no space left in rings
            break;
    }

    warn(info, "proto %d tx iov (len %d in %d bufs) done", s->hdr.ip.p, l, n);
}
