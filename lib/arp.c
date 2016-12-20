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
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/socket.h>

#ifdef __linux__
#include <netinet/ether.h>
#include <netinet/in.h>
#else
// clang-format off
// because these includes need to be in-order
#include <sys/types.h> // IWYU pragma: keep
#include <net/ethernet.h>
// clang-format on
#endif

#include "arp.h"
#include "backend.h"
#include "eth.h"
#include "ip.h"
#include "warpcore.h"


/// ARP cache entry.
///
struct arp_entry {
    SLIST_ENTRY(arp_entry) next; ///< Pointer to next cache entry.
    uint32_t ip;                 ///< IPv4 address.
    uint8_t mac[ETH_ADDR_LEN];   ///< Ethernet MAC address.
    /// @cond
    /// @internal Padding.
    uint8_t _unused[6];
    /// @endcond
};


/// Find the ARP cache entry associated with IPv4 address @p ip.
///
/// @param      w     Warpcore engine.
/// @param[in]  ip    IPv4 address to look up in ARP cache.
///
/// @return     Pointer to arp_entry of @p ip, or zero.
///
static struct arp_entry * __attribute__((nonnull))
arp_cache_find(struct warpcore * w, const uint32_t ip)
{
    struct arp_entry * a;
    SLIST_FOREACH (a, &w->arp_cache, next)
        if (a->ip == ip)
            return a;
    return 0;
}


/// Update the MAC address associated with IPv4 address @p ip in the ARP cache.
///
/// @param      w     Warpcore engine.
/// @param[in]  ip    IPv4 address to update the ARP cache for.
/// @param[in]  mac   New Ethernet MAC address of @p ip.
///
static void __attribute__((nonnull))
arp_cache_update(struct warpcore * w,
                 const uint32_t ip,
                 const uint8_t mac[ETH_ADDR_LEN])
{
    struct arp_entry * a = arp_cache_find(w, ip);
    if (a) {
        warn(info, "updating ARP cache entry: %s is at %s",
             inet_ntoa(*(const struct in_addr * const) & ip),
             ether_ntoa((const struct ether_addr * const)mac));
        memcpy(a->mac, mac, ETH_ADDR_LEN);
        return;
    }

    a = calloc(1, sizeof(*a));
    ensure(a, "cannot allocate arp_entry");
    a->ip = ip;
    memcpy(a->mac, mac, ETH_ADDR_LEN);
    SLIST_INSERT_HEAD(&w->arp_cache, a, next);
    warn(info, "new ARP cache entry: %s is at %s",
         inet_ntoa(*(const struct in_addr * const) & ip),
         ether_ntoa((const struct ether_addr * const)mac));
}


/// Modifies the ARP request in @p buf into a corresponding ARP reply, and sends
/// it. Helper function called by arp_rx().
///
/// @param      w     Warpcore engine
/// @param      buf   Buffer containing an incoming ARP request inside an
///                   Ethernet frame
///
static void __attribute__((nonnull))
arp_is_at(struct warpcore * const w, void * const buf)
{
    // grab iov for reply
    struct w_iov * v = alloc_iov(w);
    struct arp_hdr * const reply = eth_data(v->buf);

    // construct ARP header
    const struct arp_hdr * const req = eth_data(buf);

    reply->hrd = htons(ARP_HRD_ETHER);
    reply->pro = ETH_TYPE_IP;
    reply->hln = ETH_ADDR_LEN;
    reply->pln = IP_ADDR_LEN;
    reply->op = htons(ARP_OP_REPLY);
    memcpy(reply->sha, w->mac, ETH_ADDR_LEN);
    reply->spa = w->ip;
    memcpy(reply->tha, req->sha, ETH_ADDR_LEN);
    reply->tpa = req->spa;

    warn(notice, "ARP reply %s is at %s",
         inet_ntoa(*(const struct in_addr * const) & reply->spa),
         ether_ntoa((const struct ether_addr * const)reply->sha));

    // send the Ethernet packet
    struct eth_hdr * const eth = v->buf;
    memcpy(eth->dst, req->sha, ETH_ADDR_LEN);
    memcpy(eth->src, w->mac, ETH_ADDR_LEN);
    eth->type = ETH_TYPE_ARP;
    eth_tx(w, v, sizeof(*reply));
    w_nic_tx(w);
    STAILQ_INSERT_HEAD(&w->iov, v, next);
}


