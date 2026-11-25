/* SPDX-License-Identifier: MIT */
/* Ported from MIT ac3n64 04134cc. */
#ifndef TR64_IN_H
#define TR64_IN_H
#include <stdint.h>
/* The sequel's route VM needs raw action bit 16; keep the TR1 cart ABI. */
#if defined(TR64_HOST) || TR64_GAME==2
typedef struct { uint32_t buttons; int8_t x, y; } tr64_input_t;
#else
typedef struct { uint16_t buttons; int8_t x, y; } tr64_input_t;
#endif
tr64_input_t tr64_input_read(void);
/* Both pads from the same poll, in port order. An absent pad is neutral. */
void tr64_input_read_pair(tr64_input_t out[2]);
#endif
