#ifndef _icmp_h_
#define _icmp_h_

#include <stdint.h>

#define ICMP_TYPE_ECHOREPLY	0
#define ICMP_TYPE_UNREACH	3
#define ICMP_TYPE_ECHO 		8

#define ICMP_UNREACH_PROTOCOL	2 // bad protocol
#define ICMP_UNREACH_PORT	3 // bad port

struct icmp_hdr {
	uint8_t		type;	// type of message
	uint8_t		code;	// type sub code
	uint16_t	cksum;	// ones complement checksum of struct
} __packed;

struct warpcore;

extern void icmp_tx_unreach(struct warpcore * w,
                            const uint_fast8_t code, char * const buf,
                            const uint_fast16_t off);

extern void icmp_tx(struct warpcore * w, const char * const buf,
                    const uint_fast16_t off, const uint_fast16_t len);

extern void icmp_rx(struct warpcore * w, char * const buf,
                    const uint_fast16_t off, const uint_fast16_t len);

#endif
