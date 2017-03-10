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
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/queue.h>
#include <time.h>

#if 0
#include <string.h>
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


struct payload {
    uint32_t len;
    struct timespec ts __attribute__((packed));
};


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

    // start four inetd-like "small services" and one benchmark of our own
    struct w_sock * const srv[] = {
#if 0
        w_bind(w, htons(7), flags),
        w_bind(w, htons(9), flags),
        w_bind(w, htons(13), flags),
        w_bind(w, htons(37), flags)
#endif
        w_bind(w, htons(55555), flags)
    };
    uint16_t n = 0;
    struct pollfd fds[] = {
#if 0
        {.fd = w_fd(srv[n++]), .events = POLLIN},
        {.fd = w_fd(srv[n++]), .events = POLLIN},
        {.fd = w_fd(srv[n++]), .events = POLLIN},
        {.fd = w_fd(srv[n++]), .events = POLLIN}
#endif
        {.fd = w_fd(srv[n++]), .events = POLLIN}
    };

    // serve requests on the four sockets until an interrupt occurs
    while (done == false) {
        if (busywait == false)
            // if we aren't supposed to busy-wait, poll for new data
            poll(fds, n, -1);

        // receive new data (there may not be any if busy-waiting)
        w_nic_rx(w);

        // for each of the small services that have received data...
        struct w_sock_slist * sl = w_rx_ready(w);
        struct w_sock * s;
        SLIST_FOREACH (s, sl, next_rx) {
            // ...check if any new data has arrived on the socket
            struct w_iov_chain i = STAILQ_HEAD_INITIALIZER(i);
            w_rx(s, &i);
            if (STAILQ_EMPTY(&i))
                continue;

            struct w_iov_chain o = STAILQ_HEAD_INITIALIZER(o);
            uint16_t t = 0;
#if 0
            if (s == srv[t++]) {
                // echo received data back to sender (zero-copy)
                STAILQ_CONCAT(o, i);
            } else if (s == srv[t++]) {
                // discard; nothing to do
            } else if (s == srv[t++]) {
                // daytime
                const time_t t = time(0);
                const char * ct = ctime(&t);
                const uint16_t l = (uint16_t)strlen(ct);
                struct w_iov * v;
                STAILQ_FOREACH (v, i, next) {
                    memcpy(v->buf, c, l); // write a timestamp
                    v->len = l;
                }
                STAILQ_CONCAT(o, i);
            } else if (s == srv[t++]) {
                // time
                const time_t t = time(0);
                struct w_iov * v;
                STAILQ_FOREACH (v, i, next) {
                    memcpy(v->buf, &t, sizeof(t)); // write a timestamp
                    v->len = sizeof(t);
                }
                STAILQ_CONCAT(o, i);

            } else
#endif
            if (s == srv[t++]) {
                // our benchmark

                // if o is empty, the target len is in the first buf of the
                // incoming chain; otherwise, it's in the first buf of o
                const struct w_iov * const head =
                    STAILQ_FIRST(STAILQ_EMPTY(&o) ? &i : &o);
                const uint32_t len = ntohl(((struct payload *)head->buf)->len);

                while (!STAILQ_EMPTY(&i)) {
                    static struct w_iov_chain tmp =
                        STAILQ_HEAD_INITIALIZER(tmp);
                    static uint32_t tmp_len = 0;

                    while (tmp_len < len && !STAILQ_EMPTY(&i)) {
                        struct w_iov * const v = STAILQ_FIRST(&i);
                        STAILQ_REMOVE_HEAD(&i, next);
                        STAILQ_INSERT_TAIL(&tmp, v, next);
                        tmp_len += v->len;
                    }

                    // did we receive all data?
                    if (tmp_len == len) {
                        STAILQ_CONCAT(&o, &tmp);
                        tmp_len = 0;
                    } else
                        break;
                }

            } else {
                die("unknown service");
            }

            // if the current service requires replying with data, do so
            if (!STAILQ_EMPTY(&o)) {
                w_tx(s, &o);
                while (o.tx_pending)
                    w_nic_tx(w);
            }

            // we are done serving the received data
            w_free(w, &i);
        }
        free(sl);
    }

    // we only get here after an interrupt; clean up
    for (uint16_t s = 0; s < n; s++)
        w_close(srv[s]);
    w_cleanup(w);
    return 0;
}
