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
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

// IWYU pragma: no_include <sys/queue.h>
#include <warpcore/warpcore.h>

#if defined(HAVE_SENDMMSG) || defined(HAVE_RECVMMSG)
#include <limits.h>
#include <sys/param.h>
#endif

#if defined(HAVE_KQUEUE)
#include <sys/event.h>
#include <time.h>
#elif defined(HAVE_EPOLL)
#include <sys/epoll.h>
#else
#include <poll.h>
#endif

#include "backend.h"
#include "ip.h"
#include "udp.h"


/// The backend name.
///
static char backend_name[] = "socket";


/// Initialize the warpcore socket backend for engine @p w. Sets up the extra
/// buffers.
///
/// @param      w       Backend engine.
/// @param[in]  ifname  The OS name of the interface (e.g., "eth0").
/// @param[in]  nbufs   Number of packet buffers to allocate.
///
void backend_init(struct w_engine * const w,
                  const char * const ifname,
                  const uint32_t nbufs)
{
    struct w_engine * ww;
    SLIST_FOREACH (ww, &engines, next)
        ensure(strncmp(ifname, ww->ifname, IFNAMSIZ),
               "can only have one warpcore engine active on %s", ifname);

    ensure((w->mem = calloc(nbufs, w->mtu)) != 0,
           "cannot alloc %u * %u buf mem", nbufs, w->mtu);
    ensure((w->bufs = calloc(nbufs, sizeof(*w->bufs))) != 0,
           "cannot alloc bufs");

    for (uint32_t i = 0; i < nbufs; i++) {
        w->bufs[i].buf = IDX2BUF(w, i);
        w->bufs[i].idx = i;
        STAILQ_INSERT_HEAD(&w->iov, &w->bufs[i], next);
    }

    w->ifname = strndup(ifname, IFNAMSIZ);
    ensure(w->ifname, "could not strndup");
    w->backend = backend_name;

#if defined(HAVE_KQUEUE)
    w->kq = kqueue();
#ifndef NDEBUG
    const char poll_meth[] = "kqueue";
#endif
#elif defined(HAVE_EPOLL)
    w->ep = epoll_create1(0);
    const char poll_meth[] = "epoll";
#else
    const char poll_meth[] = "poll";
#endif
#ifndef NDEBUG
#if defined(HAVE_SENDMMSG)
    const char send_meth[] = "sendmmsg";
#else
    const char send_meth[] = "sendmsg";
#endif
#if defined(HAVE_RECVMMSG)
    const char recv_meth[] = "recvmmsg";
#else
    const char recv_meth[] = "recvmsg";
#endif
    warn(debug, "backend using %s, %s, %s", poll_meth, send_meth, recv_meth);
#endif
}


/// Shut a warpcore socket engine down cleanly. Does nothing, at the moment.
///
/// @param      w     Backend engine.
///
void backend_cleanup(struct w_engine * const w)
{
    free(w->mem);
    free(w->ifname);
}


/// Bind a warpcore socket-backend socket. Calls the underlying Socket API.
///
/// @param      s     The w_sock to bind.
///
void backend_bind(struct w_sock * const s)
{
    ensure((s->fd = socket(AF_INET, SOCK_DGRAM, 0)) >= 0, "socket");
    ensure(setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, &(int){1},
                      sizeof(int)) >= 0,
           "cannot setsockopt SO_REUSEADDR");
    struct sockaddr_in addr = {.sin_family = AF_INET,
                               .sin_port = s->hdr->udp.sport,
                               .sin_addr = {.s_addr = s->hdr->ip.src}};
    ensure(bind(s->fd, (const struct sockaddr *)&addr, sizeof(addr)) == 0,
           "bind failed on port %u", ntohs(s->hdr->udp.sport));

    // if we're binding to a random port, find out what it is
    if (s->hdr->udp.sport == 0) {
        socklen_t len = sizeof(addr);
        ensure(getsockname(s->fd, (struct sockaddr *)&addr, &len) >= 0,
               "getsockname");
        s->hdr->udp.sport = addr.sin_port;
    }

