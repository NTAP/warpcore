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

#ifdef __FreeBSD__
#include <sys/types.h>
#endif

#ifndef NDEBUG
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

#include <stdint.h>
#include <string.h>

#include <warpcore/warpcore.h>

#include "eth.h"
#include "icmp6.h"
#include "ip4.h"
#include "ip6.h"
#include "udp.h"


#if !defined(NDEBUG) && !defined(FUZZING)
/// Print a textual representation of ip6_hdr @p ip.
///
/// @param      ip    The ip6_hdr to print.
///
#define ip6_log(ip)                                                            \
    warn(DBG,                                                                  \
         "IPv%u: %s -> %s, flow 0x%05x, hlim %d, next-hdr %u, "                \
         "plen %d, tc 0x%02x, ecn %d",                                         \
         ip_v((ip)->vfc),                                                      \
         inet_ntop(AF_INET6, &(ip)->src, ip6_tmp, IP6_STRLEN),                 \
         inet_ntop(AF_INET6, &(ip)->dst, ip6_tmp, IP6_STRLEN),                 \
         bswap32(ip6_flow_label((ip)->vtcecnfl)), (ip)->hlim, (ip)->next_hdr,  \
         bswap16((ip)->len), ip6_tc((ip)->vtcecnfl), ip6_ecn((ip)->vtcecnfl))
#else
#define ip6_log(ip)
#endif


/// Receive processing for an IPv4 packet. Verifies the checksum and dispatches
/// the packet to udp_rx() or icmp6_rx(), as appropriate.
///
/// IPv4 options are currently unsupported; as are IPv4 fragments.
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
    ip6_rx(struct w_engine * const w,
           struct netmap_slot * const s,
           uint8_t * const buf)
{
    // an Ethernet frame is at least 64 bytes, enough for the Ethernet+IP header
    const struct ip6_hdr * const ip = (void *)eth_data(buf);
    ip6_log(ip);

    if (unlikely(ip_v(ip->vfc) != 6)) {
        warn(ERR, "illegal IPv6 version %u 0x%04x", ip_v(ip->vfc),
             ip->vtcecnfl);
        return false;
    }

    // make sure the packet is for us (or broadcast)
    if (is_my_ip6(w, ip->dst, true) == UINT16_MAX) {
        warn(INF, "IPv6 packet from %s to %s (not us); ignoring",
             inet_ntop(AF_INET6, &ip->src, ip6_tmp, IP6_STRLEN),
             inet_ntop(AF_INET6, &ip->dst, ip6_tmp, IP6_STRLEN));
        return false;
    }

    if (likely(ip->next_hdr == IP_P_UDP))
        return udp_rx(w, s, buf);
    if (ip->next_hdr == IP_P_ICMP6)
        icmp6_rx(w, s, buf);
    else {
        warn(INF, "unhandled next-header protocol %d", ip->next_hdr);
    }
    return false;
}


/// IPv4 transmit processing for the w_iov @p v of length @p len. Fills in the
/// IPv4 header, calculates the checksum, and sets the TOS bits.
///
/// @param[in]  s     The w_sock to transmit over.
/// @param      v     The w_iov containing the data to transmit.
/// @param[in]  plen   The length of the payload data in @p v.
///
void
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 8)
    __attribute__((no_sanitize("alignment")))
#endif
    mk_ip6_hdr(struct w_iov * const v, const struct w_sock * const s)
{
    struct ip6_hdr * const ip = (void *)eth_data(v->base);

    // set version, TC and ECN
    ip->vfc = (6 << 4);
    if (v->flags & ECN_MASK)
        ip->vtcecnfl |=
            (uint32_t)((v->flags & 0x0f) << 12 | (v->flags & 0xf0) >> 4);
    else if (likely(s) && s->opt.enable_ecn)
        // if there is no per-packet ECN marking, apply default
        ip->vtcecnfl |= (ECN_ECT0 << 20);

    // we never set a flow label, if we were to set it, do it like this:
    // uint32_t flow = 0x00012345;
    // ip->vtcecnfl |= bswap32(flow);

    ip->len = bswap16(v->len);

    ip->hlim = 0xff;
    if (likely(s))
        // when no socket is passed, caller must set ip->p
        ip->next_hdr = IP_P_UDP;

    if (likely(s) && w_connected(s)) {
        memcpy(ip->src, s->ws_laddr.ip6, sizeof(ip->src));
        memcpy(ip->dst, s->ws_raddr.ip6, sizeof(ip->dst));
    } else {
        memcpy(ip->src, v->w->ifaddr[0].addr.ip6, sizeof(ip->src));
        memcpy(ip->dst, v->wv_ip6, sizeof(ip->dst));
    }

    v->len += sizeof(*ip);
    ip6_log(ip);
}


/// Return index of interface matching IPv6 address in w->addr[].
///
/// @param      w            Backend engine.
/// @param[in]  ip           IPv6 address.
/// @param[in]  match_mcast  Whether to check for broadcast/multicast address
///                          match.
///
/// @return     Index of the interface address, or UINT16_MAX if no match.
///
uint16_t is_my_ip6(const struct w_engine * const w,
                   const uint8_t * const ip,
                   const bool match_mcast)
{
    uint16_t idx = 0;
    while (idx < w->addr_cnt) {
        const struct w_ifaddr * const ia = &w->ifaddr[idx];
        if (ip6_eql(ip, ia->addr.ip6))
            return idx;
        if (match_mcast && (ip6_eql(ip, ia->snma6) || ip6_eql(ip, ia->bcast6)))
            return idx;
        idx++;
    }
    return UINT16_MAX;
}
