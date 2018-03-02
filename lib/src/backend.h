// SPDX-License-Identifier: BSD-2-Clause
//
// Copyright (c) 2014-2018, NetApp, Inc.
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

#ifdef HAVE_ASAN
#include <sanitizer/asan_interface.h>
#else
#define ASAN_POISON_MEMORY_REGION(x, y)
#define ASAN_UNPOISON_MEMORY_REGION(x, y)
#endif

#include <warpcore/warpcore.h>

#include "arp.h"


struct w_backend {
#ifdef WITH_NETMAP
    int fd;                     ///< Netmap file descriptor.
    uint32_t cur_txr;           ///< Index of the TX ring currently active.
    struct netmap_if * nif;     ///< Netmap interface.
    struct nmreq * req;         ///< Netmap request structure.
    struct arp_cache arp_cache; ///< The ARP cache.
    uint32_t * tail;            ///< TX ring tails after last NIOCTXSYNC call.
    struct w_iov *** slot_buf;  ///< For each ring slot, a pointer to its w_iov.
    uint16_t next_eph;          ///< State for random port number generation.
    /// @cond
    uint8_t _unused[6]; ///< @internal Padding.
    /// @endcond
#else
#if defined(HAVE_KQUEUE)
    int kq;
#elif defined(HAVE_EPOLL)
    int ep;
#else
    /// @cond
    uint8_t _unused_2[4];
    /// @endcond
#endif
    /// @cond
    uint8_t _unused_2[4];
    /// @endcond
#endif
};


#define is_pipe(w)                                                             \
    unlikely(((w)->b->req->nr_flags & NR_REG_MASK) == NR_REG_PIPE_MASTER ||    \
             ((w)->b->req->nr_flags & NR_REG_MASK) == NR_REG_PIPE_SLAVE)


/// For a given buffer index, get a pointer to its beginning.
///
/// Since netmap uses a macro for this, we also need to use a macro for the
/// socket backend.
///
/// @param      w     Backend engine.
/// @param      i     Buffer index.
///
/// @return     Memory region associated with buffer @p i.
///
#ifdef WITH_NETMAP
#define IDX2BUF(w, i)                                                          \
    ((uint8_t *)NETMAP_BUF(NETMAP_TXRING((w)->b->nif, 0), (i)))
#else
#define IDX2BUF(w, i) (((uint8_t *)w->mem + (i * w->mtu)))
#endif


#include "eth.h"
#include "ip.h"
#include "udp.h"


/// A warpcore template packet header structure.
///
struct w_hdr {
    struct eth_hdr eth;                       ///< Ethernet header.
    struct ip_hdr ip __attribute__((packed)); ///< IPv4 header.
    struct udp_hdr udp;                       ///< UDP header.
};


static inline int w_sock_cmp(const struct w_sock * const a,
                             const struct w_sock * const b)
{
    const uint32_t ap = ((uint32_t)a->hdr->udp.sport << 16) + a->hdr->udp.dport;
    const uint32_t bp = ((uint32_t)b->hdr->udp.sport << 16) + b->hdr->udp.dport;
    return (ap > bp) - (ap < bp);
}


SPLAY_PROTOTYPE(sock, w_sock, next, w_sock_cmp)


/// Global list of initialized warpcore engines.
///
extern sl_head(w_engines, w_engine) engines;


/// Compute the IPv4 broadcast address for the given IPv4 address and netmask.
///
/// @param      ip    The IPv4 address to compute the broadcast address for.
/// @param      mask  The netmask associated with @p ip.
///
/// @return     The IPv4 broadcast address associated with @p ip and @p mask.
///
#define mk_bcast(ip, mask) ((ip) | (~mask))


/// The IPv4 network prefix for the given IPv4 address and netmask.
///
/// @param      ip    The IPv4 address to compute the prefix for.
/// @param      mask  The netmask associated with @p ip.
///
/// @return     The IPv4 prefix associated with @p ip and @p mask.
///
#define mk_net(ip, mask) ((ip) & (mask))


#define init_iov(ww, v)                                                        \
    do {                                                                       \
        (v)->w = (ww);                                                         \
        (v)->buf = IDX2BUF((ww), (v)->idx);                                    \
        (v)->len = (ww)->mtu;                                                  \
        (v)->o = 0;                                                            \
        sq_next((v), next) = 0;                                                \
    } while (0)


#define w_alloc_iov_base(w)                                                    \
    __extension__({                                                            \
        struct w_iov * const _v = sq_first(&(w)->iov);                         \
        if (likely(_v)) {                                                      \
            sq_remove_head(&(w)->iov, next);                                   \
            init_iov((w), _v);                                                 \
            ASAN_UNPOISON_MEMORY_REGION(IDX2BUF((w), _v->idx), _v->len);       \
        }                                                                      \
        _v;                                                                    \
    })


extern void __attribute__((nonnull)) backend_bind(struct w_sock * const s);

extern void __attribute__((nonnull)) backend_close(struct w_sock * const s);

extern void __attribute__((nonnull)) backend_connect(struct w_sock * const s);

extern void __attribute__((nonnull)) backend_init(struct w_engine * const w,
                                                  const uint32_t nbufs,
                                                  const bool is_lo,
                                                  const bool is_left);

extern void __attribute__((nonnull)) backend_cleanup(struct w_engine * const w);
