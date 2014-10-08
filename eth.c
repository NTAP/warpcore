#include <arpa/inet.h>
#include <string.h>

#ifdef __linux__
#include <netinet/ether.h>
#else
#include <sys/types.h>
#include <net/ethernet.h>
#endif

#include "warpcore.h"
#include "arp.h"
#include "ip.h"


// Modify the current receive buffer, swap it into the tx ring (and move a
// buffer from the tx ring to the rx ring) and transmit it.
// This is currently only used for sending replies to inbound ICMP and ARP
// requests.
void eth_tx_rx_cur(struct warpcore *w, char * const buf,
                   const uint16_t len)
{
	struct netmap_ring * const rxr = NETMAP_RXRING(w->nif, w->cur_rxr);
	struct netmap_ring * const txr = NETMAP_TXRING(w->nif, w->cur_txr);
	struct netmap_slot * const rxs = &rxr->slot[rxr->cur];
	struct netmap_slot * const txs = &txr->slot[txr->cur];

	// make the original src address the new dst, and set the src
	struct eth_hdr * const eth = (struct eth_hdr * const)(buf);
	memcpy(eth->dst, eth->src, sizeof eth->dst);
	memcpy(eth->src, w->mac, sizeof eth->src);

	dlog(info, "swapping rx ring %d slot %d (buf %d) and "
	     "tx ring %d slot %d (buf %d)", w->cur_rxr, rxr->cur, rxs->buf_idx,
	     w->cur_txr, txr->cur, txs->buf_idx);

	// move modified rx slot to tx ring, and move an unused tx slot back
	const uint32_t tmp_idx = txs->buf_idx;
	txs->buf_idx = rxs->buf_idx;
	rxs->buf_idx = tmp_idx;
	txs->len = len + sizeof(struct eth_hdr);
	txs->flags = rxs->flags = NS_BUF_CHANGED;
	// we don't need to advance the rx ring here
	txr->head = txr->cur = nm_ring_next(txr, txr->cur);

#ifndef NDEBUG
	char src[ETH_ADDR_STRLEN];
	char dst[ETH_ADDR_STRLEN];
	dlog(notice, "Eth %s -> %s, type %d",
	     ether_ntoa_r((const struct ether_addr *)eth->src, src),
	     ether_ntoa_r((const struct ether_addr *)eth->dst, dst),
	     ntohs(eth->type));
#endif
}



