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

#include <stdint.h>

struct netmap_ring;
struct warpcore;

#define ICMP_TYPE_ECHOREPLY 0 ///< ICMP echo reply type.
#define ICMP_TYPE_UNREACH 3   ///< ICMP unreachable type.
#define ICMP_TYPE_ECHO 8      ///< ICMP echo type.

#define ICMP_UNREACH_PROTOCOL 2 ///< For ICMP_TYPE_UNREACH, bad protocol code.
#define ICMP_UNREACH_PORT 3     ///< For ICMP_TYPE_UNREACH, bad port code.


/// An ICMP header representation; see
/// [RFC792](https://tools.ietf.org/html/rfc792).
///
struct icmp_hdr {
    uint8_t type;   ///< Type of ICMP message.
    uint8_t code;   ///< Code of the ICMP type.
    uint16_t cksum; ///< Ones' complement header checksum.
    uint16_t id;
    uint16_t seq;
};


extern void __attribute__((nonnull)) icmp_tx(struct warpcore * w,
                                             const uint8_t type,
                                             const uint8_t code,
                                             void * const buf);

extern void __attribute__((nonnull))
icmp_rx(struct warpcore * w, struct netmap_ring * const r);
