#include "debug.h"
#include "icmp.h"
#include "ip.h"


void icmp_tx(const struct nm_desc * const nm, struct netmap_ring *ring, const uint16_t offset, const uint16_t len) {
	struct icmp_hdr * const icmp =
		(struct icmp_hdr * const)(NETMAP_BUF(ring, ring->slot[ring->cur].buf_idx) + offset);

	D("ICMP type %d, code %d", icmp->type, icmp->code);
	icmp->cksum = 0;
	icmp->cksum = in_cksum(icmp, len);
	ip_tx(nm, ring);
}


void icmp_rx(const struct nm_desc * const nm, struct netmap_ring *ring, const uint16_t offset, const uint16_t len) {
	struct icmp_hdr * const icmp =
		(struct icmp_hdr * const)(NETMAP_BUF(ring, ring->slot[ring->cur].buf_idx) + offset);

	D("ICMP type %d, code %d", icmp->type, icmp->code);
	switch (icmp->type) {
		case ICMP_TYPE_ECHO:
			// transform the received echo into an echo reply and send it
			icmp->type = ICMP_TYPE_ECHOREPLY;
			icmp_tx(nm, ring, offset, len);
			break;
		default:
			D("unhandled ICMP type %d", icmp->type);
	}
}
