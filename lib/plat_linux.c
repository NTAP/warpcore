#include <ifaddrs.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <netinet/ether.h>
#include <netpacket/packet.h>
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
