#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <unistd.h>

#ifdef __linux__
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <netinet/ether.h>
#include <netpacket/packet.h>
#else
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <sys/types.h>
#endif

#include "icmp.h"
#include "ip.h"
#include "plat.h"
#include "udp.h"
#include "warpcore.h"

#define NUM_EXTRA_BUFS 256 // 16384

// global pointer to netmap engine
static struct warpcore * _w = 0;


// Allocates an iov of a given size for tx preparation.
struct w_iov * w_tx_alloc(struct w_sock * const s, const uint32_t len)
{
    assert(len, "len is zero");
    if (unlikely(!STAILQ_EMPTY(&s->ov))) {
        warn(warn, "output iov already allocated");
        return 0;
    }

    // add enough buffers to the iov so it is > len
    STAILQ_INIT(&s->ov);
    struct w_iov * v = 0;
    int32_t l = (int32_t)len;
    uint32_t n = 0;
    while (l > 0) {
        // grab a spare buffer
        v = STAILQ_FIRST(&s->w->iov);
        assert(v != 0, "out of spare bufs after grabbing %d", n);
        STAILQ_REMOVE_HEAD(&s->w->iov, next);
        warn(debug, "grabbing spare buf %u for user tx", v->idx);
        v->buf = IDX2BUF(s->w, v->idx) + sizeof(s->hdr);
        v->len = s->w->mtu - sizeof(s->hdr);
        l -= v->len;
        n++;

        // add the iov to the tail of the socket
        STAILQ_INSERT_TAIL(&s->ov, v, next);
    }
    // adjust length of last iov so chain is the exact length requested
    v->len += l; // l is negative

    warn(info, "allocating iov (len %d in %d bufs) for user tx", len, n);

    return STAILQ_FIRST(&s->ov);
}


// Wait until netmap is ready to send or receive more data. Parameters
// "event" and "timeout" identical to poll system call.
void w_poll(const struct warpcore * const w, const short ev, const int to)
{
    struct pollfd fds = {.fd = w->fd, .events = ev};
    const int n = poll(&fds, 1, to);

    if (unlikely(n == -1)) {
        if (errno == EINTR) {
            warn(notice, "poll: interrupt");
            return;
        } else
            die("poll");
    }
    if (unlikely(n == 0)) {
        warn(notice, "poll: timeout expired");
        return;
    }

    rwarn(debug, 1, "poll: %d descriptors ready", n);
    return;
}


// User needs to call this once they are done with touching any received data.
// This makes the iov that holds the received data available to warpcore again.
void w_rx_done(struct w_sock * const s)
{
    struct w_iov * i = STAILQ_FIRST(&s->iv);
    while (i) {
        // move i from the socket to the available iov list
        struct w_iov * const n = STAILQ_NEXT(i, next);
        STAILQ_REMOVE_HEAD(&s->iv, next);
        STAILQ_INSERT_HEAD(&s->w->iov, i, next);
        i = n;
    }
}


// Pulls new received data out of the rx ring and places it into socket iovs.
// Returns an iov of any data received.
struct w_iov * w_rx(struct w_sock * const s)
{
    // loop over all rx rings starting with cur_rxr and wrapping around
    for (uint32_t i = 0; likely(i < s->w->nif->ni_rx_rings); i++) {
        struct netmap_ring * const r = NETMAP_RXRING(s->w->nif, s->w->cur_rxr);
        while (!nm_ring_empty(r)) {
            // prefetch the next slot into the cache
            _mm_prefetch(
                NETMAP_BUF(r, r->slot[nm_ring_next(r, r->cur)].buf_idx),
                _MM_HINT_T1);

            // process the current slot
            eth_rx(s->w, NETMAP_BUF(r, r->slot[r->cur].buf_idx));
            r->head = r->cur = nm_ring_next(r, r->cur);
        }
        s->w->cur_rxr = (s->w->cur_rxr + 1) % s->w->nif->ni_rx_rings;
    }

    if (s)
        return STAILQ_FIRST(&s->iv);
    return 0;
}


// Internal warpcore function. Kick the tx ring.
void w_kick_tx(const struct warpcore * const w)
{
    assert(ioctl(w->fd, NIOCTXSYNC, 0) != -1, "cannot kick tx ring");
}


// Internal warpcore function. Kick the rx ring.
void w_kick_rx(const struct warpcore * const w)
{
    assert(ioctl(w->fd, NIOCRXSYNC, 0) != -1, "cannot kick rx ring");
}


