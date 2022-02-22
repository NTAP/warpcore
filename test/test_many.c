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

#include <netinet/in.h>
#include <stdbool.h>
#include <sys/socket.h>

#include <warpcore/warpcore.h>

#include "common.h"


int main(void)
{
    init(64 * 1024);

    // util_dlevel = WRN;

    int n = 0;
    while (1) {
        struct w_sock * const s = w_bind(w_clnt, 0, 0, 0);
        if (s == 0)
            break;
        w_connect(s, (struct sockaddr *)&(struct sockaddr_in6){
                         .sin6_family = AF_INET6,
                         .sin6_addr = IN6ADDR_LOOPBACK_INIT,
                         .sin6_port = bswap16(55555)});
        if (w_connected(s) == false)
            break;
        n++;
    }

    // util_dlevel = DBG;

    warn(WRN, "Was able to open %d connections", n);

    cleanup();
}
