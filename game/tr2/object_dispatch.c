/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/game.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Ported from src/fixed/game.h, item.h, lara.h, inventory.h and enemy.h
 * at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Keeps source constructor order and both deterministic RNG streams. */
#include "objects.h"
#include "boat.h"
#include "enemy_urban.h"
#include "game/common/object.h"
#include "tr64/code.h"

static bool wallUnitType(unsigned type)
{
    return tr2DoorType(type) || type == TR2_ITEM_CAMPFIRE ||
           type == TR2_ITEM_HELICOPTER ||
           type == TR2_ITEM_GRENADE_PROJ ||
           (TR64_RULE(INTERACTIONS) &&
            (type == TR2_ITEM_WALL_SWITCH || game_tr2_extra_switch_type(type) ||
             type == TR2_ITEM_BOULDER ||
             type == TR2_ITEM_COLLAPSE || type == TR2_ITEM_COLLAPSE_HIGH ||
             tr2TrapdoorType(type) || tr2BlockType(type) ||
             type == TR2_ITEM_DETONATOR)) ||
           (TR64_RULE(WALL_HAZARDS) &&
            (type == TR2_ITEM_GUILLOTINE || type == TR2_ITEM_STATUE_BLADE ||
             type == TR2_ITEM_ZIPLINE || type == TR2_ITEM_DISC ||
             type == TR2_ITEM_LAUNCHER || type == TR2_ITEM_ROLLING_BLADE ||
             type == TR2_ITEM_SPIKE_WALL));
}

static bool operaUnitType(unsigned type)
{
    return type == TR2_ITEM_SWING_SANDBAG || type == TR2_ITEM_DRAWBRIDGE ||
           type == TR2_ITEM_ELEVATOR || type == TR2_ITEM_FAN_PROP ||
           type == TR2_ITEM_FALLING_CEILING || type == TR2_ITEM_ROLLING_CRATE ||
           type == TR2_ITEM_FAN_AIR || type == TR2_ITEM_SWING_CRATE;
}

static bool boatUnitType(unsigned type)
{
    return type == TR2_ITEM_BOAT || type == TR2_ITEM_GONDOLA ||
           type == TR2_ITEM_RACE_CLOCK || type == TR2_ITEM_MINE;
}

static bool urbanUnitType(unsigned type)
{
    return game_tr2_urban_creature_type(type) || type == TR2_ITEM_WINDOW;
}

static bool extraActiveType(unsigned type)
{
    return boatUnitType(type) || operaUnitType(type) || type == TR2_ITEM_WINDOW ||
           type == TR2_ITEM_BIRDS || type == TR2_ITEM_WATERFALL;
}

bool game_tr2_item_ticks(unsigned type)
{
    return wallUnitType(type) || game_tr2_creature_type(type) || extraActiveType(type);
}

/* tr2-items-general, Ordinary placed-item initialization;
 * tr2-triggers-and-interaction, Item masks. */
bool game_tr2_activate_item(ItemObj *item)
{
    if (item->room && item->type==TR2_ITEM_GIANT_SPIDER)
        sys_error("Tomb Raider II: unsupported giant spider controller");
    if (!item->room || (item->flags & TR2_FLAG_RETIRED))
        return false;
    if (!game_tr2_item_ticks(item->type)) {
        if (TR64_RULE(INTERACTIONS)) {
            switch (item->type) {
            case 61: case 62: case 80: case 81: case 85:
                game_tr2_refuse_interaction(item,"activation");
                break;
            }
        }
        return false;
    }
    if (!(item->flags & ITEM_FLAG_ACTIVE)) {
        item->hitMask = 0;
        if (TR64_RULE(WALL_HAZARDS) && item->type == TR2_ITEM_ROLLING_BLADE) {
            /* Wake is the model's turn clip, frame 83, goal 2. The bank
             * check has already refused a short or wrong-state clip. */
            ItemObj_animSet(item, gs->level.models[item->type].animIndex + 3, true, 0);
            item->goalState = 2;
        }
        bool awake=true;
        if (TR64_RULE(CREATURES) && game_tr2_creature_type(item->type)) {
            TR2AiCall call={.op=TR2_AI_ACTIVATE,.self=item};
            awake=code_call("tr2_ai",(uint32_t)(uintptr_t)&call)!=0;
        }
        item->flags=(item->flags&~ITEM_FLAG_STATUS) |
                    (awake ? ITEM_FLAG_STATUS_ACTIVE : ITEM_FLAG_STATUS_INVISIBLE);
        ItemObj_activateBase(item);
    }
    return true;
}

