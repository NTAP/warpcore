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
	char *			buf;	// user data
	uint_fast32_t		len;	// length of user data
	uint_fast32_t		idx;	// netmap buffer index containing buf
	STAILQ_ENTRY(w_iov) 	vecs;	// tail queue (next iov)
};


struct w_socket {
	struct warpcore *	w;	// warpcore instance
	uint_fast8_t		p;	// protocol
	uint_fast16_t		sport;	// source port
	// uint_fast32_t	dip;	// destination IP address
	// uint_fast16_t	dport;	// destination port
	STAILQ_HEAD(ivh, w_iov)	iv;	// iov for read data
};


struct w_buf {
	char *			buf;	// the buffer
	uint_fast32_t		idx;	// netmap buffer index of this buffer
	STAILQ_ENTRY(w_buf) 	bufs;	// tail queue (next buf)
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
	STAILQ_HEAD(bh, w_buf)	buf;		// tail queue of extra free bufs
};


extern struct warpcore * w_init(const char * const ifname, const bool detach);
extern void w_cleanup(struct warpcore *w);

extern struct w_socket * w_bind(struct warpcore *w, const uint8_t p,
                                const uint16_t port);

extern struct w_iov * w_rx(struct w_socket *s);
extern void w_rx_done(struct w_socket *s);
extern void w_close(struct w_socket *s);


// internal warpcore use only; TODO: restrict exporting
extern struct w_socket ** w_get_socket(struct warpcore * w,
                                       const uint8_t p, const uint16_t port);

extern void w_poll(struct warpcore *w);
extern void * w_loop(struct warpcore *w);


#endif
