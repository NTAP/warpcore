#include <arpa/inet.h>
#include <sys/param.h>

#include "warpcore.h"
#include "tcp.h"

static const char * const tcp_state_name[] = {
	"CLOSED", "LISTEN", "SYN_SENT", "SYN_RECEIVED", "ESTABLISHED",
	"CLOSE_WAIT", "FIN_WAIT_1", "CLOSING", "LAST_ACK", "FIN_WAIT_2",
	"TIME_WAIT"
};


// Calculate the length of the TCP payload in a segment. Needs the
// buffer passed in, since we need the IP length field information for this.
// Also account for SYN and FIN in the sequence number space.
static inline uint16_t
tcp_seg_len(const char * const buf)
{
	const uint16_t ip_len = ip_data_len((struct ip_hdr *)eth_data(buf));
	const struct tcp_hdr * const tcp = (struct tcp_hdr *)ip_data(buf);
	const uint16_t tcp_hdr_len = tcp_off(tcp);

	return ip_len - tcp_hdr_len + (tcp->flags & SYN ? 1 : 0) +
		(tcp->flags & FIN ? 1: 0);
}


// Parse options in a TCP segment and update the control block
static inline void
tcp_parse_options(const struct tcp_hdr * const seg, struct tcp_cb * const cb)
{
	const uint8_t *n = (uint8_t *)(seg) + sizeof(struct tcp_hdr);
	do {
		const uint8_t kind = *n;
		const uint8_t len = *(n+1); // XXX unsure if this is always safe

		// see http://www.iana.org/assignments/tcp-parameters/
		// tcp-parameters.xhtml#tcp-parameters-1
		switch (kind) {
		case 0:	// End of Option List
			warn(debug, "eol");
			goto done;
		case 1:	// No-Operation
			warn(debug, "noop");
			n++;
			break;
		case 2:	// Maximum Segment Size
			cb->mss = ntohs((uint16_t)*(n+2));
			warn(debug, "mss %u", cb->mss);
			n += len;
			break;
		case 3:	// Window Scale
			cb->shift_cnt = *(n+2);
			warn(debug, "shift_cnt %u", cb->shift_cnt);
			n += len;
			break;
		case 4:	// SACK Permitted
			cb->sack = true;
			warn(debug, "sack true");
			n += len;
			break;
		case 8:	// Timestamps
			cb->ts_val = ntohl((uint32_t)*n);
			cb->ts_val = ntohl((uint32_t)*(n+4));
			warn(debug, "ts_val %u, ts_ecr %u",
			     cb->ts_val, cb->ts_ecr);
			n += len;
			break;
		default:
			warn(warn, "unknown option %u, data %u, len %u",
			     kind, len-2, len);
			n += len;
			break;
		}
	} while (n < (uint8_t *)(seg) + tcp_off(seg));
done:
	if (n < (uint8_t *)(seg) + tcp_off(seg)) {
		warn(warn, "%ld bytes of padding after options",
		     (uint8_t *)(seg) + tcp_off(seg) - n);
	}
}


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
	     ntohs(seg->wnd), len);
}


// Send an RST for the current received packet.
static inline void
tcp_tx_rst(struct warpcore * w, char * const buf)
{
	struct tcp_hdr * const seg = ip_data(buf);

	if (seg->flags & ACK) {
		seg->seq = seg->ack;
		seg->flags = RST;
	} else {
		seg->ack = htonl(ntohl(seg->seq) + tcp_seg_len(buf));
		seg->seq = 0;
		seg->flags = RST|ACK;
	}
	seg->offx = (sizeof(struct tcp_hdr) / sizeof(int32_t)) << 4;
	const uint16_t tmp = seg->sport;
	seg->sport = seg->dport;
	seg->dport = tmp;
	seg->wnd = 0;

	// compute the checksum
	const struct ip_hdr * const ip = eth_data(buf);
	const uint16_t len = tcp_off(seg);
	seg->cksum = in_pseudo(ip->src, ip->dst, htons(len + IP_P_TCP));
	seg->cksum = in_cksum(seg, len);

	tcp_log(seg, 20);

	// do IP transmit preparation
	ip_tx_with_rx_buf(w, IP_P_TCP, buf, sizeof(struct tcp_hdr));
	w_kick_tx(w);
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
	const uint16_t len = v->len + tcp_off(seg);

	// set the rx window
	seg->wnd = htons((uint16_t)MIN(UINT16_MAX, rx_space(s)));

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
	if (unlikely(seg->up))
		die("no support for TCP urgent pointer");

	struct w_sock **s = w_get_sock(w, IP_P_TCP, seg->dport);
	if (unlikely(*s == 0)) {
		// nobody bound to this port locally
		// send an ICMP unreachable
		icmp_tx_unreach(w, ICMP_UNREACH_PORT, buf);
		return;
	}
	struct tcp_cb * const cb = (*s)->cb;
	tcp_parse_options(seg, cb);

	warn(notice, "%s", tcp_state_name[cb->state]);

	switch (cb->state) {
	case CLOSED:
		if (!(seg->flags & RST))
			tcp_tx_rst(w, buf);
		return;

	case SYN_SENT:
		if (!(seg->flags & (SYN|ACK)))
			return;
		cb->rcv_nxt = ntohl(seg->seq) + 1;
		cb->snd_una++;
		cb->state = ESTABLISHED;
		tcp_tx(*s);
		return;

	case ESTABLISHED:
		if (seg->flags & ACK)
			cb->snd_una = ntohl(seg->ack);

		const uint16_t data_len = len - tcp_off(seg);
		if (data_len == 0)
			return;

		cb->rcv_nxt += data_len;

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
		i->buf = (char *)seg + tcp_off(seg);
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

	case LISTEN:
		if (!(seg->flags & SYN))
			return;
		cb->iss = (uint32_t)random();

		tcp_tx(*s);
		cb->state = SYN_RECEIVED;
		return;
	}

	die("unknown transition in %s", tcp_state_name[cb->state]);
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
			seg->ack = htonl(s->cb->rcv_nxt);

			tcp_tx_do(s, v);
			STAILQ_INSERT_HEAD(&s->w->iov, v, next);
		}
		w_kick_tx(s->w);
		return;
	}

	die("unknown transition in %s", tcp_state_name[s->cb->state]);
}
