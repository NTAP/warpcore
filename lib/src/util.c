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

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#if defined(DSTACK)
#include <sys/param.h>
#endif

#ifdef __FreeBSD__
#include <time.h>
#endif

#include <warpcore/warpcore.h>

#if defined(HAVE_BACKTRACE) || defined(DSTACK)
#if !defined(PARTICLE) && !defined(RIOT_VERSION)
#include <dlfcn.h>
#endif
#endif

#if defined(HAVE_BACKTRACE)
#include <time.h>
#endif

#if defined(DSTACK)
#include <sys/resource.h>
#endif

#ifdef PARTICLE
#include <core_hal.h>
#include <logging.h>
#include <timer_hal.h>

int gettimeofday(struct timeval * restrict tp,
                 void * restrict tzp __attribute__((unused)))
{
    const unsigned long usec = HAL_Timer_Get_Micro_Seconds();
    tp->tv_sec = usec / US_PER_S;
    tp->tv_usec = usec % US_PER_S;
    return 0;
}

#endif

#ifdef RIOT_VERSION
#include "esp_system.h"
#endif


#ifndef NDEBUG
#ifdef DCOMPONENT
/// Default components to see debug messages from. Can be overridden by setting
/// the DCOMPONENT define to a regular expression matching the components
/// (files) you want to see debug output from.
#include <regex.h>
static regex_t util_comp;
#endif
#endif

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
    } while (0)

#define DTHREAD_UNLOCK                                                         \
    do {                                                                       \
        if (unlikely(pthread_mutex_unlock(&util_lock)))                        \
            abort();                                                           \
    } while (0)


#define DTHREAD_ID (pthread_self() == util_master ? BBLK : BWHT)

#define DTHREAD_ID_IND(bg) "%s " bg " "

#define BBLK "\x1B[40m" ///< ANSI escape sequence: background black

#else

#define DTHREAD_LOCK
#define DTHREAD_UNLOCK
#define DTHREAD_ID ""
#define DTHREAD_ID_IND(bg) "%s" bg

#endif

#ifdef HAVE_BACKTRACE
/// Stores a pointer to name of the executable, i.e., argv[0].
static const char * util_executable;
#endif


#if !defined(PARTICLE) && !defined(RIOT_VERSION)
/// Stores the timeval at the start of the program, used to print relative times
/// in warn(), die(), etc.
///
static struct timeval util_epoch;
#endif


