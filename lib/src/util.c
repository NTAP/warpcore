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

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#ifndef __FreeBSD__
#include <dlfcn.h>
#include <time.h>
#endif

#if !defined(NDEBUG)
#ifdef DCOMPONENT
/// Default components to see debug messages from. Can be overridden by setting
/// the DCOMPONENT define to a regular expression matching the components
/// (files) you want to see debug output from.
#include <regex.h>
static regex_t util_comp;
#endif
#endif

#include <warpcore/warpcore.h>

#ifdef HAVE_BACKTRACE
#include <execinfo.h>
#endif

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


#define DTHREAD_ID (pthread_self() == util_master ? BBLK : BWHT)

#define DTHREAD_ID_IND(bg) "%s " bg " "

#define BBLK "\x1B[40m" ///< ANSI escape sequence: background black

#else

#define DTHREAD_LOCK
#define DTHREAD_UNLOCK
#define DTHREAD_ID ""
#define DTHREAD_ID_IND(bg) "%s" bg

#endif


/// Stores a pointer to name of the executable, i.e., argv[0].
static const char * util_executable;


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
/// @param[in]  argc  Same argc as main().
/// @param      argv  Same argv as main().
///
static void __attribute__((constructor))
premain(const int argc __attribute__((unused)),
        char * const argv[]
#ifdef __FreeBSD__
        __attribute__((unused))
#endif
)
{
    // Get the current time
    gettimeofday(&util_epoch, 0);

    // Remember executable name (musl doesn't pass argv, and FreeBSD crashes on
    // accessing it)
    util_executable =
#ifndef __FreeBSD__
        argv ? argv[0] :
#endif
             0;

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
              const char * const fmt,
              ...)
{
    DTHREAD_LOCK;
    va_list ap;
    va_start(ap, fmt);
    const int e = errno;
    struct timeval now = {0, 0};
    struct timeval dur = {0, 0};
    gettimeofday(&now, 0);
    timersub(&now, &util_epoch, &dur);
    fprintf(stderr, DTHREAD_ID_IND(BMAG) WHT BLD "%ld.%03ld   %s %s:%u ABORT: ",
            DTHREAD_ID, (long)(dur.tv_sec % 1000), // NOLINT
            (long)(dur.tv_usec / 1000),            // NOLINT
            func, basename(file), line);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
    vfprintf(stderr, fmt, ap);
#pragma clang diagnostic pop
    fprintf(stderr, " %serrno %d = %s%s" NRM "\n", (e ? "[" : ""), e,
            (e ? strerror(e) : ""), (e ? "]" : ""));

#ifdef HAVE_BACKTRACE
    if (util_executable) {
        void * bt_buf[128];
        const int n = backtrace(bt_buf, sizeof(bt_buf));
        char ** const bt_sym = backtrace_symbols(bt_buf, n);
        for (int j = 0; j < n; j++) {
            Dl_info dli;
            dladdr(bt_buf[j], &dli);
            bool translated = false;

            // on some platforms, dli_fname is an absolute path
            if (strlen(dli.dli_fname) > strlen(util_executable))
                // only compare final path components
                dli.dli_fname +=
                    strlen(dli.dli_fname) - strlen(util_executable);

            if (strcmp(util_executable, dli.dli_fname) == 0) {
                char cmd[8192];
#ifdef __APPLE__
                snprintf(cmd, sizeof(cmd),
                         "atos -fullPath -o %s -l %p %p 2> /dev/null",
                         util_executable, dli.dli_fbase, bt_buf[j]);
#else
                snprintf(cmd, sizeof(cmd),
                         "addr2line -C -f -i -p -e %s %p 2> /dev/null",
                         util_executable,
                         (void *)((char *)bt_buf[j] - (char *)dli.dli_fbase));
#endif
                FILE * const fp = popen(cmd, "r"); // NOLINT
                char info[8192];
                while (fgets(info, sizeof(info), fp)) {
                    translated = true;
                    fprintf(stderr, DTHREAD_GAP BLU "%s" NRM, info);
                }
                pclose(fp);
            }

            if (translated == false)
                fprintf(stderr, DTHREAD_GAP "%s\n", bt_sym[j]);
        }
        free(bt_sym);
    }
#endif

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

static void __attribute__((nonnull, , format(printf, 6, 0)))
util_warn_valist(const unsigned dlevel,
                 const bool tstamp,
                 const char * const func,
                 const char * const file,
                 const unsigned line,
                 const char * const fmt,
                 va_list ap)
{
    const char * const util_col[] = {BMAG, BRED, BYEL, BCYN, BBLU, BGRN};
    DTHREAD_LOCK;

    static struct timeval last = {-1, -1};
    struct timeval now = {0, 0};
    struct timeval dur = {0, 0};
    struct timeval diff = {0, 0};
    gettimeofday(&now, 0);
    timersub(&now, &util_epoch, &dur);
    timersub(&now, &last, &diff);

    fprintf(stderr, DTHREAD_ID_IND(NRM), DTHREAD_ID);

    static char now_str[32];
    if (tstamp || diff.tv_sec || diff.tv_usec > 1000) {
        snprintf(now_str, sizeof(now_str), "%s%ld.%03ld" NRM,
                 tstamp ? BLD : NRM,
                 (long)(dur.tv_sec % 1000), // NOLINT
                 (long)(dur.tv_usec / 1000) // NOLINT
        );
        fprintf(stderr, "%s ", now_str);
        last = now;
    } else
        // subtract out the length of the ANSI control characters
        for (size_t i = 0; i <= strlen(now_str) - 8; i++)
            fputc(' ', stderr);
    fprintf(stderr, "%s " NRM " ", util_col[dlevel]);

    if (util_dlevel == DBG)
        fprintf(stderr, MAG "%s" BLK " " BLU "%s:%u " NRM, func, basename(file),
                line);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
    vfprintf(stderr, fmt, ap);
#pragma clang diagnostic pop
    fputc('\n', stderr);
    fflush(stderr);
    DTHREAD_UNLOCK;
}


// See the warn() macro.
//
void util_warn(const unsigned dlevel,
               const bool tstamp,
               const char * const func,
               const char * const file,
               const unsigned line,
               const char * const fmt,
               ...)
{
#ifdef DCOMPONENT
    if (!regexec(&util_comp, file, 0, 0, 0)) {
#endif
        va_list ap;
        va_start(ap, fmt);
        util_warn_valist(dlevel, tstamp, func, file, line, fmt, ap);
        va_end(ap);
#ifdef DCOMPONENT
    }
#endif
}


// See the rwarn() macro.
//
void util_rwarn(time_t * const rt0,
                unsigned int * const rcnt,
                const unsigned dlevel,
                const unsigned lps,
                const char * const func,
                const char * const file,
                const unsigned line,
                const char * const fmt,
                ...)
{
    struct timeval rts = {0, 0};
    gettimeofday(&rts, 0);
    if (*rt0 != rts.tv_sec) {
        *rt0 = rts.tv_sec;
        *rcnt = 0;
    }
    if ((*rcnt)++ < lps
#ifdef DCOMPONENT
        && !regexec(&util_comp, file, 0, 0, 0)
#endif
    ) {
        va_list ap;
        va_start(ap, fmt);
        util_warn_valist(dlevel, true, func, file, line, fmt, ap);
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
    struct timeval now;
    struct timeval elapsed;
    gettimeofday(&now, 0);
    timersub(&now, &util_epoch, &elapsed);

    fprintf(stderr,
            DTHREAD_ID_IND(NRM) "%ld.%03lld " BWHT " " NRM MAG " %s" BLK " " BLU
                                "%s:%u " NRM
                                "hex-dumping %zu byte%s of %s from %p\n",
            DTHREAD_ID, elapsed.tv_sec % 1000,
            (long long)(elapsed.tv_usec / 1000), func, basename(file), line,
            len, plural(len), ptr_name, ptr);

    const uint8_t * const buf = ptr;
    for (size_t i = 0; i < len; i += 16) {
        fprintf(stderr,
                DTHREAD_ID_IND(NRM) "%ld.%03lld " BWHT " " NRM " " BLU
                                    "0x%04lx:  " NRM,
                DTHREAD_ID, elapsed.tv_sec % 1000,
                (long long)(elapsed.tv_usec / 1000), (unsigned long)i);
        for (size_t j = 0; j < 16; j++) {
            if (i + j < len)
                fprintf(stderr, "%02hhx", buf[i + j]);
            else
                fprintf(stderr, "  ");
            if (j % 2)
                fputc(' ', stderr);
        }
        fprintf(stderr, " " GRN);
        for (size_t j = 0; j < 16; j++) {
            if (i + j < len)
                fputc(isprint(buf[i + j]) ? buf[i + j] : '.', stderr);
        }
        fprintf(stderr, NRM "\n");
    }

    fflush(stderr);
    DTHREAD_UNLOCK;
}
