// SPDX-License-Identifier: BSD-2-Clause
//
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

#include <warpcore/config.h> // IWYU pragma: export
#include <warpcore/plat.h>   // IWYU pragma: export
#include <warpcore/queue.h>  // IWYU pragma: export
#include <warpcore/tree.h>   // IWYU pragma: export
#include <warpcore/util.h>   // IWYU pragma: export


/// A tail queue of w_iov I/O vectors. Also contains a counter that (on TX)
/// tracks how many w_iovs have not yet been transmitted by the NIC.
///
struct w_iov_sq {
    sq_head(, w_iov);    ///< Head of the w_iov tail queue.
    uint32_t tx_pending; ///< Counter of untransmitted w_iovs. Only valid on TX.
};


/// Initializer for struct w_iov_sq.
///
/// @param      q     A struct w_iov_sq.
///
/// @return     Empty w_iov_sq, to be assigned to @p q.
///
#define w_iov_sq_initializer(q)                                                \
    {                                                                          \
        sq_head_initializer(q), 0                                              \
    }


splay_head(sock, w_sock);

/// A warpcore backend engine.
///
struct w_engine {
    void * mem;           ///< Pointer to netmap or socket buffer memory region.
    struct w_iov * bufs;  ///< Pointer to w_iov buffers.
    struct w_backend * b; ///< Backend.
    uint32_t ip;          ///< Local IPv4 address used on this interface.
    uint32_t mask;        ///< IPv4 netmask of this interface.
    uint32_t rip;         ///< Our default IPv4 router IP address.
    uint16_t mtu;         ///< MTU of this interface.
    struct ether_addr mac; ///< Local Ethernet MAC address of the interface.
    struct sock sock;      ///< List of open (bound) w_sock sockets.
    struct w_iov_sq iov;   ///< Tail queue of w_iov buffers available.

    sl_entry(w_engine) next;   ///< Pointer to next engine.
    char * ifname;             ///< Name of the interface of this engine.
    const char * backend_name; ///< Name of the backend in @p b.
};


/// A chain of w_sock socket.
///
sl_head(w_sock_slist, w_sock);


/// Initializer for struct w_sock_slist.
///
/// @param      l     A struct w_sock_slist.
///
/// @return     Empty w_sock_slist, to be assigned to @p l.
///
#define w_sock_slist_initializer(l) sl_head_initializer(l)


/// Do not compute a UDP checksum for outgoing packets. Has no effect for the
/// socket engine.
///
#define W_ZERO_CHKSUM 1


/// A warpcore socket.
///
struct w_sock {
    /// Pointer back to the warpcore instance associated with this w_sock.
    struct w_engine * w;

    struct w_iov_sq iv;       ///< Tail queue containing incoming unread data.
    splay_entry(w_sock) next; ///< Next socket associated with this engine.

    /// The template header to be used for outbound packets on this
    /// w_sock.
    struct w_hdr * hdr;

    sl_entry(w_sock) next_rx; ///< Next socket with unread data.
    uint8_t flags;            ///< Socket flags.

    /// @cond
    uint8_t _unused[3]; ///< @internal Padding.
    /// @endcond

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
    uint8_t * buf;        ///< Start of payload data.
    sq_entry(w_iov) next; ///< Next w_iov in a w_iov_sq.
    uint32_t nm_idx;      ///< Index of netmap buffer.
    uint16_t len;         ///< Length of payload data.

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

    ///< Pointer to the w_iov_sq this w_iov resides in. Only valid on TX.
    struct w_iov_sq * o;
};


#define w_iov_idx(w, v) ((w)->bufs - (v))


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
                                                 struct w_iov_sq * const q,
                                                 const uint32_t plen,
                                                 const uint16_t len,
                                                 const uint16_t off);

extern void __attribute__((nonnull)) w_alloc_cnt(struct w_engine * const w,
                                                 struct w_iov_sq * const q,
                                                 const uint32_t count,
                                                 const uint16_t len,
                                                 const uint16_t off);

extern struct w_iov * __attribute__((nonnull))
w_alloc_iov(struct w_engine * const w, const uint16_t len, const uint16_t off);

extern void __attribute__((nonnull))
w_tx(const struct w_sock * const s, struct w_iov_sq * const o);

extern uint32_t w_iov_sq_len(const struct w_iov_sq * const q);

extern int __attribute__((nonnull)) w_fd(const struct w_sock * const s);

extern void __attribute__((nonnull))
w_rx(struct w_sock * const s, struct w_iov_sq * const i);

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


/// Return name of interface associated with warpcore engine. Must not be
/// modified by caller.
///
/// @param      w     Backend engine.
///
/// @return     Interface name.
///
#define w_ifname(w) (w)->ifname


/// Return MTU of w_engine @p w.
///
/// @param[in]  w     Backend engine.
///
/// @return     MTU value in use by engine @p w.
///
#define w_mtu(w) (w)->mtu


/// Return a w_iov tail queue obtained via w_alloc_len(), w_alloc_cnt() or
/// w_rx() back to warpcore.
///
/// @param      w     Backend engine.
/// @param      q     Tail queue of w_iov structs to return.
///
#ifndef NDEBUG
extern void __attribute__((nonnull))
w_free(struct w_engine * const w, struct w_iov_sq * const q);
#else
#define w_free(w, q) sq_concat(&(w)->iov, (q))
#endif

/// Return a single w_iov obtained via w_alloc_len(), w_alloc_cnt() or w_rx()
/// back to warpcore.
///
/// @param      w     Backend engine.
/// @param      v     w_iov struct to return.
///
/// @return     w_iov struct.
///
#ifndef NDEBUG
extern void __attribute__((nonnull))
w_free_iov(struct w_engine * const w, struct w_iov * const v);
#else
#define w_free_iov(w, v) sq_insert_head(&(w)->iov, (v), next)
#endif

/// Return the number of w_iov structures in the w_iov tail queue @p c.
///
/// @param[in]  q     The w_iov tail queue to compute the payload length of.
///
/// @return     Number of w_iov structs in @p q.
///
#define w_iov_sq_cnt(q) sq_len(q)


#ifdef __cplusplus
}
#endif
