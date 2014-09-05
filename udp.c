#include <arpa/inet.h>

#include "udp.h"
#include "debug.h"
#include "icmp.h"
#include "ip.h"
#include "warpcore.h"


void udp_rx(struct warpcore * w,
            char * const buf, const uint_fast16_t off)
{
	const struct udp_hdr * const udp = (struct udp_hdr * const)(buf + off);
	const uint_fast16_t sport = ntohs(udp->sport);
	const uint_fast16_t dport = ntohs(udp->dport);
	const uint_fast16_t len = ntohs(udp->len);
	struct w_socket **s = w_get_socket(w, IP_P_UDP, dport);

	D("UDP :%d -> :%d, len %d", sport, dport, len);

	if (*s == 0) {
		// nobody bound to this port locally
		icmp_tx_unreach(w, ICMP_UNREACH_PORT, buf, off);
	} else {
		// allocate a new iov for the data in this packet
		struct w_iov *i;
		if ((i = malloc(sizeof *i)) == 0) {
			D("cannot allocate w_iov");
			abort();
		}
		i->buf = buf + off;
		i->len = len;
		struct netmap_ring *rxr = NETMAP_RXRING(w->nif, 0);
		struct netmap_slot *rxs = &rxr->slot[rxr->cur];
		i->idx = rxs->buf_idx;

		// add the iov to the socket
		STAILQ_INSERT_TAIL(&(*s)->iv, i, vecs);

		// struct w_iov *v;
		// int n = 0;
		// STAILQ_FOREACH(v, &(*s)->iv, vecs) {
		// 	D("w_iov %d buf %p len %d", n++, v->buf, v->len);
		// }

		// grab a spare buffer
		struct w_buf *b = STAILQ_FIRST(&w->buf);
		if (b == 0) {
			D("out of spare bufs");
			abort();
		}
		STAILQ_REMOVE_HEAD(&w->buf, bufs);

		D("swapping rx slot %d (buf_idx %d) and spare buffer idx %d",
		  rxr->cur, rxs->buf_idx, b->idx);

		i->idx = rxs->buf_idx;
		rxs->buf_idx = b->idx;
		rxs->flags = NS_BUF_CHANGED;
		free(b);
	}
}
