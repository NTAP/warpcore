#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>

#include "debug.h"


static void usage(const char * const name, const uint32_t loops)
{
	printf("%s\n", name);
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
	const char *dst = 0;
	uint32_t loops = 1;

	int ch;
	while ((ch = getopt(argc, argv, "hd:l:")) != -1) {
		switch (ch) {
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

	if (dst == 0) {
		usage(argv[0], loops);
		return 0;
	}

	struct addrinfo hints, *res;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_INET;
	hints.ai_protocol = IPPROTO_UDP;
	if (getaddrinfo(dst, "echo", &hints, &res) != 0)
		die("getaddrinfo");

	log(1, "main process ready");

   	int s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
   	if (s == -1)
   		die("socket");

	struct pollfd fds = { .fd = s, .events = POLLIN };

	while (loops--) {
		#define LEN 1400
		char now[LEN], then[LEN];
		if (clock_gettime(CLOCK_REALTIME, (struct timespec *)now) == -1)
			die("clock_gettime");

		sendto(s, (struct timespec *)now, LEN, 0,
	               res->ai_addr, res->ai_addrlen);

		if (poll(&fds, 1, -1) == -1)
			die("poll");

		socklen_t fromlen = res->ai_addrlen;
		if (recvfrom(s, then, LEN, 0,
	                 res->ai_addr, &fromlen) == -1)
			die("recvfrom");

		if (clock_gettime(CLOCK_REALTIME, (struct timespec *)now) == -1)
			die("clock_gettime");

		struct timespec diff;
		ts_diff(&diff, (struct timespec *)now, (struct timespec *)then);
		if (diff.tv_sec != 0)
			die("time difference > 1 sec");

		printf("%ld ns\n", diff.tv_nsec);

	}
	close(s);
	freeaddrinfo(res);

	log(1, "main process exiting");

	return 0;
}
