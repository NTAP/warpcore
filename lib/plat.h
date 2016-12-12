// Copyright (c) 2014-2016, NetApp, Inc.
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

#include <stdbool.h>
#include <stdint.h>

struct ifaddrs;


#ifdef __linux__
#define AF_LINK AF_PACKET
#define PLAT_MMFLAGS MAP_POPULATE | MAP_LOCKED
#else
/// Platform-dependent flags to pass to mmap().
#define PLAT_MMFLAGS MAP_PREFAULT_READ | MAP_NOSYNC | MAP_ALIGNED_SUPER
#endif

extern void __attribute__((nonnull))
plat_get_mac(uint8_t * mac, const struct ifaddrs * i);

extern uint16_t __attribute__((nonnull)) plat_get_mtu(const struct ifaddrs * i);

extern uint32_t __attribute__((nonnull))
plat_get_mbps(const struct ifaddrs * i);

extern bool __attribute__((nonnull)) plat_get_link(const struct ifaddrs * i);

extern void plat_setaffinity(void);
