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

#include <cstdint>

#include <benchmark/benchmark.h>
#include <warpcore/warpcore.h>

extern "C" {
#include "common.h"
// #include "in_cksum.h"
}


static void BM_io(benchmark::State & state)
{
    const auto len = uint32_t(state.range(0));
    for (auto _ : state)
        if (!io(len)) {
            state.SkipWithError("ran out of bufs or saw packet loss");
            return;
        }
    state.SetBytesProcessed(int64_t(state.iterations()) * len *
                            w_max_udp_payload(s_serv));
}


// static void BM_ip_cksum(benchmark::State & state)
// {
//     const auto len = uint16_t(state.range(0));
//     auto * buf = new char[len];
//     memset(buf, 'x', len);
//     for (auto _ : state)
//         benchmark::DoNotOptimize(ip_cksum(buf, len));
//     state.SetBytesProcessed(int64_t(state.iterations()) * len);
//     delete[] buf;
// }


// static void BM_arc4random(benchmark::State & state)
// {
//     for (auto _ : state)
//         benchmark::DoNotOptimize(arc4random());
// }


// static void BM_random(benchmark::State & state)
// {
//     for (auto _ : state)
//         benchmark::DoNotOptimize(random()); // NOLINT
// }


// static void BM_w_rand(benchmark::State & state)
// {
//     for (auto _ : state)
//         benchmark::DoNotOptimize(w_rand());
// }


BENCHMARK(BM_io)->RangeMultiplier(2)->Range(1, 512);
// BENCHMARK(BM_ip_cksum)->RangeMultiplier(2)->Range(64, 2048);
// BENCHMARK(BM_arc4random);
// BENCHMARK(BM_random);
// BENCHMARK(BM_w_rand);


// BENCHMARK_MAIN()

int main(int argc, char ** argv)
{
    benchmark::Initialize(&argc, argv);
    util_dlevel = WRN;
    init(8192);
    benchmark::RunSpecifiedBenchmarks();
    cleanup();
}
