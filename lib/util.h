#pragma once

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>


// ANSI escape sequences (color, etc.)
#define NRM "\x1B[0m"  // reset all to normal
#define BLD "\x1B[1m"  // bold
#define DIM "\x1B[2m"  // dim
#define ULN "\x1B[3m"  // underline
#define BLN "\x1B[5m"  // blink
#define REV "\x1B[7m"  // reverse
#define HID "\x1B[8m"  // hidden
#define BLK "\x1B[30m" // black
#define RED "\x1B[31m" // red
#define GRN "\x1B[32m" // green
#define YEL "\x1B[33m" // yellow
#define BLU "\x1B[34m" // blue
#define MAG "\x1B[35m" // magenta
#define CYN "\x1B[36m" // cyan
#define WHT "\x1B[37m" // white


// Trim the path from the given file name (to be used with __FILE__)
#define basename(f) (strrchr((f), '/') ? strrchr((f), '/') + 1 : (f))


#define plural(w) ((w) == 1 ? 0 : 's')


extern pthread_mutex_t _lock;
extern pthread_t _master;


// Abort execution with a message
#define die(...)                                                               \
    do {                                                                       \
        if (pthread_mutex_lock(&_lock))                                        \
            abort();                                                           \
        const int _e = errno;                                                  \
        struct timeval _now, _elapsed;                                         \
        gettimeofday(&_now, 0);                                                \
        timeval_subtract(&_elapsed, &_now, &_epoch);                           \
        fprintf(stderr,                                                        \
                REV "%s " NRM RED BLD REV "% 2ld.%04ld   %s %s:%d ABORT: ",    \
                (pthread_self() == _master ? BLK : WHT),                       \
                (long)(_elapsed.tv_sec % 1000),                                \
                (long)(_elapsed.tv_usec / 1000), __func__, basename(__FILE__), \
                __LINE__);                                                     \
        fprintf(stderr, __VA_ARGS__);                                          \
        fprintf(stderr, " %c%s%c\n" NRM, (_e ? '[' : 0),                       \
                (_e ? strerror(_e) : ""), (_e ? ']' : 0));                     \
        fflush(stderr);                                                        \
        pthread_mutex_unlock(&_lock);                                          \
        abort();                                                               \
    } while (0)


#ifndef NDEBUG

#include <regex.h>

enum dlevel { crit = 0, err = 1, warn = 2, notice = 3, info = 4, debug = 5 };

// Set DLEVEL to the level of debug output you want to see in the Makefile
#ifndef DLEVEL
#define DLEVEL debug
#endif

// Set DCOMPONENT to a regex matching the components (files) you want to see
// debug output from in the Makefile
#ifndef DCOMPONENT
#define DCOMPONENT ".*"
#endif

extern const char * const _col[];
extern regex_t _comp;


// These macros are based on the "D" ones defined by netmap
#define warn(dlevel, ...)                                                      \
    do {                                                                       \
        if (DLEVEL >= dlevel && !regexec(&_comp, __FILE__, 0, 0, 0)) {         \
            if (pthread_mutex_lock(&_lock))                                    \
                abort();                                                       \
            struct timeval _now, _elapsed;                                     \
            gettimeofday(&_now, 0);                                            \
            timeval_subtract(&_elapsed, &_now, &_epoch);                       \
            fprintf(stderr, REV "%s " NRM "% 2ld.%04ld " REV "%s " NRM MAG     \
                                " %s" BLK " " BLU "%s:%d " NRM,                \
                    (pthread_self() == _master ? BLK : WHT),                   \
                    (long)(_elapsed.tv_sec % 1000),                            \
                    (long)(_elapsed.tv_usec / 1000), _col[dlevel], __func__,   \
                    basename(__FILE__), __LINE__);                             \
            fprintf(stderr, __VA_ARGS__);                                      \
            fprintf(stderr, "\n");                                             \
            fflush(stderr);                                                    \
            if (pthread_mutex_unlock(&_lock))                                  \
                abort();                                                       \
        }                                                                      \
    } while (0)

// Rate limited version of "log", lps indicates how many per second
#define rwarn(dlevel, lps, ...)                                                \
    do {                                                                       \
        if (DLEVEL >= dlevel && !regexec(&_comp, __FILE__, 0, 0, 0)) {         \
            static time_t _rt0, _rcnt;                                         \
            struct timeval _rts;                                               \
            gettimeofday(&_rts, 0);                                            \
            if (_rt0 != _rts.tv_sec) {                                         \
                _rt0 = _rts.tv_sec;                                            \
                _rcnt = 0;                                                     \
            }                                                                  \
            if (_rcnt++ < lps)                                                 \
                warn(dlevel, __VA_ARGS__);                                     \
        }                                                                      \
    } while (0)

#else

#define warn(...)                                                              \
    do {                                                                       \
    } while (0)

#define rwarn(...)                                                             \
    do {                                                                       \
    } while (0)

#endif


// A version of the assert() macro that isn't disabled by NDEBUG and that uses
// our other debug functions
#undef assert
#define assert(e, ...)                                                         \
    do {                                                                       \
        if (__builtin_expect(!(e), 0))                                         \
            die("assertion failed \n           " #e                            \
                " \n           " __VA_ARGS__);                                 \
    } while (0)


extern struct timeval _epoch;
extern int timeval_subtract(struct timeval * const result,
                            struct timeval * const x,
                            struct timeval * const y);

extern void hexdump(const void * const ptr, const size_t len);
