#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <unistd.h>
#include <xmmintrin.h>

#ifdef __linux__
#include <netinet/ether.h>
#else
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <sys/types.h>
#endif

#include "arp.h"
#include "plat.h"
#include "util.h"
#include "version.h"
#include "warpcore_internal.h"


/// The number of extra buffers to allocate from netmap. Extra buffers are
/// buffers that are not used to support TX or RX rings, but instead are used
/// for the warpcore w_sock::iv and w_sock::ov socket buffers, as well as for
/// maintaining packetized data inside an application using warpcore.
///
#define NUM_EXTRA_BUFS 256 // 16384 XXX this should become configurable


/// A global list of netmap engines that have been initialized for different
/// interfaces.
///
static SLIST_HEAD(engines, warpcore) wc = SLIST_HEAD_INITIALIZER(wc);


// XXX API change needed
/// Allocates a chain of w_iov structs to support transmitting of @p len bytes.
///
/// @param      s     { parameter_description }
/// @param[in]  len   The length
///
/// @return     { description_of_the_return_value }
///
struct w_iov * __attribute__((nonnull))
w_tx_alloc(struct w_sock * const s, const uint32_t len)
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


/// Return the file descriptor associated with a w_sock. This is either the
/// per-interface netmap file descriptor, or it is an OS file descriptor for the
/// warpcore shim. In either case, it can be used for poll() or with event-loop
/// libraries in the application.
///
/// @param      s     w_sock socket for which to get the underlying descriptor.
///
/// @return     A file descriptor.
///
int w_fd(struct w_sock * const s)
{
    return s->w->fd;
}


// XXX rethink API
// User needs to call this once they are done with touching any received data.
// This makes the iov that holds the received data available to warpcore again.
void __attribute__((nonnull)) w_rx_done(struct w_sock * const s)
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


/// Iterates over any new data in the RX rings, appending them to the w_sock::iv
/// socket buffers of the respective w_sock structures associated with a given
/// sender IPv4 address and port.
///
/// Unlike with the Socket API, w_rx() can append data to w_sock::iv chains
/// *other* that that of the w_sock passed as @p s. This is, because warpcore
/// needs to drain the RX rings, in order to allow new data to be received by
/// the NIC. It would be inconvenient to require the application to constantly
/// iterate over all w_sock sockets it has opened.
///
/// This means that although w_rx() may return zero, because no new data has
/// been received on @p s, it may enqueue new data into the w_sock::iv chains of
/// other w_sock socket.
///
/// @param      s     w_sock for which the application would like to receive new
///                   data.
///
/// @return     First w_iov in w_sock::iv if there is new data, or zero.
///
struct w_iov * __attribute__((nonnull)) w_rx(struct w_sock * const s)
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
    return STAILQ_FIRST(&s->iv);
}


/// Push data placed in the TX rings via udp_tx() and similar methods out onto
/// the link.{ function_description }
///
/// @param[in]  w     Warpcore engine.
///
void __attribute__((nonnull)) w_kick_tx(const struct warpcore * const w)
{
    assert(ioctl(w->fd, NIOCTXSYNC, 0) != -1, "cannot kick tx ring");
}


/// Trigger netmap to make new received data available to w_rx().
///
/// @param[in]  w     Warpcore engine.
///
void __attribute__((nonnull)) w_kick_rx(const struct warpcore * const w)
{
    assert(ioctl(w->fd, NIOCRXSYNC, 0) != -1, "cannot kick rx ring");
}


/// Places payloads that are queued up at @p s w_sock::ov into IPv4 UDP packets,
/// and attempts to move them onto a TX ring. Not all payloads may be placed if
/// the TX rings fills up first. Also, the packets are not send yet; w_kick_tx()
/// needs to be called for that. This is, so that an application has control
/// over exactly when to schedule packet I/O.
///
/// @param      s     w_sock socket whose payload data should be processed.
///
void __attribute__((nonnull)) w_tx(struct w_sock * const s)
{
    if (s->hdr.ip.p == IP_P_UDP)
        udp_tx(s);
    else
        die("unhandled IP proto %d", s->hdr.ip.p);
}


/// Close a warpcore socket. This dequeues all data from w_sock::iv and
/// w_sock::ov, i.e., data will *not* be placed in rings and sent.
///
/// @param      s     w_sock to close.
///
void __attribute__((nonnull)) w_close(struct w_sock * const s)
{
    struct w_sock ** const ss = get_sock(s->w, s->hdr.ip.p, s->hdr.udp.sport);
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


/// Connect a bound socket to a remote IP address and port. If the Ethernet MAC
/// address of the destination (or the default router towards it) is not known,
/// w_connect() will block trying to look it up via ARP.
///
/// Calling w_connect() will make subsequent w_tx() operations on the w_sock
/// enqueue payload data towards that destination. Unlike with the Socket API,
/// w_connect() can be called several times, which will re-bind a connected
/// w_sock and allows a server application to send data to multiple peers over a
/// w_sock.
///
/// @param      s      w_sock to bind.
/// @param[in]  dip    Destination IPv4 address to bind to.
/// @param[in]  dport  Destination UDP port to bind to.
///
void __attribute__((nonnull))
w_connect(struct w_sock * const s, const uint32_t dip, const uint16_t dport)
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

    // find the Ethernet addr of the destination or the default router
    while (IS_ZERO(s->hdr.eth.dst)) {
        const uint32_t ip = s->w->rip && (mk_net(dip, s->w->mask) !=
                                          mk_net(s->hdr.ip.src, s->w->mask))
                                ? s->w->rip
                                : dip;
        warn(notice, "doing ARP lookup for %s",
             ip_ntoa(ip, str, IP_ADDR_STRLEN));
        arp_who_has(s->w, ip);
        struct pollfd _fds = {.fd = s->w->fd, .events = POLLIN};
        poll(&_fds, 1, 1000);
        w_kick_rx(s->w);
        w_rx(s);
        if (!IS_ZERO(s->hdr.eth.dst))
            break;
        warn(warn, "no ARP reply for %s, retrying",
             ip_ntoa(ip, str, IP_ADDR_STRLEN));
    }

    warn(notice, "IP proto %d socket connected to %s port %d", s->hdr.ip.p,
         ip_ntoa(dip, str, IP_ADDR_STRLEN), ntohs(dport));
}


