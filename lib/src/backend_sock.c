// SPDX-License-Identifier: BSD-2-Clause
//
// Copyright (c) 2014-2019, NetApp, Inc.
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

#if defined(__linux__)
#include <limits.h>
#endif

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include <warpcore/warpcore.h>

#if !defined(HAVE_KQUEUE) && !defined(HAVE_EPOLL)
#include <khash.h>
#endif

#ifdef HAVE_ASAN
#include <sanitizer/asan_interface.h>
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


#ifdef __linux__
#define IPTOS_ECN_NOTECT IPTOS_ECN_NOT_ECT
#endif


/// The backend name.
///
static char backend_name[] = "socket";


void w_set_sockopt(struct w_sock * const s, const struct w_sockopt * const opt)
{
    if (s->opt.enable_udp_zero_checksums != opt->enable_udp_zero_checksums) {
        s->opt.enable_udp_zero_checksums = opt->enable_udp_zero_checksums;
#ifdef __linux__
        ensure(setsockopt(s->fd, SOL_SOCKET, SO_NO_CHECK,
                          &(int){s->opt.enable_udp_zero_checksums},
                          sizeof(int)) >= 0,
               "cannot setsockopt SO_NO_CHECK");
#else
        ensure(setsockopt(s->fd, IPPROTO_UDP, UDP_NOCKSUM,
                          &(int){s->opt.enable_udp_zero_checksums},
                          sizeof(int)) >= 0,
               "cannot setsockopt UDP_NOCKSUM");
#endif
    }

    if (s->opt.enable_ecn != opt->enable_ecn) {
        s->opt.enable_ecn = opt->enable_ecn;
        ensure(setsockopt(s->fd, IPPROTO_IP, IP_TOS,
                          &(int){s->opt.enable_ecn ? IPTOS_ECN_ECT0
                                                   : IPTOS_ECN_NOTECT},
                          sizeof(int)) >= 0,
               "cannot setsockopt IP_TOS");
    }
}


/// Initialize the warpcore socket backend for engine @p w. Sets up the extra
/// buffers.
///
/// @param      w        Backend engine.
/// @param[in]  nbufs    Number of packet buffers to allocate.
/// @param[in]  is_lo    Unused.
/// @param[in]  is_left  Unused.
///
void backend_init(struct w_engine * const w,
                  const uint32_t nbufs,
                  const bool is_lo __attribute__((unused)),
                  const bool is_left __attribute__((unused)))
{
    // lower the MTU to account for IP and UPD headers
    w->mtu -= sizeof(struct ip_hdr) + sizeof(struct udp_hdr);

    ensure((w->mem = calloc(nbufs, w->mtu)) != 0,
           "cannot alloc %u * %u buf mem", nbufs, w->mtu);
    ensure((w->bufs = calloc(nbufs, sizeof(*w->bufs))) != 0,
           "cannot alloc bufs");
    w->backend_name = backend_name;

    for (uint32_t i = 0; i < nbufs; i++) {
        w->bufs[i].idx = i;
        init_iov(w, &w->bufs[i]);
        sq_insert_head(&w->iov, &w->bufs[i], next);
        ASAN_POISON_MEMORY_REGION(w->bufs[i].buf, w->mtu);
    }

#if defined(HAVE_KQUEUE)
    w->b->kq = kqueue();
#ifndef NDEBUG
    const char poll_meth[] = "kqueue";
#endif

#elif defined(HAVE_EPOLL)
    w->b->ep = epoll_create1(0);
#ifndef NDEBUG
    const char poll_meth[] = "epoll";
#endif

#else
#ifndef NDEBUG
    const char poll_meth[] = "poll";
#endif
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
    warn(DBG, "backend using %s, %s, %s", poll_meth, send_meth, recv_meth);
#endif
}


/// Shut a warpcore socket engine down cleanly. Does nothing, at the moment.
///
/// @param      w     Backend engine.
///
void backend_cleanup(struct w_engine * const w)
{
#if !defined(HAVE_KQUEUE) && !defined(HAVE_EPOLL)
    free(w->b->fds);
    free(w->b->socks);
#endif
    free(w->mem);
    free(w->bufs);
}


