/* SPDX-License-Identifier: MIT */
#ifndef TR64_VIDEO_H
#define TR64_VIDEO_H
#include <stdint.h>
/* Videos of the original game (docs/formats/n64-video.md). Names are native
 * pack entries VIDEO_<NAME> (H.264) and FILM_<NAME> (film sound, MU64). */
enum {
 TR64_VIDEO_ESCAPE,TR64_VIDEO_CORE,TR64_VIDEO_CAFE,TR64_VIDEO_MANSION,
 TR64_VIDEO_SNOW,TR64_VIDEO_LIFT,TR64_VIDEO_VISION,TR64_VIDEO_CANYON,
 TR64_VIDEO_PYRAMID,TR64_VIDEO_PRISON,TR64_VIDEO_END,TR64_VIDEO_COUNT
};
enum {
 TR64_VIDEO_ABSENT,   /* the bound pack has no such video: nothing was played */
 TR64_VIDEO_DONE,     /* played to its last picture */
 TR64_VIDEO_SKIPPED   /* START pressed, or the scene's tick limit was reached */
};
typedef struct {
 uint32_t pictures,shown,dropped,pad_polls;
 uint32_t decode_us_total,decode_us_max;  /* decoder work per picture, no waits */
 uint32_t present_us_max,late_us_max;     /* blit and RDP; shown later than due */
 uint32_t early_us_max;                   /* shown before due (audio clock lead) */
 uint32_t slow_pictures,slow_run_max;  /* decoder work above one 66.7 ms period; longest run of them */
 uint32_t index_us,open_us;               /* stream index query; decoder open */
 uint32_t io_bytes,audio_underruns,audio_stalls;
 uint32_t arena_peak,arena_bytes,arena_allocs;
 int32_t sync_min_us,sync_max_us;         /* picture time minus audio time */
 uint32_t film_us,wall_us;                /* audio clock span and wall span */
} tr64_video_report_t;
/* Resident, no level resident (after eng_level_release or at boot), stopped
 * music/sound, pad polling by the engine. Borrows the empty level arena for the
 * whole play and returns it exactly; loads and unloads the `video` code unit.
 * max_ticks=0 plays to the end; otherwise stops after that many 30 Hz ticks
 * (scenes). START skips. report may be NULL. */
int tr64_video_play(unsigned video,uint32_t max_ticks,tr64_video_report_t *report);
const char *tr64_video_name(unsigned video);
/* The play loop's single hook, between eng_level_release and the next load.
 * from=NULL is boot. advance=1 only for a campaign step (not restart/load/demo).
 * Returns how many videos were played or skipped (0 when the pack holds none). */
unsigned tr64_video_transition(const char *from,const char *to,int advance);
#endif
