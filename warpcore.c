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
#include <pthread.h>
#include <poll.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "warpcore.h"
#include "ip.h"


void * w_loop(struct warpcore *w)
{
	struct pollfd fds = { .fd = w->fd, .events = POLLIN };
	D("warpcore initialized");

	while (1) {
		int n = poll(&fds, 1, INFTIM);
		switch (n) {
		case -1:
			D("poll: %s", strerror(errno));
			abort();
			break;
		case 0:
			D("poll: timeout expired");
			break;
		default:
			// D("poll: %d descriptors ready", n);
			break;
		}

		struct netmap_ring *ring = NETMAP_RXRING(w->nif, 0);
		while (!nm_ring_empty(ring)) {
			char * const buf =
				NETMAP_BUF(ring, ring->slot[ring->cur].buf_idx);
			eth_rx(w, buf);
			ring->head = ring->cur = nm_ring_next(ring, ring->cur);
		}
	}

	return NULL;
}


void w_free(struct warpcore *w)
{
	D("warpcore shutting down");

	if (pthread_cancel(w->thr) != 0) {
		perror("cannot cancel warpcore thread");
		abort();
	}

	if (pthread_join(w->thr, NULL) != 0) {
		perror("cannot wait for exiting warpcore thread");
		abort();
	}

	if (munmap(w->mem, w->req.nr_memsize) == -1) {
		perror("cannot munmap netmap memory");
		abort();
	}

	if (close(w->fd) == -1) {
		perror("cannot close /dev/netmap");
		abort();
	}

	free(w);
}


struct warpcore * w_init(const char * const ifname)
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
		D("cannot obtain needed interface information");
		abort();
	}

	w->bcast = w->ip | (~w->mask);
	char bcast[IP_ADDR_STRLEN];
	D("%s has IP broadcast address %s", ifname,
	  ip_ntoa_r(w->bcast, bcast, sizeof bcast));

	// switch interface to netmap mode
	// TODO: figure out NETMAP_NO_TX_POLL/NETMAP_DO_RX_POLL
	strncpy(w->req.nr_name, ifname, sizeof w->req.nr_name);
	w->req.nr_name[sizeof w->req.nr_name - 1] = '\0';
	w->req.nr_version = NETMAP_API;
	w->req.nr_ringid &= ~NETMAP_RING_MASK;
	w->req.nr_flags = NR_REG_ALL_NIC;
	// w->req.nr_arg1 = 0; // request extra rings
	// w->req.nr_arg2 = 0; // request them in the same region as the others
	// w->req.nr_arg3 = 0; // request extra buffers
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

	// print some info about our rings
	for(uint32_t ri = 0; ri <= w->nif->ni_tx_rings-1; ri++) {
		struct netmap_ring *r = NETMAP_TXRING(w->nif, ri);
		D("tx ring %d: first slot idx %d, last slot idx %d", ri,
		  r->slot[0].buf_idx, r->slot[r->num_slots - 1].buf_idx);
	}
	for(uint32_t ri = 0; ri <= w->nif->ni_rx_rings-1; ri++) {
		struct netmap_ring *r = NETMAP_RXRING(w->nif, ri);
		D("rx ring %d: first slot idx %d, last slot idx %d", ri,
		  r->slot[0].buf_idx, r->slot[r->num_slots - 1].buf_idx);
	}

	// check how many extra buffers we got
	D("allocated %d extra rings", w->req.nr_arg1);
	D("allocated %d extra buffers", w->req.nr_arg3);
	uint32_t idx = w->nif->ni_bufs_head;
	D("ni_bufs_head %d", idx);

	// detach the warpcore event loop thread
	if (pthread_create(&w->thr, NULL, (void *)&w_loop, w) != 0) {
		perror("cannot create warpcore thread");
		abort();
	}

	return w;
}
