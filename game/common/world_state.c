/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/world_state.h and lara.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"
static void beginFlip(void)
{
    if (gs->flipTransaction) return;
    memcpy(gs->flipBackup, gs->flipMaps, sizeof(gs->flipMaps));
    gs->flipTransaction = true;
}
void worldRequestFlipToggle(void)
{
    beginFlip();
    gs->flipPending = true;
}
void worldApplyFlipAction(uint8 action, uint16 arg, uint8 mask, bool once, uint8 type)
{
    FlipMapState *state;
    bool requested;
    if (arg >= 10) return;
    state = gs->flipMaps + arg;
    if (action == TRIGGER_ACTION_FLIP)
    {
        if (state->flags & 2) return;
        beginFlip();
        mask &= 31;
        if (type == TRIGGER_TYPE_SWITCH) state->mask ^= mask;
        else if (type == TRIGGER_TYPE_ANTIPAD) state->mask &= ~mask;
        else state->mask |= mask;
        if (state->mask == 31)
        {
            state->flags |= 1;
            if (once) state->flags |= 2;
        }
        else state->flags &= ~1;
    }
    if (action != TRIGGER_ACTION_FLIP && !(state->flags & 1)) return;
    requested = action == TRIGGER_ACTION_FLIP ? (state->flags & 1) != 0 :
                action == TRIGGER_ACTION_FLIP_ON;
    if (requested != (gs->gSaveGame.flipped != 0)) worldRequestFlipToggle();
}
bool worldApplyPendingFlip(void)
{
    if (!gs->flipTransaction) return false;
    if (!gs->flipPending)
    {
        gs->flipTransaction = false;
        return false;
    }
    if (!game_world_items_linked())
    {
        /* world_state.h:309-316: worldRebuildDerivedStateForFlip refuses (worldBuildRoomPairs
         * sees an unlinked visible item); the flip maps are restored. */
        memcpy(gs->flipMaps, gs->flipBackup, sizeof(gs->flipMaps));
        gs->flipTransaction = gs->flipPending = false;
        return false;
    }
    game_world_flip(!gs->gSaveGame.flipped);
    gs->flipTransaction = gs->flipPending = false;
    return true;
}
static void updateEffect(void)
{
    EffectState *e = &gs->effect;
    vec3i pos;
    switch (e->id)
    {
    case 5:
        switch (e->timer)
        {
        case 0: soundPlay(99, NULL); e->cameraBounce = -250; break;
        case 3: soundPlay(147, NULL); break;
        case 20: case 50: case 70: soundPlay(70, NULL); break;
        case 35: soundPlay(99, NULL); break;
        }
        if (++e->timer == 105) e->id = -1;
        break;
    case 6:
        if (e->timer > 120 || !e->lara) { e->id = -1; break; }
        pos = e->lara->pos;
        pos.y = i32_add(e->lara->extraL->camera.target.pos.y, abs((int32)e->timer - 30) * 100);
        soundPlay(81, &pos);
        ++e->timer;
        break;
    case 7: soundPlay(117, NULL); e->id = -1; break;
    case 8:
        if (e->timer == 5) { soundPlay(119, NULL); e->id = -1; }
        ++e->timer;
        break;
    case 9:
        if (e->timer > 120) { e->id = -1; break; }
        if (!e->timer) soundPlay(161, NULL);
        pos = gs->playersExtra[0].camera.target.pos;
        pos.y = i32_add(pos.y, (int32)e->timer * 100);
        soundPlay(118, &pos);
        ++e->timer;
        break;
    case 11: soundPlay(170, NULL); e->cameraBounce = -75; e->id = -1; break;
    case 15:
        if (!e->timer) soundPlay(173, NULL);
        if (++e->timer == 55) { soundPlay(33, NULL); e->id = -1; }
        break;
    case 16:
        if (e->timer > 125 || e->timer == 90 || e->timer == 92 ||
            e->timer == 105 || e->timer == 107)
        {
            worldRequestFlipToggle();
            worldApplyPendingFlip();
            if (e->timer > 125) e->id = -1;
        }
        ++e->timer;
        break;
    default: e->id = -1; break;
    }
}
void worldUpdateEffects(void)
{
    if (gs->effect.id != -1) updateEffect();
}
void worldDispatchEffect(uint16 id, Lara *lara, bool flipped)
{
    if (!flipped || !(id == 5 || id == 6 || id == 7 || id == 8 || id == 9 ||
                      id == 11 || id == 15 || id == 16) || (id == 6 && !lara)) return;
    gs->effect.lara = lara;
    gs->effect.id = id;
    gs->effect.timer = 0;
    if (id == 7 || id == 11) updateEffect();
}
