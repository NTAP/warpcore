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

#include <stdint.h>

#ifdef __FreeBSD__
#include <netinet/in.h> // IWYU pragma: keep
#include <sys/socket.h> // IWYU pragma: keep
#endif

#include <warpcore/warpcore.h>

#include "backend.h"
#include "common.h"

#define beg(v) IDX2BUF(w, w_iov_idx(v))


int main(void)
{
    init(8192);

    struct w_engine * const w = w_serv;

    struct w_iov * v = w_alloc_iov(w, 0, 0);
    warn(DBG, "base: len %u", v->len);
    ensure(v->len == w->mtu, "base len != %u", w->mtu);

    for (uint16_t x = 0; x <= w->mtu; x += 200) {
        warn(INF, "off %u", x);
        v = w_alloc_iov(w, 0, x);
        ensure(v->len == w->mtu - x, "v len != %u", w->mtu - x);
        ensure(v->buf == beg(v) + x, "start incorrect");
        w_free_iov(v);
    }

    for (uint16_t x = 0; x <= w->mtu; x += 200) {
        warn(INF, "len %u", x);
        v = w_alloc_iov(w, x, 0);
        ensure(v->len == (x == 0 ? w->mtu : x), "v len != %u", x);
        ensure(v->buf == beg(v), "start incorrect");
        w_free_iov(v);
    }

    const uint16_t off = 100;
    for (uint16_t x = 0; x <= w->mtu - off; x += 200) {
        warn(INF, "off %u & len %u", off, x);
        v = w_alloc_iov(w, x, off);
        ensure(v->len == (x == 0 ? w->mtu - off : x), "v len != %u", x);
        ensure(v->buf == beg(v) + off, "start incorrect");
        w_free_iov(v);
    }

    struct w_iov_sq q;
    for (uint16_t x = 0; x <= w->mtu * 3; x += (w->mtu / 3)) {
        warn(INF, "sq qlen %u", x);
        sq_init(&q);
        w_alloc_len(w, &q, x, 0, 0);
        const uint32_t ql = w_iov_sq_len(&q);
        ensure(ql == x, "sq len != %u", x);
        uint32_t sl = 0;
        sq_foreach (v, &q, next) {
            ensure(v->len == (sq_next(v, next) ? w->mtu : x - sl),
                   "len %u != %u", ql, (sq_next(v, next) ? w->mtu : x - sl));
            ensure(v->buf == beg(v), "start incorrect");
            sl += v->len;
        }
        w_free(&q);
    }

    for (uint16_t x = 0; x <= w->mtu * 3; x += (w->mtu / 3)) {
        warn(INF, "sq off %u qlen %u", off, x);
        sq_init(&q);
        w_alloc_len(w, &q, x, 0, off);
        const uint32_t ql = w_iov_sq_len(&q);
        ensure(ql == x, "sq len != %u", x);
        uint32_t sl = 0;
        sq_foreach (v, &q, next) {
            ensure(v->len ==
                       (sq_next(v, next) ? w->mtu - off : (uint16_t)(x - sl)),
                   "len %u != %u", v->len,
                   (sq_next(v, next) ? w->mtu : x - sl));
            ensure(v->buf == beg(v) + off, "start incorrect");
            sl += v->len;
        }
        w_free(&q);
    }

    const uint16_t len = 1111;
    for (uint16_t x = 0; x <= w->mtu * 3; x += (w->mtu / 3)) {
        warn(INF, "sq off %u len %u qlen %u", off, len, x);
        sq_init(&q);
        w_alloc_len(w, &q, x, len, off);
        const uint32_t ql = w_iov_sq_len(&q);
        ensure(ql == x, "sq len != %u", x);
        uint32_t sl = 0;
        sq_foreach (v, &q, next) {
            ensure(v->len == (sq_next(v, next) ? len : x - sl), "len %u != %u",
                   v->len, (sq_next(v, next) ? len : x - sl));
            ensure(v->buf == beg(v) + off, "start incorrect");
            sl += v->len;
        }
        w_free(&q);
    }

    cleanup();
}
