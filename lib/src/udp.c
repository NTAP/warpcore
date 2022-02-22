// SPDX-License-Identifier: BSD-2-Clause
//
// Copyright (c) 2014-2022, NetApp, Inc.
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

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/param.h>
#include <sys/socket.h>

#include <net/netmap.h>

#include "backend.h"
#include "eth.h"
#include "icmp4.h"
#include "icmp6.h"
#include "in_cksum.h"
#include "ip4.h"
#include "ip6.h"
#include "udp.h"


#ifndef NDEBUG
/// Print a summary of the udp_hdr @p udp.
///
/// @param      udp   The udp_hdr to print.
///
#define udp_log(udp)                                                           \
    warn(DBG, "UDP :%d -> :%d, cksum 0x%04x, len %u", bswap16((udp)->sport),   \
         bswap16((udp)->dport), bswap16((udp)->cksum), bswap16((udp)->len))
#else
#define udp_log(udp)
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
    // grab an unused iov for the data in this packet
    //
    // TODO: w_alloc_iov() does some (in this case) unneeded initialization;
    // determine if that overhead is a problem
    struct w_iov * const i = w_alloc_iov_base(w);
    if (unlikely(i == 0)) {
        warn(CRT, "no more bufs; UDP packet RX failed");
        return false;
    }

    const uint8_t * const ip = eth_data(buf);
    const uint8_t v = ip_v(*ip);
    uint16_t ip_hdr_len;
    struct udp_hdr * udp;
    uint16_t ip_plen;
    struct w_sockaddr local;

    if (v == 4) {
        const struct ip4_hdr * ip4 = (const void *)ip;
        ip_hdr_len = ip4_hl(ip4->vhl);
        ip_plen = bswap16(ip4->len) - ip_hdr_len;
        udp = (void *)ip4_data(buf);
        local.addr.af = i->wv_af = AF_INET;
        i->wv_ip4 = ip4->src;
        local.addr.ip4 = ip4->dst;
        i->flags = ip4->tos;
        i->ttl = ip4->ttl;
    } else {
        const struct ip6_hdr * ip6 = (const void *)ip;
        ip_hdr_len = sizeof(*ip6);
        ip_plen = bswap16(ip6->len);
        udp = (void *)ip6_data(buf);
        local.addr.af = i->wv_af = AF_INET6;
        memcpy(i->wv_ip6, ip6->src, sizeof(i->wv_ip6));
        memcpy(local.addr.ip6, ip6->dst, sizeof(local.addr.ip6));
        i->flags = ip6_tos(ip6->vtcecnfl);
        i->ttl = ip6->hlim;
    }

    if (unlikely(ip_plen < sizeof(*udp))) {
        warn(WRN, "IP payload %u too short for UDP header", ip_plen);
        return false;
    }

    const uint16_t udp_len = MIN(bswap16(udp->len), ip_plen);
    i->len = udp_len - sizeof(*udp);
    udp_log(udp);

    if (likely(udp->cksum)) {
        // validate the checksum
        if (unlikely(payload_cksum(ip, udp_len + ip_hdr_len) != 0)) {
            warn(WRN, "invalid UDP checksum, received 0x%04x",
                 bswap16(udp->cksum));
            return false;
        }
    }

    i->wv_port = udp->sport;
    local.port = udp->dport;
    struct w_sock * ws = w_get_sock(w, &local, &i->saddr);
    if (unlikely(ws == 0)) {
        // no socket connected, check for bound-only socket
        ws = w_get_sock(w, &local, 0);
        if (unlikely(ws == 0)) {
            // nobody bound to this port locally
            // send an ICMP unreachable reply, if this was not a broadcast
            if (v == 4 && is_my_ip4(w, i->wv_ip4, false) != UINT16_MAX)
                icmp4_tx(w, ICMP4_TYPE_UNREACH, ICMP4_UNREACH_PORT, buf);
            else if (v == 6 && is_my_ip6(w, i->wv_ip6, false) != UINT16_MAX)
                icmp6_tx(w, ICMP6_TYPE_UNREACH, ICMP6_UNREACH_PORT, buf);
            return false;
        }
    }

#if 0
    warn(DBG, "swapping rx slot idx %d and spare idx %u", s->buf_idx, i->idx);
#endif


    // adjust the buffer offset to the received data into the iov
    i->base = buf;
    i->buf = (uint8_t *)udp + sizeof(*udp);
    const uint32_t tmp_idx = i->idx;
    i->idx = s->buf_idx;

    // put the original buffer of the iov into the receive ring
    s->buf_idx = tmp_idx;
    s->flags = NS_BUF_CHANGED;

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
bool udp_tx(const struct w_sock * const s, struct w_iov * const v)
{
    const uint16_t vlen = v->len;
    v->len += sizeof(struct udp_hdr);

    uint16_t ip_hdr_len;
    struct udp_hdr * udp;
    if (s->ws_af == AF_INET) {
        mk_ip4_hdr(v, s);
        udp = (void *)ip4_data(v->base);
        ip_hdr_len = ip4_hl(*eth_data(v->base));
    } else {
        mk_ip6_hdr(v, s);
        udp = (void *)ip6_data(v->base);
        ip_hdr_len = sizeof(struct ip6_hdr);
    }

    udp->sport = s->ws_lport;
    udp->dport = w_connected(s) ? s->ws_rport : v->wv_port;
    udp->len = bswap16(v->len - ip_hdr_len);
    udp->cksum = 0;

    // compute the checksum, unless disabled by a socket option
    if (unlikely(s->opt.enable_udp_zero_checksums == false))
        udp->cksum = payload_cksum(eth_data(v->base), v->len);

    mk_eth_hdr(s, v);
    udp_log(udp);
    const bool ret = eth_tx(v);
    v->len = vlen;
    return ret;
}
