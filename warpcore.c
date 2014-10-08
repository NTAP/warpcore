#include <fcntl.h>
#include <sys/mman.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>
#include <sys/param.h>

#ifdef __linux__
#include <netinet/ether.h>
#include <netpacket/packet.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <sched.h>
#include <time.h>
#else
#include <sys/types.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <sys/cpuset.h>
#endif

#include "warpcore.h"
#include "ip.h"
#include "udp.h"
#include "icmp.h"

#define NUM_EXTRA_BUFS	16384

// global pointer to netmap engine
static struct warpcore * _w = 0;


// Use a spare iov to transmit an ARP query for the given destination
// IP address.
static inline void
arp_who_has(struct warpcore * const w, const uint32_t dip)
{
	// grab a spare buffer
	struct w_iov * const v = SLIST_FIRST(&w->iov);
	if (v == 0)
		die("out of spare bufs");
	SLIST_REMOVE_HEAD(&w->iov, next);
	v->buf = IDX2BUF(w, v->idx);

	// pointers to the start of the various headers
	struct eth_hdr * const eth = (struct eth_hdr *)(v->buf);
	struct arp_hdr * const arp =
		(struct arp_hdr *)((char *)(eth) + sizeof(struct eth_hdr));

	// set Ethernet header fields
	memcpy(eth->dst, ETH_BCAST, ETH_ADDR_LEN);
	memcpy(eth->src, w->mac, ETH_ADDR_LEN);
	eth->type = ETH_TYPE_ARP;

	// set ARP header fields
	arp->hrd =	htons(ARP_HRD_ETHER);
	arp->pro =	ETH_TYPE_IP;
	arp->hln =	ETH_ADDR_LEN;
	arp->pln =	IP_ADDR_LEN;
	arp->op =	htons(ARP_OP_REQUEST);
	memcpy(arp->sha, w->mac, ETH_ADDR_LEN);
	arp->spa =	w->ip;
	bzero(arp->tha, ETH_ADDR_LEN);
	arp->tpa =	dip;

#ifndef NDEBUG
	char spa[IP_ADDR_STRLEN];
	char tpa[IP_ADDR_STRLEN];
	dlog(notice, "ARP request who has %s tell %s",
	    ip_ntoa(arp->tpa, tpa, IP_ADDR_STRLEN),
	    ip_ntoa(arp->spa, spa, IP_ADDR_STRLEN));
#endif

	// send the Ethernet packet
	eth_tx(w, v, sizeof(struct eth_hdr) + sizeof(struct arp_hdr));
	w_kick_tx(w);

	// make iov available again
	SLIST_INSERT_HEAD(&w->iov, v, next);
}


