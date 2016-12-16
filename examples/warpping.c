// Copyright (c) 2014-2016, NetApp, Inc.
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

#include "warpcore.h"


static void usage(const char * const name,
                  const uint32_t start,
                  const uint32_t inc,
                  const uint32_t end,
                  const long loops)
{
    printf("%s\n", name);
    printf("\t -i interface           interface to run over\n");
    printf("\t -d destination IP      peer to connect to\n");
    printf("\t[-r router IP]          router to use for non-local peers\n");
    printf("\t[-s start packet size]  optional, default %d\n", start);
    printf("\t[-c increment]          optional, default %d\n", inc);
    printf("\t[-e end packet size]    optional, default %d\n", end);
    printf("\t[-l loop iterations]    optional, default %ld\n", loops);
    printf("\t[-b]                    busy-wait\n");
}


// Subtract the struct timespec values x and y (x-y), storing the result in r
static void time_diff(struct timespec * const r,
                      struct timespec * const x,
                      struct timespec * const y)
{
    // Perform the carry for the later subtraction by updating y
    if (x->tv_nsec < y->tv_nsec) {
        const long nsec = (y->tv_nsec - x->tv_nsec) / 1000000000 + 1;
        y->tv_nsec -= 1000000000 * nsec;
        y->tv_sec += nsec;
    }
    if (x->tv_nsec - y->tv_nsec > 1000000000) {
        const long nsec = (x->tv_nsec - y->tv_nsec) / 1000000000;
        y->tv_nsec += 1000000000 * nsec;
        y->tv_sec -= nsec;
    }

    // Compute the result; tv_nsec is certainly positive.
    r->tv_sec = x->tv_sec - y->tv_sec;
    r->tv_nsec = x->tv_nsec - y->tv_nsec;
}


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
    long loops = 1;
    uint32_t start = sizeof(struct timespec);
    uint32_t inc = 103;
    uint32_t end = 1458;
    bool busywait = false;

    // handle arguments
    int ch;
    while ((ch = getopt(argc, argv, "hi:d:l:r:s:c:e:b")) != -1) {
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
            loops = strtol(optarg, 0, 10);
            break;
        case 's':
            start =
                MIN(UINT32_MAX, MAX(start, (uint32_t)strtol(optarg, 0, 10)));
            break;
        case 'c':
            inc = MIN(UINT32_MAX, MAX(inc, (uint32_t)strtol(optarg, 0, 10)));
            break;
        case 'e':
            end = MIN(UINT32_MAX, MAX(end, (uint32_t)strtol(optarg, 0, 10)));
            break;
        case 'b':
            busywait = true;
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
        assert(getaddrinfo(rtr, 0, &hints, &router) == 0, "getaddrinfo router");
        rip = ((struct sockaddr_in *)(void *)router->ai_addr)->sin_addr.s_addr;
        freeaddrinfo(router);
    }

    // bind this app to a single core
    plat_setaffinity();

    // initialize a warpcore engine on the given network interface
    struct warpcore * w = w_init(ifname, rip);

    // bind a new socket to a random local source port
    struct w_sock * s = w_bind(w, (uint16_t)random());

    // look up the peer IP address and "echo" port
    struct addrinfo * peer;
    assert(getaddrinfo(dst, "echo", &hints, &peer) == 0, "getaddrinfo peer");

    // connect to the peer
    w_connect(s, ((struct sockaddr_in *)(void *)peer->ai_addr)->sin_addr.s_addr,
              ((struct sockaddr_in *)(void *)peer->ai_addr)->sin_port);

    // free the getaddrinfo
    freeaddrinfo(peer);

    // set a timer handler (used with busywait)
    assert(signal(SIGALRM, &timeout) != SIG_ERR, "signal");
    const struct itimerval timer = {.it_value.tv_sec = 1};

    // send packet trains of sizes between "start" and "end"
    puts("nsec\tsize");
    for (uint32_t size = start; size <= end; size += inc) {
        // allocate tx chain
        struct w_iov_chain * const o = w_alloc(w, size, 0);

        // send "loops" number of payloads of size "size" and wait for reply
        long iter = loops;
        while (likely(iter--)) {
            // timestamp the payloads
            struct timespec now;
            assert(clock_gettime(CLOCK_REALTIME, &now) != -1, "clock_gettime");
            const struct w_iov * v;
            STAILQ_FOREACH (v, o, next)
                memcpy(v->buf, &now, sizeof(now));

            // send the data and free the w_iov
            w_tx(s, o);
            w_nic_tx(w);
            STAILQ_FOREACH (v, o, next)
                assert(memcmp(v->buf, &now, sizeof(now)) == 0, "data changed");
            warn(info, "sent %d byte%s", size, plural(size));

            // wait for a reply
            struct w_iov_chain * i = 0;
            uint32_t len = 0;

            // set a timeout
            assert(setitimer(ITIMER_REAL, &timer, 0) == 0, "setitimer");
            done = false;

            // loop until timeout expires, or we have received all data
            while (likely(len < size && done == false)) {
                if (busywait == false) {
                    // poll for new data
                    struct pollfd fds = {.fd = w_fd(s), .events = POLLIN};
                    if (poll(&fds, 1, -1) == -1)
                        // if the poll was interrupted, move on
                        continue;
                } else
                    // just suck in whatever is in the NIC rings
                    w_nic_rx(w);

                // read new data
                struct w_iov_chain * new = w_rx(s);
                if (new) {
                    len += w_iov_len(new);
                    if (i)
                        STAILQ_CONCAT(i, new);
                    else
                        i = new;
                }
            }

            // if we received no data, move to next iteration
            if (done) {
                if (i)
                    w_free(w, i);
                continue;
            }

            // get the current time
            struct timespec diff;
            assert(clock_gettime(CLOCK_REALTIME, &now) != -1, "clock_gettime");

            // stop the timeout
            const struct itimerval stop = {0};
            assert(setitimer(ITIMER_REAL, &stop, 0) == 0, "setitimer");

            warn(info, "received %d/%d byte%s", len, size, plural(len));

            // if we didn't receive all the data we sent
            if (unlikely(len < size)) {
                // assume loss
                w_free(w, i);
                warn(warn, "incomplete response, packet loss?");
                continue;
            }

            // compute time difference between the packet and the current time
            time_diff(&diff, &now, STAILQ_FIRST(i)->buf);
            if (unlikely(diff.tv_sec != 0))
                warn(warn, "time difference is more than %ld sec", diff.tv_sec);
            else
                printf("%ld\t%d\n", diff.tv_nsec, size);

            // we are done with the received data
            w_free(w, i);
        }

        w_free(w, o);
    }
    w_close(s);
    w_cleanup(w);
    return 0;
}
