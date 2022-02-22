// SPDX-License-Identifier: BSD-2-Clause
//
// Copyright (c) 2014-2022, NetApp, Inc.
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

#include <sys/socket.h>

#include <warpcore/warpcore.h>


extern struct eth_addr __attribute__((nonnull))
who_has(struct w_engine * const w, const struct w_addr * const addr);

extern void __attribute__((nonnull)) free_neighbor(struct w_engine * const w);

extern void __attribute__((nonnull))
neighbor_update(struct w_engine * const w,
                const struct w_addr * const addr,
                const struct eth_addr mac);


static inline khint_t __attribute__((nonnull))
w_addr_hash(const struct w_addr * const addr)
{
    // only hash part of the struct and rely on w_addr_hash for comparison
    return addr->af == AF_INET ? fnv1a_32(&addr->ip4, IP4_LEN)
                               : fnv1a_32(addr->ip6, IP6_LEN);
}


KHASH_INIT(neighbor,
           const struct w_addr *,
           struct eth_addr,
           1,
           w_addr_hash,
           w_addr_cmp)
