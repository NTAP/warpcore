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

#endif
