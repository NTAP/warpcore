#ifndef _eth_h_
#define _eth_h_

#include <stdint.h>	// uint8_t, etc.


static const uint_fast8_t ETH_ADDR_LEN = 6; // length of an Ethernet address

struct eth_hdr {
	uint8_t dst[ETH_ADDR_LEN];
	uint8_t src[ETH_ADDR_LEN];
	uint16_t type;
} __packed;


// void eth_rx(const char * const buf);

struct nm_desc;
struct netmap_ring;

void eth_tx(const struct nm_desc * const nm, const char * const buf);
void eth_rx(const struct nm_desc * const nm, const char * const buf);

#endif
