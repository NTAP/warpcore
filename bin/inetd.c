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

#include <libgen.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <unistd.h>

#include <warpcore/warpcore.h>


static void usage(const char * const name, const uint32_t nbufs)
{
    printf("%s\n", name);
    printf("\t -i interface           interface to run over\n");
    printf("\t[-b]                    optional, busy-wait\n");
    printf("\t[-z]                    optional, turn off UDP checksums\n");
    printf("\t[-n buffers]            packet buffers to allocate "
           "(default %u)\n",
           nbufs);
#ifndef NDEBUG
    printf("\t[-v verbosity]          verbosity level (0-%d, default %d)\n",
           DLEVEL, util_dlevel);
#endif
}

// global termination flag
static bool done = false;


// set the global termination flag; pass signal through after second time
static void terminate(int signum __attribute__((unused)))
{
    if (done) {
        // we've been here before, restore the default signal handlers
        warn(WRN, "got repeated signal, passing through");
        ensure(signal(SIGTERM, SIG_DFL) != SIG_ERR, "signal");
        ensure(signal(SIGINT, SIG_DFL) != SIG_ERR, "signal");
    } else
        done = true;
}


struct payload {
    uint64_t nonce;
    uint64_t len;
};


int
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 8)
    __attribute__((no_sanitize("alignment")))
#endif
    main(const int argc, char * const argv[])
{
    const char * ifname = 0;
    bool busywait = false;
    struct w_sockopt opt = {0};
    uint32_t nbufs = 500000;

    // handle arguments
    int ch;
#ifndef NDEBUG
    while ((ch = getopt(argc, argv, "hi:bzn:v:")) != -1) {
#else
    while ((ch = getopt(argc, argv, "hi:bzn:")) != -1) {
#endif
        switch (ch) {
        case 'i':
            ifname = optarg;
            break;
        case 'b':
            busywait = true;
            break;
        case 'z':
            opt.enable_udp_zero_checksums = true;
            break;
        case 'n':
            nbufs = (uint32_t)MAX(1, strtoul(optarg, 0, 10));
            break;
        case 'v':
            util_dlevel = (short)MIN(DLEVEL, strtoul(optarg, 0, 10));
            break;
        case 'h':
        case '?':
        default:
            usage(basename(argv[0]), nbufs);
            return 0;
        }
    }

    if (ifname == 0) {
        usage(basename(argv[0]), nbufs);
        return 0;
    }

    // initialize a warpcore engine on the given network interface
    struct w_engine * w = w_init(ifname, 0, nbufs);

    // install a signal handler to clean up after interrupt
    ensure(signal(SIGTERM, &terminate) != SIG_ERR, "signal");
    ensure(signal(SIGINT, &terminate) != SIG_ERR, "signal");

    // start four inetd-like "small services" and one benchmark of our own
    for (uint16_t idx = 0; idx < w->addr_cnt; idx++) {
        w_bind(w, idx, bswap16(7), &opt);
        w_bind(w, idx, bswap16(9), &opt);
        w_bind(w, idx, bswap16(55555), &opt);
    }
    // serve requests on the four sockets until an interrupt occurs
    while (done == false) {
        // receive new data (there may not be any if busy-waiting)
        if (w_nic_rx(w, busywait ? 0 : -1) == false)
            continue;

        // for each of the small services that have received data...
        struct w_sock_slist sl = w_sock_slist_initializer(sl);
        w_rx_ready(w, &sl);
        struct w_sock * s;
        sl_foreach (s, &sl, next) {
            // ...check if any new data has arrived on the socket
            struct w_iov_sq i = w_iov_sq_initializer(i);
            w_rx(s, &i);
            if (sq_empty(&i))
                continue;
            warn(DBG, "received %" PRIu " bytes from %s:%u on %s:%u",
                 w_iov_sq_len(&i), w_ntop(&sq_first(&i)->wv_addr, ip_tmp),
                 bswap16(sq_first(&i)->saddr.port),
                 w_ntop(&s->ws_laddr, ip_tmp), bswap16(s->ws_lport));

            struct w_iov_sq o = w_iov_sq_initializer(o);

            switch (bswap16(s->ws_lport)) {
            case 7:
                // echo received data back to sender (zero-copy)
                sq_concat(&o, &i);
                break;

            case 9:
                // discard; nothing to do
                break;

            case 55555:;
                static struct w_iov_sq tmp = w_iov_sq_initializer(tmp);
                static uint64_t tmp_len = 0;
                static uint64_t nonce = 0;

                if (unlikely(nonce == 0))
                    nonce =
                        ((struct payload *)(void *)sq_first(&i)->buf)->nonce;

                bool tx = false;
                while (!sq_empty(&i)) {
                    struct w_iov * const v = sq_first(&i);

                    const struct payload * const p =
                        (struct payload *)(void *)v->buf;

                    if (unlikely(nonce != p->nonce)) {
                        // this packet belongs to a new flight
                        nonce = p->nonce;
                        tx = true;
                        break;
                    }

                    if (likely(tmp_len < p->len)) {
                        sq_remove_head(&i, next);
                        sq_insert_tail(&tmp, v, next);
                        tmp_len += v->len;
                    }

                    if (unlikely(tmp_len >= p->len)) {
                        // flight is done
                        tx = true;
                        break;
                    }
                }

                if (tx) {
                    sq_concat(&o, &tmp);
                    nonce = tmp_len = 0;
                }
                break;

            default:
                die("unknown service");
            }

            // if the current service requires replying with data, do so
            if (!sq_empty(&o)) {
                w_tx(s, &o);
                w_nic_tx(w);
            }

            // we are done serving the received data
            w_free(&i);
            w_free(&o);
        }
    }

    // we only get here after an interrupt; clean up
    w_cleanup(w);
    return 0;
}
