#ifndef _debug_h_
#define _debug_h_

#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifndef NDEBUG
#include <sys/time.h>

// these macros are based on the "D" ones defined by netmap

#define log(fmt, ...)                                             	\
	do {                                                      	\
		struct timeval t0;                                	\
		gettimeofday(&t0, 0);                             	\
		fprintf(stderr, "%03d.%06d %s [%d] " fmt "\n",    	\
			(int)(t0.tv_sec % 1000), (int)t0.tv_usec, 	\
			__FUNCTION__, __LINE__, ##__VA_ARGS__);   	\
	} while (0)

// Rate limited version of "log", lps indicates how many per second
#define rlog(lps, format, ...)                                    	\
	do {                                                      	\
		static int t0, cnt;                               	\
		struct timeval xxts;                              	\
		gettimeofday(&xxts, 0);                           	\
		if (t0 != xxts.tv_sec) {                          	\
			t0 = xxts.tv_sec;                         	\
			cnt = 0;                                  	\
		}                                                 	\
		if (cnt++ < lps) {                                	\
			log(format, ##__VA_ARGS__);               	\
		}                                                 	\
	} while (0)


#define die(fmt, ...)                                             	\
	do {                                                      	\
		const int e = errno;                              	\
		if (e)                                            	\
			log("abort: " fmt ": %s", ##__VA_ARGS__,    	\
			     strerror(e));                        	\
		else                                              	\
			log("abort: " fmt , ##__VA_ARGS__);         	\
		abort();                                          	\
	} while (0)

extern void hexdump(const void * const ptr, const unsigned len);

#else

#define log(fmt, ...)	do {} while(0)
#define hexdump(...)	do {} while(0)

#define die(fmt, ...)                                             	\
	do {                                                      	\
		const int e = errno;                              	\
		if (e)                                            	\
			fprintf(stderr, "abort: " fmt ": %s\n",       	\
			        ##__VA_ARGS__, strerror(e));      	\
		else                                              	\
			fprintf(stderr, "abort: " fmt "\n",             \
			        ##__VA_ARGS__);                         \
		abort();                                          	\
	} while (0)


#endif

#endif
