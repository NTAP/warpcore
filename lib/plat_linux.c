#include <netinet/ether.h>
#include <netpacket/packet.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <sched.h>
#include <time.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "plat.h"
#include "debug.h"
#include "eth.h"

void
plat_srandom(void)
{
	srandom(time(0));
}


void
plat_setaffinity(void)
{
	int i;
	cpu_set_t myset;
	if (sched_getaffinity(0, sizeof(cpu_set_t), &myset) == -1)
		die("sched_getaffinity");

	// Find last available CPU
	for (i = CPU_SETSIZE-1; i >= 0; i--)
		if (CPU_ISSET(i, &myset))
			break;
	if (i == 0)
		die("not allowed to run on any CPUs!?");

	// Set new CPU mask
	warn(info, "setting affinity to CPU %d", i);
	CPU_ZERO(&myset);
	CPU_SET(i, &myset);

	if (sched_setaffinity(0, sizeof(myset), &myset) == -1)
		die("sched_setaffinity");
}


void
plat_get_mac(uint8_t *mac, const struct ifaddrs *i)
{
	memcpy(mac, ((struct sockaddr_ll *)i->ifa_addr)->sll_addr,
	       ETH_ADDR_LEN);
}


uint16_t
plat_get_mtu(const struct ifaddrs *i)
{
	int s;
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		die("%s socket", i->ifa_name);

	struct ifreq ifr;
	bzero(&ifr, sizeof(struct ifreq));
	strcpy(ifr.ifr_name, i->ifa_name);

	if (ioctl(s, SIOCGIFMTU, &ifr) < 0)
		die("%s ioctl", i->ifa_name);

	const uint16_t mtu = ifr.ifr_ifru.ifru_mtu;
	close(s);

	return mtu;
}


uint32_t
plat_get_mbps(const struct ifaddrs *i)
{
	int s;
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		die("%s socket", i->ifa_name);

	struct ifreq ifr;
	bzero(&ifr, sizeof(struct ifreq));
	strcpy(ifr.ifr_name, i->ifa_name);

	struct ethtool_cmd edata;
	ifr.ifr_data = (__caddr_t)&edata;
	edata.cmd = ETHTOOL_GSET;
	if (ioctl(s, SIOCETHTOOL, &ifr) < 0)
		die("%s ioctl", i->ifa_name);

	return ethtool_cmd_speed(&edata);
}


bool
plat_get_link(const struct ifaddrs *i)
{
	int s;
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		die("%s socket", i->ifa_name);

	struct ifreq ifr;
	bzero(&ifr, sizeof(struct ifreq));
	strcpy(ifr.ifr_name, i->ifa_name);

	if (ioctl(s, SIOCGIFFLAGS, &ifr) < 0)
		die("%s ioctl", i->ifa_name);

	const bool link = (ifr.ifr_flags & IFF_UP) &&
			  (ifr.ifr_flags & IFF_RUNNING);
	close(s);

	return link;
}
