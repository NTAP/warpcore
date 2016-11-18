#include <ifaddrs.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <netinet/ether.h>
#include <netpacket/packet.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "eth.h"
#include "plat.h"
#include "util.h"


// void plat_setaffinity(void)
// {
//     int i;
//     cpu_set_t myset;
//     assert(sched_getaffinity(0, sizeof(cpu_set_t), &myset) != -1,
//            "sched_getaffinity");

//     // Find last available CPU
//     for (i = CPU_SETSIZE - 1; i >= -1; i--)
//         if (CPU_ISSET(i, &myset))
//             break;
//     assert(i != -1, "not allowed to run on any CPUs!?");

//     // Set new CPU mask
//     warn(info, "setting affinity to CPU %d", i);
//     CPU_ZERO(&myset);
//     CPU_SET(i, &myset);

//     assert(sched_setaffinity(0, sizeof(myset), &myset) != -1,
//            "sched_setaffinity");
// }


void plat_get_mac(uint8_t * mac, const struct ifaddrs * i)
{
    memcpy(mac, ((struct sockaddr_ll *)(void *)i->ifa_addr)->sll_addr,
           ETH_ADDR_LEN);
}


uint16_t plat_get_mtu(const struct ifaddrs * i)
{
    const int s = socket(AF_INET, SOCK_DGRAM, 0);
    assert(s >= 0, "%s socket", i->ifa_name);

    struct ifreq ifr;
    bzero(&ifr, sizeof(struct ifreq));
    strcpy(ifr.ifr_name, i->ifa_name);

    assert(ioctl(s, SIOCGIFMTU, &ifr) >= 0, "%s ioctl", i->ifa_name);

    const uint16_t mtu = (uint16_t)ifr.ifr_ifru.ifru_mtu;
    close(s);

    return mtu;
}


uint32_t plat_get_mbps(const struct ifaddrs * i)
{
    const int s = socket(AF_INET, SOCK_DGRAM, 0);
    assert(s >= 0, "%s socket", i->ifa_name);

    struct ifreq ifr;
    bzero(&ifr, sizeof(struct ifreq));
    strcpy(ifr.ifr_name, i->ifa_name);

    struct ethtool_cmd edata;
    ifr.ifr_data = (__caddr_t)&edata;
    edata.cmd = ETHTOOL_GSET;
    assert(ioctl(s, SIOCETHTOOL, &ifr) >= 0, "%s ioctl", i->ifa_name);

    return ethtool_cmd_speed(&edata);
}


bool plat_get_link(const struct ifaddrs * i)
{
    const int s = socket(AF_INET, SOCK_DGRAM, 0);
    assert(s >= 0, "%s socket", i->ifa_name);

    struct ifreq ifr;
    bzero(&ifr, sizeof(struct ifreq));
    strcpy(ifr.ifr_name, i->ifa_name);

    assert(ioctl(s, SIOCGIFFLAGS, &ifr) >= 0, "%s ioctl", i->ifa_name);

    const bool link = (ifr.ifr_flags & IFF_UP) && (ifr.ifr_flags & IFF_RUNNING);
    close(s);

    return link;
}
