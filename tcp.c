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
tcp_rcv_wnd(struct w_sock * const s)
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
	seg->wnd = htons((uint16_t)MIN(UINT16_MAX, tcp_rcv_wnd(s)));

	// compute the checksum
	seg->cksum = in_pseudo(s->w->ip, s->dip, htons(len + IP_P_TCP));
	seg->cksum = in_cksum(seg, len);

	tcp_log(seg, sizeof(struct tcp_hdr));

	// send the IP packet
	ip_tx(s->w, v, len);
}


static inline void
tcp_tx_syn(struct w_sock * const s)
{
	// grab a spare buffer
	struct w_iov * const v = tcp_tx_prep(s);
	struct tcp_hdr * const seg = ip_data(v->buf);

	seg->seq = htonl(s->cb->iss);
	seg->ack = htonl(s->cb->rcv_nxt);
	seg->flags = SYN;
	if (s->cb->state == LISTEN)
		seg->flags |= ACK;

	tcp_tx_do(s, v);
	w_kick_tx(s->w);

	// make iov available again
	STAILQ_INSERT_HEAD(&s->w->iov, v, next);
}


static inline void
tcp_tx_ack(struct w_sock * const s)
{
	// grab a spare buffer
	struct w_iov * const v = tcp_tx_prep(s);
	struct tcp_hdr * const seg = ip_data(v->buf);

	// seg->seq = htonl(s->cb->snd_nxt);
	// seg->ack = htonl(s->cb->rcv_nxt);
	seg->flags = ACK;

	tcp_tx_do(s, v);
	w_kick_tx(s->w);

	// make iov available again
	STAILQ_INSERT_HEAD(&s->w->iov, v, next);
}


static inline bool
tcp_ack_is_ok(const struct tcp_cb * const cb, const char * const buf)
{
	const struct tcp_hdr * const seg = ip_data(buf);
	const uint32_t seq = ntohl(seg->seq);
	const uint16_t len = tcp_seg_len(buf);
	const uint32_t rcv_wnd = tcp_rcv_wnd(cb->s);
	warn(debug, "len %d, rcv_wnd %d", len, rcv_wnd);
	if (len == 0) {
		if (rcv_wnd == 0) {
			if (seq == cb->rcv_nxt)
				return true;
		} else {
			if (cb->rcv_nxt <= seq &&
			    seq <= cb->rcv_nxt + rcv_wnd)
				return true;
		}
	} else {
		if (rcv_wnd == 0) {
			return false;
		} else {
			if ((cb->rcv_nxt <= seq &&
			     seq < cb->rcv_nxt + rcv_wnd) ||
			    (cb->rcv_nxt <= seq + len - 1 &&
			     seq + len - 1 < cb->rcv_nxt + rcv_wnd))
				return true;
		}
	}
	return false;
}


