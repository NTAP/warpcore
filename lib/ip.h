#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define IP_P_ICMP 1 // IP protocol number for ICMP
#define IP_P_UDP 17 // IP protocol number for UDP

#define IP_ECT1 1
#define IP_ECT0 2
#define IP_CE 3

#define IP_RF 0x8000      // reserved fragment flag
#define IP_DF 0x4000      // don't fragment flag
#define IP_MF 0x2000      // more fragments flag
#define IP_OFFMASK 0x1fff // mask for fragmenting bits

#define IP_ADDR_LEN 4     // IPv4 addresses are four bytes
#define IP_ADDR_STRLEN 16 // xxx.xxx.xxx.xxx\0

struct ip_hdr {
    uint8_t vhl;       // header length + version
    uint8_t tos;       // DSCP + ECN
    uint16_t len;      // total length
    uint16_t id;       // identification
    uint16_t off;      // flags & fragment offset field
    uint8_t ttl;       // time to live
    uint8_t p;         // protocol
    uint16_t cksum;    // checksum
    uint32_t src, dst; // source and dest address
};


struct warpcore;
struct w_iov;

#define ip_v(ip) ((((const struct ip_hdr * const)(ip))->vhl & 0xf0) >> 4)
#define ip_hl(ip) ((((const struct ip_hdr * const)(ip))->vhl & 0x0f) * 4)
#define ip_dscp(ip) ((((const struct ip_hdr * const)(ip))->tos & 0xfc) >> 2)
#define ip_ecn(ip) (((const struct ip_hdr * const)(ip))->tos & 0x02)

#define ip_data(buf) (void *)(((char *)eth_data(buf) + sizeof(struct eth_hdr)))
//+ ip_hl((buf)

#define ip_data_len(ip) ((ntohs((ip)->len) - ip_hl(ip)))

// see ip.c for documentation of functions
extern void ip_tx_with_rx_buf(struct warpcore * w,
                              const uint8_t p,
                              void * const buf,
                              const uint16_t len);

extern const char * ip_ntoa(uint32_t ip, void * const buf, const size_t size);

extern uint32_t ip_aton(const char * const ip);

extern void ip_rx(struct warpcore * const w, void * const buf);

extern bool
ip_tx(struct warpcore * w, struct w_iov * const v, const uint16_t len);

// these are defined in in_chksum.c, which is the FreeBSD checksum code
extern uint16_t in_cksum(const void * const buf, const uint16_t len);

extern uint16_t in_pseudo(uint32_t sum, uint32_t b, uint32_t c);
