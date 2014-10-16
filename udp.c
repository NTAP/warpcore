#include <arpa/inet.h>
#include <string.h>

#include "warpcore.h"
#include "udp.h"
#include "icmp.h"
#include "ip.h"

// Receive a UDP packet.
void
udp_rx(struct warpcore * const w, char * const buf, const uint16_t off,
	    const uint32_t src)
{
	const struct udp_hdr * const udp =
		(const struct udp_hdr * const)(buf + off);
	const uint16_t len = ntohs(udp->len);

	dlog(info, "UDP :%d -> :%d, len %ld", ntohs(udp->sport),
	     ntohs(udp->dport), len - sizeof(struct udp_hdr));

	struct w_sock **s = w_get_sock(w, IP_P_UDP, udp->dport);
	if (unlikely(*s == 0)) {
		// nobody bound to this port locally
		// send an ICMP unreachable
		icmp_tx_unreach(w, ICMP_UNREACH_PORT, buf, off);
		return;
	}

	// grab an unused iov for the data in this packet
	struct w_iov * const i = STAILQ_FIRST(&w->iov);
	if (unlikely(i == 0))
		die("out of spare bufs");
	struct netmap_ring * const rxr =
		NETMAP_RXRING(w->nif, w->cur_rxr);
	struct netmap_slot * const rxs = &rxr->slot[rxr->cur];
	STAILQ_REMOVE_HEAD(&w->iov, next);

	dlog(debug, "swapping rx ring %d slot %d (buf %d) and spare buf %d",
	     w->cur_rxr, rxr->cur, rxs->buf_idx, i->idx);

	// remember index of this buffer
	const uint32_t tmp_idx = i->idx;

	// move the received data into the iov
	i->buf = buf + off + sizeof(struct udp_hdr);
	i->len = len - sizeof(struct udp_hdr);
	i->idx = rxs->buf_idx;

	// tag the iov with the sender's information
	i->src = src;
	i->sport = udp->sport;

	// copy over the rx timestamp
	memcpy(&i->ts, &rxr->ts, sizeof(struct timeval));

	// append the iov to the socket
	STAILQ_INSERT_TAIL(&(*s)->iv, i, next);

	// put the original buffer of the iov into the receive ring
	rxs->buf_idx = tmp_idx;
	rxs->flags = NS_BUF_CHANGED;
}



// Put the socket template header in front of the data in the iov and send.
bool
udp_tx(const struct w_sock * const s, struct w_iov * const v)
{
	// copy template header into buffer and fill in remaining fields
	char *start = IDX2BUF(s->w, v->idx);
	memcpy(start, s->hdr, s->hdr_len);

	struct udp_hdr * const udp =
		(struct udp_hdr * const)(v->buf - sizeof(struct udp_hdr));
	const uint16_t l = v->len + sizeof(struct udp_hdr);

	dlog(info, "UDP :%d -> :%d, len %d",
	     ntohs(udp->sport), ntohs(udp->dport), v->len);

	udp->len = htons(l);

	// TODO: need to muck up a pseudo header to calculate checksum
	// udp->cksum = in_cksum(udp, l);

	// do IP transmit preparation
	return ip_tx(s->w, v, l);
}

