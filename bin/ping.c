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

#include <libgen.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <warpcore/warpcore.h>


struct payload {
    uint64_t nonce;
    uint64_t len;
};


static void usage(const char * const name,
                  const uint32_t start,
                  const uint32_t inc,
                  const uint32_t end,
                  const uint32_t loops,
                  const uint32_t conns,
                  const uint32_t nbufs)
{
    printf("%s\n", name);
    printf("\t -i interface           interface to run over\n");
    printf("\t -d destination IP      peer to connect to\n");
    printf("\t[-r router IP]          router to use for non-local peers\n");
    printf("\t[-n buffers]            packet buffers to allocate "
           "(default %u)\n",
           nbufs);
    printf("\t[-s start packet len]   starting packet length (default %u, max "
           "%zu)\n",
           start, sizeof(struct payload));
    printf("\t[-p increment]          packet length increment; 0 = exponential "
           "(default %u)\n",
           inc);
    printf("\t[-e end packet len]     largest packet length (default %u)\n",
           end);
    printf("\t[-l loop iterations]    repeat iterations (default %u)\n", loops);
    printf("\t[-c connections]        parallel connections (default %u)\n",
           conns);
    printf("\t[-z]                    turn off UDP checksums\n");
    printf("\t[-b]                    busy-wait\n");
#ifndef NDEBUG
    printf("\t[-v verbosity]          verbosity level (0-%d, default %d)\n",
           DLEVEL, util_dlevel);
#endif
}


// global timeout flag
static bool done = false;


// set the global timeout flag
static void timeout(int signum __attribute__((unused)))
{
    done = true;
}


int
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 8)
    __attribute__((no_sanitize("alignment")))
