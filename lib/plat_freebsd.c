#include <stdlib.h>
#include <sys/types.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <sys/param.h>
#include <sys/cpuset.h>
#include <ifaddrs.h>
#include <net/if.h>

#include "plat.h"
#include "debug.h"
#include "eth.h"

void
plat_srandom(void)
{
	srandomdev();
}


void
plat_setaffinity(void)
{
	int i;
	cpuset_t myset;
	if (cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID, -1,
	    sizeof(cpuset_t), &myset) == -1) {
		warn(crit, "cpuset_getaffinity failed");
		return;
	}

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

	if (cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID, -1,
	    sizeof(cpuset_t), &myset) == -1)
		warn(crit, "cpuset_setaffinity failed");
}

void
plat_get_mac(uint8_t *mac, const struct ifaddrs *i)
{
	memcpy(mac, LLADDR((struct sockaddr_dl *)i->ifa_addr), ETH_ADDR_LEN);
}


uint16_t
plat_get_mtu(const struct ifaddrs *i)
{
	return (uint16_t)((struct if_data *)(i->ifa_data))->ifi_mtu;
}


uint32_t
plat_get_mbps(const struct ifaddrs *i)
{
	return (uint32_t)(((struct if_data *)(i->ifa_data))->ifi_baudrate/1000000);
}


bool
plat_get_link(const struct ifaddrs *i)
{
	return (((uint8_t)((struct if_data *)(i->ifa_data))->ifi_link_state) ==
	        LINK_STATE_UP);
}
