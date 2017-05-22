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
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <warpcore/warpcore.h>

#include "backend.h"
#include "eth.h"
#include "icmp.h"
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
        warn(debug, "IP: %s -> %s, dscp %d, ecn %d, ttl %d, id %d, "           \
                    "flags [%s%s], proto %d, hlen/tot %d/%d",                  \
             inet_ntop(AF_INET, &(ip)->src, src, INET_ADDRSTRLEN),             \
             inet_ntop(AF_INET, &(ip)->dst, dst, INET_ADDRSTRLEN),             \
             ip_dscp(ip), ip_ecn(ip), (ip)->ttl, ntohs((ip)->id),              \
             (ntohs((ip)->off) & IP_MF) ? "MF" : "",                           \
             (ntohs((ip)->off) & IP_DF) ? "DF" : "", (ip)->p, ip_hl(ip),       \
             ntohs((ip)->len));                                                \
    } while (0)
#else
#define ip_log(ip)                                                             \
    do {                                                                       \
    } while (0)
#endif


/// Receive processing for an IPv4 packet. Verifies the checksum and dispatches
/// the packet to udp_rx() or icmp_rx(), as appropriate.
///
/// The Ethernet frame to operate on is in the current netmap lot of the
/// indicated RX ring.
///
/// IPv4 options are currently unsupported; as are IPv4 fragments.
///
/// @param      w     Backend engine.
/// @param      r     Currently active netmap RX ring.
///
void ip_rx(struct w_engine * const w, struct netmap_ring * const r)
{
    uint8_t * const buf = (uint8_t *)NETMAP_BUF(r, r->slot[r->cur].buf_idx);
    const struct ip_hdr * const ip = (const struct ip_hdr *)eth_data(buf);
    ip_log(ip);

    // make sure the packet is for us (or broadcast)
    if (unlikely(ip->dst != w->ip && ip->dst != mk_bcast(w->ip, w->mask) &&
                 ip->dst != IP_BCAST)) {
#ifndef NDEBUG
        char src[INET_ADDRSTRLEN];
        char dst[INET_ADDRSTRLEN];
        warn(info, "IP packet from %s to %s (not us); ignoring",
             inet_ntop(AF_INET, &ip->src, src, INET_ADDRSTRLEN),
             inet_ntop(AF_INET, &ip->dst, dst, INET_ADDRSTRLEN));
#endif
        return;
    }

    // validate the IP checksum
    if (unlikely(in_cksum(ip, sizeof(*ip)) != 0)) {
        warn(warn, "invalid IP checksum, received 0x%04x", ntohs(ip->cksum));
        return;
    }

    // TODO: handle IP options
    ensure(ip_hl(ip) == 20, "no support for IP options");

    // TODO: handle IP fragments
    ensure((ntohs(ip->off) & IP_OFFMASK) == 0, "no support for IP fragments");

    if (likely(ip->p == IP_P_UDP))
        udp_rx(w, r);
    else if (ip->p == IP_P_ICMP)
        icmp_rx(w, r);
    else {
        warn(info, "unhandled IP protocol %d", ip->p);
        // be standards compliant and send an ICMP unreachable
        icmp_tx(w, ICMP_TYPE_UNREACH, ICMP_UNREACH_PROTOCOL, buf);
    }
}


// Fill in the IP header information that isn't set as part of the
// socket packet template, calculate the header checksum, and hand off
// to the Ethernet layer.


/// IPv4 transmit processing for the w_iov @p v of length @p len. Fills in the
/// IPv4 header, calculates the checksum, sets the TOS bits and passes the
/// packet to eth_tx().
///
/// @param      w     Backend engine.
/// @param      v     The w_iov containing the data to transmit.
/// @param[in]  len   The length of the payload data in @p v.
///
/// @return     Passes on the return value from eth_tx(), which indicates
///             whether @p v was successfully placed into a TX ring.
///
bool ip_tx(struct w_engine * const w,
           struct w_iov * const v,
           const uint16_t len)
{
    struct ip_hdr * const ip = (struct ip_hdr *)eth_data(IDX2BUF(w, v->idx));
    const uint16_t l = len + sizeof(*ip);

    // fill in remaining header fields
    ip->len = htons(l);
    ip->id = (uint16_t)plat_random(); // no need to do htons() for random value
    // IP checksum is over header only
    ip->cksum = in_cksum(ip, sizeof(*ip));
    ip->tos = v->flags; // app-specified DSCP + ECN

    ip_log(ip);

    // do Ethernet transmit preparation
    return eth_tx(w, v, l);
}
