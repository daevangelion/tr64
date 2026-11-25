/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/item.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Ported from src/fixed/item.h:36-119 at
 * 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#if TR64_HAS_TR2
#include "tr64/level.h"
#endif
#include "internal.h"
static tr64_game_sound_event_t *event(int16 id, int16 sample)
{
    tr64_game_sound_event_t *e;
    if (gs->soundCount >= 64)
        sys_error("game sound command capacity");
    e = gs->sounds + gs->soundCount++;
    memset(e, 0, sizeof(*e));
    e->sound = id;
    e->sample = sample;
    return e;
}
#if TR64_HAS_TR2
enum { TR2_SOUND_CAP = 64, TR2_MIXER_VOICES = 6 }; /* TR64_SND_CHANNELS in engine/snd/snd.h. */
static bool tr2ModeUnique(uint8 mode)
{
    /* Lockstep with engine/snd/snd.c (mode_unique and oldest-plain stealing):
     * in TR2 modes 1 (unique) and 3 (loop+unique) are keyed by sound ID and
     * never stolen; modes 0 and 2 are stealable plain one-shots. */
    return (mode & 1u) != 0;
}
/* Compact gs->sounds in place only on overflow, preserving exact 6-voice TR2
 * mixer equivalence (engine/snd/snd.c:events_tick). Stop events partition the
 * tick into stop-free segments. Pass 1 folds duplicate unique events (modes 1
 * and 3, keyed by sound ID) into their earliest slot (keeping its sample and
 * order, taking the latest volume/pitch/position) and collapses duplicate
 * contiguous stops. If still full and uniqueId >= 0 matches an existing unique
 * in the tail segment, returns that slot to update in place without evicting
 * tail plains. Otherwise Pass 2 trims each segment's plain one-shots (modes 0
 * and 2) to what can survive 6 voices alongside that segment's uniques (and
 * one incoming play in the tail segment). If still full for a stop or new
 * unique, drops the oldest remaining plain so stops/uniques are not dropped
 * while plains occupy slots. */
