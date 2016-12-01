#include <getopt.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>

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
            usage(argv[0]);
            return 0;
        }
    }

    if (ifname == 0) {
        usage(argv[0]);
        return 0;
    }

    // bind this app to a single core
    plat_setaffinity();

    // initialize a warpcore engine on the given network interface
    struct warpcore * w = w_init(ifname, 0);

    // install a signal handler to clean up after interrupt
    assert(signal(SIGTERM, &terminate) == 0, "signal");
    assert(signal(SIGINT, &terminate) == 0, "signal");

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
            struct w_iov * i = w_rx(srv[s]);
            uint32_t len = 0;

            // for each new packet, handle it according to the service it is for
            for (struct w_iov * v = i; v; v = STAILQ_NEXT(v, next)) {
                struct w_iov * o = 0; // w_iov for outbound data
                switch (s) {
                // echo received data back to sender
                case 0:
                    o = w_alloc(w, v->len, 0);      // allocate outbound w_iov
                    memcpy(o->buf, v->buf, v->len); // copy data
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
                    memcpy(o->buf, c, l); // write a timestamp
                    break;
                }

                // time
                case 3: {
                    const time_t t = time(0);
                    o = w_alloc(w, sizeof(time_t), 0);        // allocate w_iov
                    *(uint32_t *)o->buf = htonl((uint32_t)t); // write timestamp
                    break;
                }

                default:
                    die("unknown service");
                }

                // if the current service requires replying with data, do so
                if (o) {
                    // send the reply
                    o->ip = v->ip;
                    o->port = v->port;
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
