// SPDX-License-Identifier: BSD-2-Clause
//
// Copyright (c) 2014-2022, NetApp, Inc.
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

// #define CHECKSUM_SSE

#ifdef CHECKSUM_SSE
#include <emmintrin.h>
#include <smmintrin.h>
#include <stdint.h>
#include <tmmintrin.h>
#endif

#ifdef __FreeBSD__
#include <sys/socket.h>
#endif

#include <warpcore/warpcore.h>

#include "in_cksum.h"
#include "ip4.h"
#include "ip6.h"


static inline uint16_t __attribute__((always_inline, const))
csum_oc16_reduce(uint32_t sum)
{
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}


#ifdef CKSUM_UPDATE
uint16_t
ip_cksum_update32(uint16_t old_check, uint32_t old_data, uint32_t new_data)
{
    old_check = ~old_check;
    old_data = ~old_data;
    const uint32_t l = (uint32_t)old_check + (old_data >> 16) +
                       (old_data & 0xffff) + (new_data >> 16) +
                       (new_data & 0xffff);
    return csum_oc16_reduce(l);
}


uint16_t
ip_cksum_update16(uint16_t old_check, uint16_t old_data, uint16_t new_data)
{
    old_check = ~old_check;
    old_data = ~old_data;
    const uint32_t l = (uint32_t)(old_check + ~old_data + new_data);
    return csum_oc16_reduce(l);
}
#endif


static inline uint32_t __attribute__((always_inline))
csum_oc16(const uint8_t * const restrict data, const uint32_t data_len)
{
    const uint16_t * restrict data16 = (const uint16_t *)(const void *)data;
    uint32_t sum = 0;

    for (uint64_t n = 0; n < data_len / sizeof(uint16_t); n++)
        sum += (uint32_t)data16[n];

    if (data_len & 1)
        sum += (uint32_t)data[data_len - 1];

    return sum;
}


#ifndef CHECKSUM_SSE

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


uint16_t payload_cksum(const void * const buf, const uint16_t len)
{
    const uint8_t v = ip_v(*(const uint8_t *)buf);
    uint16_t ip_hdr_len;
    uint32_t sum;

    if (v == 4) {
        const struct ip4_hdr * const ip = buf;
        ip_hdr_len = ip4_hl(*(const uint8_t *)buf);
        sum = (uint32_t)ip->p << 8;
        sum += csum_oc16((const uint8_t *)&ip->src, sizeof(ip->src));
        sum += csum_oc16((const uint8_t *)&ip->dst, sizeof(ip->dst));
        const uint16_t plen = bswap16(bswap16(ip->len) - ip_hdr_len);
        sum += csum_oc16((const uint8_t *)&plen, sizeof(plen));
    } else {
        const struct ip6_hdr * const ip = buf;
        ip_hdr_len = sizeof(*ip);
        sum = (uint32_t)ip->next_hdr << 24;
        sum += csum_oc16((const uint8_t *)&ip->src, sizeof(ip->src));
        sum += csum_oc16((const uint8_t *)&ip->dst, sizeof(ip->dst));
        sum += csum_oc16((const uint8_t *)&ip->len, sizeof(ip->len));
    }

    // payload
    sum += csum_oc16((const uint8_t *)buf + ip_hdr_len, len - ip_hdr_len);

    return csum_oc16_reduce(sum);
}

#else

#define DECLARE_ALIGNED(_declaration, _boundary)                               \
    _declaration __attribute__((aligned(_boundary)))

static const DECLARE_ALIGNED(uint8_t crc_xmm_shift_tab[48], 16) = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};


static inline __m128i __attribute__((always_inline))
xmm_shift_right(const __m128i reg, const unsigned int num)
{
    const __m128i * p =
        (const __m128i *)(const void *)(crc_xmm_shift_tab + 16 + num);
    return _mm_shuffle_epi8(reg, _mm_loadu_si128(p));
}


static inline __m128i __attribute__((always_inline))
xmm_shift_left(const __m128i reg, const unsigned int num)
{
    const __m128i * p =
        (const __m128i *)(const void *)(crc_xmm_shift_tab + 16 - num);
    return _mm_shuffle_epi8(reg, _mm_loadu_si128(p));
}


static __m128i swap16a;
static __m128i swap16b;
static __m128i udp_mask;


static void __attribute__((constructor)) cksum_sse_init(void)
{
    swap16a = _mm_setr_epi16((short)0x0001, (short)0xffff, (short)0x0203,
                             (short)0xffff, (short)0x0405, (short)0xffff,
                             (short)0x0607, (short)0xffff);

    swap16b = _mm_setr_epi16((short)0x0809, (short)0xffff, (short)0x0a0b,
                             (short)0xffff, (short)0x0c0d, (short)0xffff,
                             (short)0x0e0f, (short)0xffff);

    udp_mask = _mm_setr_epi8((char)0x01, (char)0x00, (char)0xff, (char)0xff,
                             (char)0x03, (char)0x02, (char)0xff, (char)0xff,
                             (char)0x05, (char)0x04, (char)0xff, (char)0xff,
                             (char)0x05, (char)0x04, (char)0xff, (char)0xff);
}


