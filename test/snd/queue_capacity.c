/* SPDX-License-Identifier: MIT */
/* Directed test of TR2 sound queue compaction against an unlimited reference
 * queue, linked with real game/common/sound.c and real engine/snd/snd.c. */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "game/common/internal.h"
#include "engine/snd/snd.h"
#include "engine/snd/format.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "tr64/res.h"
#include "tr64/mem.h"

GameState *gs;
static GameState state_storage;
static GameTR2 tr2_storage;
static uint8_t ram[4u * 1024u * 1024u] __attribute__((aligned(4096)));
static uint8_t cart[512u * 1024u] __attribute__((aligned(16)));
static uint32_t cart_size, clock_us;
static int pending;
static tr64_game_settings_t preferences = { .audio_sfx = 1, .audio_music = 1 };

static tr64_game_sound_event_t unlimited_buf[2048];
static uint32_t unlimited_count;
static int run_mode; /* 0=prod, 1=unlimited, 2=neg-loudest, 3=neg-drop-newest, 4=neg-fold-stop, 5=neg-drop-stop-64 */

const tr64_game_sound_event_t *__real_game_sound_events(uint32_t *count);

const tr64_game_sound_event_t *__wrap_game_sound_events(uint32_t *count)
{
    if (run_mode == 0)
        return __real_game_sound_events(count);
    *count = unlimited_count;
    return unlimited_buf;
}

int32 rand_draw(void)
{
    gs->gRandSeedDraw = 0x3039u + gs->gRandSeedDraw * 0x41c64e6du;
    return (int32)((gs->gRandSeedDraw >> 10) & 32767u);
}

const tr64_game_settings_t *game_settings_view(void) { return &preferences; }

void sys_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

void sys_log_write(const char *p, uint32_t n)
{
    if (fwrite(p, 1, n, stdout) != n)
        exit(1);
}

void sys_memory_boot(void)
{
    if (!mem_is_booted())
        mem_boot_carve(ram, sizeof(ram));
}

void sys_io_frame_begin(void) { }

int sys_read_start(void *dst, uint32_t offset, uint32_t bytes)
{
    if (pending || bytes > 4096 || offset > cart_size || bytes > cart_size - offset)
        return -1;
    memcpy(dst, cart + offset, bytes);
    clock_us += (bytes * 195u + 1023u) / 1024u;
    pending = 1;
    return 0;
}

int sys_read_poll(void)
{
    int p = pending;
    pending = 0;
    return p;
}

uint32_t sys_time_us(void) { return clock_us; }
uint32_t sys_profile_ticks(void) { return clock_us; }
uint32_t sys_profile_us(uint32_t ticks) { return ticks; }

void game_view_camera(tr64_game_view_t *view)
{
    memset(view, 0, sizeof(*view));
    view->camera_basis[0] = view->camera_basis[4] = view->camera_basis[8] = 16384;
}

uint32_t game_player_mask(void) { return 1; }

void game_view_camera_player(unsigned slot, tr64_game_view_t *view)
{
    if (slot)
        sys_error("queue test requested absent camera");
    game_view_camera(view);
}

static void emit_play(int16 id, const vec3i *pos)
{
    if (run_mode == 0) {
        soundPlay(id, pos);
        return;
    }
    gs->soundCount = 0;
    soundPlay(id, pos);
    for (uint32 i = 0; i < gs->soundCount; ++i) {
        if (unlimited_count >= 2048)
            sys_error("unlimited reference buffer overflow");
        unlimited_buf[unlimited_count++] = gs->sounds[i];
    }
}

static void emit_stop(int16 id)
{
    if (run_mode == 0) {
        soundStop(id);
        return;
    }
    gs->soundCount = 0;
    soundStop(id);
    for (uint32 i = 0; i < gs->soundCount; ++i) {
        if (unlimited_count >= 2048)
            sys_error("unlimited reference buffer overflow");
        unlimited_buf[unlimited_count++] = gs->sounds[i];
    }
}