#endif
    main(const int argc, char * const argv[])
{
    const char * ifname = 0;
    const char * dst = 0;
    const char * rtr = 0;
    uint32_t loops = 1;
    uint32_t start = sizeof(struct payload);
    uint32_t inc = 143;
    uint32_t end = 1458;
    uint32_t conns = 1;
    bool busywait = false;
    struct w_sockopt opt = {0};
    uint32_t nbufs = 500000;

    // handle arguments
    int ch;
#ifndef NDEBUG
    while ((ch = getopt(argc, argv, "hzbi:d:l:r:s:c:e:p:n:v:")) != -1) {
#else
    while ((ch = getopt(argc, argv, "hzbi:d:l:r:s:c:e:p:n:")) != -1) {
#endif
        switch (ch) {
        case 'i':
            ifname = optarg;
            break;
        case 'd':
            dst = optarg;
            break;
        case 'r':
            rtr = optarg;
            break;
        case 'l':
            loops = (uint32_t)MIN(UINT32_MAX, strtoul(optarg, 0, 10));
            break;
        case 's':
            start = (uint32_t)MIN(UINT32_MAX, MAX(sizeof(struct payload),
                                                  strtoul(optarg, 0, 10)));
            break;
        case 'p':
            inc = (uint32_t)MIN(UINT32_MAX, strtoul(optarg, 0, 10));
            break;
        case 'e':
            end = (uint32_t)MIN(UINT32_MAX, MAX(1, strtoul(optarg, 0, 10)));
            break;
        case 'c':
            conns = (uint32_t)MIN(50000, MAX(1, strtoul(optarg, 0, 10)));
            break;
        case 'n':
            nbufs = (uint32_t)MAX(1, strtoul(optarg, 0, 10));
            break;
        case 'b':
            busywait = true;
            break;
        case 'z':
            opt.enable_udp_zero_checksums = true;
            break;
        case 'v':
            util_dlevel = (short)MIN(DLEVEL, strtoul(optarg, 0, 10));
            break;
        case 'h':
        case '?':
        default:
            usage(basename(argv[0]), start, inc, end, loops, conns, nbufs);
            return 0;
        }
    }

    if (ifname == 0 || dst == 0) {
        usage(basename(argv[0]), start, inc, end, loops, conns, nbufs);
        return 0;
    }

    if (end < start)
        end = start;

    uint32_t rip = 0;
    if (rtr) {
        struct addrinfo * router;
        ensure(getaddrinfo(rtr, 0, 0, &router) == 0, "getaddrinfo router");
        rip = ((struct sockaddr_in *)(void *)router->ai_addr)->sin_addr.s_addr;
        freeaddrinfo(router);
    }

    // initialize a warpcore engine on the given network interface
    struct w_engine * w = w_init(ifname, rip, nbufs);

    struct w_sock ** s = calloc(conns, sizeof(struct w_sock *));
    ensure(s, "got sockets");

    // look up the peer IP address and our benchmark port
    struct addrinfo * peer;
    ensure(getaddrinfo(dst, "55555", 0, &peer) == 0, "getaddrinfo peer");

    // find a src address of the same family as the peer address
    uint16_t idx = 0;
    for (; idx < w->addr_cnt; idx++)
        if (w->ifaddr[idx].addr.af == peer->ai_family)
            break;
    ensure(idx < w->addr_cnt, "peer address family not available locally");

    for (uint32_t c = 0; c < conns; c++) {
        // connect to the peer
        s[c] = w_bind(w, idx, 0, &opt);
        ensure(s[c], "could not bind");
        w_connect(s[c], peer->ai_addr);
    }

    // free the getaddrinfo
    freeaddrinfo(peer);

    // set a timer handler (used with busywait)
    ensure(signal(SIGALRM, &timeout) != SIG_ERR, "signal");
    const struct itimerval timer = {.it_value.tv_usec = 250000};

    // send packet trains of sizes between "start" and "end"
    puts("iface\tdriver\tmbps\tbyte\tpkts\ttx\trx");

    // send "loops" number of payloads of len "len" and wait for reply
    for (uint_t len = start; len <= end; len += (inc ? inc : len)) {
        // allocate tx tail queue
        struct w_iov_sq o = w_iov_sq_initializer(o);
        w_alloc_len(w, w->ifaddr[idx].addr.af, &o, len, 0, 0);

        long iter = loops;
        while (likely(iter--)) {
            // pick a random connection for output
            const uint32_t c = w_rand_uniform32(conns);

            // get the current time
            struct timespec before_tx;
            ensure(clock_gettime(CLOCK_MONOTONIC_RAW, &before_tx) != -1,
                   "clock_gettime");

            // stamp the data
            const uint64_t nonce = w_rand64();
            struct w_iov * v = 0;
            sq_foreach (v, &o, next) {
                struct payload * const p = (void *)v->buf;
                p->nonce = nonce;
                p->len = len;
            }

            // send the data, and wait until it is out
            w_tx(s[c], &o);
            w_nic_tx(w);

            // get the current time
            struct timespec after_tx;
            ensure(clock_gettime(CLOCK_MONOTONIC_RAW, &after_tx) != -1,
                   "clock_gettime");

            // set a timeout
            ensure(setitimer(ITIMER_REAL, &timer, 0) == 0, "setitimer");
            done = false;

            warn(INF, "sent %" PRIu " byte%s", len, plural(len));

            // wait for a reply; loop until timeout or we have received all data
            struct w_iov_sq i = w_iov_sq_initializer(i);
            while (likely(w_iov_sq_cnt(&i) < w_iov_sq_cnt(&o) && !done)) {
                // receive new data (there may not be any if busy-waiting)
                if (w_nic_rx(w, busywait ? 0 : -1) == false)
                    continue;
                w_rx(s[c], &i);
            }

            // get the current time
            struct timespec after_rx;
            ensure(clock_gettime(CLOCK_MONOTONIC_RAW, &after_rx) != -1,
                   "clock_gettime");

            // stop the timeout
            const struct itimerval stop = {{0, 0}, {0, 0}};
            ensure(setitimer(ITIMER_REAL, &stop, 0) == 0, "setitimer");

            ensure(w_iov_sq_len(&i) == len || (w_iov_sq_len(&i) < len && done),
                   "data len OK");

            sq_foreach (v, &o, next) {
                struct payload * const p = (void *)v->buf;
                ensure(p->nonce == nonce, "nonce mismatch");
                ensure(p->len == len, "len mismatch");
            }

            const uint_t i_len = w_iov_sq_len(&i);
            if (i_len != len)
                warn(WRN, "received %" PRIu "/%" PRIu " byte%s", i_len, len,
                     plural(i_len));

            // compute time difference between the packet and the current time
            struct timespec diff;
            char rx[256] = "NA";
            if (i_len == len) {
                timespec_sub(&after_rx, &before_tx, &diff);
                ensure(diff.tv_sec == 0, "time difference > %lu sec",
                       diff.tv_sec);
                snprintf(rx, 256, "%ld", diff.tv_nsec);
            }
            const uint_t pkts = w_iov_sq_cnt(&i);
            timespec_sub(&after_tx, &before_tx, &diff);
            ensure(diff.tv_sec == 0, "time difference > %lu sec", diff.tv_sec);
            printf("%s\t%s\t%u\t%" PRIu "\t%" PRIu "\t%ld\t%s\n", w->ifname,
                   w->drvname, w->mbps, i_len, pkts, diff.tv_nsec, rx);

            // we are done with the data
            w_free(&i);
        }
        w_free(&o);
    }

    for (uint32_t c = 0; c < conns; c++)
        w_close(s[c]);
    w_cleanup(w);
    free(s);
    return 0;
}
