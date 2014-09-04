#ifndef _warpcore_h_
#define _warpcore_h_

#define NETMAP_WITH_LIBS
#include <net/netmap_user.h>

#include "eth.h"

struct warpcore {
	uint_fast32_t		ip;			// our IP address
	uint_fast32_t		mask;			// our IP netmask
	uint_fast32_t		bcast;			// our broadcast address
	uint8_t 		mac[ETH_ADDR_LEN];	// our Ethernet address

	int			fd;			// netmap descriptor
	void *			mem;			// netmap memory
	struct netmap_if *	nif;			// netmap interface
	struct nmreq		req;			// netmap request
};

extern struct warpcore * w_init(const char * const ifname);

extern void w_free(struct warpcore *w);

#endif
