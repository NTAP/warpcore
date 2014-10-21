#ifndef _ip_h_
#define _ip_h_

#include <stdint.h>
#include <stdbool.h>

#define IP_P_ICMP 	 1	// IP protocol number for ICMP
#define IP_P_TCP	 6	// IP protocol number for TCP
#define IP_P_UDP	17	// IP protocol number for UDP

#define IP_ADDR_LEN	 4	// IPv4 addresses are four bytes
#define IP_ADDR_STRLEN	16	// xxx.xxx.xxx.xxx\0

struct ip_hdr {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint8_t		hl:4,		// header length
			v:4;		// version
#else
	uint8_t		v:4,		// version
			hl:4;		// header length
#endif
	uint8_t		dscp;		// diff-serv code point
	uint16_t	len;		// total length
	uint16_t	id;		// identification
	uint16_t	off;		// fragment offset field
	uint8_t		ttl;		// time to live
	uint8_t		p;		// protocol
	uint16_t	cksum;		// checksum
	uint32_t	src, dst;	// source and dest address
} __packed __aligned(4);

struct warpcore;
struct w_iov;

// see ip.c for documentation of functions
extern void
ip_tx_with_rx_buf(struct warpcore * w, const uint8_t p, char * const buf,
		  const uint16_t len);

extern const char *
ip_ntoa(uint32_t ip, char * const buf, const size_t size);

extern uint32_t
ip_aton(const char * const ip);

extern void
ip_rx(struct warpcore * const w, char * const buf);

extern bool
ip_tx(struct warpcore * w, struct w_iov * const v, const uint16_t len);

// these are defined in in_chksum.c, which is the FreeBSD checksum code
extern uint16_t
in_cksum(const void * const buf, const uint16_t len);

extern uint16_t in_pseudo(uint32_t sum, uint32_t b, uint32_t c);


#endif
