#include <arpa/inet.h>

#include "warpcore.h"
#include "ip.h"
#include "icmp.h"
#include "eth.h"
#include "debug.h"
#include "udp.h"


const char * ip_ntoa_r(uint32_t ip, char * const buf, const size_t size)
{
	const uint32_t i = ntohl(ip);

	snprintf(buf, size, "%d.%d.%d.%d", (i >> 24) & 0xff, (i >> 16) & 0xff,
	         (i >>  8) & 0xff, i & 0xff);
	buf[size -1] = '\0';
	return buf;
}


void ip_tx(const struct warpcore * const w, const uint_fast8_t p,
           const char * const buf, const uint_fast16_t len)
{
	struct ip_hdr * const ip =
		(struct ip_hdr * const)(buf + sizeof(struct eth_hdr));

	// make the original IP src address the new dst, and set the src
	ip->dst = ip->src;
	ip->src = w->ip;

	// set the IP length
	const uint_fast16_t l = ip->hl * 4 + len;
	ip->len = htons(l);

	// set the IP protocol
	ip->p = p;

	// finally, calculate the IP checksum
	ip->cksum = 0;
	ip->cksum = in_cksum(ip, l);

#ifdef D
	char src[IP_ADDR_STRLEN];
	char dst[IP_ADDR_STRLEN];
	D("IP %s -> %s, proto %d, ttl %d, hlen/tot %d/%d",
	  ip_ntoa_r(ip->src, src, sizeof src),
	  ip_ntoa_r(ip->dst, dst, sizeof dst),
	  ip->p, ip->ttl, ip->hl * 4, ntohs(ip->len) - ip->hl * 4);
#endif

	// do Ethernet transmit preparation
	eth_tx(w, buf, l);
}


void ip_rx(const struct warpcore * const w, char * const buf)
{
	const struct ip_hdr * const ip =
		(struct ip_hdr * const)(buf + sizeof(struct eth_hdr));

#ifdef D
	char src[IP_ADDR_STRLEN];
	char dst[IP_ADDR_STRLEN];
	D("IP %s -> %s, proto %d, ttl %d, hlen/tot %d/%d",
	  ip_ntoa_r(ip->src, src, sizeof src),
	  ip_ntoa_r(ip->dst, dst, sizeof dst),
	  ip->p, ip->ttl, ip->hl * 4, ntohs(ip->len) - ip->hl * 4);
#endif

	// make sure the packet is for us (or broadcast)
	if (ip->dst != w->ip && ip->dst != w->bcast) {
		D("IP packet not destined to us; ignoring");
		return;
	}

	// TODO: validate the IP checksum
	// TODO: handle IP options
	if (ip->hl * 4 != 20) {
		D("no support for IP options");
		abort();
	}

	const uint_fast16_t off = sizeof(struct eth_hdr) + ip->hl * 4;
	const uint_fast16_t len = ntohs(ip->len) - ip->hl * 4;
	switch (ip->p) {
	case IP_P_ICMP:
		icmp_rx(w, buf, off, len);
		break;
	case IP_P_UDP:
		udp_rx(w, buf, off);
		break;
	// case IP_P_TCP:
	// 	break;
	default:
		D("unhandled IP protocol %d", ip->p);
		// hexdump(ip, sizeof *ip);
		icmp_tx_unreach(w, ICMP_UNREACH_PROTOCOL, buf, off);
		break;
	}
}
