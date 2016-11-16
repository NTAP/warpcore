#pragma once

#include <stdint.h>

/// A representation of a UDP header; see
/// [RFC768](https://tools.ietf.org/html/rfc768).
///
struct udp_hdr {
    uint16_t sport; ///< Source port.
    uint16_t dport; ///< Destination port.
    uint16_t len;   ///< UDP length (header + data).
    uint16_t cksum; ///< UDP checksum.
};


struct warpcore;
struct w_sock;

extern void
udp_rx(struct warpcore * const w, void * const buf, const uint32_t src);

extern void udp_tx(struct w_sock * const s);
