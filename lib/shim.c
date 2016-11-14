#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <sys/socket.h>

#include "plat.h"
#include "util.h"
#include "version.h"
#include "warpcore_internal.h"


#define IOV_COUNT 256
#define IOV_BUF_LEN 2048

static uint8_t iov_buf[IOV_COUNT][IOV_BUF_LEN];

static struct w_iov iov_v[IOV_COUNT];

static STAILQ_HEAD(iov, w_iov) iov = STAILQ_HEAD_INITIALIZER(iov);

static struct w_sock ** udp;


struct warpcore * __attribute__((nonnull))
w_init(const char * const ifname, const uint32_t rip __attribute__((unused)))
{
    // initialize random generator
    plat_srandom();

    // Set CPU affinity to one core
    plat_setaffinity();

    // lock memory
    // assert(mlockall(MCL_CURRENT | MCL_FUTURE) != -1, "mlockall");

    for (uint32_t i = 0; i < IOV_COUNT; i++) {
        iov_v[i] = (struct w_iov){.buf = iov_buf[i], .idx = i, .len = 0};
        STAILQ_INSERT_TAIL(&iov, &iov_v[i], next);
    }

    // allocate socket pointers
    assert((udp = calloc(UINT16_MAX, sizeof(struct w_sock *))) != 0,
           "cannot allocate UDP sockets");

    warn(info, "%s shim %s on %s (ignored) ready", warpcore_name,
         warpcore_version, ifname);
    return 0;
}


void w_init_common(void)
{
}


void __attribute__((nonnull))
w_cleanup(struct warpcore * const w __attribute__((unused)))
{
    free(udp);
    warn(warn, "here");
}


struct w_sock * __attribute__((nonnull))
w_bind(struct warpcore * const w __attribute__((unused)),
       const uint8_t p,
       const uint16_t port)
{
    assert(p == IP_P_UDP, "unhandled IP proto %d", p);

    struct w_sock ** const s = &udp[port];
    if (s && *s) {
        warn(warn, "IP proto %d source port %d already in use", p, ntohs(port));
        return 0;
    }

    assert((*s = calloc(1, sizeof(struct w_sock))) != 0,
           "cannot allocate struct w_sock");
    STAILQ_INIT(&(*s)->iv);
    STAILQ_INIT(&(*s)->ov);

    assert((*s)->fd = socket(PF_INET, SOCK_DGRAM, 0), "socket");
    assert(fcntl((*s)->fd, F_SETFL, O_NONBLOCK) != -1, "fcntl");

    // make a sockaddr
    struct sockaddr_in addr = {.sin_family = AF_INET,
                               .sin_port = port,
                               .sin_addr = {.s_addr = htonl(INADDR_ANY)}};
    assert(bind((*s)->fd, (struct sockaddr *)&addr, sizeof(addr)) == 0, "bind");

    warn(notice, "IP proto %d socket bound to port %d", p, ntohs(port));
    return *s;
}


void __attribute__((nonnull))
w_connect(struct w_sock * const s, const uint32_t ip, const uint16_t port)
{
    // make a sockaddr
    struct sockaddr_in addr = {
        .sin_family = AF_INET, .sin_port = port, .sin_addr = {ip}};
    assert(connect(s->fd, (struct sockaddr *)&addr, sizeof(addr)) != -1,
           "socket");
}


void __attribute__((nonnull)) w_close(struct w_sock * const s)
{
    // make iovs of the socket available again
    while (!STAILQ_EMPTY(&s->iv)) {
        struct w_iov * const v = STAILQ_FIRST(&s->iv);
        warn(debug, "free iv buf %u", v->idx);
        STAILQ_REMOVE_HEAD(&s->iv, next);
        STAILQ_INSERT_HEAD(&s->w->iov, v, next);
    }
    while (!STAILQ_EMPTY(&s->ov)) {
        struct w_iov * const v = STAILQ_FIRST(&s->ov);
        warn(debug, "free ov buf %u", v->idx);
        STAILQ_REMOVE_HEAD(&s->ov, next);
        STAILQ_INSERT_HEAD(&s->w->iov, v, next);
    }

    // free the socket
    free(s);
}


