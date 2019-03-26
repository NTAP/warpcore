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

#include <arpa/inet.h>
#include <string.h>

#ifdef __APPLE__
#include <sys/types.h>
#endif

// IWYU pragma: no_include <net/netmap.h>
#include <net/netmap_user.h> // IWYU pragma: keep
#include <warpcore/warpcore.h>

#include "arp.h"
#include "backend.h"
#include "eth.h"
#include "ip.h"


#ifndef HAVE_ETHER_NTOA_R
#include <stdio.h>

char * ether_ntoa_r(const struct ether_addr * const addr, char * const buf)
{
    sprintf(buf, "%x:%x:%x:%x:%x:%x", addr->ether_addr_octet[0],
            addr->ether_addr_octet[1], addr->ether_addr_octet[2],
            addr->ether_addr_octet[3], addr->ether_addr_octet[4],
            addr->ether_addr_octet[5]);
    return buf;
}
#endif


/// Receive an Ethernet frame. This is the lowest-level RX function, called for
/// each new inbound frame from w_rx(). Dispatches the frame to either ip_rx()
/// or arp_rx(), based on its EtherType.
///
/// @param      w     Backend engine.
/// @param      s     Currently active netmap RX slot.
/// @param      buf   Incoming packet.
///
/// @return     Whether a packet was placed into a socket.
///
bool eth_rx(struct w_engine * const w,
            struct netmap_slot * const s,
            uint8_t * const buf)
{
    // an Ethernet frame is at least 64 bytes, enough for the Ethernet header
    const struct eth_hdr * const eth = (void *)buf;

#if !defined(NDEBUG) && !defined(FUZZING)
    char src[ETH_ADDR_STRLEN];
    char dst[ETH_ADDR_STRLEN];
    warn(DBG, "Eth %s -> %s, type %d, len %d", ether_ntoa_r(&eth->src, src),
         ether_ntoa_r(&eth->dst, dst), ntohs(eth->type), s->len);
#endif

    // make sure the packet is for us (or broadcast)
    if (unlikely((memcmp(&eth->dst, &w->mac, ETHER_ADDR_LEN) != 0) &&
                 (memcmp(&eth->dst, "\xff\xff\xff\xff\xff\xff",
                         ETHER_ADDR_LEN) != 0))) {
#ifndef FUZZING
        warn(INF, "Ethernet packet to %s not destined to us (%s); ignoring",
             ether_ntoa_r(&eth->dst, dst), ether_ntoa_r(&w->mac, src));
#endif
        return false;
    }
    if (likely(eth->type == ETH_TYPE_IP))
        return ip_rx(w, s, buf);
    if (eth->type == ETH_TYPE_ARP)
        arp_rx(w, buf);
#ifndef FUZZING
    else
        warn(INF, "unhandled ethertype 0x%04x", ntohs(eth->type));
#endif
    return false;
}


/// Places an Ethernet frame into a TX ring. The Ethernet frame is contained in
/// the w_iov @p v, and will be placed into an available slot in a TX ring or -
/// if all are full - dropped.
///
/// @param      v     The w_iov containing the Ethernet frame to transmit.
/// @param[in]  len   The length of the Ethernet *payload* contained in @p v.
///
/// @return     True if the buffer was placed into a TX ring, false otherwise.
///
bool __attribute__((nonnull)) eth_tx(struct w_iov * const v, const uint16_t len)
{
    struct w_backend * const b = v->w->b;

    // find a tx ring with space
    struct netmap_ring * txr = 0;
    uint32_t r = 0;
    for (; likely(r < b->nif->ni_tx_rings); r++) {
        txr = NETMAP_TXRING(b->nif, b->cur_txr);
        if (likely(!nm_ring_empty(txr)))
            // we have space in this ring
            break;

        warn(INF, "tx ring %u full; moving to next", b->cur_txr);
        b->cur_txr = (b->cur_txr + 1) % b->nif->ni_tx_rings;
    }

    // return false if all rings are full
    if (unlikely(r == b->nif->ni_tx_rings)) {
        warn(INF, "all tx rings are full");
        return false;
    }

    struct netmap_slot * const s = &txr->slot[txr->cur];
    b->slot_buf[txr->ringid][txr->cur] = v;
    s->len = len + sizeof(struct eth_hdr);

    if (unlikely(nm_ring_space(txr) == 1 || sq_next(v, next) == 0)) {
        // we are using the last slot in this ring, or this is the last w_iov in
        // this batch - mark the slot for reporting
        s->flags = NS_REPORT | NS_BUF_CHANGED;
    } else
        s->flags = NS_BUF_CHANGED;

    warn(DBG, "placing iov idx %u into tx ring %u slot %d (swap with %u)",
         v->idx, b->cur_txr, txr->cur, s->buf_idx);
    // temporarily place v into the current tx ring
    const uint32_t slot_idx = s->buf_idx;
    s->buf_idx = v->idx;
    v->idx = slot_idx;

#if !defined(NDEBUG) && DLEVEL >= DBG
    char src[ETH_ADDR_STRLEN];
    char dst[ETH_ADDR_STRLEN];
    const struct eth_hdr * const eth = (void *)v->base;
    warn(DBG, "Eth %s -> %s, type %d, len %lu", ether_ntoa_r(&eth->src, src),
         ether_ntoa_r(&eth->dst, dst), ntohs(eth->type), len + sizeof(*eth));
#endif

    // advance tx ring
    txr->head = txr->cur = nm_ring_next(txr, txr->cur);
    return true;
}
