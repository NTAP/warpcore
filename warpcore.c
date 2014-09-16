#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>

#ifdef __linux__
#include <netinet/ether.h>
#include <netpacket/packet.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#else
#include <sys/types.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#endif

#include "warpcore.h"
#include "ip.h"
#include "udp.h"


#define NUM_EXTRA_BUFS	65536

// Internal warpcore function. Kick the tx ring.
static void w_kick_tx(struct warpcore * const w)
{
	if (ioctl(w->fd, NIOCTXSYNC, 0) == -1)
		die("cannot kick tx ring");
}


// Internal warpcore function. Given an IP protocol number and a local port
// number, returns a pointer to the w_sock pointer.
struct w_sock ** w_get_sock(struct warpcore * const w, const uint8_t p,
                            const uint16_t port)
{
	// find the respective "socket"
	struct w_sock **s;
	switch (p) {
	case IP_P_UDP:
		s = &w->udp[port];
		break;
	case IP_P_TCP:
		s = &w->tcp[port];
		break;
	default:
		die("cannot find socket for IP proto %d", p);
		return 0;
	}
	return s;

}


// User needs to call this once they are done with touching any received data.
// This makes the iov that holds the received data available to warpcore again.
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


// Pulls new received data out of the rx ring and places it into socket iovs.
// Returns an iov of any data received.
struct w_iov * w_rx(struct w_sock * const s)
{
	// loop over all rx rings starting with cur_rxr and wrapping around
	for (uint16_t i = 0; i < s->w->nif->ni_rx_rings; i++) {
		struct netmap_ring * const r =
			NETMAP_RXRING(s->w->nif, s->w->cur_rxr);
		while (!nm_ring_empty(r)) {
			eth_rx(s->w, NETMAP_BUF(r, r->slot[r->cur].buf_idx));
			r->head = r->cur = nm_ring_next(r, r->cur);
		}
		s->w->cur_rxr = (s->w->cur_rxr + 1) % s->w->nif->ni_rx_rings;
	}

	if (s)
		return SLIST_FIRST(&s->iv);
	return 0;
}


// Prepends all network headers and places s->ov in the tx ring.
void w_tx(struct w_sock * const s)
{
	// TODO: handle other protocols

	// packetize bufs and place in tx ring
	uint32_t n = 0, l = 0;
	while (!SLIST_EMPTY(&s->ov)) {
		struct w_iov * const v = SLIST_FIRST(&s->ov);
		if (udp_tx(s, v)) {
			n++;
			l += v->len;
			SLIST_REMOVE_HEAD(&s->ov, next);
			SLIST_INSERT_HEAD(&s->w->iov, v, next);
		} else {
			// no space in ring
			w_kick_tx(s->w);
			log(5, "polling for send space");
			if (w_poll(s->w, POLLOUT, -1) == false)
				// interrupt received during poll
				return;
		}
	}
	log(3, "UDP tx iov (len %d in %d bufs) done", l, n);

	// kick tx ring
	w_kick_tx(s->w);
}


