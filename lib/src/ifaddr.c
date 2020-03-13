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


#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#ifndef PARTICLE
#include <ifaddrs.h>
#include <net/if.h>
#else
// typedef struct if_list if_list;

// #include <arpa/inet.h>
// #include <ifapi.h>
// #include <lwip/sockets.h>
// #include <rng_hal.h>
// #include <socket_hal.h>
// #include <system_network.h>

#define getifaddrs if_get_if_addrs
#define freeifaddrs if_free_if_addrs
#define ifa_next next
#define ifa_name ifname
#define ifa_flags ifflags
#define ifa_addr if_addr->addr
#define ifa_netmask if_addr->netmask
#endif

#include <warpcore/warpcore.h>

#include "backend.h"
#include "ifaddr.h"


uint16_t backend_addr_cnt(const char * const ifname)
{
    uint16_t addr_cnt = 0;
    struct ifaddrs * ifap;
    ensure(getifaddrs(&ifap) != -1, "%s: cannot get interface info", ifname);
    for (struct ifaddrs * i = ifap; i; i = i->ifa_next) {
        if (strcmp(i->ifa_name, ifname) != 0)
            continue;

        if (plat_get_link(i) == false)
            continue;

        if (i->ifa_addr->sa_family == AF_INET ||
            i->ifa_addr->sa_family == AF_INET6)
            addr_cnt++;
    }
    freeifaddrs(ifap);
    return addr_cnt;
}


void backend_addr_config(struct w_engine * const w)
{
    // construct interface name of a netmap pipe for this interface
    char pipe[IFNAMSIZ];
    snprintf(pipe, IFNAMSIZ, "w-%.*s", IFNAMSIZ - 3, w->ifname);

    struct ifaddrs * ifap;
    ensure(getifaddrs(&ifap) != -1, "%s: cannot get interface info", w->ifname);

    w->addr4_pos = w->addr_cnt - 1;
    uint16_t addr6_pos = 0;
    for (struct ifaddrs * i = ifap; i; i = i->ifa_next) {
        if (strcmp(i->ifa_name, pipe) == 0)
            w->is_right_pipe = true;

        if (strcmp(i->ifa_name, w->ifname) != 0)
            continue;

        if (addr6_pos > w->addr4_pos) {
            warn(WRN, "%s: unexpectedly many addresses", w->ifname);
            break;
        }

        switch (i->ifa_addr->sa_family) {
        case AF_LINK:
            w->is_loopback = ((i->ifa_flags & IFF_LOOPBACK) != 0);
            plat_get_mac(&w->mac, i);
            w->mtu = plat_get_mtu(i);
            // mpbs can be zero on generic platforms and loopback interfaces
            w->mbps = plat_get_mbps(i);
            plat_get_iface_driver(i, w->drvname, sizeof(w->drvname));
            break;

        case AF_INET6:;
            struct w_ifaddr * ia = &w->ifaddr[addr6_pos];
            if (w_to_waddr(&ia->addr, i->ifa_addr) == false)
                continue;
            ia->scope_id =
                ((struct sockaddr_in6 *)(void *)i->ifa_addr)->sin6_scope_id;
            ip6_config(
                ia,
                (const uint8_t *)&(
                    (const struct sockaddr_in6 *)(const void *)i->ifa_netmask)
                    ->sin6_addr);
            w->have_ip6 = true;
            addr6_pos++;
            break;

        case AF_INET:
            ia = &w->ifaddr[w->addr4_pos];
            if (w_to_waddr(&ia->addr, i->ifa_addr) == false)
                continue;
            const void * const sa_mask4 =
                &((const struct sockaddr_in *)(const void *)i->ifa_netmask)
                     ->sin_addr;
            ia->prefix = contig_mask_len(ia->addr.af, sa_mask4);

            uint32_t mask4;
            memcpy(&mask4, sa_mask4, sizeof(mask4));
            ia->bcast4 = ia->addr.ip4 | ~mask4;
            w->have_ip4 = true;
            w->addr4_pos--;
            break;

        default:
            warn(NTE, "ignoring unknown addr family %d on %s",
                 i->ifa_addr->sa_family, i->ifa_name);
            break;
        }
    }
    w->addr4_pos++; // this will be decremented by one too many above
    freeifaddrs(ifap);
}
