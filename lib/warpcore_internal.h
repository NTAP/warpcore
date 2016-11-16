#pragma once

#ifdef WITH_NETMAP
#include <net/netmap_user.h>
#endif
#include <sys/queue.h>

#include "eth.h"
#include "ip.h"
#include "udp.h"
#include "warpcore.h"


/// For a given netmap buffer index, get a pointer to its beginning.
///
/// @param      w     Warpcore engine.
/// @param      i     Buffer index.
///
/// @return     Memory region associated with buffer @p i.
///
#define IDX2BUF(w, i) NETMAP_BUF(NETMAP_TXRING((w)->nif, 0), (i))


/// A warpcore socket.
///
struct w_sock {
    /// Pointer back to the warpcore instance associated with this w_sock.
    ///
    struct warpcore * w;
    STAILQ_HEAD(ivh, w_iov) iv; ///< w_iov containing incoming unread data.
    STAILQ_HEAD(ovh, w_iov) ov; ///< w_iov containing outbound unsent data.
    SLIST_ENTRY(w_sock) next;   ///< Next socket associated with this engine.
    /// The template header to be used for outbound packets on this
    /// w_sock.
    struct {
        struct eth_hdr eth;
        struct ip_hdr ip __attribute__((packed));
        struct udp_hdr udp;
    } hdr;
#ifndef WITH_NETMAP
    uint8_t _unused[2];
    int fd; ///< Socket descriptor underlying the engine, if the shim is in use.
#else
    uint8_t _unused[6];
#endif
};


/// A warpcore engine.
///
struct warpcore {
    struct netmap_if * nif;       ///< Netmap interface.
    struct w_sock ** udp;         ///< Array 64K pointers to w_sock sockets.
    SLIST_HEAD(sh, w_sock) sock;  ///< List of open (bound) w_sock sockets.
    uint32_t cur_txr;             ///< Index of the TX ring currently active.
    uint32_t cur_rxr;             ///< Index of the RX ring currently active.
    STAILQ_HEAD(iovh, w_iov) iov; ///< List of w_iov buffers available.
    uint32_t ip;   ///< Local IPv4 address used on this interface.
    uint32_t mask; ///< IPv4 netmask of this interface.
    uint16_t mtu;  ///< MTU of this interface.
    uint8_t
        mac[ETH_ADDR_LEN]; ///< Local Ethernet MAC address of this interface.
    void * mem;            ///< Pointer to netmap memory region.
    uint32_t rip;          ///< IPv4 our default router IP address
#ifdef WITH_NETMAP
    struct nmreq req; ///< Netmap request structure.
    int fd;           ///< Netmap file descriptor.
#endif
    uint8_t unused[4];
    SLIST_ENTRY(warpcore) next; ///< Pointer to next engine.
};


/// Compute the IPv4 broadcast address for the given IPv4 address and netmask.
///
/// @param      ip    The IPv4 address to compute the broadcast address for.
/// @param      mask  The netmask associated with @p ip.
///
/// @return     The IPv4 broadcast address associated with @p ip and @p mask.
///
#define mk_bcast(ip, mask) ((ip) | (~mask))


/// The IPv4 network prefix for the given IPv4 address and netmask.
///
/// @param      ip    The IPv4 address to compute the prefix for.
/// @param      mask  The netmask associated with @p ip.
///
/// @return     The IPv4 prefix associated with @p ip and @p mask.
///
#define mk_net(ip, mask) ((ip) & (mask))


/// Get the w_sock associated with protocol @p p and local port @p port. @p p
/// must be #IP_P_UDP at the moment.
///
/// @param      w     Warpcore engine.
/// @param      p     IP protocol number. Must be #IP_P_UDP.
/// @param      port  The local port to look up the w_sock for.
///
/// @return     Pointer to the w_sock, if it exists, or zero.
///
#define get_sock(w, p, port) ((p) == IP_P_UDP ? &(w)->udp[port] : 0)
