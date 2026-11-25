/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/lara.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Ported from src/fixed/lara.h:338-407,4566-4567 at
 * 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"
#include "tr64/code.h"
static void (*const sHandlers[])(Lara *) = {
    Lara_s_STATE_WALK,        Lara_s_STATE_RUN,          Lara_s_STATE_STOP,
    Lara_s_STATE_JUMP,        Lara_s_STATE_POSE,         Lara_s_STATE_BACK_FAST,
    Lara_s_STATE_TURN_RIGHT,  Lara_s_STATE_TURN_LEFT,    Lara_s_STATE_DEATH,
    Lara_s_STATE_FALL,        Lara_s_STATE_HANG,         Lara_s_STATE_REACH,
    Lara_s_STATE_SPLAT,       Lara_s_STATE_UW_TREAD,     Lara_s_STATE_LAND,
    Lara_s_STATE_COMPRESS,    Lara_s_STATE_BACK,         Lara_s_STATE_UW_SWIM,
    Lara_s_STATE_UW_GLIDE,    Lara_s_STATE_HANG_UP,      Lara_s_STATE_TURN_FAST,
    Lara_s_STATE_STEP_RIGHT,  Lara_s_STATE_STEP_LEFT,    Lara_s_STATE_ROLL_END,
    Lara_s_STATE_SLIDE,       Lara_s_STATE_JUMP_BACK,    Lara_s_STATE_JUMP_RIGHT,
    Lara_s_STATE_JUMP_LEFT,   Lara_s_STATE_JUMP_UP,      Lara_s_STATE_FALL_BACK,
    Lara_s_STATE_HANG_LEFT,   Lara_s_STATE_HANG_RIGHT,   Lara_s_STATE_SLIDE_BACK,
    Lara_s_STATE_SURF_TREAD,  Lara_s_STATE_SURF_SWIM,    Lara_s_STATE_UW_DIVE,
    Lara_s_STATE_BLOCK_PUSH,  Lara_s_STATE_BLOCK_PULL,   Lara_s_STATE_BLOCK_READY,
    Lara_s_STATE_PICKUP,      Lara_s_STATE_SWITCH_DOWN,  Lara_s_STATE_SWITCH_UP,
    Lara_s_STATE_USE_KEY,     Lara_s_STATE_USE_PUZZLE,   Lara_s_STATE_DEATH_UW,
    Lara_s_STATE_ROLL_START,  Lara_s_STATE_SPECIAL,      Lara_s_STATE_SURF_BACK,
    Lara_s_STATE_SURF_LEFT,   Lara_s_STATE_SURF_RIGHT,   Lara_s_STATE_USE_MIDAS,
    Lara_s_STATE_DEATH_MIDAS, Lara_s_STATE_SWAN_DIVE,    Lara_s_STATE_FAST_DIVE,
    Lara_s_STATE_HANDSTAND,   Lara_s_STATE_WATER_OUT,    Lara_s_STATE_CLIMB_START,
    Lara_s_STATE_CLIMB_UP,    Lara_s_STATE_CLIMB_LEFT,   Lara_s_STATE_CLIMB_END,
    Lara_s_STATE_CLIMB_RIGHT, Lara_s_STATE_CLIMB_DOWN,   Lara_s_STATE_UNUSED_1,
    Lara_s_STATE_UNUSED_2,    Lara_s_STATE_UNUSED_3,     Lara_s_STATE_WADE,
    Lara_s_STATE_ROLL_UW,     Lara_s_STATE_PICKUP_FLARE, Lara_s_STATE_ROLL_AIR,
    Lara_s_STATE_UNUSED_4,    Lara_s_STATE_ZIPLINE};