bool game_tr2_pickup_type(unsigned type)
{
    /* The authored TR2 pickups. The loose flare is not a placement. */
    return type == 137 || type == 138 || type == 141 ||
           type == 143 || type == 144 || type == 145 || type == 148 ||
           (type >= 149 && type <= 151) || (type >= 174 && type <= 177) ||
           (type >= 190 && type <= 194);
}

bool game_tr2_switch_settled(ItemObj *item, unsigned timer)
{
    if ((item->type != TR2_ITEM_WALL_SWITCH && !game_tr2_extra_switch_type(item->type)) ||
        (item->flags & ITEM_FLAG_STATUS) != ITEM_FLAG_STATUS_INACTIVE)
        return false;
    if (item->state == 0 && timer) {
        item->timer = timer == 1 ? 1 : timer*30;
        item->flags = (item->flags & ~ITEM_FLAG_STATUS) | ITEM_FLAG_STATUS_ACTIVE;
    } else {
        ItemObj_deactivateBase(item);
        item->flags &= ~ITEM_FLAG_STATUS;
    }
    return true;
}

/* Both exits ask the animation to advance with the incoming motion
 * first, then supply velocities for the ordinary Lara phase. */
void game_tr2_zipline_release(Lara *lara, int16 speed, int16 descent)
{
    lara->goalState=Lara_STATE_JUMP;
    ItemObj_animProcess(lara,true);
    lara->flags|=ITEM_FLAG_GRAVITY;
    lara->extraL->moveAngle=lara->angle.y;
    lara->hSpeed=speed;
    lara->vSpeed=descent;
}

/* Strictly driven by placed object types in the loaded level pack;
 * never branches on level name or ID. */
void game_tr2_load_wall_units(const tr64_level_t *core)
{
    bool wall=TR64_RULE(ARMS), ai=false, creatures=false, boat=false, urban=false, opera=false;
    unsigned count=tr64_level_table(core,TR64_CORE_items)->count;
    for (unsigned i=0;i<count;++i) {
        unsigned type=tr64_u16(tr64_level_items(core,i)->type);
        wall|=(TR64_RULE(WORLD_ITEMS) && wallUnitType(type)) ||
              (TR64_RULE(INTERACTIONS) && type && !game_tr2_creature_type(type) &&
               !boatUnitType(type) && !operaUnitType(type) && type != TR2_ITEM_WINDOW);
        ai|=TR64_RULE(CREATURES) && game_tr2_creature_type(type);
        creatures|=TR64_RULE(CREATURES) && game_tr2_wall_creature_type(type);
        boat|=boatUnitType(type);
        urban|=urbanUnitType(type);
        opera|=operaUnitType(type);
    }
    if (!wall && !ai && !creatures && !boat && !urban && !opera)
        return;
    code_loading_begin();
    if (wall)
        code_load("tr2_wall");
    if (ai)
        code_load("tr2_ai");
    if (creatures)
        code_load("tr2_creatures");
    if (boat)
        code_load("tr2_boat");
    if (urban)
        code_load("tr2_urban");
    if (opera)
        code_load("tr2_opera");
    code_loading_end();
    gs->tr2->wallUnit=wall;
    gs->tr2->aiUnit=ai;
    gs->tr2->creatureUnit=creatures;
    gs->tr2->boatUnit=boat;
    gs->tr2->urbanUnit=urban;
    gs->tr2->operaUnit=opera;
}

