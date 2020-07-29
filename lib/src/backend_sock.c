// SPDX-License-Identifier: BSD-2-Clause
//
// Copyright (c) 2014-2020, NetApp, Inc.
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
#include <inttypes.h>

#if defined(__linux__)
#include <limits.h>
#elif defined(__APPLE__)
#include <netinet/udp.h>
#else
#include <sys/types.h>
#endif

#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <unistd.h>

#include <warpcore/warpcore.h>

#ifndef PARTICLE
#include <sys/uio.h>
#else
#define IPV6_TCLASS IP_TOS         // unclear if this works
#define IPV6_RECVTCLASS IP_RECVTOS // unclear if this works
#define SOCK_CLOEXEC 0
#define IP_RECVTOS IP_TOS
#define strerror(...) ""
#endif

#ifdef HAVE_ASAN
#include <sanitizer/asan_interface.h>
#endif

#if defined(HAVE_KQUEUE)
#include <sys/event.h>
#include <time.h>
#elif defined(HAVE_EPOLL)
#include <sys/epoll.h>
#elif !defined(PARTICLE)
#include <poll.h>
#endif

#if defined(HAVE_SENDMMSG)
#define SENDFUNC "sendmmsg"
#else
#define SENDFUNC "sendmsg"
#endif

#if defined(HAVE_RECVMMSG)
#define RECVFUNC "recvmmsg"
#else
#define RECVFUNC "recvmsg"
#endif

#include "backend.h"
#include "ifaddr.h"


/// Set the socket options.
///
/// @param      s     The w_sock to change options for.
/// @param[in]  opt   Socket options for this socket.
///
void w_set_sockopt(struct w_sock * const s, const struct w_sockopt * const opt)
{
    if (s->ws_af == AF_INET &&
        s->opt.enable_udp_zero_checksums != opt->enable_udp_zero_checksums) {
        s->opt.enable_udp_zero_checksums = opt->enable_udp_zero_checksums;
#if defined(__linux__)
        ensure(setsockopt((int)s->fd, SOL_SOCKET, SO_NO_CHECK,
                          &(int){s->opt.enable_udp_zero_checksums},
                          sizeof(int)) >= 0,
               "cannot setsockopt SO_NO_CHECK");
#elif defined(__APPLE__)
        ensure(setsockopt((int)s->fd, IPPROTO_UDP, UDP_NOCKSUM,
                          &(int){s->opt.enable_udp_zero_checksums},
                          sizeof(int)) >= 0,
               "cannot setsockopt UDP_NOCKSUM");
#endif
    }

    if (s->opt.enable_ecn != opt->enable_ecn) {
        s->opt.enable_ecn = opt->enable_ecn;
        const int ret = setsockopt(
            (int)s->fd, s->ws_af == AF_INET ? IPPROTO_IP : IPPROTO_IPV6,
            s->ws_af == AF_INET ? IP_TOS : IPV6_TCLASS,
            // cppcheck-suppress internalAstError
            &(int){s->opt.enable_ecn ? ECN_ECT0 : ECN_NOT}, sizeof(int));
        if (unlikely(ret < 0))
            warn(WRN, "cannot setsockopt IP_TOS/IPV6_TCLASS; running on WSL?");
    }
}


/// Initialize the warpcore socket backend for engine @p w. Sets up the extra
/// buffers.
///
/// @param      w      Backend engine.
/// @param[in]  nbufs  Number of packet buffers to allocate.
///
void backend_init(struct w_engine * const w, const uint32_t nbufs)
{
    backend_addr_config(w); // do this first so w->mtu is set for max_buf_len
#ifndef PARTICLE
    // some interfaces can have huge MTUs, so cap to something more sensible
    w->mtu = MIN(w->mtu, (uint16_t)getpagesize() / 2);
#endif

    ensure((w->mem = calloc(nbufs, max_buf_len(w))) != 0,
           "cannot alloc %" PRIu32 " * %u buf mem", nbufs, max_buf_len(w));
    ensure((w->bufs = calloc(nbufs, sizeof(*w->bufs))) != 0,
           "cannot alloc bufs");
    w->backend_name = "socket";

    for (uint32_t i = 0; i < nbufs; i++) {
        init_iov(w, &w->bufs[i], i);
        sq_insert_head(&w->iov, &w->bufs[i], next);
        ASAN_POISON_MEMORY_REGION(w->bufs[i].buf, max_buf_len(w));
    }

#if defined(HAVE_KQUEUE)
    w->b->kq = kqueue();
    w->backend_variant = "kqueue/" SENDFUNC "/" RECVFUNC;
#elif defined(HAVE_EPOLL)
    w->b->ep = epoll_create1(0);
    w->backend_variant = "epoll/" SENDFUNC "/" RECVFUNC;
#else
    w->backend_variant = "poll/" SENDFUNC "/" RECVFUNC;
#endif

    warn(DBG, "%s backend using %s", w->backend_name, w->backend_variant);
}