static inline uint32_t __attribute__((always_inline))
csum_oc16_sse(const uint8_t * const data,
              const uint32_t data_len,
              __m128i sum32a,
              __m128i sum32b)
{
    uint32_t n = 0;
    for (n = 0; (n + 64) <= data_len; n += 64) {
        __m128i dblock1;
        __m128i dblock2;

        dblock1 = _mm_loadu_si128((const __m128i *)(const void *)&data[n]);
        dblock2 = _mm_loadu_si128((const __m128i *)(const void *)&data[n + 16]);

        sum32a = _mm_add_epi32(sum32a, _mm_shuffle_epi8(dblock1, swap16a));
        sum32b = _mm_add_epi32(sum32b, _mm_shuffle_epi8(dblock1, swap16b));
        sum32a = _mm_add_epi32(sum32a, _mm_shuffle_epi8(dblock2, swap16a));
        sum32b = _mm_add_epi32(sum32b, _mm_shuffle_epi8(dblock2, swap16b));

        dblock1 = _mm_loadu_si128((const __m128i *)(const void *)&data[n + 32]);
        dblock2 = _mm_loadu_si128((const __m128i *)(const void *)&data[n + 48]);

        sum32a = _mm_add_epi32(sum32a, _mm_shuffle_epi8(dblock1, swap16a));
        sum32b = _mm_add_epi32(sum32b, _mm_shuffle_epi8(dblock1, swap16b));
        sum32a = _mm_add_epi32(sum32a, _mm_shuffle_epi8(dblock2, swap16a));
        sum32b = _mm_add_epi32(sum32b, _mm_shuffle_epi8(dblock2, swap16b));
    }

    while ((n + 16) <= data_len) {
        __m128i dblock;

        dblock = _mm_loadu_si128((const __m128i *)(const void *)&data[n]);
        sum32a = _mm_add_epi32(sum32a, _mm_shuffle_epi8(dblock, swap16a));
        sum32b = _mm_add_epi32(sum32b, _mm_shuffle_epi8(dblock, swap16b));
        n += 16;
    }

    if (likely(n != data_len)) {
        __m128i dblock;

        dblock = _mm_loadu_si128((const __m128i *)(const void *)&data[n]);
        dblock = xmm_shift_left(dblock, 16 - (data_len & 15));
        dblock = xmm_shift_right(dblock, 16 - (data_len & 15));
        sum32a = _mm_add_epi32(sum32a, _mm_shuffle_epi8(dblock, swap16a));
        sum32b = _mm_add_epi32(sum32b, _mm_shuffle_epi8(dblock, swap16b));
    }

    sum32a = _mm_add_epi32(sum32a, sum32b);
    sum32a = _mm_hadd_epi32(sum32a, _mm_setzero_si128());
    sum32a = _mm_hadd_epi32(sum32a, _mm_setzero_si128());
    return (uint32_t)_mm_extract_epi32(sum32a, 0);
}


uint16_t ip_cksum(const void * const buf, const uint16_t len)
{
    const uint32_t sum =
        csum_oc16_sse(buf, len, _mm_setzero_si128(), _mm_setzero_si128());

    return bswap16(csum_oc16_reduce(sum));
}


uint16_t
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 8)
    __attribute__((no_sanitize("alignment")))
#endif
    udp_cksum(const void * const buf, const uint16_t len)
{
    const struct ip4_hdr * ip = (const struct ip4_hdr *)buf;
    __m128i sum32a;
    __m128i sum32b;
    uint32_t sum;

    /**
     * Do pseudo IPv4 header
     * Load source and destination addresses from IP header
     * Swap 16-bit words from big endian to little endian
     * Extend 16 bit words to 32 bit words for further with SSE
     */
    sum32a = _mm_loadu_si128(
        (const __m128i *)(const void *)((const uint8_t *)buf +
                                        __builtin_offsetof(struct ip4_hdr,
                                                           src)));
    sum32a = _mm_shuffle_epi8(sum32a, swap16a);

    /**
     * Read UDP header
     * Duplicate length field as it wasn't included in IPv4 pseudo header
     * Swap 16-bit words from big endian to little endian
     * Extend 16 bit words to 32 bit words for further with SSE
     */
    sum32b = _mm_loadu_si128(
        (const __m128i *)(const void *)((const uint8_t *)buf +
                                        sizeof(struct ip4_hdr)));
    sum32b = _mm_shuffle_epi8(sum32b, udp_mask);

    sum = csum_oc16_sse((const uint8_t *)buf + sizeof(struct ip4_hdr) +
                            sizeof(struct udp_hdr),
                        len - sizeof(struct ip4_hdr) - sizeof(struct udp_hdr),
                        sum32a, sum32b) +
          ((uint32_t)ip->p);

    return bswap16(csum_oc16_reduce(sum));
}
#endif
