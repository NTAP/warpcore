#include <arpa/inet.h>
#include <string.h>

#include "warpcore.h"
#include "udp.h"
#include "icmp.h"
#include "ip.h"


// Put the socket template header in front of the data in the iov and send.
void udp_tx(struct w_sock *s, struct w_iov * const v)
{
	// copy template header into buffer and fill in remaining fields
	char *start = IDX2BUF(s->w, v->idx);
	memcpy(start, s->hdr, s->hdr_len);

	struct udp_hdr * const udp =
		(struct udp_hdr * const)(v->buf - sizeof(struct udp_hdr));
 	const uint_fast16_t l = v->len + sizeof(struct udp_hdr);

	log("UDP :%d -> :%d, len %d", ntohs(udp->sport), ntohs(udp->dport), v->len);

	udp->len = htons(l);
	// udp->cksum = in_cksum(udp, l); // XXX need to muck up a pseudo header

	// do IP transmit preparation
	ip_tx(s->w, v, l);
}


// Receive a UDP packet.
void udp_rx(struct warpcore * w, char * const buf, const uint_fast16_t off)
{
	const struct udp_hdr * const udp =
		(const struct udp_hdr * const)(buf + off);
	const uint_fast16_t dport = ntohs(udp->dport);
	const uint_fast16_t len =   ntohs(udp->len);
	struct w_sock **s = w_get_sock(w, IP_P_UDP, dport);
#ifndef NDEBUG
	const uint_fast16_t sport = ntohs(udp->sport);
	log("UDP :%d -> :%d, len %d", sport, dport, len);
#endif

	if (*s == 0) {
		// nobody bound to this port locally
		// send an ICMP unreachable
		icmp_tx_unreach(w, ICMP_UNREACH_PORT, buf, off);
	} else {
		// grab an unused iov for the data in this packet
		struct w_iov * const i = SLIST_FIRST(&w->iov);
		struct netmap_ring * const rxr = NETMAP_RXRING(w->nif, 0);
		struct netmap_slot * const rxs = &rxr->slot[rxr->cur];
		SLIST_REMOVE_HEAD(&w->iov, next);

		log("swapping rx slot %d (buf_idx %d) and spare buffer idx %d",
		  rxr->cur, rxs->buf_idx, i->idx);

		// remember index of this buffer
		const uint_fast32_t tmp_idx = i->idx;

		// move the received data into the iov
		i->buf = buf + off;
		i->len = len - sizeof *udp;
		i->idx = rxs->buf_idx;

		// add the iov to the socket
		// TODO: XXX this needs to insert at the tail!
		SLIST_INSERT_HEAD(&(*s)->iv, i, next);

		// use the original buffer in the iov for the receive ring
		rxs->buf_idx = tmp_idx;
		rxs->flags = NS_BUF_CHANGED;
	}
}
