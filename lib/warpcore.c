// Copyright (c) 2014-2016, NetApp, Inc.
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
#include <netinet/ether.h>
#else
#include <net/ethernet.h>
#endif

#include "backend.h"
#include "eth.h"
#include "ip.h"
#include "udp.h"
#include "version.h"
#include "warpcore.h"


extern struct w_iov * alloc_iov(struct warpcore * const w);


/// A global list of netmap engines that have been initialized for different
/// interfaces.
///
static SLIST_HEAD(engines, warpcore) wc = SLIST_HEAD_INITIALIZER(wc);


/// Allocate a w_iov chain for @p payload bytes, for eventual use with w_tx().
/// Must be later freed with w_free(). If a @p off offset is specified, leave
/// this much extra space before @p buf in each w_iov. This is meant for
/// upper-level protocols that wish to reserve space for their headers.
///
/// @param      w     Warpcore engine.
/// @param[in]  len   Amount of payload bytes in the returned chain.
/// @param[in]  off   Additional offset for @p buf.
///
/// @return     Chain of w_iov structs.
///
struct w_iov_chain *
w_alloc(struct warpcore * const w, const uint32_t len, const uint16_t off)
{
    struct w_iov * v = 0;
    int32_t l = (int32_t)len;
    struct w_iov_chain * chain = calloc(1, sizeof(*chain));
    assert(chain, "could not calloc");
    STAILQ_INIT(chain);
#ifndef NDEBUG
    uint32_t n = 0;
#endif
    while (l > 0) {
        v = alloc_iov(w);
        v->buf = (uint8_t *)v->buf + sizeof(struct w_hdr) + off;
        v->len -= (sizeof(struct w_hdr) + off);
        l -= v->len;
        STAILQ_INSERT_TAIL(chain, v, next);
#ifndef NDEBUG
        n++;
#endif
    }

    if (v)
        // adjust length of last iov so chain is the exact length requested
        v->len += l; // l is negative

    warn(info, "allocated w_iov_chain (len %d in %d w_iov%s, offset %d)", len,
         n, plural(n), off);
    return chain;
}


/// Return a w_iov chain obtained via w_alloc() or w_rx() back to warpcore. The
/// application must not use @p v after this call.
///
/// Do not make this , so the caller doesn't have to check v.
///
/// @param      w     Warpcore engine.
/// @param      c     Chain of w_iov structs to free.
///
void w_free(struct warpcore * const w, struct w_iov_chain * const c)
{
    STAILQ_CONCAT(&w->iov, c);
    free(c);
}