#define goto_rst { warn(debug, "goto rst"); goto rst; }
#define goto_drop { warn(debug, "goto drop"); goto drop; }

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

	struct w_sock **ss = w_get_sock(w, IP_P_TCP, seg->dport);
	if (unlikely(*ss == 0)) {
		// nobody bound to this port locally
		// we used to send an ICMP unreachable
		// icmp_tx_unreach(w, ICMP_UNREACH_PORT, buf);
		// but now send an RST
		if (!(seg->flags & RST))
			goto_rst;
		return;
	}
	struct w_sock * const s = *ss;
	struct tcp_cb * const cb = s->cb;

	// if there are TCP options in the segment, parse them
	if (tcp_off(seg) > sizeof(struct tcp_hdr))
		tcp_parse_options(seg, cb);

	warn(notice, "am in state %s", tcp_state_name[cb->state]);

	if (cb->state == CLOSED) {
		// If the state is CLOSED (i.e., TCB does not exist) then:
		// All data in the incoming segment is discarded.  An incoming
		// segment containing a RST is discarded.  An incoming segment
		// not containing a RST causes a RST to be sent in response.
		if (!(seg->flags & RST))
			goto_rst;
		goto_drop;
	}

	if (cb->state == LISTEN) {
		// If the state is LISTEN then:
		// first check for an RST
		// An incoming RST should be ignored.  Return.
		if (seg->flags & RST)
			goto_drop;

		// second check for an ACK
		// Any acknowledgment is bad if it arrives on a connection
		// still in the LISTEN state.  An acceptable reset segment
		// should be formed for any arriving ACK-bearing segment.
		// Return.
		if (seg->flags & ACK)
			goto_rst;

		// third check for a SYN
		if (seg->flags & SYN) {
			// "bind" the socket to the destination
			// explicitly set the destination MAC to save an ARP
			struct eth_hdr * const eth =
				(struct eth_hdr * const)(buf);
			memcpy(s->dmac, eth->src, ETH_ADDR_LEN);
			w_connect(s, ip->src, seg->sport);

			// XXX skipping the precedence stuff

			// Set RCV.NXT to SEG.SEQ+1, IRS is set to SEG.SEQ and
			// any other control or text should be queued for
			// processing later. ISS should be selected and a SYN
			// segment sent:
			cb->rcv_nxt = ntohl(seg->seq) + 1;
			cb->irs = ntohl(seg->seq);
			cb->iss = (uint32_t)random();
			tcp_tx(s);

			// SND.NXT is set to ISS+1 and SND.UNA to ISS.  The
			// connection state should be changed to SYN-RECEIVED.
			cb->snd_nxt = cb->iss + 1;
			cb->snd_una = cb->iss;
			cb->state = SYN_RECEIVED;

			// Note that any other incoming control or data
			// (combined with SYN) will be processed in the SYN-
			// RECEIVED state, but processing of SYN and ACK should
			// not be repeated.  If the listen was not fully
			// specified (i.e., the foreign socket was not fully
			// specified), then the unspecified fields should be
			// filled in now.
			return;
		}

		// fourth other text or control
		// Any other control or text-bearing segment (not
		// containing SYN) must have an ACK and thus would be
		// discarded by the ACK processing.  An incoming RST
		// segment could not be valid, since it could not have
		// been sent in response to anything sent by this
		// incarnation of the connection.  So you are unlikely
		// to get here, but if you do, drop the segment, and
		// return.
	}

	if (cb->state == SYN_SENT) {
		// If the state is SYN-SENT then
		// first check the ACK bit
		// If the ACK bit is set
		bool ack_ok = false;
		if (seg->flags & ACK) {
			// If SEG.ACK =< ISS, or SEG.ACK > SND.NXT, send a reset
			// (unless the RST bit is set, if so drop the segment
			// and return) and discard the segment.  Return.
			if (ntohl(seg->ack) <= cb->iss ||
			    ntohl(seg->ack) > cb->snd_nxt) {
				if (seg->flags & RST) {
					goto_drop;
				} else {
					goto_rst;
				}
			}

			// If SND.UNA < SEG.ACK =< SND.NXT then the ACK is
			// acceptable.
			ack_ok = (cb->snd_una < ntohl(seg->ack) &&
				  ntohl(seg->ack) <= cb->snd_nxt);
		}

		// second check the RST bit
		// If the RST bit is set
		if (seg->flags & RST) {
			// If the ACK was acceptable then signal the user
			// "error: connection reset", drop the segment, enter
			// CLOSED state, delete TCB, and return.
			if (ack_ok)
				die("connection reset");
			// Otherwise (no ACK) drop the segment and return.
			goto_drop;
		}

		// XXX skipping the precedence stuff

		// fourth check the SYN bit
		if (seg->flags & SYN) {
			// This step should be reached only if the ACK is ok, or
			// there is no ACK, and it the segment did not contain a
			// RST.

			// If the SYN bit is on and the security/compartment and
			// precedence are acceptable then, RCV.NXT is set to
			// SEG.SEQ+1, IRS is set to SEG.SEQ.  SND.UNA should be
			// advanced to equal SEG.ACK (if there is an ACK), and
			// any segments on the retransmission queue which are
			// thereby acknowledged should be removed.
			cb->rcv_nxt = ntohl(seg->seq) + 1;
			cb->irs = ntohl(seg->seq);
			if (seg->flags & ACK)
				cb->snd_una = ntohl(seg->ack);

			if (cb->snd_una > cb->iss) {
				// If SND.UNA > ISS (our SYN has been ACKed),
				// change the connection state to ESTABLISHED,
				// form an ACK segment and send it.  Data or
				// controls which were queued for transmission
				// may be included.  If there are other controls
				// or text in the segment then continue
				// processing at the sixth step below where the
				// URG bit is checked, otherwise return.
				cb->state = ESTABLISHED;
				// send ACK
				tcp_tx(s);
				return;

			} else {
				// Otherwise enter SYN-RECEIVED, form a SYN,ACK
				// segment and send it.
				cb->state = SYN_RECEIVED;
				// send ACK
				tcp_tx(s);

				// Set the variables:
				// SND.WND <- SEG.WND
				// SND.WL1 <- SEG.SEQ
				// SND.WL2 <- SEG.ACK
				cb->snd_wnd = ntohs(seg->wnd);
				cb->snd_wl1 = ntohl(seg->seq);
				cb->snd_wl2 = ntohl(seg->ack);

				// If there are other controls or text in the
				// segment, queue them for processing after the
				// ESTABLISHED state has been reached, return.
				return;
			}
		}

		// fifth, if neither of the SYN or RST bits is set then
		// drop the segment and return.
		if (seg->flags & SYN || seg->flags & RST)
			goto_drop;
	}

	// Otherwise, first check sequence number
	if (cb->state >= SYN_RECEIVED) {
		// If an incoming segment is not acceptable, an acknowledgment
		// should be sent in reply (unless the RST bit is set, if so
		// drop the segment and return). After sending the
		// acknowledgment, drop the unacceptable segment and return.
		if (!tcp_ack_is_ok(cb, buf)) {
			if (!(seg->flags & RST)) {
				warn(debug, "ACK *NOT* OK");
				tcp_tx(s);
			}
			goto_drop;
		}
	}

	// second check the RST bit,
	if (seg->flags & RST) {
		if (cb->state == SYN_RECEIVED) {
			// If this connection was initiated with a passive OPEN
			// (i.e., came from the LISTEN state), then return this
			// connection to LISTEN state and return.  The user need
			// not be informed.

			//  If this connection was initiated with an active OPEN
			// (i.e., came from SYN-SENT state) then the connection
			// was refused, signal the user "connection refused".

			// In either case, all segments on the retransmission
			// queue should be removed.  And in the active OPEN
			// case, enter the CLOSED state and delete the TCB, and
			// return.
			die("connection refused");
		}

		if (cb->state == ESTABLISHED || cb->state == FIN_WAIT_1 ||
		    cb->state == FIN_WAIT_2 || cb->state == CLOSE_WAIT) {
			// If the RST bit is set then, any outstanding RECEIVEs
			// and SEND should receive "reset" responses.  All
			// segment queues should be flushed.  Users should also
			// receive an unsolicited general "connection reset"
			// signal.  Enter the CLOSED state, delete the TCB, and
			// return.
			die("connection reset");
		}

		if (cb->state == CLOSING || cb->state == LAST_ACK ||
		    cb->state == TIME_WAIT ) {
			// If the RST bit is set then, enter the CLOSED state,
			// delete the TCB, and return.
			cb->state = CLOSED;
			return;
		}
	}

	// XXX skipping the precedence stuff

	// fourth, check the SYN bit
	if (seg->flags & SYN) {
		if (cb->state >= SYN_RECEIVED) {
			// If the SYN is in the window it is an error, send a reset,
			// any outstanding RECEIVEs and SEND should receive "reset"
			// responses, all segment queues should be flushed, the user
			// should also receive an unsolicited general "connection
			// reset" signal, enter the CLOSED state, delete the TCB,
			// and return.
			die("connection reset");

			// If the SYN is not in the window this step would not be
			// reached and an ack would have been sent in the first step
			// (sequence number check).

		}
	}


	// fifth check the ACK field,
	if (!(seg->flags & ACK)) {
		// if the ACK bit is off drop the segment and return
		goto_drop;
	} else {
		// if the ACK bit is on
		if (cb->state == SYN_RECEIVED) {
			if (cb->snd_una < ntohl(seg->ack) &&
			    ntohl(seg->ack) <= cb->snd_nxt) {
				// If SND.UNA < SEG.ACK =< SND.NXT then enter
				// ESTABLISHED state and continue processing
				// with variables below set to:
				// SND.WND <- SEG.WND
				// SND.WL1 <- SEG.SEQ
				// SND.WL2 <- SEG.ACK
				cb->state = ESTABLISHED;
				cb->snd_wnd = ntohs(seg->wnd);
				cb->snd_wl1 = ntohl(seg->seq);
				cb->snd_wl2 = ntohl(seg->ack);

			} else {
				// If the segment acknowledgment is not
				// acceptable, form a reset segment, and send
				// it.
				goto_rst;
			}
		}

		if (cb->state == ESTABLISHED || cb->state == FIN_WAIT_1 ||
		    cb->state == FIN_WAIT_2 || cb->state == CLOSE_WAIT) {
			// If SND.UNA < SEG.ACK =< SND.NXT then, set SND.UNA <-
			// SEG.ACK.  Any segments on the retransmission queue
			// which are thereby entirely acknowledged are removed.
			// Users should receive positive acknowledgments for
			// buffers which have been SENT and fully acknowledged
			// (i.e., SEND buffer should be returned with "ok"
			// response).  If the ACK is a duplicate (SEG.ACK =<
			// SND.UNA), it can be ignored.  If the ACK acks
			// something not yet sent (SEG.ACK > SND.NXT) then send
			// an ACK, drop the segment, and return.
			if (ntohl(seg->ack) <= cb->snd_una) {
				warn(debug, "duplicate ACK");
			} else {
				if (ntohl(seg->ack) > cb->snd_nxt) {
					tcp_tx(s);
					goto_drop;
				} else {
					cb->snd_una = ntohl(seg->ack);
				}
			}

			// If SND.UNA =< SEG.ACK =< SND.NXT, the send window
			// should be updated.  If (SND.WL1 < SEG.SEQ or (SND.WL1
			// = SEG.SEQ and SND.WL2 =< SEG.ACK)), set SND.WND <-
			// SEG.WND, set SND.WL1 <- SEG.SEQ, and set SND.WL2 <-
			// SEG.ACK.
			if (cb->snd_una <= ntohl(seg->ack) &&
			    ntohl(seg->ack) <= cb->snd_nxt) {
				if (cb->snd_wl1 < ntohl(seg->seq) ||
				    (cb->snd_wl1 == ntohl(seg->seq) &&
				     cb->snd_wl2 <= ntohl(seg->ack))) {
					cb->snd_wnd = ntohs(seg->wnd);
					cb->snd_wl1 = ntohl(seg->seq);
					cb->snd_wl2 = ntohl(seg->ack);
				}
			}
		}

		if (cb->state == FIN_WAIT_1) {
			// In addition to the processing for the ESTABLISHED
			// state, if our FIN is now acknowledged then enter FIN-
			// WAIT-2 and continue processing in that state.
			die("XXX");
		}

		if (cb->state == FIN_WAIT_2) {
			// In addition to the processing for the ESTABLISHED
			// state, if the retransmission queue is empty, the
			// user's CLOSE can be acknowledged ("ok") but do not
			// delete the TCB.
			die("XXX");
		}

		if (cb->state == CLOSE_WAIT) {
			// Do the same processing as for the ESTABLISHED state.
			die("XXX");
		}

		if (cb->state == CLOSING) {
			// In addition to the processing for the ESTABLISHED
			// state, if the ACK acknowledges our FIN then enter the
			// TIME-WAIT state, otherwise ignore the segment.
			die("XXX");
		}

		if (cb->state == LAST_ACK) {
			// The only thing that can arrive in this state is an
			// acknowledgment of our FIN.  If our FIN is now
			// acknowledged, delete the TCB, enter the CLOSED state,
			// and return.
			die("XXX");
		}

		if (cb->state == TIME_WAIT) {
			// The only thing that can arrive in this state is a
			// retransmission of the remote FIN.  Acknowledge it,
			// and restart the 2 MSL timeout.
			die("XXX");
		}
	}

        // sixth, check the URG bit

	// seventh, process the segment text,
	if (cb->state == ESTABLISHED || cb->state == FIN_WAIT_1 ||
	    cb->state == FIN_WAIT_2) {
		// Once in the ESTABLISHED state, it is possible to deliver
		// segment text to user RECEIVE buffers.  Text from segments can
		// be moved into buffers until either the buffer is full or the
		// segment is empty.  If the segment empties and carries an PUSH
		// flag, then the user is informed, when the buffer is returned,
		// that a PUSH has been received.

		// When the TCP takes responsibility for delivering the data to
		// the user it must also acknowledge the receipt of the data.

		// Once the TCP takes responsibility for the data it advances
		// RCV.NXT over the data accepted, and adjusts RCV.WND as
		// appropriate to the current buffer availability.  The total of
		// RCV.NXT and RCV.WND should not be reduced.
		cb->rcv_nxt = ntohl(seg->seq) + 1;

		// Please note the window management suggestions in section 3.7.

		// Send an acknowledgment of the form:
		//   <SEQ=SND.NXT><ACK=RCV.NXT><CTL=ACK>
		// This acknowledgment should be piggybacked on a segment being
		// transmitted if possible without incurring undue delay.
		tcp_tx(s);
		sleep(3);
	}

	// eighth, check the FIN bit
	if (seg->flags & FIN) {
		// Do not process the FIN if the state is CLOSED, LISTEN or SYN-
		// SENT since the SEG.SEQ cannot be validated; drop the segment
		// and return.
		if (cb->state == CLOSED || cb->state == LISTEN ||
		    cb->state == SYN_SENT) {
			goto_drop;
		}

		// If the FIN bit is set, signal the user "connection closing"
		// and return any pending RECEIVEs with same message, advance
		// RCV.NXT over the FIN, and send an acknowledgment for the FIN.
		// Note that FIN implies PUSH for any segment text not yet
		// delivered to the user.

		if (cb->state == SYN_RECEIVED || cb->state == ESTABLISHED)
			// Enter the CLOSE-WAIT state.
			cb->state = CLOSE_WAIT;

		if (cb->state == FIN_WAIT_1) {
			// If our FIN has been ACKed (perhaps in this segment),
			// then enter TIME-WAIT, start the time-wait timer, turn
			// off the other timers; otherwise enter the CLOSING
			// state.
			die("XXX");
		}

		if (cb->state == FIN_WAIT_2) {
			// Enter the TIME-WAIT state.  Start the time-wait
			// timer, turn off the other timers.
			cb->state = TIME_WAIT;
		}

		// CLOSE-WAIT STATE
		// Remain in the CLOSE-WAIT state.

		// CLOSING STATE
		// Remain in the CLOSING state.

		// LAST-ACK STATE
		// Remain in the LAST-ACK state.

		// TIME-WAIT STATE
		// Remain in the TIME-WAIT state.  Restart the 2 MSL
		// time-wait timeout.

		return;
	}


		// if (seg->flags & ACK)
		// 	cb->snd_una = ntohl(seg->ack);

		// const uint16_t data_len = len - tcp_off(seg);
		// if (data_len == 0)
		// 	return;

		// cb->rcv_nxt += data_len;

		// // XXX TODO: code below doesn't preserve byte stream ordering

		// // grab an unused iov for the data in this packet
		// struct w_iov * const i = STAILQ_FIRST(&w->iov);
		// if (unlikely(i == 0))
		// 	die("out of spare bufs");
		// struct netmap_ring * const rxr =
		// 	NETMAP_RXRING(w->nif, w->cur_rxr);
		// struct netmap_slot * const rxs = &rxr->slot[rxr->cur];
		// STAILQ_REMOVE_HEAD(&w->iov, next);

		// warn(debug, "swapping rx ring %d slot %d (buf %d) and spare buf %d",
		//      w->cur_rxr, rxr->cur, rxs->buf_idx, i->idx);

		// // remember index of this buffer
		// const uint32_t tmp_idx = i->idx;

		// // move the received data into the iov
		// i->buf = (char *)seg + tcp_off(seg);
		// i->len = data_len;
		// i->idx = rxs->buf_idx;

		// // copy over the rx timestamp
		// memcpy(&i->ts, &rxr->ts, sizeof(struct timeval));

		// // append the iov to the socket
		// STAILQ_INSERT_TAIL(&s->iv, i, next);

		// // put the original buffer of the iov into the receive ring
		// rxs->buf_idx = tmp_idx;
		// rxs->flags = NS_BUF_CHANGED;
		return;
	// }

