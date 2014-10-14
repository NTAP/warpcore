#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h>

#include "warpcore.h"
#include "ip.h"


static void
usage(const char * const name)
{
	printf("%s\n", name);
	printf("\t -i interface           interface to run over\n");
	printf("\t[-b]                    busy-wait\n");
}


int
main(int argc, char *argv[])
{
	const char *ifname = 0;
	bool busywait = false;

	int ch;
	while ((ch = getopt(argc, argv, "hi:b")) != -1) {
		switch (ch) {
		case 'i':
			ifname = optarg;
			break;
		case 'b':
			busywait = true;
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

	// start the inetd-like "small services"
	struct w_sock *ech = w_bind(w, IP_P_UDP, htons(7));
	struct w_sock *dsc = w_bind(w, IP_P_UDP, htons(9));
	struct w_sock *dtm = w_bind(w, IP_P_UDP, htons(13));
	struct w_sock *tme = w_bind(w, IP_P_UDP, htons(37));

	while (likely(!w->interrupt)) {
		if (!busywait)
			w_poll(w, POLLIN, -1);
		else
			w_kick_rx(w);

		// echo service
		struct w_iov *i = w_rx(ech);
		while (i) {
			dlog(info, "echo %d bytes in buf %d", i->len, i->idx);
			// echo the data
			struct w_iov *o = w_tx_alloc(ech, i->len);
			memcpy(o->buf, i->buf, i->len);
			w_connect(ech, i->ip, i->port);
			w_tx(ech);
			i = SLIST_NEXT(i, next);
		}
		w_rx_done(ech);

		// discard service
		i = w_rx(dsc);
		while (i) {
			dlog(info, "discard %d bytes in buf %d",
			     i->len, i->idx);
			// discard the data
			i = SLIST_NEXT(i, next);
		}
		w_rx_done(dsc);

		// daytime service
		i = w_rx(dtm);
		while (i) {
			dlog(info, "daytime %d bytes in buf %d",
			     i->len, i->idx);
			const time_t t = time(0);
			const char *c = ctime(&t);
			const uint32_t l = (uint32_t)strlen(c);
			struct w_iov *o = w_tx_alloc(dtm, l);
			memcpy(o->buf, c, l);
			w_connect(dtm, i->ip, i->port);
			w_tx(dtm);
			i = SLIST_NEXT(i, next);
		}
		w_rx_done(dtm);

		// time service
		i = w_rx(tme);
		while (i) {
			dlog(info, "time %d bytes in buf %d", i->len, i->idx);
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

	w_cleanup(w);

	return 0;
}