/// Bind a warpcore socket-backend socket. Calls the underlying Socket API.
///
/// @param      s     The w_sock to bind.
/// @param[in]  opt   Socket options for this socket. Can be zero.
///
void backend_bind(struct w_sock * const s, const struct w_sockopt * const opt)
{
    ensure((s->fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0)) >= 0,
           "socket");
    ensure(setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, &(int){1},
                      sizeof(int)) >= 0,
           "cannot setsockopt SO_REUSEADDR");
    struct sockaddr_in addr = {.sin_family = AF_INET,
                               .sin_port = s->hdr->udp.sport,
                               .sin_addr = {.s_addr = s->hdr->ip.src}};
    ensure(bind(s->fd, (const struct sockaddr *)&addr, sizeof(addr)) == 0,
           "bind failed on %s:%u", inet_ntoa(addr.sin_addr),
           ntohs(s->hdr->udp.sport));

    // always enable receiving TOS information
    ensure(setsockopt(s->fd, IPPROTO_IP, IP_RECVTOS, &(int){1}, sizeof(int)) >=
               0,
           "cannot setsockopt IP_RECVTOS");

    if (opt)
        w_set_sockopt(s, opt);

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
    ensure(kevent(s->w->b->kq, &ev, 1, 0, 0, 0) != -1, "kevent");
#elif defined(HAVE_EPOLL)
    struct epoll_event ev = {.events = EPOLLIN, .data.ptr = s};
    ensure(epoll_ctl(s->w->b->ep, EPOLL_CTL_ADD, s->fd, &ev) != -1,
           "epoll_ctl");
#endif
}


