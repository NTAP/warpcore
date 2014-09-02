#ifndef _ip_h_
#define _ip_h_

#include <arpa/inet.h>					// struct in_addr

static const uint8_t IP_P_ICMP	=	1; 	// control message protocol
static const uint8_t IP_P_TCP	=	6 ;	// transmission control protocol

struct ip_hdr {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint8_t			hl:4,				// header length
					v:4;				// version
#endif
#if BYTE_ORDER == BIG_ENDIAN
	uint8_t			v:4,				// version
					hl:4;				// header length
#endif
	uint8_t			dscp;				// diff-serv code point
	uint16_t		len;				// total length
	uint16_t		id;					// identification
	uint16_t		off;				// fragment offset field
	uint8_t			ttl;				// time to live
	uint8_t			p;					// protocol
	uint16_t		cksum;				// checksum
	struct in_addr	src, dst;			// source and dest address
} __packed __aligned(4);


// void ip_rx(const char * const);
struct nm_desc;
struct netmap_ring;

void ip_tx(const struct nm_desc * const nm, struct netmap_ring *ring);
void ip_rx(const struct nm_desc * const nm, struct netmap_ring *ring, const uint16_t offset);

// this is defined in in_chksum.c, which is the FreeBSD checksum code
uint16_t in_cksum(void *m, uint16_t len);

#endif
