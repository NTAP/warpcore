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

#if !defined(NDEBUG) && defined(FUNCTRACE)
	log("swapping rx ring %d slot %d (buf %d) and "
	    "tx ring %d slot %d (buf %d)", w->cur_rxr, rxr->cur, rxs->buf_idx,
	    w->cur_txr, txr->cur, txs->buf_idx);
#endif

	// move modified rx slot to tx ring, and move an unused tx slot back
	const uint32_t tmp_idx = txs->buf_idx;
	txs->buf_idx = rxs->buf_idx;
	rxs->buf_idx = tmp_idx;
	txs->len = len + sizeof(struct eth_hdr);
	txs->flags = rxs->flags = NS_BUF_CHANGED;
	// we don't need to advance the rx ring here, w_poll does this
	txr->head = txr->cur = nm_ring_next(txr, txr->cur);

#if !defined(NDEBUG) && defined(PKTTRACE)
	char src[ETH_ADDR_STRLEN];
	char dst[ETH_ADDR_STRLEN];
	log("Eth %s -> %s, type %d",
	    ether_ntoa_r((const struct ether_addr *)eth->src, src),
	    ether_ntoa_r((const struct ether_addr *)eth->dst, dst), ntohs(eth->type));
#endif
}


// Swap the buffer in the iov into the tx ring, placing an empty one
// into the iov.
bool eth_tx(struct warpcore *w, struct w_iov * const v, const uint16_t len)
{
	// check if there is space in the current txr
	struct netmap_ring *txr = 0;
	uint16_t i;
	for (i = 0; i < w->nif->ni_rx_rings; i++) {
		txr = NETMAP_TXRING(w->nif, w->cur_rxr);
		if (txr->tail != nm_ring_next(txr, txr->cur))
			// we have space in this ring
			break;
		else
			// current txr is full, try next
			w->cur_txr = (w->cur_txr + 1) % w->nif->ni_tx_rings;
	}

	// return false if all rings are full
	if (i == w->nif->ni_rx_rings) {
		log("all tx rings are full");
		return false;
	}

	struct netmap_slot * const txs = &txr->slot[txr->cur];

#if !defined(NDEBUG) && defined(FUNCTRACE)
	log("placing iov %d in tx ring %d slot %d (current buf %d)",
	    v->idx, w->cur_txr, txr->cur, txs->buf_idx);
#endif

	// place v in the current tx ring
	const uint32_t tmp_idx = txs->buf_idx;
	txs->buf_idx = v->idx;
	txs->len = len + sizeof(struct eth_hdr);
	txs->flags = NS_BUF_CHANGED;

#if !defined(NDEBUG) && defined(PKTTRACE)
	const struct eth_hdr * const eth =
		(const struct eth_hdr * const)NETMAP_BUF(txr, txs->buf_idx);
	char src[ETH_ADDR_STRLEN];
	char dst[ETH_ADDR_STRLEN];
	log("Eth %s -> %s, type %d, len %ld",
	    ether_ntoa_r((const struct ether_addr *)eth->src, src),
	    ether_ntoa_r((const struct ether_addr *)eth->dst, dst),
	    ntohs(eth->type), len + sizeof(struct eth_hdr));
#endif

	// place the original rx buffer in v
	v->idx = tmp_idx;

	// advance tx ring
	txr->head = txr->cur = nm_ring_next(txr, txr->cur);

	// caller needs to make iovs available again and optionally kick tx
	return true;
}


// Receive an Ethernet packet. This is the lowest level inbound function,
// called from w_poll.
void eth_rx(struct warpcore * w, char * const buf)
{
	const struct eth_hdr * const eth = (const struct eth_hdr * const)(buf);
	const uint16_t type = ntohs(eth->type);

#if !defined(NDEBUG) && defined(PKTTRACE)
	char src[ETH_ADDR_STRLEN];
	char dst[ETH_ADDR_STRLEN];
	log("Eth %s -> %s, type %d",
	    ether_ntoa_r((const struct ether_addr *)eth->src, src),
	    ether_ntoa_r((const struct ether_addr *)eth->dst, dst), type);
#endif

	// make sure the packet is for us (or broadcast)
	if (memcmp(eth->dst, w->mac, ETH_ADDR_LEN) &&
	    memcmp(eth->dst, ETH_BCAST, ETH_ADDR_LEN)) {
		log("Eth packet to %s (not us); ignoring",
		    ether_ntoa((const struct ether_addr *)eth->dst));
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
