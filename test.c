#include "warpcore.h"
#include "ip.h"
#include "debug.h"


int main(void)
{
	struct warpcore *w = w_init("em1", false);

	// warpcore runs in its own thread spawned by w_init()
	D("main process ready");

	struct w_socket *s = w_bind(w, IP_P_UDP, 53);
	sleep(5);
	// while (1) {
		// run the receive loop
		w_poll(w);
		// read any data
		struct w_iov *i = w_rx(s);
		// access the read data
		while (i) {
			D("%d bytes in buf %p", i->len, i->buf);
			hexdump(i->buf, i->len);
			i = STAILQ_NEXT(i, vecs);
		}
		// read is done, release the iov
		w_rx_done(s);
	// }
	w_close(s);

	D("main process exiting");
	w_cleanup(w);

	return 0;
}
