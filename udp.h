#ifndef _udp_h_
#define _udp_h_

#include <stdint.h>

struct udp_hdr {
	uint16_t sport;		// source port
	uint16_t dport;		// destination port
	uint16_t len;		// udp length
	uint16_t cksum;		// udp checksum
} __packed;


struct warpcore;

extern void udp_rx(const struct warpcore * const w, char * const buf,
                   const uint_fast16_t off);

#endif
