#include <arpa/inet.h>

#include "warpcore.h"
#include "ip.h"
#include "icmp.h"
#include "eth.h"


char * ip_ntoa_r(uint32_t ip, char *buf, size_t size) {
	const uint32_t i = ntohl(ip);
	snprintf(buf, size, "%d.%d.%d.%d", (i >> 24) & 0xff, (i >> 16) & 0xff,
		(i >>  8) & 0xff, i & 0xff);
	buf[size -1] = '\0';
	return buf;
}


void ip_tx(const struct warpcore * const w, const char * const buf) {
	struct ip_hdr * const ip =
		(struct ip_hdr * const)(buf + sizeof(struct eth_hdr));

	// make the original IP src address the new dst, and set the src
	ip->dst = ip->src;
	ip->src = w->ip;

	// calculate the IP checksum
	ip->cksum = 0;
	ip->cksum = in_cksum(ip, ntohs(ip->len));

#ifdef D
	char src[IP_ADDR_STRLEN];
	char dst[IP_ADDR_STRLEN];
	D("IP %s -> %s, proto %d, ttl %d, hlen/tot %d/%d",
		ip_ntoa_r(ip->src, src, sizeof src),
		ip_ntoa_r(ip->dst, dst, sizeof dst),
		ip->p, ip->ttl, ip->hl * 4, ntohs(ip->len) - ip->hl * 4);
#endif

	// do Ethernet transmit preparation
	eth_tx(w, buf);
}


void ip_rx(const struct warpcore * const w, const char * const buf) {
	const struct ip_hdr * const ip =
		(const struct ip_hdr * const)(buf + sizeof(struct eth_hdr));

#ifdef D
	char src[IP_ADDR_STRLEN];
	char dst[IP_ADDR_STRLEN];
	D("IP %s -> %s, proto %d, ttl %d, hlen/tot %d/%d",
		ip_ntoa_r(ip->src, src, sizeof src),
		ip_ntoa_r(ip->dst, dst, sizeof dst),
		ip->p, ip->ttl, ip->hl * 4, ntohs(ip->len) - ip->hl * 4);
#endif

	// TODO: make sure the packet is for us (or broadcast)
	// TODO: validate the IP checksum

	switch (ip->p) {
		case IP_P_ICMP:
			icmp_rx(w, buf, sizeof(struct eth_hdr) + ip->hl * 4, ntohs(ip->len) - ip->hl * 4);
			break;
		// case IP_P_TCP:
		// 	break;
		default:
			D("unhandled IP protocol %d", ip->p);
			abort();
	}
}
