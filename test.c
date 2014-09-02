#include <poll.h> // poll

#include "eth.h"
#include "debug.h"


void nm_dump(const struct nm_desc * const nm) {
	D("nr_name = %s", nm->req.nr_name);
	D("nr_version = %d", nm->req.nr_version);
	D("nr_offset = %d", nm->req.nr_offset);
	D("nr_memsize = %d", nm->req.nr_memsize);
	D("nr_tx_slots = %d", nm->req.nr_tx_slots);
	D("nr_rx_slots = %d", nm->req.nr_rx_slots);
	D("nr_tx_rings = %d", nm->req.nr_tx_rings);
	D("nr_rx_rings = %d", nm->req.nr_rx_rings);
	D("nr_ringid = %d", nm->req.nr_ringid);
	D("nr_cmd = %d", nm->req.nr_cmd);
	D("nr_arg1 = %d", nm->req.nr_arg1);
	D("nr_arg2 = %d", nm->req.nr_arg2);
	D("nr_arg3 = %d", nm->req.nr_arg3);
	D("nr_flags = %d", nm->req.nr_flags);
	D("spare2 = %d", nm->req.spare2[1]);

	for (uint16_t r = nm->first_tx_ring; r <= nm->last_tx_ring; r++) {
		D("tx ring %d", r);
		struct netmap_ring *ring = NETMAP_TXRING(nm->nifp, r);
		for (uint16_t s = 0; s < ring->num_slots; s++) {
			fprintf(stderr, "s%d:i%d ", s, ring->slot[s].buf_idx);
		}
		fprintf(stderr, "\n");
	}

	for (uint16_t r = nm->first_rx_ring; r <= nm->last_rx_ring; r++) {
		D("rx ring %d", r);
		struct netmap_ring *ring = NETMAP_RXRING(nm->nifp, r);
		for (uint16_t s = 0; s < ring->num_slots; s++) {
			fprintf(stderr, "s%d:i%d ", s, ring->slot[s].buf_idx);
		}
		fprintf(stderr, "\n");
	}
}

//int main(int argc, char const *argv[]) {
int main(void) {
	struct nm_desc *nm = nm_open("netmap:em1", NULL, 0, 0);
	struct pollfd fds = { .fd = NETMAP_FD(nm), .events = POLLIN };

	// nm_dump(nm);
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
			const char * const buf = NETMAP_BUF(ring, ring->slot[ring->cur].buf_idx);
			eth_rx(nm, buf);
			ring->head = ring->cur = nm_ring_next(ring, ring->cur);
		}
	}
	nm_close(nm);
}
