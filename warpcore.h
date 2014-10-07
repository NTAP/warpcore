#ifndef _warpcore_h_
#define _warpcore_h_

#include <stdbool.h>
#include <sys/queue.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <net/netmap_user.h>

#ifdef __linux__
#include <netinet/ether.h>
#include <netpacket/packet.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#else
#include <sys/types.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#endif

#include "debug.h"
#include "eth.h"
#include "arp.h"
#include "ip.h"
#include "udp.h"
#include "icmp.h"

// according to Luigi, any ring can be passed to NETMAP_BUF
#define IDX2BUF(w, i)	NETMAP_BUF(NETMAP_TXRING(w->nif, 0), i)


struct w_iov {
	char *			buf;	// start of user data (inside buffer)
	SLIST_ENTRY(w_iov) 	next;	// next iov
	uint32_t		idx;	// index of netmap buffer
	uint16_t		len;	// length of user data (inside buffer)
	uint16_t		port;	// sender port (only valid on rx)
	uint32_t		ip;	// sender IP address (only valid on rx)
	struct timeval		ts;
} __attribute__((__aligned__(4)));


struct w_sock {
	struct warpcore *	w;			// warpcore instance
	SLIST_HEAD(ivh, w_iov)	iv;			// iov for read data
	SLIST_HEAD(ovh, w_iov)	ov;			// iov for data to write
	char *			hdr;			// header template
	uint16_t		hdr_len;		// length of template
	uint8_t 		dmac[ETH_ADDR_LEN];	// dst Eth address
	uint32_t		dip;			// dst IP address
	uint16_t		sport;			// src port
	uint16_t		dport;			// dst port
	SLIST_ENTRY(w_sock) 	next;			// next socket
	uint8_t			p;			// protocol
} __attribute__((__aligned__(4)));


struct warpcore {
	struct netmap_if *	nif;			// netmap interface
	void *			mem;			// netmap memory
	struct w_sock **	udp;			// UDP "sockets"
	struct w_sock **	tcp;			// TCP "sockets"
	uint32_t		cur_txr;		// our current tx ring
	uint32_t		cur_rxr;		// our current rx ring
	SLIST_HEAD(iovh, w_iov)	iov;			// our available bufs
	uint32_t		ip;			// our IP address
	uint32_t		bcast;			// our broadcast address
	uint8_t 		mac[ETH_ADDR_LEN];	// our Ethernet address

	// mtu could be pushed into the second cacheline
	uint16_t		mtu;			// our MTU

        // --- cacheline 1 boundary (64 bytes) ---
	uint32_t		mbps;			// our link speed
	SLIST_HEAD(sh, w_sock)	sock;			// our open sockets
	uint32_t		mask;			// our IP netmask
	int			fd;			// netmap descriptor
	struct nmreq		req;			// netmap request
} __attribute__((__aligned__(4)));


// see warpcore.c for documentation of functions
extern struct warpcore * w_init(const char * const ifname);

extern void w_init_common(void);

extern void w_cleanup(struct warpcore * const w);

extern struct w_sock * w_bind(struct warpcore * const w, const uint8_t p,
                              const uint16_t port);

extern void w_connect(struct w_sock * const s, const uint32_t ip,
                      const uint16_t port);

extern void w_close(struct w_sock * const s);


