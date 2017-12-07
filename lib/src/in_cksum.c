// SPDX-License-Identifier: BSD-2-Clause
//
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

// Code adapted from https://github.com/intel/soft-crc/blob/master/crc_tcpip.c

/*******************************************************************************
 Copyright (c) 2009-2017, Intel Corporation
 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
     * Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.
     * Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.
     * Neither the name of Intel Corporation nor the names of its contributors
       may be used to endorse or promote products derived from this software
       without specific prior written permission.
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
 FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/


#include <stdint.h>

#include "in_cksum.h"
#include "ip.h"
#include "udp.h"


static inline uint32_t __attribute__((always_inline))
csum_oc16(const uint8_t * data, uint32_t data_len)
{
    const uint16_t * data16 = (const uint16_t *)(const void *)data;
    uint32_t sum = 0;

    for (uint32_t n = 0; n < (data_len / sizeof(uint16_t)); n++)
        sum += (uint32_t)data16[n];

    if (data_len & 1)
        sum += (uint32_t)data[data_len - 1];

    return sum;
}


static inline uint16_t __attribute__((always_inline))
csum_oc16_reduce(uint32_t sum)
{
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}


/// Compute the Internet checksum over buffer @p buf of length @p len. See
/// [RFC1071](https://tools.ietf.org/html/rfc1071).
///
/// @param[in]  buf   The buffer
/// @param[in]  len   The length
///
/// @return     Internet checksum of @p buf.
///
uint16_t ip_cksum(const void * const buf, const uint16_t len)
{
    const uint32_t sum = csum_oc16(buf, len);
    return csum_oc16_reduce(sum);
}


uint16_t udp_cksum(const void * buf, uint16_t len)
{
    const struct ip_hdr * const ip = (const struct ip_hdr *)buf;
    const struct udp_hdr * const udp =
        (const struct udp_hdr *)(const void *)((const uint8_t *)buf +
                                               sizeof(*ip));

    // IPv4 pseudo header
    uint32_t sum = ((uint32_t)ip->p) << 8;
    sum += csum_oc16((const uint8_t *)&ip->src, sizeof(ip->src));
    sum += csum_oc16((const uint8_t *)&ip->dst, sizeof(ip->dst));
    sum += csum_oc16((const uint8_t *)&udp->len, sizeof(udp->len));

    // UDP header without checksum.
    sum += csum_oc16((const uint8_t *)udp, sizeof(*udp) - sizeof(uint16_t));

    // UDP payload
    sum += csum_oc16((const uint8_t *)udp + sizeof(*udp),
                     len - sizeof(*ip) - sizeof(*udp));

    return csum_oc16_reduce(sum);
}
