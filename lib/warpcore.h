#pragma once

#include <sys/queue.h>


/// The I/O vector structure that warpcore uses at the center of its API. It is
/// mostly a pointer to the first UDP payload byte contained in a netmap packet
/// buffer, together with some associated meta data.
///
/// The meta data consists of the length of the payload data, the sender IPv4
/// address and port number, the DSCP and ECN bits associated with the IPv4
/// packet in which the payload arrived, and the netmap arrival timestamp.
///
/// The w_iov structure also contains a pointer to the next I/O vector, which
/// can be used to chain together longer data items for use with w_rx() and
/// w_tx().
///
struct w_iov {
    void * buf;               ///< Start of payload data.
    STAILQ_ENTRY(w_iov) next; ///< Next w_iov.
    uint32_t idx;             ///< Index of netmap buffer. (Internal use.)
    uint16_t len;             ///< Length of payload data.
    uint16_t sport;           ///< Sender source port. Only valid on RX.
    uint32_t src;             ///< Sender IPv4 address. (Only valid on RX.)

    /// DSCP + ECN of the received IPv4 packet on RX, DSCP + ECN to use for the
    /// to-be-transmitted IPv4 packet on TX.
    uint8_t flags;

    /// @cond
    uint8_t _unused[3]; ///< @internal Padding.
    /// @endcond

    struct timeval ts; ///< Receive time of the data. Only valid on RX.
};

#define IP_P_ICMP 1 ///< IP protocol number for ICMP
#define IP_P_UDP 17 ///< IP protocol number for UDP


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