static void (*const cHandlers[])(Lara *) = {
    Lara_c_STATE_WALK,        Lara_c_STATE_RUN,          Lara_c_STATE_STOP,
    Lara_c_STATE_JUMP,        Lara_c_STATE_POSE,         Lara_c_STATE_BACK_FAST,
    Lara_c_STATE_TURN_RIGHT,  Lara_c_STATE_TURN_LEFT,    Lara_c_STATE_DEATH,
    Lara_c_STATE_FALL,        Lara_c_STATE_HANG,         Lara_c_STATE_REACH,
    Lara_c_STATE_SPLAT,       Lara_c_STATE_UW_TREAD,     Lara_c_STATE_LAND,
    Lara_c_STATE_COMPRESS,    Lara_c_STATE_BACK,         Lara_c_STATE_UW_SWIM,
    Lara_c_STATE_UW_GLIDE,    Lara_c_STATE_HANG_UP,      Lara_c_STATE_TURN_FAST,
    Lara_c_STATE_STEP_RIGHT,  Lara_c_STATE_STEP_LEFT,    Lara_c_STATE_ROLL_END,
    Lara_c_STATE_SLIDE,       Lara_c_STATE_JUMP_BACK,    Lara_c_STATE_JUMP_RIGHT,
    Lara_c_STATE_JUMP_LEFT,   Lara_c_STATE_JUMP_UP,      Lara_c_STATE_FALL_BACK,
    Lara_c_STATE_HANG_LEFT,   Lara_c_STATE_HANG_RIGHT,   Lara_c_STATE_SLIDE_BACK,
    Lara_c_STATE_SURF_TREAD,  Lara_c_STATE_SURF_SWIM,    Lara_c_STATE_UW_DIVE,
    Lara_c_STATE_BLOCK_PUSH,  Lara_c_STATE_BLOCK_PULL,   Lara_c_STATE_BLOCK_READY,
    Lara_c_STATE_PICKUP,      Lara_c_STATE_SWITCH_DOWN,  Lara_c_STATE_SWITCH_UP,
    Lara_c_STATE_USE_KEY,     Lara_c_STATE_USE_PUZZLE,   Lara_c_STATE_DEATH_UW,
    Lara_c_STATE_ROLL_START,  Lara_c_STATE_SPECIAL,      Lara_c_STATE_SURF_BACK,
    Lara_c_STATE_SURF_LEFT,   Lara_c_STATE_SURF_RIGHT,   Lara_c_STATE_USE_MIDAS,
    Lara_c_STATE_DEATH_MIDAS, Lara_c_STATE_SWAN_DIVE,    Lara_c_STATE_FAST_DIVE,
    Lara_c_STATE_HANDSTAND,   Lara_c_STATE_WATER_OUT,    Lara_c_STATE_CLIMB_START,
    Lara_c_STATE_CLIMB_UP,    Lara_c_STATE_CLIMB_LEFT,   Lara_c_STATE_CLIMB_END,
    Lara_c_STATE_CLIMB_RIGHT, Lara_c_STATE_CLIMB_DOWN,   Lara_c_STATE_UNUSED_1,
    Lara_c_STATE_UNUSED_2,    Lara_c_STATE_UNUSED_3,     Lara_c_STATE_WADE,
    Lara_c_STATE_ROLL_UW,     Lara_c_STATE_PICKUP_FLARE, Lara_c_STATE_ROLL_AIR,
    Lara_c_STATE_UNUSED_4,    Lara_c_STATE_ZIPLINE};