/* Wrong compaction strategies for negative controls. */
static void apply_negative_transform(void)
{
    if (run_mode == 2) {
        /* Wrong: coalesce unique sounds by keeping the loudest instead of last. */
        uint32_t w = 0;
        for (uint32_t i = 0; i < unlimited_count; ++i) {
            tr64_game_sound_event_t cur = unlimited_buf[i];
            if (!cur.stop && (cur.mode == 1 || cur.mode == 3)) {
                uint32_t j = 0;
                while (j < w && (unlimited_buf[j].stop || unlimited_buf[j].sound != cur.sound))
                    ++j;
                if (j < w) {
                    if (cur.volume > unlimited_buf[j].volume)
                        unlimited_buf[j] = cur;
                    continue;
                }
            }
            unlimited_buf[w++] = cur;
        }
        unlimited_count = w;
    } else if (run_mode == 3) {
        /* Wrong: drop events after 64 instead of keeping the newest plain voices. */
        if (unlimited_count > 64)
            unlimited_count = 64;
    } else if (run_mode == 4) {
        /* Wrong: fold unique events across stop barriers. */
        uint32_t w = 0;
        for (uint32_t i = 0; i < unlimited_count; ++i) {
            tr64_game_sound_event_t cur = unlimited_buf[i];
            if (!cur.stop && (cur.mode == 1 || cur.mode == 3)) {
                uint32_t j = 0;
                while (j < w && (unlimited_buf[j].stop || unlimited_buf[j].sound != cur.sound))
                    ++j;
                if (j < w) {
                    unlimited_buf[j].volume = cur.volume;
                    unlimited_buf[j].pitch = cur.pitch;
                    unlimited_buf[j].positioned = cur.positioned;
                    memcpy(unlimited_buf[j].position, cur.position, sizeof(cur.position));
                    continue;
                }
            }
            unlimited_buf[w++] = cur;
        }
        unlimited_count = w;
    } else if (run_mode == 5) {
        /* Wrong: drop events past 64 (models dropping stop or 65th unique at cap). */
        if (unlimited_count > 64)
            unlimited_count = 64;
    }
}

static void run_tick(void)
{
    if (run_mode >= 2)
        apply_negative_transform();
    tr64_io_frame();
    tr64_snd_tick();
    tr64_snd_pump();
}

static void begin_tick(void)
{
    gs->soundCount = 0;
    unlimited_count = 0;
}

