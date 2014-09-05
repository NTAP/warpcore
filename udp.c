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
	struct w_socket **s = w_find_socket(w, IP_P_UDP, dport);

	D("UDP :%d -> :%d, len %d", sport, dport, len);

	if (*s == 0) {
		// nobody bound to this port locally
		icmp_tx_unreach(w, ICMP_UNREACH_PORT, buf, off);
	} else {
		D("this is for us!");
		// allocate a new iovec for the data in this packet
		struct w_iov *i;
		if ((i = malloc(sizeof *i)) == 0) {
			D("cannot allocate w_iov");
			abort();
		}
		i->buf = buf + off;
		i->len = len;

		// add the iovec to the socket
		STAILQ_INSERT_TAIL(&(*s)->iv, i, vecs);
		struct w_iov *v;
		int n = 0;
		STAILQ_FOREACH(v, &(*s)->iv, vecs) {
			D("w_iov %d buf %p len %d", n++, v->buf, v->len);
		}

		// TODO: take the netmap slot our of the ring until after the
		// read has happened
	}
}
