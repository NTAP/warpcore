#pragma once

#include <stdbool.h>
#include <stdint.h>


#define ETH_ADDR_LEN 6 // Ethernet addresses are six bytes long
#define ETH_ADDR_STRLEN ETH_ADDR_LEN * 3 // xx:xx:xx:xx:xx:xx\0

#define ETH_TYPE_IP htons(0x0800)  // IP protocol Ethertype
#define ETH_TYPE_ARP htons(0x0806) // ARP protocol Ethertype

#define ETH_BCAST "\xff\xff\xff\xff\xff\xff"
#define IS_ZERO(e) ((e[0] | e[1] | e[2] | e[3] | e[4] | e[5]) == 0)


struct eth_hdr {
    uint8_t dst[ETH_ADDR_LEN];
    uint8_t src[ETH_ADDR_LEN];
    uint16_t type;
};


struct warpcore;
struct w_iov;

#define eth_data(buf) (void *)((char *)(buf) + sizeof(struct eth_hdr))


// see eth.c for documentation of functions
extern void
eth_tx_rx_cur(struct warpcore * w, void * const buf, const uint16_t len);

extern void eth_rx(struct warpcore * const w, void * const buf);

extern bool
eth_tx(struct warpcore * const w, struct w_iov * const v, const uint16_t len);
