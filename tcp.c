#include <arpa/inet.h>
#include <sys/param.h>

#include "warpcore.h"
#include "tcp.h"

static const char * const tcp_state_name[] = {
	"CLOSED", "LISTEN", "SYN_SENT", "SYN_RECEIVED", "ESTABLISHED",
	"CLOSE_WAIT", "FIN_WAIT_1", "CLOSING", "LAST_ACK", "FIN_WAIT_2",
	"TIME_WAIT"
};


// Log a TCP segment
static inline void
tcp_log(const struct tcp_hdr * const seg, const uint16_t len)
{
	warn(info, "TCP :%d -> :%d, flags [%s%s%s%s%s%s%s%s], cksum 0x%04x, "
	     "seq %u, ack %u, win %u, len %u",
	     ntohs(seg->sport), ntohs(seg->dport),
	     seg->flags & FIN ? "F" : "", seg->flags & SYN ? "S" : "",
	     seg->flags & RST ? "R" : "", seg->flags & PSH ? "P" : "",
	     seg->flags & ACK ? "A" : "", seg->flags & URG ? "U" : "",
	     seg->flags & ECE ? "E" : "", seg->flags & CWR ? "C" : "",
	     ntohs(seg->cksum), ntohl(seg->seq), ntohl(seg->ack),
	     ntohs(seg->win), len);
	// TODO: log options
}


// The receive window is limited by the number of free slots in all rx rings
static inline uint32_t
rx_space(struct w_sock * const s)
{
	uint32_t rx_bytes_avail = 0;
	for (uint32_t ri = 0; ri < s->w->nif->ni_rx_rings; ri++) {
		struct netmap_ring * const r =
			NETMAP_RXRING(s->w->nif, ri);
		rx_bytes_avail += (r->num_slots - nm_ring_space(r)) *
				  (s->w->mtu - s->hdr_len);
	}
	return rx_bytes_avail;
}


static inline struct w_iov *
tcp_tx_prep(struct w_sock * const s)
{
	// grab a spare buffer
	struct w_iov * const v = STAILQ_FIRST(&s->w->iov);
	if (unlikely(v == 0))
		die("out of spare bufs");
	STAILQ_REMOVE_HEAD(&s->w->iov, next);

	// copy template header into buffer and fill in remaining fields
	v->buf = IDX2BUF(s->w, v->idx);
	v->len = 0;
	memcpy(v->buf, s->hdr, s->hdr_len);

	return v;
}


static inline void
tcp_tx_do(struct w_sock * const s, struct w_iov * const v)
{
	struct tcp_hdr * const seg = ip_data(IDX2BUF(s->w, v->idx));
	const uint16_t len = v->len + seg->off * 4;

	// set the rx window
	seg->win = htons((uint16_t)MIN(UINT16_MAX, rx_space(s)));

	// compute the checksum
	seg->cksum = in_pseudo(s->w->ip, s->dip, htons(len + IP_P_TCP));
	seg->cksum = in_cksum(seg, len);

	tcp_log(seg, sizeof(struct tcp_hdr));

	// send the IP packet
	ip_tx(s->w, v, len);
}


static void
tcp_tx_syn(struct w_sock * const s)
{
	// grab a spare buffer
	struct w_iov * const v = tcp_tx_prep(s);
	struct tcp_hdr * const seg = ip_data(v->buf);

	// set the ISN
	seg->seq = (uint32_t)random();
	s->cb->snd_una = ntohl(seg->seq);
	seg->flags = SYN;

	tcp_tx_do(s, v);
	w_kick_tx(s->w);

	// make iov available again
	STAILQ_INSERT_HEAD(&s->w->iov, v, next);
}


static void
tcp_tx_rts(struct w_sock * const s, const uint32_t ack)
{
	struct w_iov * const v = tcp_tx_prep(s);
	struct tcp_hdr * const seg = ip_data(v->buf);

	seg->flags = RST;
	seg->ack = htonl(ack);
	seg->seq = htonl(++(s->cb->snd_una));

	tcp_tx_do(s, v);
	w_kick_tx(s->w);

	// make iov available again
	STAILQ_INSERT_HEAD(&s->w->iov, v, next);

	s->cb->state = CLOSED;
}


