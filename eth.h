#ifndef _eth_h_
#define _eth_h_

#include <stdint.h>	// uint8_t, etc.

static const uint_fast8_t ETH_ADDR_LEN = 6; // length of an Ethernet address

static const uint_fast16_t ETH_TYPE_IP = 0x0800;	// IP protocol
static const uint_fast16_t ETH_TYPE_ARP = 0x0806;	// Address resolution protocol

struct eth_hdr {
	uint8_t dst[ETH_ADDR_LEN];
	uint8_t src[ETH_ADDR_LEN];
	uint16_t type;
} __packed;

struct warpcore;

extern void eth_tx(const struct warpcore * const w, const char * const buf,
	const uint_fast16_t len);

extern void eth_rx(const struct warpcore * const w, char * const buf);

#endif
