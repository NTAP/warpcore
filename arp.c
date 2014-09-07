#include <arpa/inet.h>
#include <sys/types.h>
#include <net/ethernet.h>
#include <string.h>

#include "warpcore.h"
#include "arp.h"
#include "ip.h"


void arp_tx(struct warpcore * w, const char * const buf)
{
#ifndef NDEBUG
	struct arp_hdr * const arp =
		(struct arp_hdr * const)(buf + sizeof(struct eth_hdr));

	char sha[ETH_ADDR_LEN*3];
	char spa[IP_ADDR_STRLEN];
	log("ARP reply %s is at %s",
	    ip_ntoa(arp->spa, spa, sizeof spa),
	    ether_ntoa_r((struct ether_addr *)arp->sha, sha));
#endif

	// do Ethernet transmit preparation
	eth_tx(w, buf, sizeof(struct arp_hdr));
}


void arp_rx(struct warpcore * w, const char * const buf)
{
	struct arp_hdr * const arp =
		(struct arp_hdr * const)(buf + sizeof(struct eth_hdr));

	const uint_fast16_t hrd = ntohs(arp->hrd);
	if (hrd != ARP_HRD_ETHER || arp->hln != ETH_ADDR_LEN)
		die("unhandled ARP hardware format %d with len %d",
		    hrd, arp->hln);

	const uint_fast16_t pro = ntohs(arp->pro);
	if (pro != ETH_TYPE_IP || arp->pln != IP_ADDR_LEN)
		die("unhandled ARP protocol format %d with len %d",
		    pro, arp->pln);

	const uint_fast16_t op = ntohs(arp->op);
	switch (op) {
	case ARP_OP_REQUEST:
		{
#ifndef NDEBUG
		char spa[IP_ADDR_STRLEN];
		char tpa[IP_ADDR_STRLEN];
		log("ARP request who as %s tell %s",
		    ip_ntoa(arp->spa, spa, sizeof spa),
		    ip_ntoa(arp->tpa, tpa, sizeof tpa));
#endif
		if (arp->tpa == w->ip) {
			arp->op = htons(ARP_OP_REPLY);
			memcpy(arp->tha, arp->sha, sizeof arp->tha);
			arp->tpa = arp->spa;
			memcpy(arp->sha, w->mac, sizeof arp->sha);
			arp->spa = w->ip;
			arp_tx(w, buf);
		} else
			log("ignoring ARP request not asking for us");
		break;
		}

	default:
		die("unhandled ARP operation %d", op);
		break;
	}
}
