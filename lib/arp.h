#pragma once

#include "eth.h"

/// A representation of an ARP header; see
/// [RFC826](https://tools.ietf.org/html/rfc826).
/// This representation is limited to making requests for IPv4 over Ethernet,
/// which is sufficient for warpcore.
///
struct arp_hdr {
    /// Format of the hardware address. Will always be ARP_HDR_ETHER for
    /// Ethernet.
    uint16_t hrd;

    /// Format of the protocol address. Will always be ETH_TYPE_IP for IPv4
    /// in warpcore.
    uint16_t pro;

    /// Length of the hardware address. Will always be ETH_ADDR_LEN for
    /// Ethernet.
    uint8_t hln;

    /// Length of the protocol address. Will always be IP_ADDR_LEN for IPv4
    /// in warpcore.
    uint8_t pln;

    /// ARP operation. Either ARP_OP_REQUEST or ARP_OP_REPLY.
    uint16_t op;

    /// The sender hardware (i.e., Ethernet) address of the ARP operation.
    ///
    uint8_t sha[ETH_ADDR_LEN];

    /// The sender protocol (i.e., IPv4) address of the ARP operation.
    ///
    uint32_t spa __attribute__((packed));

    /// The target hardware (i.e., Ethernet) address of the ARP operation.
    ///
    uint8_t tha[ETH_ADDR_LEN];

    /// The target protocol (i.e., IPv4) address of the ARP operation.
    ///
    uint32_t tpa;
};


#define ARP_HRD_ETHER 1  ///< Ethernet hardware address format.
#define ARP_OP_REQUEST 1 ///< ARP operation, request to resolve address.
#define ARP_OP_REPLY 2   ///< ARP operation, response to request.


extern void __attribute__((nonnull))
arp_rx(struct warpcore * w, void * const buf);

extern uint8_t * __attribute__((nonnull))
arp_who_has(struct warpcore * const w, const uint32_t dip);

extern void __attribute__((nonnull))
free_arp_cache(struct warpcore * const w);
