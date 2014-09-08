#ifndef _warpcore_h_
#define _warpcore_h_

#include <stdbool.h>
#include <sys/queue.h>
#include <net/netmap_user.h>

#include "eth.h"
#include "debug.h"

#define PORT_RANGE_LO	  1
#define PORT_RANGE_HI	256
#define PORT_RANGE_LEN	PORT_RANGE_HI - PORT_RANGE_LO


struct w_iov {
	uint_fast32_t		idx;	// index of netmap buffer
	char *			buf;	// start of user data (inside buffer)
	uint_fast32_t		len;	// length of user data (inside buffer)
	SLIST_ENTRY(w_iov) 	vecs;	// tail queue (next iov)
};


struct w_socket {
	struct warpcore *	w;	// warpcore instance
	uint_fast8_t		p;	// protocol
	uint_fast16_t		sport;	// source port
	uint_fast16_t		dport;	// destination port
	uint_fast32_t		dip;	// destination IP address
	SLIST_HEAD(ivh, w_iov)	iv;	// iov for read data
	SLIST_HEAD(ovh, w_iov)	ov;	// iov for data to write
};


struct warpcore {
	// warpcore information
	uint_fast32_t		ip;			// our IP address
	uint_fast32_t		mask;			// our IP netmask
	uint_fast32_t		bcast;			// our broadcast address
	uint_fast16_t		mtu;			// our MTU
	uint8_t 		mac[ETH_ADDR_LEN];	// our Ethernet address
	pthread_t		thr;			// our main thread
	struct w_socket *	udp[PORT_RANGE_LEN];	// UDP "sockets"
	struct w_socket	*	tcp[PORT_RANGE_LEN];	// TCP "sockets"

	// netmap information
	int			fd;		// netmap descriptor
	void *			mem;		// netmap memory
	struct netmap_if *	nif;		// netmap interface
	struct nmreq		req;		// netmap request

	SLIST_HEAD(iovh, w_iov)	iov;		// list of available bufs
};


extern struct warpcore * w_init(const char * const ifname, const bool detach);
extern void w_cleanup(struct warpcore *w);

extern struct w_socket * w_bind(struct warpcore *w, const uint8_t p,
                                const uint16_t port);

extern void w_connect(struct w_socket *s, const uint_fast32_t ip,
                      const uint_fast16_t port);

extern struct w_iov * w_rx(struct w_socket *s);
extern void w_rx_done(struct w_socket *s);
extern void w_close(struct w_socket *s);

extern struct w_iov * w_tx_prep(struct w_socket *s, const uint_fast32_t len);
extern void w_tx(struct w_socket *s, struct w_iov *ov);


// internal warpcore use only; TODO: restrict exporting
extern struct w_socket ** w_get_socket(struct warpcore * w,
                                       const uint8_t p, const uint16_t port);

extern void w_poll(struct warpcore *w);
extern void * w_loop(struct warpcore *w);


#endif
