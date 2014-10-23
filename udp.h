#ifndef _udp_h_
#define _udp_h_

#include <stdint.h>
#include <stdbool.h>


struct udp_hdr {
	uint16_t sport;		// source port
	uint16_t dport;		// destination port
	uint16_t len;		// udp length
	uint16_t cksum;		// udp checksum
} __packed __aligned(4);


struct warpcore;
struct w_sock;
struct w_iov;

#define udp_hdr_offset(x) (struct udp_hdr *)\
	(x + sizeof(struct eth_hdr) + \
	 (((struct ip_hdr *)(x + sizeof(struct eth_hdr)))->hl*4))

extern void
udp_rx(struct warpcore * const w, char * const buf, const uint16_t off,
       const uint32_t src);

extern void
udp_tx(struct w_sock * const s);

#endif
