#ifndef _tcp_h_
#define _tcp_h_

#include <stdint.h>

#include "warpcore.h"

#define FIN  	0x01
#define SYN  	0x02
#define RST  	0x04
#define PSH 	0x08
#define ACK  	0x10
#define URG  	0x20
#define ECE  	0x40
#define CWR  	0x80

// Use field names similar to draft-eddy-rfc793bis
struct tcp_hdr {
	uint16_t 	sport;	// source port
	uint16_t 	dport;	// destination port
	uint32_t 	seq;	// SEG.SEQ - segment sequence number
	uint32_t 	ack;	// SEG.ACK - segment acknowledgment number
	uint8_t 	offx;	// data offset + reserved
	uint8_t 	flags;	// flags
	uint16_t 	wnd;	// SEG.WND - segment window
	uint16_t 	cksum;	// checksum
	uint16_t 	up;	// SEG.UP  - segment urgent pointer
} __packed __aligned(4);

#define tcp_off(tcp)		((((tcp)->offx & 0xf0) >> 4) * 4)

#define CLOSED		0	// closed
#define LISTEN		1	// listening for connection
#define SYN_SENT	2	// active, have sent syn
#define SYN_RECEIVED	3	// have sent and received syn
#define ESTABLISHED	4	// established
#define CLOSE_WAIT	5	// rcvd fin, waiting for close
#define FIN_WAIT_1	6	// have closed, sent fin
#define CLOSING		7	// closed xchd FIN; await FIN ACK
#define LAST_ACK	8	// had fin and close; await FIN ACK
#define FIN_WAIT_2	9	// have closed, fin is acked
#define TIME_WAIT	10	// in 2*msl quiet wait after close

// see draft-eddy-rfc793bis
struct tcp_cb {
	// Send Sequence Space
	//
	//            1         2          3          4
	//       ----------|----------|----------|----------
	//              SND.UNA    SND.NXT    SND.UNA
	//                                   +SND.WND
	//
	// 1 - old sequence numbers which have been acknowledged
	// 2 - sequence numbers of unacknowledged data
	// 3 - sequence numbers allowed for new data transmission
	// 4 - future sequence numbers which are not yet allowed

	uint32_t snd_una;	// SND.UNA - send unacknowledged
	uint32_t snd_nxt;	// SND.NXT - send next
	uint16_t snd_wnd;	// SND.WND - send window
	// uint16_t snd_up;	// SND.UP - send urgent pointer
	uint32_t snd_wl1;	// SND.WL1 - SEG.SEQ used for last window update
	uint32_t snd_wl2;	// SND.WL2 - SEG.ACK used for last window update
	uint32_t iss;		// ISS - initial send sequence number

	// Receive Sequence Space
	//
	//                1          2          3
	//            ----------|----------|----------
	//                   RCV.NXT    RCV.NXT
	//                             +RCV.WND
	//
	// 1 - old sequence numbers which have been acknowledged
	// 2 - sequence numbers allowed for new reception
	// 3 - future sequence numbers which are not yet allowed

	uint32_t rcv_nxt;	// RCV.NXT - receive next
	// uint32_t rcv_wnd;	// RCV.WND - receive window
	// uint32_t rcv_up;	// RCV.UP - receive urgent pointer
	uint32_t irs;		// IRS - initial receive sequence number

	// the fields below store information gleanded from TCP options
	uint8_t		shift_cnt;	// window scale shift amount
	bool		sack;		// SACK permitted?
	uint16_t	mss;		// maximum segment size
	uint32_t	ts_val;		// timestamp value
	uint32_t	ts_ecr;		// timestamp echo return

	struct w_sock * s;	// pointer back to the socket
	uint8_t		state;	// state of this connection
} __aligned(4);


struct warpcore;
struct w_sock;
struct w_iov;

extern void
tcp_rx(struct warpcore * const w, char * const buf);

extern void
tcp_tx(struct w_sock * const s);

#endif
