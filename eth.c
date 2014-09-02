#include <netinet/in.h> 	// ntohs
#include <sys/types.h>		// ether_ntoa_r
#include <net/ethernet.h>	// ether_ntoa_r

#include "debug.h"
#include "eth.h"
#include "ip.h"


void eth_receive(const char * const buf) {
	const struct eth_hdr * const eth = (const struct eth_hdr * const)buf;
	const uint16_t type = ntohs(eth->type);
	char src[ETH_ADDR_LEN*3];
	char dst[ETH_ADDR_LEN*3];

	D("Eth %s -> %s, type %d",
		ether_ntoa_r((struct ether_addr *)eth->src, src),
		ether_ntoa_r((struct ether_addr *)eth->dst, dst),
		type);

	switch (type) {
		case ETHERTYPE_ARP:
			break;
		case ETHERTYPE_IP:
			ip_receive(buf + sizeof(struct eth_hdr));
			break;
		default:
			D("unhandled ethertype %x", type);
	}

}
