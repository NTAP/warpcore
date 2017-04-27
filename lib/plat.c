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

#if defined(__FreeBSD__)
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#elif defined(__linux__)
#include <ifaddrs.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <netpacket/packet.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#else
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#endif

#include <warpcore.h> // IWYU pragma: keep

#include "eth.h"

#if !defined(__FreeBSD__) && !defined(__linux__)
struct ifaddrs;
#endif


/// Return the Ethernet MAC address of network interface @p i.
///
/// @param[out] mac   A buffer of at least ETH_ADDR_LEN bytes.
/// @param[in]  i     A network interface.
///
void plat_get_mac(uint8_t * mac,
                  const struct ifaddrs * i
#if !defined(__FreeBSD__) && !defined(__linux__)
                  __attribute__((unused))
#endif
                  )
{
#if defined(__FreeBSD__)
    memcpy(mac, LLADDR((struct sockaddr_dl *)(void *)i->ifa_addr),
           ETH_ADDR_LEN);
#elif defined(__linux__)
    memcpy(mac, ((struct sockaddr_ll *)(void *)i->ifa_addr)->sll_addr,
           ETH_ADDR_LEN);
#else
    warn(warn, "MAC address queries not supported on this platform");
    memcpy(mac, "\xde\xad\xde\xad\xde\xad", ETH_ADDR_LEN);
#endif
}


/// Return the MTU of network interface @p i.
///
/// @param[in]  i     A network interface.
///
/// @return     The MTU of @p i.
///
uint16_t plat_get_mtu(const struct ifaddrs * i
#if !defined(__FreeBSD__) && !defined(__linux__)
                      __attribute__((unused))
#endif
                      )
{
#if defined(__FreeBSD__)
    return (uint16_t)((struct if_data *)(i->ifa_data))->ifi_mtu;
#elif defined(__linux__)
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
#else
    warn(warn, "MTU queries not supported on this platform");
    return 1500;
#endif
}


/// Return the link speed in Mb/s of network interface @p i.
///
/// @param[in]  i     A network interface.
///
/// @return     Link speed of interface @p i.
///
uint32_t plat_get_mbps(const struct ifaddrs * i
#if !defined(__FreeBSD__) && !defined(__linux__)
                       __attribute__((unused))
#endif
                       )
{
#if defined(__FreeBSD__)
    return (uint32_t)(((struct if_data *)(i->ifa_data))->ifi_baudrate /
                      1000000);
#elif defined(__linux__)
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
#else
    warn(warn, "link speed queries not supported on this platform");
    return UINT32_MAX;
#endif
}


/// Return the link status of network interface @p i.
///
/// @param[in]  i     A network interface.
///
/// @return     Link status of interface @p i. True means link is up.
///
bool plat_get_link(const struct ifaddrs * i
#if !defined(__FreeBSD__) && !defined(__linux__)
                   __attribute__((unused))
#endif
                   )
{
#if defined(__FreeBSD__)
    if ((i->ifa_flags & (IFF_LOOPBACK | IFF_UP)) == (IFF_LOOPBACK | IFF_UP))
        return true;
    return (((struct if_data *)(i->ifa_data))->ifi_link_state) & LINK_STATE_UP;
#elif defined(__linux__)
    const int s = socket(AF_INET, SOCK_DGRAM, 0);
    ensure(s >= 0, "%s socket", i->ifa_name);

    struct ifreq ifr = {0};
    strcpy(ifr.ifr_name, i->ifa_name);

    ensure(ioctl(s, SIOCGIFFLAGS, &ifr) >= 0, "%s ioctl", i->ifa_name);

    const bool link = (ifr.ifr_flags & IFF_UP) && (ifr.ifr_flags & IFF_RUNNING);

    close(s);
    return link;
#else
    warn(warn, "link state queries not supported on this platform");
    return true;
#endif
}
