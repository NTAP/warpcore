#include "debug.h"
#include "ip.h"
#include "icmp.h"
#include "eth.h"


void ip_tx(const struct nm_desc * const nm, struct netmap_ring *ring) {
	struct ip_hdr * const ip =
		(struct ip_hdr * const)(NETMAP_BUF(ring, ring->slot[ring->cur].buf_idx) + sizeof(struct eth_hdr));
	struct in_addr tmp = ip->src;
	ip->src = ip->dst;
	ip->dst = tmp;
	ip->cksum = 0;
	ip->cksum = in_cksum(ip, ntohs(ip->len));

#ifdef D
	char src[256];
	char dst[256];
	D("IP %s -> %s, proto %d, ttl %d, hlen/tot %d/%d",
		inet_ntoa_r(ip->src, src, sizeof src),
		inet_ntoa_r(ip->dst, dst, sizeof dst),
		ip->p, ip->ttl, ip->hl * 4, ntohs(ip->len) - ip->hl * 4);
#endif

	eth_tx(nm, ring);
}


void ip_rx(const struct nm_desc * const nm, struct netmap_ring *ring, const uint16_t offset) {
	const struct ip_hdr * const ip =
		(const struct ip_hdr * const)(NETMAP_BUF(ring, ring->slot[ring->cur].buf_idx) + offset);

#ifdef D
	char src[256];
	char dst[256];
	D("IP %s -> %s, proto %d, ttl %d, hlen/tot %d/%d",
		inet_ntoa_r(ip->src, src, sizeof src),
		inet_ntoa_r(ip->dst, dst, sizeof dst),
		ip->p, ip->ttl, ip->hl * 4, ntohs(ip->len) - ip->hl * 4);
#endif

	switch (ip->p) {
		case IP_P_ICMP:
			icmp_rx(nm, ring, offset + ip->hl * 4, ntohs(ip->len) - ip->hl * 4);
			break;
		case IP_P_TCP:
			break;
		default:
			D("unhandled IP protocol %d", ip->p);
	}
}