#if defined(HAVE_KQUEUE)
    struct kevent ev;
    EV_SET(&ev, s->fd, EVFILT_READ, EV_ADD, 0, 0, s);
    ensure(kevent(s->w->kq, &ev, 1, 0, 0, 0) != -1, "kevent");
#elif defined(HAVE_EPOLL)
    struct epoll_event ev = {.events = EPOLLIN, .data.ptr = s};
    ensure(epoll_ctl(s->w->ep, EPOLL_CTL_ADD, s->fd, &ev) != -1, "epoll_ctl");
#endif
}


/// The socket backend performs no operation here.
///
/// @param      s     The w_sock to connect.
///
void backend_connect(struct w_sock * const s __attribute__((unused)))
{
}


/// Close the socket.
///
/// @param      s     The w_sock to close.
///
void backend_close(struct w_sock * const s)
{
    ensure(close(s->fd) == 0, "close");
}


/// Return the file descriptor associated with a w_sock. For the socket backend,
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
/// over w_sock @p s. This backend uses the Socket API.
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
        ensure(w_connected(s) || v->ip && v->port,
               "no destination information");
#ifdef HAVE_SENDMMSG
        // for sendmmsg, we populate the parameters
        msg[i] = (struct iovec){.iov_base = v->buf, .iov_len = v->len};
        // if w_sock is disconnected, use destination IP and port from w_iov
        // instead of the one in the template header
        dst[i] = (struct sockaddr_in){
            .sin_family = AF_INET,
            .sin_port = w_connected(s) ? s->hdr->udp.dport : v->port,
            .sin_addr = {w_connected(s) ? s->hdr->ip.dst : v->ip}};
        msgvec[i].msg_hdr = (struct msghdr){.msg_name = &dst[i],
                                            .msg_namelen = sizeof(dst[i]),
                                            .msg_iov = &msg[i],
                                            .msg_iovlen = 1};
        if (unlikely(++i == SEND_SIZE || STAILQ_NEXT(v, next) == 0)) {
            // the iov is full, or we are at the last w_iov, so send
            const ssize_t r = sendmmsg(s->fd, msgvec, (unsigned int)i, 0);
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
            .sin_port = w_connected(s) ? s->hdr->udp.dport : v->port,
            .sin_addr = {w_connected(s) ? s->hdr->ip.dst : v->ip}};
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
        int nbufs = 0;
        for (int j = 0; likely(j < RECV_SIZE); j++, nbufs++) {
            v[j] = w_alloc_iov(s->w, 0);
            if (unlikely(v[j] == 0))
                break;
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
        if (unlikely(nbufs == 0)) {
            warn(crit, "no more bufs");
            return;
        }
#ifdef HAVE_RECVMMSG
        n = recvmmsg(s->fd, msgvec, nbufs, MSG_DONTWAIT, 0);
        ensure(n != -1 || errno == EAGAIN, "recvmmsg");
#else
        n = recvmsg(s->fd, msgvec, MSG_DONTWAIT);
        ensure(n != -1 || errno == EAGAIN, "recvmsg");
#endif
        if (likely(n > 0)) {
            for (int j = 0; likely(j < nbufs); j++) {
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
        for (ssize_t j = n; likely(j < nbufs); j++)
            STAILQ_INSERT_HEAD(&s->w->iov, v[j], next);
    } while (n == RECV_SIZE);
}


/// The sock backend performs no operation here.
///
/// @param[in]  w     Backend engine.
///
void w_nic_tx(struct w_engine * const w __attribute__((unused)))
{
}


/// Check/wait until any data has been received.
///
/// @param[in]  w     Backend engine.
/// @param[in]  msec  Timeout in milliseconds. Pass zero for immediate return,
///                   -1 for infinite wait.
///
/// @return     Whether any data is ready for reading.
///
bool w_nic_rx(struct w_engine * const w, const int32_t msec)
{
    int n = 0;

#if defined(HAVE_KQUEUE)
    const struct timespec timeout = {msec / 1000000, (msec % 1000000) * 1000};
    struct kevent ev;
    n = kevent(w->kq, 0, 0, &ev, 1, msec == -1 ? 0 : &timeout);

#elif defined(HAVE_EPOLL)
    struct epoll_event ev;
    n = epoll_wait(w->ep, &ev, 1, msec);

#else
    // XXX: this is super-duper inefficient, but just a fallback

    // count sockets
    struct w_sock * s = 0;
    SLIST_FOREACH (s, &w->sock, next)
        n++;
    if (n == 0)
        return false;

    // allocate and fill pollfd
    struct pollfd * fds = calloc((unsigned long)n, sizeof(*fds));
    ensure(fds, "could not calloc");
    s = SLIST_FIRST(&w->sock);
    for (int i = 0; i < n; i++) {
        fds[i] = (struct pollfd){.fd = s->fd, .events = POLLIN};
        s = SLIST_NEXT(s, next);
    }

    // poll
    n = poll(fds, (nfds_t)n, msec);
    free(fds);
#endif

    return n > 0;
}


/// Fill a w_sock_slist with pointers to some sockets with pending inbound data.
/// Data can be obtained via w_rx() on each w_sock in the list. Call can
/// optionally block to wait for at least one ready connection. Will return the
/// number of ready connections, or zero if none are ready. When the return
/// value is not zero, a repeated call may return additional ready sockets.
///
/// @param[in]  w     Backend engine.
/// @param      sl    Empty and initialized w_sock_slist.
///
/// @return     Number of connections that are ready for reading.
///
uint32_t w_rx_ready(struct w_engine * const w, struct w_sock_slist * const sl)
{
    uint32_t n = 0;

#if defined(HAVE_KQUEUE)
#define EV_SIZE 64
    const struct timespec timeout = {0, 0};
    struct kevent ev[EV_SIZE];
    const int r = kevent(w->kq, 0, 0, &ev[0], EV_SIZE, &timeout);
    n = r >= 0 ? (uint32_t)r : 0;
    for (uint32_t i = 0; i < n; i++)
        SLIST_INSERT_HEAD(sl, (struct w_sock *)ev[i].udata, next_rx);

#elif defined(HAVE_EPOLL)
#define EV_SIZE 64
    struct epoll_event ev[EV_SIZE];
    const int r = epoll_wait(w->ep, &ev[0], EV_SIZE, 0);
    n = r >= 0 ? (uint32_t)r : 0;
    for (uint32_t i = 0; i < n; i++)
        SLIST_INSERT_HEAD(sl, (struct w_sock *)ev[i].data.ptr, next_rx);

#else
    // XXX: this is super-duper inefficient, but just a fallback

    // count sockets
    unsigned long sock_cnt = 0;
    struct w_sock * s = 0;
    SLIST_FOREACH (s, &w->sock, next)
        sock_cnt++;
    if (sock_cnt == 0)
        return 0;

    // allocate and fill pollfd
    struct pollfd * fds = calloc(sock_cnt, sizeof(*fds));
    struct w_sock ** ss = calloc(sock_cnt, sizeof(*ss));
    ensure(fds && ss, "could not calloc");
    s = SLIST_FIRST(&w->sock);
    for (uint32_t i = 0; i < sock_cnt; i++) {
        fds[i] = (struct pollfd){.fd = s->fd, .events = POLLIN};
        ss[i] = s;
        s = SLIST_NEXT(s, next);
    }

    // find ready descriptors
    poll(fds, (nfds_t)sock_cnt, 0);
    for (uint32_t i = 0; i < sock_cnt; i++) {
        if (fds[i].revents & POLLIN) {
            SLIST_INSERT_HEAD(sl, ss[i], next_rx);
            n++;
        }
    }
    free(ss);
    free(fds);
#endif

    return n;
}
