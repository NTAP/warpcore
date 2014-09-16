#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h>

#include "warpcore.h"
#include "ip.h"


static void usage(const char * const name)
{
	printf("%s\n", name);
	printf("\t -i interface           interface to run over\n");
}


int main(int argc, char *argv[])
{
	const char *ifname = 0;

	int ch;
	while ((ch = getopt(argc, argv, "hi:")) != -1) {
		switch (ch) {
		case 'i':
			ifname = optarg;
			break;
		case 'h':
		case '?':
		default:
			usage(argv[0]);
			return 0;
		}
	}

	if (ifname == 0) {
		usage(argv[0]);
		return 0;
	}

	struct warpcore *w = w_init(ifname);
	log(1, "main process ready");

	// start the inetd-like "small services"
	struct w_sock *ech = w_bind(w, IP_P_UDP, htons(7));
	struct w_sock *dsc = w_bind(w, IP_P_UDP, htons(9));
	struct w_sock *dtm = w_bind(w, IP_P_UDP, htons(13));
	struct w_sock *tme = w_bind(w, IP_P_UDP, htons(37));

	while (likely(w_poll(w, POLLIN, -1))) {
		// echo service
		struct w_iov *i = w_rx(ech);
		while (likely(i)) {
			log(5, "echo %d bytes in buf %d", i->len, i->idx);
			// ech the data
			struct w_iov *o = w_tx_alloc(ech, i->len);
			memcpy(o->buf, i->buf, i->len);
			w_connect(ech, i->ip, i->port);
			w_tx(ech);
			i = SLIST_NEXT(i, next);
		}
		w_rx_done(ech);

		// discard service
		i = w_rx(dsc);
		while (likely(i)) {
			log(5, "discard %d bytes in buf %d", i->len, i->idx);
			// dscard the data
			i = SLIST_NEXT(i, next);
		}
		w_rx_done(dsc);

		// daytime service
		i = w_rx(dtm);
		while (likely(i)) {
			log(5, "daytime %d bytes in buf %d", i->len, i->idx);
			const time_t t = time(0);
			const char *c = ctime(&t);
			const size_t l = strlen(c);
			struct w_iov *o = w_tx_alloc(dtm, l);
			memcpy(o->buf, c, l);
			w_connect(dtm, i->ip, i->port);
			w_tx(dtm);
			i = SLIST_NEXT(i, next);
		}
		w_rx_done(dtm);

		// time service
		i = w_rx(tme);
		while (likely(i)) {
			log(5, "time %d bytes in buf %d", i->len, i->idx);
			const time_t t = time(0);
			struct w_iov *o = w_tx_alloc(tme, sizeof(uint32_t));
			uint32_t * const b = (uint32_t *)o->buf;
			*b = htonl(t);
			w_connect(tme, i->ip, i->port);
			w_tx(tme);
			i = SLIST_NEXT(i, next);
		}
		w_rx_done(tme);
	}
	w_close(ech);
	w_close(dsc);
	w_close(dtm);
	w_close(tme);

	log(1, "main process exiting");
	w_cleanup(w);

	return 0;
}
