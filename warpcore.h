#ifndef _warpcore_h_
#define _warpcore_h_

#define NETMAP_WITH_LIBS
#include <net/netmap_user.h>

#include <stdbool.h>
#include <sys/queue.h>

#include "eth.h"

#define PORT_RANGE_LO	  1
#define PORT_RANGE_HI	256
#define PORT_RANGE_LEN	PORT_RANGE_HI - PORT_RANGE_LO


struct w_iov {
	char *			buf;
	uint_fast32_t		len;
	STAILQ_ENTRY(w_iov) 	vecs;			// tail queue
};


struct w_socket {
	uint_fast32_t		ip;			// dst IP address
	uint_fast16_t		port;			// dst port
	STAILQ_HEAD(ivh, w_iov)	iv;			// where to read into
};


struct warpcore {
	// warpcore information
	uint_fast32_t		ip;			// our IP address
	uint_fast32_t		mask;			// our IP netmask
	uint_fast32_t		bcast;			// our broadcast address
	uint8_t 		mac[ETH_ADDR_LEN];	// our Ethernet address
	pthread_t		thr;			// our main thread
	struct w_socket *	udp[PORT_RANGE_LEN];	// UDP "sockets"
	struct w_socket	*	tcp[PORT_RANGE_LEN];	// TCP "sockets"

	// netmap information
	int			fd;			// netmap descriptor
	void *			mem;			// netmap memory
	struct netmap_if *	nif;			// netmap interface
	uint_fast32_t		num_bufs;		// nr of extra buffers
	uint_fast32_t *		buf;			// indices of extra bufs
	struct nmreq		req;			// netmap request
};


extern struct warpcore * w_init(const char * const ifname);

extern void w_free(struct warpcore *w);

extern void w_close(struct warpcore *w, const uint8_t p, const uint16_t port);

extern bool w_bind(struct warpcore *w, const uint8_t p, const uint16_t port);

extern struct w_iov * w_rx(struct warpcore *w, const uint16_t d);

// internal warpcore use only
// TODO: restrict exporting
extern struct w_socket ** w_find_socket(struct warpcore * w,
                                        const uint8_t p, const uint16_t port);

#endif
