#include <arpa/inet.h>

#include "icmp.h"
#include "util.h"
#include "warpcore_internal.h"


// Send the modified ICMP packet in the current receive buffer.
static void __attribute__((nonnull)) icmp_tx(struct warpcore * restrict const w,
                                             void * restrict const buf,
                                             const uint16_t len)
{
    struct icmp_hdr * restrict const icmp = ip_data(buf);
    warn(notice, "ICMP type %d, code %d", icmp->type, icmp->code);

    // calculate the new ICMP checksum
    icmp->cksum = 0;
    icmp->cksum = in_cksum(icmp, len);

    // do IP transmit preparation
    ip_tx_with_rx_buf(w, IP_P_ICMP, buf, len);
    w_kick_tx(w);
}


// Make an ICMP unreachable message with the given code out of the
// current received packet.
void __attribute__((nonnull))
icmp_tx_unreach(struct warpcore * restrict const w,
                const uint8_t code,
                void * restrict const buf)
{
    // copy IP hdr + 64 bytes of the original IP packet as the ICMP payload
    struct ip_hdr * restrict const ip = eth_data(buf);
    const uint16_t len = ip_hl(ip) + 64;
    // use memmove (instead of memcpy), since the regions overlap
    struct ip_hdr * restrict const payload =
        (void *)((char *)ip_data(buf) + sizeof(struct icmp_hdr));
    memmove((char *)payload + 4, ip, len);

    // insert an ICMP header and set the fields
    struct icmp_hdr * restrict const icmp = ip_data(buf);
    icmp->type = ICMP_TYPE_UNREACH;
    icmp->code = code;

    // TODO: implement RFC4884 instead of setting the padding to zero
    uint32_t * restrict const p = (uint32_t *)(payload);
    *p = 0;

    icmp_tx(w, buf, sizeof(struct icmp_hdr) + 4 + len); // does cksum
}


// Handle an incoming ICMP packet, and optionally respond to it.
void __attribute__((nonnull))
icmp_rx(struct warpcore * restrict const w, void * restrict const buf)
{
    struct icmp_hdr * restrict const icmp = ip_data(buf);
    warn(notice, "ICMP type %d, code %d", icmp->type, icmp->code);

    // validate the ICMP checksum
    const uint16_t len = ip_data_len((struct ip_hdr *)eth_data(buf));
    if (in_cksum(icmp, len) != 0) {
        warn(warn, "invalid ICMP checksum, received 0x%04x",
             ntohs(icmp->cksum));
        return;
    }

    switch (icmp->type) {
    case ICMP_TYPE_ECHO:
        // transform the received echo into an echo reply and send it
        icmp->type = ICMP_TYPE_ECHOREPLY;
        icmp_tx(w, buf, len);
        break;
    case ICMP_TYPE_UNREACH: {
#ifndef NDEBUG
        struct ip_hdr * restrict const ip =
            (void *)((char *)ip_data(buf) + sizeof(struct icmp_hdr) + 4);
#endif
        switch (icmp->code) {
        case ICMP_UNREACH_PROTOCOL:
            warn(warn, "ICMP protocol %d unreachable", ip->p);
            break;
        case ICMP_UNREACH_PORT: {
#ifndef NDEBUG
            struct udp_hdr * restrict const udp =
                (void *)((char *)ip + ip_hl(ip));
            warn(warn, "ICMP IP proto %d port %d unreachable", ip->p,
                 ntohs(udp->dport));
#endif
            break;
        }
        default:
            die("unhandled ICMP code %d", icmp->code);
            break;
        }
        break;
    }
    default:
        die("unhandled ICMP type %d", icmp->type);
        break;
    }
}
