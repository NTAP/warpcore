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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <warpcore/warpcore.h>

#define r(m) (int)((m) == 0 ? 0 : w_rand_uniform32((uint32_t)(m)))


struct elem {
    sq_entry(elem) next;
    int n;
    int _unused;
};

#define N 10

static sq_head(sq, elem) sq[N];
static int len[N] = {0};
static int cnt = 0;


static void ins(int head)
{
    struct elem * const e = malloc(sizeof(*e));
    const int n = r(N);
    // printf("i[%d]=%d ", n, cnt);
    e->n = cnt++;
    if (head)
        sq_insert_head(&sq[n], e, next);
    else
        sq_insert_tail(&sq[n], e, next);

    len[n]++;
}


static void ins_aft(void)
{
    const int n = r(N);
    const int p = r(len[n]);

    struct elem * e = sq_first(&sq[n]);
    for (int i = 0; i < p; i++)
        e = sq_next(e, next);

    if (e) {
        struct elem * const enew = malloc(sizeof(*e));
        enew->n = cnt++;
        sq_insert_after(&sq[n], e, enew, next);

        len[n]++;
        // printf("ia[%d]=%d ", n, enew->n);
    }
}


static void rem(void)
{
    const int n = r(N);
    const int p = r(len[n]);

    struct elem * e = sq_first(&sq[n]);
    for (int i = 0; i < p; i++)
        e = sq_next(e, next);

    if (e) {
        sq_remove(&sq[n], e, elem, next);

        len[n]--;
        // printf("r[%d]=%d ", n, e->n);
        free(e);
    }
}


static void ini(void)
{
    const int n = r(N);
    struct elem * e;
    struct elem * t;
    sq_foreach_safe (e, &sq[n], next, t)
        free(e);
    sq_init(&sq[n]);

    len[n] = 0;
    // printf("x[%d] ", n);
}


static void swp(void)
{
    const int n1 = r(N);
    const int n2 = r(N);
    sq_swap(&sq[n1], &sq[n2], elem);

    const int l = len[n1];
    len[n1] = len[n2];
    len[n2] = l;
    // printf("s[%d=%d] ", n1, n2);
}


static void con(void)
{
    const int n1 = r(N);
    const int n2 = r(N);
    sq_concat(&sq[n1], &sq[n2]);

    len[n1] += len[n2];
    len[n2] = 0;
    // printf("c[%d<%d] ", n1, n2);
}


static void show(void)
{
    // printf("\n");
    for (int i = 0; i < N; i++) {
        int l = 0;
        printf("%d: ", i);
        struct elem * e;
        sq_foreach (e, &sq[i], next) {
            l++;
            printf("%d ", e->n);
        }
        printf("\n");
        ensure(l == len[i], "len[%d] %d wrong (should be %d)", i, len[i], l);
        ensure((uint_t)l == sq_len(&sq[i]),
               "sq_len[%d] %" PRIu " wrong (should be %d)", i, sq_len(&sq[i]),
               l);
    }
    printf("\n");
}


int main(void)
{
    w_init_rand();

    for (int i = 0; i < N; i++)
        sq_init(&sq[i]);

    for (int i = 0; i < 50000; i++) {
        switch (r(6)) {
        case 0:
            ins(0);
            break;
        case 1:
            ins(1);
            break;
        case 2:
            ins_aft();
            break;
        case 3:
            rem();
            break;
        case 4:
            con();
            break;
        case 5:
            swp();
            break;
        }

        if (r(1000) == 0) {
            show();
            ini();
        }
    }
}
