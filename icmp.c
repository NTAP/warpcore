#include "debug.h"
#include "icmp.h"
#include "ip.h"


void icmp_tx(const struct nm_desc * const nm, const char * const buf, const uint16_t offset, const uint16_t len) {
	struct icmp_hdr * const icmp = (struct icmp_hdr * const)(buf + offset);

	D("ICMP type %d, code %d", icmp->type, icmp->code);

	// calculate the new ICMP checksum
	icmp->cksum = 0;
	icmp->cksum = in_cksum(icmp, len);

	// do IP transmit preparation
	ip_tx(nm, buf);
}


void icmp_rx(const struct nm_desc * const nm, const char * const buf, const uint16_t offset, const uint16_t len) {
	struct icmp_hdr * const icmp = (struct icmp_hdr * const)(buf + offset);

	D("ICMP type %d, code %d", icmp->type, icmp->code);

	// TODO: validate inbound ICMP checksum

	switch (icmp->type) {
		case ICMP_TYPE_ECHO:
			// transform the received echo into an echo reply and send it
			icmp->type = ICMP_TYPE_ECHOREPLY;
			icmp_tx(nm, buf, offset, len);
			break;
		default:
			D("unhandled ICMP type %d", icmp->type);
			abort();
	}
}
