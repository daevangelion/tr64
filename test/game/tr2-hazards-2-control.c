/* SPDX-License-Identifier: MIT */
/* Directed checks for the Great Wall disc, blade and spike wall, and the
 * wrong variants those checks reject. Harness-only link. */
#include "game/internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void __real_game_tr2_tick(uint32 mask);
void __real_game_tr2_moving_contact(ItemObj *item, Lara *lara);
void __real_game_tr2_disc_launch(ItemObj *launcher);
void __real_game_tr2_spike_wall_update(ItemObj *item);
void __real_game_tr2_rolling_update(ItemObj *item);
ItemObj *__real_ItemObj_add(ItemType type, Room *room, const vec3i *pos, int32 angleY);
void __real_game_tr2_validate_world_animations(void);

static const char *mode(void)
{
    const char *value = getenv("TR64_HAZ2");
    if (!value)
        sys_error("hazard-2 control needs TR64_HAZ2");
    return value;
}

static unsigned discCount(void)
{
    unsigned n = 0;
    for (unsigned i = 0; i < ITEM_MAX; ++i)
        if (gs->items[i].type == TR2_ITEM_DISC && gs->items[i].room)
            ++n;
    return n;
}

static unsigned effectCount(void)
{
    unsigned n = 0;
    for (uint8 index = gs->tr2->effectActive; index; index = gs->tr2->effects[index - 1].next)
        ++n;
    return n;
}

static unsigned wakeLaunchers(void)
{
    unsigned n = 0;
    for (uint32 i = 0; i < gs->level.itemsCount; ++i) {
        ItemObj *item = gs->items + i;
        if (item->type != TR2_ITEM_LAUNCHER || !item->room)
            continue;
        if (!game_tr2_activate_item(item))
            sys_error("placed launcher %u did not wake", i);
        ++n;
    }
    return n;
}


static bool wrong(const char *name)
{
    return !strcmp(mode(), name);
}

static void pass(const char *text)
{
    printf("PASS %s\n", text);
    exit(0);
}

void __wrap_game_tr2_moving_contact(ItemObj *item, Lara *lara)
{
    __real_game_tr2_moving_contact(item, lara);
    /* Wrong variant: harm on the contact tick. Production waits. */
    if (wrong("damage-now") && item->hitMask && item->type == TR2_ITEM_DISC)
        Lara_hit(lara, 50, &item->pos, 0);
}

void __wrap_game_tr2_disc_launch(ItemObj *launcher)
{
    if ((!strcmp(mode(), "volley") && gs->tick >= 1500) ||
        (wrong("cadence-31") && gs->tick == 32))
        return;
    __real_game_tr2_disc_launch(launcher);
    if (wrong("offset-east") && launcher->angle.y == ANGLE_90 &&
        gs->firstActive && gs->firstActive->type == TR2_ITEM_DISC)
        gs->firstActive->pos.x = i32_add(gs->firstActive->pos.x, 412);
    if (wrong("floor-slope") && gs->firstActive && gs->firstActive->type == TR2_ITEM_DISC)
        gs->firstActive->roomFloor = Sector_getFloor(
            Room_getSector(gs->firstActive->room, gs->firstActive->pos.x, gs->firstActive->pos.z),
            gs->firstActive->pos.x, gs->firstActive->pos.y, gs->firstActive->pos.z);
}

