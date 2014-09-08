#include "warpcore.h"
#include "ip.h"


void iov_fill(struct w_iov *v)
{
	while (v) {
		log("%d bytes in buf %p", v->len, v->buf);
		for (uint_fast16_t l = 0; l < v->len; l++) {
			v->buf[l] = l % 0xff;
		}
		v = SLIST_NEXT(v, vecs);
	}
}


void iov_dump(struct w_iov *v)
{
	while (v) {
		log("%d bytes in buf %p", v->len, v->buf);
		hexdump(v->buf, v->len);
		v = SLIST_NEXT(v, vecs);
	}
}


int main(void)
{
#ifdef __linux__
	const char *i = "eth1";
#else
	const char *i = "em1";
#endif
	// warpcore can detach into its own thread spawned by w_init()
	// or inline (in which case one needs to call w_poll on occasion)
	struct warpcore *w = w_init(i, false);
	log("main process ready");

	struct w_socket *s = w_bind(w, IP_P_UDP, 53);
	w_connect(s, ip_aton("192.255.97.255"), 53);

	struct w_iov *o = w_tx_prep(s, 4096);
	iov_fill(o);
	// iov_dump(o);
	w_tx(s, o);

	while (1) {
		// run the receive loop
		w_poll(w);

		// read any data
		struct w_iov *i = w_rx(s);

		// access the read data
		while (i) {
			log("%d bytes in buf %p", i->len, i->buf);
			hexdump(i->buf, i->len);
			i = SLIST_NEXT(i, vecs);
		}

		// read is done, release the iov
		w_rx_done(s);
	}
	w_close(s);

	log("main process exiting");
	w_cleanup(w);

	return 0;
}