/// The socket backend performs no operation here.
///
/// @param      s     The w_sock to connect.
///
void backend_connect(struct w_sock * const s)
{
    struct sockaddr_in addr = {.sin_family = AF_INET,
                               .sin_port = s->hdr->udp.dport,
                               .sin_addr = {.s_addr = s->hdr->ip.dst}};
    ensure(connect(s->fd, (const struct sockaddr *)&addr, sizeof(addr)) == 0,
           "connect failed to %s:%u", inet_ntoa(addr.sin_addr),
           ntohs(s->hdr->udp.dport));
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
/// @param      o     w_iov_sq to send.
///
void w_tx(const struct w_sock * const s, struct w_iov_sq * const o)
{
#ifdef HAVE_SENDMMSG
// There is a tradeoff here in terms of how many messages we should try and
// send. Preparing to handle longer sizes has preparation overheads, whereas
// only handling shorter sizes may require multiple syscalls (and incur their
// overheads). So we're picking a number out of a hat. We could allocate
// dynamically for MAX(IOV_MAX, w_iov_sq_cnt(c)), but that seems overkill.
#define SEND_SIZE MIN(16, IOV_MAX)
    struct mmsghdr msgvec[SEND_SIZE];
#else
#define SEND_SIZE 1
    struct msghdr msgvec[SEND_SIZE];
#endif
    struct iovec msg[SEND_SIZE];
    struct sockaddr_in dst[SEND_SIZE];
#ifdef __linux__
    // kernels below 4.9 can't deal with getting an uint8_t passed in, sigh
    __extension__ uint8_t ctrl[SEND_SIZE][CMSG_SPACE(sizeof(int))];
#else
    __extension__ uint8_t ctrl[SEND_SIZE][CMSG_SPACE(sizeof(uint8_t))];
#endif
    o->tx_pending = 0; // blocking I/O, no need to update o->tx_pending

    struct w_iov * v = sq_first(o);
    do {
        size_t i;
        for (i = 0; i < SEND_SIZE && v; i++) {

            // for sendmmsg, we populate the parameters
            msg[i] = (struct iovec){.iov_base = v->buf, .iov_len = v->len};
            // if w_sock is disconnected, use destination IP and port from w_iov
            // instead of the one in the template header
            if (w_connected(s) == false) {
                ensure(v->ip && v->port, "no destination information");
                dst[i] = (struct sockaddr_in){.sin_family = AF_INET,
                                              .sin_port = v->port,
                                              .sin_addr = {v->ip}};
            } else {
                v->ip = s->hdr->ip.dst;
                v->port = s->hdr->udp.dport;
            }
#ifdef HAVE_SENDMMSG
            msgvec[i].msg_hdr =
#else
            msgvec[i] =
#endif
                (struct msghdr){.msg_name = w_connected(s) ? 0 : &dst[i],
                                .msg_namelen =
                                    w_connected(s) ? 0 : sizeof(dst[i]),
                                .msg_iov = &msg[i],
                                .msg_iovlen = 1};

            // set TOS from w_iov
            if (v->flags) {
#ifdef HAVE_SENDMMSG
                msgvec[i].msg_hdr.msg_control = &ctrl[i];
                msgvec[i].msg_hdr.msg_controllen = sizeof(ctrl[i]);
                struct cmsghdr * const cmsg = CMSG_FIRSTHDR(&msgvec[i].msg_hdr);
#else
                msgvec[i].msg_control = &ctrl[i];
                msgvec[i].msg_controllen = sizeof(ctrl[i]);
                struct cmsghdr * const cmsg = CMSG_FIRSTHDR(&msgvec[i]);
#endif
                cmsg->cmsg_level = IPPROTO_IP;
                cmsg->cmsg_type = IP_TOS;
#ifdef __linux__
                cmsg->cmsg_len = CMSG_LEN(sizeof(int));
                *(int *)(void *)CMSG_DATA(cmsg) = v->flags;
#else
                cmsg->cmsg_len = CMSG_LEN(sizeof(uint8_t));
                *(uint8_t *)CMSG_DATA(cmsg) = v->flags;
#endif
            } else if (s->opt.enable_ecn) {
                // make sure that the flags reflect what went out on the wire
                v->flags = IPTOS_ECN_ECT0;
            }

            v = sq_next(v, next);
        }

        const ssize_t r =
#ifdef HAVE_SENDMMSG
            sendmmsg(s->fd, msgvec, (unsigned int)i, 0);
#else
            sendmsg(s->fd, msgvec, 0);
#endif
        ensure(r > 0 || errno == EAGAIN || errno == ETIMEDOUT ||
                   errno == ECONNREFUSED || errno == EHOSTUNREACH,
               "sendmsg/sendmmsg");
    } while (v);
}


/// Calls recvmsg() or recvmmsg() for all sockets associated with the engine,
/// emulating the operation of netmap backend_rx() function. Appends all data to
/// the w_sock::iv socket buffers of the respective w_sock structures.
///
/// @param      s     w_sock for which the application would like to receive new
///                   data.
/// @param      i     w_iov tail queue to append new data to.
///
void w_rx(struct w_sock * const s, struct w_iov_sq * const i)
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
        __extension__ uint8_t ctrl[RECV_SIZE][CMSG_SPACE(sizeof(uint8_t))];
#ifdef HAVE_RECVMMSG
        struct mmsghdr msgvec[RECV_SIZE];
#else
        struct msghdr msgvec[RECV_SIZE];
#endif
        ssize_t nbufs = 0;
        for (int j = 0; likely(j < RECV_SIZE); j++, nbufs++) {
            v[j] = w_alloc_iov(s->w, 0, 0);
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
                                .msg_iovlen = 1,
                                .msg_control = &ctrl[j],
                                .msg_controllen = sizeof(ctrl[j])};
        }
        if (unlikely(nbufs == 0)) {
            warn(CRT, "no more bufs");
            return;
        }
#ifdef HAVE_RECVMMSG
        n = (ssize_t)recvmmsg(s->fd, msgvec, (unsigned int)nbufs, MSG_DONTWAIT,
                              0);
        ensure(n != -1 || errno == EAGAIN || errno == ETIMEDOUT ||
                   errno == ECONNREFUSED,
               "recvmmsg");
#else
        n = recvmsg(s->fd, msgvec, MSG_DONTWAIT);
        ensure(n != -1 || errno == EAGAIN || errno == ETIMEDOUT ||
                   errno == ECONNREFUSED,
               "recvmsg");