void game_tr2_update_creature(ItemObj *item, Lara *lara)
{
    TR2AiCall aiCall={.op=TR2_AI_PREPARE,.self=item,.lara=lara};
    uint32 phase=code_call("tr2_ai",(uint32_t)(uintptr_t)&aiCall);
    if (phase==TR2_AI_RES_SKIP)
        return;
    const bool urban=game_tr2_urban_creature_type(item->type);
    if (phase==TR2_AI_RES_DEAD) {
        uint32 dres;
        if (urban) {
            TR2UrbanCall ucall={.op=TR2_URBAN_DEATH,.ai=aiCall.ai};
            dres=code_call("tr2_urban",(uint32_t)(uintptr_t)&ucall);
        } else {
            TR2SpeciesCall scall={.op=TR2_SPECIES_DEATH,.ai=aiCall.ai};
            dres=code_call("tr2_creatures",(uint32_t)(uintptr_t)&scall);
        }
        if (dres!=TR2_DEATH_DONE) {
            aiCall.op=TR2_AI_DEATH_STRIDE;
            aiCall.turn=dres==TR2_DEATH_STRIDE_ORDINAL ? (int16)(item-gs->items) : 0;
            (void)code_call("tr2_ai",(uint32_t)(uintptr_t)&aiCall);
        }
        return;
    }
    if (urban) {
        TR2UrbanCall ucall={.op=TR2_URBAN_CONTROL,.ai=aiCall.ai,.lara=lara,.interest=&aiCall.interest,.turn=aiCall.turn};
        (void)code_call("tr2_urban",(uint32_t)(uintptr_t)&ucall);
    } else {
        TR2SpeciesCall scall={.op=TR2_SPECIES_CONTROL,.ai=aiCall.ai,.lara=lara,.interest=&aiCall.interest};
        (void)code_call("tr2_creatures",(uint32_t)(uintptr_t)&scall);
    }
    aiCall.op=TR2_AI_STRIDE;
    (void)code_call("tr2_ai",(uint32_t)(uintptr_t)&aiCall);
}

static void updateWallItem(ItemObj *item,Lara *lara,int32 listenerX,int32 listenerZ)
{
    GameItemCall call={.op=GAME_ITEM_UPDATE,.self=item,.lara=lara,.x=listenerX,.z=listenerZ};
    uint32 floorWord = code_call("tr2_wall",(uint32_t)(uintptr_t)&call);
    if (tr2BlockType(item->type)) {
        const FloorData *trigger = gs->blockTrigger;
        gs->blockTrigger = NULL;
        if (trigger)
            game_check_trigger(trigger,NULL);
    }
    if (item->type != TR2_ITEM_BOULDER)
        return;
    /* The heavy action can dispatch a creature wake. The Wall unit must
     * have returned, and the action must precede this boulder's next probe. */
    if (floorWord)
        game_check_trigger(gs->level.floors + (floorWord-1),NULL);
    /* The ahead rest is inside the boulder update. It has already returned. */
}

void game_tr2_update_item(ItemObj *item, Lara *lara, int32 listenerX, int32 listenerZ)
{
    if (wallUnitType(item->type)) {
        updateWallItem(item,lara,listenerX,listenerZ);
    } else if (TR64_RULE(CREATURES) && game_tr2_creature_type(item->type)) {
        game_tr2_update_creature(item,lara);
    } else if (TR64_RULE(ARMS) && item->type == TR2_ITEM_LOOSE_FLARE) {
        GameItemCall call={.op=GAME_ITEM_UPDATE,.self=item,.lara=lara};
        (void)code_call("tr2_wall",(uint32_t)(uintptr_t)&call);
    } else if (operaUnitType(item->type)) {
        TR2OperaCall call={.op=TR2_OPERA_UPDATE,.self=item,.lara=lara};
        uint32 floorWord = code_call("tr2_opera",(uint32_t)(uintptr_t)&call);
        if (item->type == TR2_ITEM_ROLLING_CRATE && floorWord)
            game_check_trigger(gs->level.floors + (floorWord - 1u), NULL);
    } else if (boatUnitType(item->type)) {
        TR2BoatCall call={.op=TR2_BOAT_UPDATE,.self=item,.lara=lara};
        uint32 res = code_call("tr2_boat",(uint32_t)(uintptr_t)&call);
        if (res == TR2_BOAT_RES_TRIGGER) {
            game_check_trigger(call.trigger, lara);
            game_check_trigger(call.trigger, NULL);
            call.op = TR2_BOAT_UPDATE_POST;
            (void)code_call("tr2_boat",(uint32_t)(uintptr_t)&call);
        } else if (res == TR2_BOAT_RES_MINE_TRIGGER) {
            game_check_trigger(call.trigger, NULL);
            call.op = TR2_BOAT_MINE_POST;
            (void)code_call("tr2_boat",(uint32_t)(uintptr_t)&call);
        }
    } else if (item->type == TR2_ITEM_WINDOW) {
        TR2UrbanCall call={.op=TR2_URBAN_WINDOW_UPDATE,.self=item,.lara=lara};
        (void)code_call("tr2_urban",(uint32_t)(uintptr_t)&call);
    } else if (item->type == TR2_ITEM_BIRDS) {
        /* Ambient singing-birds emitter: no item motion or gameplay state change. */
    } else if (item->type == TR2_ITEM_WATERFALL) {
        if (abs(i32_sub(item->pos.x, lara->pos.x)) <= 10240 &&
            abs(i32_sub(item->pos.y, lara->pos.y)) <= 10240 &&
            abs(i32_sub(item->pos.z, lara->pos.z)) <= 10240)
            sys_error("Tomb Raider II: unsupported waterfall mist emitter (item=%u)",
                      (unsigned)(item - gs->items));
    }
}

