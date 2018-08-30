// SPDX-License-Identifier: BSD-2-Clause
//
// Copyright (c) 2014-2018, NetApp, Inc.
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

#ifdef __FreeBSD__
// needs to come before net/ethernet.h
#include <netinet/in.h> // IWYU pragma: keep
#endif

#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/if_ether.h>
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
#include <sys/types.h>
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


/// Return the link speed in Mb/s of network interface @p i. Note that at least
/// on FreeBSD, since often returns "100 Mb/s" erroneously for fast interfaces.
/// Returns UINT32_MAX when the speed could not be determined.
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
        return UINT32_MAX;
    return (uint32_t)(ifa_data->ifi_baudrate / 1000000);
#elif defined(__APPLE__)
    const struct if_data * const ifa_data = i->ifa_data;
    if ((i->ifa_flags & (IFF_LOOPBACK | IFF_UP)) == (IFF_LOOPBACK | IFF_UP) ||
        ifa_data->ifi_baudrate == 0)
        return UINT32_MAX;
    warn(ERR, "%d", ifa_data->ifi_baudrate);
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
        return UINT32_MAX;
    }

    struct ethtool_cmd edata;
    ifr.ifr_data = (char *)&edata;
    edata.cmd = ETHTOOL_GSET;
    const int err = ioctl(s, SIOCETHTOOL, &ifr);
    if (err == -1 && errno == ENOTSUP) {
        // the ioctl can fail for virtual NICs
        close(s);
        return UINT32_MAX;
    }
    ensure(err >= 0, "%s ioctl", i->ifa_name);

    close(s);

    if (edata.speed == (uint16_t)SPEED_UNKNOWN)
        return UINT32_MAX;
    return ethtool_cmd_speed(&edata);
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
    if (strncmp("vboxnet", i->ifa_name, 7) == 0 ||
        strncmp("utun", i->ifa_name, 4) == 0 ||
        strncmp("vmnet", i->ifa_name, 5) == 0)
        //  SIOCGIFMEDIA not supported by some interfaces
        link = true;
    else {
        ensure(ioctl(s, SIOCGIFMEDIA, &ifr) >= 0, "%s ioctl", i->ifa_name);
        link = (ifr.ifm_status & IFM_AVALID) && (ifr.ifm_status & IFM_ACTIVE);
    }
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


/// Return the short name of the driver associated with interface @p i.
///
/// @param[in]  i         A network interface.
/// @param      name      A string to return the interface name in.
/// @param[in]  name_len  The length of @p name.
///
extern void __attribute__((nonnull))
plat_get_iface_driver(const struct ifaddrs * const i
#if !defined(__linux__) && !defined(__FreeBSD__)
                      __attribute__((unused))
#endif
                      ,
                      char * const name,
                      const size_t name_len)
{
#if defined(__linux__)
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
        strncpy(name, "lo", name_len);
        goto done;
    }

    struct ethtool_drvinfo edata;
    ifr.ifr_data = (char *)&edata;
    edata.cmd = ETHTOOL_GDRVINFO;
    const int err = ioctl(s, SIOCETHTOOL, &ifr);
    if (err == -1 && errno == ENOTSUP)
        // the ioctl can fail for virtual NICs
        goto done;
    ensure(err >= 0, "%s ioctl", i->ifa_name);

    strncpy(name, edata.driver, name_len);

done:
    close(s);
#elif defined(__FreeBSD__)
    // XXX: this assumes that the interface has not been renamed!
    const size_t pos = strcspn(i->ifa_name, "0123456789");
    strncpy(name, i->ifa_name, name_len);
    name[pos] = 0;
#else
    strncpy(name, "unknown", name_len);
#endif
}
