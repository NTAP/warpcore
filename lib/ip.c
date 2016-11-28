#include <arpa/inet.h>

#include "backend.h"
#include "icmp.h"
#include "ip.h"
#include "udp.h"
#include "util.h"


#ifndef NDEBUG
/// Print a textual representation of ip_hdr @p ip.
///
/// @param      ip    The ip_hdr to print.
///
#define ip_log(ip)                                                             \
    do {                                                                       \
        char src[INET_ADDRSTRLEN];                                             \
        char dst[INET_ADDRSTRLEN];                                             \
        warn(notice, "IP: %s -> %s, dscp %d, ecn %d, ttl %d, id %d, "          \
                     "flags [%s%s], proto %d, hlen/tot %d/%d",                 \
             inet_ntop(AF_INET, &ip->src, src, INET_ADDRSTRLEN),               \
             inet_ntop(AF_INET, &ip->dst, dst, INET_ADDRSTRLEN), ip_dscp(ip),  \
             ip_ecn(ip), ip->ttl, ntohs(ip->id),                               \
             (ntohs(ip->off) & IP_MF) ? "MF" : "",                             \
             (ntohs(ip->off) & IP_DF) ? "DF" : "", ip->p, ip_hl(ip),           \
             ntohs(ip->len));                                                  \
    } while (0)
#else
#define ip_log(ip)                                                             \
    do {                                                                       \
    } while (0)
#endif


/// This function prepares the current *receive* buffer for reflection. It swaps
/// the source and destination IPv4 addresses, adjusts various ip_hdr fields and
/// passes the buffer to eth_tx_rx_cur().
///
/// This function is only used by icmp_tx().
///
/// @param      w     Warpcore engine.
/// @param[in]  p     The IP protocol to use for the reflected packet.
/// @param      buf   The current receive buffer.
/// @param[in]  len   The length of the IPv4 payload data in the buffer.
///
void ip_tx_with_rx_buf(struct warpcore * const w,
                       const uint8_t p,
                       void * const buf,
                       const uint16_t len)
{
    struct ip_hdr * const ip = eth_data(buf);

    // TODO: we should zero out any IP options here,
    // since we're reflecting a received packet
    assert(ip_hl(ip) <= sizeof(struct ip_hdr),
           "original packet seems to have IP options");

    // make the original IP src address the new dst, and set the src
    ip->dst = ip->src;
    ip->src = w->ip;

    // set the IP length
    const uint16_t l = sizeof(struct ip_hdr) + len;
    ip->len = htons(l);

    // set other header fields
    ip->p = p;
    ip->id = (uint16_t)random(); // no need to do htons() for random value

    // finally, calculate the IP checksum (over header only)
    ip->cksum = 0;
    ip->cksum = in_cksum(ip, sizeof(struct ip_hdr));

    ip_log(ip);

    // do Ethernet transmit preparation
    eth_tx_rx_cur(w, buf, l);
}


/// Receive processing for an IPv4 packet. Verifies the checksum and dispatches
/// the packet to udp_rx() or icmp_rx(), as appropriate.
///
/// IPv4 options are currently unsupported; as are IPv4 fragments.
///
/// @param      w     Warpcore engine.
/// @param      buf   Buffer containing an Ethernet frame.
///
void ip_rx(struct warpcore * const w, void * const buf)
{
    const struct ip_hdr * const ip = eth_data(buf);
    ip_log(ip);

    // make sure the packet is for us (or broadcast)
    if (unlikely(ip->dst != w->ip && ip->dst != mk_bcast(w->ip, w->mask) &&
                 ip->dst != IP_BCAST)) {
#ifndef NDEBUG
        char src[INET_ADDRSTRLEN];
        char dst[INET_ADDRSTRLEN];
        warn(warn, "IP packet from %s to %s (not us); ignoring",
             inet_ntop(AF_INET, &ip->src, src, INET_ADDRSTRLEN),
             inet_ntop(AF_INET, &ip->dst, dst, INET_ADDRSTRLEN));
#endif
        return;
    }

    // validate the IP checksum
    if (unlikely(in_cksum(ip, sizeof(struct ip_hdr)) != 0)) {
        warn(warn, "invalid IP checksum, received 0x%04x", ntohs(ip->cksum));
        return;
    }

    // TODO: handle IP options
    assert(ip_hl(ip) == 20, "no support for IP options");

    // TODO: handle IP fragments
    assert((ntohs(ip->off) & IP_OFFMASK) == 0, "no support for IP fragments");

    if (likely(ip->p == IP_P_UDP))
        udp_rx(w, buf, ip->src);
    else if (ip->p == IP_P_ICMP)
        icmp_rx(w, buf);
    else {
        warn(warn, "unhandled IP protocol %d", ip->p);
        // be standards compliant and send an ICMP unreachable
        icmp_tx_unreach(w, ICMP_UNREACH_PROTOCOL, buf);
    }
}


// Fill in the IP header information that isn't set as part of the
// socket packet template, calculate the header checksum, and hand off
// to the Ethernet layer.


/// IPv4 transmit processing for the w_iov @p v of length @p len. Fills in the
/// IPv4 header, calculates the checksum, sets the TOS bits and passes the
/// packet to eth_tx().
///
/// @param      w     Warpcore engine.
/// @param      v     The w_iov containing the data to transmit.
/// @param[in]  len   The length of the payload data in @p v.
///
/// @return     Passes on the return value from eth_tx(), which indicates
///             whether @p v was successfully placed into a TX ring.
///
bool ip_tx(struct warpcore * const w,
           struct w_iov * const v,
           const uint16_t len)
{
    struct ip_hdr * const ip = eth_data(IDX2BUF(w, v->idx));
    const uint16_t l = len + sizeof(struct ip_hdr);

    // fill in remaining header fields
    ip->len = htons(l);
    ip->id = (uint16_t)random(); // no need to do htons() for random value
    // IP checksum is over header only
    ip->cksum = in_cksum(ip, sizeof(struct ip_hdr));
    ip->tos = v->flags; // app-specified DSCP + ECN

    ip_log(ip);

    // do Ethernet transmit preparation
    return eth_tx(w, v, l);
}
