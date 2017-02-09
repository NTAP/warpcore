// Copyright (c) 2014-2017, NetApp, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#pragma once

#include <stdint.h>
#include <sys/queue.h>
#include <sys/time.h>

// IWYU pragma: begin_exports
#include <plat.h>
#include <util.h>
// IWYU pragma: end_exports

struct warpcore;


STAILQ_HEAD(w_iov_chain, w_iov);
SLIST_HEAD(w_sock_chain, w_sock);


/// Do not compute a UDP checksum for outgoing packets. Has no effect for the
/// shim backend.
///
#define W_ZERO_CHKSUM 1


/// A warpcore socket.
///
struct w_sock {
    /// Pointer back to the warpcore instance associated with this w_sock.
    ///
    struct warpcore * w;
    struct w_iov_chain * iv;  ///< w_iov chain containing incoming unread data.
    SLIST_ENTRY(w_sock) next; ///< Next socket associated with this engine.
    /// The template header to be used for outbound packets on this
    /// w_sock.
    struct w_hdr * hdr;
    SLIST_ENTRY(w_sock) next_rx; ///< Next socket with unread data.
    uint8_t flags;
    /// @cond
    uint8_t _unused[3]; ///< @internal Padding.
    /// @endcond
#ifndef WITH_NETMAP
    int fd; ///< Socket descriptor underlying the engine, if the shim is in use.
#endif
};


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
    STAILQ_ENTRY(w_iov) next; ///< Next w_iov in a w_iov_chain.
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

#ifdef WITH_NETMAP
    struct netmap_slot * slot; ///< During TX, pointer to TX slot.
#endif

    SLIST_ENTRY(w_iov) next_tx; ///< Next w_iov during TX pending.

    struct timeval ts; ///< Receive time of the data. Only valid on RX.
};


extern struct warpcore * __attribute__((nonnull))
w_init(const char * const ifname, const uint32_t rip);

extern void __attribute__((nonnull)) w_cleanup(struct warpcore * const w);

extern struct w_sock * __attribute__((nonnull))
w_bind(struct warpcore * const w, const uint16_t port, const uint8_t flags);

extern void __attribute__((nonnull))
w_connect(struct w_sock * const s, const uint32_t ip, const uint16_t port);

extern void __attribute__((nonnull)) w_disconnect(struct w_sock * const s);

extern void __attribute__((nonnull)) w_close(struct w_sock * const s);

extern struct w_iov_chain * __attribute__((nonnull))
w_alloc(struct warpcore * const w, const uint32_t len, const uint16_t off);

extern void __attribute__((nonnull))
w_tx(const struct w_sock * const s, struct w_iov_chain * const c);

extern void __attribute__((nonnull))
w_free(struct warpcore * const w, struct w_iov_chain * const c);

extern uint32_t w_iov_chain_len(const struct w_iov_chain * const c,
                                const uint16_t off);

extern uint32_t w_iov_chain_cnt(const struct w_iov_chain * const c);

extern int __attribute__((nonnull)) w_fd(struct w_sock * const s);

extern struct w_iov_chain * __attribute__((nonnull))
w_rx(struct w_sock * const s);

extern void __attribute__((nonnull)) w_nic_tx(struct warpcore * const w);

extern void __attribute__((nonnull)) w_nic_rx(struct warpcore * const w);

extern struct warpcore * __attribute__((nonnull))
w_engine(const struct w_sock * const s);

extern struct w_sock_chain * __attribute__((nonnull))
w_rx_ready(const struct warpcore * w);

extern uint16_t __attribute__((nonnull))
w_iov_max_len(const struct warpcore * const w, const struct w_iov * const v);