/// Bind a w_sock to the given IP protocol and local port number. The protocol
/// @p p at the moment must be #IP_P_UDP.
///
/// @param      w     The w_sock to bind.
/// @param[in]  p     The IP protocol to bind to. Must be #IP_P_UDP.
/// @param[in]  port  The local port number to bind to, in network byte order.
///
/// @return     Pointer to a bound w_sock.
///
struct w_sock * __attribute__((nonnull))
w_bind(struct warpcore * const w, const uint8_t p, const uint16_t port)
{
    assert(p == IP_P_UDP, "unhandled IP proto %d", p);

    struct w_sock ** const s = get_sock(w, p, port);
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


/// Helper function for w_cleanup() that links together extra bufs allocated by
/// netmap in the strange format it requires to free them correctly.
///
/// @param[in]  w     Warpcore engine.
/// @param[in]  v     w_iov chain to link together for netmap.
///
/// @return     Last element of the linked w_iov chain.
///
static const struct w_iov * __attribute__((nonnull))
w_chain_extra_bufs(const struct warpcore * const w, const struct w_iov * v)
{
    const struct w_iov * n;
    do {
        n = STAILQ_NEXT(v, next);
        uint32_t * const buf = (void *)IDX2BUF(w, v->idx);
        if (n) {
            *buf = n->idx;
            v = n;
        } else
            *buf = 0;
    } while (n);

    // return the last list element
    return v;
}


/// Shut a warpcore engine down cleanly. This function pushes out all buffers
/// already placed into TX rings out, and returns all w_iov structures
/// associated with all w_sock structures of the engine to netmap.
///
/// @param      w     Warpcore engine.
///
void __attribute__((nonnull)) w_cleanup(struct warpcore * const w)
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
    SLIST_FOREACH (s, &w->sock, next) {
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
    SLIST_REMOVE(&wc, w, warpcore, next);
    free(w);
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


/// Initialize a warpcore engine on the given interface. Ethernet and IPv4
/// source addresses and related information, such as the netmask, are taken
/// from the active OS configuration of the interface. A default router,
/// however, needs to be specified with @p rip, if communication over a WAN is
/// desired.
///
/// @param[in]  ifname  The OS name of the interface (e.g., "eth0").
/// @param[in]  rip     The default router to be used for non-local
///                     destinations. Can be zero.
///
/// @return     Initialized warpcore engine.
///
struct warpcore * __attribute__((nonnull))
w_init(const char * const ifname, const uint32_t rip)
{
    struct warpcore * w;
    bool link_up = false;

    SLIST_FOREACH (w, &wc, next)
        assert(strcmp(ifname, w->req.nr_name),
               "can only have one warpcore engine active on %s", ifname);

    // allocate engine struct
    assert((w = calloc(1, sizeof(struct warpcore))) != 0,
           "cannot allocate struct warpcore");

    // we mostly loop here because the link may be down
    uint32_t mbps = 0;
    while (link_up == false || IS_ZERO(w->mac) || w->mtu == 0 || mbps == 0 ||
           w->ip == 0 || w->mask == 0) {

        // get interface information
        struct ifaddrs * ifap;
        assert(getifaddrs(&ifap) != -1, "%s: cannot get interface information",
               ifname);

        bool found = false;
        for (const struct ifaddrs * i = ifap; i->ifa_next; i = i->ifa_next) {
            if (strcmp(i->ifa_name, ifname) != 0)
                continue;
            else
                found = true;

            switch (i->ifa_addr->sa_family) {
            case AF_LINK:
                plat_get_mac(w->mac, i);
                w->mtu = plat_get_mtu(i);
                mbps = plat_get_mbps(i);
                link_up = plat_get_link(i);
#ifndef NDEBUG
                char mac[ETH_ADDR_STRLEN];
                warn(notice, "%s addr %s, MTU %d, speed %uG, link %s",
                     i->ifa_name,
                     ether_ntoa_r((struct ether_addr *)w->mac, mac), w->mtu,
                     mbps / 1000, link_up ? "up" : "down");
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
        assert(found, "unknown interface %s", ifname);

        // sleep for a bit, so we don't burn the CPU when link is down
        usleep(50);
    }
    assert(w->ip != 0 && w->mask != 0 && w->mtu != 0 && !IS_ZERO(w->mac),
           "%s: cannot obtain needed interface information", ifname);

    // set the IP address of our default router
    w->rip = rip;

#ifndef NDEBUG
    char ip[IP_ADDR_STRLEN];
    char rtr[IP_ADDR_STRLEN];
    char mask[IP_ADDR_STRLEN];
    warn(notice, "%s has IP addr %s/%s%s%s", ifname,
         ip_ntoa(w->ip, ip, IP_ADDR_STRLEN),
         ip_ntoa(w->mask, mask, IP_ADDR_STRLEN), rip ? ", router " : "",
         rip ? ip_ntoa(w->rip, rtr, IP_ADDR_STRLEN) : "");
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
        i = *(uint32_t *)v->buf;
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

    // store the initialized engine in our global list
    SLIST_INSERT_HEAD(&wc, w, next);

    warn(info, "%s %s on %s ready", warpcore_name, warpcore_version, ifname);
    return w;
}
