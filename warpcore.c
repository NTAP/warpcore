#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <net/ethernet.h>
// #include <pthread.h>
#include <poll.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

#ifdef __linux__
#include <linux/if.h>
#include <netpacket/packet.h>
#include <netinet/ether.h>
#define INFTIM	-1
#else
#include <net/if_dl.h>
#endif

#include "warpcore.h"
#include "ip.h"
#include "udp.h"


#define NUM_EXTRA_BUFS	512


struct w_sock ** w_get_sock(struct warpcore * const w, const uint8_t p,
                            const uint16_t port)
{
	// find the respective "socket"
	const uint16_t index = port - PORT_RANGE_LO;
	struct w_sock **s;

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


void w_rx_done(struct w_sock * const s)
{
	struct w_iov *i = SLIST_FIRST(&s->iv);
	while (i) {
		// move i from the socket to the available iov list
		struct w_iov * const n = SLIST_NEXT(i, next);
		SLIST_REMOVE_HEAD(&s->iv, next);
		SLIST_INSERT_HEAD(&s->w->iov, i, next);
		i = n;
	}
	// TODO: should be a no-op; check
	SLIST_INIT(&s->iv);
}


struct w_iov * w_rx(struct w_sock * const s)
{
	if (s)
		return SLIST_FIRST(&s->iv);
	return 0;
}


void w_tx(struct w_sock * const s)
{
	// TODO: handle other protocols

	// packetize bufs and place in tx ring
	bool ok = true;
	uint32_t n = 0, l = 0;
	while (!SLIST_EMPTY(&s->ov)) {
		struct w_iov * const v = SLIST_FIRST(&s->ov);
		ok = udp_tx(s, v);
		if (ok) {
			n++;
			l += v->len;
			SLIST_REMOVE_HEAD(&s->ov, next);
			SLIST_INSERT_HEAD(&s->w->iov, v, next);
		} else {
			// no space in ring
			if (ioctl(s->w->fd, NIOCTXSYNC, 0) == -1)
				die("cannot kick tx ring");
		}
	}
	log("UDP tx iov (len %d in %d bufs) done", l, n);

	// kick tx ring
	if (ioctl(s->w->fd, NIOCTXSYNC, 0) == -1)
		die("cannot kick tx ring");
}


struct w_iov * w_tx_alloc(struct w_sock * const s, const uint32_t len)
{
	if (!SLIST_EMPTY(&s->ov)) {
		log("output iov already allocated");
		return 0;
	}

	// determine space needed for header
	uint16_t hdr_len = sizeof(struct eth_hdr) + sizeof(struct ip_hdr);
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
	SLIST_INIT(&s->ov);
	struct w_iov *ov_tail = 0;
	struct w_iov *v = 0;
	int32_t l = (int32_t)len;
	uint32_t n = 0;
	while (l > 0) {
		// grab a spare buffer
		v = SLIST_FIRST(&s->w->iov);
		if (v == 0)
			die("out of spare bufs after grabbing %d", n);
		SLIST_REMOVE_HEAD(&s->w->iov, next);
		v->buf = IDX2BUF(s->w, v->idx) + hdr_len;
		v->len = s->w->mtu - hdr_len;
		l -= v->len;
		n++;

		// add the iov to the tail of the socket
		// using a STAILQ would be simpler, but slower
		if(SLIST_EMPTY(&s->ov))
			SLIST_INSERT_HEAD(&s->ov, v, next);
		else
			SLIST_INSERT_AFTER(ov_tail, v, next);
		ov_tail = v;
	}
	// adjust length of last iov so chain is the exact length requested
	v->len += l; // l is negative

	log("allocating iovec (len %d in %d bufs) for user tx", len, n);

	return SLIST_FIRST(&s->ov);
}


void w_close(struct w_sock * const s)
{
	struct w_sock **ss = w_get_sock(s->w, s->p, s->sport);

	// make iovs of the socket available again
	while (!SLIST_EMPTY(&s->iv)) {
             struct w_iov * const v = SLIST_FIRST(&s->iv);
             // log("free buf %d", v->idx);
             SLIST_REMOVE_HEAD(&s->iv, next);
             SLIST_INSERT_HEAD(&s->w->iov, v, next);
	}
	while (!SLIST_EMPTY(&s->ov)) {
             struct w_iov * const v = SLIST_FIRST(&s->ov);
             // log("free buf %d", v->idx);
             SLIST_REMOVE_HEAD(&s->ov, next);
             SLIST_INSERT_HEAD(&s->w->iov, v, next);
	}

	// remove the socket from list of sockets
	SLIST_REMOVE(&s->w->sock, s, w_sock, next);

	// free the socket
	free(s->hdr);
	free(*ss);
	*ss = 0;
}


void w_connect(struct w_sock * const s, const uint32_t dip,
               const uint16_t dport)
{
	if (s->dip || s->dport) {
		log("socket already connected");
		return;
	}
#ifndef NDEBUG
	char str[IP_ADDR_STRLEN];
	log("connect IP protocol %d dst %s port %d", s->p,
	    ip_ntoa(dip, str, sizeof str), dport);
#endif
	s->dip = dip;
	s->dport = dport;

	// find the Ethernet address of the destination
	while (IS_ZERO(s->dmac)) {
		arp_who_has(s->w, dip);
		w_poll(s->w);
		if(!IS_ZERO(s->dmac))
			break;
		log("no ARP reply, retrying");
		sleep(1);
	}

	// initialize the non-zero fields of outgoing template header
	struct eth_hdr *eth;
	struct ip_hdr *ip;
	struct udp_hdr *udp;
	switch (s->p) {
	case IP_P_UDP:
		s->hdr_len = sizeof(struct eth_hdr) + sizeof(struct ip_hdr) +
			     sizeof(struct udp_hdr);
		s->hdr = calloc(1, s->hdr_len);
		if (s->hdr == 0)
			die("cannot allocate w_hdr");
		eth = (struct eth_hdr *)s->hdr;
		ip = (struct ip_hdr *)((char *)(eth) + sizeof *eth);
		udp = (struct udp_hdr *)((char *)(ip) + sizeof *ip);

		udp->sport = htons(s->sport);
		udp->dport = htons(s->dport);
		ip->p =	IP_P_UDP;
		break;
	// case IP_P_TCP:
	// 	break;
	default:
		die("unhandled IP protocol %d", s->p);
		break;
	}

	ip->hl = 5;
	ip->v = 4;
	ip->ttl = 4;
	ip->src = s->w->ip;
	ip->dst = dip;

	eth->type = htons(ETH_TYPE_IP);
	memcpy(eth->src, s->w->mac, ETH_ADDR_LEN);
	memcpy(eth->dst, s->dmac, ETH_ADDR_LEN);

	log("IP protocol %d socket connected to %s port %d", s->p,
	    ip_ntoa(dip, str, sizeof str), dport);
}


struct w_sock * w_bind(struct warpcore * const w, const uint8_t p,
                       const uint16_t port)
{
	// check that the port number is valid
	if (port < PORT_RANGE_LO || port > PORT_RANGE_HI)
		die("port %d not in range %d-%d",
		    port, PORT_RANGE_LO, PORT_RANGE_HI);

	struct w_sock **s = w_get_sock(w, p, port);
	if (*s) {
		log("IP protocol %d source port %d already in use", p, port);
		return 0;
	}

	if ((*s = calloc(1, sizeof **s)) == 0)
		die("cannot allocate struct w_sock");
	log("bind IP protocol %d port %d", p, port);
	(*s)->p = p;
	(*s)->sport = port;
	(*s)->w = w;
	SLIST_INIT(&(*s)->iv);
	SLIST_INSERT_HEAD(&w->sock, *s, next);
	return *s;
}


bool w_poll(struct warpcore * const w)
{
	struct pollfd fds = { .fd = w->fd, .events = POLLIN };
	const int n = poll(&fds, 1, INFTIM);
	switch (n) {
	case -1:
		if (errno == EINTR) {
			log("poll: interrupt");
			return false;
		} else
			die("poll");
		break;
	case 0:
		log("poll: timeout expired");
		break;
	default:
		// log("poll: %d descriptors ready", n);
		break;
	}

	struct netmap_ring * const ring = NETMAP_RXRING(w->nif, 0);
	while (!nm_ring_empty(ring)) {
		char * const buf =
			NETMAP_BUF(ring, ring->slot[ring->cur].buf_idx);
		eth_rx(w, buf);
		ring->head = ring->cur = nm_ring_next(ring, ring->cur);
	}
	return true;
}


static struct w_iov * w_chain_extra_bufs(struct warpcore * const w, struct w_iov *v)
{
	struct w_iov *n;
	do {
		n = SLIST_NEXT(v, next);
		uint32_t * const buf = (uint32_t *)IDX2BUF(w, v->idx);
		if (n) {
			*buf = n->idx;
			v = n;
		} else
			*buf = 0;
	} while (n);

	// return the last list element
	return v;
}


void w_cleanup(struct warpcore * const w)
{
	log("warpcore shutting down");

	// if (w->thr) {
	// 	if (pthread_cancel(w->thr))
	// 		die("cannot cancel warpcore thread");

	// 	if (pthread_join(w->thr, 0))
	// 		die("cannot wait for exiting warpcore thread");
	// }

	// re-construct the extra bufs list, so netmap can free the memory
	struct w_iov *last = w_chain_extra_bufs(w, SLIST_FIRST(&w->iov));
	struct w_sock *s;
	SLIST_FOREACH(s, &w->sock, next) {
		if (!SLIST_EMPTY(&s->iv)) {
			struct w_iov * const l =
				w_chain_extra_bufs(w, SLIST_FIRST(&s->iv));
			*(uint32_t *)(last->buf) = SLIST_FIRST(&s->iv)->idx;
			last = l;

		}
		if (!SLIST_EMPTY(&s->ov)) {
			struct w_iov * const lov =
				w_chain_extra_bufs(w, SLIST_FIRST(&s->ov));
			*(uint32_t *)(last->buf) = SLIST_FIRST(&s->ov)->idx;
			last = lov;
		}
		*(uint32_t *)(last->buf) = 0;
	}
	w->nif->ni_bufs_head = SLIST_FIRST(&w->iov)->idx;

	// int n = w->nif->ni_bufs_head;
	// while (n) {
	// 	char *b = IDX2BUF(w, n);
	// 	log("buf in extra chain idx %d", n);
	// 	n = *(uint32_t *)b;
	// }

	if (munmap(w->mem, w->req.nr_memsize) == -1)
		die("cannot munmap netmap memory");

	if (close(w->fd) == -1)
		die("cannot close /dev/netmap");

	free(w);
}


static void w_sigint(int sig __attribute__((__unused__))) {}


struct warpcore * w_init(const char * const ifname)
{
	struct warpcore *w;

	// allocate struct
	if ((w = calloc(1, sizeof *w)) == 0)
		die("cannot allocate struct warpcore");

	// open /dev/netmap
	if ((w->fd = open("/dev/netmap", O_RDWR)) == -1)
		die("cannot open /dev/netmap");

	// get interface information
	struct ifaddrs *ifap;
	if (getifaddrs(&ifap) == -1)
		die("cannot get interface information");
	for (const struct ifaddrs *i = ifap; i->ifa_next; i = i->ifa_next) {
		if (strcmp(i->ifa_name, ifname) == 0) {
#ifndef NDEBUG
			char mac[ETH_ADDR_STRLEN];
			char ip[IP_ADDR_STRLEN];
			char mask[IP_ADDR_STRLEN];
#endif
			switch (i->ifa_addr->sa_family) {
#ifdef __linux__
			case AF_PACKET:
				// get MAC address
				memcpy(&w->mac,
				       ((struct sockaddr_ll *)i->ifa_addr)->sll_addr,
				       sizeof w->mac);
				// get MTU
				int s;
				struct ifreq ifr;
				if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
					die("socket");
				strcpy(ifr.ifr_name, i->ifa_name);
				if (ioctl(s, SIOCGIFMTU, &ifr) < 0)
					die("ioctl (get mtu)");
				w->mtu = ifr.ifr_ifru.ifru_mtu;
				close(s);

#else
			case AF_LINK:
				// get MAC address
				memcpy(&w->mac,
				       LLADDR((struct sockaddr_dl *)
				              i->ifa_addr),
				       sizeof w->mac);
				// get MTU
				w->mtu = (uint16_t)((struct if_data *)
				          (i->ifa_data))->ifi_mtu;
#endif
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
	if (w->ip == 0 || w->mask == 0 || w->mtu == 0 || IS_ZERO(w->mac))
		die("cannot obtain needed interface information");

	w->bcast = w->ip | (~w->mask);
#ifndef NDEBUG
	char bcast[IP_ADDR_STRLEN];
	log("%s has IP broadcast address %s", ifname,
	    ip_ntoa(w->bcast, bcast, sizeof bcast));
#endif

	// switch interface to netmap mode
	strncpy(w->req.nr_name, ifname, sizeof w->req.nr_name);
	w->req.nr_name[sizeof w->req.nr_name - 1] = '\0';
	w->req.nr_version = NETMAP_API;
	w->req.nr_ringid &= ~NETMAP_RING_MASK;
	// for now, we do want poll to also kick the tx ring
	// w->req.nr_ringid |= (NETMAP_NO_TX_POLL | NETMAP_DO_RX_POLL);
	w->req.nr_flags = NR_REG_ALL_NIC;
	w->req.nr_arg3 = NUM_EXTRA_BUFS; // request extra buffers
	if (ioctl(w->fd, NIOCREGIF, &w->req) == -1)
		die("cannot put interface into netmap mode");

	// mmap the buffer region
	// TODO: see TODO in nm_open() in netmap_user.h
	if ((w->mem = mmap(0, w->req.nr_memsize, PROT_WRITE|PROT_READ,
	    MAP_SHARED, w->fd, 0)) == MAP_FAILED)
		die("cannot mmap netmap memory");

	// direct pointer to the netmap interface struct for convenience
	w->nif = NETMAP_IF(w->mem, w->req.nr_offset);

#ifndef NDEBUG
	// print some info about our rings
	for(uint32_t ri = 0; ri <= w->nif->ni_tx_rings-1; ri++) {
		struct netmap_ring *r = NETMAP_TXRING(w->nif, ri);
		log("tx ring %d: first slot %d, last slot %d", ri,
		    r->slot[0].buf_idx, r->slot[r->num_slots - 1].buf_idx);
	}
	for(uint32_t ri = 0; ri <= w->nif->ni_rx_rings-1; ri++) {
		struct netmap_ring *r = NETMAP_RXRING(w->nif, ri);
		log("rx ring %d: first slot %d, last slot %d", ri,
		    r->slot[0].buf_idx, r->slot[r->num_slots - 1].buf_idx);
	}
#endif

	// save the indices of the extra buffers in the warpcore structure
	SLIST_INIT(&w->iov);
	for (uint32_t n = 0, i = w->nif->ni_bufs_head;
	     n < w->req.nr_arg3; n++) {
		struct w_iov * const v = malloc(sizeof *v);
		if (v == 0)
			die("cannot allocate w_iov");
		v->buf = IDX2BUF(w, i);
		v->idx = i;
		// log("available extra buf %d", i);
		SLIST_INSERT_HEAD(&w->iov, v, next);
		char * const b = IDX2BUF(w, i);
		i = *(uint32_t *)b;
	}

	if (w->req.nr_arg3 != NUM_EXTRA_BUFS)
		die("can only allocate %d/%d extra buffers",
		    w->req.nr_arg3, NUM_EXTRA_BUFS);
	else
		log("allocated %d extra buffers", w->req.nr_arg3);


	// initialize list of sockets
	SLIST_INIT(&w->sock);

	// block SIGINT
        if (signal(SIGINT, w_sigint) == SIG_ERR)
        	die("cannot register signal handler");

	return w;
}