#if !defined(PARTICLE) && !defined(RIOT_VERSION)
/// Constructor function to initialize the debug framework before main()
/// executes.
///
/// @param[in]  argc  Same argc as main().
/// @param      argv  Same argv as main().
///
static void __attribute__((constructor))
premain(const int argc __attribute__((unused)),
        char * const argv[]
#ifndef HAVE_BACKTRACE
        __attribute__((unused))
#endif
#ifdef __FreeBSD__
        __attribute__((unused))
#endif
)
{
    // Get the current time
    gettimeofday(&util_epoch, 0);

#ifdef HAVE_BACKTRACE
    // Remember executable name (musl doesn't pass argv, and FreeBSD crashes on
    // accessing it)
    util_executable =
#ifndef __FreeBSD__
        argv ? argv[0] :
#endif
             0;
#endif

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
#endif


short util_dlevel = DLEVEL;


#define BRED "\x1B[41m" ///< ANSI escape sequence: background red
#define BGRN "\x1B[42m" ///< ANSI escape sequence: background green
#define BYEL "\x1B[43m" ///< ANSI escape sequence: background yellow
#define BBLU "\x1B[44m" ///< ANSI escape sequence: background blue
#define BCYN "\x1B[46m" ///< ANSI escape sequence: background cyan


static void
    __attribute__((nonnull, no_instrument_function, format(printf, 6, 0)))
    util_warn_valist(const unsigned dlevel,
                     const bool tstamp,
                     const char * const func,
                     const char * const file,
                     const unsigned line,
                     const char * const fmt,
                     va_list ap)
{
#if !defined(PARTICLE)
#if !defined(RIOT_VERSION)
    const char * const util_col[] = {BMAG, BRED, BYEL, BCYN, BBLU, BGRN};
    DTHREAD_LOCK;

    static struct timeval last = {-1, -1};
    struct timeval now;
    struct timeval dur;
    struct timeval diff;
    gettimeofday(&now, 0);
    timersub(&now, &util_epoch, &dur);
    timersub(&now, &last, &diff);

    fprintf(stderr, DTHREAD_ID_IND(NRM), DTHREAD_ID);

    static int now_str_len = 0;
    if (tstamp || diff.tv_sec || diff.tv_usec > 1000) {
        static char now_str[32];
        now_str_len = snprintf(now_str, sizeof(now_str), "%s%ld.%03ld" NRM,
                               tstamp ? BLD : NRM,
                               (long)(dur.tv_sec % 1000), // NOLINT
                               (long)(dur.tv_usec / 1000) // NOLINT
        );
        fprintf(stderr, "%s ", now_str);
        last = now;
    } else
        // subtract out the length of the ANSI control characters
        for (int i = 0; i <= now_str_len - 8; i++)
            fputc(' ', stderr);
    fprintf(stderr, "%s " NRM " ", util_col[dlevel]);
#endif
    if (util_dlevel == DBG) {
        fprintf(stderr, MAG "%s" BLK " " BLU "%s:%u " NRM, func, file, line);
    }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
    vfprintf(stderr, fmt, ap);
#pragma clang diagnostic pop
    fputc('\n', stderr);
    fflush(stderr);
    // w_nanosleep(50 * NS_PER_MS);
    DTHREAD_UNLOCK;

#else

    const int util_level_trans[] = {
        [CRT] = LOG_LEVEL_PANIC, [ERR] = LOG_LEVEL_ERROR,
        [WRN] = LOG_LEVEL_WARN,  [NTE] = LOG_LEVEL_INFO,
        [INF] = LOG_LEVEL_INFO,  [DBG] = LOG_LEVEL_TRACE};

    LogAttributes util_attr = {sizeof(util_attr), {0}};
    if (*file)
        LOG_ATTR_SET(util_attr, file, file);
    if (line)
        LOG_ATTR_SET(util_attr, line, line);
    if (*func)
        LOG_ATTR_SET(util_attr, function, func);

    log_message_v(util_level_trans[dlevel], LOG_MODULE_CATEGORY, &util_attr, 0,
                  fmt, ap);
    // HAL_Delay_Microseconds(50 * MS_PER_S);

#endif
}


// See the warn() macro.
//
void __attribute__((no_instrument_function)) util_warn(const unsigned dlevel,
                                                       const bool tstamp,
                                                       const char * const func,
                                                       const char * const file,
                                                       const unsigned line,
                                                       const char * const fmt,
                                                       ...)
{
#ifdef DCOMPONENT
    if (regexec(&util_comp, file, 0, 0, 0))
        return;
#endif
    va_list ap;
    va_start(ap, fmt);
    util_warn_valist(dlevel, tstamp, func, file, line, fmt, ap);
    va_end(ap);
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


// See the die() macro.
//
void util_die(const char * const func,
              const char * const file,
              const unsigned line,
              const char * const fmt,
              ...)
{
#if !defined(PARTICLE)
    va_list ap;
    va_start(ap, fmt);
    const int e = errno;
#if !defined(RIOT_VERSION)
    DTHREAD_LOCK;
    struct timeval now = {0, 0};
    struct timeval dur = {0, 0};
    gettimeofday(&now, 0);
    timersub(&now, &util_epoch, &dur);
    fprintf(stderr, DTHREAD_ID_IND(BMAG) WHT BLD "%ld.%03ld   ", DTHREAD_ID,
            (long)(dur.tv_sec % 1000), // NOLINT
            (long)(dur.tv_usec / 1000) // NOLINT
    );
#endif
    fprintf(stderr, "%s %s:%u ABORT: ", func, file, line);

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
                    fprintf(stderr, DTIMESTAMP_GAP BLU "%s" NRM, info);
                }
                pclose(fp);
            }

            if (translated == false)
                fprintf(stderr, DTIMESTAMP_GAP "%s\n", bt_sym[j]);
        }
        free(bt_sym);
    }