// Allocates an iov of a given size for tx preparation.
struct w_iov * w_tx_alloc(struct w_sock * const s, const uint32_t len)
{
	if (!SLIST_EMPTY(&s->ov)) {
		log(1, "output iov already allocated");
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
		die("unhandled IP proto %d", s->p);
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

	log(3, "allocating iovec (len %d in %d bufs) for user tx", len, n);

	return SLIST_FIRST(&s->ov);
}


// Close a warpcore socket, making all its iovs available again.
void w_close(struct w_sock * const s)
{
	struct w_sock **ss = w_get_sock(s->w, s->p, s->sport);

	// make iovs of the socket available again
	while (!SLIST_EMPTY(&s->iv)) {
             struct w_iov * const v = SLIST_FIRST(&s->iv);
             // log(5, "free buf %d", v->idx);
             SLIST_REMOVE_HEAD(&s->iv, next);
             SLIST_INSERT_HEAD(&s->w->iov, v, next);
	}
	while (!SLIST_EMPTY(&s->ov)) {
             struct w_iov * const v = SLIST_FIRST(&s->ov);
             // log(5, "free buf %d", v->idx);
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


// Connect a bound socket to a remote IP address and port.
void w_connect(struct w_sock * const s, const uint32_t dip,
               const uint16_t dport)
{
#ifndef NDEBUG
	char str[IP_ADDR_STRLEN];
	log(3, "connect IP proto %d dst %s port %d", s->p,
	    ip_ntoa(dip, str, sizeof str), ntohs(dport));
#endif
	s->dip = dip;
	s->dport = dport;

	// find the Ethernet addr of the destination
	while (IS_ZERO(s->dmac)) {
		arp_who_has(s->w, dip);
		if(w_poll(s->w, POLLIN, 1000) == false)
			// interrupt received during poll
			return;
		w_rx(s);
		if(!IS_ZERO(s->dmac))
			break;
		log(1, "no ARP reply, retrying");
	}

	// initialize the remaining fields of outgoing template header
	struct eth_hdr * const eth = (struct eth_hdr *)s->hdr;
	struct ip_hdr * const ip = (struct ip_hdr *)((char *)(eth) + sizeof *eth);
	struct udp_hdr * const udp = (struct udp_hdr *)((char *)(ip) + sizeof *ip);
	switch (s->p) {
	case IP_P_UDP:
		udp->dport = dport;
		break;
	// case IP_P_TCP:
	// 	break;
	default:
		die("unhandled IP proto %d", s->p);
		break;
	}

	ip->dst = dip;
	memcpy(eth->dst, s->dmac, ETH_ADDR_LEN);

	log(3, "IP proto %d socket connected to %s port %d", s->p,
	    ip_ntoa(dip, str, sizeof str), ntohs(dport));
}


// Bind a socket for the given IP protocol and local port number.
struct w_sock * w_bind(struct warpcore * const w, const uint8_t p,
                       const uint16_t port)
{
	struct w_sock **s = w_get_sock(w, p, port);
	if (*s) {
		log(1, "IP proto %d source port %d already in use",
		    p, ntohs(port));
		return 0;
	}

	if ((*s = calloc(1, sizeof **s)) == 0)
		die("cannot allocate struct w_sock");

	(*s)->p = p;
	(*s)->sport = port;
	(*s)->w = w;
	SLIST_INIT(&(*s)->iv);
	SLIST_INSERT_HEAD(&w->sock, *s, next);

	// initialize the non-zero fields of outgoing template header
	struct eth_hdr *eth;
	struct ip_hdr *ip;
	struct udp_hdr *udp;
	switch ((*s)->p) {
	case IP_P_UDP:
		(*s)->hdr_len = sizeof(struct eth_hdr) + sizeof(struct ip_hdr) +
			     sizeof(struct udp_hdr);
		(*s)->hdr = calloc(1, (*s)->hdr_len);
		if ((*s)->hdr == 0)
			die("cannot allocate w_hdr");
		eth = (struct eth_hdr *)(*s)->hdr;
		ip = (struct ip_hdr *)((char *)(eth) + sizeof *eth);
		udp = (struct udp_hdr *)((char *)(ip) + sizeof *ip);

		udp->sport = (*s)->sport;
		// udp->dport is set on w_connect()
		ip->p =	IP_P_UDP;
		break;
	// case IP_P_TCP:
	// 	break;
	default:
		die("unhandled IP proto %d", (*s)->p);
		break;
	}

	ip->hl = 5;
	ip->v = 4;
	ip->ttl = 4;
	ip->src = (*s)->w->ip;
	// ip->dst  is set on w_connect()

	eth->type = htons(ETH_TYPE_IP);
	memcpy(eth->src, (*s)->w->mac, ETH_ADDR_LEN);
	// eth->dst is set on w_connect()

	log(1, "IP proto %d socket bound to port %d", (*s)->p, ntohs(port));

	return *s;
}


// Wait until netmap is ready to send or receive more data. Parameters
// "event" and "timeout" identical to poll system call.
// Returns false if an interrupt occurs during the poll, which usually means
// someone hit Ctrl-C.
// (TODO: This interrupt handling needs some rethinking.)
bool w_poll(struct warpcore * const w, const short ev, const int to)
{
	struct pollfd fds = { .fd = w->fd, .events = ev };
	const int n = poll(&fds, 1, to);
	switch (n) {
	case -1:
		if (errno == EINTR) {
			log(3, "poll: interrupt");
			return false;
		} else
			die("poll");
		break;
	case 0:
		// log(1, "poll: timeout expired");
		return true;
	default:
		// rlog(1, 1, "poll: %d descriptors ready", n);
		break;
	}
	return true;
}


// Helper function for w_cleanup that links together extra bufs allocated
// by netmap in the strange format it requires to free them correctly.
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


// Shut down warpcore cleanly.
void w_cleanup(struct warpcore * const w)
{
	log(1, "warpcore shutting down");

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
	// 	log(5, "buf in extra chain idx %d", n);
	// 	n = *(uint32_t *)b;
	// }

	if (munmap(w->mem, w->req.nr_memsize) == -1)
		die("cannot munmap netmap memory");

	if (close(w->fd) == -1)
		die("cannot close /dev/netmap");

	free(w->udp);
	free(w->tcp);

	free(w);
}


// Interrupt handler.
static void w_sigint(int sig __attribute__((__unused__))) {}


// Initialize warpcore on the given interface.
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
		die("%s: cannot get interface information", ifname);
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
				// get MAC addr
				memcpy(&w->mac,
				       ((struct sockaddr_ll *)i->ifa_addr)->sll_addr,
				       sizeof w->mac);
				// get MTU
				int s;
				struct ifreq ifr;
				if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
					die("%s socket", ifname);
				strcpy(ifr.ifr_name, i->ifa_name);
				if (ioctl(s, SIOCGIFMTU, &ifr) < 0)
					die("%s ioctl", ifname);
				w->mtu = ifr.ifr_ifru.ifru_mtu;
				// get link speed
				struct ethtool_cmd edata;
				ifr.ifr_data = (__caddr_t)&edata;
				edata.cmd = ETHTOOL_GSET;
				if (ioctl(s, SIOCETHTOOL, &ifr) < 0)
					die("%s ioctl", ifname);
				w->mbps = ethtool_cmd_speed(&edata);
				close(s);
#else
			case AF_LINK:
				// get MAC addr
				memcpy(&w->mac,
				       LLADDR((struct sockaddr_dl *)
				              i->ifa_addr),
				       sizeof w->mac);
				// get MTU
				w->mtu = (uint16_t)((struct if_data *)
				         (i->ifa_data))->ifi_mtu;
				// get link speed
				w->mbps = (uint64_t)((struct if_data *)
				          (i->ifa_data))->ifi_baudrate/1000000;
#endif
				log(1, "%s addr %s, MTU %d, speed %dG",
				    i->ifa_name,
				    ether_ntoa_r((struct ether_addr *)w->mac,
				                 mac), w->mtu, w->mbps/1000);
				break;
			case AF_INET:
				// get IP addr and netmask
				w->ip = ((struct sockaddr_in *)
				         i->ifa_addr)->sin_addr.s_addr;
				w->mask = ((struct sockaddr_in *)
				           i->ifa_netmask)->sin_addr.s_addr;
				log(1, "%s has IP addr %s/%s", i->ifa_name,
				    ip_ntoa(w->ip, ip, sizeof ip),
				    ip_ntoa(w->mask, mask, sizeof mask));
				break;
			default:
				log(1, "ignoring unknown addr family %d on %s",
				    i->ifa_addr->sa_family, i->ifa_name);
				break;
			}
		}
	}
	freeifaddrs(ifap);
	if (w->ip == 0 || w->mask == 0 || w->mtu == 0 || IS_ZERO(w->mac))
		die("%s: cannot obtain needed interface information", ifname);

	w->bcast = w->ip | (~w->mask);