// Allocates an iov of a given size for tx preparation.
static inline __attribute__((always_inline)) struct w_iov *
w_tx_alloc(struct w_sock * const s, const uint32_t len)
{
	if (unlikely(!SLIST_EMPTY(&s->ov))) {
		log(1, "output iov already allocated");
		return 0;
	}

	// determine space needed for header
	uint16_t hdr_len = sizeof(struct eth_hdr) + sizeof(struct ip_hdr);
	if (likely(s->p == IP_P_UDP))
		hdr_len += sizeof(struct udp_hdr);
	else {
		die("unhandled IP proto %d", s->p);
		return 0;
	}

	// add enough buffers to the iov so it is > len
	SLIST_INIT(&s->ov);
	struct w_iov *ov_tail = 0;
	struct w_iov *v = 0;
	int32_t l = (int32_t)len;
	uint32_t n = 0;
	while (l > 0) {
		// grab a spare buffer
		v = SLIST_FIRST(&s->w->iov);
		if (unlikely(v == 0))
			die("out of spare bufs after grabbing %d", n);
		SLIST_REMOVE_HEAD(&s->w->iov, next);
		log(5, "grabbing spare buf %d for user tx", v->idx);
		v->buf = IDX2BUF(s->w, v->idx) + hdr_len;
		v->len = s->w->mtu - hdr_len;
		l -= v->len;
		n++;

		// add the iov to the tail of the socket
		// using a STAILQ would be simpler, but slower
		if(SLIST_EMPTY(&s->ov))
			SLIST_INSERT_HEAD(&s->ov, v, next);
		else
			SLIST_INSERT_AFTER(ov_tail, v, next);
		ov_tail = v;
	}
	// adjust length of last iov so chain is the exact length requested
	v->len += l; // l is negative

	log(3, "allocating iov (len %d in %d bufs) for user tx", len, n);

	return SLIST_FIRST(&s->ov);
}


// Wait until netmap is ready to send or receive more data. Parameters
// "event" and "timeout" identical to poll system call.
// Returns false if an interrupt occurs during the poll, which usually means
// someone hit Ctrl-C.
// (TODO: This interrupt handling needs some rethinking.)
static inline __attribute__((always_inline)) bool
w_poll(const struct warpcore * const w, const short ev, const int to)
{
	struct pollfd fds = { .fd = w->fd, .events = ev };
	const int n = poll(&fds, 1, to);

	if (unlikely(n == -1)) {
		if (errno == EINTR) {
			log(3, "poll: interrupt");
			return false;
		} else
			die("poll");
	}
	if (unlikely(n == 0)) {
		// log(1, "poll: timeout expired");
		return true;
	}

	// rlog(1, 1, "poll: %d descriptors ready", n);
	return true;
}


// User needs to call this once they are done with touching any received data.
// This makes the iov that holds the received data available to warpcore again.
static inline __attribute__((always_inline)) void
w_rx_done(struct w_sock * const s)
{
	struct w_iov *i = SLIST_FIRST(&s->iv);
	while (i) {
		// move i from the socket to the available iov list
		struct w_iov * const n = SLIST_NEXT(i, next);
		SLIST_REMOVE_HEAD(&s->iv, next);
		SLIST_INSERT_HEAD(&s->w->iov, i, next);
		i = n;
	}
	// TODO: should be a no-op; check
	SLIST_INIT(&s->iv);
}



// Internal warpcore function. Given an IP protocol number and a local port
// number, returns a pointer to the w_sock pointer.
// static inline __attribute__((always_inline)) struct w_sock **
// w_get_sock(const struct warpcore * const w, const uint8_t p,
//            const uint16_t port)
// {
// 	// find the respective "socket"
// 	if (likely(p == IP_P_UDP))
// 		return &w->udp[port];
// 	if (likely(p == IP_P_TCP))
// 		return &w->tcp[port];
// 	die("cannot find socket for IP proto %d", p);
// 	return 0;
// }


#define w_get_sock(w, p, port) ((p) == IP_P_UDP ? \
				&(w)->udp[port] : &(w)->tcp[port])