#endif

    fflush(stderr);
    va_end(ap);
    DTHREAD_UNLOCK;
    w_nanosleep(1 * NS_PER_S);

#else

#ifndef NDEBUG
    va_list ap;
    va_start(ap, fmt);
    util_warn_valist(CRT, false, func, file, line, fmt, ap);
    va_end(ap);
    w_nanosleep(1 * NS_PER_S);
#endif
    panic_(NotUsedPanicCode, 0, HAL_Delay_Microseconds);

#endif

    abort();
}


#ifndef PARTICLE
#define hexlog(...) fprintf(stderr, __VA_ARGS__)
#else
#define hexlog(...) LOG_PRINTF(ERROR, __VA_ARGS__)
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
#ifndef PARTICLE
    DTHREAD_LOCK;
#ifndef RIOT_VERSION
    struct timeval now;
    struct timeval elapsed;
    gettimeofday(&now, 0);
    timersub(&now, &util_epoch, &elapsed);

    hexlog(DTHREAD_ID_IND(NRM) "%ld.%03lld " BWHT " " NRM MAG " %s" BLK " " BLU
                               "%s:%u " NRM,
           DTHREAD_ID, elapsed.tv_sec % 1000,
           (long long)(elapsed.tv_usec / 1000), func, file, line);
#endif
#endif

    hexlog("hex-dumping %lu byte%s of %s from %p\n", (unsigned long)len,
           plural(len), ptr_name, ptr);

    const uint8_t * const buf = ptr;
    for (size_t i = 0; i < len; i += 16) {
#if !defined(PARTICLE) && !defined(RIOT_VERSION)
        hexlog(DTHREAD_ID_IND(NRM) "%ld.%03lld " BWHT " " NRM " " BLU,
               DTHREAD_ID, elapsed.tv_sec % 1000,
               (long long)(elapsed.tv_usec / 1000));
#endif
        hexlog("0x%04lx:  " NRM, (unsigned long)i);

        for (size_t j = 0; j < 16; j++) {
            if (i + j < len)
                hexlog("%02x", buf[i + j]);
            else
                hexlog("  ");
            if (j % 2)
                hexlog(" ");
        }
        hexlog(" " GRN);
        for (size_t j = 0; j < 16; j++) {
            if (i + j < len)
                hexlog("%c", isprint(buf[i + j]) ? buf[i + j] : '.');
        }
        hexlog(NRM "\n");
    }

#ifndef PARTICLE
    fflush(stderr);
    DTHREAD_UNLOCK;
#endif
}


/// Compute an [FNV-1a 64-bit
/// hash](http://www.isthe.com/chongo/tech/comp/fnv/index.html) over the given
/// buffer.
///
/// @param      buf       The buffer.
/// @param      len       The length of @p buf.
///
/// @return     The FNV-1a 64-bit hash of @p buffer.
///
uint64_t fnv1a_64(const void * const buf, const size_t len)
{
    const uint64_t prime = 0x100000001b3;
    uint64_t hash = 0xcbf29ce484222325;

    const uint8_t * const bytes = (const uint8_t * const)buf;
    for (size_t i = 0; i < len; i++) {
        hash ^= bytes[i];
        hash *= prime;
    }
    return hash;
}


/// Compute an [FNV-1a 32-bit
/// hash](http://www.isthe.com/chongo/tech/comp/fnv/index.html) over the given
/// buffer.
///
/// @param      buf       The buffer.
/// @param      len       The length of @p buf.
///
/// @return     The FNV-1a 32-bit hash of @p buffer.
///
uint32_t fnv1a_32(const void * const buf, const size_t len)
{
    const uint32_t prime = 0x811c9dc5;
    uint32_t hash = 0x1000193;

    const uint8_t * const bytes = (const uint8_t * const)buf;
    for (size_t i = 0; i < len; i++) {
        hash ^= bytes[i];
        hash *= prime;
    }
    return hash;
}


