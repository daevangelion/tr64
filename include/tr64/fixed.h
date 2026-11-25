/* SPDX-License-Identifier: MIT */
/* Ported from MIT ac3n64 04134cc. */
#ifndef TR64_FIXED_H
#define TR64_FIXED_H
#include <stdint.h>
#include <limits.h>
/* Saturating Q16.16. Integer division truncates toward zero on both targets. */
typedef int32_t tr64_fixed_t;
static inline tr64_fixed_t tr64_fixed_clamp(int64_t value)
{ return value > INT32_MAX ? INT32_MAX : value < INT32_MIN ? INT32_MIN : (int32_t)value; }
static inline tr64_fixed_t tr64_fixed_mul(tr64_fixed_t a, tr64_fixed_t b)
{ return tr64_fixed_clamp((int64_t)a*b/65536); }
static inline tr64_fixed_t tr64_fixed_add(tr64_fixed_t a, tr64_fixed_t b)
{ return tr64_fixed_clamp((int64_t)a+b); }
#endif