int main(int argc, char **argv)
{
    FILE *f;
    uint32_t bytes, capacity, count = 0, offset, remaining, i;
    uint8_t *meta;
    tr64_pack_stream_block_t *index;
    tr64_level_t level = { 0 };
    tr64_core_sound_sources_t source = { 0 };
    tr64_core_sound_details_t details[128];
    tr64_i16_be_t map[256];
    const char *scenario;

    if (argc != 4)
        sys_error("usage: queue_capacity SN64 SCENARIO --prod|--unlimited|--neg-loudest|--neg-drop-newest|--neg-fold-stop|--neg-drop-stop-64");
    scenario = argv[2];
    if (!strcmp(argv[3], "--prod")) run_mode = 0;
    else if (!strcmp(argv[3], "--unlimited")) run_mode = 1;
    else if (!strcmp(argv[3], "--neg-loudest")) run_mode = 2;
    else if (!strcmp(argv[3], "--neg-drop-newest")) run_mode = 3;
    else if (!strcmp(argv[3], "--neg-fold-stop")) run_mode = 4;
    else if (!strcmp(argv[3], "--neg-drop-stop-64")) run_mode = 5;
    else sys_error("unknown mode %s", argv[3]);

    f = fopen(argv[1], "rb");
    if (!f) sys_error("sn64 open failed");
    cart_size = (uint32_t)fread(cart, 1, sizeof(cart), f);
    if (ferror(f) || !feof(f)) sys_error("sn64 read failed");
    fclose(f);

    tr64_io_init(cart_size);
    tr64_snd_boot();

    level.ready = 1;
    level.game = 2;
    memset(details, 0, sizeof(details));
    for (i = 0; i < 256; ++i)
        map[i].raw = tr64_core_be16(65535);

    /* 0..7: mode 0 plain, 8..15: mode 2 plain, distinct samples 0..15. */
    for (i = 0; i < 16; ++i) {
        uint16_t flags = (uint16_t)((1u << 2) | (i < 8 ? 0u : 2u) | ((i & 1u) ? (0x2000u | 0x4000u) : 0u));
        map[i].raw = tr64_core_be16((uint16_t)i);
        details[i].sample.raw = tr64_core_be16((uint16_t)i);
        details[i].volume.raw = tr64_core_be16(32767);
        details[i].flags.raw = tr64_core_be16(flags);
    }
    /* 16: flame loop (mode 3, sample 0, gain+pitch RNG). */
    map[16].raw = tr64_core_be16(16);
    details[16].sample.raw = tr64_core_be16(0);
    details[16].volume.raw = tr64_core_be16(32767);
    details[16].flags.raw = tr64_core_be16((1u << 2) | 3u | 0x2000u | 0x4000u);
    /* 17: loop U1 (mode 3, sample 1). */
    map[17].raw = tr64_core_be16(17);
    details[17].sample.raw = tr64_core_be16(1);
    details[17].volume.raw = tr64_core_be16(32767);
    details[17].flags.raw = tr64_core_be16((1u << 2) | 3u);
    /* 18: unique non-loop with 2 sample variants (mode 1, samples 2..3, gain+pitch RNG). */
    map[18].raw = tr64_core_be16(18);
    details[18].sample.raw = tr64_core_be16(2);
    details[18].volume.raw = tr64_core_be16(32767);
    details[18].flags.raw = tr64_core_be16((2u << 2) | 1u | 0x2000u | 0x4000u);
    /* 19: ambient source (mode 3, sample 4, no variation). */
    map[19].raw = tr64_core_be16(19);
    details[19].sample.raw = tr64_core_be16(4);
    details[19].volume.raw = tr64_core_be16(24000);
    details[19].flags.raw = tr64_core_be16((1u << 2) | 3u);
    /* 32..105: 74 distinct unique loop sounds for saturation boundary testing. */
    for (i = 0; i < 74; ++i) {
        uint16_t slot = (uint16_t)(20 + i);
        map[32 + i].raw = tr64_core_be16(slot);
        details[slot].sample.raw = tr64_core_be16((uint16_t)(i & 15u));
        details[slot].volume.raw = tr64_core_be16(32767);
        details[slot].flags.raw = tr64_core_be16((1u << 2) | 3u);
    }

    source.z.raw = tr64_core_be32(1024);
    source.sound.raw = tr64_core_be16(19);
    source.flags.raw = tr64_core_be16(128);
    if (!strcmp(scenario, "mixed-stop-150")) {
        level.tables[TR64_CORE_sound_sources - 1] = (tr64_level_table_t){
            .data = (const uint8_t *)&source, .count = 1, .stride = 16
        };
    }
    level.tables[TR64_CORE_sound_map - 1] = (tr64_level_table_t){
        .data = (const uint8_t *)map, .count = 256, .stride = 2
    };
    level.tables[TR64_CORE_sound_details - 1] = (tr64_level_table_t){
        .data = (const uint8_t *)details, .count = 128, .stride = 8
    };

    meta = tr64_snd_metadata_buffer(&level, &capacity);
    bytes = snd_be32(cart + 12);
    if (bytes > capacity || bytes > cart_size)
        sys_error("sn64 metadata bounds");
    memcpy(meta, cart, bytes);
    index = tr64_snd_index_buffer(&capacity);
    offset = 0;
    remaining = cart_size;
    while (remaining) {
        uint32_t n = offset ? (remaining > 4096 ? 4096 : remaining) : bytes;
        if (count >= capacity)
            sys_error("sn64 index overflow");
        index[count++] = (tr64_pack_stream_block_t){ offset, offset, n, tr64_crc32(0, cart + offset, n) };
        offset += n;
        remaining -= n;
    }
    if (!tr64_snd_validate_metadata(meta, bytes, cart_size, &level))
        sys_error("sn64 metadata validation");
    tr64_snd_bind(meta, bytes, index, count, cart_size);
    tr64_snd_level_start(&level);

    memset(&state_storage, 0, sizeof(state_storage));
    memset(&tr2_storage, 0, sizeof(tr2_storage));
    gs = &state_storage;
    gs->core = &level;
    gs->tr2 = &tr2_storage;
    gs->level.soundMap = map;
    gs->level.soundsInfo = details;
    gs->gRandSeedDraw = 0x13579bdfu;
    gs->gRandSeedLogic = 0x2468ace0u;

    if (!strcmp(scenario, "flames-100")) {
        /* Tick 0: 100 flames from loud center to quieter hard-right. */
        begin_tick();
        for (i = 0; i < 100; ++i) {
            vec3i p = { (int32)(i * 55), 0, (int32)(100 + i * 4) };
            emit_play(16, &p);
        }
        run_tick();
        /* Tick 1: unrefreshed loop stops. */
        begin_tick();
        run_tick();
        /* Tick 2: 100 flames from far-left to close center. */
        begin_tick();
        for (i = 0; i < 100; ++i) {
            vec3i p = { (int32)(-5500 + (int32)i * 55), 0, (int32)(500 - (int32)i * 3) };
            emit_play(16, &p);
        }
        run_tick();
        for (i = 0; i < 5; ++i) { begin_tick(); run_tick(); }
    } else if (!strcmp(scenario, "plain-100")) {
        /* Tick 0: 100 plain one-shots across modes 0 and 2, samples 0..15. */
        begin_tick();
        for (i = 0; i < 100; ++i) {
            vec3i p = { (int32)((i & 1u ? 1 : -1) * (int32)(100 + (i % 17u) * 180)), 0, 500 };
            emit_play((int16)(i & 15u), &p);
        }
        run_tick();
        /* Tick 1: 2 new plain one-shots steal the 2 oldest surviving Tick-0 voices. */
        begin_tick();
        {
            vec3i pa = { 300, 0, 500 }, pb = { -300, 0, 500 };
            emit_play(0, &pa);
            emit_play(1, &pb);
        }
        run_tick();
        for (i = 0; i < 6; ++i) { begin_tick(); run_tick(); }
    } else if (!strcmp(scenario, "mixed-stop-150")) {
        /* Tick 0: start loop U1 + 2 plain sounds (plus ambient emitter). */
        begin_tick();
        {
            vec3i p0 = { 0, 0, 400 }, p1 = { 600, 0, 400 }, p2 = { -600, 0, 400 };
            emit_play(17, &p0);
            emit_play(2, &p1);
            emit_play(3, &p2);
        }
        run_tick();
        /* Tick 1: 40 flames + 30 plain + 2x stop(16) + 40 flames + 30 plain + 10x unique(18) + U1. */
        begin_tick();
        for (i = 0; i < 40; ++i) {
            vec3i p = { (int32)(i * 60), 0, 300 };
            emit_play(16, &p);
        }
        for (i = 0; i < 30; ++i) {
            vec3i p = { (int32)(-2000 + (int32)i * 120), 0, 600 };
            emit_play((int16)(i & 15u), &p);
        }
        emit_stop(16);
        emit_stop(16);
        for (i = 0; i < 40; ++i) {
            vec3i p = { (int32)(-1000 + (int32)i * 50), 0, 250 };
            emit_play(16, &p);
        }
        for (i = 0; i < 30; ++i) {
            vec3i p = { (int32)(1500 - (int32)i * 90), 0, 500 };
            emit_play((int16)((i + 5u) & 15u), &p);
        }
        for (i = 0; i < 10; ++i) {
            vec3i p = { (int32)(200 * (int32)(i - 5)), 0, 400 };
            emit_play(18, &p);
        }
        emit_play(17, NULL);
        run_tick();
        /* Tick 2: refresh loops + steal 2 oldest plain voices from Tick 1. */
        begin_tick();
        {
            vec3i pf = { 0, 0, 300 }, pa = { 400, 0, 400 }, pb = { -400, 0, 400 };
            emit_play(16, &pf);
            emit_play(17, NULL);
            emit_play(4, &pa);
            emit_play(5, &pb);
        }
        run_tick();
        for (i = 0; i < 5; ++i) { begin_tick(); run_tick(); }
    } else if (!strcmp(scenario, "full-uniques-plus-plain-70")) {
        /* Tick 0: 58 distinct uniques + 6 plains (exact 64) + 7th plain + tail dup + 7 more uniques. */
        begin_tick();
        for (i = 0; i < 58; ++i) {
            vec3i p = { (int32)(i * 20), 0, 300 };
            emit_play((int16)(32 + i), &p);
        }
        for (i = 0; i < 6; ++i) {
            vec3i p = { (int32)(i * 100), 0, 400 };
            emit_play((int16)i, &p);
        }
        {
            vec3i p7 = { -900, 0, 400 }, pdup = { 2500, 0, 400 };
            emit_play(6, &p7);
            emit_play(89, &pdup);
        }
        for (i = 0; i < 7; ++i) {
            vec3i p = { (int32)(i * 50), 0, 300 };
            emit_play((int16)(90 + i), &p);
        }
        run_tick();
        for (i = 0; i < 4; ++i) { begin_tick(); run_tick(); }
    } else if (!strcmp(scenario, "adv-unique-after-stop-6-plains")) {
        /* 52 uniques + 6 stops (freeing all 6 voices) + 6 plains (exact 64) + 1 new unique = 65. */
        begin_tick();
        for (i = 0; i < 52; ++i) {
            vec3i p = { (int32)(i * 20), 0, 300 };
            emit_play((int16)(32 + i), &p);
        }
        for (i = 0; i < 6; ++i)
            emit_stop((int16)(32 + i));
        for (i = 0; i < 6; ++i) {
            vec3i p = { (int32)(-500 + (int32)i * 200), 0, 400 };
            emit_play((int16)i, &p);
        }
        {
            vec3i pu = { 1200, 0, 300 };
            emit_play(90, &pu);
        }
        run_tick();
        for (i = 0; i < 4; ++i) { begin_tick(); run_tick(); }
    } else if (!strcmp(scenario, "adv-plain-after-stop-6-plains")) {
        /* 52 uniques + 6 stops + 6 plains (exact 64) + 7th plain = 65 (kills keep=6 vs 5 mutant M2). */
        begin_tick();
        for (i = 0; i < 52; ++i) {
            vec3i p = { (int32)(i * 20), 0, 300 };
            emit_play((int16)(32 + i), &p);
        }
        for (i = 0; i < 6; ++i)
            emit_stop((int16)(32 + i));
        for (i = 0; i < 6; ++i) {
            vec3i p = { (int32)(-500 + (int32)i * 200), 0, 400 };
            emit_play((int16)i, &p);
        }
        {
            vec3i p7 = { -1400, 0, 350 };
            emit_play(6, &p7);
        }
        run_tick();
        for (i = 0; i < 4; ++i) { begin_tick(); run_tick(); }
    } else if (!strcmp(scenario, "adv-tail-dup-with-5-plains")) {
        /* 52 uniques + 6 stops + 1 unique + 5 plains (exact 64) + tail dup must NOT drop any plain. */
        begin_tick();
        for (i = 0; i < 52; ++i) {
            vec3i p = { (int32)(i * 20), 0, 300 };
            emit_play((int16)(32 + i), &p);
        }
        for (i = 0; i < 6; ++i)
            emit_stop((int16)(32 + i));
        {
            vec3i pu = { 5500, 0, 800 };
            emit_play(90, &pu);
        }
        for (i = 0; i < 5; ++i) {
            vec3i p = { (int32)(-600 + (int32)i * 250), 0, 400 };
            emit_play((int16)i, &p);
        }
        emit_play(90, NULL);
        run_tick();
        for (i = 0; i < 4; ++i) { begin_tick(); run_tick(); }
    } else if (!strcmp(scenario, "adv-stop-at-64")) {
        /* Tick 0: 58 uniques + 6 dead plains (exact 64) + stop(32) of active unique (kills M4 Pass 2). */
        begin_tick();
        for (i = 0; i < 58; ++i) {
            vec3i p = { (int32)(i * 20), 0, 300 };
            emit_play((int16)(32 + i), &p);
        }
        for (i = 0; i < 6; ++i) {
            vec3i p = { (int32)(i * 100), 0, 400 };
            emit_play((int16)i, &p);
        }
        emit_stop(32);
        run_tick();
        /* Tick 1: 1 loop + 63 duplicate flames (exact 64) + stop(16) (kills M4 Pass 1). */
        begin_tick();
        {
            vec3i p17 = { -300, 0, 400 };
            emit_play(17, &p17);
        }
        for (i = 0; i < 63; ++i) {
            vec3i p = { (int32)(i * 40), 0, 300 };
            emit_play(16, &p);
        }
        emit_stop(16);
        run_tick();
        for (i = 0; i < 4; ++i) { begin_tick(); run_tick(); }
    } else if (!strcmp(scenario, "mutant-m1-multivariant")) {
        /* 70 calls of multi-variant unique (SI_COUNT=2, samples 2..3): Pass 1 folds 64 into slot 0
         * and must preserve the earliest sample (kills M1). */
        begin_tick();
        for (i = 0; i < 70; ++i) {
            vec3i p = { (int32)(-2000 + (int32)i * 60), 0, 350 };
            emit_play(18, &p);
        }
        run_tick();
        for (i = 0; i < 4; ++i) { begin_tick(); run_tick(); }
    } else if (!strcmp(scenario, "mutant-m3-m6-sat-dup-clear-pos")) {
        /* 64 distinct uniques (saturated, no plains/stops) + dup of active 32 with pos=NULL
         * clearing positioned + dup of active 33 with new pos (kills M3 and M6). */
        begin_tick();
        {
            vec3i p32 = { 6500, 0, 1000 }, p33 = { -6500, 0, 1000 };
            emit_play(32, &p32);
            emit_play(33, &p33);
        }
        for (i = 2; i < 64; ++i) {
            vec3i p = { (int32)(i * 20), 0, 300 };
            emit_play((int16)(32 + i), &p);
        }
        {
            vec3i p33_new = { 1800, 0, 250 };
            emit_play(32, NULL);
            emit_play(33, &p33_new);
        }
        run_tick();
        for (i = 0; i < 4; ++i) { begin_tick(); run_tick(); }
    } else if (!strcmp(scenario, "mutant-m5-stop-dedup")) {
        /* 58 uniques + 6 interleaved contiguous stops with duplicates (exact 64) + new unique 90
         * taking a freed voice slot (kills M5 and adjacent-only stop dedup). */
        begin_tick();
        for (i = 0; i < 58; ++i) {
            vec3i p = { (int32)(i * 20), 0, 300 };
            emit_play((int16)(32 + i), &p);
        }
        emit_stop(32);
        emit_stop(33);
        emit_stop(32);
        emit_stop(33);
        emit_stop(34);
        emit_stop(32);
        {
            vec3i p90 = { -1200, 0, 300 };
            emit_play(90, &p90);
        }
        run_tick();
        for (i = 0; i < 4; ++i) { begin_tick(); run_tick(); }
    } else {
        sys_error("unknown scenario %s", scenario);
    }

    log_printf("queue-test: scenario=%s mode=%s rng_draw=%08lx rng_logic=%08lx sound_calls=%lu\n",
               scenario, argv[3], (unsigned long)gs->gRandSeedDraw,
               (unsigned long)gs->gRandSeedLogic, (unsigned long)gs->tr2->soundCalls);
    tr64_snd_report();
    log_flush();
    tr64_snd_level_stop();
    return 0;
}
