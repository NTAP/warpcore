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
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <netpacket/packet.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

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
    memcpy(mac, ((struct sockaddr_ll *)(void *)i->ifa_addr)->sll_addr,
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
    const int s = socket(AF_INET, SOCK_DGRAM, 0);
    assert(s >= 0, "%s socket", i->ifa_name);

    struct ifreq ifr = {0};
    strcpy(ifr.ifr_name, i->ifa_name);

    assert(ioctl(s, SIOCGIFMTU, &ifr) >= 0, "%s ioctl", i->ifa_name);

    const uint16_t mtu = (uint16_t)ifr.ifr_ifru.ifru_mtu;
    close(s);

    return mtu;
}


/// Return the link speed in Mb/s of network interface @p i.
///
/// @param[in]  i     A network interface.
///
/// @return     Link speed of interface @p i.
///
uint32_t plat_get_mbps(const struct ifaddrs * i)
{
    const int s = socket(AF_INET, SOCK_DGRAM, 0);
    assert(s >= 0, "%s socket", i->ifa_name);

    struct ifreq ifr = {0};
    strcpy(ifr.ifr_name, i->ifa_name);

    struct ethtool_cmd edata;
    ifr.ifr_data = (__caddr_t)&edata;
    edata.cmd = ETHTOOL_GSET;
    assert(ioctl(s, SIOCETHTOOL, &ifr) >= 0, "%s ioctl", i->ifa_name);

    return ethtool_cmd_speed(&edata);
}


/// Return the link status of network interface @p i.
///
/// @param[in]  i     A network interface.
///
/// @return     Link status of interface @p i. True means link is up.
///
bool plat_get_link(const struct ifaddrs * i)
{
    const int s = socket(AF_INET, SOCK_DGRAM, 0);
    assert(s >= 0, "%s socket", i->ifa_name);

    struct ifreq ifr = {0};
    strcpy(ifr.ifr_name, i->ifa_name);

    assert(ioctl(s, SIOCGIFFLAGS, &ifr) >= 0, "%s ioctl", i->ifa_name);

    const bool link = (ifr.ifr_flags & IFF_UP) && (ifr.ifr_flags & IFF_RUNNING);
    close(s);

    return link;
}


/// Sets the affinity of the current thread to the highest existing CPU core.
///
void plat_setaffinity(void)
{
    int i;
    cpu_set_t myset;
    assert(sched_getaffinity(0, sizeof(cpu_set_t), &myset) != -1,
           "sched_getaffinity");

    // Find last available CPU
    for (i = CPU_SETSIZE - 1; i >= -1; i--)
        if (CPU_ISSET(i, &myset))
            break;
    assert(i != -1, "not allowed to run on any CPUs!?");

    // Set new CPU mask
    warn(info, "setting affinity to CPU %d", i);
    CPU_ZERO(&myset);
    CPU_SET(i, &myset);

    assert(sched_setaffinity(0, sizeof(myset), &myset) != -1,
           "sched_setaffinity");
}
