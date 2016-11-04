#pragma once

#include <stdint.h>

struct udp_hdr {
    uint16_t sport; // source port
    uint16_t dport; // destination port
    uint16_t len;   // udp length
    uint16_t cksum; // udp checksum
} __aligned(4);


struct warpcore;
struct w_sock;
struct w_iov;

extern void
udp_rx(struct warpcore * const w, char * const buf, const uint32_t src);

extern void udp_tx(struct w_sock * const s);
