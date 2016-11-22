#include <getopt.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdbool.h>

// example applications MUST only depend on warpcore.h
#include "plat.h"
#include "util.h"
#include "warpcore.h"


static void usage(const char * const name)
{
    printf("%s\n", name);
    printf("\t -i interface           interface to run over\n");
    printf("\t[-b]                    busy-wait\n");
}


int main(int argc, char * argv[])
{
    const char * ifname = 0;
    bool busywait = false;

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
            usage(argv[0]);
            return 0;
        }
    }

    if (ifname == 0) {
        usage(argv[0]);
        return 0;
    }

    plat_setaffinity();
    struct warpcore * w = w_init(ifname, 0);

    // start the inetd-like "small services"
    struct w_sock * const srv[] = {w_bind(w, htons(7)), w_bind(w, htons(9)),
                                   w_bind(w, htons(13)), w_bind(w, htons(37))};
    const uint16_t n = sizeof(srv) / sizeof(struct w_sock *);
    struct pollfd fds[] = {{.fd = w_fd(srv[0]), .events = POLLIN},
                           {.fd = w_fd(srv[1]), .events = POLLIN},
                           {.fd = w_fd(srv[2]), .events = POLLIN},
                           {.fd = w_fd(srv[3]), .events = POLLIN}};

    while (1) {
        w_kick_rx(w);
        if (busywait == false)
            poll(fds, n, 1000);

        for (uint16_t s = 0; s < n; s++) {
            struct w_iov * i = w_rx(srv[s]);
            uint32_t len = 0;

            while (i) {
                switch (s) {
                case 0: { // echo
                    struct w_iov * o = w_tx_alloc(srv[s], i->len);
                    memcpy(o->buf, i->buf, i->len);
                    w_connect(srv[s], i->src, i->sport);
                    w_tx(srv[s]);
                    break;
                }

                case 1: // discard
                    break;

                case 2: { // daytime
                    const time_t t = time(0);
                    const char * c = ctime(&t);
                    const uint32_t l = (uint32_t)strlen(c);
                    struct w_iov * o = w_tx_alloc(srv[s], l);
                    memcpy(o->buf, c, l);
                    w_connect(srv[s], i->src, i->sport);
                    w_tx(srv[s]);
                    break;
                }

                case 3: { // time
                    const time_t t = time(0);
                    struct w_iov * o = w_tx_alloc(srv[s], sizeof(time_t));
                    *(uint32_t *)o->buf = htonl((uint32_t)t);
                    w_connect(srv[s], i->src, i->sport);
                    w_tx(srv[s]);
                    break;
                }

                default:
                    die("unknown service");
                }

                len += i->len;
                i = STAILQ_NEXT(i, next);
            }

            w_rx_done(srv[s]);
            if (len)
                warn(info, "echo %d byte%c", len, plural(len));
        }
        w_kick_tx(w);
    }

    // for (uint16_t s = 0; s < n; s++)
    //     w_close(srv[s]);
    // w_cleanup(w);
    // return 0;
}
