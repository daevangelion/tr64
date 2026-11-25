/* SPDX-License-Identifier: MIT */
/* Ported from MIT ac3n64 04134cc. */
#ifndef TR64_ENGINE_H
#define TR64_ENGINE_H
#include <stdint.h>
#include "tr64/route.h"
/* Diagnostic route state, not game logic. Hash encoding excludes pointers
 * and native padding: four big-endian words, including the packed pad. */
typedef struct {
 uint32_t tick, seed, input_crc;
 tr64_input_t input;
} tr64_state_t;
uint32_t tr64_random(uint32_t *state);
void tr64_eng_boot(void);
void tr64_eng_start(uint32_t seed);
void tr64_eng_tick(tr64_input_t input);
uint32_t tr64_state_hash(const tr64_state_t *state);
const tr64_state_t *tr64_eng_state(void);
void tr64_boot_card(uint16_t *pixels);
#endif
