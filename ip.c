#include "debug.h"
#include "ip.h"
#include "icmp.h"
#include "eth.h"


void ip_tx(const struct nm_desc * const nm, const char * const buf) {
	struct ip_hdr * const ip =
		(struct ip_hdr * const)(buf + sizeof(struct eth_hdr));

	// swap the src and dst IP addresses
	struct in_addr tmp = ip->src;
	ip->src = ip->dst;
	ip->dst = tmp;

	// calculate the IP checksum
	ip->cksum = 0;
	ip->cksum = in_cksum(ip, ntohs(ip->len));

#ifdef D
	char src[256];
	char dst[256];
	D("IP %s -> %s, proto %d, ttl %d, hlen/tot %d/%d",
		inet_ntoa_r(ip->src, src, sizeof src),
		inet_ntoa_r(ip->dst, dst, sizeof dst),
		ip->p, ip->ttl, ip->hl * 4, ntohs(ip->len) - ip->hl * 4);
#endif

	// do Ethernet transmit preparation
	eth_tx(nm, buf);
}


void ip_rx(const struct nm_desc * const nm, const char * const buf) {
	const struct ip_hdr * const ip =
		(const struct ip_hdr * const)(buf + sizeof(struct eth_hdr));

#ifdef D
	char src[256];
	char dst[256];
	D("IP %s -> %s, proto %d, ttl %d, hlen/tot %d/%d",
		inet_ntoa_r(ip->src, src, sizeof src),
		inet_ntoa_r(ip->dst, dst, sizeof dst),
		ip->p, ip->ttl, ip->hl * 4, ntohs(ip->len) - ip->hl * 4);
#endif

	// TODO: make sure the packet is for us (or broadcast)
	// TODO: validate the IP checksum

	switch (ip->p) {
		case IP_P_ICMP:
			icmp_rx(nm, buf, sizeof(struct eth_hdr) + ip->hl * 4, ntohs(ip->len) - ip->hl * 4);
			break;
		// case IP_P_TCP:
		// 	break;
		default:
			D("unhandled IP protocol %d", ip->p);
			abort();
	}
}