// Receive a UDP packet.
static inline __attribute__((always_inline)) void
udp_rx(struct warpcore * const w, char * const buf, const uint16_t off,
            const uint32_t ip)
{
	const struct udp_hdr * const udp =
		(const struct udp_hdr * const)(buf + off);
	const uint16_t len = ntohs(udp->len);

	log(5, "UDP :%d -> :%d, len %ld",
	    ntohs(udp->sport), ntohs(udp->dport), len - sizeof(struct udp_hdr));

	struct w_sock **s = w_get_sock(w, IP_P_UDP, udp->dport);
	if (unlikely(*s == 0)) {
		// nobody bound to this port locally
		// send an ICMP unreachable
		icmp_tx_unreach(w, ICMP_UNREACH_PORT, buf, off);
		return;
	}

	// grab an unused iov for the data in this packet
	struct w_iov * const i = SLIST_FIRST(&w->iov);
	if (unlikely(i == 0))
		die("out of spare bufs");
	struct netmap_ring * const rxr =
		NETMAP_RXRING(w->nif, w->cur_rxr);
	struct netmap_slot * const rxs = &rxr->slot[rxr->cur];
	SLIST_REMOVE_HEAD(&w->iov, next);

	log(5, "swapping rx ring %d slot %d (buf %d) and spare buf %d",
	    w->cur_rxr, rxr->cur, rxs->buf_idx, i->idx);

	// remember index of this buffer
	const uint32_t tmp_idx = i->idx;

	// move the received data into the iov
	i->buf = buf + off + sizeof(struct udp_hdr);
	i->len = len - sizeof(struct udp_hdr);
	i->idx = rxs->buf_idx;

	// tag the iov with the sender's information
	i->ip = ip;
	i->port = udp->sport;

	// copy over the rx timestamp
	memcpy(&i->ts, &rxr->ts, sizeof(struct timeval));

	// add the iov to the socket
	// TODO: XXX this needs to insert at the tail!
	SLIST_INSERT_HEAD(&(*s)->iv, i, next);

	// put the original buffer of the iov into the receive ring
	rxs->buf_idx = tmp_idx;
	rxs->flags = NS_BUF_CHANGED;
}



// Receive an IP packet.
static inline __attribute__((always_inline)) void
ip_rx(struct warpcore * const w, char * const buf)
{
	const struct ip_hdr * const ip =
		(const struct ip_hdr * const)(buf + sizeof(struct eth_hdr));
#ifndef NDEBUG
	char dst[IP_ADDR_STRLEN];
	char src[IP_ADDR_STRLEN];
	log(3, "IP %s -> %s, proto %d, ttl %d, hlen/tot %d/%d",
	    ip_ntoa(ip->src, src, sizeof src),
	    ip_ntoa(ip->dst, dst, sizeof dst),
	    ip->p, ip->ttl, ip->hl * 4, ntohs(ip->len));
#endif

	// make sure the packet is for us (or broadcast)
	if (unlikely(ip->dst != w->ip && ip->dst != w->bcast)) {
		log(1, "IP packet from %s to %s (not us); ignoring",
		    ip_ntoa(ip->src, src, sizeof src),
		    ip_ntoa(ip->dst, dst, sizeof dst));
		return;
	}

	// validate the IP checksum
	if (unlikely(in_cksum(ip, sizeof *ip) != 0)) {
		log(1, "invalid IP checksum, received %x", ip->cksum);
		return;
	}

	// TODO: handle IP options
	if (unlikely(ip->hl * 4 != 20))
		die("no support for IP options");

	const uint16_t off = sizeof(struct eth_hdr) + ip->hl * 4;
	const uint16_t len = ntohs(ip->len) - ip->hl * 4;

	if (likely(ip->p == IP_P_UDP))
		udp_rx(w, buf, off, ip->src);
	else if (ip->p == IP_P_ICMP)
		icmp_rx(w, buf, off, len);
	else {
		log(1, "unhandled IP protocol %d", ip->p);
		// be standards compliant and send an ICMP unreachable
		icmp_tx_unreach(w, ICMP_UNREACH_PROTOCOL, buf, off);
	}
}



