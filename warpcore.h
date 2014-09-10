#ifndef _warpcore_h_
#define _warpcore_h_

#include <stdbool.h>
#include <sys/queue.h>

#ifdef __linux__
#define IFNAMSIZ	IF_NAMESIZE
#include <sys/time.h>
// #include <pthread.h>
#endif

#include <net/netmap_user.h>

#include "debug.h"
#include "eth.h"
#include "arp.h"
#include "ip.h"
#include "udp.h"

#define PORT_RANGE_LO	  1
#define PORT_RANGE_HI	256
#define PORT_RANGE_LEN	PORT_RANGE_HI - PORT_RANGE_LO

// according to Luigi, any ring can be passed to NETMAP_BUF
#define IDX2BUF(w, i)	NETMAP_BUF(NETMAP_TXRING(w->nif, 0), i)


struct w_iov {
	uint_fast32_t		idx;	// index of netmap buffer
	char *			buf;	// start of user data (inside buffer)
	uint_fast32_t		len;	// length of user data (inside buffer)
	SLIST_ENTRY(w_iov) 	next;	// next iov
};


struct w_sock {
	struct warpcore *	w;			// warpcore instance
	uint_fast8_t		p;			// protocol
	uint_fast16_t		sport;			// src port
	uint_fast16_t		dport;			// dst port
	uint_fast32_t		dip;			// dst IP address
	uint8_t 		dmac[ETH_ADDR_LEN];	// dst Eth address
	char *			hdr;			// header template
	uint_fast16_t		hdr_len;		// length of template
	SLIST_HEAD(ivh, w_iov)	iv;			// iov for read data
	SLIST_HEAD(ovh, w_iov)	ov;			// iov for data to write
	SLIST_ENTRY(w_sock) 	next;			// next socket
};


struct warpcore {
	// warpcore information
	uint_fast32_t		ip;			// our IP address
	uint_fast32_t		mask;			// our IP netmask
	uint_fast32_t		bcast;			// our broadcast address
	uint_fast16_t		mtu;			// our MTU
	uint8_t 		mac[ETH_ADDR_LEN];	// our Ethernet address
	// pthread_t		thr;			// our main thread
	struct w_sock *		udp[PORT_RANGE_LEN];	// UDP "sockets"
	struct w_sock *		tcp[PORT_RANGE_LEN];	// TCP "sockets"

	// netmap information
	int			fd;		// netmap descriptor
	void *			mem;		// netmap memory
	struct netmap_if *	nif;		// netmap interface
	struct nmreq		req;		// netmap request

	SLIST_HEAD(iovh, w_iov)	iov;		// list of available bufs
	SLIST_HEAD(sh, w_sock)	sock;		// list of open sockets
};


// see udp.c for documentation of functions
extern struct warpcore * w_init(const char * const ifname);

extern void w_cleanup(struct warpcore * const w);

extern struct w_sock * w_bind(struct warpcore * const w, const uint8_t p,
                              const uint16_t port);

extern void w_connect(struct w_sock * const s, const uint_fast32_t ip,
                      const uint_fast16_t port);

extern struct w_iov * w_rx(struct w_sock * const s);

extern void w_rx_done(struct w_sock * const s);

extern void w_close(struct w_sock * const s);

extern struct w_iov * w_tx_alloc(struct w_sock * const s,
                                 const uint_fast32_t len);

extern void w_tx(struct w_sock * const s);

extern bool w_poll(struct warpcore * const w);

// internal warpcore use only; TODO: restrict exporting
extern struct w_sock ** w_get_sock(struct warpcore * const w,
                                   const uint_fast8_t p,
                                   const uint_fast16_t port);

#endif
