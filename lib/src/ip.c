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

#ifdef __FreeBSD__
#include <sys/types.h> // IWYU pragma: keep
#endif

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdint.h>
#include <sys/socket.h>

#include <warpcore/warpcore.h>

#include "backend.h"
#include "eth.h"
#include "icmp.h"
#include "in_cksum.h"
#include "ip.h"
#include "udp.h"


#ifndef NDEBUG
/// Print a textual representation of ip_hdr @p ip.
///
/// @param      ip    The ip_hdr to print.
///
#define ip_log(ip)                                                             \
    do {                                                                       \
        char src[INET_ADDRSTRLEN];                                             \
        char dst[INET_ADDRSTRLEN];                                             \
        warn(DBG,                                                              \
             "IP: %s -> %s, dscp %d, ecn %d, ttl %d, id %d, "                  \
             "flags [%s%s], proto %d, hlen/tot %d/%d, cksum %04x",             \
             inet_ntop(AF_INET, &(ip)->src, src, INET_ADDRSTRLEN),             \
             inet_ntop(AF_INET, &(ip)->dst, dst, INET_ADDRSTRLEN),             \
             ip_dscp(ip), ip_ecn(ip), (ip)->ttl, ntohs((ip)->id),              \
             (ntohs((ip)->off) & IP_MF) ? "MF" : "",                           \
             (ntohs((ip)->off) & IP_DF) ? "DF" : "", (ip)->p, ip_hl(ip),       \
             ntohs((ip)->len), ntohs((ip)->cksum));                            \
    } while (0)
#else
#define ip_log(ip)                                                             \
    do {                                                                       \
    } while (0)
#endif


/// Receive processing for an IPv4 packet. Verifies the checksum and dispatches
/// the packet to udp_rx() or icmp_rx(), as appropriate.
///
/// IPv4 options are currently unsupported; as are IPv4 fragments.
///
/// @param      w     Backend engine.
/// @param      s     Currently active netmap RX slot.
/// @param      buf   Incoming packet.
///
void
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 8)
    __attribute__((no_sanitize("alignment")))
#endif
    ip_rx(struct w_engine * const w,
          struct netmap_slot * const s,
          uint8_t * const buf)
{
    // an Ethernet frame is at least 64 bytes, enough for the Ethernet+IP header
    const struct ip_hdr * const ip = (void *)eth_data(buf);
#ifndef FUZZING
    ip_log(ip);
#endif
    // make sure the packet is for us (or broadcast)
    if (unlikely(ip->dst != w->ip && ip->dst != mk_bcast(w->ip, w->mask) &&
                 ip->dst != IP_BCAST)) {
#if !defined(NDEBUG) && !defined(FUZZING)
        char src[INET_ADDRSTRLEN];
        char dst[INET_ADDRSTRLEN];
        warn(INF, "IP packet from %s to %s (not us); ignoring",
             inet_ntop(AF_INET, &ip->src, src, INET_ADDRSTRLEN),
             inet_ntop(AF_INET, &ip->dst, dst, INET_ADDRSTRLEN));
#endif
        return;
    }

#ifndef FUZZING
    // validate the IP checksum
    if (unlikely(ip_cksum(ip, sizeof(*ip)) != 0)) {
        warn(WRN, "invalid IP checksum, received 0x%04x != 0x%04x",
             ntohs(ip->cksum), ip_cksum(ip, sizeof(*ip)));
        return;
    }
#endif

    if (unlikely(ip_hl(ip) != sizeof(*ip))) {
        // TODO: handle IP options
#ifndef FUZZING
        warn(WRN, "no support for IP options");
#endif
        return;
    }

    if (unlikely(ntohs(ip->off) & IP_OFFMASK)) {
        // TODO: handle IP fragments
#ifndef FUZZING
        warn(WRN, "no support for IP fragments");
#endif
        return;
    }

    if (likely(ip->p == IP_P_UDP))
        udp_rx(w, s, buf);
    else if (ip->p == IP_P_ICMP)
        icmp_rx(w, s, buf);
    else {
#ifndef FUZZING
        warn(INF, "unhandled IP protocol %d", ip->p);
#endif
        // be standards compliant and send an ICMP unreachable
        icmp_tx(w, ICMP_TYPE_UNREACH, ICMP_UNREACH_PROTOCOL, buf);
    }
}


/// IPv4 transmit processing for the w_iov @p v of length @p len. Fills in the
/// IPv4 header, calculates the checksum, and sets the TOS bits.
///
/// @param[in]  s     The w_sock to transmit over.
/// @param      v     The w_iov containing the data to transmit.
/// @param[in]  len   The length of the payload data in @p v.
///
void
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 8)
    __attribute__((no_sanitize("alignment")))
#endif
    mk_ip_hdr(struct w_iov * const v,
              const uint16_t plen,
              const struct w_sock * const s)
{
    struct ip_hdr * const ip = (void *)eth_data(v->base);
    ip->vhl = (4 << 4) + 5;

    // set DSCP and ECN
    ip->tos = v->flags;
    // if there is no per-packet ECN marking, apply default
    if ((ip->tos & IPTOS_ECN_MASK) == 0 && likely(s) && s->opt.enable_ecn)
        ip->tos |= IPTOS_ECN_ECT0;

    const uint16_t len = plen + sizeof(*ip);
    ip->len = htons(len);

    // no need to do htons() for random value
    ip->id = (uint16_t)w_rand_uniform(UINT16_MAX);

    ip->off = htons(IP_DF);
    ip->ttl = 64; // XXX this should be configurable
    if (likely(s))
        // when no socket is passed, caller must set ip->p
        ip->p = IP_P_UDP;

    ip->src = likely(s) && w_connected(s) ? s->tup.sip : v->w->ip;
    struct sockaddr_in * const addr4 = (struct sockaddr_in *)&v->addr;
    ip->dst = likely(s) && w_connected(s) ? s->tup.dip : addr4->sin_addr.s_addr;

    // IP checksum is over header only
    ip->cksum = 0;
    ip->cksum = ip_cksum(ip, sizeof(*ip));

    ip_log(ip);
}
