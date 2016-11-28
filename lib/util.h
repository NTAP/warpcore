#pragma once

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>


#define NRM "\x1B[0m"  ///< ANSI escape sequence: reset all to normal
#define BLD "\x1B[1m"  ///< ANSI escape sequence: bold
#define DIM "\x1B[2m"  ///< ANSI escape sequence: dim
#define ULN "\x1B[3m"  ///< ANSI escape sequence: underline
#define BLN "\x1B[5m"  ///< ANSI escape sequence: blink
#define REV "\x1B[7m"  ///< ANSI escape sequence: reverse
#define HID "\x1B[8m"  ///< ANSI escape sequence: hidden
#define BLK "\x1B[30m" ///< ANSI escape sequence: black
#define RED "\x1B[31m" ///< ANSI escape sequence: red
#define GRN "\x1B[32m" ///< ANSI escape sequence: green
#define YEL "\x1B[33m" ///< ANSI escape sequence: yellow
#define BLU "\x1B[34m" ///< ANSI escape sequence: blue
#define MAG "\x1B[35m" ///< ANSI escape sequence: magenta
#define CYN "\x1B[36m" ///< ANSI escape sequence: cyan
#define WHT "\x1B[37m" ///< ANSI escape sequence: white


/// Trim the path from the given file name. Mostly to be used with __FILE__.
///
/// @param      f     An (absolute) file name, to trim.
///
/// @return     The standalone file name (no path).
///
#define basename(f) (strrchr((f), '/') ? strrchr((f), '/') + 1 : (f))

#ifndef plural
/// Helper to pluralize output words.
///
/// @param      n     An integer value.
///
/// @return     The character 's' when @p n is 1; an empty character otherwise.
///
#define plural(n) ((n) == 1 ? 0 : 's')
#endif

#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif


/// Lock to prevent multiple threads from corrupting each others output.
///
extern pthread_mutex_t _lock;

/// Thread ID of the master thread.
///
extern pthread_t _master;


/// Abort execution with a message.
///
/// @param      fmt   A printf()-style format string.
/// @param      ...   Subsequent arguments to be converted for output according
///                   to @p fmt.
///
#define die(...)                                                               \
    do {                                                                       \
        if (pthread_mutex_lock(&_lock))                                        \
            abort();                                                           \
        const int _e = errno;                                                  \
        struct timeval _now, _elapsed;                                         \
        gettimeofday(&_now, 0);                                                \
        timeval_subtract(&_elapsed, &_now, &_epoch);                           \
        fprintf(                                                               \
            stderr, REV "%s " NRM RED BLD REV " %ld.%04ld   %s %s:%d ABORT: ", \
            (pthread_self() == _master ? BLK : WHT),                           \
            (long)(_elapsed.tv_sec % 1000), (long)(_elapsed.tv_usec / 1000),   \
            __func__, basename(__FILE__), __LINE__);                           \
        fprintf(stderr, __VA_ARGS__);                                          \
        fprintf(stderr, " %c%s%c\n" NRM, (_e ? '[' : 0),                       \
                (_e ? strerror(_e) : ""), (_e ? ']' : 0));                     \
        fflush(stderr);                                                        \
        pthread_mutex_unlock(&_lock);                                          \
        abort();                                                               \
    } while (0)


#ifndef NDEBUG

#include <regex.h>

/// Debug levels, decreasing severity.
///
enum dlevel {
    crit = 0,   ///< Critical
    err = 1,    ///< Error
    warn = 2,   ///< Warning
    notice = 3, ///< Notice
    info = 4,   ///< Informational
    debug = 5   ///< Debug
};

// Set DLEVEL to the level of debug output you want to see in the Makefile
#ifndef DLEVEL
/// Default debug level. Can be overridden by setting the DLEVEL define in
/// CFLAGS.
#define DLEVEL debug
#endif

#ifndef DCOMPONENT
/// Default components to see debug messages from. Can be overridden by setting
/// the DCOMPONENT define to a regular expression matching the components
/// (files) you want to see debug output from.
#define DCOMPONENT ".*"
#endif


/// An array of ANSI color sequences associated with the different #dlevel
/// severities.
///
extern const char * const _col[];

/// Holds the regex compiled from DCOMPONENT.
///
extern regex_t _comp;


// These macros are based on the "D" ones defined by netmap

/// Print a debug message to stderr, including a black/white indicator whether
/// the message comes from the master thread (black) or not (white), a timestamp
/// since start of the program, a colored indicator for the #dlevel severity
/// level, as well as a printf()-style format string fed by further optional
/// arguments.
///
/// @param      dlevel  The #dlevel severity level of the message
/// @param      fmt     A printf()-style format string.
/// @param      ...     Subsequent arguments to be converted for output
///                     according to @p fmt.
///
#define warn(dlevel, ...)                                                      \
    do {                                                                       \
        if (DLEVEL >= dlevel && !regexec(&_comp, __FILE__, 0, 0, 0)) {         \
            if (pthread_mutex_lock(&_lock))                                    \
                abort();                                                       \
            struct timeval _now, _elapsed;                                     \
            gettimeofday(&_now, 0);                                            \
            timeval_subtract(&_elapsed, &_now, &_epoch);                       \
            fprintf(stderr, REV "%s " NRM " %ld.%04ld " REV "%s " NRM MAG      \
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


/// Rate-limited variant of warn(), which repeats the message prints at most @p
/// lps times.
///
/// @param      dlevel  The #dlevel severity level of the message
/// @param      lps     The maximum rate at which to repeat the message (lines
///                     per second).
/// @param      fmt     A printf()-style format string.
/// @param      ...     Subsequent arguments to be converted for output
///                     according to @p fmt.
///
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


//
#undef assert
/// A version of the C assert() macro that isn't disabled by NDEBUG and that
/// ties in with warn(), die() and our other debug functions.
///
/// @param      e       Expression to check. No action if @p e is true.
/// @param      fmt     A printf()-style format string.
/// @param      ...     Subsequent arguments to be converted for output
///                     according to @p fmt.
#define assert(e, ...)                                                         \
    do {                                                                       \
        if (__builtin_expect(!(e), 0))                                         \
            die("assertion failed \n           " #e                            \
                " \n           " __VA_ARGS__);                                 \
    } while (0)


/// Stores the timeval at the start of the program, used to print relative times
/// in warn(), die(), etc.
///
extern struct timeval _epoch;


extern int __attribute__((nonnull))
timeval_subtract(struct timeval * const result,
                 struct timeval * const x,
                 struct timeval * const y);

extern void __attribute__((nonnull))
hexdump(const void * const ptr, const size_t len);
