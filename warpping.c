#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <time.h>

#include "warpcore.h"
#include "ip.h"


static void usage(const char * const name, const uint32_t loops)
{
	printf("%s\n", name);
	printf("\t -i interface           interface to run over\n");
	printf("\t -d destination IP      peer to connect to\n");
	printf("\t[-l loop interations]   optional, default %d\n", loops);
}


// Subtract the struct timespec values x and y (x-y), storing the result in r
static void ts_diff(struct timespec * const r, struct timespec * const x,
                   struct timespec * const y)
{
	// Perform the carry for the later subtraction by updating y
	if (x->tv_nsec < y->tv_nsec) {
		const int nsec = (y->tv_nsec - x->tv_nsec) / 1000000000 + 1;
		y->tv_nsec -= 1000000000 * nsec;
		y->tv_sec += nsec;
	}
	if (x->tv_nsec - y->tv_nsec > 1000000000) {
		const int nsec = (x->tv_nsec - y->tv_nsec) / 1000000000;
		y->tv_nsec += 1000000000 * nsec;
		y->tv_sec -= nsec;
	}

	// Compute the result; tv_nsec is certainly positive.
	r->tv_sec = x->tv_sec - y->tv_sec;
	r->tv_nsec = x->tv_nsec - y->tv_nsec;
}


int main(int argc, char *argv[])
{
	const char *ifname = 0;
	const char *dst = 0;
	uint32_t loops = 1;

	int ch;
	while ((ch = getopt(argc, argv, "hi:d:l:")) != -1) {
		switch (ch) {
		case 'i':
			ifname = optarg;
			break;
		case 'd':
			dst = optarg;
			break;
		case 'l':
			loops = strtol(optarg, 0, 10);
			break;
		case 'h':
		case '?':
		default:
			usage(argv[0], loops);
			return 0;
		}
	}

	if (ifname == 0 || dst == 0) {
		usage(argv[0], loops);
		return 0;
	}

	struct addrinfo hints, *res;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_INET;
	hints.ai_protocol = IP_P_UDP;
	if (getaddrinfo(dst, "echo", &hints, &res) != 0)
		die("getaddrinfo");
	const uint32_t ip =
		((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr;
	const uint16_t port = ((struct sockaddr_in *)res->ai_addr)->sin_port;
	freeaddrinfo(res);

	struct warpcore *w = w_init(ifname);
	log(1, "main process ready");

	struct w_sock * const s = w_bind(w, IP_P_UDP, port);
	w_connect(s, ip, port);

	while (loops--) {
		struct w_iov * const o = w_tx_alloc(s, sizeof(struct timespec));
		if (clock_gettime(CLOCK_REALTIME,
		                  (struct timespec *)o->buf) == -1)
			die("clock_gettime");
		w_tx(s);
		if (w_poll(w, POLLIN, -1) == false)
			break;
		struct w_iov * const i = w_rx(s);
		struct timespec diff, now;
		if (clock_gettime(CLOCK_REALTIME, &now) == -1)
			die("clock_gettime");
		ts_diff(&diff, &now, (struct timespec *)i->buf);
		if (diff.tv_sec != 0)
			die("time difference > 1 sec");
		printf("%ld ns\n", diff.tv_nsec);
		w_rx_done(s);
	}

	w_close(s);

	log(1, "main process exiting");
	w_cleanup(w);

	return 0;
}
