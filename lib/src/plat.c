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

// needs to come before net/ethernet.h
#include <sys/types.h> // IWYU pragma: keep

#include <ifaddrs.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>


#if defined(__linux__)
#include <errno.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <netpacket/packet.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <unistd.h>

#elif defined(__FreeBSD__)
#include <net/if_dl.h>

#elif defined(__APPLE__)
#include <net/if_dl.h>
#include <net/if_media.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <unistd.h>
#endif

#include <warpcore/warpcore.h> // IWYU pragma: keep


/// Return the Ethernet MAC address of network interface @p i.
///
/// @param[out] mac   A buffer of at least ETH_ADDR_LEN bytes.
/// @param[in]  i     A network interface.
///
void plat_get_mac(struct ether_addr * const mac, const struct ifaddrs * const i)
{
#ifdef __linux__
    memcpy(mac, ((struct sockaddr_ll *)(void *)i->ifa_addr)->sll_addr,
           ETHER_ADDR_LEN);
#else
    memcpy(mac, LLADDR((struct sockaddr_dl *)(void *)i->ifa_addr),
           ETHER_ADDR_LEN);
#endif
}


/// Return the MTU of network interface @p i.
///
/// @param[in]  i     A network interface.
///
/// @return     The MTU of @p i.
///
uint16_t plat_get_mtu(const struct ifaddrs * i)
{
#ifndef __linux__
    const struct if_data * const ifa_data = i->ifa_data;
    return (uint16_t)ifa_data->ifi_mtu;
#else
    const int s = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    ensure(s >= 0, "%s socket", i->ifa_name);

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, i->ifa_name, IFNAMSIZ);
    ifr.ifr_name[IFNAMSIZ - 1] = 0;

    ensure(ioctl(s, SIOCGIFMTU, &ifr) >= 0, "%s ioctl", i->ifa_name);
    const uint16_t mtu = (uint16_t)MIN(UINT16_MAX, ifr.ifr_ifru.ifru_mtu);

    close(s);
    return mtu;
#endif
}


/// Return the link speed in Mb/s of network interface @p i.
///
/// @param[in]  i     A network interface.
///
/// @return     Link speed of interface @p i.
///
uint32_t plat_get_mbps(const struct ifaddrs * i)
{
#if defined(__FreeBSD__)
    const struct if_data * const ifa_data = i->ifa_data;
    if ((ifa_data->ifi_link_state & LINK_STATE_UP) != LINK_STATE_UP)
        return 0;
    return (uint32_t)(ifa_data->ifi_baudrate / 1000000);
#elif defined(__APPLE__)
    const struct if_data * const ifa_data = i->ifa_data;
    // XXX this seems to contain garbage?
    return ifa_data->ifi_baudrate / 1000000;
#else
    const int s = socket(AF_INET, SOCK_DGRAM, 0);
    ensure(s >= 0, "%s socket", i->ifa_name);

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, i->ifa_name, IFNAMSIZ);
    ifr.ifr_name[IFNAMSIZ - 1] = 0;

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
    const int err = ioctl(s, SIOCETHTOOL, &ifr);
    if (err == -1 && errno == ENOTSUP) {
        // the ioctl can fail for virtual NICs
        close(s);
        return 0;
    }
    ensure(err >= 0, "%s ioctl", i->ifa_name);

    close(s);
    const uint32_t speed = ethtool_cmd_speed(&edata);
    return speed != (uint32_t)SPEED_UNKNOWN ? speed : 0;
#endif
}


/// Return the link status of network interface @p i.
///
/// @param[in]  i     A network interface.
///
/// @return     Link status of interface @p i. True means link is up.
///
bool plat_get_link(const struct ifaddrs * i)
{
#if defined(__FreeBSD__) || defined(__APPLE__)
    if ((i->ifa_flags & (IFF_LOOPBACK | IFF_UP)) == (IFF_LOOPBACK | IFF_UP))
        return true;
#endif
    bool link = false;
#ifdef __FreeBSD__
    const struct if_data * const ifa_data = i->ifa_data;
    link = ((ifa_data->ifi_link_state & LINK_STATE_UP) == LINK_STATE_UP);
#else
    const int s = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    ensure(s >= 0, "%s socket", i->ifa_name);

#ifdef __APPLE__
    struct ifmediareq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifm_name, i->ifa_name, IFNAMSIZ);
    ifr.ifm_name[IFNAMSIZ - 1] = 0;
    ensure(ioctl(s, SIOCGIFMEDIA, &ifr) >= 0, "%s ioctl", i->ifa_name);
    link = (ifr.ifm_status & IFM_AVALID) && (ifr.ifm_status & IFM_ACTIVE);
#else
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, i->ifa_name, IFNAMSIZ);
    ifr.ifr_name[IFNAMSIZ - 1] = 0;
    ensure(ioctl(s, SIOCGIFFLAGS, &ifr) >= 0, "%s ioctl", i->ifa_name);
    link = (ifr.ifr_flags & IFF_UP) && (ifr.ifr_flags & IFF_RUNNING);
#endif
    close(s);
#endif
    return link;
}


void plat_initrandom(void)
{
#ifndef HAVE_ARC4RANDOM
    // initialize random number generator
    struct timeval seed;
    gettimeofday(&seed, 0);
    srandom((seed.tv_sec * 1000) + (seed.tv_usec / 1000));
#endif
}
