#ifndef _udp_h_
#define _udp_h_

#include <stdint.h>

struct udp_hdr {
	uint16_t sport;		// source port
	uint16_t dport;		// destination port
	uint16_t len;		// udp length
	uint16_t cksum;		// udp checksum
} __attribute__ ((__packed__));


struct warpcore;
struct w_sock;
struct w_iov;

// see udp.c for documentation of functions
extern bool udp_tx(struct w_sock *s, struct w_iov * const v);

extern void udp_rx(struct warpcore * w, char * const buf,
                   const uint_fast16_t off);

#endif
