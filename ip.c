#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>

#include "warpcore.h"
#include "ip.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"


static inline void
ip_log(const struct ip_hdr * const ip)
{
#ifndef NDEBUG
	char src[IP_ADDR_STRLEN];
	char dst[IP_ADDR_STRLEN];
#endif
	dlog(notice, "IP: %s -> %s, dscp %d, ecn %d, ttl %d, id %d, "
	     "flags [%s%s], proto %d, hlen/tot %d/%d",
	     ip_ntoa(ip->src, src, sizeof src),
	     ip_ntoa(ip->dst, dst, sizeof dst),
	     ntohs(ip->dscp), ip->ecn, ip->ttl, ntohs(ip->id),
	     ntohs(ip->off) & IP_MF ? "MF" : "",
	     ntohs(ip->off) & IP_DF ? "DF" : "",
	     ip->p, ip->hl * 4, ntohs(ip->len));
}


// Convert a network byte order IP address into a string.
const char *
ip_ntoa(uint32_t ip, char * const buf, const size_t size)
{
	const uint32_t i = ntohl(ip);
	snprintf(buf, size, "%d.%d.%d.%d", (i >> 24) & 0xff, (i >> 16) & 0xff,
		 (i >>  8) & 0xff, i & 0xff);
	buf[size - 1] = '\0';
	return buf;
}


// Convert a string into a network byte order IP address.
uint32_t
ip_aton(const char * const ip)
{

	uint32_t i;
	const int r = sscanf(ip, "%hhu.%hhu.%hhu.%hhu", (char *)(&i),
			     (char *)(&i)+1, (char *)(&i)+2, (char *)(&i)+3);
	return r == 4 ? i : 0;
}


// Make an IP reply packet out of the IP packet in the current receive buffer.
// Only used by icmp_tx.
void
ip_tx_with_rx_buf(struct warpcore * w, const uint8_t p,
		  char * const buf, const uint16_t len)
{
	struct ip_hdr * const ip = ip_hdr_offset(buf);

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

	ip_log(ip);

	// do Ethernet transmit preparation
	eth_tx_rx_cur(w, buf, l);
}

// Receive an IP packet.
void
ip_rx(struct warpcore * const w, char * const buf)
{
	const struct ip_hdr * const ip = ip_hdr_offset(buf);

	ip_log(ip);

	// make sure the packet is for us (or broadcast)
	if (unlikely(ip->dst != w->ip && ip->dst != w->bcast)) {
#ifndef NDEBUG
		char src[IP_ADDR_STRLEN];
		char dst[IP_ADDR_STRLEN];
#endif
		dlog(warn, "IP packet from %s to %s (not us); ignoring",
		     ip_ntoa(ip->src, src, sizeof src),
		     ip_ntoa(ip->dst, dst, sizeof dst));
		return;
	}

	// validate the IP checksum
	if (unlikely(in_cksum(ip, sizeof(struct ip_hdr)) != 0)) {
		dlog(warn, "invalid IP checksum, received 0x%04x",
		     ntohs(ip->cksum));
		return;
	}

	// TODO: handle IP options
	if (unlikely(ip->hl * 4 != 20))
		die("no support for IP options");

	// TODO: handle IP fragments
	if (unlikely(ntohs(ip->off) & IP_OFFMASK))
		die("no support for IP options");

	const uint16_t off = sizeof(struct eth_hdr) + ip->hl * 4;
	const uint16_t len = ntohs(ip->len) - ip->hl * 4;

	if (likely(ip->p == IP_P_UDP))
		udp_rx(w, buf, off, ip->src);
	else if (ip->p == IP_P_TCP)
		tcp_rx(w, buf, off, len, ip->src);
	else if (ip->p == IP_P_ICMP)
		icmp_rx(w, buf, off, len);
	else {
		dlog(warn, "unhandled IP protocol %d", ip->p);
		// be standards compliant and send an ICMP unreachable
		icmp_tx_unreach(w, ICMP_UNREACH_PROTOCOL, buf, off);
	}
}



// Fill in the IP header information that isn't set as part of the
// socket packet template, calculate the header checksum, and hand off
// to the Ethernet layer.
bool
ip_tx(struct warpcore * w, struct w_iov * const v, const uint16_t len)
{
	struct ip_hdr * const ip = ip_hdr_offset(IDX2BUF(w, v->idx));
	const uint16_t l = len + 20; // ip->hl * 4

	// fill in remaining header fields
	ip->len = htons(l);
	ip->id = (uint16_t)random(); // no need to do htons() for random value
	ip->cksum = in_cksum(ip, sizeof *ip); // IP checksum is over header only

	ip_log(ip);

	// do Ethernet transmit preparation
	return eth_tx(w, v, l);
}


