// SPDX-License-Identifier: BSD-2-Clause
//
// Copyright (c) 2014-2019, NetApp, Inc.
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

#ifdef WITH_NETMAP
// IWYU pragma: no_include <net/netmap.h>
#include <net/netmap_user.h> // IWYU pragma: keep
#endif

#include <warpcore/warpcore.h>

#if defined(HAVE_KQUEUE)
#include <sys/event.h>
#include <time.h>
#elif defined(HAVE_EPOLL)
#include <sys/epoll.h>
#elif !defined(PARTICLE) && !defined(RIOT_VERSION)
#include <poll.h>
#endif

#ifdef WITH_NETMAP
#include "arp.h"
#include "neighbor.h"
#include "udp.h"
#endif


struct w_backend {
#ifdef WITH_NETMAP
    int fd;                     ///< Netmap file descriptor.
    uint32_t cur_txr;           ///< Index of the TX ring currently active.
    struct netmap_if * nif;     ///< Netmap interface.
    struct nmreq * req;         ///< Netmap request structure.
    khash_t(neighbor) neighbor; ///< The ARP cache.
    uint32_t * tail;            ///< TX ring tails after last NIOCTXSYNC call.
    struct w_iov *** slot_buf;  ///< For each ring slot, a pointer to its w_iov.
#else
#if defined(HAVE_KQUEUE)
    struct kevent ev[64]; // XXX arbitrary value
    int kq;
#elif defined(HAVE_EPOLL)
    int ep;
    struct epoll_event ev[64]; // XXX arbitrary value
#else
    struct pollfd * fds;
    struct w_sock ** socks;
#endif
    int n;
#ifndef HAVE_KQUEUE
    /// @cond
    uint8_t _unused[4]; ///< @internal Padding.
    /// @endcond
#endif
#endif
};


static inline bool __attribute__((nonnull))
is_pipe(const struct w_engine * const w
#ifndef WITH_NETMAP
        __attribute__((unused))
#endif
)
{
#ifdef WITH_NETMAP
    return unlikely((w->b->req->nr_flags & NR_REG_MASK) == NR_REG_PIPE_MASTER ||
                    (w->b->req->nr_flags & NR_REG_MASK) == NR_REG_PIPE_SLAVE);
#else
    return false;
#endif
}


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
static inline uint8_t * __attribute__((nonnull))
idx_to_buf(const struct w_engine * const w, const uint32_t i)
{
#ifdef WITH_NETMAP
    return (uint8_t *)NETMAP_BUF(NETMAP_TXRING(w->b->nif, 0), i);
#else
    return (uint8_t *)w->mem + ((intptr_t)i * w->mtu);
#endif
}


static inline void set_ip(struct w_addr * const wa,
                          const struct sockaddr * const sa)
{
    if (sa->sa_family == AF_INET) {
        wa->af = AF_IP4;
        memcpy(&wa->ip4,
               &((const struct sockaddr_in *)(const void *)sa)->sin_addr,
               sizeof(wa->ip4));
    } else {
        wa->af = AF_IP6;
        memcpy(&wa->ip6,
               &((const struct sockaddr_in6 *)(const void *)sa)->sin6_addr,
               sizeof(wa->ip6));
    }
}


#define sa_port(s)                                                             \
    _Pragma("clang diagnostic push")                                           \
                _Pragma("clang diagnostic ignored \"-Wcast-align\"")(          \
                    (const struct sockaddr *)(s))                              \
                    ->sa_family == AF_INET                                     \
        ? ((const struct sockaddr_in *)(s))->sin_port                          \
        : ((const struct sockaddr_in6 *)(s))                                   \
              ->sin6_port _Pragma("clang diagnostic pop")


/// Global list of initialized warpcore engines.
///
extern sl_head(w_engines, w_engine) engines;


extern void __attribute__((nonnull))
init_iov(struct w_engine * const w, struct w_iov * const v, const uint32_t idx);

extern struct w_iov * __attribute__((nonnull))
w_alloc_iov_base(struct w_engine * const w);

extern int __attribute__((nonnull(1)))
backend_bind(struct w_sock * const s, const struct w_sockopt * const opt);

extern void __attribute__((nonnull)) backend_close(struct w_sock * const s);

extern int __attribute__((nonnull)) backend_connect(struct w_sock * const s);

extern void __attribute__((nonnull)) backend_init(struct w_engine * const w,
                                                  const uint32_t nbufs,
                                                  const bool is_lo,
                                                  const bool is_left);

extern void __attribute__((nonnull)) backend_cleanup(struct w_engine * const w);
