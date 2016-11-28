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
struct w_iov;

extern void __attribute__((nonnull))
udp_rx(struct warpcore * const w, void * const buf, const uint32_t src);

extern void __attribute__((nonnull))
udp_tx(const struct w_sock * const s, struct w_iov * const v);
