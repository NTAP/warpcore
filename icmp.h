#ifndef _icmp_h_
#define _icmp_h_

#include <stdint.h>

static const uint_fast8_t ICMP_TYPE_ECHOREPLY =		0;
static const uint_fast8_t ICMP_TYPE_UNREACH =		3;
static const uint_fast8_t ICMP_TYPE_ECHO = 		8;

static const uint_fast8_t ICMP_UNREACH_PROTOCOL =	2; // bad protocol
static const uint_fast8_t ICMP_UNREACH_PORT =		3; // bad port

struct icmp_hdr {
	uint8_t		type;	// type of message
	uint8_t		code;	// type sub code
	uint16_t	cksum;	// ones complement checksum of struct
} __packed;

struct warpcore;

extern void icmp_tx_unreach(const struct warpcore * const w,
                            const uint_fast8_t code, char * const buf,
                            const uint_fast16_t off);

extern void icmp_tx(const struct warpcore * const w, const char * const buf,
                    const uint_fast16_t off, const uint_fast16_t len);

extern void icmp_rx(const struct warpcore * const w, char * const buf,
                    const uint_fast16_t off, const uint_fast16_t len);

#endif