const char *game_tr2_inert_interaction(unsigned type)
{
    static const struct { uint8_t lo, hi; const char *name; } ranges[] = {
        {TR2_ITEM_BOAT, TR2_ITEM_BOAT, "speedboat"},
        {TR2_ITEM_DOBERMAN, TR2_ITEM_DOBERMAN, "Doberman"},
        {TR2_ITEM_GUNMAN_1, TR2_ITEM_GUNMAN_3, "gunman"},
        {TR2_ITEM_OPERA_BOSS, TR2_ITEM_OPERA_BOSS, "Opera boss"},
        {TR2_ITEM_RAT, TR2_ITEM_RAT, "canal rat"},
        {TR2_ITEM_GONDOLA, TR2_ITEM_GONDOLA, "gondola"},
        {TR2_ITEM_THUG, TR2_ITEM_THUG, "club thug"},
        {TR2_ITEM_COLLAPSE, TR2_ITEM_COLLAPSE, "collapsing tile"},
        {TR2_ITEM_COLLAPSE_HIGH, TR2_ITEM_COLLAPSE_HIGH, "collapsing tile"},
        {TR2_ITEM_SWING_SANDBAG, TR2_ITEM_SWING_SANDBAG, "swinging sandbag"},
        {59, 59, "stationary spikes"},
        {60, 60, "rolling boulder"},
        {61, 62, "flying disc/launcher"},
        {TR2_ITEM_DRAWBRIDGE, TR2_ITEM_DRAWBRIDGE, "drawbridge"},
        {TR2_ITEM_ELEVATOR, TR2_ITEM_ELEVATOR, "elevator"},
        {TR2_ITEM_BLOCK_FIRST, TR2_ITEM_BLOCK_LAST, "push block"},
        {TR2_ITEM_WINDOW, TR2_ITEM_WINDOW, "breakable window"},
        {TR2_ITEM_FAN_PROP, TR2_ITEM_FAN_PROP, "spinning fan"},
        {TR2_ITEM_FALLING_CEILING, TR2_ITEM_FALLING_CEILING, "falling sandbag"},
        {80, 80, "rolling blade"},
        {81, 81, "guillotine"},
        {TR2_ITEM_STATUE_BLADE, TR2_ITEM_STATUE_BLADE, "knife statue"},
        {TR2_ITEM_ROLLING_CRATE, TR2_ITEM_ROLLING_CRATE, "rolling crate"},
        {85, 85, "spiked wall"},
        {92, 93, "other switch variant"},
        {TR2_ITEM_FAN_AIR, TR2_ITEM_FAN_AIR, "air fan"},
        {TR2_ITEM_SWING_CRATE, TR2_ITEM_SWING_CRATE, "swinging crate"},
        {102, 102, "zip line"},
        {103, 103, "other switch variant"},
        {105, 105, "other switch variant"},
        {TR2_ITEM_TRAPDOOR_FIRST, TR2_ITEM_TRAPDOOR_LAST, "hinged trapdoor"},
        {135, 141, "unsupported pickup"},
        {145, 148, "unsupported pickup"},
        {152, 152, "loose flare retrieval"},
        {174, 177, "unsupported pickup"},
        {182, 189, "puzzle socket"},
        {195, 196, "unsupported pickup"},
        {201, 204, "keyhole"},
        {205, 206, "unsupported pickup"},
        {TR2_ITEM_BIRDS, TR2_ITEM_BIRDS, "singing birds"},
        {TR2_ITEM_RACE_CLOCK, TR2_ITEM_RACE_CLOCK, "race clock"},
        {TR2_ITEM_MINE, TR2_ITEM_MINE, "aquatic mine"},
        {TR2_ITEM_DETONATOR, TR2_ITEM_DETONATOR, "detonator box"},
        {TR2_ITEM_WATERFALL, TR2_ITEM_WATERFALL, "waterfall mist"},
        {TR2_ITEM_GRENADE_PROJ, TR2_ITEM_GRENADE_PROJ, "grenade"}
    };
    for (unsigned i = 0; i < sizeof(ranges)/sizeof(ranges[0]); ++i)
        if (type >= ranges[i].lo && type <= ranges[i].hi)
            return ranges[i].name;
    return NULL;
}

