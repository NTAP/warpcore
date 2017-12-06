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

#include <benchmark/benchmark.h>
#include <cstdint>
#include <cstring>
#include <warpcore/warpcore.h>

extern "C" {
#include "common.h"
#include "ip.h"
}


static void BM_io(benchmark::State & state)
{
    const auto len = uint32_t(state.range(0));
    while (state.KeepRunning())
        if (!io(len)) {
            state.SkipWithError("ran out of bufs or saw packet loss");
            break;
        }
    state.SetBytesProcessed(state.iterations() * len * w_mtu(w_serv));
}


static void BM_in_cksum(benchmark::State & state)
{
    const auto len = uint16_t(state.range(0));
    auto * buf = new char[len];
    memset(buf, 'x', len);
    while (state.KeepRunning())
        in_cksum(buf, len);
    state.SetBytesProcessed(state.iterations() * len);
    delete[] buf;
}


static void BM_arc4random(benchmark::State & state)
{
    while (state.KeepRunning())
        arc4random();
}


static void BM_random(benchmark::State & state)
{
    while (state.KeepRunning())
        random();
}

/* The state must be seeded so that it is not all zero */
static uint64_t s[2];

static uint64_t xorshift128plus(void)
{
    uint64_t x = s[0];
    uint64_t const y = s[1];
    s[0] = y;
    x ^= x << 23;                         // a
    s[1] = x ^ y ^ (x >> 17) ^ (y >> 26); // b, c
    return s[1] + y;
}

static void BM_xorshift128plus(benchmark::State & state)
{
    while (state.KeepRunning())
        xorshift128plus();
}


BENCHMARK(BM_io)->RangeMultiplier(2)->Range(16, 8192);
BENCHMARK(BM_in_cksum)->RangeMultiplier(2)->Range(4, 2048);
BENCHMARK(BM_arc4random);
BENCHMARK(BM_random);
BENCHMARK(BM_xorshift128plus);


// BENCHMARK_MAIN()

int main(int argc, char ** argv)
{
    benchmark::Initialize(&argc, argv);
#ifndef NDEBUG
    util_dlevel = WRN;
#endif
    init(8192);
    benchmark::RunSpecifiedBenchmarks();
    cleanup();
}
