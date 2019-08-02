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

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>

#include <warpcore/warpcore.h>

#include "common.h"


struct w_engine *w_serv, *w_clnt;
static struct w_sock *s_serv, *s_clnt;

#define OFFSET 64

bool io(const uint_t len)
{
    // allocate a w_iov chain for tx
    struct w_iov_sq o = w_iov_sq_initializer(o);
    w_alloc_cnt(w_clnt, &o, len, 512, OFFSET);
    if (w_iov_sq_cnt(&o) != len) {
        w_free(&o);
        return false;
    }

    // fill it with data
    struct w_iov * ov;
    uint8_t fill = 0x0f;
    sq_foreach (ov, &o, next) {
        memset(ov->buf, fill++, ov->len);
        ov->flags = 0x55;
    }
    const uint_t olen = w_iov_sq_len(&o);

    // tx
    w_tx(s_clnt, &o);
    while (w_tx_pending(&o)) {
        w_nic_tx(w_clnt);
        w_nic_rx(w_serv, 0); // warpcore over loopback pipe needs this
    }
    ensure(olen == w_iov_sq_len(&o), "same length");

    // read the chain back
    struct w_iov_sq i = w_iov_sq_initializer(i);
    uint_t ilen = 0;
    bool again = true;
    while (ilen < olen) {
        w_rx(s_serv, &i);
        ilen = w_iov_sq_len(&i);
        if (ilen < olen) {
            if (again) {
                w_nic_rx(w_serv, 100);
                again = false;
            } else
                return false;
        }
    }
    ensure(w_iov_sq_cnt(&i) == w_iov_sq_cnt(&o),
           "icnt %" PRIu " != ocnt %" PRIu "", w_iov_sq_cnt(&i),
           w_iov_sq_cnt(&o));
    ensure(ilen == olen, "ilen %" PRIu " != olen %" PRIu, ilen, olen);

    // validate data (o was sent by client, i is received by server)
    struct w_iov * iv = sq_first(&i);
    ov = sq_first(&o);
    while (ov && iv) {
        iv->buf += OFFSET;
        iv->len -= OFFSET;
        ensure(memcmp(iv->buf, ov->buf, iv->len) == 0,
               "ov %u = 0x%02x (len %u) != iv %u = 0x%02x (len %u)", ov->idx,
               ov->buf[0], ov->len, iv->idx, iv->buf[0], iv->len);
#ifndef __APPLE__
        // XXX Apple has an OS bug with TOS
        ensure(ov->flags == iv->flags, "TOS byte 0x%02x != 0x%02x", ov->flags,
               iv->flags);
#endif
        ensure(((struct sockaddr_in *)&iv->addr)->sin_port ==
                   ((const struct sockaddr_in *)(const void *)w_get_addr(s_clnt,
                                                                         true))
                       ->sin_port,
               "port %u != port %u",
               bswap16(((struct sockaddr_in *)&iv->addr)->sin_port),
               bswap16(((const struct sockaddr_in *)(const void *)w_get_addr(
                            s_clnt, true))
                           ->sin_port));

        ensure(((struct sockaddr_in *)&ov->addr)->sin_addr.s_addr == 0 ||
                   ((struct sockaddr_in *)&iv->addr)->sin_addr.s_addr ==
                       ((struct sockaddr_in *)&ov->addr)->sin_addr.s_addr,
               "IP %08x != IP %08x",
               bswap32(((struct sockaddr_in *)&iv->addr)->sin_addr.s_addr),
               bswap32(((struct sockaddr_in *)&ov->addr)->sin_addr.s_addr));

        ov = sq_next(ov, next);
        iv = sq_next(iv, next);
    }
    ensure(ov == 0 && iv == 0, "done with data ov %p iv %p", (void *)ov,
           (void *)iv);

    w_free(&o);
    w_free(&i);
    return true;
}


void init(const uint_t len)
{
    char i[IFNAMSIZ] = "lo"
#ifndef __linux__
                       "0"
#endif
        ;

    w_serv = w_init(i, 0, len);
    w_clnt = w_init(i, 0, len);

    // bind server socket
    s_serv = w_bind(w_serv, bswap16(55555), 0);

    // connect to server
    s_clnt = w_bind(w_clnt, 0, 0);
    w_connect(s_clnt, (struct sockaddr *)&(struct sockaddr_in){
                          .sin_family = AF_INET,
                          .sin_addr.s_addr = inet_addr("127.0.0.1"),
                          .sin_port = bswap16(55555)});
    ensure(w_connected(s_clnt), "not connected");
}


void cleanup(void)
{
    // close down
    w_close(s_clnt);
    w_close(s_serv);
    w_cleanup(w_clnt);
    w_cleanup(w_serv);
}
