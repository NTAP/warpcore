// SPDX-License-Identifier: BSD-2-Clause
//
// Copyright (c) 2014-2020, NetApp, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#pragma once
// IWYU pragma: private, include <warpcore/warpcore.h>

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define __FILENAME__                                                           \
    (__builtin_strrchr(__FILE__, '/') ? __builtin_strrchr(__FILE__, '/') + 1   \
                                      : __FILE__)

#ifndef MS_PER_S
#define MS_PER_S UINT16_C(1000) ///< Milliseconds per second.
#endif

#ifndef US_PER_S
#define US_PER_S UINT32_C(1000000) ///< Microseconds per second.
#endif

#ifndef NS_PER_S
#define NS_PER_S UINT64_C(1000000000) ///< Nanoseconds per second.
#endif

#ifndef US_PER_MS
#define US_PER_MS UINT16_C(1000) ///< Microseconds per millisecond.
#endif

#ifndef NS_PER_MS
#define NS_PER_MS UINT32_C(1000000) ///< Nanoseconds per millisecond.
#endif

#ifndef NS_PER_US
#define NS_PER_US UINT16_C(1000) ///< Nanoseconds per microsecond.
#endif


// Approximate division by NS_PER_S. Only up to 0x0fffffffffffffff.
#define APPROX_NS_TO_S(x) (div_mulhi64(0x112e0be826d694b3, (x)) >> 26)

// Approximate division by NS_PER_MS. Up to 0xffffffffffffffff.
#define APPROX_NS_TO_MS(x) (div_mulhi64(0x431bde82d7b634db, (x)) >> 18)

// Approximate division by NS_PER_US. Only up to 0x0fffffffffffffff.
#define APPROX_NS_TO_US(x) (div_mulhi64(0x20c49ba5e353f7d, (x)) >> 3)

#if HAVE_64BIT
#define NS_TO_S(x) ((x) / NS_PER_S)
#define NS_TO_MS(x) ((x) / NS_PER_MS)
#define NS_TO_US(x) ((x) / NS_PER_US)
#else
#define NS_TO_S(x) APPROX_NS_TO_S(x)
#define NS_TO_MS(x) APPROX_NS_TO_MS(x)
#define NS_TO_US(x) APPROX_NS_TO_US(x)
#endif


#ifndef plural
/// Helper to pluralize output words.
///
/// @param      n     An integer value.
///
/// @return     The character 's' when @p n is 1; an empty character otherwise.
///
#define plural(n) ((n) == 1 ? "" : "s")
#endif

#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif


#if !defined(PARTICLE) && !defined(RIOT_VERSION)
#define NRM "\x1B[0m"   ///< ANSI escape sequence: reset all to normal
#define BLD "\x1B[1m"   ///< ANSI escape sequence: bold
#define DIM "\x1B[2m"   ///< ANSI escape sequence: dim
#define ULN "\x1B[3m"   ///< ANSI escape sequence: underline
#define BLN "\x1B[5m"   ///< ANSI escape sequence: blink
#define REV "\x1B[7m"   ///< ANSI escape sequence: reverse
#define HID "\x1B[8m"   ///< ANSI escape sequence: hidden
#define BLK "\x1B[30m"  ///< ANSI escape sequence: black
#define RED "\x1B[31m"  ///< ANSI escape sequence: red
#define GRN "\x1B[32m"  ///< ANSI escape sequence: green
#define YEL "\x1B[33m"  ///< ANSI escape sequence: yellow
#define BLU "\x1B[34m"  ///< ANSI escape sequence: blue
#define MAG "\x1B[35m"  ///< ANSI escape sequence: magenta
#define CYN "\x1B[36m"  ///< ANSI escape sequence: cyan
#define WHT "\x1B[37m"  ///< ANSI escape sequence: white
#define BMAG "\x1B[45m" ///< ANSI escape sequence: background magenta
#define BWHT "\x1B[47m" ///< ANSI escape sequence: background white
#else
#define NRM ""
#define BLD ""
#define DIM ""
#define ULN ""
#define BLN ""
#define REV ""
#define HID ""
#define BLK ""
#define RED ""
#define GRN ""
#define YEL ""
#define BLU ""
#define MAG ""
#define CYN ""
#define WHT ""
#define BMAG ""
#define BWHT ""
#endif


