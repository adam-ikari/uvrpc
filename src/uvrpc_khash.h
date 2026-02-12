/* The MIT License

   Copyright (c) 2008-2023 by Attractive Chaos <attractivechaos@outlook.com>

   Permission is hereby granted, free of charge, to any person obtaining
   a copy of this software and associated documentation files (the
   "Software"), to deal in the Software without restriction, including
   without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense, and/or sell copies of the Software, and to
   permit persons to whom the Software is furnished to do so, subject to
   the following conditions:

   The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
   BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, CONTRACT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.
*/

#ifndef __AC_KHASH_H
#define __AC_KHASH_H

#define AC_VERSION_KHASH_H "0.3.1"

#include <stdlib.h>
#include <string.h>
#include <limits.h>

#if UINTPTR_MAX == 0xffffffffUL
#define KH_INT32_T uint32_t
#else
#define KH_INT32_T uint64_t
#endif

typedef uint32_t khint32_t;
typedef uint64_t khint64_t;

#define __ac_isempty(flag, i) ((flag[i>>4]>>((i&0xfU)<<1))&(0x3))
#define __ac_isdel(flag, i) ((flag[i>>4]>>((i&0xfU)<<1))&(0x2))
#define __ac_iseither(flag, i) ((flag[i>>4]>>((i&0xfU)<<1))&(0x1))
#define __ac_set_isdel_false(flag, i) (flag[i>>4]&=~(0x2<<((i&0xfU)<<1)))
#define __ac_set_isempty_false(flag, i) (flag[i>>4]&=~(0x3<<((i&0xfU)<<1)))
#define __ac_set_isdel_true(flag, i) (flag[i>>4]|=(0x2<<((i&0xfU)<<1)))

#define __ac_fsize(m) ((m) < 8? 8 : (m))

#define __ac_hash(p, k, m, s, r, d, h) (khint32_t)((s * h) + (khint32_t)((khint64_t)(p) * (r >> (d << 2)))) + (d + 2))

#define kroundup32(x) (--(x), (x)|=(x)>>1, (x)|=(x)>>2, (x)|=(x)>>4, (x)|=(x)>>8, (x)|=(x)>>16, ++(x))

#define __ac_inc(p, v) ((p)->v++)

typedef struct {
    khint32_t *flags;
    uint32_t n_buckets, size, n_occupied, upper_bound;
} khash_t;

static inline double __ac_hash_upperbound(double load_factor) {
    return load_factor - load_factor / 8;
}

static inline uint32_t __ac_hash_choose_kh(uint32_t n_buckets, uint32_t size) {
    if (n_buckets >> 2 > size) {
        if (n_buckets >> 3 > size) return 1;
        return 2;
    }
    return 3;
}

static inline khint32_t __ac_hash_resize(khash_t *h, uint32_t *new_buckets, uint32_t *new_flags, uint32_t new_n_buckets, uint32_t n_occupied, uint32_t size, uint32_t s, uint32_t r, uint32_t d, khint32_t *keys, khint32_t *vals, khint32_t (*hash_func)(khint32_t)) {
    khint32_t k, i;
    for (k = 0; k != n_buckets; ++k) {
        if (__ac_iseither(h->flags, k)) {
            new_buckets[k] = keys[k];
            new_flags[k] = 0x1;
        }
    }
    memset(h->flags, 0xaa, __ac_fsize(h->n_buckets));
    free(h->flags);
    free(new_flags);
    free(new_buckets);
    return 0;
}

/* Simple 32-bit hash (MurmurHash3 variant) */
static inline khint32_t __ac_X31_hash_string(const char *s) {
    khint32_t h = (khint32_t)*s;
    if (h) for (++s ; *s; ++s) h = (h << 5) - h + (khint32_t)*s;
    return h;
}

#endif