/// Subtract the struct timespec values @p tvp and @p uvp (tvp - uvp), storing
/// the result in @p vvp. Inspired by timersub().
///
/// @param[in]  tvp   The minuend.
/// @param[in]  uvp   The subtrahend.
/// @param[out] vvp   The difference.
///
void timespec_sub(const struct timespec * const tvp,
                  const struct timespec * const uvp,
                  struct timespec * const vvp)
{
    vvp->tv_sec = tvp->tv_sec - uvp->tv_sec;
    vvp->tv_nsec = tvp->tv_nsec - uvp->tv_nsec;
    if (vvp->tv_nsec < 0) {
        vvp->tv_sec--;
        vvp->tv_nsec += (long)NS_PER_S;
    }
}


// see
// https://stackoverflow.com/questions/28868367/getting-the-high-part-of-64-bit-integer-multiplication

uint64_t div_mulhi64(const uint64_t a, const uint64_t b)
{
    const uint32_t a_lo = (const uint32_t)a;
    const uint32_t a_hi = (const uint32_t)(a >> 32);
    const uint32_t b_lo = (const uint32_t)b;
    const uint32_t b_hi = (const uint32_t)(b >> 32);

    const uint64_t a_x_b_mid = (const uint64_t)a_hi * b_lo;
    const uint64_t b_x_a_mid = (const uint64_t)b_hi * a_lo;
    const uint64_t a_x_b_lo = (const uint64_t)a_lo * b_lo;
    const uint64_t a_x_b_hi = (const uint64_t)a_hi * b_hi;

    const uint32_t carry_bits =
        ((uint64_t)(uint32_t)a_x_b_mid + (uint64_t)(uint32_t)b_x_a_mid +
         (a_x_b_lo >> 32)) >>
        32;

    const uint64_t mulhi =
        a_x_b_hi + (a_x_b_mid >> 32) + (b_x_a_mid >> 32) + carry_bits;

    return mulhi;
}


#ifdef DSTACK
static uint_t stack_lim = 0;
static uint_t max_stack = 0;
static uint_t heap_lim = 0;
static uint_t dstack_depth = 0;

#if defined(RIOT_VERSION)
extern uint8_t _sheap;
extern uint8_t _eheap;
#endif


void __attribute__((no_instrument_function))
__cyg_profile_func_enter(void * this_fn,
                         void * call_site __attribute__((unused)))
{
    static const char * stack_start = 0;
    dstack_depth++;
    const char * const frame = __builtin_frame_address(0);
    if (unlikely(stack_lim == 0)) {
        stack_start = frame;
#if defined(PARTICLE)
        stack_lim = 6144; // TODO: can this be determined dynamically?
#elif defined(RIOT_VERSION)
        stack_lim = 8192; // TODO: can this be determined dynamically?
        heap_lim = &_eheap - &_sheap;
#else
        struct rlimit lim;
        getrlimit(RLIMIT_STACK, &lim);
        stack_lim = lim.rlim_cur;
#endif
    }

    uint_t heap = 0;
#if defined(PARTICLE)
    runtime_info_t info = {.size = sizeof(info)};
    HAL_Core_Runtime_Info(&info, NULL);
    heap = info.freeheap;
    heap_lim = info.total_init_heap;
#elif defined(RIOT_VERSION)
    heap = esp_get_free_heap_size();
#else
    stack_start = MAX(stack_start, frame);
#endif

    const uint_t stack = (uint_t)(stack_start - frame);

#if !defined(PARTICLE) && !defined(RIOT_VERSION)
    Dl_info info;
    dladdr(this_fn, &info);
    DSTACK_LOG("%s" DSTACK_LOG_NEWLINE, info.dli_sname);
#endif
    DSTACK_LOG("s=%" PRIu " h=%" PRIu " l=%" PRIu DSTACK_LOG_NEWLINE, stack,
               heap, dstack_depth);

    if (stack < UINT16_MAX)
        max_stack = MAX(max_stack, stack);
}

void __attribute__((no_instrument_function))
__cyg_profile_func_exit(void * this_fn, void * call_site)
{
    dstack_depth -= 2;
    __cyg_profile_func_enter(this_fn, call_site);
    if (unlikely(dstack_depth == 0))
        DSTACK_LOG("ms=%" PRIu " sl=%" PRIu " hl=%" PRIu DSTACK_LOG_NEWLINE,
                   max_stack, stack_lim, heap_lim);
}
#endif
