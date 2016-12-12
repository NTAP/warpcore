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

#include <arpa/inet.h> // for htons, htonl
#include <getopt.h>    // for getopt, optarg
#include <poll.h>      // for POLLIN, poll, pollfd
#include <signal.h>    // for signal, SIG_ERR, SIGINT, SIGTERM
#include <stdbool.h>   // for false, bool, true
#include <stdint.h>    // for uint32_t, uint16_t
#include <stdio.h>     // for printf
#include <string.h>    // for memcpy, strlen
#include <sys/queue.h> // for STAILQ_FIRST, STAILQ_FOREACH, w_iov::(anonymous)
#include <time.h>      // for time, time_t, ctime

#include "warpcore.h" // for assert, basename, dlevel::info, plat_setaffinity

struct w_sock;


static void usage(const char * const name)
{
    printf("%s\n", name);
    printf("\t -i interface           interface to run over\n");
    printf("\t[-b]                    busy-wait\n");
}

// global termination flag
static bool done = false;


// set the global termination flag
static void terminate(int signum __attribute__((unused)))
{
    done = true;
}


int main(const int argc, char * const argv[])
{
    const char * ifname = 0;
    bool busywait = false;

    // handle arguments
    int ch;
    while ((ch = getopt(argc, argv, "hi:b")) != -1) {
        switch (ch) {
        case 'i':
            ifname = optarg;
            break;
        case 'b':
            busywait = true;
            break;
        case 'h':
        case '?':
        default:
            usage(basename(argv[0]));
            return 0;
        }
    }

    if (ifname == 0) {
        usage(basename(argv[0]));
        return 0;
    }

    // bind this app to a single core
    plat_setaffinity();

    // initialize a warpcore engine on the given network interface
    struct warpcore * w = w_init(ifname, 0);

    // install a signal handler to clean up after interrupt
    assert(signal(SIGTERM, &terminate) != SIG_ERR, "signal");
    assert(signal(SIGINT, &terminate) != SIG_ERR, "signal");

    // start four inetd-like "small services"
    struct w_sock * const srv[] = {w_bind(w, htons(7)), w_bind(w, htons(9)),
                                   w_bind(w, htons(13)), w_bind(w, htons(37))};
    const uint16_t n = sizeof(srv) / sizeof(struct w_sock *);
    struct pollfd fds[] = {{.fd = w_fd(srv[0]), .events = POLLIN},
                           {.fd = w_fd(srv[1]), .events = POLLIN},
                           {.fd = w_fd(srv[2]), .events = POLLIN},
                           {.fd = w_fd(srv[3]), .events = POLLIN}};

    // serve requests on the four sockets until an interrupt occurs
    while (done == false) {
        if (busywait == false)
            // if we aren't supposed to busy-wait, poll for new data
            poll(fds, n, -1);
        else
            // otherwise, just pull in whatever is in the NIC rings
            w_nic_rx(w);

        // for each of the small services...
        for (uint16_t s = 0; s < n; s++) {
            // ...check if any new data has arrived on the socket
            struct w_chain * i = w_rx(srv[s]);
            uint32_t len = 0;

            if (i == 0)
                continue;

            // for each new packet, handle it according to the service it is for
            struct w_iov * v;
            STAILQ_FOREACH (v, i, next) {
                struct w_chain * o = 0; // w_iov for outbound data
                switch (s) {
                // echo received data back to sender
                case 0:
                    o = w_alloc(w, v->len, 0); // allocate outbound w_iov
                    memcpy(STAILQ_FIRST(o)->buf, v->buf, v->len); // copy data
                    break;

                // discard; nothing to do
                case 1:
                    break;

                // daytime
                case 2: {
                    const time_t t = time(0);
                    const char * c = ctime(&t);
                    const uint32_t l = (uint32_t)strlen(c);
                    o = w_alloc(w, l, 0); // allocate outbound w_iov
                    memcpy(STAILQ_FIRST(o)->buf, c, l); // write a timestamp
                    break;
                }

                // time
                case 3: {
                    const time_t t = time(0);
                    o = w_alloc(w, sizeof(time_t), 0); // allocate w_iov
                    *(uint32_t *)STAILQ_FIRST(o)->buf =
                        htonl((uint32_t)t); // write timestamp
                    break;
                }

                default:
                    die("unknown service");
                }

                // if the current service requires replying with data, do so
                if (o) {
                    // send the reply
                    STAILQ_FIRST(o)->ip = v->ip;
                    STAILQ_FIRST(o)->port = v->port;
                    w_tx(srv[s], o);
                    w_nic_tx(w);

                    // deallocate the outbound w_iov
                    w_free(w, o);
                }

                // track how much data was served
                len += v->len;
            }

            // we are done serving the received data
            w_free(w, i);

            if (len)
                warn(info, "handled %d byte%c", len, plural(len));
        }
    }

    // we only get here after an interrupt; clean up
    for (uint16_t s = 0; s < n; s++)
        w_close(srv[s]);
    w_cleanup(w);
    return 0;
}
