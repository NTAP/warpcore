// Copyright (c) 2014-2017, NetApp, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

// IWYU pragma: no_include <net/netmap.h>
#ifdef WITH_NETMAP
#include <net/netmap_user.h> // IWYU pragma: keep
#endif

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef __linux__
#include <net/ethernet.h>
#include <netinet/ether.h>
#else
#include <net/ethernet.h>
#endif

#include <warpcore.h>

#include "backend.h"
#include "config.h"
#include "eth.h"
#include "ip.h"
#include "udp.h"

extern struct w_iov * alloc_iov(struct warpcore * w);
extern struct w_sock * get_sock(struct warpcore * w, uint16_t port);


/// A global list of netmap engines that have been initialized for different
/// interfaces.
///
struct w_engines engines = SLIST_HEAD_INITIALIZER(engines);


/// Helper function for w_alloc_size and w_alloc_cnt. Really only needed,
/// because Linux doesn't define STALIQ_LAST in sys/queue.h for whatever reason.
///
/// @param      w         Warpcore engine
/// @param      c         Chain w_iov structs.
/// @param[in]  count     Number of w_iov structs to allocate.
/// @param[in]  off       Additional offset for each buffer.
/// @param[in]  adj_last  Amount to reduce the length of the last w_iov by.
///
static inline void alloc_cnt(struct warpcore * const w,
                             struct w_iov_chain * const c,
                             const uint32_t count,
                             const uint16_t off,
                             const uint16_t adj_last)
{
    STAILQ_INIT(c);
    struct w_iov * v = 0;
    for (uint32_t i = 0; i < count; i++) {
        v = alloc_iov(w);
        v->buf = (uint8_t *)v->buf + sizeof(struct w_hdr) + off;
        v->len -= (sizeof(struct w_hdr) + off);
        STAILQ_INSERT_TAIL(c, v, next);
    }
    if (v)
        v->len -= adj_last;
    warn(info,
         "allocated w_iov_chain of len %zu byte%s (%d w_iov%s, offset %d)",
         count * (w->mtu - sizeof(struct w_hdr) - off) - adj_last,
         plural(count * (w->mtu - sizeof(struct w_hdr) - off) - adj_last),
         count, plural(count), off);
}


/// Allocate a w_iov chain for @p len payload bytes, for eventual use with
/// w_tx(). The chain must be later returned to warpcore w_free().  If a @p off
/// offset is specified, leave this much extra space before @p buf in each
/// w_iov. This is meant for upper-level protocols that wish to reserve space
/// for their headers.
///
/// @param      w     Warpcore engine.
/// @param      c     Chain of w_iov structs.
/// @param[in]  len   Amount of payload bytes in the returned chain.
/// @param[in]  off   Additional offset for @p buf.
///
void w_alloc_len(struct warpcore * const w,
                 struct w_iov_chain * const c,
                 const uint32_t len,
                 const uint16_t off)
{
    const uint32_t space = w->mtu - sizeof(struct w_hdr) - off;
    const uint32_t count = len / space + (len % space != 0);
    alloc_cnt(w, c, count, off, (uint16_t)(space * count - len));
}


/// Allocate a w_iov chain of @p count packets, for eventual use with w_tx().
/// The chain must be later returned to warpcore w_free(). If a @p off offset is
/// specified, leave this much extra space before @p buf in each w_iov. This is
/// meant for upper-level protocols that wish to reserve space for their
/// headers.
///
/// @param      w      Warpcore engine.
/// @param      c      Chain of w_iov structs.
/// @param[in]  count  Number of packets in the returned chain.
/// @param[in]  off    Additional offset for @p buf.
///
void w_alloc_cnt(struct warpcore * const w,
                 struct w_iov_chain * const c,
                 const uint32_t count,
                 const uint16_t off)
{
    alloc_cnt(w, c, count, off, 0);
}


/// Return a w_iov chain obtained via w_alloc_len(), w_alloc_cnt() or w_rx()
/// back to warpcore.
///
/// @param      w     Warpcore engine.
/// @param      c     Chain of w_iov structs to free.
///
void w_free(struct warpcore * const w, struct w_iov_chain * const c)
{
    STAILQ_CONCAT(&w->iov, c);
}


/// Return the total payload length of w_iov chain @p c.
///
/// @param[in]  c     The w_iov chain to compute the payload length of.
/// @param[in]  off   Additional offset in each @p buf.
///
/// @return     Sum of the payload lengths of the w_iov structs in @p c.
///
uint32_t w_iov_chain_len(const struct w_iov_chain * const c, const uint16_t off)
{
    uint32_t l = 0;
    if (likely(c)) {
        const struct w_iov * v;
        STAILQ_FOREACH (v, c, next)
            l += (v->len - off);
    }
    return l;
}


