#ifndef _tcp_h_
#define _tcp_h_

#include <stdint.h>

#include "warpcore.h"

#define FIN  0x01
#define SYN  0x02
#define RST  0x04
#define PUSH 0x08
#define ACK  0x10
#define URG  0x20
#define ECE  0x40
#define CWR  0x80

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
        uint16_t 	sum;		// checksum
        uint16_t 	urp;		// urgent pointer
} __packed __aligned(4);



struct warpcore;
struct w_sock;
struct w_iov;

extern void
tcp_rx(struct warpcore * const w, char * const buf, const uint16_t off,
       const uint16_t len, const uint32_t src);

extern void
tcp_tx(struct w_sock * const s);

#endif
