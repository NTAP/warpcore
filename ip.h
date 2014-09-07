#ifndef _ip_h_
#define _ip_h_

#define IP_P_ICMP 	 1
#define IP_P_TCP	 6
#define IP_P_UDP	17

#define IP_ADDR_LEN	 4 // IP addresses are four bytes
#define IP_ADDR_STRLEN	16 // xxx.xxx.xxx.xxx\0

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
	uint16_t	id;		// identification
	uint16_t	off;		// fragment offset field
	uint8_t		ttl;		// time to live
	uint8_t		p;		// protocol
	uint16_t	cksum;		// checksum
	uint32_t	src, dst;	// source and dest address
} __packed __aligned(4);

struct warpcore;

extern void ip_tx(struct warpcore * w, const uint_fast8_t p,
                  const char * const buf, const uint_fast16_t len);

extern void ip_rx(struct warpcore * w, char * const buf);

extern const char * ip_ntoa(uint32_t ip, char * const buf, const size_t size);
extern uint32_t ip_aton(const char * const ip);

// this is defined in in_chksum.c, which is the FreeBSD checksum code
extern uint16_t in_cksum(void * const buf, const uint_fast16_t len);

#endif