// Receive an Ethernet packet. This is the lowest level inbound function,
// called from w_poll.
static inline __attribute__((always_inline)) void
eth_rx(struct warpcore * const w, char * const buf)
{
	const struct eth_hdr * const eth = (const struct eth_hdr * const)buf;

#ifndef NDEBUG
	char src[ETH_ADDR_STRLEN];
	char dst[ETH_ADDR_STRLEN];
	log(3, "Eth %s -> %s, type %d",
	    ether_ntoa_r((const struct ether_addr *)eth->src, src),
	    ether_ntoa_r((const struct ether_addr *)eth->dst, dst),
	    ntohs(eth->type));
#endif

	// make sure the packet is for us (or broadcast)
	if (unlikely(memcmp(eth->dst, w->mac, ETH_ADDR_LEN) &&
		     memcmp(eth->dst, ETH_BCAST, ETH_ADDR_LEN))) {
		log(1, "Ethernet packet not destined to us; ignoring");
		return;
	}

	if (likely(eth->type == ETH_TYPE_IP))
		ip_rx(w, buf);
	else if (eth->type == ETH_TYPE_ARP)
		arp_rx(w, buf);
	else
		die("unhandled ethertype %x", eth->type);
}


// Pulls new received data out of the rx ring and places it into socket iovs.
// Returns an iov of any data received.
static inline __attribute__((always_inline)) struct w_iov *
w_rx(struct w_sock * const s)
{
	// loop over all rx rings starting with cur_rxr and wrapping around
	for (uint32_t i = 0; likely(i < s->w->nif->ni_rx_rings); i++) {
		struct netmap_ring * const r =
			NETMAP_RXRING(s->w->nif, s->w->cur_rxr);
		while (!nm_ring_empty(r)) {
			eth_rx(s->w, NETMAP_BUF(r, r->slot[r->cur].buf_idx));
			r->head = r->cur = nm_ring_next(r, r->cur);
		}
		s->w->cur_rxr = (s->w->cur_rxr + 1) % s->w->nif->ni_rx_rings;
	}

	if (s)
		return SLIST_FIRST(&s->iv);
	return 0;
}


// Swap the buffer in the iov into the tx ring, placing an empty one
// into the iov.
static inline __attribute__((always_inline)) bool
eth_tx(struct warpcore *w, struct w_iov * const v, const uint16_t len)
{
	// check if there is space in the current txr
	struct netmap_ring *txr = 0;
	uint32_t i;
	for (i = 0; i < w->nif->ni_tx_rings; i++) {
		txr = NETMAP_TXRING(w->nif, w->cur_txr);
		if (likely(nm_ring_space(txr)))
			// we have space in this ring
			break;
		else
			// current txr is full, try next
			w->cur_txr = (w->cur_txr + 1) % w->nif->ni_tx_rings;
	}

	// return false if all rings are full
	if (unlikely(i == w->nif->ni_tx_rings)) {
		die("all tx rings are full");
		log(3, "all tx rings are full");
		return false;
	}

	struct netmap_slot * const txs = &txr->slot[txr->cur];

	log(5, "placing iov buf %d in tx ring %d slot %d (current buf %d)",
	    v->idx, w->cur_txr, txr->cur, txs->buf_idx);

	// place v in the current tx ring
	const uint32_t tmp_idx = txs->buf_idx;
	txs->buf_idx = v->idx;
	txs->len = len + sizeof(struct eth_hdr);
	txs->flags = NS_BUF_CHANGED;

#ifndef NDEBUG
	const struct eth_hdr * const eth =
		(const struct eth_hdr * const)NETMAP_BUF(txr, txs->buf_idx);
	char src[ETH_ADDR_STRLEN];
	char dst[ETH_ADDR_STRLEN];
	log(3, "Eth %s -> %s, type %d, len %ld",
	    ether_ntoa_r((const struct ether_addr *)eth->src, src),
	    ether_ntoa_r((const struct ether_addr *)eth->dst, dst),
	    ntohs(eth->type), len + sizeof(struct eth_hdr));
#endif

	// place the original tx buffer in v
	v->idx = tmp_idx;

	// advance tx ring
	txr->head = txr->cur = nm_ring_next(txr, txr->cur);

	// caller needs to make iovs available again and optionally kick tx
	return true;
}


