#ifndef _debug_h_
#define _debug_h_

#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

// ANSI escape sequences (color, etc.)
#define NRM "\x1B[0m"	// reset all to normal
#define BLD "\x1B[1m"	// bold
#define DIM "\x1B[2m"	// dim
#define ULN "\x1B[3m"	// underline
#define BLN "\x1B[5m"	// blink
#define REV "\x1B[7m"	// reverse
#define HID "\x1B[8m"	// hidden
#define BLK "\x1B[30m"	// black
#define RED "\x1B[31m"	// red
#define GRN "\x1B[32m"	// green
#define YEL "\x1B[33m"	// yellow
#define BLU "\x1B[34m"	// blue
#define MAG "\x1B[35m"	// magenta
#define CYN "\x1B[36m"	// cyan
#define WHT "\x1B[37m"	// white

#ifndef NDEBUG

enum dlevel { crit = 0, err = 1, warn = 2, notice = 3, info = 4, debug = 5 };

// Set DLEVEL to the level of debug output you want to see in the Makefile
#ifndef DLEVEL
#define DLEVEL info
#endif

// Set DCOMPONENT to a regex matching the components (files) you want to see
// debug output from in the Makefile
#ifndef DCOMPONENT
#define DCOMPONENT ".*"
#endif

extern char *col[];

#include <sys/time.h>
#include <regex.h>

extern regex_t _comp;

// These macros are based on the "D" ones defined by netmap
#define warn(dlevel, fmt, ...)						\
	if (DLEVEL >= dlevel && !regexec(&_comp, __FILE__, 0, 0, 0)) {	\
		struct timeval _lt0;					\
		gettimeofday(&_lt0, 0);					\
		fprintf(stderr, "%s%03ld.%03ld"NRM " %s:%d " fmt "\n",	\
			col[DLEVEL], (long)(_lt0.tv_sec % 1000),	\
			(long)(_lt0.tv_usec / 1000),			\
			__FUNCTION__, __LINE__, ##__VA_ARGS__);		\
	}

// Rate limited version of "log", lps indicates how many per second
#define rwarn(dlevel, lps, format, ...)					\
	if (DLEVEL >= dlevel && !regexec(&_comp, __FILE__, 0, 0, 0)) {	\
		static time_t _rt0, _rcnt;				\
		struct timeval _rts;					\
		gettimeofday(&_rts, 0);					\
		if (_rt0 != _rts.tv_sec) {				\
			_rt0 = _rts.tv_sec;				\
			_rcnt = 0;					\
		}							\
		if (_rcnt++ < lps)					\
			warn(dlevel, format, ##__VA_ARGS__);		\
	}

#else

#define warn(fmt, ...)	do {} while (0)
#define rwarn(fmt, ...)	do {} while (0)

#endif

#define die(fmt, ...)							\
	do {								\
		const int _e = errno;					\
		struct timeval _lt0;					\
		gettimeofday(&_lt0, 0);					\
		fprintf(stderr, BLD"%03ld.%03ld %s [%d] abort: "NRM fmt	\
			" [%s]\n", (long)(_lt0.tv_sec % 1000),		\
			(long)(_lt0.tv_usec / 1000), __FUNCTION__, 	\
			__LINE__, ##__VA_ARGS__, 			\
			(_e ? strerror(_e) : ""));			\
		abort();						\
	} while (0)

extern void hexdump(const void * const ptr, const unsigned len);

#endif
