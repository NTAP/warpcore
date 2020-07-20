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

#include <string.h>

#include <stdlib.h>

#include <warpcore/warpcore.h>

#include "arp.h"
#include "backend.h"
#include "eth.h"
#include "icmp6.h"
#include "neighbor.h"


/// Update the MAC address associated with IP address @p addr in the neighbor
/// cache.
///
/// @param      w     Backend engine.
/// @param[in]  addr  IP address to update the neighbor cache for.
/// @param[in]  mac   New Ethernet MAC address of @p addr.
///
void neighbor_update(struct w_engine * const w,
                     const struct w_addr * const addr,
                     const struct eth_addr mac)
{
    khiter_t k = kh_get(neighbor, &w->b->neighbor, addr);
    if (k == kh_end(&w->b->neighbor)) {
        struct w_addr * const a = malloc(sizeof(*addr));
        ensure(a, "could not malloc");
        memcpy(a, addr, sizeof(*addr));
        int ret;
        k = kh_put(neighbor, &w->b->neighbor, a, &ret); // NOLINT
        assure(ret >= 1, "inserted");
    }
    kh_val(&w->b->neighbor, k) = mac;

    warn(INF, "neighbor cache entry: %s is at %s", w_ntop(addr, ip_tmp),
         eth_ntoa(&mac, eth_tmp, ETH_STRLEN));
}


/// Find the neighbor cache entry associated with IP address @p ip.
///
/// @param      w     Backend engine.
/// @param[in]  addr  IP address to look up in neighbor cache.
///
/// @return     Pointer to eth_addr of @p addr, or zero.
///
static struct eth_addr __attribute__((nonnull))
neighbor_find(struct w_engine * const w, const struct w_addr * const addr)
{
    const khiter_t k = kh_get(neighbor, &w->b->neighbor, addr);
    if (unlikely(k == kh_end(&w->b->neighbor)))
        return (struct eth_addr){ETH_ADDR_BCAST};
    return kh_val(&w->b->neighbor, k);
}


/// Return the Ethernet MAC address for target IP address @p addr. If there is
/// no entry in the neighbor cache for the Ethernet MAC address corresponding to
/// IPv4 address @p addr, this function will block while attempting to resolve
/// the address.
///
/// @param      w     Backend engine.
/// @param[in]  addr  IP address that is the target of the neighbor request
///
/// @return     Ethernet MAC address of @p addr.
///
struct eth_addr
#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 8)
    __attribute__((no_sanitize("alignment")))
#endif
    who_has(struct w_engine * const w, const struct w_addr * const addr)
{
    struct eth_addr a = neighbor_find(w, addr);
    while (unlikely(memcmp(&a, ETH_ADDR_BCAST, sizeof(a)) == 0)) {
        warn(INF, "no neighbor entry for %s, sending query",
             w_ntop(addr, ip_tmp));

        if (addr->af == AF_INET)
            arp_who_has(w, addr->ip4);
        else
            icmp6_nsol(w, addr->ip6);

        // wait until packets have been received, then handle them
        w_nic_rx(w, 1 * NS_PER_S);

        // check if we can now resolve dip
        a = neighbor_find(w, addr);
    }
    return a;
}


/// Free the neighbor cache entries associated with engine @p w.
///
/// @param[in]  w     Backend engine.
///
void free_neighbor(struct w_engine * const w)
{
    const struct w_addr * k;
    struct eth_addr v;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-qual"
    kh_foreach(&w->b->neighbor, k, v, { free((void *)k); });
#pragma clang diagnostic pop

    kh_release(neighbor, &w->b->neighbor);
}
