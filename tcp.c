#include <arpa/inet.h>
#include <sys/param.h>

#include "warpcore.h"
#include "tcp.h"


// Log a TCP segment
static inline void
tcp_log(const struct tcp_hdr * const tcp, const uint16_t len)
{
	dlog(info, "TCP :%d -> :%d, flags [%s%s%s%s%s%s%s%s], cksum 0x%04x, "
	     "seq %u, ack %u, win %u, urp %u, len %u",
	     ntohs(tcp->sport), ntohs(tcp->dport),
	     tcp->flags & FIN ? "F" : "", tcp->flags & SYN ? "S" : "",
	     tcp->flags & RST ? "R" : "", tcp->flags & PSH ? "P" : "",
	     tcp->flags & ACK ? "A" : "", tcp->flags & URG ? "U" : "",
	     tcp->flags & ECE ? "E" : "", tcp->flags & CWR ? "C" : "",
	     ntohs(tcp->cksum), ntohl(tcp->seq), ntohl(tcp->ack),
	     ntohs(tcp->win), ntohs(tcp->urp), len);
	// TODO: log options
}


// netmap nm_ring_space() is mis-named, does not return ring space
static inline uint32_t
ring_space(const struct netmap_ring * const ring)
{
	int ret = (int)ring->tail - (int)ring->cur;
	if (ret <= 0)
	        ret += ring->num_slots;
	return (uint32_t)ret;
}


// The receive window is limited by the number of free slots in all rx rings
static inline uint32_t
rx_space(struct w_sock * const s)
{
	uint32_t rx_bytes_avail = 0;
	for (uint32_t ri = 0; ri < s->w->nif->ni_rx_rings; ri++) {
		const struct netmap_ring * const r =
			NETMAP_RXRING(s->w->nif, ri);
		rx_bytes_avail += ring_space(r) * (s->w->mtu - s->hdr_len);
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
	memcpy(v->buf, s->hdr, s->hdr_len);

	// set the rx window
	struct tcp_hdr * const tcp = ip_data(v->buf);
	tcp->win = (uint16_t)MIN(UINT16_MAX, rx_space(s));

	return v;
}


static inline void
tcp_tx_do(struct w_sock * const s, struct w_iov * const v)
{
	// compute the checksum
	struct tcp_hdr * const tcp = ip_data(v->buf);
	tcp->cksum = in_pseudo(s->w->ip, s->dip,
	                       htons(sizeof(struct tcp_hdr) + IP_P_TCP));
	tcp->cksum = in_cksum(tcp, sizeof(struct tcp_hdr));

	tcp_log(tcp, sizeof(struct tcp_hdr));

	// send the IP packet
	ip_tx(s->w, v, sizeof(struct tcp_hdr));
	w_kick_tx(s->w);

	// make iov available again
	STAILQ_INSERT_HEAD(&s->w->iov, v, next);
}


static void
tcp_tx_syn(struct w_sock * const s)
{
	// grab a spare buffer
	struct w_iov * const v = tcp_tx_prep(s);
	struct tcp_hdr * const tcp = ip_data(v->buf);

	// set the ISN
	s->cb->snd_una = tcp->seq = (uint32_t)random();
	tcp->flags = SYN;
	tcp_tx_do(s, v);
}


static void
tcp_tx_rts(struct w_sock * const s)
{
	struct w_iov * const v = tcp_tx_prep(s);
	struct tcp_hdr * const tcp = ip_data(v->buf);

	tcp->flags = RST;
	// tcp->ack = s->cb->rx_ack;
	// tcp->seq = ++s->cb->snd_una;
	tcp_tx_do(s, v);
}


void
tcp_rx(struct warpcore * const w, char * const buf)
{
	const struct ip_hdr * const ip = eth_data(buf);
	struct tcp_hdr * const tcp = ip_data(buf);
	const uint16_t len = ip_data_len(ip);

	tcp_log(tcp, len);

	// validate the checksum
	const uint16_t orig = tcp->cksum;
	tcp->cksum = in_pseudo(ip->src, ip->dst, htons(len + ip->p));
	const uint16_t cksum = in_cksum(tcp, len);
	tcp->cksum = orig;
	if (unlikely(orig != cksum)) {
		dlog(warn, "invalid TCP checksum, received 0x%04x != 0x%04x",
		     ntohs(orig), ntohs(cksum));
		return;
	}

	// TODO: handle urgent pointer
	if (unlikely(tcp->urp))
		die("no support for TCP urgent pointer");

	struct w_sock **s = w_get_sock(w, IP_P_TCP, tcp->dport);
	if (unlikely(*s == 0)) {
		// nobody bound to this port locally
		// send an ICMP unreachable
		icmp_tx_unreach(w, ICMP_UNREACH_PORT, buf);
		return;
	}

	// (*s)->cb->rx_win = tcp->win;
	// (*s)->cb->rx_ack = tcp->ack;

	switch ((*s)->cb->state) {
	case SYN_SENT:
		if (tcp->flags & (SYN|ACK))
			(*s)->cb->state = SYN_RECEIVED;
		break;
	default:
		dlog(crit, "unknown transition in TCP state %d", (*s)->cb->state);
		tcp_tx_rts(*s);
	}
}


void
tcp_tx(struct w_sock * const s)
{
	switch (s->cb->state) {
	case CLOSED:
		tcp_tx_syn(s);
		s->cb->state = SYN_SENT;
		break;
	default:
		dlog(crit, "unknown transition in TCP state %d", s->cb->state);
		tcp_tx_rts(s);
	}
}
