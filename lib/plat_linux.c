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
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "eth.h"
#include "plat.h" // IWYU pragma: keep
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
    ensure(s >= 0, "%s socket", i->ifa_name);

    struct ifreq ifr = {0};
    strcpy(ifr.ifr_name, i->ifa_name);

    ensure(ioctl(s, SIOCGIFMTU, &ifr) >= 0, "%s ioctl", i->ifa_name);

    // loopback MTU is 65536 on Linux, which is larger than what an IP header
    // can encode - sigh
    const uint16_t mtu = MIN(UINT16_MAX, (uint16_t)ifr.ifr_ifru.ifru_mtu);

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
    ensure(s >= 0, "%s socket", i->ifa_name);

    struct ifreq ifr = {0};
    strcpy(ifr.ifr_name, i->ifa_name);

    // if this is loopback interface, SIOCETHTOOL will fail, so just return a
    // placeholder value
    ensure(ioctl(s, SIOCGIFFLAGS, &ifr) >= 0, "%s ioctl", i->ifa_name);
    if (ifr.ifr_flags & IFF_LOOPBACK) {
        close(s);
        return 0;
    }

    struct ethtool_cmd edata;
    ifr.ifr_data = (__caddr_t)&edata;
    edata.cmd = ETHTOOL_GSET;
    ensure(ioctl(s, SIOCETHTOOL, &ifr) >= 0, "%s ioctl", i->ifa_name);

    close(s);
    const uint32_t speed = ethtool_cmd_speed(&edata);
    return speed != (uint32_t)SPEED_UNKNOWN ? speed : 0;
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
    ensure(s >= 0, "%s socket", i->ifa_name);

    struct ifreq ifr = {0};
    strcpy(ifr.ifr_name, i->ifa_name);

    ensure(ioctl(s, SIOCGIFFLAGS, &ifr) >= 0, "%s ioctl", i->ifa_name);

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
    ensure(sched_getaffinity(0, sizeof(cpu_set_t), &myset) != -1,
           "sched_getaffinity");

    // Find last available CPU
    for (i = CPU_SETSIZE - 1; i >= -1; i--)
        if (CPU_ISSET(i, &myset))
            break;
    ensure(i != -1, "not allowed to run on any CPUs!?");

    // Set new CPU mask
    warn(info, "setting affinity to CPU %d", i);
    CPU_ZERO(&myset);
    CPU_SET(i, &myset);

    ensure(sched_setaffinity(0, sizeof(myset), &myset) != -1,
           "sched_setaffinity");
}
