#ifndef _debug_h_
#define _debug_h_

#include <stdlib.h>

#ifndef NDEBUG
#include <stdio.h>

// these macros are based on the "D" ones defined by netmap

#define log(_fmt, ...)                                              \
	do {                                                        \
		struct timeval _t0;                                 \
		gettimeofday(&_t0, NULL);                           \
		fprintf(stderr, "%03d.%06d %s [%d] " _fmt "\n",     \
			(int)(_t0.tv_sec % 1000), (int)_t0.tv_usec, \
			__FUNCTION__, __LINE__, ##__VA_ARGS__);     \
	} while (0)

// Rate limited version of "log", lps indicates how many per second
#define rlog(lps, format, ...)                                      \
	do {                                                        \
		static int __t0, __cnt;                             \
		struct timeval __xxts;                              \
		gettimeofday(&__xxts, NULL);                        \
		if (__t0 != __xxts.tv_sec) {                        \
			__t0 = __xxts.tv_sec;                       \
			__cnt = 0;                                  \
		}                                                   \
		if (__cnt++ < lps) {                                \
			D(format, ##__VA_ARGS__);                   \
		}                                                   \
	} while (0)


#define die(_fmt, ...)                                              \
	do {                                                        \
		log("die: " _fmt, ##__VA_ARGS__);                   \
		abort();                                            \
	} while (0)

extern void hexdump(const void * const ptr, const unsigned len);

#else

#define log(_fmt, ...) do {} while(0)
#define die(_fmt, ...) do { abort(); } while(0)

#endif

#endif
