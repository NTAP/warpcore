#ifndef _warpcore_h_
#define _warpcore_h_

#define NETMAP_WITH_LIBS
#include <net/netmap_user.h>

#include "eth.h"

struct warpcore {
	uint32_t			ip;					// our IP address
	uint32_t			mask;				// our IP netmask
	uint8_t 			mac[ETH_ADDR_LEN];	// our Ethernet address

	int					fd;					// netmap file descriptor
	void *				mem;				// netmap memory
	struct netmap_if *	nif;				// netmap interface
	struct nmreq		req;				// netmap request structure

};

extern struct warpcore * w_open(const char * const ifname);

#endif
