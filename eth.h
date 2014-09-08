#ifndef _eth_h_
#define _eth_h_

#define ETH_ADDR_LEN	6	// length of an address

#define ETH_TYPE_IP	0x0800	// IP protocol
#define ETH_TYPE_ARP	0x0806	// ARP protocol

#define ETH_BCAST	"\xff\xff\xff\xff\xff\xff"

struct eth_hdr {
	uint8_t		dst[ETH_ADDR_LEN];
	uint8_t		src[ETH_ADDR_LEN];
	uint16_t	type;
} __attribute__ ((__packed__));

struct warpcore;

extern void eth_tx(struct warpcore * w, const char * const buf,
		   const uint_fast16_t len);

extern void eth_rx(struct warpcore * w, char * const buf);

#endif
