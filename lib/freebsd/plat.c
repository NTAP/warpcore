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

#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <sys/cdefs.h>
// clang-format off
// because these includes need to be in-order
#include <sys/types.h>
#include <sys/cpuset.h>
// clang-format on
#include <string.h>

#include "eth.h"
#include "plat.h"
#include "util.h"


/// Return the Ethernet MAC address of network interface @p i.
///
/// @param[out] mac   A buffer of at least ETH_ADDR_LEN bytes.
/// @param[in]  i     A network interface.
///
void plat_get_mac(uint8_t * mac, const struct ifaddrs * i)
{
    memcpy(mac, LLADDR((struct sockaddr_dl *)(void *)i->ifa_addr),
           ETH_ADDR_LEN);
}


/// Return the MTU of network interface @p i.
///
/// @param[in]  i     A network interface.
///
/// @return     The MTU of @p i.
///
uint16_t plat_get_mtu(const struct ifaddrs * i)
{
    return (uint16_t)((struct if_data *)(i->ifa_data))->ifi_mtu;
}


/// Return the link speed in Mb/s of network interface @p i.
///
/// @param[in]  i     A network interface.
///
/// @return     Link speed of interface @p i.
///
uint32_t plat_get_mbps(const struct ifaddrs * i)
{
    return (uint32_t)(((struct if_data *)(i->ifa_data))->ifi_baudrate /
                      1000000);
}


/// Return the link status of network interface @p i.
///
/// @param[in]  i     A network interface.
///
/// @return     Link status of interface @p i. True means link is up.
///
bool plat_get_link(const struct ifaddrs * i)
{
    if ((i->ifa_flags & (IFF_LOOPBACK | IFF_UP)) == (IFF_LOOPBACK | IFF_UP))
        return true;
    return (((uint8_t)((struct if_data *)(i->ifa_data))->ifi_link_state) &
            LINK_STATE_UP);
}


/// Sets the affinity of the current thread to the highest existing CPU core.
///
void plat_setaffinity(void)
{
    int i;
    cpuset_t myset;
    if (cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID, -1, sizeof(cpuset_t),
                           &myset) == -1) {
        warn(crit, "cpuset_getaffinity failed");
        return;
    }

    // Find last available CPU
    for (i = CPU_SETSIZE - 1; i >= 0; i--)
        if (CPU_ISSET(i, &myset))
            break;
    assert(i != 0, "not allowed to run on any CPUs!?");

    // Set new CPU mask
    warn(info, "setting affinity to CPU %d", i);
    CPU_ZERO(&myset);
    CPU_SET(i, &myset);

    if (cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID, -1, sizeof(cpuset_t),
                           &myset) == -1)
        warn(crit, "cpuset_setaffinity failed");
}
