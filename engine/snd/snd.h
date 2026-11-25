/* SPDX-License-Identifier: MIT */
#ifndef TR64_SND_H
#define TR64_SND_H
#include <stdint.h>
#include "tr64/level.h"
#include "tr64/game.h"
#define TR64_SND_CHANNELS 6u
#define TR64_SND_ARENA_BYTES 98304u
#define TR64_SND_METADATA_BYTES TR64_LIMIT_sound_metadata_1
#define TR64_SND_TR2_METADATA_BYTES TR64_LIMIT_sound_metadata_2
#define TR64_SND_INDEX_BLOCKS TR64_LIMIT_sound_blocks_1
#define TR64_SND_BLOCK_BYTES 4096u
/* Engine mastering choice (Main): shared SFX/music bus -6.02dB headroom. */
#define TR64_SND_MASTER_DIVISOR 2
/* One permanent audio arena and AI queue at boot, never a post-boot allocation.
 * Loader owns bind; the play/scene owner owns tick and level lifetimes. */
void tr64_snd_boot(void);
void tr64_snd_level_start(const tr64_level_t *level);
void tr64_snd_level_stop(void);
/* Exactly once after every completed game_tick, including a terminal tick.
 * Consumes borrowed events immediately, never on render/query calls. */
void tr64_snd_tick(void);
void tr64_snd_pump(void);
void tr64_snd_report(void);
void *tr64_snd_metadata_buffer(const tr64_level_t *,uint32_t *capacity);
tr64_pack_stream_block_t *tr64_snd_index_buffer(uint32_t *capacity);
/* Internal loader precondition: stream partition, pure metadata and Core
 * reference validation succeeded. Bind checks ownership and lifetime. */
void tr64_snd_bind(const void *metadata,uint32_t metadata_bytes,
                   const tr64_pack_stream_block_t *blocks,uint32_t block_count,
                   uint32_t total);
int tr64_snd_validate_metadata(const void *data,uint32_t bytes,
                              uint32_t total,const tr64_level_t *level);
/* Caller supplies 2*frames cached words: left sums/final stereo in the first
 * bank, right-minus-left sums in the second. Centred voices keep the mono path. */
typedef union { int32_t sum; int16_t stereo[2]; } tr64_snd_frame_t;
/* Common, allocation-free mixer. rate is the actual platform DAC frequency;
 * output is signed interleaved stereo, frames counts sample pairs.
 * Returns the active voice count at the beginning of this buffer. */
uint32_t tr64_snd_mix(tr64_snd_frame_t *output,uint32_t frames,uint32_t rate);
/* Platform adapter: allocate returns a permanent, 16-byte-aligned arena. */
void *tr64_audio_boot(uint32_t bytes);
void tr64_audio_active(int enabled);
void tr64_audio_tick(void);
void tr64_audio_pump(void);
void tr64_audio_report(void);
/* Running totals for the profiler overlay: time in the mixer pump (mix, music decode and the copy to the DAC
 * buffer) in microseconds modulo 2^32, and output frames written. */
void tr64_audio_perf(uint32_t *mix_us,uint32_t *output_frames);
#endif
