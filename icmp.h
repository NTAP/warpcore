#ifndef _icmp_h_
#define _icmp_h_

#include <stdint.h>

static const uint_fast8_t ICMP_TYPE_ECHOREPLY =	0;
static const uint_fast8_t ICMP_TYPE_ECHO = 		8;

struct icmp_hdr {
	uint8_t		type;	// type of message
	uint8_t		code;	// type sub code
	uint16_t	cksum;	// ones complement checksum of struct
};

struct warpcore;
extern void icmp_rx(const struct warpcore * const w, const char * const buf, const uint16_t offset, const uint16_t len);

#endif