void Lara_updateState(Lara *self)
{
    if (self->state >= Lara_X_MAX)
        sys_error("game Lara state");
    if (self->state < 71)
        sHandlers[self->state](self);
    else
        game_lara_tr3_s_state(self);
}
void game_lara_collision(Lara *self)
{
    ExtraInfoLara *e = self->extraL;
    Lara_updateObjectsCollision(self);
    if (gs->stop.kind != TR64_GAME_STOP_NONE)
        return;
#if TR64_HAS_TR2
    if (TR64_SECOND_GAME && (gs->tr2->vehicle >= 0 || gs->tr2->extraAnim))
        return;
#endif
    if (self->state >= Lara_X_MAX)
        sys_error("game Lara collision state");
    if (self->state < 71)
        cHandlers[self->state](self);
    else
        game_lara_tr3_c_state(self);
    if (gs->stop.kind != TR64_GAME_STOP_NONE)
        return;
    if (e->hitTimer <= 0)
        return;
    if (!e->hitFrame)
        soundPlay(SND_HIT, &self->pos);
    if (++e->hitFrame > 34)
        e->hitFrame = 34;
    if (--e->hitTimer == 0)
    {
        e->hitQuadrant = -1;
        e->hitFrame = 0;
    }
}
static const char *familyName(uint32 type)
{
    static const char *const family[ITEM_ADAM + 1] = {
        [ITEM_DOPPELGANGER] = "enemy_doppelganger", [ITEM_WOLF] = "enemy_peru",
        [ITEM_BEAR] = "enemy_peru", [ITEM_BAT] = "enemy_peru",
        [ITEM_CROCODILE_LAND] = "enemy_greece", [ITEM_CROCODILE_WATER] = "enemy_greece",
        [ITEM_LION_MALE] = "enemy_greece", [ITEM_LION_FEMALE] = "enemy_greece",
        [ITEM_PUMA] = "enemy_greece", [ITEM_GORILLA] = "enemy_greece",
        [ITEM_RAT_LAND] = "enemy_rat", [ITEM_RAT_WATER] = "enemy_rat",
        [ITEM_REX] = "enemy_dinosaurs", [ITEM_RAPTOR] = "enemy_dinosaurs",
        [ITEM_MUTANT_1] = "enemy_mutant", [ITEM_MUTANT_2] = "enemy_mutant",
        [ITEM_MUTANT_3] = "enemy_mutant", [ITEM_CENTAUR] = "enemy_atlanteans",
        [ITEM_MUMMY] = "enemy_atlanteans", [ITEM_LARSON] = "enemy_gunmen",
        [ITEM_PIERRE] = "enemy_gunmen", [ITEM_SKATER] = "enemy_henchmen",
        [ITEM_COWBOY] = "enemy_henchmen", [ITEM_MR_T] = "enemy_henchmen",
        [ITEM_NATLA] = "enemy_natla", [ITEM_ADAM] = "enemy_adam"
    };
    if (type <= ITEM_ADAM)
        return family[type];
    if (type == ITEM_MUTANT_EGG_SMALL || type == ITEM_MUTANT_EGG_BIG ||
        type == ITEM_CENTAUR_STATUE)
        return "enemy_encounter";
    return NULL;
}
void game_load_item_units(void)
{
    uint32 i;
    code_loading_begin();
    if (!code_is_loaded("objects"))
        code_load("objects");
    if (!code_is_loaded("inv"))
        code_load("inv");
    if (!code_is_loaded("save"))
        code_load("save");
    for (i = 0; i < MAX_ITEMS; ++i)
    {
        const char *name;
        if (!gs->items[i].room)
            continue;
        name = familyName(gs->items[i].type);
        if (name && !code_is_loaded(name))
            code_load(name);
    }
    code_loading_end();
}
void game_unload_item_units(void)
{
    static const char *const names[] = {
        "objects", "enemy_peru", "enemy_greece", "enemy_rat", "enemy_dinosaurs",
        "enemy_mutant", "enemy_atlanteans", "enemy_gunmen", "enemy_henchmen",
        "enemy_natla", "enemy_adam", "enemy_doppelganger", "enemy_encounter", "inv", "save"
    };
    uint32 i;
    code_loading_begin();
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (code_is_loaded(names[i]))
            code_unload(names[i]);
    code_loading_end();
}
#if !TR64_CART_TR2
static void itemUnit(const char *name,GameItemCall *call)
{
    code_call(name,(uint32)(uintptr_t)call);
    game_inventory_flush_open();
}
#endif
void game_item_call(GameItemCall *call)
{
    ItemObj *self = call->self;
#if !TR64_CART_TR2
    const char *family;
#endif
    if (!self)
        sys_error("game item operation without item");
    if (gs->stop.kind != TR64_GAME_STOP_NONE)
        return;
#if TR64_CART_TR2
    /* The sequel's authored items use the TR2 update/activation path.
     * These IDs must never reach the first game's constructors or units. */
    if (call->op == GAME_ITEM_HIT && self->type == ITEM_LARA) {
        Lara_hit(self, call->damage, call->point, call->soundId);
        return;
    }
    sys_error("Tomb Raider II: unsupported item dispatch operation %u type %u",
              (unsigned)call->op,(unsigned)self->type);
#else
    family = familyName(self->type);
    if (family && call->op != GAME_ITEM_GEOMETRY)
    {
        bool encounter = self->type == ITEM_MUTANT_EGG_SMALL ||
                         self->type == ITEM_MUTANT_EGG_BIG || self->type == ITEM_CENTAUR_STATUE;
        if (call->op == GAME_ITEM_CONSTRUCT)
        {
            if (encounter) Encounter_construct(self);
            else Enemy_construct(self);
            call->result = 1;
        }
        else if (call->op == GAME_ITEM_ACTIVATE)
        {
            if (encounter) Encounter_activate(self);
            else if (self->type == ITEM_SKATER) Skater_activate(self);
            else Enemy_activate(self);
        }
        else
            itemUnit(family,call);
        return;
    }
    switch (call->op)
    {
    case GAME_ITEM_CONSTRUCT:
        call->result = game_effect_init(self) || Object_construct(self);
        return;
    case GAME_ITEM_ACTIVATE:
        Object_activate(self);
        ItemObj_activateBase(self);
        return;
    case GAME_ITEM_UPDATE:
        if (!game_effect_update(self))
            itemUnit("objects",call);
        return;
    case GAME_ITEM_COLLIDE:
        if (self->type == ITEM_LARA || self->type == ITEM_SPLASH ||
            self->type == ITEM_BLOOD || self->type == ITEM_SMOKE ||
            self->type == ITEM_RICOCHET || self->type == ITEM_SPARKLES ||
            self->type == ITEM_BUBBLE || self->type == ITEM_EXPLOSION ||
            self->type == ITEM_MUZZLE_FLASH)
            return;
        if (self->type == ITEM_NATLA_BULLET || self->type == ITEM_MUTANT_BULLET ||
            self->type == ITEM_CENTAUR_BULLET)
            Bullet_collide(self, call->lara, &gs->cinfo);
        else
            itemUnit("objects",call);
        return;
    case GAME_ITEM_HIT:
        if (self->type == ITEM_LARA)
            Lara_hit(self, call->damage, call->point, call->soundId);
        else if (self->type == ITEM_SCION_TARGET)
            itemUnit("objects",call);
        return;
    case GAME_ITEM_GEOMETRY:
        ItemObj_getItemFloorCeiling(self, call->x, call->y, call->z,
                                   call->floor, call->ceiling);
        return;
    }
    sys_error("game item operation invalid");
#endif
}
void ItemObj_activate(ItemObj *self)
{
    GameItemCall call = {0};
    call.op = GAME_ITEM_ACTIVATE;
    call.self = self;
    game_item_call(&call);
}
void game_item_activate(ItemObj *self)
{
    ItemObj_activate(self);
}
void ItemObj_deactivate(ItemObj *self)
{
#if TR64_CART_TR2
    ItemObj_deactivateBase(self);
#else
    if (self->type == ITEM_SKATER)
        Skater_deactivate(self);
    else
    {
        ItemObj_deactivateBase(self);
        Object_deactivate(self);
    }
#endif
}
void ItemObj_hitBy(ItemObj *self,Lara *shooter,int32 damage,const vec3i *point,int32 soundId)
{
    GameItemCall call = {0};
    call.op = GAME_ITEM_HIT;
    call.self = self;
    call.lara = shooter;
    call.damage = damage;
    call.point = point;
    call.soundId = soundId;
    game_item_call(&call);
}
void ItemObj_hit(ItemObj *self,int32 damage,const vec3i *point,int32 soundId)
{
    ItemObj_hitBy(self,NULL,damage,point,soundId);
}
