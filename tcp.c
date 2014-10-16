#include <arpa/inet.h>

#include "warpcore.h"
#include "tcp.h"


void
tcp_rx(struct warpcore * const w, char * const buf, const uint16_t off,
       const uint16_t len, const uint32_t src)
{
	const struct tcp_hdr * const tcp =
		(const struct tcp_hdr * const)(buf + off);

	dlog(info, "TCP :%d -> :%d, flags %d, len %ld", ntohs(tcp->sport),
	     ntohs(tcp->dport), tcp->flags, len - sizeof(struct tcp_hdr));
}


void
tcp_tx(struct w_sock * const s)
{

}