// Close a warpcore socket, making all its iovs available again.
void
w_close(struct w_sock * const s)
{
	struct w_sock **ss = w_get_sock(s->w, s->p, s->sport);

	// make iovs of the socket available again
	while (!SLIST_EMPTY(&s->iv)) {
	     struct w_iov * const v = SLIST_FIRST(&s->iv);
	     dlog(debug, "free iv buf %d", v->idx);
	     SLIST_REMOVE_HEAD(&s->iv, next);
	     SLIST_INSERT_HEAD(&s->w->iov, v, next);
	}
	while (!SLIST_EMPTY(&s->ov)) {
	     struct w_iov * const v = SLIST_FIRST(&s->ov);
	     dlog(debug, "free ov buf %d", v->idx);
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
void
w_connect(struct w_sock * const s, const uint32_t dip, const uint16_t dport)
{
#ifndef NDEBUG
	char str[IP_ADDR_STRLEN];
	dlog(notice, "connect IP proto %d dst %s port %d", s->p,
	    ip_ntoa(dip, str, IP_ADDR_STRLEN), ntohs(dport));
#endif
	s->dip = dip;
	s->dport = dport;

	// find the Ethernet addr of the destination
	while (IS_ZERO(s->dmac)) {
		dlog(notice, "doing ARP lookup for %s",
		    ip_ntoa(dip, str, IP_ADDR_STRLEN));
		arp_who_has(s->w, dip);
		w_poll(s->w, POLLIN, 1000);
		w_kick_rx(s->w);
		w_rx(s);
		if (!IS_ZERO(s->dmac))
			break;
		dlog(warn, "no ARP reply, retrying");
	}

	// initialize the remaining fields of outgoing template header
	struct eth_hdr * const eth = (struct eth_hdr *)s->hdr;
	struct ip_hdr * const ip =
		(struct ip_hdr *)((char *)(eth) + sizeof(struct eth_hdr));
	struct udp_hdr * const udp =
		(struct udp_hdr *)((char *)(ip) + sizeof(struct ip_hdr));
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

	dlog(notice, "IP proto %d socket connected to %s port %d", s->p,
	    ip_ntoa(dip, str, IP_ADDR_STRLEN), ntohs(dport));
}


// Bind a socket for the given IP protocol and local port number.
struct w_sock *
w_bind(struct warpcore * const w, const uint8_t p, const uint16_t port)
{
	struct w_sock **s = w_get_sock(w, p, port);
	if (*s) {
		dlog(warn, "IP proto %d source port %d already in use",
		    p, ntohs(port));
		return 0;
	}

	if ((*s = calloc(1, sizeof(struct w_sock))) == 0)
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
		if (((*s)->hdr = calloc(1, (*s)->hdr_len)) == 0)
			die("cannot allocate w_hdr");
		eth = (struct eth_hdr *)(*s)->hdr;
		ip = (struct ip_hdr *)((char *)(eth) + sizeof(struct eth_hdr));
		udp = (struct udp_hdr *)((char *)(ip) + sizeof(struct ip_hdr));

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

	eth->type = ETH_TYPE_IP;
	memcpy(eth->src, (*s)->w->mac, ETH_ADDR_LEN);
	// eth->dst is set on w_connect()

	dlog(warn, "IP proto %d socket bound to port %d", (*s)->p, ntohs(port));

	return *s;
}


// Helper function for w_cleanup that links together extra bufs allocated
// by netmap in the strange format it requires to free them correctly.
static const struct w_iov *
w_chain_extra_bufs(const struct warpcore * const w, const struct w_iov *v)
{
	const struct w_iov * n;
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
void
w_cleanup(struct warpcore * const w)
{
	dlog(notice, "warpcore shutting down");

	// clean out all the tx rings
	for (uint32_t i = 0; i < w->nif->ni_rx_rings; i++) {
		struct netmap_ring * const txr =
			NETMAP_TXRING(w->nif, w->cur_txr);
		while (nm_tx_pending(txr)) {
			dlog(info, "tx pending on ring %d", w->cur_txr);
			w_kick_tx(w);
			usleep(1); // wait 1 tick
		}
	}

	// re-construct the extra bufs list, so netmap can free the memory
	const struct w_iov * last = w_chain_extra_bufs(w, SLIST_FIRST(&w->iov));
	struct w_sock *s;
	SLIST_FOREACH(s, &w->sock, next) {
		if (!SLIST_EMPTY(&s->iv)) {
			const struct w_iov * const l =
				w_chain_extra_bufs(w, SLIST_FIRST(&s->iv));
			*(uint32_t *)(last->buf) = SLIST_FIRST(&s->iv)->idx;
			last = l;

		}
		if (!SLIST_EMPTY(&s->ov)) {
			const struct w_iov * const lov =
				w_chain_extra_bufs(w, SLIST_FIRST(&s->ov));
			*(uint32_t *)(last->buf) = SLIST_FIRST(&s->ov)->idx;
			last = lov;
		}
		*(uint32_t *)(last->buf) = 0;
	}
	w->nif->ni_bufs_head = SLIST_FIRST(&w->iov)->idx;

#ifndef NDEBUG
	// print some info about our rings
	for (uint32_t ri = 0; ri < w->nif->ni_tx_rings; ri++) {
		const struct netmap_ring * const txr = NETMAP_TXRING(w->nif, ri);
		for (uint32_t txs = 0; txs < txr->num_slots; txs++)
			dlog(debug, "tx ring %d slot %d buf %d", ri, txs,
			    txr->slot[txs].buf_idx);
	}
	for (uint32_t ri = 0; ri < w->nif->ni_rx_rings; ri++) {
		const struct netmap_ring * const rxr = NETMAP_RXRING(w->nif, ri);
		for (uint32_t rxs = 0; rxs < rxr->num_slots; rxs++)
			dlog(debug, "rx ring %d slot %d buf %d", ri, rxs,
			    rxr->slot[rxs].buf_idx);
	}
#endif

	if (munmap(w->mem, w->req.nr_memsize) == -1)
		die("cannot munmap netmap memory");

	if (close(w->fd) == -1)
		die("cannot close /dev/netmap");

	// free extra buffer list
	while (!SLIST_EMPTY(&w->iov)) {
		struct w_iov * const n = SLIST_FIRST(&w->iov);
		SLIST_REMOVE_HEAD(&w->iov, next);
		free(n);
	}

	free(w->udp);
	free(w->tcp);

	free(w);
	_w = 0;
}


// Interrupt handler.
static void
w_handler(int sig __attribute__((__unused__)))
{
	if (_w)
		_w->interrupt = true;
}


void
w_init_common(void)
{
	// initialize random generator
#ifdef __linux__
	srandom(time(0));
#else
	srandomdev();
#endif

	// Set CPU affinity to highest core
	int i;
#ifdef __linux__
	cpu_set_t myset;
	if (sched_getaffinity(0, sizeof(cpu_set_t), &myset) == -1)
		die("sched_getaffinity");
#else
	cpuset_t myset;
	if (cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID, -1,
	    sizeof(cpuset_t), &myset) == -1)
		die("cpuset_getaffinity");
#endif

	// Find last available CPU
	for (i = CPU_SETSIZE-1; i >= 0; i--)
		if (CPU_ISSET(i, &myset))
			break;
	if (i == 0)
		die("not allowed to run on any CPUs!?");

	// Set new CPU mask
	dlog(warn, "setting affinity to CPU %d", i);
	CPU_ZERO(&myset);
	CPU_SET(i, &myset);

#ifdef __linux__
	if (sched_setaffinity(0, sizeof(myset), &myset) == -1)
		die("sched_setaffinity");
#else
	if (cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID, -1,
	    sizeof(cpuset_t), &myset) == -1)
		die("cpuset_setaffinity");
#endif

	// lock memory
	if (mlockall(MCL_CURRENT|MCL_FUTURE) == -1)
		die("mlockall");
}


// Initialize warpcore on the given interface.
struct warpcore *
w_init(const char * const ifname)
{
	struct warpcore *w;
	bool link_up = false;

	if (_w)
		die("can only have one warpcore engine active");

	// allocate struct
	if ((w = calloc(1, sizeof(struct warpcore))) == 0)
		die("cannot allocate struct warpcore");

	// we mostly loop here because the link may be down
	while (link_up == false || IS_ZERO(w->mac) ||
	       w->mtu == 0 || w->mbps == 0 || w->ip == 0 || w->mask == 0) {

		// get interface information
		struct ifaddrs *ifap;
		if (getifaddrs(&ifap) == -1)
			die("%s: cannot get interface information", ifname);

		for (const struct ifaddrs *i = ifap; i->ifa_next;
		     i = i->ifa_next) {
			if (strcmp(i->ifa_name, ifname) != 0)
				continue;
#ifndef NDEBUG
			char mac[ETH_ADDR_STRLEN];
#endif
			switch (i->ifa_addr->sa_family) {
#ifdef __linux__
			case AF_PACKET:
				// get MAC addr
				memcpy(&w->mac,
				       ((struct sockaddr_ll *)i->ifa_addr)
				       ->sll_addr, ETH_ADDR_LEN);

				// get MTU
				int s;
				struct ifreq ifr;
				bzero(&ifr, sizeof(struct ifreq));
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

				// get link status
				if (ioctl(s, SIOCGIFFLAGS, &ifr) < 0)
					die("%s ioctl", ifname);
				link_up = (ifr.ifr_flags & IFF_UP) &&
					  (ifr.ifr_flags & IFF_RUNNING);
				close(s);
#else
			case AF_LINK:
				// get MAC addr
				memcpy(&w->mac,
				       LLADDR((struct sockaddr_dl *)
					      i->ifa_addr),
				       ETH_ADDR_LEN);

				// get MTU
				w->mtu = (uint16_t)((struct if_data *)
					 (i->ifa_data))->ifi_mtu;

				// get link speed
				w->mbps = (uint32_t)((struct if_data *)
					  (i->ifa_data))->ifi_baudrate/1000000;

				// get link status
				link_up = (((uint8_t)((struct if_data *)
				           (i->ifa_data))->ifi_link_state) ==
					   LINK_STATE_UP);
#endif
				dlog(warn, "%s addr %s, MTU %d, speed %dG, link %s",
				    i->ifa_name,
				    ether_ntoa_r((struct ether_addr *)w->mac,
						 mac), w->mtu, w->mbps/1000,
				    link_up ? "up" : "down");
				break;
			case AF_INET:
				// get IP addr and netmask
				if (!w->ip)
					w->ip = ((struct sockaddr_in *)
						 i->ifa_addr)->sin_addr.s_addr;
				if (!w->mask)
					w->mask = ((struct sockaddr_in *)
						   i->ifa_netmask)->sin_addr.s_addr;
				break;
			default:
				dlog(notice, "ignoring unknown addr family %d on %s",
				    i->ifa_addr->sa_family, i->ifa_name);
				break;
			}
		}
		freeifaddrs(ifap);
		sleep(1);
	}
	if (w->ip == 0 || w->mask == 0 || w->mtu == 0 || IS_ZERO(w->mac))
		die("%s: cannot obtain needed interface information", ifname);

	w->bcast = w->ip | (~w->mask);

#ifndef NDEBUG
	char ip[IP_ADDR_STRLEN];
	char mask[IP_ADDR_STRLEN];
	dlog(warn, "%s has IP addr %s/%s", ifname,
	    ip_ntoa(w->ip, ip, IP_ADDR_STRLEN),
	    ip_ntoa(w->mask, mask, IP_ADDR_STRLEN));
	char bcast[IP_ADDR_STRLEN];
	dlog(warn, "%s has IP broadcast addr %s", ifname,
	    ip_ntoa(w->bcast, bcast, IP_ADDR_STRLEN));
#endif

	// open /dev/netmap
	if ((w->fd = open("/dev/netmap", O_RDWR)) == -1)
		die("cannot open /dev/netmap");

	// switch interface to netmap mode
	strncpy(w->req.nr_name, ifname, sizeof w->req.nr_name);
	w->req.nr_name[sizeof w->req.nr_name - 1] = '\0';
	w->req.nr_version = NETMAP_API;
	w->req.nr_ringid &= ~NETMAP_RING_MASK;
	// don't always transmit on poll
	w->req.nr_ringid |= NETMAP_NO_TX_POLL;
	w->req.nr_flags = NR_REG_ALL_NIC;
	w->req.nr_arg3 = NUM_EXTRA_BUFS; // request extra buffers
	if (ioctl(w->fd, NIOCREGIF, &w->req) == -1)
		die("%s: cannot put interface into netmap mode", ifname);

	// mmap the buffer region
	// TODO: see TODO in nm_open() in netmap_user.h
	const int flags =
#ifdef __linux__
		MAP_POPULATE|MAP_LOCKED;
#else
		MAP_PREFAULT_READ|MAP_NOSYNC|MAP_ALIGNED_SUPER;
#endif
	if ((w->mem = mmap(0, w->req.nr_memsize, PROT_WRITE|PROT_READ,
	    MAP_SHARED|flags, w->fd, 0)) == MAP_FAILED)
		die("cannot mmap netmap memory");

	// direct pointer to the netmap interface struct for convenience
	w->nif = NETMAP_IF(w->mem, w->req.nr_offset);

#ifndef NDEBUG
	// print some info about our rings
	for (uint32_t ri = 0; ri < w->nif->ni_tx_rings; ri++) {
		const struct netmap_ring * const r = NETMAP_TXRING(w->nif, ri);
		dlog(notice, "tx ring %d has %d slots (%d-%d)", ri, r->num_slots,
		    r->slot[0].buf_idx, r->slot[r->num_slots - 1].buf_idx);
	}
	for (uint32_t ri = 0; ri < w->nif->ni_rx_rings; ri++) {
		const struct netmap_ring * const r = NETMAP_RXRING(w->nif, ri);
		dlog(notice, "rx ring %d has %d slots (%d-%d)", ri, r->num_slots,
		    r->slot[0].buf_idx, r->slot[r->num_slots - 1].buf_idx);
	}
#endif

	// save the indices of the extra buffers in the warpcore structure
	SLIST_INIT(&w->iov);
	for (uint32_t n = 0, i = w->nif->ni_bufs_head;
	     n < w->req.nr_arg3; n++) {
		struct w_iov * const v = calloc(1, sizeof(struct w_iov));
		if (v == 0)
			die("cannot allocate w_iov");
		v->buf = IDX2BUF(w, i);
		v->idx = i;
		// dlog(notice, "available extra buf %d", i);
		SLIST_INSERT_HEAD(&w->iov, v, next);
		char * const b = v->buf;
		i = *(uint32_t *)b;
	}

	if (w->req.nr_arg3 != NUM_EXTRA_BUFS)
		die("can only allocate %d/%d extra buffers",
		    w->req.nr_arg3, NUM_EXTRA_BUFS);
	else
		dlog(warn, "allocated %d extra buffers", w->req.nr_arg3);


	// initialize list of sockets
	SLIST_INIT(&w->sock);

	// allocate socket pointers
	if ((w->udp = calloc(UINT16_MAX, sizeof(struct w_sock *))) == 0)
		die("cannot allocate UDP sockets");
	if ((w->tcp = calloc(UINT16_MAX, sizeof(struct w_sock *))) == 0)
		die("cannot allocate TCP sockets");

	// do the common system setup which is also useful for non-warpcore
	w_init_common();

	// block SIGINT and SIGTERM
	if (signal(SIGINT, w_handler) == SIG_ERR)
		die("cannot register SIGINT handler");
	if (signal(SIGTERM, w_handler) == SIG_ERR)
		die("cannot register SIGTERM handler");

	_w = w;
	return w;
}