// Prepends all network headers and places s->ov in the tx ring.
void w_tx(struct w_sock * const s)
{
    if (s->hdr.ip.p == IP_P_UDP)
        udp_tx(s);
    else
        die("unhandled IP proto %d", s->hdr.ip.p);
}


// Close a warpcore socket, making all its iovs available again.
void w_close(struct w_sock * const s)
{
    struct w_sock ** ss = w_get_sock(s->w, s->hdr.ip.p, s->hdr.udp.sport);
    assert(ss && *ss, "no socket found");

    // make iovs of the socket available again
    while (!STAILQ_EMPTY(&s->iv)) {
        struct w_iov * const v = STAILQ_FIRST(&s->iv);
        warn(debug, "free iv buf %u", v->idx);
        STAILQ_REMOVE_HEAD(&s->iv, next);
        STAILQ_INSERT_HEAD(&s->w->iov, v, next);
    }
    while (!STAILQ_EMPTY(&s->ov)) {
        struct w_iov * const v = STAILQ_FIRST(&s->ov);
        warn(debug, "free ov buf %u", v->idx);
        STAILQ_REMOVE_HEAD(&s->ov, next);
        STAILQ_INSERT_HEAD(&s->w->iov, v, next);
    }

    // remove the socket from list of sockets
    SLIST_REMOVE(&s->w->sock, s, w_sock, next);

    // free the socket
    free(*ss);
    *ss = 0;
}


// Connect a bound socket to a remote IP address and port.
void w_connect(struct w_sock * const s,
               const uint32_t dip,
               const uint16_t dport)
{
    assert(s->hdr.ip.p == IP_P_UDP, "unhandled IP proto %d", s->hdr.ip.p);

#ifndef NDEBUG
    char str[IP_ADDR_STRLEN];
#endif
    if (s->hdr.ip.dst == dip && s->hdr.udp.dport == dport)
        // already connected to that peer
        return;

    s->hdr.ip.dst = dip;
    s->hdr.udp.dport = dport;

    // find the Ethernet addr of the destination
    while (IS_ZERO(s->hdr.eth.dst)) {
        warn(notice, "doing ARP lookup for %s",
             ip_ntoa(dip, str, IP_ADDR_STRLEN));
        arp_who_has(s->w, dip);
        w_poll(s->w, POLLIN, 1000);
        if (s->w->interrupt)
            return;
        w_kick_rx(s->w);
        w_rx(s);
        if (!IS_ZERO(s->hdr.eth.dst))
            break;
        warn(warn, "no ARP reply for %s, retrying",
             ip_ntoa(dip, str, IP_ADDR_STRLEN));
    }

    warn(notice, "IP proto %d socket connected to %s port %d", s->hdr.ip.p,
         ip_ntoa(dip, str, IP_ADDR_STRLEN), ntohs(dport));
}


// Bind a socket for the given IP protocol and local port number.
struct w_sock *
w_bind(struct warpcore * const w, const uint8_t p, const uint16_t port)
{
    assert(p == IP_P_UDP, "unhandled IP proto %d", p);

    struct w_sock ** s = w_get_sock(w, p, port);
    if (s && *s) {
        warn(warn, "IP proto %d source port %d already in use", p, ntohs(port));
        return 0;
    }

    assert((*s = calloc(1, sizeof(struct w_sock))) != 0,
           "cannot allocate struct w_sock");

    (*s)->hdr.ip.p = p;
    (*s)->hdr.udp.sport = port;
    (*s)->w = w;
    STAILQ_INIT(&(*s)->iv);
    SLIST_INSERT_HEAD(&w->sock, *s, next);

    // initialize the non-zero fields of outgoing template header
    (*s)->hdr.eth.type = ETH_TYPE_IP;
    memcpy(&(*s)->hdr.eth.src, (*s)->w->mac, ETH_ADDR_LEN);
    // (*s)->hdr.eth.dst is set on w_connect()

    (*s)->hdr.ip.vhl = (4 << 4) + 5;
    (*s)->hdr.ip.ttl = 1; // XXX TODO: pick something sensible
    (*s)->hdr.ip.off |= htons(IP_DF);
    (*s)->hdr.ip.p = p;
    (*s)->hdr.ip.src = (*s)->w->ip;
    // (*s)->hdr.ip.dst is set on w_connect()

    warn(notice, "IP proto %d socket bound to port %d", (*s)->hdr.ip.p,
         ntohs(port));

    return *s;
}


