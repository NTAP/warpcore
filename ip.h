#ifndef _ip_h_
#define _ip_h_

#include <stdint.h>

static const uint_fast8_t IP_P_ICMP	=	1; 	// control message protocol
static const uint_fast8_t IP_P_TCP	=	6;	// transmission control protocol

static const uint_fast8_t IP_ADDR_LEN =		 4; // IP addresses are four bytes
static const uint_fast8_t IP_ADDR_STRLEN =	16; // xxx.xxx.xxx.xxx\0

struct ip_hdr {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint8_t		hl:4,		// header length
				v:4;		// version
#endif
#if BYTE_ORDER == BIG_ENDIAN
	uint8_t		v:4,		// version
				hl:4;		// header length
#endif
	uint8_t		dscp;		// diff-serv code point
	uint16_t	len;		// total length
	uint16_t	id;			// identification
	uint16_t	off;		// fragment offset field
	uint8_t		ttl;		// time to live
	uint8_t		p;			// protocol
	uint16_t	cksum;		// checksum
	uint32_t	src, dst;	// source and dest address
} __packed __aligned(4);

struct warpcore;

extern void ip_tx(const struct warpcore * const w, const char * const buf);
extern void ip_rx(const struct warpcore * const w, const char * const buf);

extern char * ip_ntoa_r(uint32_t ip, char *buf, size_t size);

// this is defined in in_chksum.c, which is the FreeBSD checksum code
extern uint16_t in_cksum(void *m, uint16_t len);

#endif
