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
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
// #include <string.h>
#include <sys/queue.h>
// #include <time.h>

#ifdef __linux__
#include <netinet/in.h>
#else
#include <arpa/inet.h>
#endif

#include <warpcore.h>


static void usage(const char * const name)
{
    printf("%s\n", name);
    printf("\t -i interface           interface to run over\n");
    printf("\t[-b]                    optional, busy-wait\n");
    printf("\t[-z]                    optional, turn off UDP checksums\n");
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
    uint8_t flags = 0;

    // handle arguments
    int ch;
    while ((ch = getopt(argc, argv, "hi:bz")) != -1) {
        switch (ch) {
        case 'i':
            ifname = optarg;
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
    ensure(signal(SIGTERM, &terminate) != SIG_ERR, "signal");
    ensure(signal(SIGINT, &terminate) != SIG_ERR, "signal");

    // start four inetd-like "small services"
    struct w_sock * const srv[] = {
        w_bind(w, htons(7), flags),
        // w_bind(w, htons(9), flags),
        // w_bind(w, htons(13), flags),
        // w_bind(w, htons(37), flags)
    };
    const uint16_t n = sizeof(srv) / sizeof(struct w_sock *);
    struct pollfd fds[] = {
        {.fd = w_fd(srv[0]), .events = POLLIN},
        // {.fd = w_fd(srv[1]), .events = POLLIN},
        // {.fd = w_fd(srv[2]), .events = POLLIN},
        // {.fd = w_fd(srv[3]), .events = POLLIN}
    };

    // serve requests on the four sockets until an interrupt occurs
    while (done == false) {
        if (busywait == false) {
            // if we aren't supposed to busy-wait, poll for new data
            poll(fds, n, -1);
        }

        // receive new data (there may not be any if busy-waiting)
        w_nic_rx(w);

        // for each of the small services that have received data...
        struct w_sock_chain * c = w_rx_ready(w);
        struct w_sock * s;
        SLIST_FOREACH (s, c, next_rx) {
            // ...check if any new data has arrived on the socket
            struct w_iov_chain * i = w_rx(s);
            if (i == 0)
                continue;

            const uint32_t i_len = w_iov_chain_len(i, 0);
            struct w_iov_chain * o = 0; // w_iov for outbound data

            if (s == srv[0]) {
                // echo received data back to sender (zero-copy)
                o = i;
                // } else if (s == srv[1]) {
                //     // discard; nothing to do
                // } else if (s == srv[2]) {
                //     // daytime
                //     const time_t t = time(0);
                //     const char * ct = ctime(&t);
                //     const uint16_t l = (uint16_t)strlen(ct);
                //     struct w_iov * v;
                //     STAILQ_FOREACH (v, i, next) {
                //         memcpy(v->buf, c, l); // write a timestamp
                //         v->len = l;
                //     }
                //     o = i;
                // } else if (s == srv[3]) {
                //     // time
                //     const time_t t = time(0);
                //     struct w_iov * v;
                //     STAILQ_FOREACH (v, i, next) {
                //         memcpy(v->buf, &t, sizeof(t)); // write a timestamp
                //         v->len = sizeof(t);
                //     }
                //     o = i;
            } else {
                die("unknown service");
            }

            // if the current service requires replying with data, do so
            if (o) {
                w_tx(s, o);
                while (o->tx_pending)
                    w_nic_tx(w);
            }

            // track how much data was served
            const uint32_t o_len = w_iov_chain_len(o, 0);
            if (i_len || o_len)
                warn(info, "handled %d byte%s in, %d byte%s out", i_len,
                     plural(i_len), o_len, plural(o_len));

            // we are done serving the received data
            w_free(w, i);
        }
        free(c);
    }

    // we only get here after an interrupt; clean up
    for (uint16_t s = 0; s < n; s++)
        w_close(srv[s]);
    w_cleanup(w);
    return 0;
}
