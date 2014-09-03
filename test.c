#include <poll.h> // poll
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "warpcore.h"
#include "eth.h"


//int main(int argc, char const *argv[]) {
int main(void) {
	struct warpcore *w = w_open("em1");
	struct pollfd fds = { .fd = w->fd, .events = POLLIN };

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
				// D("poll: %d descriptors ready", n);
				break;
		}

		struct netmap_ring *ring = NETMAP_RXRING(w->nif, 0);
		while (!nm_ring_empty(ring)) {
			const char * const buf = NETMAP_BUF(ring, ring->slot[ring->cur].buf_idx);
			eth_rx(w, buf);
			ring->head = ring->cur = nm_ring_next(ring, ring->cur);
		}
	}
	// w_close(w);
}
