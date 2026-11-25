/* SPDX-License-Identifier: MIT */
#ifndef TR64_MUSIC_DECODE_H
#define TR64_MUSIC_DECODE_H
#include <stdint.h>
#define TR64_MUSIC_SPU 1u
/* Bounded music block cache over the effects' existing SPU recurrence. */
typedef struct {
    int32_t history[2];
    int16_t decoded[28];
    uint32_t unit;
} tr64_music_decoder_t;
void tr64_music_decoder_init(tr64_music_decoder_t *state,uint32_t codec);
/* unit is sequential; first call for a new unit may ask for a later sample.
 * raw points to one complete checked16B SPU block. */
int16_t tr64_music_decoder_sample(tr64_music_decoder_t *state,
                                  const uint8_t *raw,uint32_t unit,uint32_t sample);
#endif
