#include <fcntl.h>
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
#include <errno.h>
#include <unistd.h>
#include <stdio.h>

#include "warpcore.h"
#include "ip.h"
#include "udp.h"


#define NUM_EXTRA_BUFS	16


struct w_socket ** w_get_socket(struct warpcore * w,
                                 const uint8_t p, const uint16_t port)
{
	// find the respective "socket"
	const uint16_t index = port - PORT_RANGE_LO;
	if (index >= PORT_RANGE_HI) {
		log("port %d not in range %d-%d", port,
		    PORT_RANGE_LO, PORT_RANGE_HI);
		return 0;
	}

	struct w_socket **s;
	switch (p){
	case IP_P_UDP:
		s = &w->udp[index];
		break;
	case IP_P_TCP:
		s = &w->tcp[index];
		break;
	default:
		log("cannot find socket for IP protocol %d", p);
		return 0;
	}
	return s;

}


static void w_make_idx_avail(struct warpcore * w, const uint32_t idx)
{
	struct w_buf *b = malloc(sizeof *b);
	if (b == 0) {
		perror("cannot allocate w_buf");
		abort();
	}
	// according to Luigi, any ring can be passed to NETMAP_BUF
	b->buf = NETMAP_BUF(NETMAP_TXRING(w->nif, 0), idx);
	b->idx = idx;
	log("available extra bufidx %d is %p", idx, b->buf);
	STAILQ_INSERT_TAIL(&w->buf, b, bufs);
}


void w_rx_done(struct w_socket *s)
{
	struct w_iov *i = STAILQ_FIRST(&s->iv);
	while (i) {
		struct w_iov *n = STAILQ_NEXT(i, vecs);
		// make the buffer available again
		w_make_idx_avail(s->w, i->idx);
		free(i);
		i = n;
	}
	STAILQ_INIT(&s->iv);

}


struct w_iov * w_rx(struct w_socket *s)
{
	if (s)
		return STAILQ_FIRST(&s->iv);
	return 0;
}


void w_tx(struct w_socket *s, struct w_iov *ov)
{
	while (ov) {
		log("%d bytes in buf %p", ov->len, ov->buf);
		udp_tx(s, ov->buf, ov->len);
		ov = STAILQ_NEXT(ov, vecs);
	}

}


struct w_iov * w_tx_prep(struct w_socket *s, const uint_fast32_t len)
{
	if (!STAILQ_EMPTY(&s->ov)) {
		log("output iov already allocated");
		return 0;
	}

	// determine space needed for header
	uint_fast16_t hdr_len =	sizeof(struct eth_hdr) + sizeof(struct ip_hdr);
	switch (s->p) {
	case IP_P_UDP:
		hdr_len += sizeof(struct udp_hdr);
		break;
	// case IP_P_TCP:
	// 	hdr_len += sizeof(struct tcp_hdr);
	// 	// TODO: handle TCP options
	// 	break;
	default:
		die("unhandled IP protocol %d", s->p);
		return 0;
	}

	// add enough buffers to the iov so it is > len
	STAILQ_INIT(&s->ov);
	int_fast32_t l = len;
	struct w_iov *i;
	while (l > 0) {
		// log("%d still to allocate", l);
		// allocate a new iov
		if ((i = malloc(sizeof *i)) == 0)
			die("cannot allocate w_iov");

		// grab a spare buffer
		struct w_buf *b = STAILQ_FIRST(&s->w->buf);
		if (b == 0)
			die("out of spare bufs");
		STAILQ_REMOVE_HEAD(&s->w->buf, bufs);
		i->buf = b->buf + hdr_len;
		i->idx = b->idx;
		i->len = s->w->mtu - hdr_len;
		l -= i->len;
		free(b);

		// add the iov to the socket
		STAILQ_INSERT_TAIL(&s->ov, i, vecs);
	}
	// adjust length of last iov so chain is the exact length requested
	i->len += l; // l is negative

	// log("l %d ilen %d", l, i->len);
	return STAILQ_FIRST(&s->ov);
}


void w_close(struct w_socket *s)
{
	struct w_socket **ss = w_get_socket(s->w, s->p, s->sport);
	free(*ss);
	*ss = 0;
}


void w_connect(struct w_socket *s, const uint_fast32_t ip,
               const uint_fast16_t port)
{
	if (s->dip || s->dport) {
		log("socket already connected");
		return;
	}
#ifndef NDEBUG
	char str[IP_ADDR_STRLEN];
	log("connect IP protocol %d dst %s port %d", s->p,
	    ip_ntoa(ip, str, sizeof str), port);
#endif
	s->dip = ip;
	s->dport = port;
}


struct w_socket * w_bind(struct warpcore *w, const uint8_t p,
                         const uint16_t port)
{
	struct w_socket **s = w_get_socket(w, p, port);
	if (*s == 0) {
		if ((*s = calloc(1, sizeof **s)) == 0) {
			perror("cannot allocate struct w_socket");
			abort();
		}
		log("bind IP protocol %d port %d", p, port);
		(*s)->p = p;
		(*s)->sport = port;
		(*s)->w = w;
	} else {
		log("IP protocol %d source port %d already in use", p, port);
		return 0;
	}
	STAILQ_INIT(&(*s)->iv);

	return *s;
}


