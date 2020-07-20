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


#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>


#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-align"

#ifdef __APPLE__
// Darwin doesn't have this, but cppcheck doesn't shut up about adding it
#define SOCK_CLOEXEC 0
#endif


static const unsigned dst_port = 12345;


static socklen_t __attribute__((const)) sa_len(const int af)
{
    return af == AF_INET ? sizeof(struct sockaddr_in)
                         : sizeof(struct sockaddr_in6);
}


static int __attribute__(())
sock_open(const sa_family_t af, const bool test_with_cmsg)
{
    const int s = socket(af, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    assert(s >= 0);

    // always receive DSCP/ECN info
    const int proto = af == AF_INET ? IPPROTO_IP : IPPROTO_IPV6;
    int ret = setsockopt(s, proto, af == AF_INET ? IP_RECVTOS : IPV6_RECVTCLASS,
                         &(int){1}, sizeof(int));
    assert(ret == 0);

    if (test_with_cmsg == false) {
        // test with socket default
        ret = setsockopt(s, proto, af == AF_INET ? IP_TOS : IPV6_TCLASS,
                         &(int){0x11}, sizeof(int));
        assert(ret == 0);
    }

    return s;
}


static void __attribute__((nonnull))
sock_send(const int s, struct sockaddr * const dst, const bool test_with_cmsg)
{
    struct msghdr msgvec = (struct msghdr){
        .msg_name = dst,
        .msg_namelen = sa_len(dst->sa_family),
        .msg_iov = &(struct iovec){.iov_base = "XXX", .iov_len = 3},
        .msg_iovlen = 1};

    __extension__ uint8_t ctrl[CMSG_SPACE(sizeof(int))];
    if (test_with_cmsg) {
        msgvec.msg_control = &ctrl;
        msgvec.msg_controllen = sizeof(ctrl);
        struct cmsghdr * const cmsg = CMSG_FIRSTHDR(&msgvec);
        cmsg->cmsg_level =
            dst->sa_family == AF_INET ? IPPROTO_IP : IPPROTO_IPV6;
        cmsg->cmsg_type = dst->sa_family == AF_INET ? IP_TOS : IPV6_TCLASS;
        cmsg->cmsg_len =
#ifdef __FreeBSD__
            CMSG_LEN(dst->sa_family == AF_INET ? sizeof(char) : sizeof(int));
#else
            CMSG_LEN(sizeof(int));
#endif
        *(int *)CMSG_DATA(cmsg) = 0x55;
    }

#ifndef NDEBUG
    const ssize_t sent =
#endif
        sendmsg(s, &msgvec, 0);
    assert(sent == (ssize_t)msgvec.msg_iov->iov_len);
}


static void ecn_test(const sa_family_t af,
                     const bool test_with_cmsg,
                     const bool test_loopback)
{
    const int s = sock_open(af, test_with_cmsg);

    printf("testing: IPv%d, cmsg %s, loopback %s\n", af == AF_INET ? 4 : 6,
           test_with_cmsg ? "true" : "false", test_loopback ? "true" : "false");

    struct sockaddr_in dst_loop4 = {.sin_family = AF_INET,
                                    .sin_addr = {htonl(INADDR_LOOPBACK)},
                                    .sin_port = htons(dst_port)};

    struct sockaddr_in6 dst_loop6 = {.sin6_family = AF_INET6,
                                     .sin6_addr = IN6ADDR_LOOPBACK_INIT,
                                     .sin6_port = htons(dst_port)};

    struct sockaddr_in dst_net4 = {.sin_family = AF_INET,
                                   .sin_addr = {htonl(0x01020304)},
                                   .sin_port = htons(dst_port)};

    struct sockaddr_in6 dst_net6 = {
        .sin6_family = AF_INET6,
        .sin6_addr = {{{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}}},
        .sin6_port = htons(dst_port)};


    struct sockaddr * dst = 0;

    if (test_loopback)
        dst = af == AF_INET ? (struct sockaddr *)&dst_loop4
                            : (struct sockaddr *)&dst_loop6;
    else
        dst = af == AF_INET ? (struct sockaddr *)&dst_net4
                            : (struct sockaddr *)&dst_net6;

    sock_send(s, dst, test_with_cmsg);

    close(s);
}


int main(void)
{
    printf(
        "run tcpdump on loopback and default interface for UDP dst port %u\n\n",
        dst_port);

    bool test_loopback = true;
    bool test_with_cmsg = false;
    ecn_test(AF_INET, test_with_cmsg, test_loopback);
    ecn_test(AF_INET6, test_with_cmsg, test_loopback);

    test_with_cmsg = true;
    ecn_test(AF_INET, test_with_cmsg, test_loopback);
    ecn_test(AF_INET6, test_with_cmsg, test_loopback);

    test_loopback = false;
    test_with_cmsg = false;
    ecn_test(AF_INET, test_with_cmsg, test_loopback);
    ecn_test(AF_INET6, test_with_cmsg, test_loopback);

    test_with_cmsg = true;
    ecn_test(AF_INET, test_with_cmsg, test_loopback);
    ecn_test(AF_INET6, test_with_cmsg, test_loopback);

    return 0;
}
