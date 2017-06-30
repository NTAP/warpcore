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

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <sys/time.h>


// Add some defines that FreeBSD has in cdefs.h which tree.h and queue.h
// require.

#ifdef __linux__
typedef uintptr_t __uintptr_t;
#endif

#ifndef __offsetof
#define __offsetof(type, field) __builtin_offsetof(type, field)
#endif

#ifndef __DEQUALIFY
#define __DEQUALIFY(type, var) ((type)(__uintptr_t)(const volatile void *)(var))
#endif

#ifndef __containerof
#define __containerof(x, s, m)                                                 \
    ({                                                                         \
        const volatile __typeof(((s *)0)->m) * __x = (x);                      \
        __DEQUALIFY(s *, (const volatile char *)__x - __offsetof(s, m));       \
    })
#endif


#include <warpcore/config.h> // IWYU pragma: export
#include <warpcore/plat.h>   // IWYU pragma: export
#include <warpcore/queue.h>  // IWYU pragma: export
#include <warpcore/tree.h>   // IWYU pragma: export
#include <warpcore/util.h>   // IWYU pragma: export


struct w_engine;

/// A chain of w_sock socket.
///
SLIST_HEAD(w_sock_slist, w_sock);


/// Initializer for struct w_sock_slist.
///
/// @param      l     A struct w_sock_slist.
///
/// @return     Empty w_sock_slist, to be assigned to @p l.
///
#define w_sock_slist_initializer(l) SLIST_HEAD_INITIALIZER(l)


/// A tail queue of w_iov I/O vectors. Also contains a counter that (on TX)
/// tracks how many w_iovs have not yet been transmitted by the NIC.
///
struct w_iov_stailq {
    STAILQ_HEAD(, w_iov); ///< Head of the w_iov tail queue.
    uint32_t tx_pending; ///< Counter of untransmitted w_iovs. Only valid on TX.
};


/// Initializer for struct w_iov_stailq.
///
/// @param      q     A struct w_iov_stailq.
///
/// @return     Empty w_iov_stailq, to be assigned to @p q.
///
#define w_iov_stailq_initializer(q)                                            \
    {                                                                          \
        STAILQ_HEAD_INITIALIZER(q), 0                                          \
    }


/// Do not compute a UDP checksum for outgoing packets. Has no effect for the
/// socket engine.
///
#define W_ZERO_CHKSUM 1


/// A warpcore socket.
///
struct w_sock {
    /// Pointer back to the warpcore instance associated with this w_sock.
    ///
    struct w_engine * w;
    STAILQ_HEAD(, w_iov) iv;  ///< Tail queue containing incoming unread data.
    SPLAY_ENTRY(w_sock) next; ///< Next socket associated with this engine.
    /// The template header to be used for outbound packets on this
    /// w_sock.
    struct w_hdr * hdr;
    SLIST_ENTRY(w_sock) next_rx; ///< Next socket with unread data.
    uint8_t flags;

    int fd; ///< Socket descriptor underlying the engine.
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
    uint8_t * buf;            ///< Start of payload data.
    STAILQ_ENTRY(w_iov) next; ///< Next w_iov in a w_iov_stailq.
    uint32_t idx;             ///< Index of buffer, starting at zero.
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

    ///< Pointer to the w_iov_stailq this w_iov resides in. Only valid on TX.
    struct w_iov_stailq * o;
};


extern struct w_engine * __attribute__((nonnull))
w_init(const char * const ifname, const uint32_t rip, const uint32_t nbufs);

extern void __attribute__((nonnull)) w_cleanup(struct w_engine * const w);

extern struct w_sock * __attribute__((nonnull))
w_bind(struct w_engine * const w, const uint16_t port, const uint8_t flags);

extern void __attribute__((nonnull))
w_connect(struct w_sock * const s, const uint32_t ip, const uint16_t port);

extern void __attribute__((nonnull)) w_disconnect(struct w_sock * const s);

extern void __attribute__((nonnull)) w_close(struct w_sock * const s);

extern void __attribute__((nonnull)) w_alloc_len(struct w_engine * const w,
                                                 struct w_iov_stailq * const q,
                                                 const uint32_t len,
                                                 const uint16_t off);

extern void __attribute__((nonnull)) w_alloc_cnt(struct w_engine * const w,
                                                 struct w_iov_stailq * const q,
                                                 const uint32_t count,
                                                 const uint16_t off);

extern struct w_iov * __attribute__((nonnull))
w_alloc_iov(struct w_engine * const w, const uint16_t off);

extern void __attribute__((nonnull))
w_tx(const struct w_sock * const s, struct w_iov_stailq * const o);

extern void __attribute__((nonnull))
w_free(struct w_engine * const w, struct w_iov_stailq * const q);

extern void __attribute__((nonnull))
w_free_iov(struct w_engine * const w, struct w_iov * const v);

extern uint32_t w_iov_stailq_len(const struct w_iov_stailq * const q);

extern uint32_t w_iov_stailq_cnt(const struct w_iov_stailq * const q);

extern int __attribute__((nonnull)) w_fd(const struct w_sock * const s);

extern void __attribute__((nonnull))
w_rx(struct w_sock * const s, struct w_iov_stailq * const i);

extern void __attribute__((nonnull)) w_nic_tx(struct w_engine * const w);

extern bool __attribute__((nonnull))
w_nic_rx(struct w_engine * const w, const int32_t msec);

extern uint32_t __attribute__((nonnull))
w_rx_ready(struct w_engine * const w, struct w_sock_slist * sl);

extern uint16_t __attribute__((nonnull))
w_iov_max_len(const struct w_engine * const w, const struct w_iov * const v);

extern bool __attribute__((nonnull)) w_connected(const struct w_sock * const s);


/// Return the number of w_iov structs in @p q that are still waiting for
/// transmission. Only valid after w_tx() has been called on @p p.
///
/// @param      q     A tail queue of w_iov structs.
///
/// @return     Number of w_iov structs not yet transmitted.
///
#define w_tx_pending(q) (q)->tx_pending


/// Return warpcore engine serving w_sock @p s.
///
/// @param[in]  s     A w_sock.
///
/// @return     The warpcore engine for w_sock @p s.
///
#define w_engine(s) (s)->w

#ifdef __cplusplus
}
#endif
