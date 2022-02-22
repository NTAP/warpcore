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

#ifdef __FreeBSD__
#include <sys/types.h>
#endif

#ifndef NDEBUG
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

#include <stdint.h>

#include <net/netmap_user.h>

#include <warpcore/warpcore.h>

#include "eth.h"
#include "icmp4.h"
#include "in_cksum.h"
#include "ip4.h"
#include "udp.h"


#if !defined(NDEBUG) && !defined(FUZZING)
/// Print a textual representation of ip4_hdr @p ip.
///
/// @param      ip    The ip4_hdr to print.
///
#define ip4_log(ip)                                                            \
    warn(DBG,                                                                  \
         "IPv%u: %s -> %s, dscp %d, ecn %d, ttl %d, id %d, "                   \
         "flags [%s%s], proto %d, hlen/tot %d/%d, cksum 0x%04x",               \
         ip_v((ip)->vhl), inet_ntop(AF_INET, &(ip)->src, ip4_tmp, IP4_STRLEN), \
         inet_ntop(AF_INET, &(ip)->dst, ip4_tmp, IP4_STRLEN),                  \
         ip4_dscp((ip)->tos), ip4_ecn((ip)->tos), (ip)->ttl,                   \
         bswap16((ip)->id), ((ip)->off & IP4_MF) ? "MF" : "",                  \
         ((ip)->off & IP4_DF) ? "DF" : "", (ip)->p, ip4_hl((ip)->vhl),         \
         bswap16((ip)->len), bswap16((ip)->cksum))
#else
#define ip4_log(ip)                                                            \
    do {                                                                       \
    } while (0)
#endif


/// Receive processing for an IPv4 packet. Verifies the checksum and dispatches
/// the packet to udp_rx() or icmp4_rx(), as appropriate.
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
    ip4_rx(struct w_engine * const w,
           struct netmap_slot * const s,
           uint8_t * const buf)
{
    // an Ethernet frame is at least 64 bytes, enough for the Ethernet+IP header
    const struct ip4_hdr * const ip = (void *)eth_data(buf);
    ip4_log(ip);

    if (unlikely(ip_v(ip->vhl) != 4)) {
        warn(ERR, "illegal IPv4 version %u", ip_v(ip->vhl));
        return false;
    }

    const uint8_t hl = ip4_hl(ip->vhl);

    // make sure the packet is for us (or broadcast)
    if (is_my_ip4(w, (ip)->dst, true) == UINT16_MAX) {
        warn(INF, "IP packet from %s to %s (not us); ignoring",
             inet_ntop(AF_INET, &ip->src, ip4_tmp, IP4_STRLEN),
             inet_ntop(AF_INET, &ip->dst, ip4_tmp, IP4_STRLEN));
        return false;
    }

    // validate the IP checksum
    if (unlikely(ip_cksum(ip, hl) != 0)) {
        warn(WRN, "invalid IP checksum, received 0x%04x != 0x%04x",
             bswap16(ip->cksum), ip_cksum(ip, hl));
        return false;
    }

    if (unlikely(ip4_hl(ip->vhl) != hl)) {
        // TODO: handle IP options
        warn(WRN, "no support for IP options");
        return false;
    }

    if (unlikely(ip->off & IP4_OFFMASK)) {
        // TODO: handle IP fragments
        warn(WRN, "no support for IP fragments");
        return false;
    }

    if (likely(ip->p == IP_P_UDP))
        return udp_rx(w, s, buf);
    if (ip->p == IP_P_ICMP)
        icmp4_rx(w, s, buf);
    else {
        warn(INF, "unhandled IP protocol %d", ip->p);
        // be standards compliant and send an ICMP unreachable
        icmp4_tx(w, ICMP4_TYPE_UNREACH, ICMP4_UNREACH_PROTOCOL, buf);
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
    mk_ip4_hdr(struct w_iov * const v, const struct w_sock * const s)
{
    struct ip4_hdr * const ip = (void *)eth_data(v->base);
    ip->vhl = (4 << 4) | (sizeof(*ip) >> 2);

    // set DSCP and ECN
    ip->tos = v->flags;
    // if there is no per-packet ECN marking, apply default
    if ((v->flags & ECN_MASK) == 0 && likely(s) && s->opt.enable_ecn)
        ip->tos |= ECN_ECT0;

    v->len += sizeof(*ip);
    ip->len = bswap16(v->len);

    // no need to do bswap16() for random value
    ip->id = (uint16_t)w_rand_uniform32(UINT16_MAX);

    ip->off = IP4_DF;
    ip->ttl = 0xff;
    if (likely(s))
        // when no socket is passed, caller must set ip->p
        ip->p = IP_P_UDP;

    if (likely(s) && w_connected(s)) {
        ip->src = s->ws_laddr.ip4;
        ip->dst = s->ws_raddr.ip4;
    } else {
        ip->src = v->w->ifaddr[v->w->addr4_pos].addr.ip4;
        ip->dst = v->wv_ip4;
    }

    // IP checksum is over header only
    ip->cksum = 0;
    ip->cksum = ip_cksum(ip, sizeof(*ip));

    ip4_log(ip);
}


/// Return index of interface matching IPv4 address in w->addr[].
///
/// @param      w            Backend engine.
/// @param[in]  ip           IPv4 address.
/// @param[in]  match_bcast  Whether to check for broadcast address match.
///
/// @return     Index of the interface address, or UINT16_MAX if no match.
///
uint16_t is_my_ip4(const struct w_engine * const w,
                   const uint32_t ip,
                   const bool match_bcast)
{
    uint16_t idx = w->addr4_pos;
    while (idx < w->addr_cnt) {
        const struct w_ifaddr * const ia = &w->ifaddr[idx];
        if (ip == ia->addr.ip4)
            return idx;
        if (match_bcast && (ip == ia->bcast4 || ip == UINT32_MAX))
            return idx;
        idx++;
    }
    return UINT16_MAX;
}
