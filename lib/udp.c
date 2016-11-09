#include <arpa/inet.h>
#include <string.h>

#include "icmp.h"
#include "ip.h"
#include "udp.h"
#include "warpcore.h"


// Log a UDP segment
#ifndef NDEBUG
#define udp_log(udp)                                                           \
    do {                                                                       \
        warn(info, "UDP :%d -> :%d, cksum 0x%04x, len %u", ntohs(udp->sport),  \
             ntohs(udp->dport), ntohs(udp->cksum), ntohs(udp->len));           \
    } while (0)
#else
#define udp_log(udp)                                                           \
    do {                                                                       \
    } while (0)
#endif

// Receive a UDP packet.
void udp_rx(struct warpcore * const w, void * const buf, const uint32_t src)
{
    const struct ip_hdr * const ip = eth_data(buf);
    struct udp_hdr * const udp = ip_data(buf);
    const uint16_t len = ntohs(udp->len);

    udp_log(udp);

    // validate the checksum
    const uint16_t orig = udp->cksum;
    udp->cksum = in_pseudo(ip->src, ip->dst, htons(len + ip->p));
    const uint16_t cksum = in_cksum(udp, len);
    udp->cksum = orig;
    if (unlikely(orig != cksum)) {
        warn(warn, "invalid UDP checksum, received 0x%04x != 0x%04x",
             ntohs(orig), ntohs(cksum));
        return;
    }

    struct w_sock ** s = w_get_sock(w, IP_P_UDP, udp->dport);
    if (unlikely(*s == 0)) {
        // nobody bound to this port locally
        // send an ICMP unreachable
        icmp_tx_unreach(w, ICMP_UNREACH_PORT, buf);
        return;
    }

    // grab an unused iov for the data in this packet
    struct w_iov * const i = STAILQ_FIRST(&w->iov);
    assert(i != 0, "out of spare bufs");
    struct netmap_ring * const rxr = NETMAP_RXRING(w->nif, w->cur_rxr);
    struct netmap_slot * const rxs = &rxr->slot[rxr->cur];
    STAILQ_REMOVE_HEAD(&w->iov, next);

    warn(debug, "swapping rx ring %u slot %d (buf %d) and spare buf %u",
         w->cur_rxr, rxr->cur, rxs->buf_idx, i->idx);

    // remember index of this buffer
    const uint32_t tmp_idx = i->idx;

    // move the received data into the iov
    i->buf = (char *)ip_data(buf) + sizeof(struct udp_hdr);
    i->len = len - sizeof(struct udp_hdr);
    i->idx = rxs->buf_idx;

    // tag the iov with the sender's information
    i->src = src;
    i->sport = udp->sport;

    // append the iov to the socket
    STAILQ_INSERT_TAIL(&(*s)->iv, i, next);

    // put the original buffer of the iov into the receive ring
    rxs->buf_idx = tmp_idx;
    rxs->flags = NS_BUF_CHANGED;
}


// Put the socket template header in front of the data in the iov and send.
void udp_tx(struct w_sock * const s)
{
#ifndef NDEBUG
    uint32_t n = 0, l = 0;
#endif
    // packetize bufs and place in tx ring
    while (likely(!STAILQ_EMPTY(&s->ov))) {
        struct w_iov * const v = STAILQ_FIRST(&s->ov);

        // copy template header into buffer and fill in remaining fields
        void * const buf = IDX2BUF(s->w, v->idx);
        memcpy(buf, &s->hdr, sizeof(s->hdr));

        struct udp_hdr * const udp = ip_data(buf);
        const uint16_t len = v->len + sizeof(struct udp_hdr);
        udp->len = htons(len);

        // compute the checksum
        udp->cksum = in_pseudo(s->w->ip, s->hdr.ip.dst, htons(len + IP_P_UDP));
        udp->cksum = in_cksum(udp, len);

        udp_log(udp);

        // do IP transmit preparation
        if (ip_tx(s->w, v, len)) {
#ifndef NDEBUG
            n++;
            l += v->len;
#endif
            STAILQ_REMOVE_HEAD(&s->ov, next);
            STAILQ_INSERT_HEAD(&s->w->iov, v, next);
        } else {
            // no space in rings
            w_kick_tx(s->w);
            warn(warn, "polling for send space");
            w_poll(s->w, POLLOUT, -1);
        }
    }
    warn(info, "proto %d tx iov (len %d in %d bufs) done", s->hdr.ip.p, l, n);

    // kick tx ring
    w_kick_tx(s->w);
}
