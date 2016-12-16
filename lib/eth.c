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

// clang-format off
// because these includes need to be in-order
#include <net/if.h> // IWYU pragma: keep
#include <stdint.h>
#include <net/netmap.h>
// clang-format on
#include <stdbool.h>
#include <string.h>
#include <sys/queue.h>

#ifdef __linux__
#include <netinet/ether.h>
#else
#include <arpa/inet.h>
// clang-format off
// because these includes need to be in-order
#include <sys/types.h> // IWYU pragma: keep
#include <net/ethernet.h>
// clang-format on
#endif

#include "arp.h"
#include "backend.h"
#include "eth.h"
#include "ip.h"
#include "warpcore.h"


/// Receive an Ethernet frame. This is the lowest-level RX function, called for
/// each new inbound frame from w_rx(). Dispatches the frame to either ip_rx()
/// or arp_rx(), based on its EtherType.
///
/// The Ethernet frame to operate on is in the current netmap lot of the
/// indicated RX ring.
///
/// @param      w     Warpcore engine.
/// @param      r     Currently active netmap RX ring.
///
void eth_rx(struct warpcore * const w, struct netmap_ring * const r)
{
    struct eth_hdr * const eth = (void *)NETMAP_BUF(r, r->slot[r->cur].buf_idx);

#ifndef NDEBUG
    char src[ETH_ADDR_STRLEN];
    char dst[ETH_ADDR_STRLEN];
    warn(debug, "Eth %s -> %s, type %d, len %d",
         ether_ntoa_r((const struct ether_addr *)eth->src, src),
         ether_ntoa_r((const struct ether_addr *)eth->dst, dst),
         ntohs(eth->type), r->slot[r->cur].len);
#endif

    // make sure the packet is for us (or broadcast)
    if (unlikely(memcmp(eth->dst, w->mac, ETH_ADDR_LEN) &&
                 memcmp(eth->dst, ETH_BCAST, ETH_ADDR_LEN))) {
        warn(info, "Ethernet packet not destined to us; ignoring");
        return;
    }

    if (likely(eth->type == ETH_TYPE_IP))
        ip_rx(w, r);
    else if (eth->type == ETH_TYPE_ARP)
        arp_rx(w, r);
    else
        die("unhandled ethertype 0x%04x", ntohs(eth->type));
}


/// Places an Ethernet frame into a TX ring. The Ethernet frame is contained in
/// the w_iov @p v, and will be placed into an available slot in a TX ring or -
/// if all are full - dropped.
///
/// @param      w     Warpcore engine.
/// @param      v     The w_iov containing the Ethernet frame to transmit.
/// @param[in]  len   The length of the Ethernet *payload* contained in @p v.
///
/// @return     True if the buffer was placed into a TX ring, false otherwise.
///
bool eth_tx(struct warpcore * const w,
            struct w_iov * const v,
            const uint16_t len)
{
    // find a tx ring with space
    struct netmap_ring * txr = 0;
    for (uint32_t r = 0; r < w->nif->ni_tx_rings; r++) {
        txr = NETMAP_TXRING(w->nif, w->cur_txr);
        if (likely(nm_ring_space(txr)))
            // we have space in this ring
            break;

        w->cur_txr = (w->cur_txr + 1) % w->nif->ni_tx_rings;
        txr = 0;
        warn(info, "current tx ring full; moving to tx ring %u", w->cur_txr);
    }

    // return false if all rings are full
    if (unlikely(txr == 0)) {
        warn(warn, "all tx rings are full");
        return false;
    }

    // remember the slot we're placing this buffer into
    v->ring = w->cur_txr;
    v->slot = txr->cur;
    SLIST_INSERT_HEAD(&w->tx_pending, v, next_tx);
    struct netmap_slot * const txs = &txr->slot[txr->cur];

    // prefetch the next slot into the cache, too
    __builtin_prefetch(
        NETMAP_BUF(txr, txr->slot[nm_ring_next(txr, txr->cur)].buf_idx));

    warn(debug, "placing iov buf %u in tx ring %u slot %d (current buf %u)",
         v->idx, v->ring, txr->cur, txs->buf_idx);

    // place v in the current tx ring
    const uint32_t tmp_idx = txs->buf_idx;
    txs->buf_idx = v->idx;
    txs->len = len + sizeof(struct eth_hdr);
    txs->flags = NS_BUF_CHANGED;

#ifndef NDEBUG
    char src[ETH_ADDR_STRLEN];
    char dst[ETH_ADDR_STRLEN];
    const struct eth_hdr * const eth = (void *)NETMAP_BUF(txr, txs->buf_idx);
    warn(debug, "Eth %s -> %s, type %d, len %lu",
         ether_ntoa_r((const struct ether_addr *)eth->src, src),
         ether_ntoa_r((const struct ether_addr *)eth->dst, dst),
         ntohs(eth->type), len + sizeof(*eth));
#endif

    // place the original tx buffer in v
    v->idx = tmp_idx;

    // advance tx ring
    txr->head = txr->cur = nm_ring_next(txr, txr->cur);

    return true;
}
