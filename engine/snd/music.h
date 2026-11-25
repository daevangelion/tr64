/* SPDX-License-Identifier: MIT */
#ifndef TR64_MUSIC_H
#define TR64_MUSIC_H
#include <stdint.h>
#include "tr64/pack.h"
#include "engine/snd/music_format.h"
#define TR64_MUSIC_OUTPUT_FRAMES 448u
/* Platform allocates exactly memory_bytes in its boot-only audio scope. */
uint32_t tr64_music_memory_bytes(void);
void tr64_music_init(void *memory,uint32_t bytes);
void tr64_music_catalog_bind(const tr64_pack_t *pack);
void tr64_music_level_start(void);
void tr64_music_level_stop(void);
void tr64_music_tick(void);
void tr64_music_pump(void);
/* Resident main-thread pull, never AI IRQ. NULL means transport is stopped,
 * unavailable or priming. Mono PCM is added identically before final clip. */
const int16_t *tr64_music_mono_pcm(uint32_t frames,uint32_t output_rate);
void tr64_music_report(void);
/* Profiler overlay: running decode time in microseconds modulo 2^32. */
uint32_t tr64_music_perf_us(void);
#endif
