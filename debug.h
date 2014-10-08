#ifndef _debug_h_
#define _debug_h_

#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifndef NDEBUG

enum dlevel { crit, err, warn, notice, info, debug };

#ifndef DLEVEL
#define DLEVEL err
#endif

#include <sys/time.h>

// These macros are based on the "D" ones defined by netmap
#define dlog(dlevel, fmt, ...)                                          	\
	if (DLEVEL >= dlevel) {                                          \
		struct timeval _lt0;                                	\
		gettimeofday(&_lt0, 0);                             	\
		fprintf(stderr, "%03d.%06d %s [%d] " fmt "\n",    	\
			(int)(_lt0.tv_sec % 1000), (int)_lt0.tv_usec, 	\
			__FUNCTION__, __LINE__, ##__VA_ARGS__);   	\
	}

// Rate limited version of "log", lps indicates how many per second
#define drlog(dlevel, lps, format, ...)                                 	\
	if (DLEVEL >= dlevel) {                                          \
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

#else

#define dlog(fmt, ...)	do {} while (0)
#define drlog(fmt, ...)	do {} while (0)

#endif

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

extern void hexdump(const void * const ptr, const unsigned len);

#endif