void w_poll(struct warpcore *w)
{
	struct pollfd fds = { .fd = w->fd, .events = POLLIN };
	const int n = poll(&fds, 1, INFTIM);
	switch (n) {
	case -1:
		die("poll: %s", strerror(errno));
		break;
	case 0:
		log("poll: timeout expired");
		break;
	default:
		// log("poll: %d descriptors ready", n);
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


void * w_loop(struct warpcore *w)
{
	while (1)
		w_poll(w);
}


void w_cleanup(struct warpcore *w)
{
	log("warpcore shutting down");

	if (w->thr) {
		if (pthread_cancel(w->thr)) {
			perror("cannot cancel warpcore thread");
			abort();
		}

		if (pthread_join(w->thr, 0)) {
			perror("cannot wait for exiting warpcore thread");
			abort();
		}
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


struct warpcore * w_init(const char * const ifname, const bool detach)
{
	struct warpcore *w;

	// allocate struct
	if ((w = calloc(1, sizeof *w)) == 0) {
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
	for (struct ifaddrs *i = ifap; i->ifa_next; i = i->ifa_next) {
		if (strcmp(i->ifa_name, ifname) == 0) {
#ifndef NDEBUG
			char mac[ETH_ADDR_LEN*3];
			char ip[IP_ADDR_STRLEN];
			char mask[IP_ADDR_STRLEN];
#endif
			switch (i->ifa_addr->sa_family) {
			case AF_LINK:
				// get MAC address
				memcpy(&w->mac,
				       LLADDR((struct sockaddr_dl *)
				              i->ifa_addr),
				       sizeof w->mac);
				// get MTU
				w->mtu = ((struct if_data *)
				          (i->ifa_data))->ifi_mtu;
				log("%s has Ethernet address %s with MTU %d",
				    i->ifa_name,
				    ether_ntoa_r((struct ether_addr *)w->mac,
				                 mac), w->mtu);
				break;
			case AF_INET:
				// get IP address and netmask
				w->ip = ((struct sockaddr_in *)
				         i->ifa_addr)->sin_addr.s_addr;
				w->mask = ((struct sockaddr_in *)
				           i->ifa_netmask)->sin_addr.s_addr;
				log("%s has IP address %s/%s", i->ifa_name,
				    ip_ntoa(w->ip, ip, sizeof ip),
				    ip_ntoa(w->mask, mask, sizeof mask));
				break;
			default:
				log("ignoring unknown address family %d on %s",
				    i->ifa_addr->sa_family, i->ifa_name);
				break;
			}
		}
	}
	freeifaddrs(ifap);
	if (w->ip == 0 || w->mask == 0 || w->mtu == 0 ||
	    ((w->mac[0] | w->mac[1] | w->mac[2] |
	      w->mac[3] | w->mac[4] | w->mac[5]) == 0))
		die("cannot obtain needed interface information");

	w->bcast = w->ip | (~w->mask);
#ifndef NDEBUG
	char bcast[IP_ADDR_STRLEN];
	log("%s has IP broadcast address %s", ifname,
	    ip_ntoa(w->bcast, bcast, sizeof bcast));
#endif

	// switch interface to netmap mode
	// TODO: figure out NETMAP_NO_TX_POLL/NETMAP_DO_RX_POLL
	strncpy(w->req.nr_name, ifname, sizeof w->req.nr_name);
	w->req.nr_name[sizeof w->req.nr_name - 1] = '\0';
	w->req.nr_version = NETMAP_API;
	w->req.nr_ringid &= ~NETMAP_RING_MASK;
	w->req.nr_flags = NR_REG_ALL_NIC;
	w->req.nr_arg3 = NUM_EXTRA_BUFS; // request extra buffers
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

#ifndef NDEBUG
	// print some info about our rings
	for(uint32_t ri = 0; ri <= w->nif->ni_tx_rings-1; ri++) {
		struct netmap_ring *r = NETMAP_TXRING(w->nif, ri);
		log("tx ring %d: first slot idx %d, last slot idx %d", ri,
		    r->slot[0].buf_idx, r->slot[r->num_slots - 1].buf_idx);
	}
	for(uint32_t ri = 0; ri <= w->nif->ni_rx_rings-1; ri++) {
		struct netmap_ring *r = NETMAP_RXRING(w->nif, ri);
		log("rx ring %d: first slot idx %d, last slot idx %d", ri,
		    r->slot[0].buf_idx, r->slot[r->num_slots - 1].buf_idx);
	}
#endif

	// save the indices of the extra buffers in the warpcore structure
	STAILQ_INIT(&w->buf);
	for (uint32_t n = 0, i = w->nif->ni_bufs_head;
	     n < w->req.nr_arg3; n++) {
	     	w_make_idx_avail(w, i);
		char * b = NETMAP_BUF(NETMAP_TXRING(w->nif, 0), i);
		i = *(uint32_t *)b;
	}
	log("allocated %d extra buffers", w->req.nr_arg3);

	if (detach) {
		// detach the warpcore event loop thread
		if (pthread_create(&w->thr, 0, (void *)&w_loop, w)) {
			perror("cannot create warpcore thread");
			abort();
		}
	}

	return w;
}
