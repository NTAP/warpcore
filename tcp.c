#include <arpa/inet.h>
#include <sys/param.h>

#include "warpcore.h"
#include "tcp.h"


// Sequence number comparisons
#define seq_lt(a, b)	((int32_t)((a) - (b)) < 0)	// a < b
#define seq_lte(a, b)	((int32_t)((a) - (b)) <= 0)	// a <= b
#define seq_gt(a, b)	((int32_t)((a) - (b)) > 0)	// a > b


// Log a TCP segment. Since we're normalizing the sequence number spaces using
// some static variables, this only really works for a single active connection.
static uint32_t iss_sm = 0;
static uint32_t iss_lg = 0;

#ifndef NDEBUG
static const char * const tcp_state_name[] = {
	"CLOSED", "LISTEN", "SYN_SENT", "SYN_RECEIVED", "ESTABLISHED",
	"CLOSE_WAIT", "FIN_WAIT_1", "CLOSING", "LAST_ACK", "FIN_WAIT_2",
	"TIME_WAIT"
};

#define tcp_log(seg, len)						\
	do {								\
		uint32_t _irs;						\
		if (seg->sport < seg->dport) {				\
			iss_sm = iss_sm ? iss_sm : ntohl(seg->seq);	\
			_irs = iss_lg;					\
		} else {						\
			iss_lg = iss_lg ? iss_lg : ntohl(seg->seq);	\
			_irs = iss_sm;					\
		}							\
		const uint32_t _seq = ntohl(seg->seq) - 		\
			(seg->sport < seg->dport ? iss_sm : iss_lg);	\
		const uint16_t _len = len - tcp_off(seg);		\
		warn(info, BLD"TCP :%s%d"NRM BLD"->:%s%d"NRM ", "	\
		     "flags [%s%s%s%s%s%s%s%s], cksum 0x%04x, "		\
		     BLD"%sseq %u%c%u%c"NRM ", " BLD"%sack %u"NRM 	\
		     ", win %u, len %u",				\
		     seg->sport < seg->dport ? RED : BLU,		\
		     ntohs(seg->sport),					\
		     seg->sport < seg->dport ? BLU : RED,		\
		     ntohs(seg->dport),					\
		     seg->flags & FIN ? RED REV BLD"F"NRM : "",		\
		     seg->flags & SYN ? RED REV BLD"S"NRM : "",		\
		     seg->flags & RST ? "R" : "",			\
		     seg->flags & PSH ? "P" : "",			\
		     seg->flags & ACK ? "A" : "",			\
		     seg->flags & URG ? "U" : "",			\
		     seg->flags & ECE ? "E" : "",			\
		     seg->flags & CWR ? "C" : "",			\
		     ntohs(seg->cksum),					\
		     seg->sport < seg->dport ? RED : BLU,		\
		     _seq, (_len ? ':' : 0), (_len ? _seq + _len : 0),	\
		     (_len ? 0 : '\b'),					\
		     seg->sport < seg->dport ? BLU : RED,		\
		     ntohl(seg->ack) - _irs, ntohs(seg->wnd), _len);	\
	} while (0)
#else
#define tcp_log(seg, len)	do {} while (0)
#endif

#define tcp_log_init	do { iss_sm = iss_lg = 0; } while (0)


// Log the TCP control block
#define tcp_log_cb(cb)							\
	if (cb)								\
		warn(debug, "state %s, snd_una %u, snd_nxt %u, "	\
		     "snd_wnd %u, snd_wl1 %u, snd_wl2 %u, iss %u, "	\
		     "rcv_nxt %u, irs %u", tcp_state_name[cb->state],	\
		     cb->snd_una - cb->iss, cb->snd_nxt - cb->iss,	\
		     cb->snd_wnd, cb->snd_wl1 - cb->irs,		\
		     cb->snd_wl2 - cb->iss, cb->iss,			\
		     cb->rcv_nxt - cb->irs, cb->irs)


