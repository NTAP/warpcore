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

#include <stdint.h>

struct netmap_slot;
struct w_engine;

#define ICMP6_TYPE_ECHOREPLY 129 ///< ICMP echo reply type.
#define ICMP6_TYPE_UNREACH 1     ///< ICMP unreachable type.
#define ICMP6_TYPE_ECHO 128      ///< ICMP echo type.
#define ICMP6_TYPE_NSOL 135      ///< ICMP neighbor solicitation type.
#define ICMP6_TYPE_NADV 136      ///< ICMP neighbor advertisement type.

#define ICMP6_UNREACH_PORT 4 ///< For ICMP6_TYPE_UNREACH, bad port code.


/// An ICMP header representation; see
/// [RFC792](https://tools.ietf.org/html/rfc792).
///
struct icmp6_hdr {
    uint8_t type;   ///< Type of ICMP message.
    uint8_t code;   ///< Code of the ICMP type.
    uint16_t cksum; ///< Ones' complement header checksum.
    uint16_t id;
    uint16_t seq;
} __attribute__((aligned(1)));


extern void __attribute__((nonnull)) icmp6_tx(struct w_engine * w,
                                              const uint8_t type,
                                              const uint8_t code,
                                              uint8_t * const buf);

extern void __attribute__((nonnull)) icmp6_rx(struct w_engine * w,
                                              struct netmap_slot * const s,
                                              uint8_t * const buf);

extern void __attribute__((nonnull))
icmp6_nsol(struct w_engine * const w, const uint8_t * const addr);
