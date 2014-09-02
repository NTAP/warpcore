#include "debug.h"
#include "icmp.h"


void icmp_receive(const char * const buf) {
	const struct icmp_hdr * const icmp = (const struct icmp_hdr * const)buf;
	D("ICMP type %d, code %d", icmp->type, icmp->code);

	switch (icmp->type) {
		default:
			D("unhandled ICMP type %d", icmp->type);
	}
}

