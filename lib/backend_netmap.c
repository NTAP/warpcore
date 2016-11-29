#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
// #include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <xmmintrin.h>

#ifdef __linux__
#include <netinet/ether.h>
#else
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <sys/types.h>
#endif

#include "arp.h"
#include "backend.h"
#include "version.h"

/// The backend name.
///
static char backend_name[] = "netmap";


/// Initialize the warpcore netmap backend for engine @p w. This switches the
/// interface to netmap mode, maps the underlying buffers into memory and locks
/// it there, and sets up the extra buffers.
///
/// @param      w       Warpcore engine.
/// @param[in]  ifname  The OS name of the interface (e.g., "eth0").
///
void backend_init(struct warpcore * w, const char * const ifname)
{
    // open /dev/netmap
    assert((w->fd = open("/dev/netmap", O_RDWR)) != -1,
           "cannot open /dev/netmap");

    w->req = calloc(1, sizeof(struct nmreq));
    assert(w->req != 0, "cannot allocate nmreq");

    // switch interface to netmap mode
    strncpy(w->req->nr_name, ifname, sizeof w->req->nr_name);
    w->req->nr_name[sizeof w->req->nr_name - 1] = '\0';
    w->req->nr_version = NETMAP_API;
    w->req->nr_ringid &= ~NETMAP_RING_MASK;
    // don't always transmit on poll
    w->req->nr_ringid |= NETMAP_NO_TX_POLL;
    w->req->nr_flags = NR_REG_ALL_NIC;
    w->req->nr_arg3 = NUM_BUFS; // request extra buffers
    assert(ioctl(w->fd, NIOCREGIF, w->req) != -1,
           "%s: cannot put interface into netmap mode", ifname);

    // mmap the buffer region
    // TODO: see TODO in nm_open() in netmap_user.h
    const int flags = PLAT_MMFLAGS;
    assert((w->mem = mmap(0, w->req->nr_memsize, PROT_WRITE | PROT_READ,
                          MAP_SHARED | flags, w->fd, 0)) != MAP_FAILED,
           "cannot mmap netmap memory");

    // direct pointer to the netmap interface struct for convenience
    w->nif = NETMAP_IF(w->mem, w->req->nr_offset);

#ifndef NDEBUG
    // print some info about our rings
    for (uint32_t ri = 0; ri < w->nif->ni_tx_rings; ri++) {
        const struct netmap_ring * const r = NETMAP_TXRING(w->nif, ri);
        warn(info, "tx ring %d has %d slots (%d-%d)", ri, r->num_slots,
             r->slot[0].buf_idx, r->slot[r->num_slots - 1].buf_idx);
    }
    for (uint32_t ri = 0; ri < w->nif->ni_rx_rings; ri++) {
        const struct netmap_ring * const r = NETMAP_RXRING(w->nif, ri);
        warn(info, "rx ring %d has %d slots (%d-%d)", ri, r->num_slots,
             r->slot[0].buf_idx, r->slot[r->num_slots - 1].buf_idx);
    }
#endif

    // save the indices of the extra buffers in the warpcore structure
    STAILQ_INIT(&w->iov);
    w->bufs = calloc(w->req->nr_arg3, sizeof(struct w_iov));
    assert(w->bufs != 0, "cannot allocate w_iov");
    for (uint32_t n = 0, i = w->nif->ni_bufs_head; n < w->req->nr_arg3; n++) {
        w->bufs[n].buf = IDX2BUF(w, i);
        w->bufs[n].idx = i;
        STAILQ_INSERT_HEAD(&w->iov, &w->bufs[n], next);
        i = *(uint32_t *)w->bufs[n].buf;
    }

    if (w->req->nr_arg3 != NUM_BUFS)
        die("can only allocate %d/%d extra buffers", w->req->nr_arg3, NUM_BUFS);
    else
        warn(notice, "allocated %d extra buffers", w->req->nr_arg3);

    // lock memory
    assert(mlockall(MCL_CURRENT | MCL_FUTURE) != -1, "mlockall");

    w->backend = backend_name;
    SLIST_INIT(&w->arp_cache);
}


