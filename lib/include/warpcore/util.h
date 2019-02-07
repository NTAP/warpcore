// SPDX-License-Identifier: BSD-2-Clause
//
// Copyright (c) 2014-2019, NetApp, Inc.
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
#include <time.h>

/// Trim the path from the given file name. Mostly to be used with __FILE__.
///
/// @param      f     An (absolute) file name, to trim.
///
/// @return     The standalone file name (no path).
///
#ifndef basename
#include <string.h>
#define basename(f) (strrchr((f), '/') ? strrchr((f), '/') + 1 : (f))
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
#ifndef NDEBUG
// cppcheck gets confused by __builtin_expect()
#define likely(x) (x)
#define unlikely(x) (x)
#else
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif
#endif


/// Abort execution with a message.
///
/// @param      fmt   A printf()-style format string.
/// @param      ...   Subsequent arguments to be converted for output according
///                   to @p fmt.
///
#define die(...) util_die(__func__, __FILE__, __LINE__, __VA_ARGS__)

extern void __attribute__((nonnull(1, 2, 4), noreturn, format(printf, 4, 5)))
util_die(const char * const func,
         const char * const file,
         const unsigned line,
         const char * const fmt,
         ...);


#ifndef NDEBUG

#include <regex.h>

/// Debug levels, decreasing severity.
///
#define CRT 0 ///< Critical
#define ERR 1 ///< Error
#define WRN 2 ///< Warning
#define NTE 3 ///< Notice
#define INF 4 ///< Informational
#define DBG 5 ///< Debug

// Set DLEVEL to the level of debug output you want to compile in support for
#ifndef DLEVEL
/// Default debug level. Can be overridden by setting the DLEVEL define in
/// CFLAGS.
#define DLEVEL DBG
#endif

/// Dynamically adjust util_dlevel from your code to show or suppress debug
/// messages at runtime. Increasing this past what was compiled in by setting
/// DLEVEL is obviously not going to have any effect.
extern short util_dlevel;


// These macros are based on the "D" ones defined by netmap

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
#define warn(dlevel, ...)                                                      \
    do {                                                                       \
        if (unlikely(DLEVEL >= dlevel && util_dlevel >= dlevel)) {             \
            util_warn(dlevel, false, __func__, __FILE__, __LINE__,             \
                      __VA_ARGS__);                                            \
        }                                                                      \
    } while (0) // NOLINT


/// Like warn(), but always prints a timestamp.
///
/// @param      dlevel  The #dlevel severity level of the message
/// @param      fmt     A printf()-style format string.
/// @param      ...     Subsequent arguments to be converted for output
///                     according to @p fmt.
///
#define twarn(dlevel, ...)                                                     \
    do {                                                                       \
        if (unlikely(DLEVEL >= dlevel && util_dlevel >= dlevel)) {             \
            util_warn(dlevel, true, __func__, __FILE__, __LINE__,              \
                      __VA_ARGS__);                                            \
        }                                                                      \
    } while (0) // NOLINT


extern void __attribute__((nonnull(3, 4, 6), format(printf, 6, 7)))
util_warn(const unsigned dlevel,
          const bool tstamp,
          const char * const func,
          const char * const file,
          const unsigned line,
          const char * const fmt,
          ...);


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
        if (unlikely(DLEVEL >= dlevel && util_dlevel >= dlevel)) {             \
            static time_t __rt0;                                               \
            unsigned int __rcnt;                                               \
            util_rwarn(&__rt0, &__rcnt, dlevel, lps, __func__, __FILE__,       \
                       __LINE__, __VA_ARGS__);                                 \
        }                                                                      \
    } while (0) // NOLINT


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

#else

#define warn(...)                                                              \
    do {                                                                       \
    } while (0) // NOLINT

#define twarn(...)                                                             \
    do {                                                                       \
    } while (0) // NOLINT

#define rwarn(...)                                                             \
    do {                                                                       \
    } while (0) // NOLINT

#endif


#ifdef DTHREADED
#define DTHREAD_GAP "          "
#else
#define DTHREAD_GAP "        "
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
            die("assertion failed \n" DTHREAD_GAP #e                           \
                " \n" DTHREAD_GAP __VA_ARGS__);                                \
    } while (0) // NOLINT


/// Print a hexdump of the memory region given by @p ptr and @p len to stderr.
/// Also emits an ASCII representation. Uses util_hexdump internally to augment
/// the output with meta data.
///
/// @param[in]  ptr   The beginning of the memory region to hexdump.
/// @param[in]  len   The length of the memory region to hexdump.
///
#define hexdump(ptr, len)                                                      \
    util_hexdump(ptr, len, #ptr, __func__, __FILE__, __LINE__)

extern void __attribute__((nonnull)) util_hexdump(const void * const ptr,
                                                  const size_t len,
                                                  const char * const ptr_name,
                                                  const char * const func,
                                                  const char * const file,
                                                  const unsigned line);
