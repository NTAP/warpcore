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
#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>

#include <warpcore/warpcore.h>

#if defined(HAVE_SENDMMSG) || defined(HAVE_RECVMMSG)
// IWYU pragma: no_include "warpcore/config.h"
#include <limits.h>
#include <sys/param.h>
#endif

#if defined(HAVE_KQUEUE)
// IWYU pragma: no_include "warpcore/config.h"
#include <sys/event.h>
#include <time.h>
#elif defined(HAVE_EPOLL)
#include <sys/epoll.h>
#endif

#include "backend.h"
#include "ip.h"
#include "udp.h"


/// The backend name.
///
static char backend_name[] = "shim";


/// Initialize the warpcore shim backend for engine @p w. Sets up the extra
/// buffers.
///
/// @param      w       Backend engine.
/// @param[in]  ifname  The OS name of the interface (e.g., "eth0").
///
void backend_init(struct w_engine * w, const char * const ifname)
{
    struct w_engine * ww;
    SLIST_FOREACH (ww, &engines, next)
        ensure(strncmp(ifname, ww->ifname, IFNAMSIZ),
               "can only have one warpcore engine active on %s", ifname);

    ensure((w->mem = calloc(NUM_BUFS, IOV_BUF_LEN)) != 0,
           "cannot alloc buf mem");
    ensure((w->bufs = calloc(NUM_BUFS, sizeof(*w->bufs))) != 0,
           "cannot alloc bufs");

    for (uint32_t i = 0; i < NUM_BUFS; i++) {
        w->bufs[i].buf = IDX2BUF(w, i);
        w->bufs[i].idx = i;
        STAILQ_INSERT_HEAD(&w->iov, &w->bufs[i], next);
    }

    w->ifname = strndup(ifname, IFNAMSIZ);
    ensure(w->ifname, "could not strndup");
    w->backend = backend_name;
#if defined(HAVE_KQUEUE)
    w->kq = kqueue();
#elif defined(HAVE_EPOLL)
    w->ep = epoll_create1(0);
#endif
}


/// Shut a warpcore shim engine down cleanly. Does nothing, at the moment.
///
/// @param      w     Backend engine.
///
void backend_cleanup(struct w_engine * const w)
{
    STAILQ_INIT(&w->iov);
    free(w->mem);
    free(w->ifname);
}


/// Bind a warpcore shim socket. Calls the underlying Socket API.
///
/// @param      s     The w_sock to bind.
///
void backend_bind(struct w_sock * s)
{
    ensure((s->fd = socket(AF_INET, SOCK_DGRAM, 0)) >= 0, "socket");
    ensure(setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, &(int){1},
                      sizeof(int)) >= 0,
           "cannot setsockopt SO_REUSEADDR");
    const struct sockaddr_in addr = {.sin_family = AF_INET,
                                     .sin_port = s->hdr->udp.sport,
                                     .sin_addr = {.s_addr = s->hdr->ip.src}};
    ensure(bind(s->fd, (const struct sockaddr *)&addr, sizeof(addr)) == 0,
           "bind failed on port %u", ntohs(s->hdr->udp.sport));

#if defined(HAVE_KQUEUE)
    struct kevent ev;
    EV_SET(&ev, s->fd, EVFILT_READ, EV_ADD, 0, 0, s);
    ensure(kevent(s->w->kq, &ev, 1, 0, 0, 0) != -1, "kevent");
#elif defined(HAVE_EPOLL)
    struct epoll_event ev = {.events = EPOLLIN, .data.ptr = s};
    ensure(epoll_ctl(s->w->ep, EPOLL_CTL_ADD, s->fd, &ev) != -1, "epoll_ctl");
#endif
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
int w_fd(const struct w_sock * const s)
{
    return s->fd;
}