/// Return the total payload length of w_iov chain @p c.
///
/// @param[in]  c     The w_iov chain to compute the payload length of.
///
/// @return     Sum of the payload lengths of the w_iov structs in @p c.
///
uint32_t w_iov_chain_len(const struct w_iov_chain * const c)
{
    uint32_t l = 0;
    if (c) {
        const struct w_iov * v;
        STAILQ_FOREACH (v, c, next)
            l += v->len;
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
    if (c) {
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
/// @param[in]  dip    Destination IPv4 address to bind to.
/// @param[in]  dport  Destination UDP port to bind to.
///
void w_connect(struct w_sock * const s,
               const uint32_t dip,
               const uint16_t dport)
{
    s->hdr.ip.dst = dip;
    s->hdr.udp.dport = dport;
    backend_connect(s);

    warn(notice, "IP proto %d socket connected to %s port %d", s->hdr.ip.p,
         inet_ntoa(*(const struct in_addr * const) & dip), ntohs(dport));
}


void w_disconnect(struct w_sock * const s)
{
    s->hdr.ip.dst = 0;
    s->hdr.udp.dport = 0;

    warn(notice, "IP proto %d socket disconnected", s->hdr.ip.p);
}


/// Bind a w_sock to the given local UDP port number.
///
/// @param      w     The w_sock to bind.
/// @param[in]  port  The local port number to bind to, in network byte order.
///
/// @return     Pointer to a bound w_sock.
///
struct w_sock * w_bind(struct warpcore * const w, const uint16_t port)
{
    struct w_sock * s = w->udp[port];
    if (s) {
        warn(warn, "UDP source port %d already in bound", ntohs(port));
        return s;
    }

    assert((w->udp[port] = s = calloc(1, sizeof(*s))) != 0,
           "cannot allocate w_sock");

    // initialize the non-zero fields of outgoing template header
    s->hdr.eth.type = ETH_TYPE_IP;
    memcpy(&s->hdr.eth.src, w->mac, ETH_ADDR_LEN);
    // s->hdr.eth.dst is set on w_connect()

    ip_hdr_init(&s->hdr.ip);
    s->hdr.ip.src = w->ip;
    s->hdr.udp.sport = port;
    // s->hdr.ip.dst is set on w_connect()

    s->w = w;
    SLIST_INSERT_HEAD(&w->sock, s, next);
    s->iv = calloc(1, sizeof(*s->iv));
    assert(s->iv, "could not calloc");
    STAILQ_INIT(s->iv);

    backend_bind(s);

    warn(notice, "IP proto %d socket bound to port %d", s->hdr.ip.p,
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
    w_free(s->w, s->iv);

    // remove the socket from list of sockets
    SLIST_REMOVE(&s->w->sock, s, w_sock, next);

    // free the socket
    s->w->udp[s->hdr.udp.sport] = 0;
    free(s);
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
/// @return     Chain of received wFirst w_iov in w_sock::iv if there is new
/// data, or zero. Needs
///             to be freed with w_free() by the caller.
///
struct w_iov_chain * w_rx(struct w_sock * const s)
{
    backend_rx(s->w);
    if (STAILQ_EMPTY(s->iv))
        return 0;
    struct w_iov_chain * const empty = calloc(1, sizeof(*empty));
    assert(empty, "could not calloc");
    STAILQ_INIT(empty);
    struct w_iov_chain * const tmp = s->iv;
    s->iv = empty;
    return tmp;
}


/// Loops over the w_iov structures in the chain @p c, attemoting to send them
/// all.
///
/// @param[in]  s     { parameter_description }
/// @param      c     { parameter_description }
///
void w_tx(const struct w_sock * const s, struct w_iov_chain * const c)
{
    struct w_iov * v;
    STAILQ_FOREACH (v, c, next) {
        assert(s->hdr.ip.dst && s->hdr.udp.dport || v->ip && v->port,
               "no destination information");
        backend_tx(s, v);
    }
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
    backend_cleanup(w);
    free(w->bufs);
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
struct warpcore * w_init(const char * const ifname, const uint32_t rip)
{
    struct warpcore * w;
    bool link_up = false;

    // SLIST_FOREACH (w, &wc, next)
    //     assert(strcmp(ifname, w->req.nr_name),
    //            "can only have one warpcore engine active on %s", ifname);

    // allocate engine struct
    assert((w = calloc(1, sizeof(*w))) != 0, "cannot allocate struct warpcore");

    // we mostly loop here because the link may be down
    do {
        // get interface information
        struct ifaddrs * ifap;
        assert(getifaddrs(&ifap) != -1, "%s: cannot get interface information",
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
                warn(notice, "%s addr %s, MTU %d, speed %uG, link %s",
                     i->ifa_name,
                     ether_ntoa((const struct ether_addr * const)w->mac),
                     w->mtu, mbps / 1000, link_up ? "up" : "down");
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
        assert(found, "unknown interface %s", ifname);

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

    // initialize lists of sockets and iovs
    SLIST_INIT(&w->sock);
    STAILQ_INIT(&w->iov);

    // allocate socket pointers
    assert((w->udp = calloc(UINT16_MAX, sizeof(*w->udp))) != 0,
           "cannot allocate UDP sockets");

    backend_init(w, ifname);

    // store the initialized engine in our global list
    SLIST_INSERT_HEAD(&wc, w, next);

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
