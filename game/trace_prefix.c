/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/common.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"
#include "engine/core/fmt.h"

/* Compact schema-2 Lara header prefix, byte-identical to game/trace.c record()
 * up to capacity bytes (used by shipping play logs without keeping the full
 * world trace serializer resident). */
uint32 game_trace_prefix(char *out, uint32 capacity)
{
    char buf[320];
    if (!gs || (!gs->players[0] && gs->gLevelID != LVL_TR1_TITLE &&
                !game_is_cutscene_level(gs->gLevelID)))
        sys_error("game trace before level");
    if (gs->stop.kind != TR64_GAME_STOP_NONE)
        sys_error("%s", gs->stop.name);
    if (!capacity)
        return 0;
    const ItemObj *s = gs->players[0];
    int n;
    if (!s) {
        n = tr64_snprintf(buf, sizeof(buf),
                          "{\"schema\":2,\"tick\":%lu,\"lara\":null",
                          (unsigned long)gs->tick);
    } else {
        const int32 index = (int32)(s - gs->items);
        const int32 room = s->room ? (int32)(s->room - gs->rooms) : -1;
        n = tr64_snprintf(buf, sizeof(buf),
                          "{\"schema\":2,\"tick\":%lu,\"lara\":{\"index\":%ld,"
                          "\"type\":%ld,\"pos\":[%ld,%ld,%ld],\"rot\":[%ld,%ld,%ld],"
                          "\"room\":%ld,\"anim\":%ld,\"frame\":%ld,\"state\":%ld,"
                          "\"goal\":%ld,\"next\":%ld,\"flags\":%ld,\"health\":%ld,"
                          "\"hSpeed\":%ld,\"vSpeed\":%ld,\"turnSpeed\":%ld,"
                          "\"timer\":%ld,\"water\":%ld,\"mood\":%ld,\"headOffset\":%ld,"
                          "\"aggression\":%ld,\"intensity\":%ld,\"roomFloor\":%ld,"
                          "\"hitMask\":%lu,\"visibleMask\":%lu",
                          (unsigned long)gs->tick, (long)index, (long)s->type,
                          (long)s->pos.x, (long)s->pos.y, (long)s->pos.z,
                          (long)s->angle.x, (long)s->angle.y, (long)s->angle.z,
                          (long)room, (long)s->animIndex, (long)s->frameIndex,
                          (long)s->state, (long)s->goalState, (long)s->nextState,
                          (long)s->flags, (long)s->health, (long)s->hSpeed,
                          (long)s->vSpeed, (long)s->turnSpeed, (long)s->timer,
                          (long)s->waterState, (long)s->input, (long)s->headOffset,
                          (long)s->aggression, (long)s->intensity, (long)s->roomFloor,
                          (unsigned long)s->hitMask, (unsigned long)s->visibleMask);
    }
    if (n <= 0)
        return 0;
    uint32 written = (uint32)n;
    if (written >= sizeof(buf))
        written = (uint32)(sizeof(buf) - 1u);
    if (written > capacity)
        written = capacity;
    memcpy(out, buf, written);
    return written;
}
