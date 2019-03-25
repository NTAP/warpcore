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
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/param.h>
#include <sys/socket.h>

// IWYU pragma: no_include <net/netmap.h>
#include <net/netmap_user.h> // IWYU pragma: keep

#include "backend.h"
#include "eth.h"
#include "icmp.h"
#include "in_cksum.h"
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
/// @param      s     Currently active netmap RX slot.
/// @param      buf   Incoming packet.
///
/// @return     Whether a packet was placed into a socket.
///
bool
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 8)
    __attribute__((no_sanitize("alignment")))
#endif
    udp_rx(struct w_engine * const w,
           struct netmap_slot * const s,
           uint8_t * const buf)
{
    const struct ip_hdr * const ip = (const void *)eth_data(buf);
    const uint16_t ip_len = ntohs(ip->len);
    struct udp_hdr * const udp = (void *)ip_data(buf);

    if (unlikely(ip_len - sizeof(*ip) < sizeof(*udp))) {
#ifndef FUZZING
        warn(WRN, "IP len %lu too short for UDP", ip_len - sizeof(*ip));
#endif
        return false;
    }

    const uint16_t udp_len = MIN(ntohs(udp->len), ip_len - sizeof(*ip));
    udp_log(udp);

#ifndef FUZZING
    if (likely(udp->cksum)) {
        // validate the checksum
        const uint16_t cksum = udp_cksum(ip, udp_len + sizeof(*ip));
        if (unlikely(udp->cksum != cksum)) {
            warn(WRN, "invalid UDP checksum, received 0x%04x != 0x%04x",
                 ntohs(udp->cksum), ntohs(cksum));
            return false;
        }
    }
#endif

    struct w_sock * ws =
        w_get_sock(w, ip->dst, udp->dport, ip->src, udp->sport);
    if (unlikely(ws == 0)) {
        // no socket connected, check for bound-only socket
        ws = w_get_sock(w, ip->dst, udp->dport, 0, 0);
        if (unlikely(ws == 0)) {
            // nobody bound to this port locally
            // send an ICMP unreachable reply, if this was not a broadcast
            if (ip->dst == w->ip)
                icmp_tx(w, ICMP_TYPE_UNREACH, ICMP_UNREACH_PORT, buf);
            return false;
        }
    }
    // grab an unused iov for the data in this packet
    //
    // XXX w_alloc_iov() does some (in this case) unneeded initialization;
    // determine if that overhead is a problem
    struct w_iov * const i = w_alloc_iov_base(w);
    if (unlikely(i == 0)) {
        warn(CRT, "no more bufs; UDP packet RX failed");
        return false;
    }

    warn(DBG, "%sing rx slot idx %d %s spare idx %u",
         unlikely(is_pipe(w)) ? "copy" : "swap", s->buf_idx,
         unlikely(is_pipe(w)) ? "into" : "and", i->idx);

    if (unlikely(is_pipe(w))) {
        // we need to copy the data for pipes
        memcpy(i->base, buf, s->len);
        i->buf = ip_data(i->base) + sizeof(*udp);
    } else {
        // remember index of this buffer
        const uint32_t tmp_idx = i->idx;

        // adjust the buffer offset to the received data into the iov
        i->base = buf;
        i->buf = (uint8_t *)udp + sizeof(*udp);
        i->idx = s->buf_idx;

        // put the original buffer of the iov into the receive ring
        s->buf_idx = tmp_idx;
        s->flags = NS_BUF_CHANGED;
    }

    // tag the iov with sender information and metadata
    i->len = udp_len - sizeof(*udp);

    struct sockaddr_in * const addr4 = (struct sockaddr_in *)&i->addr;
    addr4->sin_family = AF_INET;
    addr4->sin_addr.s_addr = ip->src;
    addr4->sin_port = udp->sport;
    i->flags = ip->tos;

    // append the iov to the socket
    sq_insert_tail(&ws->iv, i, next);
    return true;
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
bool
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 8)
    __attribute__((no_sanitize("alignment")))
#endif
    udp_tx(const struct w_sock * const s, struct w_iov * const v)
{
    struct ip_hdr * const ip = (void *)eth_data(v->base);
    struct udp_hdr * const udp = (void *)ip_data(v->base);
    const uint16_t len = v->len + sizeof(*udp);

    mk_eth_hdr(s, v);
    mk_ip_hdr(v, len, s);

    udp->sport = s->tup.sport;
    struct sockaddr_in * const addr4 = (struct sockaddr_in *)&v->addr;
    udp->dport = w_connected(s) ? s->tup.dport : addr4->sin_port;
    udp->len = htons(len);

    // compute the checksum, unless disabled by a socket option
    udp->cksum = 0;
    if (unlikely(s->opt.enable_udp_zero_checksums == false))
        udp->cksum = udp_cksum(ip, len + sizeof(*ip));

    udp_log(udp);

    return eth_tx(v, len + sizeof(struct ip_hdr));
}
