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

#include <string.h>

#include <net/netmap_user.h>

#include <net/netmap.h>
#include <warpcore/warpcore.h>

#include "arp.h"
#include "backend.h"
#include "eth.h"
#include "ip4.h"
#include "ip6.h"


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

    warn(DBG, "Eth %s -> %s, type 0x%04x, len %d",
         eth_ntoa(&eth->src, eth_tmp, ETH_STRLEN),
         eth_ntoa(&eth->dst, eth_tmp, ETH_STRLEN), bswap16(eth->type), s->len);

#ifndef FUZZING
    // make sure the packet is for us (or broadcast)
    if (unlikely((memcmp(&eth->dst, &w->mac, sizeof(eth->dst)) != 0) &&
                 (memcmp(&eth->dst, ETH_ADDR_BCAST, sizeof(eth->dst)) != 0) &&
                 (memcmp(&eth->dst, ETH_ADDR_MCAST6, 2) != 0))) {
        warn(INF, "Ethernet packet to %s not destined to us (%s); ignoring",
             eth_ntoa(&eth->dst, eth_tmp, ETH_STRLEN),
             eth_ntoa(&w->mac, eth_tmp, ETH_STRLEN));
        return false;
    }
#endif

    switch (eth->type) {
    case ETH_TYPE_IP6:
        return likely(w->have_ip6) ? ip6_rx(w, s, buf) : false;
    case ETH_TYPE_IP4:
        return likely(w->have_ip4) ? ip4_rx(w, s, buf) : false;
    case ETH_TYPE_ARP:
        if (likely(w->have_ip4))
            arp_rx(w, buf);
        return false;
    }

    warn(INF, "unhandled ethertype 0x%04x", bswap16(eth->type));
    return false;
}


/// Places an Ethernet frame into a TX ring. The Ethernet frame is contained in
/// the w_iov @p v, and will be placed into an available slot in a TX ring or -
/// if all are full - dropped.
///
/// @param      v     The w_iov containing the Ethernet frame to transmit.
///
/// @return     True if the buffer was placed into a TX ring, false otherwise.
///
bool eth_tx(struct w_iov * const v)
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
        warn(NTE, "all tx rings are full");
        return false;
    }

    struct netmap_slot * const s = &txr->slot[txr->cur];
    b->slot_buf[txr->ringid][txr->cur] = v;
    s->len = v->len + sizeof(struct eth_hdr);

    warn(DBG, "Eth %s -> %s, type 0x%04x, len %u",
         eth_ntoa(&((struct eth_hdr *)(void *)v->base)->src, eth_tmp,
                  ETH_STRLEN),
         eth_ntoa(&((struct eth_hdr *)(void *)v->base)->dst, eth_tmp,
                  ETH_STRLEN),
         bswap16(((struct eth_hdr *)(void *)v->base)->type), s->len);


    if (unlikely(is_pipe(v->w))) {
#if 0
        warn(DBG, "copying iov idx %u into tx ring %u slot %d (into %u)",
             v->idx, b->cur_txr, txr->cur, s->buf_idx);
#endif
        memcpy(NETMAP_BUF(txr, s->buf_idx), v->base, s->len);

    } else {
#if 0
        warn(DBG, "placing iov idx %u into tx ring %u slot %d (swap with %u)",
             v->idx, b->cur_txr, txr->cur, s->buf_idx);
#endif

        // temporarily place v into the current tx ring
        const uint32_t slot_idx = s->buf_idx;
        s->buf_idx = v->idx;
        v->idx = slot_idx;
        s->flags = NS_BUF_CHANGED;
        if (unlikely(nm_ring_space(txr) == 1 || sq_next(v, next) == 0)) {
            // we are using the last slot in this ring, or this is the last
            // w_iov in this batch - mark the slot for reporting
            s->flags |= NS_REPORT;
        }
    }

    // advance tx ring
    txr->head = txr->cur = nm_ring_next(txr, txr->cur);
    return true;
}


/// Wait until @p v has been transmitted, and return free it.
///
/// @param      v     The w_iov containing the Ethernet frame to transmit.
///
void eth_tx_and_free(struct w_iov * const v)
{
    // send the packet, and make sure it went out before returning
    const uint32_t orig_idx = v->idx;
    eth_tx(v);
    do {
        w_nanosleep(100 * NS_PER_US);
        w_nic_tx(v->w);
    } while (v->idx != orig_idx);
    sq_insert_head(&v->w->iov, v, next);
}
