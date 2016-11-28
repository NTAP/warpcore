#pragma once

#include <sys/queue.h>

#include <plat.h>
#include <util.h>

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

    /// Sender port on RX. Destination port on TX on a disconnected
    /// w_sock. Ignored on TX on a connected w_sock.
    uint16_t port;

    /// Sender IPv4 address on RX. Destination IPv4 address on TX on a
    /// disconnected w_sock. Ignored on TX on a connected w_sock.
    uint32_t ip;

    /// DSCP + ECN of the received IPv4 packet on RX, DSCP + ECN to use for the
    /// to-be-transmitted IPv4 packet on TX.
    uint8_t flags;

    /// @cond
    uint8_t _unused[3]; ///< @internal Padding.
    /// @endcond

    struct timeval ts; ///< Receive time of the data. Only valid on RX.
};


extern struct warpcore * __attribute__((nonnull))
w_init(const char * const ifname, const uint32_t rip);

extern void __attribute__((nonnull)) w_cleanup(struct warpcore * const w);

extern struct w_sock * __attribute__((nonnull))
w_bind(struct warpcore * const w, const uint16_t port);

extern void __attribute__((nonnull))
w_connect(struct w_sock * const s, const uint32_t ip, const uint16_t port);

extern void __attribute__((nonnull)) w_disconnect(struct w_sock * const s);

extern void __attribute__((nonnull)) w_close(struct w_sock * const s);

extern struct w_iov * __attribute__((nonnull))
w_alloc(struct warpcore * const w, const uint32_t len);

extern void __attribute__((nonnull))
w_tx(const struct w_sock * const s, struct w_iov * const v);

extern void w_free(struct warpcore * const w, struct w_iov * v);

extern uint32_t w_iov_len(const struct w_iov * v);

extern int __attribute__((nonnull)) w_fd(struct w_sock * const s);

extern struct w_iov * __attribute__((nonnull)) w_rx(struct w_sock * const s);

extern void __attribute__((nonnull)) w_nic_tx(const struct warpcore * const w);

extern void __attribute__((nonnull)) w_nic_rx(const struct warpcore * const w);
