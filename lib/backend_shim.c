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

#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

#include <warpcore.h>

#include "backend.h"
#include "ip.h"
#include "udp.h"


/// The backend name.
///
static char backend_name[] = "shim";


/// Initialize the warpcore shim backend for engine @p w. Sets up the extra
/// buffers.
///
/// @param      w       Warpcore engine.
/// @param[in]  ifname  The OS name of the interface (e.g., "eth0").
///
void backend_init(struct warpcore * w, const char * const ifname)
{
    struct warpcore * ww;
    SLIST_FOREACH (ww, &engines, next)
        ensure(strncmp(ifname, ww->ifname, IFNAMSIZ),
               "can only have one warpcore engine active on %s", ifname);

    ensure((w->mem = calloc(NUM_BUFS, IOV_BUF_LEN)) != 0, "cannot alloc bufs");

    for (uint32_t i = 0; i < NUM_BUFS; i++) {
        struct w_iov * const v = calloc(1, sizeof(*v));
        ensure(v != 0, "cannot allocate w_iov");
        v->buf = IDX2BUF(w, i);
        v->idx = i;
        STAILQ_INSERT_HEAD(&w->iov, v, next);
    }

    w->ifname = strndup(ifname, IFNAMSIZ);
    ensure(w->ifname, "could not strndup");
    w->backend = backend_name;
}


/// Shut a warpcore shim engine down cleanly. Does nothing, at the moment.
///
/// @param      w     Warpcore engine.
///
void backend_cleanup(struct warpcore * const w)
{
    while (!STAILQ_EMPTY(&w->iov)) {
        struct w_iov * const v = STAILQ_FIRST(&w->iov);
        STAILQ_REMOVE_HEAD(&w->iov, next);
        free(v);
    }
    free(w->mem);
    free(w->ifname);
}


/// Bind a warpcore shim socket. Calls the underlying Socket API.
///
/// @param      s     The w_sock to bind.
///
void backend_bind(struct w_sock * s)
{
    ensure(s->fd = socket(AF_INET, SOCK_DGRAM, 0), "socket");
    // ensure(fcntl(s->fd, F_SETFL, O_NONBLOCK) != -1, "fcntl");
    const struct sockaddr_in addr = {.sin_family = AF_INET,
                                     .sin_port = s->hdr->udp.sport,
                                     .sin_addr = {.s_addr = s->hdr->ip.src}};
    ensure(bind(s->fd, (const struct sockaddr *)&addr, sizeof(addr)) == 0,
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


/// Loops over the w_iov structures in the chain @p c, sending them all over
/// w_sock @p s. This backend uses the Socket API.
///
/// @param      s     w_sock socket to transmit over.
/// @param      c     w_iov_chain to send.
///
void w_tx(const struct w_sock * const s, struct w_iov_chain * const c)
{
    struct w_iov * v;
    c->tx_pending = 0;
    STAILQ_FOREACH (v, c, next) {
        // since we use blocking I/O, there is no need to update c->tx_pending

        ensure(s->hdr->ip.dst && s->hdr->udp.dport || v->ip && v->port,
               "no destination information");

        // if w_sock is disconnected, use destination IP and port from w_iov
        // instead of the one in the template header
        const struct sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_port = s->hdr->ip.dst ? s->hdr->udp.dport : v->port,
            .sin_addr = {s->hdr->ip.dst ? s->hdr->ip.dst : v->ip}};

        sendto(s->fd, v->buf, v->len, 0, (const struct sockaddr *)&addr,
               sizeof(addr));
    }
}


/// Calls recvfrom() for all sockets associated with the engine, emulating the
/// operation of netmap backend_rx() function. Appends all data to the
/// w_sock::iv socket buffers of the respective w_sock structures.
///
/// @param[in]  w     Warpcore engine.
///
void w_nic_rx(struct warpcore * const w)
{
    const struct w_sock * s;
    SLIST_FOREACH (s, &w->sock, next) {
        ssize_t n = 0;
        do {
            // grab a spare buffer
            struct w_iov * const v = alloc_iov(w);
            struct sockaddr_in peer;
            socklen_t plen = sizeof(peer);
            n = recvfrom(s->fd, v->buf, IOV_BUF_LEN, MSG_DONTWAIT,
                         (struct sockaddr *)&peer, &plen);
            ensure(n != -1 || errno == EAGAIN, "recv");
            if (n > 0) {
                // add the iov to the tail of the result
                STAILQ_INSERT_TAIL(s->iv, v, next);

                // store the length and other info
                v->len = (uint16_t)n;
                v->ip = peer.sin_addr.s_addr;
                v->port = peer.sin_port;
                v->flags = 0; // can't get TOS and ECN info from the kernel
                ensure(gettimeofday(&v->ts, 0) == 0, "gettimeofday");
            } else
                // we didn't need this iov after all
                STAILQ_INSERT_HEAD(&s->w->iov, v, next);

        } while (n > 0);
    }
}


/// The shim backend performs no operation here.
///
/// @param[in]  w     Warpcore engine.
///
void w_nic_tx(struct warpcore * const w __attribute__((unused)))
{
}
