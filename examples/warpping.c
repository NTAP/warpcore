#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/param.h>
#include <sys/socket.h>

// example applications MUST only depend on warpcore.h
#include "util.h"
#include "warpcore.h"

#ifdef __linux__
#define CLOCK_REALTIME_PRECISE CLOCK_REALTIME
#endif


static void
usage(const char * const name, const uint16_t size, const long loops)
{
    printf("%s\n", name);
    printf("\t[-k]                    use kernel, default is warpcore\n");
    printf("\t -i interface           interface to run over\n");
    printf("\t -d destination IP      peer to connect to\n");
    printf("\t[-r router IP]          router to use for non-local peers\n");
    printf("\t[-s packet size]        optional, default %d\n", size);
    printf("\t[-l loop interations]   optional, default %ld\n", loops);
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


int main(int argc, char * argv[])
{
    const char * ifname = 0;
    const char * dst = 0;
    const char * rtr = 0;
    long loops = 1;
    uint16_t size = sizeof(struct timespec);
    bool use_warpcore = true;
    bool busywait = false;

    int ch;
    while ((ch = getopt(argc, argv, "hi:d:l:r:s:kb")) != -1) {
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
            size = (uint16_t)MIN(UINT16_MAX, MAX(size, strtol(optarg, 0, 10)));
            break;
        case 'k':
            use_warpcore = false;
            break;
        case 'b':
            busywait = true;
            break;
        case 'h':
        case '?':
        default:
            usage(argv[0], size, loops);
            return 0;
        }
    }

    if (ifname == 0 || dst == 0) {
        usage(argv[0], size, loops);
        return 0;
    }

    const uint8_t proto = IP_P_UDP;
    struct addrinfo hints, *peer;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_INET;
    hints.ai_protocol = proto;
    if (getaddrinfo(dst, "echo", &hints, &peer) != 0)
        die("getaddrinfo peer");

    struct warpcore * w = 0;
    struct w_sock * ws = 0;
    int ks = 0;
    struct pollfd fds;
    void * before = 0;
    void * after = 0;
    uint32_t rip = 0;
    if (use_warpcore) {
        struct addrinfo * router;
        if (rtr) {
            assert(getaddrinfo(rtr, "echo", &hints, &router) == 0,
                   "getaddrinfo router");
            rip = ((struct sockaddr_in *)(void *)router->ai_addr)
                      ->sin_addr.s_addr;
        }
        w = w_init(ifname, rip);
        ws = w_bind(w, proto, (uint16_t)random());
        w_connect(
            ws, ((struct sockaddr_in *)(void *)peer->ai_addr)->sin_addr.s_addr,
            ((struct sockaddr_in *)(void *)peer->ai_addr)->sin_port);
    } else {
        w_init_common();
        ks = socket(peer->ai_family,
                    peer->ai_socktype | (busywait ? SOCK_NONBLOCK : 0),
                    peer->ai_protocol);
        if (ks == -1)
            die("socket");
        fds.fd = ks;
        fds.events = POLLIN;
        before = calloc(1, size);
        after = calloc(1, size);
    }

    printf("nsec\tsize\n");
    while (likely(loops--)) {
        if (use_warpcore) {
            struct w_iov * const o = w_tx_alloc(ws, size);
            before = o->buf;
        }

        if (clock_gettime(CLOCK_REALTIME_PRECISE, before) == -1)
            die("clock_gettime");

        if (use_warpcore) {
            w_tx(ws);

            const struct w_iov * i = 0;
            struct timespec diff, now;
            if (!busywait) {
                w_poll(w, POLLIN, 1000);
                w_kick_rx(w);
                i = w_rx(ws);
            } else {
                do {
                    w_kick_rx(w);
                    i = w_rx(ws);
                    if (clock_gettime(CLOCK_REALTIME_PRECISE, &now) == -1)
                        die("clock_gettime");
                    time_diff(&diff, &now, before);
                } while (i == 0 && diff.tv_sec == 0);
            }

            if (i)
                after = i->buf;
            else {
                warn(err, "packet loss");
                w_rx_done(ws);
                continue;
            }
        } else {
            sendto(ks, before, size, 0, peer->ai_addr, peer->ai_addrlen);

            if (!busywait) {
                const int p = poll(&fds, 1, 1000);
                if (unlikely(p == -1))
                    die("poll");
                else if (unlikely(p == 0)) {
                    continue;
                }
            }

            socklen_t fromlen = peer->ai_addrlen;
            ssize_t s = 0;
            do {
                s = recvfrom(ks, after, size, 0, peer->ai_addr, &fromlen);
            } while (s == 0 || (s == -1 && errno == EAGAIN));
        }

        struct timespec diff, now;
        if (clock_gettime(CLOCK_REALTIME_PRECISE, &now) == -1)
            die("clock_gettime");

        time_diff(&diff, &now, after);
        if (unlikely(diff.tv_sec != 0))
            die("time difference is more than %ld sec", diff.tv_sec);

        printf("%ld\t%d\n", diff.tv_nsec, size);
        if (use_warpcore)
            w_rx_done(ws);
    }

    if (use_warpcore) {
        w_close(ws);
        w_cleanup(w);
    } else {
        free(before);
        free(after);
    }
    freeaddrinfo(peer);

    return 0;
}
