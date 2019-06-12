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

#pragma once
// IWYU pragma: private, include <warpcore/warpcore.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

struct ifaddrs;

#if defined(__linux__)
#include <netinet/ether.h>
#include <sys/socket.h>
#define AF_LINK AF_PACKET
#define PLAT_MMFLAGS MAP_POPULATE | MAP_LOCKED

#elif defined(__FreeBSD__)
#include <net/ethernet.h>
/// Platform-dependent flags to pass to mmap().
#define PLAT_MMFLAGS MAP_PREFAULT_READ | MAP_NOSYNC | MAP_ALIGNED_SUPER

#elif defined(__APPLE__)
#include <net/ethernet.h>
#define PLAT_MMFLAGS 0
#define SOCK_CLOEXEC 0

#elif defined(PARTICLE)
#define ETHER_ADDR_LEN 6

struct ether_addr {
    u_char ether_addr_octet[ETHER_ADDR_LEN];
};

typedef struct if_list if_list;
#include "ifapi.h"
#define ifaddrs if_addrs
#endif

#define ETH_ADDR_STRLEN ETHER_ADDR_LEN * 3


extern void __attribute__((nonnull))
plat_get_mac(struct ether_addr * const mac, const struct ifaddrs * const i);

extern uint16_t __attribute__((nonnull))
plat_get_mtu(const struct ifaddrs * const i);

extern uint32_t __attribute__((nonnull))
plat_get_mbps(const struct ifaddrs * const i);

extern bool __attribute__((nonnull))
plat_get_link(const struct ifaddrs * const i);

extern void __attribute__((nonnull))
plat_get_iface_driver(const struct ifaddrs * const i,
                      char * const name,
                      const size_t name_len);

#ifndef HAVE_ETHER_NTOA_R
extern char * __attribute__((nonnull))
ether_ntoa_r(const struct ether_addr * const addr, char * const buf);
#endif