/// Return the number of w_iov structures in the w_iov chain @p c.
///
/// @param[in]  c     The w_iov chain to compute the payload length of.
///
/// @return     Number of w_iov structs in @p c.
///
uint32_t w_iov_chain_cnt(const struct w_iov_chain * const c)
{
    uint32_t l = 0;
    if (likely(c)) {
        const struct w_iov * v;
        STAILQ_FOREACH (v, c, next)
            l++;
    }
    return l;
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
/// @param      s      w_sock to connect.
/// @param[in]  ip    Destination IPv4 address to bind to.
/// @param[in]  port  Destination UDP port to bind to.
///
void w_connect(struct w_sock * const s, const uint32_t ip, const uint16_t port)
{
    s->hdr->ip.dst = ip;
    s->hdr->udp.dport = port;
    backend_connect(s);

#ifndef NDEBUG
    char str[INET_ADDRSTRLEN];
    warn(notice, "IP proto %d socket connected to %s port %d", s->hdr->ip.p,
         inet_ntop(AF_INET, &ip, str, INET_ADDRSTRLEN), ntohs(port));
#endif
}


void w_disconnect(struct w_sock * const s)
{
    s->hdr->ip.dst = 0;
    s->hdr->udp.dport = 0;

    warn(notice, "IP proto %d socket disconnected", s->hdr->ip.p);
}


/// Bind a w_sock to the given local UDP port number.
///
/// @param      w      The w_sock to bind.
/// @param[in]  port   The local port number to bind to, in network byte order.
/// @param[in]  flags  Flags for this socket.
///
/// @return     Pointer to a bound w_sock.
///
struct w_sock *
w_bind(struct warpcore * const w, const uint16_t port, const uint8_t flags)
{
    struct w_sock * s = get_sock(w, port);
    if (unlikely(s)) {
        warn(warn, "UDP source port %d already in bound", ntohs(port));
        return s;
    }

    ensure((s = calloc(1, sizeof(*s))) != 0, "cannot allocate w_sock");
    ensure((s->hdr = calloc(1, sizeof(*s->hdr))) != 0, "cannot allocate w_hdr");

    // initialize flags
    s->flags = flags;

    // initialize the non-zero fields of outgoing template header
    s->hdr->eth.type = ETH_TYPE_IP;
    memcpy(&s->hdr->eth.src, w->mac, ETH_ADDR_LEN);
    // s->hdr->eth.dst is set on w_connect()

    ip_hdr_init(&s->hdr->ip);
    s->hdr->ip.src = w->ip;
    s->hdr->udp.sport = port;
    // s->hdr->ip.dst is set on w_connect()

    s->w = w;
    SLIST_INSERT_HEAD(&w->sock, s, next);
    STAILQ_INIT(&s->iv);

    backend_bind(s);

    warn(notice, "IP proto %d socket bound to port %d", s->hdr->ip.p,
         ntohs(port));

    return s;
}


/// Close a warpcore socket. This dequeues all data from w_sock::iv and
/// w_sock::ov, i.e., data will *not* be placed in rings and sent.
///
/// @param      s     w_sock to close.
///
void w_close(struct w_sock * const s)
{
    // make iovs of the socket available again
    STAILQ_CONCAT(&s->w->iov, &s->iv);

    // remove the socket from list of sockets
    SLIST_REMOVE(&s->w->sock, s, w_sock, next);

    // free the template header
    free(s->hdr);

    // free the socket
    free(s);
}


/// Return any new data that has been received on a socket by appending it to @p
/// c. The chain must eventually be returned to warpcore via w_free().
///
/// @param      s     w_sock for which the application would like to receive new
///                   data.
/// @param      c     w_iov chain.
///
///
void w_rx(struct w_sock * const s, struct w_iov_chain * const c)
{
    STAILQ_CONCAT(c, &s->iv);
}


/// Shut a warpcore engine down cleanly. In addition to calling into the
/// backend-specific cleanup function, it frees up the extra buffers and other
/// memory structures.
///
/// @param      w     Warpcore engine.
///
void w_cleanup(struct warpcore * const w)
{
    warn(notice, "warpcore shutting down");

    // close all sockets
    while (!SLIST_EMPTY(&w->sock)) {
        struct w_sock * s = SLIST_FIRST(&w->sock);
        SLIST_REMOVE_HEAD(&w->sock, next);
        w_close(s);
    }
    backend_cleanup(w);
    free(w->bufs);
    SLIST_REMOVE(&engines, w, warpcore, next);
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
struct warpcore * w_init(const char * const ifname, const uint32_t rip)
{
    struct warpcore * w;
    bool link_up = false;

    // allocate engine struct
    ensure((w = calloc(1, sizeof(*w))) != 0, "cannot allocate struct warpcore");

    // initialize lists of sockets and iovs
    SLIST_INIT(&w->sock);
    STAILQ_INIT(&w->iov);

    // backend-specific init
    backend_init(w, ifname);

    // get interface config
    // we mostly loop here because the link may be down
    do {
        // get interface information
        struct ifaddrs * ifap;
        ensure(getifaddrs(&ifap) != -1, "%s: cannot get interface information",
               ifname);

        bool found = false;
        for (const struct ifaddrs * i = ifap; i; i = i->ifa_next) {
            if (strcmp(i->ifa_name, ifname) != 0)
                continue;
            else
                found = true;

            switch (i->ifa_addr->sa_family) {
            case AF_LINK:
                plat_get_mac(w->mac, i);
                w->mtu = plat_get_mtu(i);
                link_up = plat_get_link(i);
#ifndef NDEBUG
                // mpbs can be zero on generic platforms and loopback interfaces
                const uint32_t mbps = plat_get_mbps(i);
                struct ether_addr a;
                memcpy(&a, w->mac, ETH_ADDR_LEN);
                warn(notice, "%s addr %s, MTU %d, speed %uG, link %s",
                     i->ifa_name, ether_ntoa(&a), w->mtu, mbps / 1000,
                     link_up ? "up" : "down");
#endif
                break;
            case AF_INET:
                // get IP addr and netmask
                if (w->ip == 0) {
                    w->ip = ((struct sockaddr_in *)(void *)i->ifa_addr)
                                ->sin_addr.s_addr;
                    w->mask = ((struct sockaddr_in *)(void *)i->ifa_netmask)
                                  ->sin_addr.s_addr;
                }
                break;
            case AF_INET6:
                break;
            default:
                warn(notice, "ignoring unknown addr family %d on %s",
                     i->ifa_addr->sa_family, i->ifa_name);
                break;
            }
        }
        ensure(found, "unknown interface %s", ifname);

        freeifaddrs(ifap);
        if (link_up == false || w->mtu == 0 || w->ip == 0 || w->mask == 0) {
            // sleep for a bit, so we don't burn the CPU when link is down
            warn(warn, "%s: could not obtain required interface "
                       "information, retrying",
                 ifname);
            sleep(1);
        }

    } while (link_up == false || w->mtu == 0 || w->ip == 0 || w->mask == 0);

    // set the IP address of our default router
    w->rip = rip;

#ifndef NDEBUG
    char ip_str[INET_ADDRSTRLEN];
    char mask_str[INET_ADDRSTRLEN];
    char rip_str[INET_ADDRSTRLEN];
    warn(notice, "%s has IP addr %s/%s%s%s", ifname,
         inet_ntop(AF_INET, &w->ip, ip_str, INET_ADDRSTRLEN),
         inet_ntop(AF_INET, &w->mask, mask_str, INET_ADDRSTRLEN),
         rip ? ", router " : "",
         rip ? inet_ntop(AF_INET, &w->rip, rip_str, INET_ADDRSTRLEN) : "");
#endif

    // store the initialized engine in our global list
    SLIST_INSERT_HEAD(&engines, w, next);

    warn(info, "%s %s with %s backend on %s ready", warpcore_name,
         warpcore_version, w->backend, ifname);
    return w;
}


/// Return warpcore engine serving w_sock @p s.
///
/// @param[in]  s     A w_sock.
///
/// @return     The warpcore engine for w_sock @p s.
///
struct warpcore * w_engine(const struct w_sock * const s)
{
    return s->w;
}


/// Return a w_sock_slist containing all sockets with pending inbound data.
/// Caller needs to free() the returned value before the next call to
/// w_rx_ready(). Data can be obtained via w_rx() on each w_sock in the list.
///
/// @param[in]  w     Warpcore engine.
///
/// @return     List of w_sock sockets that have incoming data pending.
///
struct w_sock_slist * w_rx_ready(const struct warpcore * w)
{
    // make a new w_sock_slist
    struct w_sock_slist * sl = calloc(1, sizeof(*sl));
    ensure(sl, "calloc w_sock_slist");
    SLIST_INIT(sl);

    // insert all sockets with pending inbound data
    struct w_sock * s;
    SLIST_FOREACH (s, &w->sock, next)
        if (!STAILQ_EMPTY(&s->iv))
            SLIST_INSERT_HEAD(sl, s, next_rx);

    return sl;
}


/// Return the maximum size a given w_iov may have for the given engine.
/// Basically, subtracts the header space and any offset specified when
/// allocating the w_iov from the MTU.
///
/// @param[in]  w     Warpcore engine.
/// @param[in]  v     The w_iov in question.
///
/// @return     Maximum length of the data in a w_iov for this engine.
///
uint16_t w_iov_max_len(const struct warpcore * const w,
                       const struct w_iov * const v)
{
    const uint16_t offset =
        (const uint16_t)((const uint8_t * const)v->buf -
                         (const uint8_t * const)IDX2BUF(w, v->idx));
    return w->mtu - offset;
}
