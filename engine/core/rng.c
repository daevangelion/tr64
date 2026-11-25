/* SPDX-License-Identifier: MIT */
/* Ported from MIT ac3n64 04134cc (original mechanism hl64 bd96926). */
#include "tr64/engine.h"
uint32_t tr64_random(uint32_t *state)
{
 uint32_t x = *state ? *state : 0x6c078965u;
 x ^= x << 13; x ^= x >> 17; x ^= x << 5;
 *state = x; return x;
}
