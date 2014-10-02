#include <string.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>

#ifdef __linux__
#include <netinet/ether.h>
#else
#include <sys/types.h>
#include <net/ethernet.h>
#endif

#include "warpcore.h"
#include "arp.h"
#include "ip.h"


// This modifies the ARP query in the current receive buffer into an ARP reply
// and sends it out.
static void
arp_is_at(struct warpcore * w, char * const buf)
{
	struct arp_hdr * const arp =
		(struct arp_hdr * const)(buf + sizeof(struct eth_hdr));
#ifndef NDEBUG
	char tha[ETH_ADDR_STRLEN];
	char tpa[IP_ADDR_STRLEN];
	log(3, "ARP reply %s is at %s",
	    ip_ntoa(arp->tpa, tpa, sizeof tpa),
	    ether_ntoa_r((const struct ether_addr *)arp->tha, tha));
#endif

	// modify ARP header
	arp->op = htons(ARP_OP_REPLY);
	memcpy(arp->tha, arp->sha, sizeof arp->tha);
	arp->tpa = arp->spa;
	memcpy(arp->sha, w->mac, sizeof arp->sha);
	arp->spa = w->ip;

	// send the Ethernet packet
	eth_tx_rx_cur(w, buf, sizeof(struct arp_hdr));
	w_kick_tx(w);
}


// Receive an ARP packet, and react
void
arp_rx(struct warpcore * w, char * const buf)
{
#ifndef NDEBUG
	char tpa[IP_ADDR_STRLEN];
	char spa[IP_ADDR_STRLEN];
	char sha[ETH_ADDR_STRLEN];
#endif
	const struct arp_hdr * const arp =
		(const struct arp_hdr * const)(buf + sizeof(struct eth_hdr));

	const uint16_t hrd = ntohs(arp->hrd);
	if (hrd != ARP_HRD_ETHER || arp->hln != ETH_ADDR_LEN)
		die("unhandled ARP hardware format %d with len %d",
		    hrd, arp->hln);

	if (arp->pro != ETH_TYPE_IP || arp->pln != IP_ADDR_LEN)
		die("unhandled ARP protocol format %d with len %d",
		    ntohs(arp->pro), arp->pln);

	const uint16_t op = ntohs(arp->op);
	switch (op) {
	case ARP_OP_REQUEST:
		log(3, "ARP request who has %s tell %s",
		    ip_ntoa(arp->tpa, tpa, sizeof tpa),
		    ip_ntoa(arp->spa, spa, sizeof spa));
		if (arp->tpa == w->ip)
			arp_is_at(w, buf);
		else
			log(3, "ignoring ARP request not asking for us");
		break;

	case ARP_OP_REPLY:
		{
		log(3, "ARP reply %s is at %s",
		    ip_ntoa(arp->spa, spa, sizeof spa),
		    ether_ntoa_r((const struct ether_addr *)arp->sha, sha));

		// check if any socket has an IP address matching this ARP
		// reply, and if so, set its destination MAC
		struct w_sock *s;
		SLIST_FOREACH(s, &w->sock, next)
			if (s->dip == arp->spa) {
				log(1, "updating socket with %s for %s",
				    ether_ntoa_r((const struct ether_addr *)
				                 arp->sha, sha),
				    ip_ntoa(arp->spa, spa, sizeof spa));
				memcpy(s->dmac, arp->sha, ETH_ADDR_LEN);
			}
		break;
		}

	default:
		die("unhandled ARP operation %d", op);
		break;
	}
}
