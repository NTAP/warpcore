#ifndef _warpcore_h_
#define _warpcore_h_

#include <stdbool.h>
#include <sys/queue.h>
#include <poll.h>

#ifdef __linux__
#include <ifaddrs.h>
#include <linux/if.h>
#include <sys/time.h>
// #include <pthread.h>
#endif

#include <net/netmap_user.h>

#include "debug.h"
#include "eth.h"
#include "arp.h"
#include "ip.h"
#include "udp.h"

#define PORT_RANGE_LO	49152
#define PORT_RANGE_HI	49152 + 32
#define PORT_RANGE_LEN	PORT_RANGE_HI - PORT_RANGE_LO

// according to Luigi, any ring can be passed to NETMAP_BUF
#define IDX2BUF(w, i)	NETMAP_BUF(NETMAP_TXRING(w->nif, 0), i)


struct w_iov {
	SLIST_ENTRY(w_iov) 	next;	// next iov
	char *			buf;	// start of user data (inside buffer)
	uint32_t		idx;	// index of netmap buffer
	uint16_t		len;	// length of user data (inside buffer)
} __attribute__((__aligned__(4)));


struct w_sock {
	struct warpcore *	w;			// warpcore instance
	SLIST_HEAD(ivh, w_iov)	iv;			// iov for read data
	SLIST_HEAD(ovh, w_iov)	ov;			// iov for data to write
	char *			hdr;			// header template
	uint16_t		hdr_len;		// length of template
	uint8_t 		dmac[ETH_ADDR_LEN];	// dst Eth address
	uint32_t		dip;			// dst IP address
	uint16_t		sport;			// src port
	uint16_t		dport;			// dst port
	SLIST_ENTRY(w_sock) 	next;			// next socket
	uint8_t			p;			// protocol
} __attribute__((__aligned__(4)));


struct warpcore {
	// netmap information
	void *			mem;			// netmap memory
	struct netmap_if *	nif;			// netmap interface
	int			fd;			// netmap descriptor

	// warpcore information
	uint32_t		ip;			// our IP address
	uint16_t		mtu;			// our MTU
	uint8_t 		mac[ETH_ADDR_LEN];	// our Ethernet address
	SLIST_HEAD(iovh, w_iov)	iov;			// our available bufs
	uint32_t		mask;			// our IP netmask
	uint32_t		bcast;			// our broadcast address
	uint32_t		mbps;			// our link speed
	uint16_t		cur_txr;		// our current tx ring
	uint16_t		cur_rxr;		// our current rx ring
	SLIST_HEAD(sh, w_sock)	sock;			// our open sockets

        // --- cacheline 1 boundary (64 bytes) ---

	struct w_sock *		udp[PORT_RANGE_LEN];	// UDP "sockets"
	struct w_sock *		tcp[PORT_RANGE_LEN];	// TCP "sockets"

	struct nmreq		req;			// netmap request

	// pthread_t		thr;			// our main thread
} __attribute__((__aligned__(4)));


// see udp.c for documentation of functions
extern struct warpcore * w_init(const char * const ifname);

extern void w_cleanup(struct warpcore * const w);

extern struct w_sock * w_bind(struct warpcore * const w, const uint8_t p,
                              const uint16_t port);

extern void w_connect(struct w_sock * const s, const uint32_t ip,
                      const uint16_t port);

extern struct w_iov * w_rx(struct w_sock * const s);

extern void w_rx_done(struct w_sock * const s);

extern void w_close(struct w_sock * const s);

extern struct w_iov * w_tx_alloc(struct w_sock * const s,
                                 const uint32_t len);

extern void w_tx(struct w_sock * const s);

extern bool w_poll(struct w_sock * const s, const short ev, const int to);

// internal warpcore use only; TODO: restrict exporting
extern struct w_sock ** w_get_sock(struct warpcore * const w,
                                   const uint8_t p,
                                   const uint16_t port);

#endif