// Helper function for w_cleanup that links together extra bufs allocated
// by netmap in the strange format it requires to free them correctly.
static const struct w_iov * w_chain_extra_bufs(const struct warpcore * const w,
                                               const struct w_iov * v)
{
    const struct w_iov * n;
    do {
        n = STAILQ_NEXT(v, next);
        uint32_t * const buf = (uint32_t *)(void *)IDX2BUF(w, v->idx);
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
    warn(notice, "warpcore shutting down");

    // clean out all the tx rings
    for (uint32_t i = 0; i < w->nif->ni_rx_rings; i++) {
        struct netmap_ring * const txr = NETMAP_TXRING(w->nif, w->cur_txr);
        while (nm_tx_pending(txr)) {
            warn(info, "tx pending on ring %u", w->cur_txr);
            w_kick_tx(w);
            usleep(1); // wait 1 tick
        }
    }

    // re-construct the extra bufs list, so netmap can free the memory
    const struct w_iov * last = w_chain_extra_bufs(w, STAILQ_FIRST(&w->iov));
    struct w_sock * s;
    SLIST_FOREACH(s, &w->sock, next)
    {
        if (!STAILQ_EMPTY(&s->iv)) {
            const struct w_iov * const l =
                w_chain_extra_bufs(w, STAILQ_FIRST(&s->iv));
            *(uint32_t *)(last->buf) = STAILQ_FIRST(&s->iv)->idx;
            last = l;
        }
        if (!STAILQ_EMPTY(&s->ov)) {
            const struct w_iov * const lov =
                w_chain_extra_bufs(w, STAILQ_FIRST(&s->ov));
            *(uint32_t *)(last->buf) = STAILQ_FIRST(&s->ov)->idx;
            last = lov;
        }
        *(uint32_t *)(last->buf) = 0;
    }
    w->nif->ni_bufs_head = STAILQ_FIRST(&w->iov)->idx;

    assert(munmap(w->mem, w->req.nr_memsize) != -1,
           "cannot munmap netmap memory");

    assert(close(w->fd) != -1, "cannot close /dev/netmap");

    // free extra buffer list
    while (!STAILQ_EMPTY(&w->iov)) {
        struct w_iov * const n = STAILQ_FIRST(&w->iov);
        STAILQ_REMOVE_HEAD(&w->iov, next);
        free(n);
    }

    free(w->udp);

    free(w);
    _w = 0;
}


// Interrupt handler.
static void w_handler(int sig __attribute__((unused)))
{
    if (_w)
        _w->interrupt = true;
}


void w_init_common(void)
{
    // initialize random generator
    plat_srandom();

    // Set CPU affinity to one core
    plat_setaffinity();

    // lock memory
    assert(mlockall(MCL_CURRENT | MCL_FUTURE) != -1, "mlockall");
}


// Initialize warpcore on the given interface.
struct warpcore * w_init(const char * const ifname)
{
    struct warpcore * w;
    bool link_up = false;

    assert(_w == 0, "can only have one warpcore engine active");

    // allocate struct
    assert((w = calloc(1, sizeof(struct warpcore))) != 0,
           "cannot allocate struct warpcore");

    // we mostly loop here because the link may be down
    while (link_up == false || IS_ZERO(w->mac) || w->mtu == 0 || w->mbps == 0 ||
           w->ip == 0 || w->mask == 0) {

        // get interface information
        struct ifaddrs * ifap;
        assert(getifaddrs(&ifap) != -1, "%s: cannot get interface information",
               ifname);

        for (const struct ifaddrs * i = ifap; i->ifa_next; i = i->ifa_next) {
            if (strcmp(i->ifa_name, ifname) != 0)
                continue;

            switch (i->ifa_addr->sa_family) {
            case AF_LINK:
                plat_get_mac(w->mac, i);
                w->mtu = plat_get_mtu(i);
                w->mbps = plat_get_mbps(i);
                link_up = plat_get_link(i);
#ifndef NDEBUG
                char mac[ETH_ADDR_STRLEN];
                warn(notice, "%s addr %s, MTU %d, speed %uG, link %s",
                     i->ifa_name,
                     ether_ntoa_r((struct ether_addr *)w->mac, mac), w->mtu,
                     w->mbps / 1000, link_up ? "up" : "down");
#endif
                break;
            case AF_INET:
                // get IP addr and netmask
                if (!w->ip)
                    w->ip = ((struct sockaddr_in *)(void *)i->ifa_addr)
                                ->sin_addr.s_addr;
                if (!w->mask)
                    w->mask = ((struct sockaddr_in *)(void *)i->ifa_netmask)
                                  ->sin_addr.s_addr;
                break;
            default:
                warn(notice, "ignoring unknown addr family %d on %s",
                     i->ifa_addr->sa_family, i->ifa_name);
                break;
            }
        }
        freeifaddrs(ifap);
        // sleep for a bit, so we don't burn the CPU when link is down
        usleep(50);
    }
    assert(w->ip != 0 && w->mask != 0 && w->mtu != 0 && !IS_ZERO(w->mac),
           "%s: cannot obtain needed interface information", ifname);

    w->bcast = w->ip | (~w->mask);

#ifndef NDEBUG
    char ip[IP_ADDR_STRLEN];
    char mask[IP_ADDR_STRLEN];
    warn(notice, "%s has IP addr %s/%s", ifname,
         ip_ntoa(w->ip, ip, IP_ADDR_STRLEN),
         ip_ntoa(w->mask, mask, IP_ADDR_STRLEN));
    char bcast[IP_ADDR_STRLEN];
    warn(notice, "%s has IP broadcast addr %s", ifname,
         ip_ntoa(w->bcast, bcast, IP_ADDR_STRLEN));
#endif

    // open /dev/netmap
    assert((w->fd = open("/dev/netmap", O_RDWR)) != -1,
           "cannot open /dev/netmap");

    // switch interface to netmap mode
    strncpy(w->req.nr_name, ifname, sizeof w->req.nr_name);
    w->req.nr_name[sizeof w->req.nr_name - 1] = '\0';
    w->req.nr_version = NETMAP_API;
    w->req.nr_ringid &= ~NETMAP_RING_MASK;
    // don't always transmit on poll
    w->req.nr_ringid |= NETMAP_NO_TX_POLL;
    w->req.nr_flags = NR_REG_ALL_NIC;
    w->req.nr_arg3 = NUM_EXTRA_BUFS; // request extra buffers
    assert(ioctl(w->fd, NIOCREGIF, &w->req) != -1,
           "%s: cannot put interface into netmap mode", ifname);

    // mmap the buffer region
    // TODO: see TODO in nm_open() in netmap_user.h
    const int flags = PLAT_MMFLAGS;
    assert((w->mem = mmap(0, w->req.nr_memsize, PROT_WRITE | PROT_READ,
                          MAP_SHARED | flags, w->fd, 0)) != MAP_FAILED,
           "cannot mmap netmap memory");

    // direct pointer to the netmap interface struct for convenience
    w->nif = NETMAP_IF(w->mem, w->req.nr_offset);

#ifndef NDEBUG
    // print some info about our rings
    for (uint32_t ri = 0; ri < w->nif->ni_tx_rings; ri++) {
        const struct netmap_ring * const r = NETMAP_TXRING(w->nif, ri);
        warn(info, "tx ring %d has %d slots (%d-%d)", ri, r->num_slots,
             r->slot[0].buf_idx, r->slot[r->num_slots - 1].buf_idx);
    }
    for (uint32_t ri = 0; ri < w->nif->ni_rx_rings; ri++) {
        const struct netmap_ring * const r = NETMAP_RXRING(w->nif, ri);
        warn(info, "rx ring %d has %d slots (%d-%d)", ri, r->num_slots,
             r->slot[0].buf_idx, r->slot[r->num_slots - 1].buf_idx);
    }
#endif

    // save the indices of the extra buffers in the warpcore structure
    STAILQ_INIT(&w->iov);
    for (uint32_t n = 0, i = w->nif->ni_bufs_head; n < w->req.nr_arg3; n++) {
        struct w_iov * const v = calloc(1, sizeof(struct w_iov));
        assert(v != 0, "cannot allocate w_iov");
        v->buf = IDX2BUF(w, i);
        v->idx = i;
        STAILQ_INSERT_HEAD(&w->iov, v, next);
        void * const b = v->buf;
        i = *(uint32_t *)b;
    }

    if (w->req.nr_arg3 != NUM_EXTRA_BUFS)
        die("can only allocate %d/%d extra buffers", w->req.nr_arg3,
            NUM_EXTRA_BUFS);
    else
        warn(notice, "allocated %d extra buffers", w->req.nr_arg3);


    // initialize list of sockets
    SLIST_INIT(&w->sock);

    // allocate socket pointers
    assert((w->udp = calloc(UINT16_MAX, sizeof(struct w_sock *))) != 0,
           "cannot allocate UDP sockets");

    // do the common system setup which is also useful for non-warpcore
    w_init_common();

    // block SIGINT and SIGTERM
    assert(signal(SIGINT, w_handler) != SIG_ERR,
           "cannot register SIGINT handler");
    assert(signal(SIGTERM, w_handler) != SIG_ERR,
           "cannot register SIGTERM handler");

    _w = w;
    return w;
}
