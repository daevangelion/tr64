/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/hazard_lara.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
/* Burn constants from src/fixed/hazard_oracles/flame.h at the same revision. */
#include "internal.h"


void Lara_restore(Lara *self)
{
    if (self->health > 0)
        return;
    self->health = LARA_MAX_HEALTH;
    self->timer = LARA_MAX_OXYGEN;
    ItemObj_animSet(self, (tr64_u16(self->room->info->flags) & ROOM_FLAG_WATER_MASK) ?
        Lara_ANIM_UNDERWATER : Lara_ANIM_STAND, true, 0);
}
enum { BURN_DAMAGE = 5, BURN_DAMAGE_CADENCE_TICKS = 1, BURN_DURATION_TICKS = 90,
       FLAME_SOUND = 150, LARA_HAZARD_BURNING = 1 };

static int32 hazardLaraPlayerIndex(const Lara *lara)
{
    return lara && lara==gs->players[0] ? 0 : lara && lara==gs->players[1] ? 1 : -1;
}

static void hazardStopBurnSoundIfUnused(void)
{
    for (unsigned slot=0;slot<TR64_GAME_PLAYERS;++slot)
        if (gs->gLaraHazardState[slot].flags & LARA_HAZARD_BURNING)
            return;
    for (ItemObj *item = gs->firstActive; item; item = item->nextActive)
        if (item->type == ITEM_FLAME)
            return;
    if (gs->level.soundMap)
        soundStop(FLAME_SOUND);
}

void hazardClearLaraBurn(Lara *lara)
{
    int32 playerIndex = hazardLaraPlayerIndex(lara);
    if (playerIndex < 0)
        return;
    LaraHazardState *state = &gs->gLaraHazardState[playerIndex];
    bool wasBurning = (state->flags & LARA_HAZARD_BURNING) != 0;
    state->burnTicks = 0;
    state->damageCooldown = 0;
    state->flameItemIndex = -1;
    state->flags = state->reserved = 0;
    if (wasBurning)
        hazardStopBurnSoundIfUnused();
}

void hazardKillLara(Lara *lara, int32 cause, const vec3i *point)
{
    (void)point;
    if (!lara || cause == HAZARD_HIT_NONE || (uint8)cause > HAZARD_HIT_LAVA)
        return;
    lara->health = 0;
    lara->extraL->hazardCause = (uint8)cause;
    if (cause == HAZARD_HIT_FLAME)
        hazardClearLaraBurn(lara);
    int32 animation = cause == HAZARD_HIT_SPIKES ? Lara_ANIM_DEATH_SPIKES :
                      cause == HAZARD_HIT_BOULDER ? Lara_ANIM_DEATH_BOULDER : -1;
    if (animation < 0)
        return;
    ItemObj_animSet(lara, animation, true, 0);
    lara->state = lara->goalState = Lara_STATE_DEATH;
}

void hazardDamageLara(Lara *lara, int32 damage, int32 cause, const vec3i *point)
{
    if (!lara || damage <= 0 || lara->health <= 0 || cause == HAZARD_HIT_NONE ||
        (uint8)cause > HAZARD_HIT_LAVA)
        return;
    Lara_hit(lara, damage, point, 0);
    if (lara->health <= 0)
        hazardKillLara(lara, cause, point);
}

bool hazardLaraIsBurning(const Lara *lara)
{
    int32 playerIndex = hazardLaraPlayerIndex(lara);
    return playerIndex >= 0 && (gs->gLaraHazardState[playerIndex].flags & LARA_HAZARD_BURNING) != 0;
}

void hazardIgniteLara(Lara *lara, uint16 flameIndex)
{
    int32 playerIndex = hazardLaraPlayerIndex(lara);
    if (!lara || playerIndex < 0 || lara->health <= 0 || flameIndex >= 256)
        return;
    LaraHazardState *state = &gs->gLaraHazardState[playerIndex];
    state->burnTicks = BURN_DURATION_TICKS;
    state->damageCooldown = 0;
    state->flameItemIndex = (int16)flameIndex;
    state->flags = LARA_HAZARD_BURNING;
    state->reserved = 0;
}

void hazardUpdateLaraBurn(Lara *lara)
{
    int32 playerIndex = hazardLaraPlayerIndex(lara);
    if (!lara || playerIndex < 0)
        return;
    LaraHazardState *state = &gs->gLaraHazardState[playerIndex];
    if (!(state->flags & LARA_HAZARD_BURNING))
        return;
    /* Oracle hunk 23: no timeout; Lara burns until she reaches water or dies (the source stopped after 90 ticks). */
    if (lara->health <= 0 || lara->waterState == WATER_STATE_SURFACE ||
        lara->waterState == WATER_STATE_UNDER)
    {
        hazardClearLaraBurn(lara);
        return;
    }
    soundPlay(FLAME_SOUND, &lara->pos);
    if (state->damageCooldown != 0)
    {
        --state->damageCooldown;
        return;
    }
    state->damageCooldown = BURN_DAMAGE_CADENCE_TICKS - 1;
    hazardDamageLara(lara, BURN_DAMAGE, HAZARD_HIT_FLAME, &lara->pos);
}
/* Oracle hunk 21 (hazard_lara.h hazardLavaBurn; docs/specs/wall/objects-missing.md 15.3, triggers-and-effects.md 2.1):
 * Lara on a lava floor. Nothing if her hit points are already below 0, if she is not on the floor (y equals her
 * recorded floor) or if the real floor of the sector (read at y = 32000) is not her recorded floor; otherwise her hit
 * points become -1, the cause is lava and she burns. */
void hazardLavaBurn(ItemObj *item)
{
    Lara *lara = (Lara *)item;
    int32 slot=hazardLaraPlayerIndex(lara);
    if (slot<0 || lara->health < 0 || lara->pos.y != lara->roomFloor)
        return;
    const FloorData *lastData = gs->gLastFloorData;
    uint16 lastSlant = gs->gLastFloorSlant;
    int32 realFloor = Sector_getFloor(Room_getSector(lara->room, lara->pos.x, lara->pos.z), lara->pos.x, 32000, lara->pos.z);
    gs->gLastFloorData = lastData;
    gs->gLastFloorSlant = lastSlant;
    if (realFloor != lara->roomFloor)
        return;
    lara->health = -1;
    lara->extraL->hazardCause = (uint8)HAZARD_HIT_LAVA;
    LaraHazardState *state = &gs->gLaraHazardState[slot];
    state->burnTicks = BURN_DURATION_TICKS;
    state->damageCooldown = 0;
    state->flameItemIndex = -1;
    state->flags = LARA_HAZARD_BURNING;
    state->reserved = 0;
}
