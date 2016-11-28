#pragma once

#include <stdint.h>


#define ICMP_TYPE_ECHOREPLY 0 ///< ICMP echo reply type.
#define ICMP_TYPE_UNREACH 3   ///< ICMP unreachable type.
#define ICMP_TYPE_ECHO 8      ///< ICMP echo type.

#define ICMP_UNREACH_PROTOCOL 2 ///< For ICMP_TYPE_UNREACH, bad protocol code.
#define ICMP_UNREACH_PORT 3     ///< For ICMP_TYPE_UNREACH, bad port code.


/// An ICMP header representation; see
/// [RFC792](https://tools.ietf.org/html/rfc792).
///
struct icmp_hdr {
    uint8_t type;   ///< Type of ICMP message.
    uint8_t code;   ///< Code of the ICMP type.
    uint16_t cksum; ///< Ones' complement header checksum.
};


struct warpcore;

extern void __attribute__((nonnull))
icmp_tx_unreach(struct warpcore * w, const uint8_t code, void * const buf);

extern void __attribute__((nonnull))
icmp_rx(struct warpcore * w, void * const buf);
