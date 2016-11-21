#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef __linux__
#include <netinet/ether.h>
#else
#include <net/ethernet.h>
#endif

// #include "arp.h"
#include "plat.h"
#include "util.h"
#include "version.h"
#include "warpcore_internal.h"


/// A global list of netmap engines that have been initialized for different
/// interfaces.
///
static SLIST_HEAD(engines, warpcore) wc = SLIST_HEAD_INITIALIZER(wc);


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


/// Connect a bound socket to a remote IP address and port. Depending on the
/// backend, this function may block until a MAC address has been resolved with
/// ARP.
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

    backend_connect(s);

    warn(notice, "IP proto %d socket connected to %s port %d", s->hdr.ip.p,
         ip_ntoa(dip, str, IP_ADDR_STRLEN), ntohs(dport));
}


/// Bind a w_sock to the given local UDP port number.
///
/// @param      w     The w_sock to bind.
/// @param[in]  port  The local port number to bind to, in network byte order.
///
/// @return     Pointer to a bound w_sock.
///
struct w_sock * __attribute__((nonnull))
w_bind(struct warpcore * const w, const uint16_t port)
{
    struct w_sock ** const s = get_sock(w, port);
    if (s && *s) {
        warn(warn, "UDP source port %d already in use", ntohs(port));
        return 0;
    }

    assert((*s = calloc(1, sizeof(struct w_sock))) != 0,
           "cannot allocate struct w_sock");

    // initialize the non-zero fields of outgoing template header
    (*s)->hdr.eth.type = ETH_TYPE_IP;
    memcpy(&(*s)->hdr.eth.src, (*s)->w->mac, ETH_ADDR_LEN);
    // (*s)->hdr.eth.dst is set on w_connect()

    (*s)->hdr.ip.vhl = (4 << 4) + 5;
    (*s)->hdr.ip.ttl = 1; // XXX TODO: pick something sensible
    (*s)->hdr.ip.off |= htons(IP_DF);
    (*s)->hdr.ip.p = IP_P_UDP;
    (*s)->hdr.ip.src = (*s)->w->ip;
    (*s)->hdr.udp.sport = port;
    // (*s)->hdr.ip.dst is set on w_connect()

    (*s)->w = w;
    STAILQ_INIT(&(*s)->iv);
    SLIST_INSERT_HEAD(&w->sock, *s, next);

    backend_bind(*s);

    warn(notice, "IP proto %d socket bound to port %d", (*s)->hdr.ip.p,
         ntohs(port));

    return *s;
}


/// Close a warpcore socket. This dequeues all data from w_sock::iv and
/// w_sock::ov, i.e., data will *not* be placed in rings and sent.
///
/// @param      s     w_sock to close.
///
void __attribute__((nonnull)) w_close(struct w_sock * const s)
{
    struct w_sock ** const ss = get_sock(s->w, s->hdr.udp.sport);
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


/// Shut a warpcore engine down cleanly. In addition to calling into the
/// backend-specific cleanup function, it frees up the extra buffers and other
/// memory structures.
///
/// @param      w     Warpcore engine.
///
void __attribute__((nonnull)) w_cleanup(struct warpcore * const w)
{
    warn(notice, "warpcore shutting down");
    backend_cleanup(w);

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


/// Initialize a warpcore engine on the given interface. Ethernet and IPv4
/// source addresses and related information, such as the netmask, are taken
/// from the active OS configuration of the interface. A default router,
/// however, needs to be specified with @p rip, if communication over a WAN is
/// desired.
///
/// Since warpcore relies on random() to generate random values, the caller
/// should also set an initial seed with srandom() or srandomdev(). Warpcore
/// does not do this, to allow the application control over the seed.
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

    // SLIST_FOREACH (w, &wc, next)
    //     assert(strcmp(ifname, w->req.nr_name),
    //            "can only have one warpcore engine active on %s", ifname);

    // allocate engine struct
    assert((w = calloc(1, sizeof(struct warpcore))) != 0,
           "cannot allocate struct warpcore");

    // we mostly loop here because the link may be down
    // mpbs can be zero on generic platforms
    uint32_t mbps = 0;
    while (link_up == false || IS_ZERO(w->mac) || w->mtu == 0 || w->ip == 0 ||
           w->mask == 0) {

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
                warn(notice, "%s addr %s, MTU %d, speed %uG, link %s",
                     i->ifa_name, ether_ntoa((struct ether_addr *)w->mac),
                     w->mtu, mbps / 1000, link_up ? "up" : "down");
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
        usleep(10000);
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

    // initialize lists of sockets and iovs
    SLIST_INIT(&w->sock);
    STAILQ_INIT(&w->iov);

    // allocate socket pointers
    assert((w->udp = calloc(UINT16_MAX, sizeof(struct w_sock *))) != 0,
           "cannot allocate UDP sockets");

    backend_init(w, ifname);

    // store the initialized engine in our global list
    SLIST_INSERT_HEAD(&wc, w, next);

    warn(info, "%s %s with %s backend on %s ready", warpcore_name,
         warpcore_version, w->backend, ifname);
    return w;
}


/// Convert a network byte order IPv4 address into a string.
///
/// @param[in]     ip    An IPv4 address in network byte order.
/// @param[in,out] buf   The buffer in which to place the result.
/// @param[in]     size  The size of @p buf in bytes.
///
/// @return        A pointer to @p buf.
///
const __attribute__((nonnull)) char *
ip_ntoa(uint32_t ip, void * const buf, const size_t size)
{
    const uint32_t i = ntohl(ip);
    snprintf(buf, size, "%u.%u.%u.%u", (i >> 24) & 0xff, (i >> 16) & 0xff,
             (i >> 8) & 0xff, i & 0xff);
    ((char *)buf)[size - 1] = '\0';
    return buf;
}


/// Convert a string into a network byte order IP address.
///
/// @param[in]  ip    A string containing an IPv4 address in "xxx.xxx.xxx.xxx\0"
///                   format.
///
/// @return     The IPv4 address in @p ip as a 32-bit network byte order value.
///
uint32_t __attribute__((nonnull)) ip_aton(const char * const ip)
{
    uint32_t i;
    const int r = sscanf(ip, "%hhu.%hhu.%hhu.%hhu", (unsigned char *)(&i),
                         (unsigned char *)(&i) + 1, (unsigned char *)(&i) + 2,
                         (unsigned char *)(&i) + 3);
    return r == 4 ? i : 0;
}
