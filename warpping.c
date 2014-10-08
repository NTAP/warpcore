#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <time.h>
#include <sys/param.h>

#include "warpcore.h"
#include "ip.h"


static void usage(const char * const name, const uint16_t size,
                  const long loops)
{
	printf("%s\n", name);
	printf("\t[-k]                    use kernel, default is warpcore\n");
	printf("\t -i interface           interface to run over\n");
	printf("\t -d destination IP      peer to connect to\n");
	printf("\t[-s packet size]        optional, default %d\n", size);
	printf("\t[-l loop interations]   optional, default %ld\n", loops);
	printf("\t[-b]                    busy-wait\n");
}


// Subtract the struct timespec values x and y (x-y), storing the result in r
static void time_diff(struct timespec * const r, struct timespec * const x,
                   struct timespec * const y)
{
	// Perform the carry for the later subtraction by updating y
	if (x->tv_nsec < y->tv_nsec) {
		const long nsec = (y->tv_nsec - x->tv_nsec) / 1000000000 + 1;
		y->tv_nsec -= 1000000000 * nsec;
		y->tv_sec += nsec;
	}
	if (x->tv_nsec - y->tv_nsec > 1000000000) {
		const long nsec = (x->tv_nsec - y->tv_nsec) / 1000000000;
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
	long loops = 1;
	uint16_t size = sizeof(struct timespec);
	bool use_warpcore = true;
	bool busywait = false;

	int ch;
	while ((ch = getopt(argc, argv, "hi:d:l:s:kb")) != -1) {
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
		case 's':
			size = (uint16_t)MIN(UINT16_MAX,
			                     MAX(size, strtol(optarg, 0, 10)));
			break;
		case 'k':
			use_warpcore = false;
			break;
		case 'b':
			busywait = true;
			break;
		case 'h':
		case '?':
		default:
			usage(argv[0], size, loops);
			return 0;
		}
	}

	if (ifname == 0 || dst == 0) {
		usage(argv[0], size, loops);
		return 0;
	}

	struct addrinfo hints, *res;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_INET;
	hints.ai_protocol = IP_P_UDP;
	if (getaddrinfo(dst, "echo", &hints, &res) != 0)
		die("getaddrinfo");

	struct warpcore *w = 0;
	struct w_sock *ws = 0;
	int ks = 0;
	struct pollfd fds;
	char *before = 0;
	char *after = 0;
	if (use_warpcore) {
		w = w_init(ifname);
		ws = w_bind(w, IP_P_UDP, (uint16_t)random());
		w_connect(ws,
		          ((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr,
		          ((struct sockaddr_in *)res->ai_addr)->sin_port);
	} else {
		w_init_common();
	   	ks = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	   	if (ks == -1)
	   		die("socket");
		fds.fd = ks;
		fds.events = POLLIN;
		before = calloc(1, size);
		after = calloc(1, size);
	}

	printf("time\tcodetime\tsize\n");
	while (likely(loops--)) {
		// struct timeval *rx = 0;
		if (use_warpcore) {
			struct w_iov * const o = w_tx_alloc(ws, size);
			if (clock_gettime(CLOCK_MONOTONIC,
			                  (struct timespec *)o->buf) == -1)
				die("clock_gettime");
			w_tx(ws);

			if (!busywait)
				w_poll(w, POLLIN, 1000);
			else
				w_kick_rx(w);

			if (w->interrupt)
				goto done;

			struct w_iov * const i = w_rx(ws);
			if (unlikely(i == 0)) {
				dlog(info, "packet loss?");
				continue;
			}
			after = i->buf;
			// rx = &i->ts;
		} else {
			if (clock_gettime(CLOCK_MONOTONIC,
			                  (struct timespec *)before) == -1)
				die("clock_gettime");
			sendto(ks, before, size, 0, res->ai_addr,
			       res->ai_addrlen);

			if (!busywait) {
				const int p = poll(&fds, 1, 1000);
				if (unlikely(p == -1))
					die("poll");
				else if (unlikely(p == 0)) {
					dlog(warn, "packet loss?");
					continue;
				}
			}

			socklen_t fromlen = res->ai_addrlen;
			if (unlikely(recvfrom(ks, after, size, 0, res->ai_addr,
			                      &fromlen) == -1))
				die("recvfrom");
		}

		struct timespec diff, now;
		if (clock_gettime(CLOCK_MONOTONIC, &now) == -1)
			die("clock_gettime");

		time_diff(&diff, &now, (struct timespec *)after);
		if (unlikely(diff.tv_sec != 0))
			die("time difference is more than %ld sec",
			    diff.tv_sec);

		printf("%ld\t", diff.tv_nsec);

		if (use_warpcore) {
			// time_diff(&diff, &now, rx);
			// printf("%ld\t", diff.tv_nsec);
			printf("0\t");
			w_rx_done(ws);
		} else
			printf("0\t");
		printf("%d\n", size);

	}
done:
	if (use_warpcore) {
		w_close(ws);
		w_cleanup(w);
	} else {
		free(before);
		free(after);
	}
	freeaddrinfo(res);

	return 0;
}
