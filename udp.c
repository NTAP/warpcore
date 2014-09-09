#include <arpa/inet.h>

#include "warpcore.h"
#include "udp.h"
#include "icmp.h"
#include "ip.h"


void udp_tx(struct w_socket *s,
            char * const buf, const uint_fast16_t len)
{
	struct udp_hdr * const udp =
		(struct udp_hdr * const)(buf - sizeof(struct udp_hdr));
	udp->sport = htons(s->sport);
	udp->dport = htons(s->dport);
	udp->len =   htons(len + sizeof(struct udp_hdr));
	udp->cksum = 0;
	udp->cksum = in_cksum(udp, len + sizeof(struct udp_hdr));
	// TODO: no transmit happening yet
}


void udp_rx(struct warpcore * w,
            char * const buf, const uint_fast16_t off)
{
	const struct udp_hdr * const udp = (struct udp_hdr * const)(buf + off);
	const uint_fast16_t dport = ntohs(udp->dport);
	const uint_fast16_t len =   ntohs(udp->len);
	struct w_socket **s = w_get_socket(w, IP_P_UDP, dport);
#ifndef NDEBUG
	const uint_fast16_t sport = ntohs(udp->sport);
	log("UDP :%d -> :%d, len %d", sport, dport, len);
#endif

	if (*s == 0) {
		// nobody bound to this port locally
		icmp_tx_unreach(w, ICMP_UNREACH_PORT, buf, off);
	} else {
		// grab an unused iov for the data in this packet
		struct w_iov *i = SLIST_FIRST(&w->iov);
		struct netmap_ring *rxr = NETMAP_RXRING(w->nif, 0);
		struct netmap_slot *rxs = &rxr->slot[rxr->cur];
		SLIST_REMOVE_HEAD(&w->iov, vecs);

		log("swapping rx slot %d (buf_idx %d) and spare buffer idx %d",
		  rxr->cur, rxs->buf_idx, i->idx);

		// remember index of this buffer
		const uint_fast32_t tmp_idx = i->idx;

		// move the received data into the iov
		i->buf = buf + off;
		i->len = len;
		i->idx = rxs->buf_idx;

		// add the iov to the socket
		// TODO: XXX this needs to insert at the tail!
		SLIST_INSERT_HEAD(&(*s)->iv, i, vecs);


		// use the original buffer in the iov for the receive ring
		rxs->buf_idx = tmp_idx;
		rxs->flags = NS_BUF_CHANGED;
	}
}