void game_tr2_refuse_interaction(const ItemObj *item, const char *operation)
{
    const char *name = game_tr2_inert_interaction(item->type);
    sys_error("Tomb Raider II: %s: %s (type=%u item=%u tick=%u count=%u)",
              name ? name : "interaction unavailable", operation, item->type,
              (unsigned)(item-gs->items), (unsigned)gs->tick+1,
              (unsigned)++gs->tr2->interactionRefusals);
}

bool game_tr2_interact(ItemObj *item, Lara *lara)
{
    if (operaUnitType(item->type)) {
        if (!gs->tr2->operaUnit)
            return false;
        TR2OperaCall call={.op=TR2_OPERA_COLLIDE,.self=item,.lara=lara};
        return code_call("tr2_opera",(uint32_t)(uintptr_t)&call)==TR2_INTERACT_BUMP;
    }
    if (boatUnitType(item->type)) {
        if (!gs->tr2->boatUnit)
            return false;
        TR2BoatCall call={.op=TR2_BOAT_INTERACT,.self=item,.lara=lara};
        return code_call("tr2_boat",(uint32_t)(uintptr_t)&call)==TR2_BOAT_RES_BUMP;
    }
    if (item->type == TR2_ITEM_WINDOW) {
        if (!gs->tr2->urbanUnit)
            return false;
        TR2UrbanCall call={.op=TR2_URBAN_WINDOW_INTERACT,.self=item,.lara=lara};
        return code_call("tr2_urban",(uint32_t)(uintptr_t)&call)!=0;
    }
    if (!gs->tr2->wallUnit)
        return false;
    const bool freeAct = (lara->input & IN_ACTION) &&
                         lara->extraL->weaponState == WEAPON_STATE_FREE;
    if (!freeAct) {
        if (item->type == TR2_ITEM_WALL_SWITCH)
            return false;
        if (game_tr2_pickup_type(item->type) && lara->state != Lara_STATE_PICKUP)
            return false;
    }
    GameItemCall call={.op=GAME_ITEM_COLLIDE,.self=item,.lara=lara};
    uint32 res = code_call("tr2_wall",(uint32_t)(uintptr_t)&call);
    if (res == TR2_INTERACT_CHOOSE_KEY) {
        if (!gs->tr2->chooseKey)
            game_tr2_refuse_interaction(item,"key-ring confirmation required");
        gs->tr2->chooseKey(gs->tr2->chooseKeyContext);
        call.x = 1;
        return code_call("tr2_wall",(uint32_t)(uintptr_t)&call) == TR2_INTERACT_BUMP;
    }
    return res == TR2_INTERACT_BUMP;
}
