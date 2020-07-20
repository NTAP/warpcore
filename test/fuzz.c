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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <net/netmap_user.h>

#include <net/netmap.h>
#include <warpcore/warpcore.h>

#include "backend.h"
#include "eth.h"


extern int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size);
extern int LLVMFuzzerInitialize(int * argc, char *** argv);


static struct netmap_if *iface, i_init = {.ni_rx_rings = 1};
static struct w_backend b;
__extension__ static struct w_engine w = {
    .b = &b,
    .ifaddr = {[0] = {.addr = {.af = AF_INET, .ip4 = 0x0100007f},
                      .bcast4 = 0xffffff7f,
                      .prefix = 8}}};

static int init(void)
{
#ifndef NDEBUG
    util_dlevel = DBG;
#endif

    // init the interface shim
    iface = calloc(1, sizeof(*iface) + 8192);
    ensure(iface, "could not calloc");
    memcpy(iface, &i_init, sizeof(*iface));
    b.nif = iface;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-qual"
    ssize_t * ring_ofs = (ssize_t *)&iface->ring_ofs[2];
#pragma clang diagnostic pop
    *ring_ofs = (ssize_t)(iface + 128);

    // init the ring
    struct netmap_ring * r = NETMAP_TXRING(iface, 0);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-qual"
    // these are all const in netmap.h so force-overwrite
    uint32_t * num_slots = (uint32_t *)&r->num_slots;
    uint32_t * nr_buf_size = (uint32_t *)&r->nr_buf_size;
#pragma clang diagnostic pop
    *num_slots = 2;
    *nr_buf_size = 512;

    // init the slots
    for (uint32_t idx = 0; idx < r->num_slots; idx++) {
        struct netmap_slot * const s = &r->slot[idx];
        s->buf_idx = idx;
    }

    for (uint16_t p = 1; p < UINT16_MAX; p++)
        w_bind(&w, 0, bswap16(p), 0);

    return 0;
}


int LLVMFuzzerTestOneInput(const uint8_t * data, const size_t size)
{
    static int needs_init = 1;
    if (needs_init)
        needs_init = init();

    struct netmap_ring * const r = NETMAP_TXRING(iface, 0);
    r->cur = r->head = 1;
    r->tail = 0;
    struct netmap_slot * const s = &r->slot[r->cur];
    uint8_t * const buf = (uint8_t *)NETMAP_BUF(r, r->cur);

    s->len = (uint16_t)MIN(size, r->nr_buf_size);
    memcpy(buf, data, s->len);

    eth_rx(&w, s, buf);
    return 0;
}
