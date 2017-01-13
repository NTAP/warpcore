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
#include <pthread.h>
#include <regex.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include <warpcore.h>

pthread_mutex_t _lock;
pthread_t _master;
struct timeval _epoch;


#ifndef NDEBUG

const char * const _col[] = {MAG, RED, YEL, CYN, BLU, GRN};
regex_t _comp;


/// Constructor function to initialize the debug framework before main()
/// executes.
///
static void __attribute__((constructor)) premain()
{
    // Get the current time
    gettimeofday(&_epoch, 0);

    // Initialize lock
    ensure(pthread_mutex_init(&_lock, 0) == 0, "could not initialize mutex");

    // Remember the ID of the main thread
    _master = pthread_self();

    // Initialize the regular expression used for restricting debug output
    ensure(regcomp(&_comp, DCOMPONENT, REG_EXTENDED | REG_ICASE | REG_NOSUB) ==
               0,
           "may not be a valid regexp: %s", DCOMPONENT);
}


/// Destructor function to clean up after the debug framework, before the
/// program exits.
///
static void __attribute__((destructor)) postmain()
{
    // Free the regular expression used for restricting debug output
    regfree(&_comp);

    // Free the lock
    pthread_mutex_destroy(&_lock);
}

#endif


// See the hexdump() macro.
//
extern void __attribute__((nonnull)) _hexdump(const void * const ptr,
                                              const size_t len,
                                              const char * const ptr_name,
                                              const char * const func,
                                              const char * const file,
                                              const int line)
{
    if (pthread_mutex_lock(&_lock))
        abort();
    struct timeval _now, _elapsed;
    gettimeofday(&_now, 0);
    timersub(&_now, &_epoch, &_elapsed);

    fprintf(stderr, REV "%s " NRM " %ld.%04ld " REV WHT " " NRM MAG " %s" BLK
                        " " BLU "%s:%d " NRM "hex-dumping %zu byte%s of %s\n",
            (pthread_self() == _master ? BLK : WHT), _elapsed.tv_sec % 1000,
            (long)(_elapsed.tv_usec / 1000), func, basename(file), line, len,
            plural(len), ptr_name);

    const uint8_t * const buf = ptr;
    for (size_t i = 0; i < len; i += 16) {
        fprintf(stderr, REV "%s " NRM " %ld.%04ld " REV WHT " " NRM " " BLU
                            "0x%04lx:  " NRM,
                (pthread_self() == _master ? BLK : WHT), _elapsed.tv_sec % 1000,
                (long)(_elapsed.tv_usec / 1000), i);
        for (size_t j = 0; j < 16; j += 2) {
            if (i + j < len)
                fprintf(stderr, "%02hhx%02hhx ", buf[i + j], buf[i + j + 1]);
            else
                fprintf(stderr, "     ");
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
    if (pthread_mutex_unlock(&_lock))
        abort();
}
