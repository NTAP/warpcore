#ifndef _debug_h_
#define _debug_h_

#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifndef NDEBUG

enum dlevel { crit, err, warn, notice, info, debug };

// Set DLEVEL to the level of debug output you want to see in the Makefile
#ifndef DLEVEL
#define DLEVEL info
#endif

// Set DCOMPONENT to a regex matching the components (files) you want to see
// debug output from in the Makefile
#ifndef DCOMPONENT
#define DCOMPONENT "warpcore|tcp"
#endif

#include <sys/time.h>
#include <regex.h>

extern regex_t _comp;

// These macros are based on the "D" ones defined by netmap
#define warn(dlevel, fmt, ...)                                         	\
	if (DLEVEL >= dlevel && !regexec(&_comp, __FILE__, 0, 0, 0)) {	\
		struct timeval _lt0;                                	\
		gettimeofday(&_lt0, 0);                             	\
		fprintf(stderr, "%03d.%06d %s [%d] " fmt "\n",    	\
			(int)(_lt0.tv_sec % 1000), (int)_lt0.tv_usec, 	\
			__FUNCTION__, __LINE__, ##__VA_ARGS__);   	\
	}

// Rate limited version of "log", lps indicates how many per second
#define rwarn(dlevel, lps, format, ...)                                	\
	if (DLEVEL >= dlevel && !regexec(&_comp, __FILE__, 0, 0, 0)) {	\
		static time_t _rt0, _rcnt;                            	\
		struct timeval _rts;                              	\
		gettimeofday(&_rts, 0);                           	\
		if (_rt0 != _rts.tv_sec) {                          	\
			_rt0 = _rts.tv_sec;                         	\
			_rcnt = 0;                                  	\
		}                                                 	\
		if (_rcnt++ < lps) {                                	\
			warn(dlevel, format, ##__VA_ARGS__);          	\
		}                                                 	\
	}

#else

#define warn(fmt, ...)	do {} while (0)
#define rwarn(fmt, ...)	do {} while (0)

#endif

#define die(fmt, ...)                                             	\
	do {                                                      	\
		const int e = errno;                              	\
		struct timeval _lt0;                                	\
		gettimeofday(&_lt0, 0);                             	\
		fprintf(stderr, "%03d.%06d %s [%d] abort: " fmt 	\
			" [%s]\n", (int)(_lt0.tv_sec % 1000), 		\
			(int)_lt0.tv_usec, __FUNCTION__, __LINE__, 	\
			##__VA_ARGS__, (e ? strerror(e) : ""));		\
		abort();                                          	\
	} while (0)

extern void hexdump(const void * const ptr, const unsigned len);

#endif