#ifndef NDEBUG
	char bcast[IP_ADDR_STRLEN];
	log(1, "%s has IP broadcast addr %s", ifname,
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
		die("%s: cannot put interface into netmap mode", ifname);

	// mmap the buffer region
	// TODO: see TODO in nm_open() in netmap_user.h
	if ((w->mem = mmap(0, w->req.nr_memsize, PROT_WRITE|PROT_READ,
	    MAP_SHARED, w->fd, 0)) == MAP_FAILED)
		die("cannot mmap netmap memory");

	// direct pointer to the netmap interface struct for convenience
	w->nif = NETMAP_IF(w->mem, w->req.nr_offset);

#ifndef NDEBUG
	// print some info about our rings
	for(uint32_t ri = 0; ri < w->nif->ni_tx_rings; ri++) {
		struct netmap_ring *r = NETMAP_TXRING(w->nif, ri);
		log(1, "tx ring %d has %d slots (%d-%d)", ri, r->num_slots,
		    r->slot[0].buf_idx, r->slot[r->num_slots - 1].buf_idx);
	}
	for(uint32_t ri = 0; ri < w->nif->ni_rx_rings; ri++) {
		struct netmap_ring *r = NETMAP_RXRING(w->nif, ri);
		log(1, "rx ring %d has %d slots (%d-%d)", ri, r->num_slots,
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
		// log(3, "available extra buf %d", i);
		SLIST_INSERT_HEAD(&w->iov, v, next);
		char * const b = v->buf;
		i = *(uint32_t *)b;
	}

	if (w->req.nr_arg3 != NUM_EXTRA_BUFS)
		die("can only allocate %d/%d extra buffers",
		    w->req.nr_arg3, NUM_EXTRA_BUFS);
	else
		log(1, "allocated %d extra buffers", w->req.nr_arg3);


	// initialize list of sockets
	SLIST_INIT(&w->sock);

	// allocate socket pointers
	w->udp = calloc(UINT16_MAX, sizeof(uint16_t));
	w->tcp = calloc(UINT16_MAX, sizeof(uint16_t));

	// block SIGINT
        if (signal(SIGINT, w_sigint) == SIG_ERR)
        	die("cannot register signal handler");

	return w;
}
