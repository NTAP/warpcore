#ifndef _debug_h_
#define _debug_h_

#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifndef NDEBUG

#ifndef DLEVEL
#define DLEVEL 1
#endif

#include <sys/time.h>

// these macros are based on the "D" ones defined by netmap

#define log(level, fmt, ...)                                          	\
	if (DLEVEL >= level) {                                          \
		struct timeval _lt0;                                	\
		gettimeofday(&_lt0, 0);                             	\
		fprintf(stderr, "%03d.%06d %s [%d] " fmt "\n",    	\
			(int)(_lt0.tv_sec % 1000), (int)_lt0.tv_usec, 	\
			__FUNCTION__, __LINE__, ##__VA_ARGS__);   	\
	}

// Rate limited version of "log", lps indicates how many per second
#define rlog(level, lps, format, ...)                                 	\
	if (DLEVEL >= level) {                                          \
		static int _rt0, _rcnt;                               	\
		struct timeval _rts;                              	\
		gettimeofday(&_rts, 0);                           	\
		if (_rt0 != _rts.tv_sec) {                          	\
			_rt0 = _rts.tv_sec;                         	\
			_rcnt = 0;                                  	\
		}                                                 	\
		if (_rcnt++ < lps) {                                	\
			log(level, format, ##__VA_ARGS__);             	\
		}                                                 	\
	}


#define die(fmt, ...)                                             	\
	do {                                                      	\
		const int e = errno;                              	\
		if (e) {                                           	\
			log(0, "abort: " fmt ": %s", ##__VA_ARGS__,    	\
			     strerror(e));                        	\
		} else {                                              	\
			log(0, "abort: " fmt , ##__VA_ARGS__);        	\
		}							\
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
