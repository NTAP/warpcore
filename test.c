#include <poll.h> // poll

#include "eth.h"
#include "debug.h"


void nm_dump(const struct nmreq * const req) {
	D("nr_name = %s", req->nr_name);
	D("nr_version = %d", req->nr_version);
	D("nr_offset = %d", req->nr_offset);
	D("nr_memsize = %d", req->nr_memsize);
	D("nr_tx_slots = %d", req->nr_tx_slots);
	D("nr_rx_slots = %d", req->nr_rx_slots);
	D("nr_tx_rings = %d", req->nr_tx_rings);
	D("nr_rx_rings = %d", req->nr_rx_rings);
	D("nr_ringid = %d", req->nr_ringid);
	D("nr_cmd = %d", req->nr_cmd);
	D("nr_arg1 = %d", req->nr_arg1);
	D("nr_arg2 = %d", req->nr_arg2);
	D("nr_arg3 = %d", req->nr_arg3);
	D("nr_flags = %d", req->nr_flags);
	D("spare2 = %d", req->spare2[1]);
}

//int main(int argc, char const *argv[]) {
int main(void) {
	struct nm_desc *nm = nm_open("netmap:em1", NULL, 0, 0);
	struct pollfd fds = { .fd = NETMAP_FD(nm), .events = POLLIN };

	nm_dump(&nm->req);
	for (;;) {
		int n = poll(&fds, 1, INFTIM);
		switch (n) {
			case -1:
				D("poll: %s", strerror(errno));
				abort();
				break;
			case 0:
				D("poll: timeout expired");
				break;
			default:
				D("poll: %d descriptors ready", n);
				break;
		}

		struct netmap_ring *ring = NETMAP_RXRING(nm->nifp, nm->cur_rx_ring);
		while (!nm_ring_empty(ring)) {
			const u_int i = ring->cur;
			eth_rx(nm, ring);
			ring->head = ring->cur = nm_ring_next(ring, i);
		}
	}
	nm_close(nm);
}

