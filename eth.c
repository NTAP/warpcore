#include <netinet/in.h> 	// ntohs
#include <sys/types.h>		// ether_ntoa_r
#include <net/ethernet.h>	// ether_ntoa_r

#define NETMAP_WITH_LIBS
#include <net/netmap_user.h>

#include "debug.h"
#include "eth.h"
#include "ip.h"


void eth_tx(const struct nm_desc * const nm, struct netmap_ring *rxr) {
	struct netmap_ring *txr = NETMAP_TXRING(nm->nifp, nm->cur_tx_ring);
	struct netmap_slot *rx_slot = &rxr->slot[rxr->cur];
	struct netmap_slot *tx_slot = &txr->slot[txr->cur];
	const uint32_t tmp_idx = tx_slot->buf_idx;
	struct eth_hdr * const eth =
		(struct eth_hdr * const)NETMAP_BUF(rxr, rx_slot->buf_idx);

	uint8_t tmp[ETH_ADDR_LEN];
	memcpy(tmp, eth->src, sizeof tmp);
	memcpy(eth->src, eth->dst, sizeof eth->src);
	memcpy(eth->dst, tmp, sizeof eth->dst);

	tx_slot->buf_idx = rx_slot->buf_idx;
	tx_slot->len = rx_slot->len;
	rx_slot->flags = tx_slot->flags = NS_BUF_CHANGED;
	rx_slot->buf_idx = tmp_idx;
	rxr->head = rxr->cur = nm_ring_next(rxr, rxr->cur);
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


void eth_rx(const struct nm_desc * const nm, struct netmap_ring *ring) {
	const struct eth_hdr * const eth =
		(const struct eth_hdr * const)NETMAP_BUF(ring, ring->slot[ring->cur].buf_idx);
	const uint16_t type = ntohs(eth->type);

#ifdef D
	char src[ETH_ADDR_LEN*3];
	char dst[ETH_ADDR_LEN*3];
	D("Eth %s -> %s, type %d",
		ether_ntoa_r((struct ether_addr *)eth->src, src),
		ether_ntoa_r((struct ether_addr *)eth->dst, dst),
		type);
#endif

	switch (type) {
		case ETHERTYPE_ARP:
			break;
		case ETHERTYPE_IP:
			ip_rx(nm, ring, sizeof(struct eth_hdr));
			break;
		default:
			D("unhandled ethertype %x", type);
	}
}
