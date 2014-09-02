#include "debug.h"
#include "ip.h"
#include "icmp.h"


void ip_receive(const char * const buf) {
	const struct ip_hdr * const ip = (const struct ip_hdr * const)buf;
	char src[256];
	char dst[256];
	D("IP %s -> %s, protocol %d",
		inet_ntoa_r(ip->src, src, sizeof src),
		inet_ntoa_r(ip->dst, dst, sizeof dst),
		ip->p);

	switch (ip->p) {
		case IP_P_ICMP:
			icmp_receive(buf + ip->hl * 4);
			break;
		case IP_P_TCP:
			break;
		default:
			D("unhandled IP protocol %d", ip->p);
	}
}
