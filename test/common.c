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

#include <arpa/inet.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <warpcore/warpcore.h>

#include "common.h"


struct w_engine * w;
struct w_sock * ss;
struct w_sock * cs;


bool io(const uint32_t len)
{
    // allocate a w_iov chain for tx
    struct w_iov_stailq o = w_iov_stailq_initializer(o);
    w_alloc_cnt(w, &o, len, 0);
    ensure(w_iov_stailq_cnt(&o) == len, "wrong length");

    // fill it with data
    struct w_iov * ov;
    uint8_t fill = 0;
    STAILQ_FOREACH (ov, &o, next)
        memset(ov->buf, fill++, ov->len);
    const uint32_t olen = w_iov_stailq_len(&o);

    // tx
    w_tx(cs, &o);
    while (w_tx_pending(&o))
        w_nic_tx(w);

    // read the chain back
    struct w_iov_stailq i = w_iov_stailq_initializer(i);
    uint32_t ilen = 0;
    do {
        w_nic_rx(w, -1);
        w_rx(ss, &i);
        const uint32_t new_ilen = w_iov_stailq_len(&i);
        if (ilen == new_ilen) {
            // we ran out of buffers, can't test further
            w_free(w, &o);
            w_free(w, &i);
            return false;
        }
        ilen = new_ilen;
    } while (ilen < olen);
    ensure(ilen == olen, "wrong length");

    // validate data
    struct w_iov * iv = STAILQ_FIRST(&i);
    ov = STAILQ_FIRST(&o);
    while (ov && iv) {
        ensure(memcmp(iv->buf, ov->buf, iv->len) == 0,
               "ov %u = 0x%02x (len %u) != iv %u = 0x%02x (len %u)", ov->idx,
               ov->buf[0], ov->len, iv->idx, iv->buf[0], iv->len);
        ov = STAILQ_NEXT(ov, next);
        iv = STAILQ_NEXT(iv, next);
    }

    w_free(w, &o);
    w_free(w, &i);
    return true;
}


void init(void)
{
    w = w_init(
#ifndef __linux__
        "lo0"
#else
        "lo"
#endif
        ,
        0, 100);

    // bind server socket
    ss = w_bind(w, htons(55555), 0);
    ensure(w_engine(ss) == w, "same engine");

    // connect to server
    cs = w_bind(w, 0, 0);
    w_connect(cs, inet_addr("127.0.0.1"), htons(55555));
    ensure(w_connected(cs), "not connected");
}


void cleanup(void)
{
    // close down
    w_close(cs);
    w_close(ss);
    w_cleanup(w);
}
