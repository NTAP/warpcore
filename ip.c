#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>

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
void ip_tx_with_rx_buf(struct warpcore * w, const uint8_t p,
		       char * const buf, const uint16_t len)
{
	struct ip_hdr * const ip =
		(struct ip_hdr * const)(buf + sizeof(struct eth_hdr));

	// make the original IP src address the new dst, and set the src
	ip->dst = ip->src;
	ip->src = w->ip;

	// set the IP length
	const uint16_t l = ip->hl * 4 + len;
	ip->len = htons(l);

	// set other header fields
	ip->p = p;
	ip->id = (uint16_t)random(); // no need to do htons() for random value

	// TODO: we should zero out any IP options here,
	// since we're reflecing a received packet
	if (ip->hl * 4 > 20)
		die("packet seems to have IP options");

	// finally, calculate the IP checksum
	ip->cksum = 0;
	ip->cksum = in_cksum(ip, l);

#ifndef NDEBUG
	char src[IP_ADDR_STRLEN];
	char dst[IP_ADDR_STRLEN];
	dlog(notice, "IP %s -> %s, proto %d, ttl %d, hlen/tot %d/%d",
	    ip_ntoa(ip->src, src, sizeof src),
	    ip_ntoa(ip->dst, dst, sizeof dst),
	    ip->p, ip->ttl, ip->hl * 4, ntohs(ip->len));
#endif

	// do Ethernet transmit preparation
	eth_tx_rx_cur(w, buf, l);
}