// Fill in the IP header information that isn't set as part of the
// socket packet template, calculate the header checksum, and hand off
// to the Ethernet layer.
static inline __attribute__((always_inline)) bool
ip_tx(struct warpcore * w, struct w_iov * const v, const uint16_t len)
{
	char * const start = IDX2BUF(w, v->idx);
	struct ip_hdr * const ip =
		(struct ip_hdr * const)(start + sizeof(struct eth_hdr));
 	const uint16_t l = len + 20; // ip->hl * 4

	// fill in remaining header fields
	ip->len = htons(l);
	ip->id = (uint16_t)random(); // no need to do htons() for random value
	ip->cksum = in_cksum(ip, sizeof *ip); // IP checksum is over header only

#ifndef NDEBUG
	char dst[IP_ADDR_STRLEN];
	char src[IP_ADDR_STRLEN];
	log(3, "IP tx buf %d, %s -> %s, proto %d, ttl %d, hlen/tot %d/%d",
	    v->idx, ip_ntoa(ip->src, src, sizeof src),
	    ip_ntoa(ip->dst, dst, sizeof dst),
	    ip->p, ip->ttl, ip->hl * 4, ntohs(ip->len));
#endif

	// do Ethernet transmit preparation
	return eth_tx(w, v, l);
}


// Put the socket template header in front of the data in the iov and send.
static inline __attribute__((always_inline)) bool
udp_tx(const struct w_sock * const s, struct w_iov * const v)
{
	// copy template header into buffer and fill in remaining fields
	char *start = IDX2BUF(s->w, v->idx);
	memcpy(start, s->hdr, s->hdr_len);

	struct udp_hdr * const udp =
		(struct udp_hdr * const)(v->buf - sizeof(struct udp_hdr));
 	const uint16_t l = v->len + sizeof(struct udp_hdr);

	log(5, "UDP :%d -> :%d, len %d",
	    ntohs(udp->sport), ntohs(udp->dport), v->len);

	udp->len = htons(l);
	// udp->cksum = in_cksum(udp, l); // XXX need to muck up a pseudo header

	// do IP transmit preparation
	return ip_tx(s->w, v, l);
}


// Internal warpcore function. Kick the tx ring.
static inline __attribute__((always_inline)) void
w_kick_tx(const struct warpcore * const w)
{
	if (unlikely(ioctl(w->fd, NIOCTXSYNC, 0) == -1))
		die("cannot kick tx ring");
}


// Internal warpcore function. Kick the rx ring.
static inline __attribute__((always_inline)) void
w_kick_rx(const struct warpcore * const w)
{
	if (unlikely(ioctl(w->fd, NIOCRXSYNC, 0) == -1))
		die("cannot kick rx ring");
}


// Prepends all network headers and places s->ov in the tx ring.
static inline __attribute__((always_inline)) void
w_tx(struct w_sock * const s)
{
	// TODO: handle other protocols

	// packetize bufs and place in tx ring
	uint32_t n = 0, l = 0;
	while (likely(!SLIST_EMPTY(&s->ov))) {
		struct w_iov * const v = SLIST_FIRST(&s->ov);
		if (likely(udp_tx(s, v))) {
			n++;
			l += v->len;
			SLIST_REMOVE_HEAD(&s->ov, next);
			SLIST_INSERT_HEAD(&s->w->iov, v, next);
		} else {
			// no space in ring
			w_kick_tx(s->w);
			log(1, "polling for send space");
			if (w_poll(s->w, POLLOUT, -1) == false)
				// interrupt received during poll
				return;
		}
	}
	log(3, "UDP tx iov (len %d in %d bufs) done", l, n);

	// kick tx ring
	w_kick_tx(s->w);
}

#endif
