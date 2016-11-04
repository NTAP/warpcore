#ifndef _warpcore_h_
#define _warpcore_h_

#include <net/netmap_user.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/queue.h>
#include <xmmintrin.h>


#ifndef __aligned
#define __aligned(x) __attribute__((__aligned__(x)))
#endif

#ifndef __packed
#define __packed __attribute__((__packed__))
#endif

#ifndef __unused
#define __unused __attribute__((__unused__))
#endif

#include "arp.h"
#include "debug.h"
#include "eth.h"
#include "icmp.h"
#include "ip.h"
#include "udp.h"

// according to Luigi, any ring can be passed to NETMAP_BUF
#define IDX2BUF(w, i) NETMAP_BUF(NETMAP_TXRING(w->nif, 0), i)


struct w_iov {
    char * buf;               // start of user data (inside buffer)
    STAILQ_ENTRY(w_iov) next; // next iov
    uint32_t idx;             // index of netmap buffer
    uint16_t len;             // length of user data (inside buffer)
    uint16_t sport;           // sender port (only valid on rx)
    uint32_t src;             // sender IP address (only valid on rx)
    uint32_t _unused1;
    struct timeval ts;
} __aligned(4);


struct w_sock {
    struct warpcore * w;        // warpcore instance
    STAILQ_HEAD(ivh, w_iov) iv; // iov for read data
    STAILQ_HEAD(ovh, w_iov) ov; // iov for data to write
    char * hdr;                 // header template
    uint16_t hdr_len;           // length of template
    uint8_t dmac[ETH_ADDR_LEN]; // dst Eth address
    uint32_t dip;               // dst IP address
    uint16_t sport;             // src port
    uint16_t dport;             // dst port
    SLIST_ENTRY(w_sock) next;   // next socket
    uint8_t p;                  // protocol

    uint8_t _unused1;
    uint16_t _unused2;
    uint32_t _unused3;

} __aligned(4);


struct warpcore {
    struct netmap_if * nif;       // netmap interface
    void * mem;                   // netmap memory
    struct w_sock ** udp;         // UDP "sockets"
    uint32_t cur_txr;             // our current tx ring
    uint32_t cur_rxr;             // our current rx ring
    STAILQ_HEAD(iovh, w_iov) iov; // our available bufs
    uint32_t ip;                  // our IP address
    uint32_t bcast;               // our broadcast address
    uint8_t mac[ETH_ADDR_LEN];    // our Ethernet address
    bool interrupt;               // termination flag
    uint8_t _unused1;

    // mtu could be pushed into the second cacheline
    uint16_t mtu; // our MTU
    uint16_t _unused2;

    // --- cacheline 1 boundary (64 bytes) ---
    uint32_t mbps;               // our link speed
    SLIST_HEAD(sh, w_sock) sock; // our open sockets
    uint32_t mask;               // our IP netmask
    int fd;                      // netmap descriptor
    struct nmreq req;            // netmap request
    uint32_t _unused3;
} __aligned(4);


#define w_get_sock(w, p, port) ((p) == IP_P_UDP ? &(w)->udp[port] : 0)


// see warpcore.c for documentation of functions
extern struct warpcore * w_init(const char * const ifname);

extern void w_init_common(void);

extern void w_cleanup(struct warpcore * const w);

extern struct w_sock *
w_bind(struct warpcore * const w, const uint8_t p, const uint16_t port);

extern void
w_connect(struct w_sock * const s, const uint32_t ip, const uint16_t port);

extern void w_close(struct w_sock * const s);

extern struct w_iov * w_tx_alloc(struct w_sock * const s, const uint32_t len);

extern void
w_poll(const struct warpcore * const w, const short ev, const int to);

extern void w_rx_done(struct w_sock * const s);

extern struct w_iov * w_rx(struct w_sock * const s);

extern void w_kick_tx(const struct warpcore * const w);

extern void w_kick_rx(const struct warpcore * const w);

extern void w_tx(struct w_sock * const s);

#endif