void __wrap_game_tr2_spike_wall_update(ItemObj *item)
{
    __real_game_tr2_spike_wall_update(item);
    if (wrong("wall-continues") &&
        (item->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INACTIVE)
        item->pos.z = i32_add(item->pos.z, 16);
}

void __wrap_game_tr2_rolling_update(ItemObj *item)
{
    if (wrong("blade-early") && item->state == 2 && item->goalState == 2 && gs->tick == 2)
        item->goalState = 1;
    __real_game_tr2_rolling_update(item);
}

ItemObj *__wrap_ItemObj_add(ItemType type, Room *room, const vec3i *pos, int32 angleY)
{
    if (wrong("pool") && type == TR2_ITEM_DISC)
        return NULL;
    return __real_ItemObj_add(type, room, pos, angleY);
}

void __wrap_game_tr2_validate_world_animations(void)
{
    if (wrong("short-bank"))
        gs->level.models[TR2_ITEM_ROLLING_BLADE].animIndex = gs->tr2->animCount - 1;
    if (wrong("bad-state")) {
        Anim *turn = (Anim *)&gs->level.anims[
            gs->level.models[TR2_ITEM_ROLLING_BLADE].animIndex + 3];
        turn->state.raw = 0;
    }
    __real_game_tr2_validate_world_animations();
}

static ItemObj *findType(unsigned type, unsigned index)
{
    for (ItemObj *item = gs->firstActive; item; item = item->nextActive)
        if (item->type == type && (unsigned)(item - gs->items) == index)
            return item;
    return NULL;
}

static unsigned countType(unsigned type)
{
    unsigned n = 0;
    for (ItemObj *item = gs->firstActive; item; item = item->nextActive)
        if (item->type == type)
            ++n;
    return n;
}

void __wrap_game_tr2_tick(uint32 mask)
{
    const char *which = mode();
    static unsigned maxEffects, maxDiscs, baseDiscs, discsAtStop;
    static bool woke;
    if (!strcmp(which, "volley") && !woke) {
        unsigned launchers = wakeLaunchers();
        if (launchers != 14)
            sys_error("placed start woke %u launchers, want 14", launchers);
        baseDiscs = discCount();
        woke = true;
    }
    const int16 health = gs->players[0]->health;
    if ((!strcmp(which, "boulder-rest") || !strcmp(which, "boulder-rolls")) && gs->tick >= 1) {
        Lara *lara = gs->players[0];
        if (lara->room != gs->rooms + 35) {
            Room_remove(lara->room, lara);
            lara->pos = vec3i_create(62976, 1536, 23040);
            lara->angle.y = 0;
            Room_add(gs->rooms + 35, lara);
        }
        lara->hSpeed = 0;
        lara->vSpeed = 0;
        lara->flags &= ~ITEM_FLAG_GRAVITY;
    }
    ItemObj *savedFree = NULL;
    if (!strcmp(which, "pool")) {
        savedFree = gs->firstFree;
        gs->firstFree = NULL;
    }
    __real_game_tr2_tick(mask);
    if (!strcmp(which, "pool")) {
        gs->firstFree = savedFree;
        if (discCount() != 0)
            sys_error("a full item list created a disc");
        if (gs->tick == 63)
            pass("a full item list creates no disc and does not stop");
        return;
    }
    if (!strcmp(which, "damage") || !strcmp(which, "damage-now")) {
        if (gs->tick == 57 && gs->players[0]->health != health)
            sys_error("damage applied on the contact tick");
        if (gs->tick == 58) {
            if (gs->players[0]->health != (int16)(health - 50))
                sys_error("damage was not 50 on the disc's next update");
            pass("50 on the disc's next update, not the contact tick");
        }
    } else if (!strcmp(which, "cadence") || !strcmp(which, "cadence-31")) {
        if (gs->tick == 3 && countType(TR2_ITEM_DISC) != 4)
            sys_error("first disc bank did not spawn four at tick 3");
        if (gs->tick == 33) {
            unsigned n = countType(TR2_ITEM_DISC);
            if (!strcmp(which, "cadence-31"))
                sys_error("cadence 31 rejected: discs %u at tick 33", n);
            if (n != 8)
                sys_error("cadence was not 30: discs %u at tick 33", n);
            pass("cadence 30; a 31-tick gap is rejected");
        }
    } else if (!strcmp(which, "offset") || !strcmp(which, "offset-east")) {
        if (gs->tick == 65) {
            ItemObj *east = findType(TR2_ITEM_DISC, 126);
            if (!east || east->angle.y != ANGLE_90)
                sys_error("east disc 126 missing at tick 65");
            if (east->pos.x != 10752)
                sys_error("spawn offset for the wrong facing: x %d", east->pos.x);
            pass("exact east has no horizontal offset");
        }
    } else if (!strcmp(which, "floor") || !strcmp(which, "floor-slope")) {
        if (gs->tick == 65 || gs->tick == 66) {
            ItemObj *disc = findType(TR2_ITEM_DISC, 126);
            int32 want = gs->tick == 65 ? 4352 : 4480;
            if (!disc || disc->roomFloor != want)
                sys_error("disc floor %d at tick %u, want %d",
                          disc ? disc->roomFloor : -1, gs->tick, want);
            if (gs->tick == 66)
                pass("sector byte 4352, then the slope sample 4480");
        }
    } else if (!strcmp(which, "wall") || !strcmp(which, "wall-continues")) {
        if (gs->tick == 353) {
            ItemObj *wall = findType(TR2_ITEM_SPIKE_WALL, 62);
            if (!wall || (wall->flags & ITEM_FLAG_STATUS) != ITEM_FLAG_STATUS_INACTIVE)
                sys_error("spike wall 62 was not stopped at tick 353");
            if (wall->pos.z != 59376)
                sys_error("wall step continued after the stop: z %d", wall->pos.z);
            pass("status 2 holds the last position");
        }
    } else if (!strcmp(which, "blade") || !strcmp(which, "blade-early")) {
        if (gs->tick == 3) {
            ItemObj *blade = findType(TR2_ITEM_ROLLING_BLADE, 90);
            if (!blade || blade->goalState != 2)
                sys_error("blade reversed a tick early");
        }
        if (gs->tick == 4) {
            ItemObj *blade = findType(TR2_ITEM_ROLLING_BLADE, 90);
            if (!blade || blade->goalState != 1)
                sys_error("blade did not request reversal on the missing-floor tick");
            pass("reversal on the missing-floor tick, not one earlier");
        }
    } else if (!strcmp(which, "volley")) {
        unsigned effects = effectCount();
        unsigned discs = discCount();
        if (effects > maxEffects)
            maxEffects = effects;
        if (discs > maxDiscs)
            maxDiscs = discs;
        if (effects >= 100)
            sys_error("effect pool filled at tick %u", gs->tick);
        if (gs->tick == 1500)
            discsAtStop = discs;
        if (gs->tick == 2200) {
            uint8 freeBefore = gs->tr2->effectFree;
            game_tr2_shot_effect(TR2_FX_BLOOD, gs->players[0]->room, gs->players[0]->pos, 1, 0);
            if (!freeBefore || gs->tr2->effectFree == freeBefore)
                sys_error("blood was refused after the firing window");
            printf("PASS pool bounded maxEffects=%u maxDiscs=%u atStop=%u discs=%u base=%u\n",
                   maxEffects, maxDiscs, discsAtStop, discs, baseDiscs);
            exit(0);
        }
    } else if (!strcmp(which, "boulder-rest") || !strcmp(which, "boulder-rolls")) {
        static int32 restX[2], restZ[2];
        static unsigned restTick[2];
        static bool resting[2];
        const unsigned index[2] = { 74, 73 };
        for (unsigned n = 0; n < 2; ++n) {
            ItemObj *ball = findType(TR2_ITEM_BOULDER, index[n]);
            if (!ball)
                sys_error("boulder %u left the active list", index[n]);
            if (!resting[n] && ball->hSpeed == 0 && gs->tick > 2) {
                resting[n] = true;
                restX[n] = ball->pos.x;
                restZ[n] = ball->pos.z;
                restTick[n] = gs->tick;
            }
            if (wrong("boulder-rolls") && resting[n] && gs->tick == restTick[n]) {
                ball->pos.x = i32_add(ball->pos.x, 60);
                ball->hSpeed = 60;
            }
            if (resting[n] && (ball->pos.x != restX[n] || ball->pos.z != restZ[n] || ball->hSpeed != 0))
                sys_error("boulder %u kept rolling after the rest", index[n]);
        }
        if (resting[0] && resting[1] && gs->tick >= restTick[0] + 300 && gs->tick >= restTick[1] + 300)
            pass("both boulders held the rest for 300 ticks");
    } else {
        sys_error("unknown hazard-2 control %s", which);
    }
}
