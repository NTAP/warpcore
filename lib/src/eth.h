// SPDX-License-Identifier: BSD-2-Clause
//
// Copyright (c) 2014-2018, NetApp, Inc.
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

#pragma once

#include <arpa/inet.h>
#include <netinet/if_ether.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifndef __linux__
#include <sys/types.h>
#endif

#include <warpcore/warpcore.h> // IWYU pragma: keep


/// An [Ethernet II MAC
/// header](https://en.wikipedia.org/wiki/Ethernet_frame#Ethernet_II).
///
struct eth_hdr {
    struct ether_addr dst; ///< Destination MAC address.
    struct ether_addr src; ///< Source MAC address.
    uint16_t type;         ///< EtherType of the payload data.
};


#define ETH_TYPE_IP htons(0x0800)  ///< EtherType for IPv4.
#define ETH_TYPE_ARP htons(0x0806) ///< EtherType for ARP.

#define ETH_ADDR_STRLEN ETHER_ADDR_LEN * 3

/// Return a pointer to the first data byte inside the Ethernet frame in @p buf.
///
/// @param      buf   The buffer to find data in.
///
/// @return     Pointer to the first data byte inside @p buf.
///
#define eth_data(buf) ((buf) + sizeof(struct eth_hdr))


extern void __attribute__((nonnull))
eth_tx_rx_cur(struct w_engine * w, void * const buf, const uint16_t len);

extern void __attribute__((nonnull))
eth_rx(struct w_engine * const w, struct netmap_ring * const r);

#ifndef HAVE_ETHER_NTOA_R
extern char * __attribute__((nonnull))
ether_ntoa_r(const struct ether_addr * const addr, char * const buf);
#endif


#ifdef WITH_NETMAP
// IWYU pragma: no_include <net/netmap.h>
#include <net/netmap_user.h> // IWYU pragma: keep

#include "backend.h" // IWYU pragma: keep


/// Places an Ethernet frame into a TX ring. The Ethernet frame is contained in
/// the w_iov @p v, and will be placed into an available slot in a TX ring or -
/// if all are full - dropped.
///
/// @param      w     Backend engine.
/// @param      v     The w_iov containing the Ethernet frame to transmit.
/// @param[in]  len   The length of the Ethernet *payload* contained in @p v.
///
/// @return     True if the buffer was placed into a TX ring, false otherwise.
///
static inline bool __attribute__((nonnull, always_inline))
eth_tx(struct w_engine * const w, struct w_iov * const v, const uint16_t len)
{
    // find a tx ring with space
    struct netmap_ring * txr = 0;
    for (uint32_t r = 0; likely(r < w->b->nif->ni_tx_rings); r++) {
        txr = NETMAP_TXRING(w->b->nif, w->b->cur_txr);
        if (likely(nm_ring_space(txr)))
            // we have space in this ring
            break;

        warn(INF, "tx ring %u full; moving to next", w->b->cur_txr);
        w->b->cur_txr = (w->b->cur_txr + 1) % w->b->nif->ni_tx_rings;
        txr = 0;
    }

    // return false if all rings are full
    if (unlikely(txr == 0)) {
        warn(INF, "all tx rings are full");
        return false;
    }

    struct netmap_slot * const s = &txr->slot[txr->cur];
    w->b->slot_buf[txr->ringid][txr->cur] = v;
    s->flags = NS_BUF_CHANGED;
    s->len = len + sizeof(struct eth_hdr);
    warn(DBG, "%s iov idx %u into tx ring %u slot %d (%s %u)",
         is_pipe(w) ? "copying" : "placing", v->idx, w->b->cur_txr, txr->cur,
         is_pipe(w) ? "idx" : "swap with", s->buf_idx);
    if (unlikely(is_pipe(w)))
        // for netmap pipes, we need to copy the buffer into the slot
        memcpy(NETMAP_BUF(txr, s->buf_idx), IDX2BUF(w, v->idx), s->len);
    else {
        // for NIC rings, temporarily place v into the current tx ring
        const uint32_t slot_idx = s->buf_idx;
        s->buf_idx = v->idx;
        v->idx = slot_idx;
    }

#if !defined(NDEBUG) && DLEVEL >= DBG
    char src[ETH_ADDR_STRLEN];
    char dst[ETH_ADDR_STRLEN];
    const struct eth_hdr * const eth = (void *)NETMAP_BUF(txr, s->buf_idx);
    warn(DBG, "Eth %s -> %s, type %d, len %lu", ether_ntoa_r(&eth->src, src),
         ether_ntoa_r(&eth->dst, dst), ntohs(eth->type), len + sizeof(*eth));
#endif

    // advance tx ring
    txr->head = txr->cur = nm_ring_next(txr, txr->cur);
    return true;
}
#endif
