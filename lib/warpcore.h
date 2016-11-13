#pragma once

#include <sys/queue.h>


struct w_iov {
    void * buf;               // start of user data (inside buffer)
    STAILQ_ENTRY(w_iov) next; // next iov
    uint32_t idx;             // index of netmap buffer
    uint16_t len;             // length of user data (inside buffer)
    uint16_t sport;           // sender port (only valid on rx)
    uint32_t src;             // sender IP address (only valid on rx)
    uint8_t _unused[4];
};

#define IP_P_ICMP 1 // IP protocol number for ICMP
#define IP_P_UDP 17 // IP protocol number for UDP


extern struct warpcore * w_init(const char * const ifname, const uint32_t rip);

extern void w_init_common(void); // TODO deprecated

extern void w_cleanup(struct warpcore * const w);

extern struct w_sock *
w_bind(struct warpcore * const w, const uint8_t p, const uint16_t port);

extern void
w_connect(struct w_sock * const s, const uint32_t ip, const uint16_t port);

extern void w_close(struct w_sock * const s);

extern struct w_iov * w_tx_alloc(struct w_sock * const s, const uint32_t len);

extern int w_fd(struct w_sock * const s);

extern void w_rx_done(struct w_sock * const s);

extern struct w_iov * w_rx(struct w_sock * const s);

extern void w_kick_tx(const struct warpcore * const w);

extern void w_kick_rx(const struct warpcore * const w);

extern void w_tx(struct w_sock * const s);