/// Loops over the w_iov structures in the tail queue @p o, sending them all
/// over
/// w_sock @p s. This backend uses the Socket API.
///
/// @param      s     w_sock socket to transmit over.
/// @param      o     w_iov_stailq to send.
///
void w_tx(const struct w_sock * const s, struct w_iov_stailq * const o)
{
#ifdef HAVE_SENDMMSG
// There is a tradeoff here in terms of how many messages we should try and
// send. Preparing to handle longer sizes has preparation overheads, whereas
// only handling shorter sizes may require multiple syscalls (and incur their
// overheads). So we're picking a number out of a hat. We could allocate
// dynamically for MAX(IOV_MAX, w_iov_stailq_cnt(c)), but that seems overkill.
#define SEND_SIZE MIN(16, IOV_MAX)
    struct mmsghdr msgvec[SEND_SIZE];
    struct iovec msg[SEND_SIZE];
    struct sockaddr_in dst[SEND_SIZE];
    size_t i = 0;
#endif
    o->tx_pending = 0; // blocking I/O, no need to update o->tx_pending
    const struct w_iov * v;
    STAILQ_FOREACH (v, o, next) {
        ensure(s->hdr->ip.dst && s->hdr->udp.dport || v->ip && v->port,
               "no destination information");
#ifdef HAVE_SENDMMSG
        // for sendmmsg, we populate the parameters
        msg[i] = (struct iovec){.iov_base = v->buf, .iov_len = v->len};
        // if w_sock is disconnected, use destination IP and port from w_iov
        // instead of the one in the template header
        dst[i] = (struct sockaddr_in){
            .sin_family = AF_INET,
            .sin_port = s->hdr->ip.dst ? s->hdr->udp.dport : v->port,
            .sin_addr = {s->hdr->ip.dst ? s->hdr->ip.dst : v->ip}};
        msgvec[i].msg_hdr = (struct msghdr){.msg_name = &dst[i],
                                            .msg_namelen = sizeof(dst[i]),
                                            .msg_iov = &msg[i],
                                            .msg_iovlen = 1};
        if (unlikely(++i == SEND_SIZE || STAILQ_NEXT(v, next) == 0)) {
            // the iov is full, or we are at the last w_iov, so send
            const ssize_t r = sendmmsg(s->fd, msgvec, i, 0);
            ensure(r == (ssize_t)i, "sendmmsg %zu %zu", i, r);
            // reuse the iov structure for the next batch
            i = 0;
        }
#else
        // without sendmmsg, send one packet directly
        // if w_sock is disconnected, use destination IP and port from w_iov
        // instead of the one in the template header
        const struct sockaddr_in dst = {
            .sin_family = AF_INET,
            .sin_port = s->hdr->ip.dst ? s->hdr->udp.dport : v->port,
            .sin_addr = {s->hdr->ip.dst ? s->hdr->ip.dst : v->ip}};
        sendto(s->fd, v->buf, v->len, 0, (const struct sockaddr *)&dst,
               sizeof(dst));
#endif
    }
}


