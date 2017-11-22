
// SPDX-License-Identifier: BSD-2-Clause
//
// Copyright (c) 2014-2017, NetApp, Inc.
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

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#if !defined(NDEBUG)
#include <regex.h>
static regex_t util_comp;

#ifdef DCOMPONENT
/// Default components to see debug messages from. Can be overridden by setting
/// the DCOMPONENT define to a regular expression matching the components
/// (files) you want to see debug output from.
#define DO_REGEXEC 1
#else
#define DO_REGEXEC 0
#endif
#endif

#include <warpcore/warpcore.h>

#ifdef DTHREADED
#include <pthread.h>

/// Lock to prevent multiple threads from corrupting each others output.
///
static pthread_mutex_t util_lock;

/// Thread ID of the master thread.
///
static pthread_t util_master;

#define DTHREAD_LOCK                                                           \
    do {                                                                       \
        if (unlikely(pthread_mutex_lock(&util_lock)))                          \
            abort();                                                           \
    } while (0) // NOLINT

#define DTHREAD_UNLOCK                                                         \
    do {                                                                       \
        if (unlikely(pthread_mutex_unlock(&util_lock)))                        \
            abort();                                                           \
    } while (0) // NOLINT


#define DTHREAD_ID (pthread_self() == util_master ? BBLK : BWHT),

#define DTHREAD_ID_IND(bg) "%s " bg " "

#define BBLK "\x1B[40m" ///< ANSI escape sequence: background black

#else

#define DTHREAD_LOCK
#define DTHREAD_UNLOCK
#define DTHREAD_ID
#define DTHREAD_ID_IND(bg) bg

#endif


/// Stores the timeval at the start of the program, used to print relative times
/// in warn(), die(), etc.
///
static struct timeval util_epoch;


#define NRM "\x1B[0m" ///< ANSI escape sequence: reset all to normal
#define BLD "\x1B[1m" ///< ANSI escape sequence: bold
// #define DIM "\x1B[2m"   ///< ANSI escape sequence: dim
// #define ULN "\x1B[3m"   ///< ANSI escape sequence: underline
// #define BLN "\x1B[5m"   ///< ANSI escape sequence: blink
// #define REV "\x1B[7m"   ///< ANSI escape sequence: reverse
// #define HID "\x1B[8m"   ///< ANSI escape sequence: hidden
#define BLK "\x1B[30m" ///< ANSI escape sequence: black
// #define RED "\x1B[31m"  ///< ANSI escape sequence: red
#define GRN "\x1B[32m" ///< ANSI escape sequence: green
// #define YEL "\x1B[33m"  ///< ANSI escape sequence: yellow
#define BLU "\x1B[34m" ///< ANSI escape sequence: blue
#define MAG "\x1B[35m" ///< ANSI escape sequence: magenta
// #define CYN "\x1B[36m"  ///< ANSI escape sequence: cyan
#define WHT "\x1B[37m"  ///< ANSI escape sequence: white
#define BMAG "\x1B[45m" ///< ANSI escape sequence: background magenta
#define BWHT "\x1B[47m" ///< ANSI escape sequence: background white


/// Constructor function to initialize the debug framework before main()
/// executes.
///
static void __attribute__((constructor)) premain()
{
    // Get the current time
    gettimeofday(&util_epoch, 0);

#ifdef DTHREADED
    // Initialize a recursive logging lock
    pthread_mutexattr_t attr;
    ensure(pthread_mutexattr_init(&attr) == 0,
           "could not initialize mutex attr");
    ensure(pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) == 0,
           "could not set mutex attr");
    ensure(pthread_mutex_init(&util_lock, &attr) == 0,
           "could not initialize mutex");
    ensure(pthread_mutexattr_destroy(&attr) == 0,
           "could not destroy mutex attr");

    // Remember the ID of the main thread
    util_master = pthread_self();
#endif

#if !defined(NDEBUG) && defined(DCOMPONENT)
    // Initialize the regular expression used for restricting debug output
    ensure(regcomp(&util_comp, DCOMPONENT,
                   REG_EXTENDED | REG_ICASE | REG_NOSUB) == 0,
           "may not be a valid regexp: %s", DCOMPONENT);
#endif
}


/// Destructor function to clean up after the debug framework, before the
/// program exits.
///
static void __attribute__((destructor)) postmain()
{
#if !defined(NDEBUG) && defined(DCOMPONENT)
    // Free the regular expression used for restricting debug output
    regfree(&util_comp);
#endif

#ifdef DTHREADED
    // Free the lock
    pthread_mutex_destroy(&util_lock);
#endif
}