// Calculate the length of the TCP payload in a segment. Needs the
// buffer passed in, since we need the IP length field information for this.
// Also account for SYN and FIN in the sequence number space.
static inline uint16_t
tcp_seg_len(char * const buf)
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
	const uint8_t *n = (const uint8_t * const)(seg) + sizeof(struct tcp_hdr);
	const uint8_t * const last = (const uint8_t * const )(seg) + tcp_off(seg);
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
	} while (n < last);
done:
	if (n < last) {
		warn(warn, "%ld bytes of padding after options", last - n);
	}
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
tcp_rcv_wnd(struct w_sock * const s __unused)
{
	// uint32_t rx_bytes_avail = 0;
	// for (uint32_t ri = 0; ri < s->w->nif->ni_rx_rings; ri++) {
	// 	struct netmap_ring * const r =
	// 		NETMAP_RXRING(s->w->nif, ri);
	// 	rx_bytes_avail += (r->num_slots - nm_ring_space(r)) *
	// 			  (s->w->mtu - s->hdr_len);
	// }
	// return rx_bytes_avail;
	return 0xffff;
}


void
tcp_rx(struct warpcore * const w, char * const buf)
{
	const struct ip_hdr * const ip = eth_data(buf);
	struct tcp_hdr * const seg = ip_data(buf);
	const uint16_t len = ip_data_len(ip);

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
			goto rst;
		return;
	}
	struct w_sock * const s = *ss;
	struct tcp_cb * const cb = s->cb;


	if (cb->state == LISTEN)
		tcp_log_init;

	tcp_log(seg, len);
	tcp_log_cb(cb);

	// if there are TCP options in the segment, parse them
	if (tcp_off(seg) > sizeof(struct tcp_hdr))
		tcp_parse_options(seg, cb);

	// some shortcuts for segment header fields
	const uint32_t seg_seq = ntohl(seg->seq);
	const uint32_t seg_ack = ntohl(seg->ack);
	const uint16_t seg_wnd = ntohs(seg->wnd);
	const uint16_t seg_len = tcp_seg_len(buf);
	const uint32_t rcv_wnd = tcp_rcv_wnd(cb->s);

	if (cb->state == CLOSED) {
		// If the state is CLOSED (i.e., TCB does not exist) then:
		// All data in the incoming segment is discarded.  An incoming
		// segment containing a RST is discarded.  An incoming segment
		// not containing a RST causes a RST to be sent in response.
		if (!(seg->flags & RST))
			goto rst;
		goto drop;
	}

	if (cb->state == LISTEN) {
		// If the state is LISTEN then:
		// first check for an RST
		// An incoming RST should be ignored.  Return.
		if (seg->flags & RST)
			goto drop;

		// second check for an ACK
		// Any acknowledgment is bad if it arrives on a connection
		// still in the LISTEN state.  An acceptable reset segment
		// should be formed for any arriving ACK-bearing segment.
		// Return.
		if (seg->flags & ACK)
			goto rst;

		// third check for a SYN
		if (seg->flags & SYN) {
			// "bind" the socket to the destination
			// explicitly set the destination MAC to save an ARP
			struct eth_hdr * const eth =
				(struct eth_hdr * const)(buf);
			memcpy(s->dmac, eth->src, ETH_ADDR_LEN);
			w_connect(s, ip->src, seg->sport);
			cb->state = SYN_RECEIVED;

			// Set RCV.NXT to SEG.SEQ+1, IRS is set to SEG.SEQ and
			// any other control or text should be queued for
			// processing later. ISS should be selected and a SYN
			// segment sent:
			cb->rcv_nxt = seg_seq + 1;
			cb->irs = seg_seq;
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
		goto drop;
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
			if (seq_lte(seg_ack, cb->iss) ||
			    seq_gt(seg_ack, cb->snd_nxt)) {
				if (seg->flags & RST) {
					goto drop;
				} else {
					goto rst;
				}
			}

			// If SND.UNA < SEG.ACK =< SND.NXT then the ACK is
			// acceptable.
			ack_ok = seq_lt(cb->snd_una, seg_ack) &&
				 seq_lte(seg_ack, cb->snd_nxt);
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
			goto drop;
		}

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
			cb->rcv_nxt = seg_seq + 1;
			cb->irs = seg_seq;
			if (seg->flags & ACK)
				cb->snd_una = seg_ack;

			if (seq_gt(cb->snd_una, cb->iss)) {
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
				cb->snd_wnd = seg_wnd;
				cb->snd_wl1 = seg_seq;
				cb->snd_wl2 = seg_ack;

				// If there are other controls or text in the
				// segment, queue them for processing after the
				// ESTABLISHED state has been reached, return.
				return;
			}
		}

		// fifth, if neither of the SYN or RST bits is set then
		// drop the segment and return.
		if (seg->flags & SYN || seg->flags & RST)
			goto drop;
	}

	// Otherwise, first check sequence number
	if (cb->state >= SYN_RECEIVED) {
		// Segments are processed in sequence.  Initial tests on arrival
		// are used to discard old duplicates, but further processing is
		// done in SEG.SEQ order.  If a segment's contents straddle the
		// boundary between old and new, only the new parts should be
		// processed.

		// There are four cases for the acceptability test for an
		// incoming segment:

		// Segment Receive  Test
		// Length  Window
		// ------- -------  -------------------------------------------

		//  0       0     SEG.SEQ = RCV.NXT

		//  0      >0     RCV.NXT =< SEG.SEQ < RCV.NXT+RCV.WND

		// >0       0     not acceptable

		// >0      >0     RCV.NXT =< SEG.SEQ < RCV.NXT+RCV.WND or
		// 		  RCV.NXT =< SEG.SEQ+SEG.LEN-1 < RCV.NXT+RCV.WND

		// If the RCV.WND is zero, no segments will be acceptable, but
		// special allowance should be made to accept valid ACKs, URGs
		// and RSTs.

		bool ack_ok = false;
		if (seg_len == 0) {
			if (rcv_wnd == 0) {
				if (seg_seq == cb->rcv_nxt)
					ack_ok = true;
				// else
				// 	warn(debug, "case 0 0");
			} else {
				if (seq_lte(cb->rcv_nxt, seg_seq) &&
				    seq_lt(seg_seq, cb->rcv_nxt + rcv_wnd))
					ack_ok = true;
				// else
				// 	warn(debug, "case 0 >0");
			}
		} else {
			if (rcv_wnd != 0) {
				if ((seq_lte(cb->rcv_nxt, seg_seq) &&
				     seq_lt(seg_seq, cb->rcv_nxt + rcv_wnd)) ||
				    (seq_lte(cb->rcv_nxt,
					     seg_seq + seg_len - 1) &&
				     seq_lt(seg_seq + seg_len - 1,
					    cb->rcv_nxt + rcv_wnd)))
					ack_ok = true;
				// else
				// 	warn(debug, "case >0 >0");
			}
			// else
			// 	warn(debug, "case case >0 0");
		}

		// If an incoming segment is not acceptable, an acknowledgment
		// should be sent in reply (unless the RST bit is set, if so
		// drop the segment and return). After sending the
		// acknowledgment, drop the unacceptable segment and return.
		if (!ack_ok) {
			if (!(seg->flags & RST)) {
				// warn(debug, "ACK *NOT* OK");
				tcp_tx(s);
			}
			goto drop;
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

	// fourth, check the SYN bit
	if (seg->flags & SYN) {
		if (cb->state >= SYN_RECEIVED) {
			// If the SYN is in the window it is an error, send a
			// reset, any outstanding RECEIVEs and SEND should
			// receive "reset" responses, all segment queues should
			// be flushed, the user should also receive an
			// unsolicited general "connection reset" signal, enter
			// the CLOSED state, delete the TCB, and return.
			die("connection reset");

			// If the SYN is not in the window this step would not
			// be reached and an ack would have been sent in the
			// first step (sequence number check).
		}
	}


	// fifth check the ACK field,
	if (!(seg->flags & ACK)) {
		// if the ACK bit is off drop the segment and return
		goto drop;
	} else {
		// if the ACK bit is on
		if (cb->state == SYN_RECEIVED) {
			if (seq_lt(cb->snd_una, seg_ack) &&
			    seq_lte(seg_ack, cb->snd_nxt)) {
				// If SND.UNA < SEG.ACK =< SND.NXT then enter
				// ESTABLISHED state and continue processing
				// with variables below set to:
				// SND.WND <- SEG.WND
				// SND.WL1 <- SEG.SEQ
				// SND.WL2 <- SEG.ACK
				cb->state = ESTABLISHED;
				cb->snd_wnd = seg_wnd;
				cb->snd_wl1 = seg_seq;
				cb->snd_wl2 = seg_ack;

			} else {
				// If the segment acknowledgment is not
				// acceptable, form a reset segment, and send
				// it.
				goto rst;
			}
		}

		if (cb->state == ESTABLISHED || cb->state == FIN_WAIT_1 ||
		    cb->state == FIN_WAIT_2 || cb->state == CLOSE_WAIT ||
		    cb->state == CLOSING) {
			// If SND.UNA < SEG.ACK =< SND.NXT then, set SND.UNA <-
			// SEG.ACK.  Any segments on the retransmission queue
			// which are thereby entirely acknowledged are removed.
			// Users should receive positive acknowledgments for
			// buffers which have been SENT and fully acknowledged
			// (i.e., SEND buffer should be returned with "ok"
			// response).
			if (seq_lt(cb->snd_una, seg_ack) &&
			    seq_lte(seg_ack, cb->snd_nxt)) {
				cb->snd_una = seg_ack;
				warn(debug, "XXX snd_una advance, do rx queue");
			}

			// If the ACK is a duplicate (SEG.ACK =< SND.UNA), it
			// can be ignored.  If the ACK acks something not yet
			// sent (SEG.ACK > SND.NXT) then send an ACK, drop the
			// segment, and return.
			if (seq_gt(seg_ack, cb->snd_nxt)) {
				tcp_tx(s);
				goto drop;
			}

			// If SND.UNA =< SEG.ACK =< SND.NXT, the send window
			// should be updated.  If (SND.WL1 < SEG.SEQ or (SND.WL1
			// = SEG.SEQ and SND.WL2 =< SEG.ACK)), set SND.WND <-
			// SEG.WND, set SND.WL1 <- SEG.SEQ, and set SND.WL2 <-
			// SEG.ACK.
			if (seq_lte(cb->snd_una, seg_ack) &&
			    seq_lte(seg_ack, cb->snd_nxt)) {
				if (seq_lt(cb->snd_wl1, seg_seq) ||
				    (cb->snd_wl1 == seg_seq &&
				     seq_lte(cb->snd_wl2, seg_ack))) {
					cb->snd_wnd = seg_wnd;
					cb->snd_wl1 = seg_seq;
					cb->snd_wl2 = seg_ack;
				}
			}
		}

		if (cb->state == FIN_WAIT_1) {
			// In addition to the processing for the ESTABLISHED
			// state, if our FIN is now acknowledged then enter FIN-
			// WAIT-2 and continue processing in that state.
			if (seg_ack == cb->snd_nxt)
				cb->state = FIN_WAIT_2;
		}

		if (cb->state == FIN_WAIT_2) {
			// In addition to the processing for the ESTABLISHED
			// state, if the retransmission queue is empty, the
			// user's CLOSE can be acknowledged ("ok") but do not
			// delete the TCB.
		}

		if (cb->state == CLOSE_WAIT) {
			// Do the same processing as for the ESTABLISHED state.
			// die("XXX");
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
			// tcp_log_cb(cb);
			if (seg_ack == cb->snd_nxt + 1) {
				warn(debug, "XXX going to CLOSED or LISTEN");
				cb->state = cb->active ? CLOSED : LISTEN;
				return;
			}
		}

		if (cb->state == TIME_WAIT) {
			// The only thing that can arrive in this state is a
			// retransmission of the remote FIN.  Acknowledge it,
			// and restart the 2 MSL timeout.
			die("XXX");
		}
	}

	// seventh, process the segment text,
	if (cb->state == ESTABLISHED || cb->state == FIN_WAIT_1 ||
	    cb->state == FIN_WAIT_2) {
		if (len - tcp_off(seg) > 0) {
			// Once in the ESTABLISHED state, it is possible to
			// deliver segment text to user RECEIVE buffers.  Text
			// from segments can be moved into buffers until either
			// the buffer is full or the segment is empty.  If the
			// segment empties and carries an PUSH flag, then the
			// user is informed, when the buffer is returned, that a
			// PUSH has been received.

			// If this segment is out of order, panic
			if (seg_seq != cb->rcv_nxt)
				die("XXX out of order segment");

			// grab an unused iov for the data in this packet
			struct w_iov * const i = STAILQ_FIRST(&w->iov);
			if (unlikely(i == 0))
				die("out of spare bufs");
			struct netmap_ring * const rxr =
				NETMAP_RXRING(w->nif, w->cur_rxr);
			struct netmap_slot * const rxs = &rxr->slot[rxr->cur];
			STAILQ_REMOVE_HEAD(&w->iov, next);

			// warn(debug, "swapping rx ring %d slot %d (buf %d) "
			//      "and spare buf %d",
			//      w->cur_rxr, rxr->cur, rxs->buf_idx, i->idx);

			// remember index of this buffer
			const uint32_t tmp_idx = i->idx;

			// move the received data into the iov
			i->buf = (char *)seg + tcp_off(seg);
			i->len = len - tcp_off(seg);
			i->idx = rxs->buf_idx;

			// tag the iov with the sender's information
			i->src = ip->src;
			i->sport = seg->sport;

			// copy over the rx timestamp
			memcpy(&i->ts, &rxr->ts, sizeof(struct timeval));

			// append the iov to the socket
			STAILQ_INSERT_TAIL(&s->iv, i, next);

			// put the original buffer of the iov into the receive
			// ring
			rxs->buf_idx = tmp_idx;
			rxs->flags = NS_BUF_CHANGED;

			// When the TCP takes responsibility for delivering the
			// data to the user it must also acknowledge the receipt
			// of the data.

			// Once the TCP takes responsibility for the data it
			// advances RCV.NXT over the data accepted, and adjusts
			// RCV.WND as appropriate to the current buffer
			// availability.  The total of RCV.NXT and RCV.WND
			// should not be reduced.
			cb->rcv_nxt = seg_seq + seg_len;

			// Please note the window management suggestions in
			// section 3.7.

			// Send an acknowledgment. This acknowledgment should be
			// piggybacked on a segment being transmitted if
			// possible without incurring undue delay.
			tcp_tx(s);
		}
	}

	// eighth, check the FIN bit
	if (seg->flags & FIN) {
		// Do not process the FIN if the state is CLOSED, LISTEN or SYN-
		// SENT since the SEG.SEQ cannot be validated; drop the segment
		// and return.
		if (cb->state == CLOSED || cb->state == LISTEN ||
		    cb->state == SYN_SENT)
			goto drop;

		// If the FIN bit is set, signal the user "connection closing"
		// and return any pending RECEIVEs with same message, advance
		// RCV.NXT over the FIN, and send an acknowledgment for the FIN.
		// Note that FIN implies PUSH for any segment text not yet
		// delivered to the user.
		cb->rcv_nxt = seg_seq + 1;

		if (cb->state == SYN_RECEIVED || cb->state == ESTABLISHED) {
			// Enter the CLOSE-WAIT state.
			cb->state = CLOSE_WAIT;
			tcp_tx(s);
		}

		if (cb->state == FIN_WAIT_1) {
			// If our FIN has been ACKed (perhaps in this segment),
			// then enter TIME-WAIT, start the time-wait timer, turn
			// off the other timers; otherwise enter the CLOSING
			// state.
			if (seg_seq == cb->snd_nxt)
				cb->state = TIME_WAIT;
			else
				cb->state = CLOSING;
			tcp_tx(s);
		}

		if (cb->state == FIN_WAIT_2) {
			// Enter the TIME-WAIT state.  Start the time-wait
			// timer, turn off the other timers.
			tcp_tx(s);
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

		// XXX We don't really implement TIME_WAIT yet.
		if (cb->state == TIME_WAIT) {
			warn(debug, "TIME_WAIT -> CLOSED");
			cb->state = CLOSED;
		}
		return;
	}
	return;
rst:
	tcp_tx_rst(w, buf);
drop:
	return;
}


static inline struct w_iov *
tcp_tx_prep(struct w_sock * const s)
{
	struct w_iov *v;
	if (STAILQ_EMPTY(&s->ov) || s->cb->state < ESTABLISHED) {
		// not ready to send data, this will be an empty segment
		v = STAILQ_FIRST(&s->w->iov);
		if (unlikely(v == 0))
			die("out of spare bufs");
		STAILQ_REMOVE_HEAD(&s->w->iov, next);
		v->len = 0;
		// warn(debug, "grabbing empty iov");
	} else {
		// use the first iov with data to send
		v = STAILQ_FIRST(&s->ov);
		STAILQ_REMOVE_HEAD(&s->ov, next);
		// warn(debug, "grabbing iov with %d bytes of data", v->len);
	}

	// copy template header into buffer, caller to fill in remaining fields
	v->buf = IDX2BUF(s->w, v->idx);
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

	tcp_log(seg, len);

	// send the IP packet
	ip_tx(s->w, v, len);
}


void
tcp_tx(struct w_sock * const s)
{
	struct tcp_cb * const cb = s->cb;
	do {
		tcp_log_cb(cb);

		// grab a buffer
		struct w_iov * const v = tcp_tx_prep(s);
		struct tcp_hdr * const seg = ip_data(v->buf);

		if (cb->state == CLOSED) {
			tcp_log_init;
			cb->snd_una = cb->iss = (uint32_t)random();
			cb->snd_nxt = cb->iss + 1;
			cb->state = SYN_SENT;
			seg->flags = SYN;
			seg->seq = htonl(cb->iss);
			// this is an active open
			cb->active = true;
		}

		if (cb->state == SYN_RECEIVED) {
			seg->seq = htonl(cb->iss);
			seg->ack = htonl(cb->rcv_nxt);
			seg->flags = SYN|ACK;
		}

		if (cb->state == FIN_WAIT_1)
			seg->flags |= FIN;

		if (cb->state == ESTABLISHED ||
		    cb->state == LISTEN ||
		    cb->state == CLOSE_WAIT ||
		    cb->state == FIN_WAIT_1 ||
		    cb->state == FIN_WAIT_2 ||
		    cb->state == LAST_ACK ||
		    cb->state == TIME_WAIT) {
			seg->seq = htonl(cb->snd_nxt);
			seg->ack = htonl(cb->rcv_nxt);
			seg->flags |= ACK;
			// if (v->len && STAILQ_EMPTY(&s->ov))
			// 	seg->flags |= PSH;
			if ((cb->state == CLOSE_WAIT || cb->state == LAST_ACK) &&
			    STAILQ_EMPTY(&s->ov) &&
			    STAILQ_EMPTY(&s->iv)) {
				seg->flags |= FIN;
				cb->state = LAST_ACK;
			}

			if (cb->state == FIN_WAIT_1)
				cb->snd_nxt++;

			s->cb->snd_nxt += v->len;
		}

		tcp_tx_do(s, v);

		// make iov available again
		STAILQ_INSERT_HEAD(&s->w->iov, v, next);

	// send more if there is more to send
	} while (s->cb->state == ESTABLISHED && !STAILQ_EMPTY(&s->ov));

	w_kick_tx(s->w);
}