/// Dynamically adjust util_dlevel from your code to show or suppress debug
/// messages at runtime. Increasing this past what was compiled in by setting
/// DLEVEL is obviously not going to have any effect.
extern short util_dlevel;


// Set DLEVEL to the level of debug output you want to compile in support for
#ifndef DLEVEL
/// Default debug level. Can be overridden by setting the DLEVEL define in
/// CFLAGS.
#define DLEVEL DBG
#endif

/// Debug levels, decreasing severity.
///
#define CRT 0 ///< Critical
#define ERR 1 ///< Error
#define WRN 2 ///< Warning
#define NTE 3 ///< Notice
#define INF 4 ///< Informational
#define DBG 5 ///< Debug


/// Print a debug message to stderr, including a black/white indicator whether
/// the message comes from the master thread (black) or not (white), a timestamp
/// since start of the program, a coloMAG indicator for the #dlevel severity
/// level, as well as a printf()-style format string fed by further optional
/// arguments.
///
/// @param      dlevel  The #dlevel severity level of the message
/// @param      fmt     A printf()-style format string.
/// @param      ...     Subsequent arguments to be converted for output
///                     according to @p fmt.
///
extern void __attribute__((nonnull(3, 4, 6), format(printf, 6, 7)))
util_warn(const unsigned dlevel,
          const bool tstamp,
          const char * const func,
          const char * const file,
          const unsigned line,
          const char * const fmt,
          ...);


extern void __attribute__((nonnull(1, 2, 5, 6, 8), format(printf, 8, 9)))
util_rwarn(time_t * const rt0,
           unsigned int * const rcnt,
           const unsigned dlevel,
           const unsigned lps,
           const char * const func,
           const char * const file,
           const unsigned line,
           const char * const fmt,
           ...);


#ifndef NDEBUG
#include <regex.h>

#define warn(dlevel, ...)                                                      \
    do {                                                                       \
        if (unlikely(DLEVEL >= (dlevel) && util_dlevel >= (dlevel)))           \
            util_warn((dlevel), false, DLEVEL == DBG ? __func__ : "",          \
                      DLEVEL == DBG ? __FILENAME__ : "", __LINE__,             \
                      __VA_ARGS__);                                            \
    } while (0)


/// Like warn(), but always prints a timestamp.
///
/// @param      dlevel  The #dlevel severity level of the message
/// @param      fmt     A printf()-style format string.
/// @param      ...     Subsequent arguments to be converted for output
///                     according to @p fmt.
///
#define twarn(dlevel, ...)                                                     \
    do {                                                                       \
        if (unlikely(DLEVEL >= (dlevel) && util_dlevel >= (dlevel)))           \
            util_warn((dlevel), true, DLEVEL == DBG ? __func__ : "",           \
                      DLEVEL == DBG ? __FILENAME__ : "", __LINE__,             \
                      __VA_ARGS__);                                            \
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
        if (unlikely(DLEVEL >= (dlevel) && util_dlevel >= (dlevel))) {         \
            static time_t __rt0 = 0;                                           \
            unsigned int __rcnt = 0;                                           \
            util_rwarn(                                                        \
                &__rt0, &__rcnt, (dlevel), lps, DLEVEL == DBG ? __func__ : "", \
                DLEVEL == DBG ? __FILENAME__ : "", __LINE__, __VA_ARGS__);     \
        }                                                                      \
    } while (0)
#else

#define warn(...)                                                              \
    do {                                                                       \
    } while (0)
#define twarn(...)                                                             \
    do {                                                                       \
    } while (0)
#define rwarn(...)                                                             \
    do {                                                                       \
    } while (0)

#endif