/// Shut a warpcore socket engine down cleanly. Does nothing, at the moment.
///
/// @param      w     Backend engine.
///
void backend_cleanup(struct w_engine * const w)
{
#if !defined(HAVE_KQUEUE) && !defined(HAVE_EPOLL)
    free(w->b->fds);
    w->b->fds = 0;
    struct w_sock * s;
    sl_foreach (s, &w->b->socks, __next)
        w_close(s);
#endif
    free(w->mem);
    free(w->bufs);
    w->b->n = 0;
}


/// Bind a warpcore socket-backend socket. Calls the underlying Socket API.
///
/// @param      s     The w_sock to bind.
/// @param[in]  opt   Socket options for this socket. Can be zero.
///
/// @return     Zero on success, @p errno otherwise.
///
int backend_bind(struct w_sock * const s, const struct w_sockopt * const opt)
{
    s->fd = socket(s->ws_af, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (unlikely(s->fd < 0))
        return errno;

    struct sockaddr_storage ss;
    to_sockaddr((struct sockaddr *)&ss, &s->ws_laddr, s->ws_lport, s->ws_scope);
    if (unlikely(bind((int)s->fd, (struct sockaddr *)&ss, sa_len(s->ws_af)) !=
                 0))
        return errno;

    // enable always receiving TOS information
    ensure(setsockopt((int)s->fd,
                      s->ws_af == AF_INET ? IPPROTO_IP : IPPROTO_IPV6,
                      s->ws_af == AF_INET ? IP_RECVTOS : IPV6_RECVTCLASS,
                      &(int){1}, sizeof(int)) >= 0,
           "cannot setsockopt IP_RECVTOS/IPV6_RECVTCLASS");

    // enable always receiving TTL information
#ifdef __APPLE__
    ensure(setsockopt((int)s->fd,
                      s->ws_af == AF_INET ? IPPROTO_IP : IPPROTO_IPV6,
                      IP_RECVTTL, &(int){1}, sizeof(int)) >= 0,
           "cannot setsockopt IP_RECVTTL");
#elif !defined(PARTICLE)
    ensure(setsockopt((int)s->fd, IPPROTO_IP, IP_RECVTTL, &(int){1},
                      sizeof(int)) >= 0,
           "cannot setsockopt IP_RECVTTL");
#endif

#if !defined(__APPLE__) && !defined(PARTICLE)
    if (s->ws_af == AF_INET) {
        // enable set DF
        const int ret =
            setsockopt((int)s->fd, IPPROTO_IP,
#if defined(__linux__)
                       IP_MTU_DISCOVER, &(int){IP_PMTUDISC_DO}, sizeof(int)
#else
                       IP_DONTFRAG, &(int){1}, sizeof(int)
#endif
            );
        ensure(ret >= 0, "cannot setsockopt IP_DONTFRAG");
    }
#endif

    if (opt)
        w_set_sockopt(s, opt);

    // if we're binding to a random port, find out what it is
    if (s->ws_lport == 0) {
        socklen_t len = sizeof(ss);
        ensure(getsockname((int)s->fd, (struct sockaddr *)&ss, &len) >= 0,
               "getsockname");
        s->ws_lport = sa_port(&ss);
    }

#if defined(HAVE_KQUEUE)
    struct kevent ev;
    EV_SET(&ev, s->fd, EVFILT_READ, EV_ADD, 0, 0, s);
    ensure(kevent(s->w->b->kq, &ev, 1, 0, 0, 0) != -1, "kevent");
#elif defined(HAVE_EPOLL)
    struct epoll_event ev = {.events = EPOLLIN, .data.ptr = s};
    ensure(epoll_ctl(s->w->b->ep, EPOLL_CTL_ADD, (int)s->fd, &ev) != -1,
           "epoll_ctl");
#else
    sl_insert_head(&s->w->b->socks, s, __next);
#endif

    return 0;
}


void backend_preconnect(struct w_sock * const s __attribute__((unused))) {}


/// The socket backend performs no operation here.
///
/// @param      s     The w_sock to connect.
///
/// @return     Zero on success, @p errno otherwise.
///
int backend_connect(struct w_sock * const s)
{
    struct sockaddr_storage ss;
    to_sockaddr((struct sockaddr *)&ss, &s->ws_raddr, s->ws_rport, s->ws_scope);
    if (unlikely(connect((int)s->fd, (struct sockaddr *)&ss,
                         sa_len(ss.ss_family)) != 0))
        return errno;
    return 0;
}


/// Close the socket.
///
/// @param      s     The w_sock to close.
///
void backend_close(struct w_sock * const s)
{
#if defined(HAVE_KQUEUE)
    struct kevent ev;
    EV_SET(&ev, s->fd, EVFILT_READ, EV_DELETE, 0, 0, s);
    ensure(kevent(s->w->b->kq, &ev, 1, 0, 0, 0) != -1, "kevent");
#elif defined(HAVE_EPOLL)
    struct epoll_event ev = {.events = EPOLLIN, .data.ptr = s};
    ensure(epoll_ctl(s->w->b->ep, EPOLL_CTL_DEL, (int)s->fd, &ev) != -1,
           "epoll_ctl");
#else
    sl_remove(&s->w->b->socks, s, w_sock, __next);
#endif

    ensure(close((int)s->fd) == 0, "close");
}


/// Loops over the w_iov structures in the tail queue @p o, sending them all
/// over w_sock @p s. This backend uses the Socket API.
///
/// @param      s     w_sock socket to transmit over.
/// @param      o     w_iov_sq to send.
///
void w_tx(struct w_sock * const s, struct w_iov_sq * const o)
{
#ifdef HAVE_SENDMMSG
// There is a tradeoff here in terms of how many messages we should try and
// send. Preparing to handle longer sizes has preparation overheads, whereas
// only handling shorter sizes may require multiple syscalls (and incur their
// overheads). So we're picking a number out of a hat. We could allocate
// dynamically for MAX(IOV_MAX, w_iov_sq_cnt(c)), but that seems overkill.
#define SEND_SIZE MIN(64, IOV_MAX)
    struct mmsghdr msgvec[SEND_SIZE];
#else
#define SEND_SIZE 1
    struct msghdr msgvec[SEND_SIZE];
#endif
    struct iovec msg[SEND_SIZE];
    struct sockaddr_storage sa[SEND_SIZE];
#ifdef __linux__
    // kernels below 4.9 can't deal with getting an uint8_t passed in, sigh
    __extension__ uint8_t ctrl[SEND_SIZE][CMSG_SPACE(sizeof(int))];
#else
    __extension__ uint8_t ctrl[SEND_SIZE][CMSG_SPACE(sizeof(uint8_t))];
#endif

    struct w_iov * v = sq_first(o);
    do {
        size_t i;
        for (i = 0; i < SEND_SIZE && v; i++) {

            // for sendmmsg, we populate the parameters
            msg[i] = (struct iovec){.iov_base = v->buf, .iov_len = v->len};
            // if w_sock is disconnected, use destination IP and port from w_iov
            // instead of the one in the template header
            if (w_connected(s))
                v->saddr = s->tup.remote;
            else
                to_sockaddr((struct sockaddr *)&sa[i], &v->wv_addr, v->wv_port,
                            s->ws_scope);
#ifdef HAVE_SENDMMSG
            msgvec[i].msg_hdr =
#else
            msgvec[i] =
#endif
                (struct msghdr){
                    .msg_name = w_connected(s) ? 0 : &sa[i],
                    .msg_namelen = w_connected(s) ? 0 : sa_len(sa[i].ss_family),
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
                cmsg->cmsg_level =
                    v->wv_af == AF_INET ? IPPROTO_IP : IPPROTO_IPV6;
                cmsg->cmsg_type = v->wv_af == AF_INET ? IP_TOS : IPV6_TCLASS;
                cmsg->cmsg_len =
#ifdef __FreeBSD__
                    CMSG_LEN(v->wv_af == AF_INET ? sizeof(char) : sizeof(int));
#else
                    CMSG_LEN(sizeof(int));
#endif
                *(int *)(void *)CMSG_DATA(cmsg) = v->flags;
            } else if (s->opt.enable_ecn)
                // make sure that the flags reflect what went out on the wire
                v->flags = ECN_ECT0;

            v = sq_next(v, next);
        }

        const ssize_t r =
#if defined(HAVE_SENDMMSG)
            sendmmsg((int)s->fd, msgvec, (unsigned int)i, 0);
#else
            sendmsg((int)s->fd, msgvec, 0);
#endif
        if (unlikely(r < 0 && errno != EAGAIN && errno != ETIMEDOUT))
            warn(ERR, "sendmsg/sendmmsg returned %d (%s)", errno,
                 strerror(errno));
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
#define RECV_SIZE MIN(64, IOV_MAX)
#else
#define RECV_SIZE 1
#endif
    ssize_t n = 0;
    do {
        struct w_iov * v[RECV_SIZE];
        struct iovec msg[RECV_SIZE];
        struct sockaddr_storage sa[RECV_SIZE];
        __extension__ uint8_t ctrl[RECV_SIZE][CMSG_SPACE(sizeof(uint8_t)) +
                                              CMSG_SPACE(sizeof(uint8_t))];
#ifdef HAVE_RECVMMSG
        struct mmsghdr msgvec[RECV_SIZE];
#else
        struct msghdr msgvec[RECV_SIZE];
#endif
        ssize_t nbufs = 0;
        for (int j = 0; likely(j < RECV_SIZE); j++, nbufs++) {
            v[j] = w_alloc_iov(s->w, s->ws_af, 0, 0);
            if (unlikely(v[j] == 0))
                break;
            msg[j] =
                (struct iovec){.iov_base = v[j]->buf, .iov_len = v[j]->len};
#ifdef HAVE_RECVMMSG
            msgvec[j].msg_hdr =
#else
            msgvec[j] =
#endif
                (struct msghdr){.msg_name = &sa[j],
                                .msg_namelen = sizeof(sa[j]),
                                .msg_iov = &msg[j],
                                .msg_iovlen = 1,
                                .msg_control = &ctrl[j],
                                .msg_controllen = sizeof(ctrl[j])};
        }
        if (unlikely(nbufs == 0)) {
            warn(CRT, "no more bufs");
            return;
        }
#if defined(HAVE_RECVMMSG)
        n = (ssize_t)recvmmsg((int)s->fd, msgvec, (unsigned int)nbufs,
                              MSG_DONTWAIT, 0);
#else
        n = recvmsg((int)s->fd, msgvec, MSG_DONTWAIT);
#endif
        if (likely(n > 0)) {
            for (int j = 0; likely(j < MIN(n, nbufs)); j++) {
                v[j]->wv_port = sa_port(&sa[j]);
                w_to_waddr(&v[j]->wv_addr, (struct sockaddr *)&sa[j]);

#ifdef HAVE_RECVMMSG
                v[j]->len = (uint16_t)msgvec[j].msg_len;
#else
                v[j]->len = (uint16_t)n;
                // recvmsg returns number of bytes, we need number of
                // messages for the return loop below
                n = 1;
#endif

                // extract TOS byte (Particle uses recvfrom w/o cmsg support)
#ifdef HAVE_RECVMMSG
                for (struct cmsghdr * cmsg = CMSG_FIRSTHDR(&msgvec[j].msg_hdr);
                     cmsg; cmsg = CMSG_NXTHDR(&msgvec[j].msg_hdr, cmsg)) {
#else
                for (struct cmsghdr * cmsg = CMSG_FIRSTHDR(&msgvec[j]); cmsg;
                     cmsg = CMSG_NXTHDR(&msgvec[j], cmsg)) {
#endif
                    if (cmsg->cmsg_level == IPPROTO_IP ||
                        cmsg->cmsg_level == IPPROTO_IPV6) {
                        if (cmsg->cmsg_type ==
#ifdef __linux__
                                IP_TOS
#else
                                IP_RECVTOS
#endif
                            || cmsg->cmsg_type == IPV6_TCLASS)
                            v[j]->flags = *(uint8_t *)CMSG_DATA(cmsg);
#ifndef PARTICLE
                        else if (cmsg->cmsg_type ==
#ifdef __linux__
                                 IP_TTL
#else
                                 IP_RECVTTL
#endif
                        )
                            v[j]->ttl = *(uint8_t *)CMSG_DATA(cmsg);
#endif
                    }
                }
                // add the iov to the tail of the result
                sq_insert_tail(i, v[j], next);
            }
        } else {
            if (unlikely(n < 0 && errno != EAGAIN && errno != ETIMEDOUT))
                warn(ERR, "recvmsg/recvmmsg returned %d (%s)", errno,
                     strerror(errno));
            n = 0;
        }

        // return any unused buffers
        for (ssize_t j = n; likely(j < nbufs); j++)
            w_free_iov(v[j]);
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
/// @param[in]  nsec  Timeout in nanoseconds. Pass zero for immediate return, -1
///                   for infinite wait.
///
/// @return     Whether any data is ready for reading.
///
bool w_nic_rx(struct w_engine * const w, const int64_t nsec)
{
    struct w_backend * const b = w->b;

#if defined(HAVE_KQUEUE)
    b->n = kevent(b->kq, 0, 0, b->ev, sizeof(b->ev) / sizeof(b->ev[0]),
                  nsec == -1
                      ? 0
                      : &(struct timespec){(uint64_t)nsec / NS_PER_S,
                                           (long)((uint64_t)nsec % NS_PER_S)});
    return b->n > 0;

#elif defined(HAVE_EPOLL)
    b->n = epoll_wait(b->ep, b->ev, sizeof(b->ev) / sizeof(b->ev[0]),
                      nsec == -1 ? -1 : (int)(nsec / NS_PER_MS));
    return b->n > 0;

#else

    int i = 0;
    struct w_sock * s;
    sl_foreach (s, &b->socks, __next) {
        if (i == b->n) {
            b->n *= 2; // arbitrary value, also used below
            struct pollfd * const cur_fds = b->fds;
            b->fds = realloc(b->fds, (size_t)b->n * sizeof(*b->fds));
            if (unlikely(b->fds == 0)) {
                warn(CRT, "cannot realloc %d pollfds", b->n);
                b->fds = cur_fds;
                b->n /= 2;
                break;
            }
        }
        b->fds[i].fd = (int)s->fd;
        b->fds[i].events = POLLIN;
        i++;
    }

    return poll(b->fds, (nfds_t)i, nsec == -1 ? -1 : (int)NS_TO_MS(nsec)) > 0;
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
    if (b->n <= 0)
        b->n = kevent(b->kq, 0, 0, b->ev, sizeof(b->ev) / sizeof(b->ev[0]),
                      &(struct timespec){0, 0});

    int i;
    for (i = 0; i < b->n; i++)
        sl_insert_head(sl, (struct w_sock *)b->ev[i].udata, next);
    b->n = 0;
    return (uint32_t)i;

#elif defined(HAVE_EPOLL)
    if (b->n <= 0)
        b->n = epoll_wait(b->ep, b->ev, sizeof(b->ev) / sizeof(b->ev[0]), 0);

    int i;
    for (i = 0; i < b->n; i++)
        sl_insert_head(sl, (struct w_sock *)b->ev[i].data.ptr, next);
    b->n = 0;
    return (uint32_t)i;

#else
    uint32_t i = 0;
    struct w_sock * s;
    sl_foreach (s, &b->socks, __next)
        if (b->fds[i].revents & POLLIN) {
            sl_insert_head(sl, s, next);
            i++;
        }
    return i;
#endif
}
