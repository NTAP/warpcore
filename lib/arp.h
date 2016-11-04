#ifndef _arp_h_
#define _arp_h_

#include <stdint.h>

#include "eth.h"

#define ARP_HRD_ETHER 1 // Ethernet hardware format

#define ARP_OP_REQUEST 1 // request to resolve address
#define ARP_OP_REPLY 2   // response to request

struct arp_hdr {
    uint16_t hrd; // format of hardware address
    uint16_t pro; // format of protocol address
    uint8_t hln;  // length of hardware address
    uint8_t pln;  // length of protocol address
    uint16_t op;  // operation

    // remainder of this struct is
    // only correct for Ethernet/IP:

    uint8_t sha[ETH_ADDR_LEN]; // sender hardware address
    uint32_t spa;              // sender protocol address
    uint8_t tha[ETH_ADDR_LEN]; // target hardware address
    uint32_t tpa;              // target protocol address
} __packed;


struct warpcore;

// see arp.c for documentation of functions

extern void arp_rx(struct warpcore * w, char * const buf);

extern void arp_who_has(struct warpcore * const w, const uint32_t dip);

#endif
