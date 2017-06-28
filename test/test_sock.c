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
#include <stdint.h>
#include <stdio.h>

#include <warpcore/warpcore.h>


int main()
{
    struct w_engine * w = w_init(
#ifndef __linux__
        "lo0"
#else
        "lo"
#endif
        ,
        0, 100);

    // bind server socket
    struct w_sock * ss = w_bind(w, htons(55555), 0);
    ensure(w_engine(ss) == w, "same engine");

    // connect to server
    struct w_sock * cs = w_bind(w, 0, 0);
    w_connect(cs, inet_addr("127.0.0.1"), htons(55555));
    ensure(w_connected(cs), "not connected");

    // send something
    struct w_iov_stailq o = STAILQ_HEAD_INITIALIZER(o);
    w_alloc_cnt(w, &o, 1, 0);
    struct w_iov * const ov = STAILQ_FIRST(&o);
    ov->len = (uint16_t)snprintf((char *)ov->buf, ov->len, "Hello, world!");
    w_tx(cs, &o);
    while (w_tx_pending(&o))
        w_nic_tx(w);

    // read something
    struct w_iov_stailq i = STAILQ_HEAD_INITIALIZER(i);
    w_nic_rx(w, -1);
    w_rx(ss, &i);
    if (!STAILQ_EMPTY(&i))
        warn(warn, "%s", STAILQ_FIRST(&i)->buf);

    // close down
    w_close(cs);
    w_close(ss);
    w_cleanup(w);
}
