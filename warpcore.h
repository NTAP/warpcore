#ifndef _warpcore_h_
#define _warpcore_h_

#define NETMAP_WITH_LIBS
#include <net/netmap_user.h>

#include <stdbool.h>

#include "eth.h"

static const uint_fast16_t W_PORT_RANGE_LO = 1;
static const uint_fast16_t W_PORT_RANGE_HI = 256;
static const uint_fast16_t W_PORT_RANGE_LEN = W_PORT_RANGE_HI - W_PORT_RANGE_LO;

struct w_iovec {
	void *			buf;
	uint_fast32_t		len;
};

struct w_socket {
	uint_fast32_t		ip;			// dst IP address
	uint_fast16_t		port;			// dst port
	struct w_iovec *	iv;
};

struct warpcore {
	uint_fast32_t		ip;			// our IP address
	uint_fast32_t		mask;			// our IP netmask
	uint_fast32_t		bcast;			// our broadcast address
	uint8_t 		mac[ETH_ADDR_LEN];	// our Ethernet address
	pthread_t		thr;			// our main thread

	struct w_socket *	udp[W_PORT_RANGE_LEN];
	struct w_socket	*	tcp[W_PORT_RANGE_LEN];

	int			fd;			// netmap descriptor
	void *			mem;			// netmap memory
	struct netmap_if *	nif;			// netmap interface
	struct nmreq		req;			// netmap request
};

extern struct warpcore * w_init(const char * const ifname);

extern void w_free(struct warpcore *w);

// extern int16_t w_socket(struct warpcore *w, const uint8_t proto);

extern void w_close(struct warpcore *w, const uint8_t p, const uint16_t port);

extern bool w_bind(struct warpcore *w, const uint8_t p, const uint16_t port);

extern struct w_iovec * w_rx(struct warpcore *w, const uint16_t d);

#endif
