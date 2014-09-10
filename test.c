#include "warpcore.h"
#include "ip.h"


void iov_fill(struct w_iov *v)
{
	while (v) {
		log("%d bytes in buf %d", v->len, v->idx);
		for (uint_fast16_t l = 0; l < v->len; l++) {
			v->buf[l] = (char)(l % 0xff);
		}
		v = SLIST_NEXT(v, next);
	}
}


void iov_dump(struct w_iov *v)
{
	while (v) {
		log("%d bytes in buf %d", v->len, v->idx);
		hexdump(v->buf, v->len);
		v = SLIST_NEXT(v, next);
	}
}


int main(void)
{
#ifdef __linux__
	const char *ifname = "eth1";
#else
	const char *ifname = "em1";
#endif

	struct warpcore *w = w_init(ifname);;
	log("main process ready");

	struct w_sock *s = w_bind(w, IP_P_UDP, 7);
	w_connect(s, ip_aton("192.168.125.129"), 7);

	for (int i = 0; i < 3; i++) {
		int len = 1200;
		struct w_iov *o = w_tx_alloc(s, len);
		iov_fill(o);
		// iov_dump(o);
		w_tx(s);

		while (len > 0) {
			// run the receive loop
			log("call poll");
			if (w_poll(w) == false)
				goto done;

			// read any data
			struct w_iov *i = w_rx(s);

			// access the read data
			while (i) {
				log("%d bytes in buf %d", i->len, i->idx);
				len -= i->len;
				// hexdump(i->buf, i->len);
				i = SLIST_NEXT(i, next);
			}

			// read is done, release the iov
			w_rx_done(s);
		}
	}
done:
	w_close(s);

	// keep running
	while (w_poll(w)) {}

	log("main process exiting");
	w_cleanup(w);

	return 0;
}
