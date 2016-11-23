#include <getopt.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>

// example applications MUST only depend on warpcore.h
#include <warpcore.h>


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
    assert(signal(SIGTERM, &terminate) == 0, "signal");
    assert(signal(SIGINT, &terminate) == 0, "signal");

    // start the inetd-like "small services"
    struct w_sock * const srv[] = {w_bind(w, htons(7)), w_bind(w, htons(9)),
                                   w_bind(w, htons(13)), w_bind(w, htons(37))};
    const uint16_t n = sizeof(srv) / sizeof(struct w_sock *);
    struct pollfd fds[] = {{.fd = w_fd(srv[0]), .events = POLLIN},
                           {.fd = w_fd(srv[1]), .events = POLLIN},
                           {.fd = w_fd(srv[2]), .events = POLLIN},
                           {.fd = w_fd(srv[3]), .events = POLLIN}};

    while (done == false) {
        if (busywait == false)
            poll(fds, n, -1);
        else
            w_nic_rx(w);

        for (uint16_t s = 0; s < n; s++) {
            struct w_iov * i = w_rx(srv[s]);
            uint32_t len = 0;

            for (struct w_iov * v = i; v; v = STAILQ_NEXT(v, next)) {
                struct w_iov * o = 0;
                switch (s) {
                case 0: // echo
                    o = w_tx_alloc(w, v->len);
                    memcpy(o->buf, v->buf, v->len);
                    break;

                case 1: // discard
                    break;

                case 2: { // daytime
                    const time_t t = time(0);
                    const char * c = ctime(&t);
                    const uint32_t l = (uint32_t)strlen(c);
                    o = w_tx_alloc(w, l);
                    memcpy(o->buf, c, l);
                    break;
                }

                case 3: { // time
                    const time_t t = time(0);
                    o = w_tx_alloc(w, sizeof(time_t));
                    *(uint32_t *)o->buf = htonl((uint32_t)t);
                    break;
                }

                default:
                    die("unknown service");
                }

                if (o) {
                    w_connect(srv[s], v->src, v->sport);
                    w_tx(srv[s], o);
                    w_nic_tx(w);
                    w_tx_done(w, o);
                }

                len += v->len;
            }

            w_rx_done(srv[s]);
            if (len)
                warn(info, "handled %d byte%c", len, plural(len));
        }
    }

    for (uint16_t s = 0; s < n; s++)
        w_close(srv[s]);
    w_cleanup(w);
    return 0;
}