struct w_iov * __attribute__((nonnull))
w_tx_alloc(struct w_sock * const s, const uint32_t len)
{
    assert(len, "len is zero");

    // add enough buffers to the iov so it is > len
    struct w_iov * v = 0;
    int32_t l = (int32_t)len;
    uint32_t n = 0;
    while (l > 0) {
        // grab a spare buffer
        v = STAILQ_FIRST(&iov);
        assert(v != 0, "out of spare bufs after grabbing %d", n);
        STAILQ_REMOVE_HEAD(&iov, next);
        // warn(debug, "grabbing spare buf %u for user tx", v->idx);
        v->len = 1500 - sizeof(s->hdr); // XXX correct?
        l -= v->len;
        n++;

        // add the iov to the tail of the result
        STAILQ_INSERT_TAIL(&s->ov, v, next);
    }
    // adjust length of last iov so chain is the exact length requested
    v->len += l; // l is negative

    warn(info, "allocating iov (len %d in %d bufs) for user tx", len, n);
    return STAILQ_FIRST(&s->ov);
}


int __attribute__((nonnull)) w_fd(struct w_sock * const s)
{
    return s->fd;
}


void __attribute__((nonnull)) w_rx_done(struct w_sock * const s)
{
    struct w_iov * i = STAILQ_FIRST(&s->iv);
    while (i) {
        // move i from the socket to the available iov list
        struct w_iov * const n = STAILQ_NEXT(i, next);
        STAILQ_REMOVE_HEAD(&s->iv, next);
        STAILQ_INSERT_HEAD(&iov, i, next);
        i = n;
    }
}


struct w_iov * __attribute__((nonnull)) w_rx(struct w_sock * const s)
{
    while (1) {
        // grab a spare buffer
        struct w_iov * const v = STAILQ_FIRST(&iov);
        assert(v != 0, "out of spare bufs");
        struct sockaddr_in peer;
        socklen_t plen = sizeof(peer);
        const ssize_t n = recvfrom(s->fd, v->buf, IOV_BUF_LEN, 0,
                                   (struct sockaddr *)&peer, &plen);
        assert(n != -1 || errno == EAGAIN, "recv");
        if (n == -1)
            // if there is no (more) data, simply return the current iv
            return STAILQ_FIRST(&s->iv);

        // add the iov to the tail of the result
        // warn(debug, "grabbing spare buf %u for user rx", v->idx);
        STAILQ_REMOVE_HEAD(&iov, next);
        STAILQ_INSERT_TAIL(&s->iv, v, next);

        // store the length and other info
        v->len = (uint16_t)n;
        v->src = peer.sin_addr.s_addr;
        v->sport = peer.sin_port;
        // warn(warn, "placed %d byte%c into iov %d", v->len, plural(n),
        // v->idx);
    }
}


void __attribute__((nonnull))
w_kick_tx(const struct warpcore * const w __attribute__((unused)))
{
}


void __attribute__((nonnull))
w_kick_rx(const struct warpcore * const w __attribute__((unused)))
{
}


void __attribute__((nonnull)) w_tx(struct w_sock * const s)
{
    while (likely(!STAILQ_EMPTY(&s->ov))) {
        struct w_iov * const v = STAILQ_FIRST(&s->ov);
        const ssize_t n = send(s->fd, v->buf, v->len, 0);
        warn(debug, "sent %zu byte%c from buf %d", n, plural(n), v->idx);
        assert(n == v->len, "send");
        STAILQ_REMOVE_HEAD(&s->ov, next);
        STAILQ_INSERT_HEAD(&iov, v, next);
    }
    w_kick_tx(s->w);
}