/// Calls recvmsg() or recvmmsg() for all sockets associated with the engine,
/// emulating the operation of netmap backend_rx() function. Appends all data to
/// the w_sock::iv socket buffers of the respective w_sock structures.
///
/// @param      s     w_sock for which the application would like to receive new
///                   data.
/// @param      i     w_iov tail queue to append new data to.
///
void w_rx(struct w_sock * const s, struct w_iov_stailq * const i)
{
#ifdef HAVE_RECVMMSG
// There is a tradeoff here in terms of how many messages we should try and
// receive. Preparing to handle longer sizes has preparation overheads, whereas
// only handling shorter sizes may require multiple syscalls (and incur their
// overheads). So we're picking a number out of a hat.
#define RECV_SIZE MIN(16, IOV_MAX)
#else
#define RECV_SIZE 1
#endif
    ssize_t n = 0;
    do {
        struct sockaddr_in peer[RECV_SIZE];
        struct w_iov * v[RECV_SIZE];
        struct iovec msg[RECV_SIZE];
#ifdef HAVE_RECVMMSG
        struct mmsghdr msgvec[RECV_SIZE];
#else
        struct msghdr msgvec[RECV_SIZE];
#endif
        for (int j = 0; likely(j < RECV_SIZE); j++) {
            v[j] = alloc_iov(s->w);
            msg[j] =
                (struct iovec){.iov_base = v[j]->buf, .iov_len = v[j]->len};
#ifdef HAVE_RECVMMSG
            msgvec[j].msg_hdr =
#else
            msgvec[j] =
#endif
                (struct msghdr){.msg_name = &peer[j],
                                .msg_namelen = sizeof(peer[j]),
                                .msg_iov = &msg[j],
                                .msg_iovlen = 1};
        }
#ifdef HAVE_RECVMMSG
        n = recvmmsg(s->fd, msgvec, RECV_SIZE, MSG_DONTWAIT, 0);
        ensure(n != -1 || errno == EAGAIN, "recvmmsg");
#else

        n = recvmsg(s->fd, msgvec, MSG_DONTWAIT);
        ensure(n != -1 || errno == EAGAIN, "recvmsg");
#endif
        if (n > 0) {
            for (ssize_t j = 0; likely(j < n); j++) {
#ifdef HAVE_RECVMMSG
                v[j]->len = (uint16_t)msgvec[j].msg_len;
#else
                v[j]->len = (uint16_t)n;
                // recvmsg returns number of bytes, we need number of
                // messages for the return loop below
                n = 1;
#endif
                v[j]->ip = peer[j].sin_addr.s_addr;
                v[j]->port = peer[j].sin_port;
                v[j]->flags = 0;
                // add the iov to the tail of the result
                STAILQ_INSERT_TAIL(i, v[j], next);
            }
        } else
            // in case EAGAIN was returned (n == -1)
            n = 0;

        // return any unused buffers
        for (ssize_t j = n; likely(j < RECV_SIZE); j++)
            STAILQ_INSERT_HEAD(&s->w->iov, v[j], next);
    } while (n > 0);
}


/// The shim backend performs no operation here.
///
/// @param[in]  w     Backend engine.
///
void w_nic_tx(struct w_engine * const w __attribute__((unused)))
{
}


/// The shim backend performs no operation here.
///
/// @param[in]  w     Backend engine.
///
void w_nic_rx(struct w_engine * const w __attribute__((unused)))
{
}


/// Return a w_sock_slist containing all sockets with pending inbound data.
/// Caller needs to free() the returned value before the next call to
/// w_rx_ready(). Data can be obtained via w_rx() on each w_sock in the list.
///
/// @param[in]  w     Backend engine.
///
/// @return     List of w_sock sockets that have incoming data pending.
///
struct w_sock_slist * w_rx_ready(const struct w_engine * w)
{
    // make a new w_sock_slist
    struct w_sock_slist * sl = calloc(1, sizeof(*sl));
    ensure(sl, "calloc w_sock_slist");
    SLIST_INIT(sl);

    // insert all sockets with pending inbound data
#if defined(HAVE_KQUEUE)
#define EV_SIZE 10
    const struct timespec timeout = {0, 0};
    struct kevent ev[EV_SIZE];
    const int n = kevent(w->kq, 0, 0, &ev[0], EV_SIZE, &timeout);
    // ensure(n >= 0, "kevent");
    for (int i = 0; i < n; i++)
        SLIST_INSERT_HEAD(sl, (struct w_sock *)ev[i].udata, next_rx);

#elif defined(HAVE_EPOLL)
#define EV_SIZE 10
    struct epoll_event ev[EV_SIZE];
    const int n = epoll_wait(w->ep, &ev[0], EV_SIZE, 0);
    // ensure(n >= 0, "kevent");
    for (int i = 0; i < n; i++)
        SLIST_INSERT_HEAD(sl, (struct w_sock *)ev[i].data.ptr, next_rx);
#else
#error "TODO: standard poll() implementation"
#endif
    return sl;
}