/// Shut a warpcore netmap engine down cleanly. This function returns all w_iov
/// structures associated the engine to netmap.
///
/// @param      w     Warpcore engine.
///
void backend_cleanup(struct warpcore * const w)
{
    // free ARP cache
    free_arp_cache(w);

    // re-construct the extra bufs list, so netmap can free the memory
    for (uint32_t n = 0; n < w->req->nr_arg3; n++) {
        uint32_t * const buf = (void *)IDX2BUF(w, w->bufs[n].idx);
        if (n < w->req->nr_arg3 - 1)
            *buf = w->bufs[n + 1].idx;
        else
            *buf = 0;
    }

    assert(munmap(w->mem, w->req->nr_memsize) != -1,
           "cannot munmap netmap memory");

    assert(close(w->fd) != -1, "cannot close /dev/netmap");
    free(w->req);
}


/// Netmap-specific code to bind a warpcore socket. Does nothing currently.
///
/// @param      s     The w_sock to bind.
///
void backend_bind(struct w_sock * s __attribute__((unused)))
{
}


/// Connect the given w_sock, using the netmap backend. If the Ethernet MAC
/// address of the destination (or the default router towards it) is not known,
/// it will block trying to look it up via ARP.
///
/// @param      s     w_sock to connect.
///
void backend_connect(struct w_sock * const s)
{
    // find the Ethernet MAC address of the destination or the default router,
    // and update the template header
    const uint32_t ip = s->w->rip && (mk_net(s->hdr.ip.dst, s->w->mask) !=
                                      mk_net(s->hdr.ip.src, s->w->mask))
                            ? s->w->rip
                            : s->hdr.ip.dst;
    const uint8_t * const mac = arp_who_has(s->w, ip);
    memcpy(s->hdr.eth.dst, mac, ETH_ADDR_LEN);
}


/// Return the file descriptor associated with a w_sock. For the netmap backend,
/// this is the per-interface netmap file descriptor. It can be used for poll()
/// or with event-loop libraries in the application.
///
/// @param      s     w_sock socket for which to get the underlying descriptor.
///
/// @return     A file descriptor.
///
int w_fd(struct w_sock * const s)
{
    return s->w->fd;
}


/// Iterates over any new data in the RX rings, appending them to the w_sock::iv
/// socket buffers of the respective w_sock structures associated with a given
/// sender IPv4 address and port. Also handles pending inbound ARP and ICMP
/// packets.
///
/// @param      w     Warpcore engine.
///
void backend_rx(struct warpcore * const w)
{
    // loop over all rx rings starting with cur_rxr and wrapping around
    for (uint32_t i = 0; likely(i < w->nif->ni_rx_rings); i++) {
        struct netmap_ring * const r = NETMAP_RXRING(w->nif, w->cur_rxr);
        while (!nm_ring_empty(r)) {
            // prefetch the next slot into the cache
            _mm_prefetch(
                NETMAP_BUF(r, r->slot[nm_ring_next(r, r->cur)].buf_idx),
                _MM_HINT_T1);

            // process the current slot
            eth_rx(w, NETMAP_BUF(r, r->slot[r->cur].buf_idx));
            r->head = r->cur = nm_ring_next(r, r->cur);
        }
        w->cur_rxr = (w->cur_rxr + 1) % w->nif->ni_rx_rings;
    }
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
/// @return     First w_iov in w_sock::iv if there is new data, or zero. Needs
///             to be freed with w_free() by the caller.
///
struct w_iov * w_rx(struct w_sock * const s)
{
    backend_rx(s->w);
    struct w_iov * const v = STAILQ_FIRST(&s->iv);
    STAILQ_INIT(&s->iv);
    return v;
}


/// Places payloads from @p v into IPv4 UDP packets, and attempts to move them
/// onto a TX ring. Not all payloads may be placed if the TX rings fills up
/// first. Also, the packets are not send yet; w_nic_tx() needs to be called for
/// that. This is, so that an application has control over exactly when to
/// schedule packet I/O.
///
/// @param      s     w_sock socket to transmit over.
/// @param      v     w_iov chain to transmit.
///
void w_tx(const struct w_sock * const s, struct w_iov * const v)
{
    udp_tx(s, v);
}


/// Trigger netmap to make new received data available to w_rx().
///
/// @param[in]  w     Warpcore engine.
///
void w_nic_rx(const struct warpcore * const w)
{
    assert(ioctl(w->fd, NIOCRXSYNC, 0) != -1, "cannot kick rx ring");
}


/// Push data placed in the TX rings via udp_tx() and similar methods out onto
/// the link.{ function_description }
///
/// @param[in]  w     Warpcore engine.
///
void w_nic_tx(const struct warpcore * const w)
{
    assert(ioctl(w->fd, NIOCTXSYNC, 0) != -1, "cannot kick tx ring");
}