/// Return the Ethernet MAC address for target IP address @p dip. If there is no
/// entry in the ARP cache for the Ethernet MAC address corresponding to IPv4
/// address @p dip, this function will block while attempting to resolve the
/// address.
///
/// @param      w     Warpcore engine
/// @param[in]  dip   IP address that is the target of the ARP request
///
/// @return     Pointer to Ethernet MAC address (#ETH_ADDR_LEN bytes long) of @p
///             dip.
///
uint8_t * arp_who_has(struct warpcore * const w, const uint32_t dip)
{
    struct arp_entry * a = arp_cache_find(w, dip);
    while (a == 0) {
        warn(warn, "no ARP entry for %s, sending query",
             inet_ntoa(*(const struct in_addr * const) & dip));

        // grab a spare buffer
        struct w_iov * const v = alloc_iov(w);

        // pointers to the start of the various headers
        struct eth_hdr * const eth = v->buf;
        struct arp_hdr * const arp = eth_data(v->buf);

        // set Ethernet header fields
        memcpy(eth->dst, ETH_BCAST, ETH_ADDR_LEN);
        memcpy(eth->src, w->mac, ETH_ADDR_LEN);
        eth->type = ETH_TYPE_ARP;

        // set ARP header fields
        arp->hrd = htons(ARP_HRD_ETHER);
        arp->pro = ETH_TYPE_IP;
        arp->hln = ETH_ADDR_LEN;
        arp->pln = IP_ADDR_LEN;
        arp->op = htons(ARP_OP_REQUEST);
        memcpy(arp->sha, w->mac, ETH_ADDR_LEN);
        arp->spa = w->ip;
        memset(arp->tha, 0, ETH_ADDR_LEN);
        arp->tpa = dip;

#ifndef NDEBUG
        char tpa[INET_ADDRSTRLEN];
        char spa[INET_ADDRSTRLEN];
        warn(notice, "ARP request who has %s tell %s",
             inet_ntop(AF_INET, &arp->tpa, tpa, INET_ADDRSTRLEN),
             inet_ntop(AF_INET, &arp->spa, spa, INET_ADDRSTRLEN));
#endif

        // send the Ethernet packet
        eth_tx(w, v, sizeof(*eth) + sizeof(*arp));
        w_nic_tx(w);

        // make iov available again
        STAILQ_INSERT_HEAD(&w->iov, v, next);

        // wait until packets have been received, then handle them
        struct pollfd fds = {.fd = w->fd, .events = POLLIN};
        poll(&fds, 1, 1000);
        w_nic_rx(w);

        // check if we can now resolve dip
        a = arp_cache_find(w, dip);
    }
    return a->mac;
}


/// Receive an ARP packet, and react to it. This function parses an incoming ARP
/// packet contained in an Ethernet frame. For incoming ARP requests for the
/// local interface, respond appropriately. For incoming ARP replies, updates
/// the information in the w_sock structures of all open connections, as needed.
///
/// The Ethernet frame to operate on is in the current netmap lot of the
/// indicated RX ring.
///
/// @param      w     Warpcore engine
/// @param      r     Currently active netmap RX ring.
///
void arp_rx(struct warpcore * const w, struct netmap_ring * const r)
{
    void * const buf = NETMAP_BUF(r, r->slot[r->cur].buf_idx);
    const struct arp_hdr * const arp = eth_data(buf);
    const uint16_t hrd = ntohs(arp->hrd);

    ensure(hrd == ARP_HRD_ETHER && arp->hln == ETH_ADDR_LEN,
           "unhandled ARP hardware format %d with len %d", hrd, arp->hln);

    ensure(arp->pro == ETH_TYPE_IP && arp->pln == IP_ADDR_LEN,
           "unhandled ARP protocol format %d with len %d", ntohs(arp->pro),
           arp->pln);

    const uint16_t op = ntohs(arp->op);
    switch (op) {
    case ARP_OP_REQUEST: {
#ifndef NDEBUG
        char tpa[INET_ADDRSTRLEN];
        char spa[INET_ADDRSTRLEN];
        warn(notice, "ARP request who has %s tell %s",
             inet_ntop(AF_INET, &arp->tpa, tpa, INET_ADDRSTRLEN),
             inet_ntop(AF_INET, &arp->spa, spa, INET_ADDRSTRLEN));
#endif
        if (arp->tpa == w->ip)
            arp_is_at(w, buf);
        else
            warn(warn, "ignoring ARP request not asking for us");
        break;
    }

    case ARP_OP_REPLY: {
        warn(notice, "ARP reply %s is at %s",
             inet_ntoa(*(const struct in_addr * const) & arp->spa),
             ether_ntoa((const struct ether_addr * const)arp->sha));

        arp_cache_update(w, arp->spa, arp->sha);

        // check if any socket has an IP address matching this ARP
        // reply, and if so, change its destination MAC
        struct w_sock * s;
        SLIST_FOREACH (s, &w->sock, next) {
            if ( // is local-net socket and ARP src IP matches its dst
                ((mk_net(s->w->ip, s->w->mask) ==
                      mk_net(s->hdr->ip.dst, s->w->mask) &&
                  arp->spa == s->hdr->ip.dst)) ||
                // or non-local socket and ARP src IP matches router
                (s->w->rip && (s->w->rip == arp->spa))) {
                warn(notice, "updating socket with %s for %s",
                     ether_ntoa((const struct ether_addr * const)arp->sha),
                     inet_ntoa(*(const struct in_addr * const) & arp->spa));
                memcpy(&s->hdr->eth.dst, arp->sha, ETH_ADDR_LEN);
            }
        }
        break;
    }

    default:
        die("unhandled ARP operation %d", op);
        break;
    }
}


/// Free the ARP cache entries associated with engine @p w.
///
/// @param[in]  w     Warpcore engine.
///
void free_arp_cache(struct warpcore * const w)
{
    while (!SLIST_EMPTY(&w->arp_cache)) {
        struct arp_entry * a = SLIST_FIRST(&w->arp_cache);
        SLIST_REMOVE_HEAD(&w->arp_cache, next);
        free(a);
    }
}
