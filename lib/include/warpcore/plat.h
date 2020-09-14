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

#pragma once
// IWYU pragma: private, include <warpcore/warpcore.h>

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#if HAVE_64BIT
#ifdef __OPTIMIZE_SIZE__
typedef uint_least64_t uint_t;
typedef int_least64_t dint_t;
#define PRIu PRIuLEAST64
#define PRId PRIdLEAST64
#define PRIx PRIxLEAST64
#else
typedef uint_fast64_t uint_t;
typedef int_fast64_t dint_t;
#define PRIu PRIuFAST64
#define PRId PRIdFAST64
#define PRIx PRIxFAST64
#endif
#define UINT_T_MAX UINT64_MAX
#define UINT_C UINT64_C
#else
#ifdef __OPTIMIZE_SIZE__
typedef uint_least32_t uint_t;
typedef int_least32_t dint_t;
#define PRIu PRIuLEAST32
#define PRId PRIdLEAST32
#define PRIx PRIxLEAST32
#else
typedef uint_fast32_t uint_t;
typedef int_fast32_t dint_t;
#define PRIu PRIuFAST32
#define PRId PRIdFAST32
#define PRIx PRIxFAST32
#endif
#define UINT_T_MAX UINT32_MAX
#define UINT_C UINT32_C
#endif

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

#if __has_builtin(__builtin_bswap16) || defined(__GNUC__)
#define bswap16(x) __builtin_bswap16(x)
#elif defined(HAVE_ENDIAN_H)
#include <endian.h>
#define bswap16(x) htobe16(x)
#elif defined(HAVE_SYS_ENDIAN_H)
#include <sys/endian.h>
#define bswap16(x) htobe16(x)
#else
#include <arpa/inet.h>
#define bswap16(x) htons(x)
#endif

#if __has_builtin(__builtin_bswap32) || defined(__GNUC__)
#define bswap32(x) __builtin_bswap32(x)
#elif defined(HAVE_ENDIAN_H)
#include <endian.h>
#define bswap32(x) htobe32(x)
#elif defined(HAVE_SYS_ENDIAN_H)
#include <sys/endian.h>
#define bswap32(x) htobe32(x)
#else
#include <arpa/inet.h>
#define bswap32(x) htonl(x)
#endif

#if __has_builtin(__builtin_bswap64) || defined(__GNUC__)
#define bswap64(x) __builtin_bswap64(x)
#elif defined(HAVE_ENDIAN_H)
#include <endian.h>
#define bswap64(x) htobe64(x)
#elif defined(HAVE_SYS_ENDIAN_H)
#include <sys/endian.h>
#define bswap64(x) htobe64(x)
#else
#include <arpa/inet.h>
#define bswap64(x)                                                             \
    (((uint64_t)bswap32((x)&0xFFFFFFFF) << 32) | bswap32((x) >> 32))
#endif

#if defined(__linux__)
// #include <sys/socket.h>
#define AF_LINK AF_PACKET
#define PLAT_MMFLAGS MAP_POPULATE | MAP_LOCKED

#elif defined(__FreeBSD__)
/// Platform-dependent flags to pass to mmap().
#define PLAT_MMFLAGS MAP_PREFAULT_READ | MAP_NOSYNC | MAP_ALIGNED_SUPER

#elif defined(__APPLE__)
#define PLAT_MMFLAGS 0
#define SOCK_CLOEXEC 0

#elif defined(PARTICLE) || defined(RIOT_VERSION)
#define CLOCK_MONOTONIC_RAW 0
#ifdef PARTICLE
typedef struct if_list if_list;
#include "ifapi.h"
#define ifaddrs if_addrs
#define SOCK_CLOEXEC 0
#endif

#endif

#define ECN_NOT 0x00  // not-ECT
#define ECN_ECT1 0x01 // ECN-capable transport (1)
#define ECN_ECT0 0x02 // ECN-capable transport (0)
#define ECN_CE 0x03   // congestion experienced
#define ECN_MASK 0x03 // ECN field mask


struct ifaddrs;
struct eth_addr;

extern void __attribute__((nonnull))
plat_get_mac(struct eth_addr * const mac, const struct ifaddrs * const i);

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

extern const char * __attribute__((nonnull))
eth_ntoa(const struct eth_addr * const addr,
         char * const buf,
         const size_t len);