// See the die() macro.
//
void util_die(const char * const func,
              const char * const file,
              const unsigned line,
              ...)
{
    DTHREAD_LOCK;
    va_list ap;
    va_start(ap, line);
    const int e = errno;
    struct timeval now = {0, 0}, dur = {0, 0};
    gettimeofday(&now, 0);
    timersub(&now, &util_epoch, &dur);
    fprintf(stderr, DTHREAD_ID_IND(BMAG) WHT BLD "%ld.%03ld   %s %s:%u ABORT: ",
            DTHREAD_ID(long)(dur.tv_sec % 1000), // NOLINT
            (long)(dur.tv_usec / 1000),          // NOLINT
            func, basename(file), line);
    char * fmt = va_arg(ap, char *);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
    vfprintf(stderr, fmt, ap);
#pragma clang diagnostic pop
    fprintf(stderr, " %c%s%c\n" NRM, (e ? '[' : 0), (e ? strerror(e) : ""),
            (e ? ']' : 0));
    fflush(stderr);
    va_end(ap);
    DTHREAD_UNLOCK;
    abort();
}


#ifndef NDEBUG

#define BRED "\x1B[41m" ///< ANSI escape sequence: background red
#define BGRN "\x1B[42m" ///< ANSI escape sequence: background green
#define BYEL "\x1B[43m" ///< ANSI escape sequence: background yellow
#define BBLU "\x1B[44m" ///< ANSI escape sequence: background blue
#define BCYN "\x1B[46m" ///< ANSI escape sequence: background cyan


short util_dlevel = DLEVEL;

static void __attribute__((nonnull)) util_warn_valist(const unsigned dlevel,
                                                      const char * const func,
                                                      const char * const file,
                                                      const unsigned line,
                                                      va_list ap)
{
    DTHREAD_LOCK;
    struct timeval now = {0, 0}, dur = {0, 0};
    gettimeofday(&now, 0);
    timersub(&now, &util_epoch, &dur);
    const char * const util_col[] = {BMAG, BRED, BYEL, BCYN, BBLU, BGRN};
    fprintf(stderr,
            DTHREAD_ID_IND(NRM) "%ld.%03ld %s " NRM MAG " %s" BLK " " BLU
                                "%s:%u " NRM,
            DTHREAD_ID(long)(dur.tv_sec % 1000), // NOLINT
            (long)(dur.tv_usec / 1000),          // NOLINT
            util_col[dlevel], func, basename(file), line);
    char * fmt = va_arg(ap, char *);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
    vfprintf(stderr, fmt, ap);
#pragma clang diagnostic pop
    fprintf(stderr, "\n");
    fflush(stderr);
    DTHREAD_UNLOCK;
}


// See the warn() macro.
//
void util_warn(const unsigned dlevel,
               const char * const func,
               const char * const file,
               const unsigned line,
               ...)
{
    if (DO_REGEXEC ? !regexec(&util_comp, file, 0, 0, 0) : 1) {
        va_list ap;
        va_start(ap, line);
        util_warn_valist(dlevel, func, file, line, ap);
        va_end(ap);
    }
}


// See the rwarn() macro.
//
void util_rwarn(const unsigned dlevel,
                const unsigned lps,
                const char * const func,
                const char * const file,
                const unsigned line,
                ...)
{
    static time_t rt0, rcnt;
    struct timeval rts = {0, 0};
    gettimeofday(&rts, 0);
    if (rt0 != rts.tv_sec) {
        rt0 = rts.tv_sec;
        rcnt = 0;
    }
    if (rcnt++ < lps &&
        (DO_REGEXEC ? !regexec(&util_comp, file, 0, 0, 0) : 1)) {
        va_list ap;
        va_start(ap, line);
        util_warn_valist(dlevel, func, file, line, ap);
        va_end(ap);
    }
}
#endif


// See the hexdump() macro.
//
void util_hexdump(const void * const ptr,
                  const size_t len,
                  const char * const ptr_name,
                  const char * const func,
                  const char * const file,
                  const unsigned line)
{
    DTHREAD_LOCK;
    struct timeval now, elapsed;
    gettimeofday(&now, 0);
    timersub(&now, &util_epoch, &elapsed);

    fprintf(stderr,
            DTHREAD_ID_IND(NRM) "%ld.%03lld " BWHT " " NRM MAG " %s" BLK " " BLU
                                "%s:%u " NRM
                                "hex-dumping %zu byte%s of %s from %p\n",
            DTHREAD_ID elapsed.tv_sec % 1000,
            (long long)(elapsed.tv_usec / 1000), func, basename(file), line,
            len, plural(len), ptr_name, ptr);

    const uint8_t * const buf = ptr;
    for (size_t i = 0; i < len; i += 16) {
        fprintf(stderr,
                DTHREAD_ID_IND(NRM) "%ld.%03lld " BWHT " " NRM " " BLU
                                    "0x%04lx:  " NRM,
                DTHREAD_ID elapsed.tv_sec % 1000,
                (long long)(elapsed.tv_usec / 1000), i);
        for (size_t j = 0; j < 16; j++) {
            if (i + j < len)
                fprintf(stderr, "%02hhx", buf[i + j]);
            else
                fprintf(stderr, "  ");
            if (j % 2)
                fprintf(stderr, " ");
        }
        fprintf(stderr, " ");
        for (size_t j = 0; j < 16; j++) {
            if (i + j < len)
                fprintf(stderr, GRN "%c",
                        isprint(buf[i + j]) ? buf[i + j] : '.');
        }
        fprintf(stderr, "\n");
    }

    fflush(stderr);
    DTHREAD_UNLOCK;
}
