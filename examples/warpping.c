#include <fcntl.h>
#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/param.h>
#include <sys/socket.h>

// example applications MUST only depend on warpcore.h
#include "plat.h"
#include "util.h"
#include "warpcore.h"


static void
usage(const char * const name, const uint32_t size, const long loops)
{
    printf("%s\n", name);
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


// global timeout flag
static bool done = false;


// set the global timeout flag
static void timeout(int signum __attribute__((unused)))
{
    done = true;
}


int main(int argc, char * argv[])
{
    const char * ifname = 0;
    const char * dst = 0;
    const char * rtr = 0;
    long loops = 1;
    uint32_t size = sizeof(struct timespec);
    bool busywait = false;

    int ch;
    while ((ch = getopt(argc, argv, "hi:d:l:r:s:b")) != -1) {
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
            size = MIN(UINT32_MAX, MAX(size, (uint32_t)strtol(optarg, 0, 10)));
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

    struct addrinfo hints = {.ai_family = PF_INET, .ai_protocol = IP_P_UDP};
    struct addrinfo * peer;
    assert(getaddrinfo(dst, "echo", &hints, &peer) == 0, "getaddrinfo peer");

    uint32_t rip = 0;
    if (rtr) {
        struct addrinfo * router;
        assert(getaddrinfo(rtr, "echo", &hints, &router) == 0,
               "getaddrinfo router");
        rip = ((struct sockaddr_in *)(void *)router->ai_addr)->sin_addr.s_addr;
    }

    plat_setaffinity();
    struct warpcore * w = w_init(ifname, rip);
    struct w_sock * s = w_bind(w, (uint16_t)random());

    w_connect(s, ((struct sockaddr_in *)(void *)peer->ai_addr)->sin_addr.s_addr,
              ((struct sockaddr_in *)(void *)peer->ai_addr)->sin_port);
    freeaddrinfo(peer);

    // set a timer handler (used with busywait)
    const struct sigaction sa = {.sa_handler = &timeout};
    assert(sigaction(SIGALRM, &sa, 0) == 0, "sigaction");
    const struct itimerval timer = {.it_value.tv_sec = 1};

    printf("nsec\tsize\n");
    while (likely(loops--)) {
        // allocate tx chain
        struct w_iov * const o = w_tx_alloc(s, size);

        // timestamp the payload
        void * const before = o->buf;
        assert(clock_gettime(CLOCK_REALTIME, before) != -1, "clock_gettime");

        // send the data
        w_tx(s);
        w_kick_tx(w);
        warn(info, "sent %d byte%c", size, plural(size));

        // wait for a reply
        struct w_iov * i = 0;
        uint32_t len = 0;

        // set a timeout
        assert(setitimer(ITIMER_REAL, &timer, 0) == 0, "setitimer");

        while (len != size && done == false) {
            if (busywait == false) {
                // poll for new data
                struct pollfd fds = {.fd = w_fd(s), .events = POLLIN};
                poll(&fds, 1, -1);
            }

            // read new data
            w_kick_rx(w);
            i = w_rx(s);

            // compute the length of the received data
            len = 0;
            for (struct w_iov * v = i; v; v = STAILQ_NEXT(v, next))
                len += v->len;
        }

        // stop the timeout
        const struct itimerval stop = {0};
        assert(setitimer(ITIMER_REAL, &stop, 0) == 0, "setitimer");

        warn(info, "received %d/%d byte%c", len, size, plural(len));

        if (unlikely(len != size)) {
            // assume loss and send next packet
            w_rx_done(s);
            warn(warn, "incomplete response, packet loss?");
            continue;
        }

        // compute time difference
        struct timespec diff, now;
        assert(clock_gettime(CLOCK_REALTIME, &now) != -1, "clock_gettime");
        time_diff(&diff, &now, i->buf);
        if (unlikely(diff.tv_sec != 0))
            die("time difference is more than %ld sec", diff.tv_sec);

        printf("%ld\t%d\n", diff.tv_nsec, size);
        w_rx_done(s);
    }

    w_close(s);
    w_cleanup(w);
    return 0;
}
