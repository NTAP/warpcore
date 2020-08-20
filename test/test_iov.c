// SPDX-License-Identifier: BSD-2-Clause
//
// Copyright (c) 2014-2020, NetApp, Inc.
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

#include <inttypes.h>
#include <stdint.h>

#ifdef __FreeBSD__
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <warpcore/warpcore.h>

#include "backend.h"
#include "common.h"

#define beg(v) idx_to_buf(w, w_iov_idx(v))


int main(void)
{
    init(8192);

    struct w_engine * const w = w_serv;

    struct w_iov * v = w_alloc_iov(w, s_serv->ws_af, 0, 0);
    warn(DBG, "base: len %u", v->len);
    ensure(v->len == max_buf_len(w), "base len != %u", max_buf_len(w));

    for (uint16_t x = 0; x <= max_buf_len(w); x += 200) {
        warn(INF, "off %u", x);
        v = w_alloc_iov(w, s_serv->ws_af, 0, x);
        ensure(v->len == max_buf_len(w) - x, "v len != %u", max_buf_len(w) - x);
        ensure(v->buf == beg(v) + x, "start incorrect %p != %p", (void *)v->buf,
               (void *)(beg(v) + x));
        w_free_iov(v);
    }

    for (uint16_t x = 0; x <= max_buf_len(w); x += 200) {
        warn(INF, "len %u", x);
        v = w_alloc_iov(w, s_serv->ws_af, x, 0);
        ensure(v->len == (x == 0 ? max_buf_len(w) : x), "v len != %u", x);
        ensure(v->buf == beg(v), "start incorrect");
        w_free_iov(v);
    }

    const uint16_t off = 100;
    for (uint16_t x = 0; x <= max_buf_len(w) - off; x += 200) {
        warn(INF, "off %u & len %u", off, x);
        v = w_alloc_iov(w, s_serv->ws_af, x, off);
        ensure(v->len == (x == 0 ? max_buf_len(w) - off : x), "v len != %u", x);
        ensure(v->buf == beg(v) + off, "start incorrect");
        w_free_iov(v);
    }

    struct w_iov_sq q;
    for (uint32_t x = 0; x <= max_buf_len(w) * 3; x += (max_buf_len(w) / 3)) {
        warn(INF, "sq qlen %u", x);
        sq_init(&q);
        w_alloc_len(w, s_serv->ws_af, &q, x, 0, 0);
        const uint64_t ql = w_iov_sq_len(&q);
        ensure(ql == x, "sq len != %u", x);
        uint32_t sl = 0;
        sq_foreach (v, &q, next) {
            ensure(v->len == (sq_next(v, next) ? max_buf_len(w) : x - sl),
                   "len %" PRIu64 " != %u", ql,
                   (sq_next(v, next) ? max_buf_len(w) : x - sl));
            ensure(v->buf == beg(v), "start incorrect");
            sl += v->len;
        }
        w_free(&q);
    }

    for (uint32_t x = 0; x <= max_buf_len(w) * 3; x += (max_buf_len(w) / 3)) {
        warn(INF, "sq off %u qlen %u", off, x);
        sq_init(&q);
        w_alloc_len(w, s_serv->ws_af, &q, x, 0, off);
        const uint64_t ql = w_iov_sq_len(&q);
        ensure(ql == x, "sq len != %u", x);
        uint32_t sl = 0;
        sq_foreach (v, &q, next) {
            ensure(v->len == (sq_next(v, next) ? max_buf_len(w) - off
                                               : (uint16_t)(x - sl)),
                   "len %u != %u", v->len,
                   (sq_next(v, next) ? max_buf_len(w) : x - sl));
            ensure(v->buf == beg(v) + off, "start incorrect");
            sl += v->len;
        }
        w_free(&q);
    }

    const uint16_t len = 1111;
    for (uint32_t x = 0; x <= max_buf_len(w) * 3; x += (max_buf_len(w) / 3)) {
        warn(INF, "sq off %u len %u qlen %u", off, len, x);
        sq_init(&q);
        w_alloc_len(w, s_serv->ws_af, &q, x, len, off);
        const uint64_t ql = w_iov_sq_len(&q);
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
