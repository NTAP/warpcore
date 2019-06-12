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

#include <warpcore/warpcore.h>

#include "backend.h"
#include "ip.h"
#include "udp.h"


/// The backend name.
///
static char backend_name[] = "particle";


/// Initialize the warpcore socket backend for engine @p w. Sets up the extra
/// buffers.
///
/// @param      w        Backend engine.
/// @param[in]  nbufs    Number of packet buffers to allocate.
/// @param[in]  is_lo    Unused.
/// @param[in]  is_left  Unused.
///
void backend_init(struct w_engine * const w,
                  const uint32_t nbufs,
                  const bool is_lo __attribute__((unused)),
                  const bool is_left __attribute__((unused)))
{
    // lower the MTU to account for IP and UPD headers
    w->mtu -= sizeof(struct ip_hdr) + sizeof(struct udp_hdr);

    ensure((w->mem = calloc(nbufs, w->mtu)) != 0,
           "cannot alloc %u * %u buf mem", nbufs, w->mtu);
    ensure((w->bufs = calloc(nbufs, sizeof(*w->bufs))) != 0,
           "cannot alloc bufs");
    w->backend_name = backend_name;

    for (uint32_t i = 0; i < nbufs; i++) {
        w->bufs[i].idx = i;
        init_iov(w, &w->bufs[i]);
        sq_insert_head(&w->iov, &w->bufs[i], next);
        ASAN_POISON_MEMORY_REGION(w->bufs[i].buf, w->mtu);
    }
}
