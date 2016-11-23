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
#include <warpcore.h>


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


int main(int argc, char * argv[])
{
    const char * ifname = 0;
    const char * dst = 0;
    const char * rtr = 0;
    long loops = 1;
    uint32_t start = sizeof(struct timespec);
    uint32_t inc = 103;
    uint32_t end = 1458;
    bool busywait = false;

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
            usage(argv[0], start, inc, end, loops);
            return 0;
        }
    }

    if (ifname == 0 || dst == 0) {
        usage(argv[0], start, inc, end, loops);
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
    assert(signal(SIGALRM, &timeout) == 0, "signal");
    const struct itimerval timer = {.it_value.tv_sec = 1};

    printf("nsec\tsize\n");
    for (uint32_t size = start; size <= end; size += inc) {
        long iter = loops;
        while (likely(iter--)) {
            // allocate tx chain
            struct w_iov * const o = w_tx_alloc(w, size);

            // timestamp the payload
            assert(clock_gettime(CLOCK_REALTIME, o->buf) != -1,
                   "clock_gettime");

            // send the data
            w_tx(s, o);
            w_nic_tx(w);
            w_tx_done(w, o);
            warn(info, "sent %d byte%c", size, plural(size));

            // wait for a reply
            struct w_iov * i = 0;
            uint32_t len = 0;

            // set a timeout
            assert(setitimer(ITIMER_REAL, &timer, 0) == 0, "setitimer");

            while (likely(len < size && done == false)) {
                if (busywait == false) {
                    // poll for new data
                    struct pollfd fds = {.fd = w_fd(s), .events = POLLIN};
                    if (poll(&fds, 1, -1) == -1)
                        continue;
                }

                // read new data
                w_nic_rx(w);
                i = w_rx(s);
                if (i)
                    len = w_iov_len(i);
            }

            struct timespec diff, now;
            assert(clock_gettime(CLOCK_REALTIME, &now) != -1, "clock_gettime");

            // stop the timeout
            const struct itimerval stop = {0};
            assert(setitimer(ITIMER_REAL, &stop, 0) == 0, "setitimer");

            warn(info, "received %d/%d byte%c", len, size, plural(len));

            if (unlikely(len < size)) {
                // assume loss
                w_rx_done(s);
                warn(warn, "incomplete response, packet loss?");
                continue;
            }

            // compute time difference
            time_diff(&diff, &now, i->buf);
            if (unlikely(diff.tv_sec != 0))
                die("time difference is more than %ld sec", diff.tv_sec);

            printf("%ld\t%d\n", diff.tv_nsec, size);
            w_rx_done(s);
        }
    }
    w_close(s);
    w_cleanup(w);
    return 0;
}