/// Abort execution with a message.
///
/// @param      fmt   A printf()-style format string.
/// @param      ...   Subsequent arguments to be converted for output according
///                   to @p fmt.
///
#ifndef NDEBUG
#define die(...)                                                               \
    util_die(DLEVEL == DBG ? __func__ : "", DLEVEL == DBG ? __FILENAME__ : "", \
             __LINE__, __VA_ARGS__)
#else
#define die(...) util_die("", "", 0, "DIED")
#endif

extern void __attribute__((nonnull(1, 2, 4), noreturn, format(printf, 4, 5)))
util_die(const char * const func,
         const char * const file,
         const unsigned line,
         const char * const fmt,
         ...);


#ifdef DTHREADED
#define DTHREAD_GAP "  "
#else
#define DTHREAD_GAP ""
#endif

#if !defined(PARTICLE) && !defined(RIOT_VERSION)
#define DTIMESTAMP_GAP DTHREAD_GAP "        "
#else
#define DTIMESTAMP_GAP DTHREAD_GAP ""
#endif


/// A version of the C assert() macro that isn't disabled by NDEBUG and that
/// ties in with warn(), die() and our other debug functions.
///
/// @param      e       Expression to check. No action if @p e is true.
/// @param      fmt     A printf()-style format string.
/// @param      ...     Subsequent arguments to be converted for output
///                     according to @p fmt.
#define ensure(e, ...)                                                         \
    do {                                                                       \
        if (unlikely(!(e)))                                                    \
            die("assertion failed: \n" DTIMESTAMP_GAP #e " \n" __VA_ARGS__);   \
    } while (0)


/// A version of the C assert() macro that *is* disabled by NDEBUG and that
/// ties in with warn(), die() and our other debug functions.
///
/// @param      e       Expression to check. No action if @p e is true.
/// @param      fmt     A printf()-style format string.
/// @param      ...     Subsequent arguments to be converted for output
///
#ifndef NDEBUG
#define assure(...) ensure(__VA_ARGS__)
#else
#define assure(...)
#endif


/// Print a hexdump of the memory region given by @p ptr and @p len to stderr.
/// Also emits an ASCII representation. Uses util_hexdump internally to augment
/// the output with meta data.
///
/// @param[in]  ptr   The beginning of the memory region to hexdump.
/// @param[in]  len   The length of the memory region to hexdump.
///
#define hexdump(ptr, len)                                                      \
    util_hexdump(ptr, len, #ptr, __func__, __FILENAME__, __LINE__)

extern void __attribute__((nonnull)) util_hexdump(const void * const ptr,
                                                  const size_t len,
                                                  const char * const ptr_name,
                                                  const char * const func,
                                                  const char * const file,
                                                  const unsigned line);


extern uint64_t __attribute__((nonnull
#if defined(__clang__)
                               ,
                               no_sanitize("unsigned-integer-overflow")
#endif
                                   ))
fnv1a_64(const void * const buf, const size_t len);


extern uint32_t __attribute__((nonnull
#if defined(__clang__)
                               ,
                               no_sanitize("unsigned-integer-overflow")
#endif
                                   ))
fnv1a_32(const void * const buf, const size_t len);


extern void __attribute__((nonnull))
timespec_sub(const struct timespec * const tvp,
             const struct timespec * const uvp,
             struct timespec * const vvp);


extern uint64_t div_mulhi64(const uint64_t a, const uint64_t b);


#ifdef DSTACK
#if defined(PARTICLE)
#include <logging.h>

#define DSTACK_LOG_NEWLINE ""
#define DSTACK_LOG(...)                                                        \
    log_message(LOG_LEVEL_TRACE, LOG_MODULE_CATEGORY, &(LogAttributes){0}, 0,  \
                __VA_ARGS__)
#else
#define DSTACK_LOG_NEWLINE "\n"
#define DSTACK_LOG(...) fprintf(stderr, __VA_ARGS__)
#endif

void __cyg_profile_func_enter(void * this_fn, void * call_site);
void __cyg_profile_func_exit(void * this_fn, void * call_site);
#else
#define DSTACK_LOG_NEWLINE ""
#define DSTACK_LOG(...)                                                        \
    do {                                                                       \
    } while (0)
#endif
