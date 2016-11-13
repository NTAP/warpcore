#include <arpa/inet.h>

#ifdef __linux__
#include <netinet/ether.h>
#else
// clang-format off
// because these includes need to be in-order
#include <sys/types.h>
#include <net/ethernet.h>
// clang-format on
#endif

#include "arp.h"
#include "util.h"
#include "warpcore_internal.h"


// This modifies the ARP query in the current receive buffer into an ARP reply
// and sends it out.
static void __attribute__((nonnull))
arp_is_at(struct warpcore * const w, void * const buf)
{
    struct arp_hdr * const arp = eth_data(buf);

    // modify ARP header
    arp->op = htons(ARP_OP_REPLY);
    memcpy(arp->tha, arp->sha, sizeof arp->tha);
    arp->tpa = arp->spa;
    memcpy(arp->sha, w->mac, sizeof arp->sha);
    arp->spa = w->ip;

#ifndef NDEBUG
    char sha[ETH_ADDR_STRLEN];
    char spa[IP_ADDR_STRLEN];
    warn(notice, "ARP reply %s is at %s", ip_ntoa(arp->spa, spa, sizeof spa),
         ether_ntoa_r((const struct ether_addr *)arp->sha, sha));
#endif

    // send the Ethernet packet
    eth_tx_rx_cur(w, buf, sizeof(struct arp_hdr));
    w_kick_tx(w);
}


// Use a spare iov to transmit an ARP query for the given destination
// IP address.
void __attribute__((nonnull))
arp_who_has(struct warpcore * const w, const uint32_t dip)
{
    // grab a spare buffer
    struct w_iov * const v = STAILQ_FIRST(&w->iov);
    assert(v != 0, "out of spare bufs");
    STAILQ_REMOVE_HEAD(&w->iov, next);
    v->buf = IDX2BUF(w, v->idx);

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
    bzero(arp->tha, ETH_ADDR_LEN);
    arp->tpa = dip;

#ifndef NDEBUG
    char spa[IP_ADDR_STRLEN];
    char tpa[IP_ADDR_STRLEN];
    warn(notice, "ARP request who has %s tell %s",
         ip_ntoa(arp->tpa, tpa, IP_ADDR_STRLEN),
         ip_ntoa(arp->spa, spa, IP_ADDR_STRLEN));
#endif

    // send the Ethernet packet
    eth_tx(w, v, sizeof(struct eth_hdr) + sizeof(struct arp_hdr));
    w_kick_tx(w);

    // make iov available again
    STAILQ_INSERT_HEAD(&w->iov, v, next);
}


// Receive an ARP packet, and react
void arp_rx(struct warpcore * const w, void * const buf)
{
#ifndef NDEBUG
    char tpa[IP_ADDR_STRLEN];
    char spa[IP_ADDR_STRLEN];
    char sha[ETH_ADDR_STRLEN];
#endif
    const struct arp_hdr * const arp = eth_data(buf);
    const uint16_t hrd = ntohs(arp->hrd);

    assert(hrd == ARP_HRD_ETHER && arp->hln == ETH_ADDR_LEN,
           "unhandled ARP hardware format %d with len %d", hrd, arp->hln);

    assert(arp->pro == ETH_TYPE_IP && arp->pln == IP_ADDR_LEN,
           "unhandled ARP protocol format %d with len %d", ntohs(arp->pro),
           arp->pln);

    const uint16_t op = ntohs(arp->op);
    switch (op) {
    case ARP_OP_REQUEST:
        warn(notice, "ARP request who has %s tell %s",
             ip_ntoa(arp->tpa, tpa, sizeof tpa),
             ip_ntoa(arp->spa, spa, sizeof spa));
        if (arp->tpa == w->ip)
            arp_is_at(w, buf);
        else
            warn(warn, "ignoring ARP request not asking for us");
        break;

    case ARP_OP_REPLY: {
        warn(notice, "ARP reply %s is at %s",
             ip_ntoa(arp->spa, spa, sizeof spa),
             ether_ntoa_r((const struct ether_addr *)arp->sha, sha));

        // check if any socket has an IP address matching this ARP
        // reply, and if so, change its destination MAC
        struct w_sock * s;
        SLIST_FOREACH (s, &w->sock, next) {
            if ( // is local-net socket and ARP src IP matches its dst
                ((mk_net(s->w->ip, s->w->mask) ==
                      mk_net(s->hdr.ip.dst, s->w->mask) &&
                  arp->spa == s->hdr.ip.dst)) ||
                // or non-local socket and ARP src IP matches router
                (s->w->rip && (s->w->rip == arp->spa))) {
                warn(notice, "updating socket with %s for %s",
                     ether_ntoa_r((const struct ether_addr *)arp->sha, sha),
                     ip_ntoa(arp->spa, spa, sizeof spa));
                memcpy(&s->hdr.eth.dst, arp->sha, ETH_ADDR_LEN);
            }
        }
        break;
    }

    default:
        die("unhandled ARP operation %d", op);
        break;
    }
}
