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

#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>

#include <warpcore.h>


static void usage(const char * const name,
                  const uint32_t start,
                  const uint32_t inc,
                  const uint32_t end,
                  const uint32_t loops)
{
    printf("%s\n", name);
    printf("\t -i interface           interface to run over\n");
    printf("\t -d destination IP      peer to connect to\n");
    printf("\t[-r router IP]          router to use for non-local peers\n");
    printf("\t[-s start packet size]  optional, default %u\n", start);
    printf("\t[-c increment]          optional (0 = exponential), default %u\n",
           inc);
    printf("\t[-e end packet size]    optional, default %u\n", end);
    printf("\t[-l loop iterations]    optional, default %u\n", loops);
    printf("\t[-z]                    optional, turn off UDP checksums\n");
    printf("\t[-b]                    busy-wait\n");
}


// Subtract the struct timespec values x and y (x-y), storing the result in r.
// Inspired by timersub()
#define time_diff(r, x, y)                                                     \
    do {                                                                       \
        (r)->tv_sec = (x)->tv_sec - (y)->tv_sec;                               \
        (r)->tv_nsec = (x)->tv_nsec - (y)->tv_nsec;                            \
        if ((r)->tv_nsec < 0) {                                                \
            --(r)->tv_sec;                                                     \
            (r)->tv_nsec += 1000000000;                                        \
        }                                                                      \
    } while (0)


// global timeout flag
static bool done = false;


// set the global timeout flag
static void timeout(int signum __attribute__((unused)))
{
    done = true;
}


int main(const int argc, char * const argv[])
{
    const char * ifname = 0;
    const char * dst = 0;
    const char * rtr = 0;
    uint32_t loops = 1;
    uint32_t start = sizeof(struct timespec);
    uint32_t inc = 103;
    uint32_t end = 1458;
    bool busywait = false;
    uint8_t flags = 0;

    // handle arguments
    int ch;
    while ((ch = getopt(argc, argv, "hzi:d:l:r:s:c:e:b")) != -1) {
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
            loops = MIN(UINT32_MAX, (uint32_t)strtoul(optarg, 0, 10));
            break;
        case 's':
            start = MIN(UINT32_MAX, MAX(1, (uint32_t)strtoul(optarg, 0, 10)));
            break;
        case 'c':
            inc = MIN(UINT32_MAX, MAX(0, (uint32_t)strtoul(optarg, 0, 10)));
            break;
        case 'e':
            end = MIN(UINT32_MAX, MAX(1, (uint32_t)strtoul(optarg, 0, 10)));
            break;
        case 'b':
            busywait = true;
            break;
        case 'z':
            flags |= W_ZERO_CHKSUM;
            break;
        case 'h':
        case '?':
        default:
            usage(basename(argv[0]), start, inc, end, loops);
            return 0;
        }
    }

    if (ifname == 0 || dst == 0) {
        usage(basename(argv[0]), start, inc, end, loops);
        return 0;
    }

    if (end < start)
        end = start;

    const struct addrinfo hints = {.ai_family = PF_INET,
                                   .ai_protocol = IPPROTO_UDP};
    uint32_t rip = 0;
    if (rtr) {
        struct addrinfo * router;
        ensure(getaddrinfo(rtr, 0, &hints, &router) == 0, "getaddrinfo router");
        rip = ((struct sockaddr_in *)(void *)router->ai_addr)->sin_addr.s_addr;
        freeaddrinfo(router);
    }

    // bind this app to a single core
    plat_setaffinity();

    // initialize a warpcore engine on the given network interface
    struct warpcore * w = w_init(ifname, rip);

    // bind a new socket to a random local source port
    struct w_sock * s = w_bind(w, (uint16_t)plat_random(), flags);

    // look up the peer IP address and "echo" port
    struct addrinfo * peer;
    ensure(getaddrinfo(dst, "echo", &hints, &peer) == 0, "getaddrinfo peer");

    // connect to the peer
    w_connect(s, ((struct sockaddr_in *)(void *)peer->ai_addr)->sin_addr.s_addr,
              ((struct sockaddr_in *)(void *)peer->ai_addr)->sin_port);

    // free the getaddrinfo
    freeaddrinfo(peer);

    // set a timer handler (used with busywait)
    ensure(signal(SIGALRM, &timeout) != SIG_ERR, "signal");
    const struct itimerval timer = {.it_value.tv_sec = 1};

    // send packet trains of sizes between "start" and "end"
    puts("time\tbyte\ttx\trx");

    // send "loops" number of payloads of size "size" and wait for reply
    long iter = loops;
    while (likely(iter--)) {

        for (uint32_t size = start; size <= end; size += (inc ? inc : size)) {
            // allocate tx chain
            struct w_iov_chain * o = w_alloc_size(w, size, 0);

            // get the current time
            struct timespec before_tx;
            ensure(clock_gettime(CLOCK_MONOTONIC, &before_tx) != -1,
                   "clock_gettime");

            // timestamp the payloads
            const struct w_iov * v;
            STAILQ_FOREACH (v, o, next)
                memcpy(v->buf, &before_tx, sizeof(struct timespec));

            // send the data, and wait until it is out
            w_tx(s, o);
            while (o->tx_pending)
                w_nic_tx(w);

            // set a timeout
            ensure(setitimer(ITIMER_REAL, &timer, 0) == 0, "setitimer");
            done = false;

            warn(info, "sent %u byte%s", size, plural(size));

            // get the current time
            struct timespec after_tx;
            ensure(clock_gettime(CLOCK_MONOTONIC, &after_tx) != -1,
                   "clock_gettime");

            // wait for a reply
            struct w_iov_chain * i = 0;
            uint32_t len = 0;

            // loop until timeout expires, or we have received all data
            while (likely(len < size && done == false)) {
                if (unlikely(busywait == false)) {
                    // poll for new data
                    struct pollfd fds = {.fd = w_fd(s), .events = POLLIN};
                    if (poll(&fds, 1, -1) == -1)
                        // if the poll was interrupted, move on
                        continue;
                }

                // receive new data (there may not be any if busy-waiting)
                w_nic_rx(w);

                // read new data
                struct w_iov_chain * new = w_rx(s);
                if (new) {
                    len += w_iov_chain_len(new, 0);
                    if (i)
                        STAILQ_CONCAT(i, new);
                    else
                        i = new;
                }
            }

            // get the current time
            struct timespec after_rx;
            ensure(clock_gettime(CLOCK_MONOTONIC, &after_rx) != -1,
                   "clock_gettime");

            // stop the timeout
            const struct itimerval stop = {0};
            ensure(setitimer(ITIMER_REAL, &stop, 0) == 0, "setitimer");

            warn(info, "received %u/%u byte%s", len, size, plural(len));

            // compute time difference between the packet and the current time
            struct timespec diff_tx, diff_rx;
            time_diff(&diff_tx, &after_tx, &before_tx);
            char rx[256] = "NA";
            if (len == size) {
                time_diff(&diff_rx, &after_rx, &before_tx);
                snprintf(rx, 256, "%ld.%.9ld", diff_rx.tv_sec, diff_rx.tv_nsec);
            }
            printf("%ld.%.9ld\t%d\t%ld.%.9ld\t%s\n", after_rx.tv_sec,
                   after_rx.tv_nsec, size, diff_tx.tv_sec, diff_tx.tv_nsec, rx);

            // we are done with the received data
            if (i)
                w_free(w, i);
            w_free(w, o);
        }
    }
    w_close(s);
    w_cleanup(w);
    return 0;
}
