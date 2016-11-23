#include <arpa/inet.h>
#include <xmmintrin.h>

#ifdef __linux__
#include <netinet/ether.h>
#else
// clang-format off
// because these includes need to be in-order
#include <sys/types.h>
#include <net/ethernet.h>
// clang-format on
#endif

#include "arp.h"
#include "backend.h"
#include "util.h"


/// Special version of eth_tx() that transmits the current receive buffer after
/// in-place modification. Modifies the current receive buffer in place by
/// swapping source and destination MAC addresses. Moves the buffer into the TX
/// ring (swapping a fresh buffer from the TX ring into its place in the RX
/// ring) and then transmits it.
///
/// This special version of eth_tx() is currently only used for sending replies
/// to inbound ICMP and ARP requests, in icmp_tx() (via ip_tx_with_rx_buf()) and
/// arp_is_at().
///
/// @param      w     Warpcore engine.
/// @param      buf   Ethernet frame to modify and transmit.
/// @param[in]  len   Length of the Ethernet frame in @p buf.
///
void __attribute__((nonnull))
eth_tx_rx_cur(struct warpcore * w, void * const buf, const uint16_t len)
{
    struct netmap_ring * const rxr = NETMAP_RXRING(w->nif, w->cur_rxr);
    struct netmap_ring * const txr = NETMAP_TXRING(w->nif, w->cur_txr);
    struct netmap_slot * const rxs = &rxr->slot[rxr->cur];
    struct netmap_slot * const txs = &txr->slot[txr->cur];

    // make the original src address the new dst, and set the src
    struct eth_hdr * const eth = buf;
    memcpy(eth->dst, eth->src, sizeof eth->dst);
    memcpy(eth->src, w->mac, sizeof eth->src);

    warn(info, "swapping rx ring %u slot %d (buf %d) and "
               "tx ring %u slot %d (buf %d)",
         w->cur_rxr, rxr->cur, rxs->buf_idx, w->cur_txr, txr->cur,
         txs->buf_idx);

    // move modified rx slot to tx ring, and move an unused tx slot back
    const uint32_t tmp_idx = txs->buf_idx;
    txs->buf_idx = rxs->buf_idx;
    rxs->buf_idx = tmp_idx;
    txs->len = len + sizeof(struct eth_hdr);
    txs->flags = rxs->flags = NS_BUF_CHANGED;
    // we don't need to advance the rx ring here
    txr->head = txr->cur = nm_ring_next(txr, txr->cur);

    warn(notice, "Eth %s -> %s, type %d",
         ether_ntoa((const struct ether_addr * const)eth->src),
         ether_ntoa((const struct ether_addr * const)eth->dst),
         ntohs(eth->type));
}


/// Receive an Ethernet frame. This is the lowest-level RX function, called for
/// each new inbound frame from w_rx(). Dispatches the frame to either ip_rx()
/// or arp_rx(), based on its EtherType.
///
/// @param      w     Warpcore engine.
/// @param      buf   Buffer containing the inbound Ethernet frame.
///
void __attribute__((nonnull))
eth_rx(struct warpcore * const w, void * const buf)
{
    struct eth_hdr * const eth = buf;
    warn(info, "Eth %s -> %s, type %d",
         ether_ntoa((const struct ether_addr * const)eth->src),
         ether_ntoa((const struct ether_addr * const)eth->dst),
         ntohs(eth->type));

    // make sure the packet is for us (or broadcast)
    if (unlikely(memcmp(eth->dst, w->mac, ETH_ADDR_LEN) &&
                 memcmp(eth->dst, ETH_BCAST, ETH_ADDR_LEN))) {
        warn(warn, "Ethernet packet not destined to us; ignoring");
        return;
    }

    if (likely(eth->type == ETH_TYPE_IP))
        ip_rx(w, buf);
    else if (eth->type == ETH_TYPE_ARP)
        arp_rx(w, buf);
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
bool __attribute__((nonnull))
eth_tx(struct warpcore * const w, struct w_iov * const v, const uint16_t len)
{
    // check if there is space in the current txr
    struct netmap_ring * txr = 0;
    uint32_t i;
    for (i = 0; i < w->nif->ni_tx_rings; i++) {
        txr = NETMAP_TXRING(w->nif, w->cur_txr);
        if (likely(nm_ring_space(txr)))
            // we have space in this ring
            break;
        else {
            // current txr is full, try next
            w->cur_txr = (w->cur_txr + 1) % w->nif->ni_tx_rings;
            warn(warn, "moving to tx ring %u", w->cur_txr);
        }
    }

    // return false if all rings are full
    if (unlikely(i == w->nif->ni_tx_rings)) {
        warn(warn, "all tx rings are full");
        return false;
    }

    struct netmap_slot * const txs = &txr->slot[txr->cur];

    // prefetch the next slot into the cache, too
    _mm_prefetch(
        NETMAP_BUF(txr, txr->slot[nm_ring_next(txr, txr->cur)].buf_idx),
        _MM_HINT_T1);

    warn(debug, "placing iov buf %u in tx ring %u slot %d (current buf %u)",
         v->idx, w->cur_txr, txr->cur, txs->buf_idx);

    // place v in the current tx ring
    const uint32_t tmp_idx = txs->buf_idx;
    txs->buf_idx = v->idx;
    txs->len = len + sizeof(struct eth_hdr);
    txs->flags = NS_BUF_CHANGED;

#ifndef NDEBUG
    const struct eth_hdr * const eth = (void *)NETMAP_BUF(txr, txs->buf_idx);
    warn(info, "Eth %s -> %s, type %d, len %lu",
         ether_ntoa((const struct ether_addr * const)eth->src),
         ether_ntoa((const struct ether_addr * const)eth->dst),
         ntohs(eth->type), len + sizeof(struct eth_hdr));
#endif

    // place the original tx buffer in v
    v->idx = tmp_idx;

    // advance tx ring
    txr->head = txr->cur = nm_ring_next(txr, txr->cur);

    // caller needs to make iovs available again and optionally kick tx
    return true;
}
