#include <netinet/in.h> 	// ntohs
#include <sys/types.h>		// ether_ntoa_r
#include <net/ethernet.h>	// ether_ntoa_r

#define NETMAP_WITH_LIBS
#include <net/netmap_user.h>

#include "debug.h"
#include "eth.h"
#include "ip.h"


void eth_tx(const struct nm_desc * const nm, const char * const buf) {
	struct netmap_ring *rxr = NETMAP_RXRING(nm->nifp, nm->cur_rx_ring);
	struct netmap_ring *txr = NETMAP_TXRING(nm->nifp, nm->cur_tx_ring);
	struct netmap_slot *rxs = &rxr->slot[rxr->cur];
	struct netmap_slot *txs = &txr->slot[txr->cur];

	// swap the src and dst eth addresses
	struct eth_hdr * const eth = (struct eth_hdr * const)(buf);
	uint8_t tmp[ETH_ADDR_LEN];
	memcpy(tmp, eth->src, sizeof tmp);
	memcpy(eth->src, eth->dst, sizeof eth->src);
	memcpy(eth->dst, tmp, sizeof eth->dst);

	// move modified rx slot to tx ring, and move an unused tx slot back
	const uint32_t tmp_idx = txs->buf_idx;
	D("swapping rx slot %d (buf_idx %d) and tx slot %d (buf_idx %d)",
		rxr->cur, rxs->buf_idx, txr->cur, txs->buf_idx);
	// const uint16_t tmp_len = txs->len;
	txs->buf_idx = rxs->buf_idx;
	txs->len = rxs->len;
	txs->flags = NS_BUF_CHANGED;
	rxs->buf_idx = tmp_idx;
	// the man page example doesn't set the rxs length, so let's not either
	// rxs->len = tmp_len;
	rxs->flags = NS_BUF_CHANGED;
	// we don't need to advance the rx ring here, the main loop
	// currently does this
	// rxr->head = rxr->cur = nm_ring_next(rxr, rxr->cur);
	txr->head = txr->cur = nm_ring_next(txr, txr->cur);

#ifdef D
	char src[ETH_ADDR_LEN*3];
	char dst[ETH_ADDR_LEN*3];
	D("Eth %s -> %s, type %d",
		ether_ntoa_r((struct ether_addr *)eth->src, src),
		ether_ntoa_r((struct ether_addr *)eth->dst, dst),
		ntohs(eth->type));
#endif
}


void eth_rx(const struct nm_desc * const nm, const char * const buf) {
	const struct eth_hdr * const eth = (const struct eth_hdr * const)(buf);
	const uint16_t type = ntohs(eth->type);

#ifdef D
	char src[ETH_ADDR_LEN*3];
	char dst[ETH_ADDR_LEN*3];
	D("Eth %s -> %s, type %d",
		ether_ntoa_r((struct ether_addr *)eth->src, src),
		ether_ntoa_r((struct ether_addr *)eth->dst, dst),
		type);
#endif

	// TODO: make sure the packet is for us (or broadcast)

	switch (type) {
		// case ETHERTYPE_ARP:
		// 	break;
		case ETHERTYPE_IP:
			ip_rx(nm, buf);
			break;
		default:
			D("unhandled ethertype %x", type);
			abort();
	}
}
