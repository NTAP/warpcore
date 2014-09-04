#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <net/ethernet.h>
#include <net/if_dl.h>

#include "warpcore.h"
#include "ip.h"


struct warpcore * w_open(const char * const ifname)
{
	struct warpcore *w;

	// allocate struct
	if ((w = calloc(1, sizeof *w)) == NULL) {
		perror("cannot allocate struct warpcore");
		abort();
	}

	// open /dev/netmap
	if ((w->fd = open("/dev/netmap", O_RDWR)) == -1) {
		perror("cannot open /dev/netmap");
		abort();
	}

	// get interface information
	struct ifaddrs *ifap;
	if (getifaddrs(&ifap) == -1) {
		perror("cannot get interface information");
		abort();
	}
	for (struct ifaddrs *i = ifap; i->ifa_next != NULL; i = i->ifa_next) {
		if (strcmp(i->ifa_name, ifname) == 0) {
			char mac[ETH_ADDR_LEN*3];
			char ip[IP_ADDR_STRLEN];
			char mask[IP_ADDR_STRLEN];
			switch (i->ifa_addr->sa_family) {
			case AF_LINK:
				memcpy(&w->mac,
				       LLADDR((struct sockaddr_dl *)
				              i->ifa_addr),
				       sizeof w->mac);
				D("%s has Ethernet address %s", i->ifa_name,
				  ether_ntoa_r((struct ether_addr *)w->mac,
				               mac));
				break;
			case AF_INET:
				w->ip = ((struct sockaddr_in *)
				         i->ifa_addr)->sin_addr.s_addr;
				w->mask = ((struct sockaddr_in *)
				           i->ifa_netmask)->sin_addr.s_addr;
				D("%s has IP address %s/%s", i->ifa_name,
				  ip_ntoa_r(w->ip, ip, sizeof ip),
				  ip_ntoa_r(w->mask, mask, sizeof mask));
				break;
			default:
				D("ignoring unknown address family %d on %s",
				  i->ifa_addr->sa_family, i->ifa_name);
				break;
			}
		}
	}
	freeifaddrs(ifap);
	if (w->ip == 0 || w->mask == 0 ||
	    (w->mac[0] & w->mac[1] & w->mac[2] &
	     w->mac[3] & w->mac[4] & w->mac[5] < 0)) {
		D("could not obtain needed information");
		abort();
	}

	w->bcast = w->ip | (~w->mask);
	char bcast[IP_ADDR_STRLEN];
	D("%s has IP broadcast address %s", ifname,
	  ip_ntoa_r(w->bcast, bcast, sizeof bcast));

	// switch interface to netmap mode
	strncpy(w->req.nr_name, ifname, sizeof w->req.nr_name);
	w->req.nr_name[sizeof w->req.nr_name - 1] = '\0';
	w->req.nr_version = NETMAP_API;
	w->req.nr_ringid &= ~NETMAP_RING_MASK;
	w->req.nr_flags = NR_REG_ALL_NIC;
	// TODO: figure out NETMAP_NO_TX_POLL/NETMAP_DO_RX_POLL
	if (ioctl(w->fd, NIOCREGIF, &w->req) == -1) {
		perror("cannot put interface into netmap mode");
		abort();
	}

	// mmap the buffer region
	// TODO: see TODO in nm_open() in netmap_user.h
	if ((w->mem = mmap(0, w->req.nr_memsize, PROT_WRITE|PROT_READ,
	    MAP_SHARED, w->fd, 0)) == MAP_FAILED) {
		perror("cannot mmap netmap memory");
		abort();
	}

	// direct pointer to the netmap interface struct for convenience
	w->nif = NETMAP_IF(w->mem, w->req.nr_offset);

	return w;
}
