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

struct tcp_hdr {
	uint16_t 	sport;		// source port
	uint16_t 	dport;		// destination port
	uint32_t 	seq;
	uint32_t 	ack;

#if BYTE_ORDER == LITTLE_ENDIAN
	uint8_t 	unused:4,	// (unused)
			off:4;		// data offset
#endif
#if BYTE_ORDER == BIG_ENDIAN
	uint8_t 	unused:4,	// (unused)
			off:4;		// data offset
#endif
	uint8_t 	flags;		// flags
	uint16_t 	win;		// window
	uint16_t 	cksum;		// checksum
	uint16_t 	urp;		// urgent pointer
} __packed __aligned(4);


#define CLOSED		0		// closed
#define LISTEN		1		// listening for connection
#define SYN_SENT	2		// active, have sent syn
#define SYN_RECEIVED	3		// have sent and received syn
#define ESTABLISHED	4		// established
#define CLOSE_WAIT	5		// rcvd fin, waiting for close
#define FIN_WAIT_1	6		// have closed, sent fin
#define CLOSING		7		// closed xchd FIN; await FIN ACK
#define LAST_ACK	8		// had fin and close; await FIN ACK
#define FIN_WAIT_2	9		// have closed, fin is acked
#define TIME_WAIT	10		// in 2*msl quiet wait after close

struct tcp_cb {
	uint8_t		state;		// state of this connection
	uint32_t	snd_una;
	uint32_t	irs;
	uint32_t	iss;
} __aligned(4);


struct warpcore;
struct w_sock;
struct w_iov;

extern void
tcp_rx(struct warpcore * const w, char * const buf);

extern void
tcp_tx(struct w_sock * const s);

#endif
