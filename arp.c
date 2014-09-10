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


// Use a spare iov to transmit an ARP query for the given destination
// IP address.
void arp_who_has(struct warpcore * w, const uint_fast32_t dip)
{
	// grab a spare buffer
	struct w_iov * const v = SLIST_FIRST(&w->iov);
	if (v == 0)
		die("out of spare bufs");
	SLIST_REMOVE_HEAD(&w->iov, next);
	v->buf = IDX2BUF(w, v->idx);

	// pointers to the start of the various headers
	struct eth_hdr * const eth = (struct eth_hdr *)(v->buf);
	struct arp_hdr * const arp =
		(struct arp_hdr *)((char *)(eth) + sizeof(struct eth_hdr));

	// set Ethernet header fields
	memcpy(eth->dst, ETH_BCAST, ETH_ADDR_LEN);
	memcpy(eth->src, w->mac, ETH_ADDR_LEN);
	eth->type =	htons(ETH_TYPE_ARP);

	// set ARP header fields
	arp->hrd =	htons(ARP_HRD_ETHER);
	arp->pro =	htons(ETH_TYPE_IP);
	arp->hln =	ETH_ADDR_LEN;
	arp->pln =	IP_ADDR_LEN;
	arp->op =	htons(ARP_OP_REQUEST);
	memcpy(arp->sha, w->mac, ETH_ADDR_LEN);
	arp->spa =	w->ip;
	bzero(arp->tha, ETH_ADDR_LEN);
	arp->tpa =	dip;

#ifndef NDEBUG
	char spa[IP_ADDR_STRLEN];
	char tpa[IP_ADDR_STRLEN];
	log("ARP request who has %s tell %s",
	    ip_ntoa(arp->tpa, tpa, sizeof tpa),
	    ip_ntoa(arp->spa, spa, sizeof spa));
#endif

	// send the Ethernet packet
	eth_tx(w, v, sizeof(struct eth_hdr) + sizeof(struct arp_hdr));

	// we would need to kick tx ring with NETMAP_NO_TX_POLL set
	// if (ioctl(w->fd, NIOCTXSYNC, 0) == -1)
	// 	die("cannot kick tx ring");

	// make iov available again
	SLIST_INSERT_HEAD(&w->iov, v, next);
}


// This modifies the ARP query in the current receive buffer into an ARP reply
// and sends it out.
static void arp_is_at(struct warpcore * w, char * const buf)
{
	struct arp_hdr * const arp =
		(struct arp_hdr * const)(buf + sizeof(struct eth_hdr));
#ifndef NDEBUG
	char sha[ETH_ADDR_STRLEN];
	char spa[IP_ADDR_STRLEN];
	log("ARP reply %s is at %s",
	    ip_ntoa(arp->spa, spa, sizeof spa),
	    ether_ntoa_r((const struct ether_addr *)arp->sha, sha));
#endif

	// modify ARP header
	arp->op = htons(ARP_OP_REPLY);
	memcpy(arp->tha, arp->sha, sizeof arp->tha);
	arp->tpa = arp->spa;
	memcpy(arp->sha, w->mac, sizeof arp->sha);
	arp->spa = w->ip;

	// send the Ethernet packet
	eth_tx_rx_cur(w, buf, sizeof(struct arp_hdr));
}


// Receive an ARP packet, and react
void arp_rx(struct warpcore * w, char * const buf)
{
#ifndef NDEBUG
	char spa[IP_ADDR_STRLEN];
	char tpa[IP_ADDR_STRLEN];
	char sha[ETH_ADDR_STRLEN];
#endif
	const struct arp_hdr * const arp =
		(const struct arp_hdr * const)(buf + sizeof(struct eth_hdr));

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
		log("ARP request who has %s tell %s",
		    ip_ntoa(arp->tpa, tpa, sizeof tpa),
		    ip_ntoa(arp->spa, spa, sizeof spa));
		if (arp->tpa == w->ip)
			arp_is_at(w, buf);
		else
			log("ignoring ARP request not asking for us");
		break;

	case ARP_OP_REPLY:
		log("ARP reply %s is at %s",
		    ip_ntoa(arp->spa, spa, sizeof spa),
		    ether_ntoa_r((const struct ether_addr *)arp->sha, sha));

		// check if any socket has an IP address matching this ARP
		// reply, and if so, set its destination MAC
		struct w_sock *s;
		SLIST_FOREACH(s, &w->sock, next)
			if (s->dip == arp->spa) {
				log("found socket waiting for ARP");
				memcpy(s->dmac, arp->sha, ETH_ADDR_LEN);
			}
		break;

	default:
		die("unhandled ARP operation %d", op);
		break;
	}
}
