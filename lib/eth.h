#pragma once

#include <stdbool.h>
#include <stdint.h>


#define ETH_ADDR_LEN 6 ///< MAC address length in bytes. Six.

/// Length of the string representation of an MAC address. The format we use
/// is "xx:xx:xx:xx:xx:xx\0" and includes a final zero byte.
#define ETH_ADDR_STRLEN ETH_ADDR_LEN * 3

#define ETH_TYPE_IP htons(0x0800)  ///< EtherType for IPv4.
#define ETH_TYPE_ARP htons(0x0806) ///< EtherType for ARP.

#define ETH_BCAST "\xff\xff\xff\xff\xff\xff" ///< Ethernet broadcast address.

/// Check whether MAC address @p e is all zero.
#define IS_ZERO(e) ((e[0] | e[1] | e[2] | e[3] | e[4] | e[5]) == 0)

/// An [Ethernet II MAC
/// header](https://en.wikipedia.org/wiki/Ethernet_frame#Ethernet_II).
///
struct eth_hdr {
    uint8_t dst[ETH_ADDR_LEN]; ///< Destination MAC address.
    uint8_t src[ETH_ADDR_LEN]; ///< Source MAC address.
    uint16_t type;             ///< EtherType of the payload data.
};


struct warpcore;
struct w_iov;

/// Return a pointer to the first data byte inside the Ethernet frame in @p buf.
///
/// @param      buf   The buffer to find data in.
///
/// @return     Pointer to the first data byte inside @p buf.
///
#define eth_data(buf) (void *)((char *)(buf) + sizeof(struct eth_hdr))


extern void
eth_tx_rx_cur(struct warpcore * w, void * const buf, const uint16_t len);

extern void eth_rx(struct warpcore * const w, void * const buf);

extern bool
eth_tx(struct warpcore * const w, struct w_iov * const v, const uint16_t len);
