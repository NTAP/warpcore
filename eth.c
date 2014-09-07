#include <arpa/inet.h>
#include <sys/types.h>
#include <net/ethernet.h>
#include <string.h>

#include "warpcore.h"
#include "arp.h"
#include "ip.h"


void eth_tx(struct warpcore * w, const char * const buf,
            const uint_fast16_t len)
{
	struct netmap_ring *rxr = NETMAP_RXRING(w->nif, 0);
	struct netmap_ring *txr = NETMAP_TXRING(w->nif, 0);
	struct netmap_slot *rxs = &rxr->slot[rxr->cur];
	struct netmap_slot *txs = &txr->slot[txr->cur];

	// make the original src address the new dst, and set the src
	struct eth_hdr * const eth = (struct eth_hdr * const)(buf);
	memcpy(eth->dst, eth->src, sizeof eth->dst);
	memcpy(eth->src, w->mac, sizeof eth->src);

	// move modified rx slot to tx ring, and move an unused tx slot back
	log("swapping rx slot %d (buf_idx %d) and tx slot %d (buf_idx %d)",
	    rxr->cur, rxs->buf_idx, txr->cur, txs->buf_idx);
	const uint_fast32_t tmp_idx = txs->buf_idx;
	txs->buf_idx = rxs->buf_idx;
	rxs->buf_idx = tmp_idx;
	txs->len = len + sizeof(struct eth_hdr);
	txs->flags = rxs->flags = NS_BUF_CHANGED;
	// we don't need to advance the rx ring here, the main loop
	// currently does this
	txr->head = txr->cur = nm_ring_next(txr, txr->cur);

#ifndef NDEBUG
	char src[ETH_ADDR_LEN*3];
	char dst[ETH_ADDR_LEN*3];
	log("Eth %s -> %s, type %d",
	    ether_ntoa_r((struct ether_addr *)eth->src, src),
	    ether_ntoa_r((struct ether_addr *)eth->dst, dst), ntohs(eth->type));
#endif
}


void eth_rx(struct warpcore * w, char * const buf)
{
	const struct eth_hdr * const eth = (struct eth_hdr * const)(buf);
	const uint_fast16_t type = ntohs(eth->type);

#ifndef NDEBUG
	char src[ETH_ADDR_LEN*3];
	char dst[ETH_ADDR_LEN*3];
	log("Eth %s -> %s, type %d",
	    ether_ntoa_r((struct ether_addr *)eth->src, src),
	    ether_ntoa_r((struct ether_addr *)eth->dst, dst), type);
#endif

	// make sure the packet is for us (or broadcast)
	if (memcmp(eth->dst, w->mac, ETH_ADDR_LEN) &&
	    memcmp(eth->dst, ETH_BCAST, ETH_ADDR_LEN)) {
		log("Ethernet packet not destined to us; ignoring");
		return;
	}

	switch (type) {
	case ETH_TYPE_ARP:
		arp_rx(w, buf);
		break;
	case ETH_TYPE_IP:
		ip_rx(w, buf);
		break;
	default:
		die("unhandled ethertype %x", type);
		break;
	}
}
