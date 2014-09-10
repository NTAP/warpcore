#include <arpa/inet.h>
#include <stdio.h>

#include "warpcore.h"
#include "ip.h"
#include "icmp.h"
#include "udp.h"


// Convert a network byte order IP address into a string.
const char * ip_ntoa(uint32_t ip, char * const buf, const size_t size)
{
	const uint32_t i = ntohl(ip);
	snprintf(buf, size, "%d.%d.%d.%d", (i >> 24) & 0xff, (i >> 16) & 0xff,
	         (i >>  8) & 0xff, i & 0xff);
	buf[size - 1] = '\0';
	return buf;
}


// Convert a string into a network byte order IP address.
uint32_t ip_aton(const char * const ip)
{

	uint32_t i;
	const int r = sscanf(ip, "%hhu.%hhu.%hhu.%hhu", (char *)(&i),
		             (char *)(&i)+1, (char *)(&i)+2, (char *)(&i)+3);
	return r == 4 ? i : 0;
}


// Make an IP reply packet out of the IP packet in the current receive buffer.
// Only used by icmp_tx.
void ip_tx_with_rx_buf(struct warpcore * w, const uint_fast8_t p,
		       char * const buf, const uint_fast16_t len)
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

#ifndef NDEBUG
	char src[IP_ADDR_STRLEN];
	char dst[IP_ADDR_STRLEN];
	log("IP %s -> %s, proto %d, ttl %d, hlen/tot %d/%d",
	    ip_ntoa(ip->src, src, sizeof src),
	    ip_ntoa(ip->dst, dst, sizeof dst),
	    ip->p, ip->ttl, ip->hl * 4, ntohs(ip->len) - ip->hl * 4);
#endif

	// do Ethernet transmit preparation
	eth_tx_rx_cur(w, buf, l);
}


// Fill in the IP header information that isn't set as part of the
// socket packet template, calculate the header checksum, and hand off
// to the Ethernet layer.
void ip_tx(struct warpcore * w, struct w_iov * const v, const uint_fast16_t len)
{
	char * const start = IDX2BUF(w, v->idx);
	struct ip_hdr * const ip =
		(struct ip_hdr * const)(start + sizeof(struct eth_hdr));
 	const uint_fast16_t l = len + 20; // ip->hl * 4

	// fill in remaining header fields
	ip->len = htons(l);
	ip->id = htons(777); // XXX
	ip->cksum = in_cksum(ip, sizeof *ip); // IP checksum is over header only

	log("IP tx buf %d IP len %d", v->idx, l);

	// do Ethernet transmit preparation
	eth_tx(w, v, l);
}


// Receive an IP packet.
void ip_rx(struct warpcore * w, char * const buf)
{
	const struct ip_hdr * const ip =
		(const struct ip_hdr * const)(buf + sizeof(struct eth_hdr));
#ifndef NDEBUG
	char src[IP_ADDR_STRLEN];
	char dst[IP_ADDR_STRLEN];
	log("IP %s -> %s, proto %d, ttl %d, hlen/tot %d/%d",
	    ip_ntoa(ip->src, src, sizeof src),
	    ip_ntoa(ip->dst, dst, sizeof dst),
	    ip->p, ip->ttl, ip->hl * 4, ntohs(ip->len) - ip->hl * 4);
#endif

	// make sure the packet is for us (or broadcast)
	if (ip->dst != w->ip && ip->dst != w->bcast) {
		log("IP packet not destined to us; ignoring");
		return;
	}

	// validate the IP checksum
	if (in_cksum(ip, sizeof *ip) != 0) {
		log("invalid IP checksum, received %x", ip->cksum);
		return;
	}

	// TODO: handle IP options
	if (ip->hl * 4 != 20)
		die("no support for IP options");

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
		log("unhandled IP protocol %d", ip->p);
		// be standards compliant and send an ICMP unreachable
		icmp_tx_unreach(w, ICMP_UNREACH_PROTOCOL, buf, off);
		break;
	}
}