rst:
	tcp_tx_rst(w, buf);

drop:
	return;
}


void
tcp_tx(struct w_sock * const s)
{
	warn(notice, "am in state %s", tcp_state_name[s->cb->state]);

	switch (s->cb->state) {
	case CLOSED:
		tcp_tx_syn(s);
		// s->cb->state = SYN_SENT;
		return;

	case LISTEN:
		tcp_tx_syn(s);
		return;

	case ESTABLISHED:
		if (STAILQ_EMPTY(&s->ov)) {
			tcp_tx_ack(s);
		}
		// // packetize bufs and place in tx ring
		// while (likely(!STAILQ_EMPTY(&s->ov))) {
		// 	struct w_iov * const v = STAILQ_FIRST(&s->ov);
		// 	STAILQ_REMOVE_HEAD(&s->ov, next);

		// 	// copy template header into buffer and fill in remaining fields
		// 	char * const buf = IDX2BUF(s->w, v->idx);
		// 	memcpy(buf, s->hdr, s->hdr_len);

		// 	struct tcp_hdr * const seg = ip_data(buf);
		// 	seg->seq = htonl(s->cb->snd_una);
		// 	s->cb->snd_una += v->len;
		// 	seg->flags |= ACK;
		// 	if (STAILQ_EMPTY(&s->ov))
		// 		seg->flags |= PSH;
		// 	seg->ack = htonl(s->cb->rcv_nxt);

		// 	tcp_tx_do(s, v);
		// 	STAILQ_INSERT_HEAD(&s->w->iov, v, next);
		// }
		// w_kick_tx(s->w);
		return;
	}

	die("unknown transition in %s", tcp_state_name[s->cb->state]);
}
