/* SPDX-License-Identifier: MIT */
#ifndef TR64_MUSIC_FORMAT_H
#define TR64_MUSIC_FORMAT_H
#include <stdint.h>
#define TR64_MUSIC_KIND 0x60000u
#define TR64_MUSIC_HEADER_BYTES 64u
#define TR64_MUSIC_UNAVAILABLE 1u
/* Film sound of a video (docs/formats/n64-video.md): not a game request id. */
#define TR64_MUSIC_FILM_TRACK 60u
typedef struct {
    uint32_t total,flags,track,codec,rate,frames,data_offset,data_bytes;
    uint32_t unit_bytes,unit_frames;
} tr64_music_format_t;
void tr64_music_format(const void *header,uint32_t bytes,uint32_t total,
                       uint32_t expected_track,tr64_music_format_t *format);
#endif
