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
#include "backend.h"
#include "util.h"


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
    struct arp_hdr * const arp = eth_data(buf);

    // modify ARP header
    arp->op = htons(ARP_OP_REPLY);
    memcpy(arp->tha, arp->sha, sizeof arp->tha);
    arp->tpa = arp->spa;
    memcpy(arp->sha, w->mac, sizeof arp->sha);
    arp->spa = w->ip;

    warn(notice, "ARP reply %s is at %s",
         inet_ntoa(*(const struct in_addr * const) & arp->spa),
         ether_ntoa((const struct ether_addr * const)arp->sha));

    // send the Ethernet packet
    eth_tx_rx_cur(w, buf, sizeof(struct arp_hdr));
    w_nic_tx(w);
}


/// Send an ARP request for target IP address @p dip.
///
/// @param      w     Warpcore engine
/// @param[in]  dip   IP address that is the target of the ARP request
///
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
    eth_tx(w, v, sizeof(struct eth_hdr) + sizeof(struct arp_hdr));
    w_nic_tx(w);

    // make iov available again
    STAILQ_INSERT_HEAD(&w->iov, v, next);
}


/// Receive an ARP packet, and react to it. This function parses an incoming ARP
/// packet contained in an Ethernet frame. For incoming ARP requests for the
/// local interface, respond appropriately. For incoming ARP replies, updates
/// the information in the struct w_sock structures of all open connections, as
/// needed.
///
/// @param      w     Warpcore engine
/// @param      buf   Buffer containing incoming ARP request inside an Ethernet
///                   frame
///
void arp_rx(struct warpcore * const w, void * const buf)
{
    const struct arp_hdr * const arp = eth_data(buf);
    const uint16_t hrd = ntohs(arp->hrd);

    assert(hrd == ARP_HRD_ETHER && arp->hln == ETH_ADDR_LEN,
           "unhandled ARP hardware format %d with len %d", hrd, arp->hln);

    assert(arp->pro == ETH_TYPE_IP && arp->pln == IP_ADDR_LEN,
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
                     ether_ntoa((const struct ether_addr * const)arp->sha),
                     inet_ntoa(*(const struct in_addr * const) & arp->spa));
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
