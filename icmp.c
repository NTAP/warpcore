#include "warpcore.h"
#include "icmp.h"
#include "ip.h"


void icmp_tx_unreach(const struct warpcore * const w, const uint_fast8_t code,
	char * const buf, const uint_fast16_t off) {
	// make an ICMP unreachable out of this received packet
	// copy IP hdr + 64 bytes of the original IP packet as the ICMP payload
	struct ip_hdr * const ip =
		(struct ip_hdr * const)(buf + sizeof(struct eth_hdr));
	const uint_fast16_t len = ip->hl * 4 + 64;
	memmove(buf + off + sizeof(struct icmp_hdr) + 4, ip, len);

	// insert an ICMP header
	struct icmp_hdr * const icmp = (struct icmp_hdr * const)(buf + off);
	icmp->type = ICMP_TYPE_UNREACH;
	icmp->code = code;

	// TODO: implement RFC4884 instead of setting the padding to zero
	uint32_t *pad = (uint32_t *)(buf + off + sizeof(struct icmp_hdr));
	*pad = 0;

	icmp_tx(w, buf, off, sizeof(struct icmp_hdr) + 4 + len); // calculates cksum
}


void icmp_tx(const struct warpcore * const w, const char * const buf,
	const uint_fast16_t off, const uint_fast16_t len) {
	struct icmp_hdr * const icmp = (struct icmp_hdr * const)(buf + off);

	D("ICMP type %d, code %d", icmp->type, icmp->code);

	// calculate the new ICMP checksum
	icmp->cksum = 0;
	icmp->cksum = in_cksum(icmp, len);

	// do IP transmit preparation
	ip_tx(w, IP_P_ICMP, buf, len);
}


void icmp_rx(const struct warpcore * const w, char * const buf,
	const uint_fast16_t off, const uint_fast16_t len) {
	struct icmp_hdr * const icmp = (struct icmp_hdr * const)(buf + off);

	D("ICMP type %d, code %d", icmp->type, icmp->code);

	// TODO: validate inbound ICMP checksum

	switch (icmp->type) {
		case ICMP_TYPE_ECHO:
			// transform the received echo into an echo reply and send it
			icmp->type = ICMP_TYPE_ECHOREPLY;
			icmp_tx(w, buf, off, len);
			break;
		default:
			D("unhandled ICMP type %d", icmp->type);
			abort();
	}
}