static __attribute__((noinline, cold)) tr64_game_sound_event_t *compactTr2Sounds(int16 uniqueId, bool incomingPlay)
{
    tr64_game_sound_event_t *buf = gs->sounds, *endAll = buf + gs->soundCount, *w = buf, *seg = buf;
    for (tr64_game_sound_event_t *cur = buf; cur < endAll; ++cur)
    {
        if (cur->stop)
        {
            tr64_game_sound_event_t *j = w;
            while (j > buf && j[-1].stop && j[-1].sample != cur->sample)
                --j;
            if (j > buf && j[-1].stop)
            {
                seg = w;
                continue;
            }
            seg = w + 1;
        }
        else if (tr2ModeUnique(cur->mode))
        {
            tr64_game_sound_event_t *j = seg;
            while (j < w && (!tr2ModeUnique(j->mode) || j->sound != cur->sound))
                ++j;
            if (j < w)
            {
                j->volume = cur->volume;
                j->pitch = cur->pitch;
                j->positioned = cur->positioned;
                j->position[0] = cur->position[0];
                j->position[1] = cur->position[1];
                j->position[2] = cur->position[2];
                continue;
            }
        }
        if (w != cur)
            *w = *cur;
        ++w;
    }
    if (w >= buf + TR2_SOUND_CAP && uniqueId >= 0)
    {
        for (tr64_game_sound_event_t *j = seg; j < w; ++j)
        {
            if (tr2ModeUnique(j->mode) && j->sound == uniqueId)
            {
                gs->soundCount = (uint32)(w - buf);
                j->positioned = 0;
                j->position[0] = 0;
                j->position[1] = 0;
                j->position[2] = 0;
                return j;
            }
        }
    }
    if (w >= buf + TR2_SOUND_CAP)
    {
        tr64_game_sound_event_t *out = buf, *s = buf;
        while (s < w)
        {
            tr64_game_sound_event_t *end = s;
            uint32 plain = 0, skip = 0;
            if (s->stop)
                end = s + 1;
            else
            {
                uint32 uniques = 0;
                while (end < w && !end->stop)
                {
                    if (tr2ModeUnique(end->mode))
                        ++uniques;
                    else
                        ++plain;
                    ++end;
                }
                uint32 used = uniques + (incomingPlay && end == w ? 1u : 0u);
                uint32 keep = used < TR2_MIXER_VOICES ? TR2_MIXER_VOICES - used : 0;
                skip = plain > keep ? plain - keep : 0;
            }
            for (tr64_game_sound_event_t *j = s; j < end; ++j)
            {
                if (skip && !tr2ModeUnique(j->mode))
                {
                    --skip;
                    continue;
                }
                if (out != j)
                    *out = *j;
                ++out;
            }
            s = end;
        }
        w = out;
    }
    if (w >= buf + TR2_SOUND_CAP && !(incomingPlay && uniqueId < 0))
    {
        for (tr64_game_sound_event_t *p = buf; p < w; ++p)
        {
            if (!p->stop && !tr2ModeUnique(p->mode))
            {
                --w;
                memmove(p, p + 1, (size_t)((const uint8 *)w - (const uint8 *)p));
                break;
            }
        }
    }
    gs->soundCount = (uint32)(w - buf);
    return NULL;
}
static __attribute__((noinline, cold)) tr64_game_sound_event_t *tr2OverflowPlay(int16 id, int16 sample, uint8 mode)
{
    tr64_game_sound_event_t *dup = compactTr2Sounds(tr2ModeUnique(mode) ? id : -1, true);
    if (dup)
        return dup;
    return gs->soundCount < TR2_SOUND_CAP ? event(id, sample) : NULL;
}
#endif
void *soundPlay(int16 id, const vec3i *pos)
{
    int16 a;
    const SoundInfo *b;
    int32 volume, pitch = 128, index;
    uint32 flags;
    tr64_game_sound_event_t *e;
#if TR64_HAS_TR2
    /* Diagnostic count only. An out-of-range id returns before the map index. */
    if (TR64_SECOND_GAME) {
        ++gs->tr2->soundCalls;
        if (id < 0 || (uint32_t)id >= tr64_level_table(gs->core, TR64_CORE_sound_map)->count)
            return NULL;
    }
#endif
    if (id < 0)
        return NULL;
    a = tr64_i16(gs->level.soundMap[id]);
    if (a == -1)
        return NULL;
    b = gs->level.soundsInfo + a;
    flags = tr64_u16(b->flags);
    if (tr64_u16(b->chance) && tr64_u16(b->chance) < rand_draw())
        return NULL;
    volume = tr64_u16(b->volume);
    if (pos)
    {
        const Camera *listener=&gs->playersExtra[0].camera;
        if (gs->players[1] &&
            game_distance_squared(*pos,gs->playersExtra[1].camera.view.pos) <=
            game_distance_squared(*pos,listener->view.pos))
            listener=&gs->playersExtra[1].camera;
        vec3i d = vec3i_sub(*pos, listener->target.pos);
        if (abs(d.x) >= 8192 || abs(d.y) >= 8192 || abs(d.z) >= 8192)
            return NULL;
        volume -= phd_sqrt((uint32)d.x * d.x + (uint32)d.y * d.y + (uint32)d.z * d.z) << 2;
    }
    if (SI_GAIN(flags))
        volume -= rand_draw() >> 2;
    volume = (volume < 32767 ? volume : 32767) >> 9;
    if (volume <= 0)
        return NULL;
    ++volume;
    if (SI_PITCH(flags))
        pitch += ((rand_draw() * 13) >> 14) - 13;
    index = tr64_u16(b->sample);
    if (SI_COUNT(flags) > 1)
        index += (rand_draw() * SI_COUNT(flags)) >> 15;
#if TR64_HAS_TR2
    if (TR64_SECOND_GAME && gs->soundCount >= TR2_SOUND_CAP)
    {
        e = tr2OverflowPlay(id, index, SI_MODE(flags));
        if (!e)
            return NULL;
    }
    else
#endif
    e = event(id, index);
    e->volume = volume;
    e->pitch = pitch;
    e->mode = SI_MODE(flags);
    if (pos)
    {
        e->positioned = 1;
        e->position[0] = pos->x;
        e->position[1] = pos->y;
        e->position[2] = pos->z;
    }
    return e;
}
void soundStop(int16 id)
{
#if TR64_HAS_TR2
    if (TR64_SECOND_GAME) {
        ++gs->tr2->soundCalls;
        if (id < 0 || (uint32_t)id >= tr64_level_table(gs->core, TR64_CORE_sound_map)->count)
            return;
    }
#endif
    int16 a = tr64_i16(gs->level.soundMap[id]);
    const SoundInfo *b;
    uint32 i;
    if (a == -1)
        return;
    b = gs->level.soundsInfo + a;
    for (i = 0; i < SI_COUNT(tr64_u16(b->flags)); ++i)
    {
#if TR64_HAS_TR2
        if (TR64_SECOND_GAME && gs->soundCount >= TR2_SOUND_CAP)
        {
            compactTr2Sounds(-1, false);
            if (gs->soundCount >= TR2_SOUND_CAP)
                break;
        }
#endif
        event(id, tr64_u16(b->sample) + i)->stop = 1;
    }
}
const tr64_game_sound_event_t *game_sound_events(uint32 *count)
{
    if (!gs)
        sys_error("game sound view before level");
    *count = gs->soundCount;
    return gs->sounds;
}
