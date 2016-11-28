#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <sys/socket.h>

#include "backend.h"
#include "plat.h"
#include "util.h"
#include "version.h"


/// Length of a buffer. Same as netmap uses.
///
#define IOV_BUF_LEN 2048


/// The backend name.
///
static char backend_name[] = "shim";


/// Initialize the warpcore shim backend for engine @p w. Sets up the extra
/// buffers.
///
/// @param      w       Warpcore engine.
/// @param[in]  ifname  The OS name of the interface (e.g., "eth0").
///
void backend_init(struct warpcore * w,
                  const char * const ifname __attribute__((unused)))
{
    assert((w->mem = calloc(NUM_BUFS, IOV_BUF_LEN)) != 0, "cannot alloc bufs");

    for (uint32_t i = 0; i < NUM_BUFS; i++) {
        struct w_iov * const v = calloc(1, sizeof(struct w_iov));
        assert(v != 0, "cannot allocate w_iov");
        v->buf = IDX2BUF(w, i);
        v->idx = i;
        STAILQ_INSERT_HEAD(&w->iov, v, next);
    }

    w->backend = backend_name;
}


/// Shut a warpcore shim engine down cleanly. Does nothing, at the moment.
///
/// @param      w     Warpcore engine.
///
void backend_cleanup(struct warpcore * const w __attribute__((unused)))
{
}


/// Bind a warpcore shim socket. Calls the underlying Socket API.
///
/// @param      s     The w_sock to bind.
///
void backend_bind(struct w_sock * s)
{
    assert(s->fd = socket(AF_INET, SOCK_DGRAM, 0), "socket");
    assert(fcntl(s->fd, F_SETFL, O_NONBLOCK) != -1, "fcntl");
    const struct sockaddr_in addr = {.sin_family = AF_INET,
                                     .sin_port = s->hdr.udp.sport,
                                     .sin_addr = {.s_addr = s->hdr.ip.src}};
    assert(bind(s->fd, (const struct sockaddr *)&addr, sizeof(addr)) == 0,
           "bind");
}


/// The shim backend performs no operation here.
///
/// @param      s     The w_sock to connect.
///
void backend_connect(struct w_sock * const s __attribute__((unused)))
{
}


/// Return the file descriptor associated with a w_sock. For the shim backend,
/// this an OS file descriptor of the underlying socket. It can be used for
/// poll() or with event-loop libraries in the application.
///
/// @param      s     w_sock socket for which to get the underlying descriptor.
///
/// @return     A file descriptor.
///
int w_fd(struct w_sock * const s)
{
    return s->fd;
}


/// Calls recvfrom() until no more data is queued, appending all data to the
/// w_sock::iv socket buffers of the respective w_sock structures associated
/// with a given sender IPv4 address and port.
///
/// Unlike with the Socket API, w_rx() can append data to w_sock::iv chains
/// *other* that that of the w_sock passed as @p s. This is due to how the
/// netmap backend needs to do things. This means that although w_rx() may
/// return zero, because no new data has been received on @p s, it may enqueue
/// new data into the w_sock::iv chains of other w_sock socket.
///
/// @param      s     w_sock for which the application would like to receive new
///                   data.
///
/// @return     First w_iov in w_sock::iv if there is new data, or zero. Needs
///             to be freed with w_free() by the caller.
///
struct w_iov * w_rx(struct w_sock * const s)
{
    while (1) {
        // grab a spare buffer
        struct w_iov * v = STAILQ_FIRST(&s->w->iov);
        assert(v != 0, "out of spare bufs");
        struct sockaddr_in peer;
        socklen_t plen = sizeof(peer);
        const ssize_t n = recvfrom(s->fd, v->buf, IOV_BUF_LEN, 0,
                                   (struct sockaddr *)&peer, &plen);
        assert(n != -1 || errno == EAGAIN, "recv");
        if (n == -1) {
            // if there is no (more) data, simply return the current iv
            v = STAILQ_FIRST(&s->iv);
            STAILQ_INIT(&s->iv);
            return v;
        }

        // add the iov to the tail of the result
        STAILQ_REMOVE_HEAD(&s->w->iov, next);
        STAILQ_INSERT_TAIL(&s->iv, v, next);

        // store the length and other info
        v->len = (uint16_t)n;
        v->ip = peer.sin_addr.s_addr;
        v->port = peer.sin_port;
        v->flags = 0; // since we can't get TOS and ECN info from the kernel
        assert(gettimeofday(&v->ts, 0) == 0, "gettimeofday");
    }
}


/// Sends payloads from @p v using the Socket API. Not all payloads may be sent.
///
/// @param      s     w_sock socket to transmit over..
/// @param      v     w_iov chain to transmit.
///
void w_tx(const struct w_sock * const s, struct w_iov * const v)
{
    struct sockaddr_in addr = {.sin_family = AF_INET,
                               .sin_port = s->hdr.udp.dport,
                               .sin_addr = {s->hdr.ip.dst}};

    for (const struct w_iov * o = v; o; o = STAILQ_NEXT(o, next)) {
        // if w_sock is disconnected, use destination IP and port from w_iov
        // instead of the one in the template header
        if (s->hdr.ip.dst == 0 && s->hdr.udp.dport == 0) {
            addr.sin_port = o->port;
            addr.sin_addr.s_addr = o->ip;
        }

        const ssize_t n = sendto(s->fd, o->buf, o->len, 0,
                                 (const struct sockaddr *)&addr, sizeof(addr));
        if (n == -1) {
            warn(err, "could not send all data");
            return;
        }
        warn(debug, "sent %u byte%c from buf %d", o->len, plural(o->len),
             o->idx);
    }
}


/// The shim backend performs no operation here.
///
/// @param[in]  w     Warpcore engine.
///
void w_nic_rx(const struct warpcore * const w __attribute__((unused)))
{
}


/// The shim backend performs no operation here.
///
/// @param[in]  w     Warpcore engine.
///
void w_nic_tx(const struct warpcore * const w __attribute__((unused)))
{
}