void
tcp_rx(struct warpcore * const w, char * const buf)
{
	const struct ip_hdr * const ip = eth_data(buf);
	struct tcp_hdr * const seg = ip_data(buf);
	const uint16_t len = ip_data_len(ip);

	tcp_log(seg, len);

	// validate the checksum
	const uint16_t orig = seg->cksum;
	seg->cksum = in_pseudo(ip->src, ip->dst, htons(len + ip->p));
	const uint16_t cksum = in_cksum(seg, len);
	seg->cksum = orig;
	if (unlikely(orig != cksum)) {
		warn(warn, "invalid TCP checksum, received 0x%04x != 0x%04x",
		     ntohs(orig), ntohs(cksum));
		return;
	}

	// TODO: handle urgent pointer
	if (unlikely(seg->urp))
		die("no support for TCP urgent pointer");

	struct w_sock **s = w_get_sock(w, IP_P_TCP, seg->dport);
	if (unlikely(*s == 0)) {
		// nobody bound to this port locally
		// send an ICMP unreachable
		icmp_tx_unreach(w, ICMP_UNREACH_PORT, buf);
		return;
	}
	struct tcp_cb * const cb = (*s)->cb;

	warn(notice, "%s", tcp_state_name[cb->state]);

	switch (cb->state) {
	case SYN_SENT:
		if (!(seg->flags & (SYN|ACK)))
			break;
		cb->rcv_next = ntohl(seg->seq) + 1;
		cb->snd_una++;
		cb->state = ESTABLISHED;
		tcp_tx(*s);
		return;

	case ESTABLISHED:
		if (seg->flags & ACK)
			cb->snd_una = ntohl(seg->ack);

		const uint16_t data_len = len - seg->off * 4;
		if (data_len == 0)
			return;

		cb->rcv_next += data_len;

		// XXX TODO: code below doesn't preserve byte stream ordering

		// grab an unused iov for the data in this packet
		struct w_iov * const i = STAILQ_FIRST(&w->iov);
		if (unlikely(i == 0))
			die("out of spare bufs");
		struct netmap_ring * const rxr =
			NETMAP_RXRING(w->nif, w->cur_rxr);
		struct netmap_slot * const rxs = &rxr->slot[rxr->cur];
		STAILQ_REMOVE_HEAD(&w->iov, next);

		warn(debug, "swapping rx ring %d slot %d (buf %d) and spare buf %d",
		     w->cur_rxr, rxr->cur, rxs->buf_idx, i->idx);

		// remember index of this buffer
		const uint32_t tmp_idx = i->idx;

		// move the received data into the iov
		i->buf = (char *)seg + seg->off * 4;
		i->len = data_len;
		i->idx = rxs->buf_idx;

		// copy over the rx timestamp
		memcpy(&i->ts, &rxr->ts, sizeof(struct timeval));

		// append the iov to the socket
		STAILQ_INSERT_TAIL(&(*s)->iv, i, next);

		// put the original buffer of the iov into the receive ring
		rxs->buf_idx = tmp_idx;
		rxs->flags = NS_BUF_CHANGED;

		return;
	}

	die("unknown transition in %s", tcp_state_name[cb->state]);
	tcp_tx_rts(*s, seg->ack);
}


void
tcp_tx(struct w_sock * const s)
{
	warn(notice, "%s", tcp_state_name[s->cb->state]);

	switch (s->cb->state) {
	case CLOSED:
		tcp_tx_syn(s);
		s->cb->state = SYN_SENT;
		return;

	case ESTABLISHED:
		// packetize bufs and place in tx ring
		while (likely(!STAILQ_EMPTY(&s->ov))) {
			struct w_iov * const v = STAILQ_FIRST(&s->ov);
			STAILQ_REMOVE_HEAD(&s->ov, next);

			// copy template header into buffer and fill in remaining fields
			char * const buf = IDX2BUF(s->w, v->idx);
			memcpy(buf, s->hdr, s->hdr_len);

			struct tcp_hdr * const seg = ip_data(buf);
			seg->seq = htonl(s->cb->snd_una);
			s->cb->snd_una += v->len;
			seg->flags |= ACK;
			if (STAILQ_EMPTY(&s->ov))
				seg->flags |= PSH;
			seg->ack = htonl(s->cb->rcv_next);

			tcp_tx_do(s, v);
			STAILQ_INSERT_HEAD(&s->w->iov, v, next);
		}
		w_kick_tx(s->w);
		return;
	}

	die("unknown transition in %s", tcp_state_name[s->cb->state]);
	tcp_tx_rts(s, 0 /*XXX*/);
}