#endif
        if (likely(n > 0)) {
            for (int j = 0; likely(j < MIN(n, nbufs)); j++) {
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

                // extract TOS byte
#ifdef HAVE_RECVMMSG
                for (struct cmsghdr * cmsg = CMSG_FIRSTHDR(&msgvec[j].msg_hdr);
                     cmsg; cmsg = CMSG_NXTHDR(&msgvec[j].msg_hdr, cmsg)) {
#else
                for (struct cmsghdr * cmsg = CMSG_FIRSTHDR(&msgvec[j]); cmsg;
                     cmsg = CMSG_NXTHDR(&msgvec[j], cmsg)) {
#endif
                    if (cmsg->cmsg_level == IPPROTO_IP && cmsg->cmsg_len &&
#ifdef __linux__
                        // why do you always need to be different, Linux?
                        cmsg->cmsg_type == IP_TOS
#else
                        cmsg->cmsg_type == IP_RECVTOS
#endif
                    ) {
                        v[j]->flags = *CMSG_DATA(cmsg);
                        break;
                    }
                }

                // add the iov to the tail of the result
                sq_insert_tail(i, v[j], next);
            }
        } else
            // in case EAGAIN/ETIMEDOUT was returned (n == -1)
            n = 0;

        // return any unused buffers
        for (ssize_t j = n; likely(j < nbufs); j++)
            sq_insert_head(&s->w->iov, v[j], next);
    } while (
#ifdef HAVE_RECVMMSG
        n == RECV_SIZE
#else
        n == 1
#endif
    );
}


/// The sock backend performs no operation here.
///
/// @param[in]  w     Backend engine.
///
void w_nic_tx(struct w_engine * const w __attribute__((unused))) {}


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
    struct w_backend * const b = w->b;

#if defined(HAVE_KQUEUE)
    const struct timespec timeout = {msec / 1000000, (msec % 1000000) * 1000};
    b->n = kevent(b->kq, 0, 0, b->ev, sizeof(b->ev) / sizeof(b->ev[0]),
                  msec == -1 ? 0 : &timeout);
    return b->n > 0;

#elif defined(HAVE_EPOLL)
    b->n = epoll_wait(b->ep, b->ev, sizeof(b->ev) / sizeof(b->ev[0]), msec);
    return b->n > 0;

#else
    const size_t cur_n = kh_size((khash_t(sock) *)w->sock);
    if (unlikely(cur_n == 0)) {
        free(b->fds);
        free(b->socks);
        b->fds = 0;
        b->socks = 0;
        b->n = 0;
        return false;
    }

    // allocate and fill pollfd
    if (unlikely(b->n == 0 || b->n < (int)cur_n)) {
        b->n = (int)cur_n;
        b->fds = realloc(b->fds, cur_n * sizeof(*b->fds));
        b->socks = realloc(b->socks, cur_n * sizeof(*b->socks));
        ensure(b->fds && b->socks, "could not realloc");
    }

    struct w_sock * s = 0;
    int n = 0;
    kh_foreach_value ((khash_t(sock) *)w->sock, s, {
        b->fds[n].fd = s->fd;
        b->fds[n].events = POLLIN;
        b->socks[n++] = s;
    })

        // poll
        n = poll(b->fds, (nfds_t)cur_n, msec);

    return n > 0;
#endif
}


/// Fill a w_sock_slist with pointers to some sockets with pending inbound
/// data. Data can be obtained via w_rx() on each w_sock in the list. Call
/// can optionally block to wait for at least one ready connection. Will
/// return the number of ready connections, or zero if none are ready. When
/// the return value is not zero, a repeated call may return additional
/// ready sockets.
///
/// @param[in]  w     Backend engine.
/// @param      sl    Empty and initialized w_sock_slist.
///
/// @return     Number of connections that are ready for reading.
///
uint32_t w_rx_ready(struct w_engine * const w, struct w_sock_slist * const sl)
{
    struct w_backend * const b = w->b;

#if defined(HAVE_KQUEUE)
    if (b->n <= 0) {
        const struct timespec timeout = {0, 0};
        b->n = kevent(b->kq, 0, 0, b->ev, sizeof(b->ev) / sizeof(b->ev[0]),
                      &timeout);
    }

    int i;
    for (i = 0; i < b->n; i++)
        sl_insert_head(sl, (struct w_sock *)b->ev[i].udata, next_rx);
    b->n = 0;
    return (uint32_t)i;

#elif defined(HAVE_EPOLL)
    if (b->n <= 0)
        b->n = epoll_wait(b->ep, b->ev, sizeof(b->ev) / sizeof(b->ev[0]), 0);

    int i;
    for (i = 0; i < b->n; i++)
        sl_insert_head(sl, (struct w_sock *)b->ev[i].data.ptr, next_rx);
    b->n = 0;
    return (uint32_t)i;

#else

    uint32_t n = 0;
    for (int i = 0; i < b->n; i++)
        if (b->fds[i].revents & POLLIN && b->socks[i]) {
            sl_insert_head(sl, b->socks[i], next_rx);
            n++;
        }
    return n;
#endif
}
