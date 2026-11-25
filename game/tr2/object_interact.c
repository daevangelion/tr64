/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/object.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a (reach and alignment primitives). */
#include "objects.h"
#include "../object.h"

/* Both cases use a level or pitched frame with no roll. Rotate the world
 * delta with the inverse of its Y-X orientation, rounding at Q14. */
static vec3i interactionLocal(vec3i d, int16 pitch, int16 yaw)
{
    int32 c = tr2_sin(yaw + ANGLE_90), s = tr2_sin(yaw);
    int32 cz = tr2_sin(pitch + ANGLE_90), sz = tr2_sin(pitch);
    return vec3i_create(q14((int64_t)c*d.x - (int64_t)s*d.z),
                        q14((int64_t)q14(sz*s)*d.x + (int64_t)cz*d.y + q14(sz*c)*d.z),
                        q14((int64_t)q14(cz*s)*d.x - (int64_t)sz*d.y + q14(cz*c)*d.z));
}

static int32 angleDistance(int16 a, int16 b)
{
    return abs(wrap_i16((int32)a - b));
}

static bool pickupReach(const ItemObj *item, const Lara *lara, bool wet)
{
    int16 pitch = wet ? -4550 : 0;
    vec3i d = interactionLocal(vec3i_sub(lara->pos,item->pos), pitch, lara->angle.y);
    unsigned reach = wet ? 512 : 256, vertical = wet ? 512 : 100;
    return abs(d.x) <= (int32)reach && abs(d.z) <= (int32)reach &&
           abs(d.y) <= (int32)vertical && angleDistance(lara->angle.x, pitch) <= (wet ? 8190 : 1820) &&
           angleDistance(lara->angle.z, 0) <= (wet ? 8190 : 0);
}

static bool alignPickup(const ItemObj *item, Lara *lara, bool wet)
{
    int32 s = tr2_sin(lara->angle.y), c = tr2_sin(lara->angle.y + ANGLE_90);
    int32 y = 0, z = -100;
    if (wet) {
        int32 cp = tr2_sin(ANGLE_90 - 4550), sp = tr2_sin(-4550);
        y = q14(-200*cp + 350*sp);
        z = q14(-200*sp - 350*cp);
    }
    vec3i dst = vec3i_add(item->pos,vec3i_create(q14(s*z), y, q14(c*z)));
    if (!wet) {
        lara->pos = dst;
        lara->angle.x = lara->angle.z = 0;
        return true;
    }
    vec3i d = vec3i_sub(dst,lara->pos);
    uint32 square = d.x*d.x + d.y*d.y + d.z*d.z;
    if (square <= 16*16)
        lara->pos = dst;
    else {
        /* OpenLara fixed object.h ItemObj::moveTo: capped linear and
         * angular approach. Round the length up to keep the cap. */
        int32 dist = phd_sqrt(square);
        if ((uint32)(dist*dist) < square)
            ++dist;
        lara->pos.x += d.x*16 / dist;
        lara->pos.y += d.y*16 / dist;
        lara->pos.z += d.z*16 / dist;
    }
    lara->angle.x = angleLerp(lara->angle.x,-4550,364);
    lara->angle.z = angleLerp(lara->angle.z,0,364);
    return vec3i_equal(lara->pos,dst) && lara->angle.x == -4550 && !lara->angle.z;
}

static bool freeAction(const Lara *lara)
{
    return (lara->input & IN_ACTION) && lara->extraL->weaponState == WEAPON_STATE_FREE;
}

__attribute__((optimize("Os")))
void game_tr2_interact_pickup(ItemObj *item, Lara *lara)
{
    bool wet = lara->waterState == WATER_STATE_UNDER;
    if (!wet && lara->waterState != WATER_STATE_ABOVE && lara->waterState != WATER_STATE_WADE)
        return;
    unsigned clip = wet ? PICKUP_SWIM : PICKUP_STAND;
    unsigned idle = wet ? Lara_STATE_UW_TREAD : Lara_STATE_STOP;
    if (lara->state == Lara_STATE_PICKUP) {
        unsigned frame = tr64_u16(gs->level.anims[clip].frame_start) +
                         (wet ? PICKUP_SWIM_COMMIT : PICKUP_STAND_COMMIT);
        if (lara->animIndex != clip || lara->frameIndex != frame || !pickupReach(item,lara,wet))
            return;
        game_tr2_pickup_credit(item);
        game_pickup_notify(item,lara);
        Room_remove(item->room,item);
        /* Keep authored slot and room identity for the pickup floor action. */
        item->flags = (item->flags & ~ITEM_FLAG_STATUS) | ITEM_FLAG_STATUS_INVISIBLE;
        ++gs->tr2->pickupCommits;
    } else if (lara->state == idle && freeAction(lara) &&
               (wet || !(lara->flags & ITEM_FLAG_GRAVITY)) &&
               pickupReach(item,lara,wet) && alignPickup(item,lara,wet)) {
        ItemObj_animSet(lara,clip,true,0);
        lara->goalState = idle;
        lara->extraL->weaponState = WEAPON_STATE_BUSY;
    }
}

/* A plain lever starts Lara at the first frame, turns her yaw but does
 * not translate her, and advances the lever itself without adding mask bits. */
__attribute__((optimize("Os")))
void game_tr2_interact_switch(ItemObj *item, Lara *lara)
{
    if (lara->waterState != WATER_STATE_ABOVE || lara->state != Lara_STATE_STOP ||
        !freeAction(lara) || (lara->flags & ITEM_FLAG_GRAVITY) ||
        (item->flags & ITEM_FLAG_STATUS) || item->state > 1)
        return;
    vec3i d = interactionLocal(vec3i_sub(lara->pos,item->pos),0,item->angle.y);
    if (d.x < -220 || d.x > 220 || d.y || d.z < 292 || d.z > 512 ||
        angleDistance(lara->angle.x,item->angle.x) > 1820 ||
        angleDistance(lara->angle.y,item->angle.y) > 5460 ||
        angleDistance(lara->angle.z,item->angle.z) > 1820)
        return;
    lara->angle.y = item->angle.y;
    ItemObj_animSet(lara,item->state == 1 ? SWITCH_TO_0 : SWITCH_TO_1,true,0);
    lara->goalState = Lara_STATE_STOP;
    lara->extraL->weaponState = WEAPON_STATE_BUSY;
    item->goalState = !item->state;
    game_tr2_activate_item(item);
    ItemObj_animProcess(item,true);
    ++gs->tr2->switchUses;
}

void game_tr2_switch_update(ItemObj *item)
{
    item->flags |= ITEM_FLAG_MASK;
    if (!Object_isActive(item)) {
        item->goalState = 1;
        item->timer = 0;
    }
    ItemObj_animProcess(item,true);
}

/* tr2-triggers-and-interaction.md, Levers and button switches:
 * types 93 (snap +362, clips 195/196), 103 (snap +292, clip 197),
 * and 105 (submerged approach to (0,0,108), clip 129). */
bool __attribute__((optimize("Os"))) game_tr2_extra_switch_interact(ItemObj *item, Lara *lara)
{
    enum {
        TR2_LARA_SWITCH_SMALL_0 = 195,
        TR2_LARA_SWITCH_SMALL_1 = 196,
        TR2_LARA_SWITCH_BUTTON = 197,
        TR2_LARA_SWITCH_UW = 129
    };
    if (!freeAction(lara) || (item->flags & ITEM_FLAG_STATUS) || item->state > 1)
        return false;

    if (item->type == TR2_ITEM_UW_SWITCH) {
        if (lara->waterState != WATER_STATE_UNDER || lara->state != Lara_STATE_UW_TREAD)
            return false;
        vec3i d = interactionLocal(vec3i_sub(lara->pos, item->pos), 0, item->angle.y);
        if (abs(d.x) > 1024 || abs(d.y) > 1024 || d.z < -1024 || d.z > 512 ||
            angleDistance(lara->angle.x, item->angle.x) > 14560 ||
            angleDistance(lara->angle.y, item->angle.y) > 14560 ||
            angleDistance(lara->angle.z, item->angle.z) > 14560)
            return false;
        vec3i dst = vec3i_add(item->pos, game_tr2_heading_step(item->angle.y, 108));
        vec3i delta = vec3i_sub(dst, lara->pos);
        uint32 sq = (uint32)(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        if (sq <= 16u * 16u) {
            lara->pos = dst;
        } else {
            int32 dist = phd_sqrt(sq);
            if ((uint32)(dist * dist) < sq)
                ++dist;
            lara->pos.x += delta.x * 16 / dist;
            lara->pos.y += delta.y * 16 / dist;
            lara->pos.z += delta.z * 16 / dist;
        }
        lara->angle.x = angleLerp(lara->angle.x, item->angle.x, 364);
        lara->angle.y = angleLerp(lara->angle.y, item->angle.y, 364);
        lara->angle.z = angleLerp(lara->angle.z, item->angle.z, 364);
        if (!vec3i_equal(lara->pos, dst) || lara->angle.x != item->angle.x ||
            lara->angle.y != item->angle.y || lara->angle.z != item->angle.z)
            return false;
        lara->hSpeed = 0;
        lara->vSpeed = 0;
        ItemObj_animSet(lara, TR2_LARA_SWITCH_UW, true, 0);
        lara->goalState = Lara_STATE_UW_TREAD;
        lara->extraL->weaponState = WEAPON_STATE_BUSY;
        item->goalState = !item->state;
        game_tr2_activate_item(item);
        ItemObj_animProcess(item, true);
        ++gs->tr2->switchUses;
        return false;
    }

    if (lara->waterState != WATER_STATE_ABOVE || lara->state != Lara_STATE_STOP ||
        (lara->flags & ITEM_FLAG_GRAVITY))
        return false;
    vec3i d = interactionLocal(vec3i_sub(lara->pos, item->pos), 0, item->angle.y);
    if (d.x < -220 || d.x > 220 || d.y || d.z < 292 || d.z > 512 ||
        angleDistance(lara->angle.x, item->angle.x) > 1820 ||
        angleDistance(lara->angle.y, item->angle.y) > 5460 ||
        angleDistance(lara->angle.z, item->angle.z) > 1820)
        return false;

    int32 snapZ = item->type == TR2_ITEM_SMALL_SWITCH ? 362 : 292;
    lara->pos = vec3i_add(item->pos, game_tr2_heading_step(item->angle.y, snapZ));
    lara->angle = item->angle;
    unsigned clip = item->type == TR2_ITEM_PUSH_BUTTON
                        ? TR2_LARA_SWITCH_BUTTON
                        : (item->state == 1 ? TR2_LARA_SWITCH_SMALL_0 : TR2_LARA_SWITCH_SMALL_1);
    ItemObj_animSet(lara, clip, true, 0);
    lara->goalState = Lara_STATE_STOP;
    lara->extraL->weaponState = WEAPON_STATE_BUSY;
    item->goalState = !item->state;
    game_tr2_activate_item(item);
    ItemObj_animProcess(item, true);
    ++gs->tr2->switchUses;
    return false;
}

static bool actionNear(const ItemObj *item, const Lara *lara, vec3i pos, int16 yaw,
                       int32 x, int32 y, int32 zMin, int32 zMax,
                       int32 pitch, int32 turn, int32 roll)
{
    vec3i d = interactionLocal(vec3i_sub(pos,item->pos),item->angle.x,yaw);
    return d.x >= -x && d.x <= x && d.y >= -y && d.y <= y &&
           d.z >= zMin && d.z <= zMax &&
           angleDistance(lara->angle.x,item->angle.x) <= pitch &&
           angleDistance(lara->angle.y,yaw) <= turn &&
           angleDistance(lara->angle.z,item->angle.z) <= roll;
}

/* tr2-triggers-and-interaction, Keys and puzzle sockets: reach admits the
 * menu, then the chosen key must still match; the authored use pose is
 * the commitment point. Route choices enter at the same contact. */
void game_tr2_finish_keyhole(ItemObj *item, Lara *lara)
{
    GameTR2 *tr2 = gs->tr2;
    unsigned key = item->type - 201 + 197;
    unsigned chosen=tr2->selectedKey;
    tr2->selectedKey=0;
    if (chosen!=key) return; /* Cancel, wrong key, or no carried keys. */
    TR2RingEntry *entry=game_tr2_ring_find(key);
    if (!entry || !entry->quantity)
        game_tr2_refuse_interaction(item,"selected key is not carried");
    game_tr2_ring_debit(entry);

    lara->pos = vec3i_add(item->pos,game_tr2_heading_step(item->angle.y,362));
    lara->angle = item->angle;
    ItemObj_animSet(lara,KEY_USE,true,0);
    lara->goalState = Lara_STATE_STOP;
    lara->extraL->weaponState = WEAPON_STATE_BUSY;
    item->flags |= ITEM_FLAG_STATUS_ACTIVE;
}

static uint32 useKeyhole(ItemObj *item, Lara *lara)
{
    GameTR2 *tr2 = gs->tr2;
    if (item->flags & ITEM_FLAG_STATUS) {
        tr2->selectedKey=0; /* Spent: no reopening and no stock debit. */
        tr2->keyReplayChoice=0;
        tr2->keyPauseTicks=0;
        return TR2_INTERACT_NONE;
    }

    if (tr2->keyReplayChoice) {
        unsigned type=tr2->keyReplayChoice-1;
        tr2->keyReplayChoice=0;
        game_tr2_ring_commit(type,tr2->keyPauseTicks);
        tr2->keyPauseTicks=0;
    } else if (!tr2->selectedKey) {
        /* Return to resident code before opening the interactive key ring
         * so it can load and call the inventory unit without unit re-entry. */
        return TR2_INTERACT_CHOOSE_KEY;
    }
    game_tr2_finish_keyhole(item,lara);
    return TR2_INTERACT_NONE;
}

/* tr2-opera-house.md §1: Puzzle sockets (182..185) and completed models (186..189). */
static void puzzleRefuse(Lara *lara)
{
    GameTR2 *tr2 = gs->tr2;
    tr2->selectedKey = 0;
    if (!tr2->refusalValid || !vec3i_equal(lara->pos, tr2->refusalPos)) {
        soundPlay(SND_NO, &lara->pos);
        tr2->refusalPos = lara->pos;
        tr2->refusalValid = true;
    }
}

void __attribute__((optimize("Os"))) game_tr2_finish_puzzle(ItemObj *item, Lara *lara, bool afterRing)
{
    GameTR2 *tr2 = gs->tr2;
    unsigned chosen = tr2->selectedKey;
    tr2->selectedKey = 0;
    if (afterRing && !chosen) {
        if (!game_tr2_ring_has_keys())
            puzzleRefuse(lara);
        return;
    }
    if (chosen && tr2->refusalValid)
        tr2->refusalPos.y = i32_sub(tr2->refusalPos.y, 1);
    unsigned expected = item->type - 182u + 178u;
    if (chosen != expected || (item->flags & ITEM_FLAG_STATUS) != 0) {
        puzzleRefuse(lara);
        return;
    }
    TR2RingEntry *entry = game_tr2_ring_find(expected);
    if (!entry || !entry->quantity) {
        puzzleRefuse(lara);
        return;
    }
    game_tr2_ring_debit(entry);

    lara->pos = vec3i_add(item->pos, game_tr2_heading_step(item->angle.y, 327));
    lara->angle = item->angle;
    ItemObj_animSet(lara, PUZZLE_USE, true, 0);
    lara->goalState = Lara_STATE_STOP;
    lara->extraL->weaponState = WEAPON_STATE_BUSY;
    item->flags = (item->flags & ~ITEM_FLAG_STATUS) | ITEM_FLAG_STATUS_ACTIVE;
    tr2->refusalPos = lara->pos;
    tr2->refusalValid = true;
}

uint32 __attribute__((optimize("Os"))) game_tr2_interact_puzzle(ItemObj *item, Lara *lara)
{
    if (item->type >= 186u && item->type <= 189u)
        return TR2_INTERACT_NONE;
    if (lara->state == Lara_STATE_USE_PUZZLE) {
        unsigned base = tr64_u16(gs->level.anims[PUZZLE_USE].frame_start);
        if (lara->animIndex == PUZZLE_USE &&
            lara->frameIndex == base + 80u &&
            actionNear(item, lara, lara->pos, item->angle.y, 200, 0, 312, 512, 1820, 5460, 1820))
            item->type = (uint16)(item->type + 4u);
        return TR2_INTERACT_NONE;
    }
    GameTR2 *tr2 = gs->tr2;
    const bool haveAction = (lara->input & IN_ACTION) || tr2->selectedKey || tr2->keyReplayChoice;
    if (!haveAction || lara->state != Lara_STATE_STOP ||
        lara->extraL->weaponState != WEAPON_STATE_FREE ||
        (lara->flags & ITEM_FLAG_GRAVITY) ||
        (lara->waterState != WATER_STATE_ABOVE && lara->waterState != WATER_STATE_WADE) ||
        !actionNear(item, lara, lara->pos, item->angle.y, 200, 0, 312, 512, 1820, 5460, 1820))
        return TR2_INTERACT_NONE;

    if ((item->flags & ITEM_FLAG_STATUS) != 0) {
        if (tr2->keyReplayChoice) {
            unsigned type = tr2->keyReplayChoice - 1u;
            tr2->keyReplayChoice = 0;
            game_tr2_ring_commit(type, tr2->keyPauseTicks);
            tr2->keyPauseTicks = 0;
        }
        if (tr2->selectedKey && tr2->refusalValid)
            tr2->refusalPos.y = i32_sub(tr2->refusalPos.y, 1);
        puzzleRefuse(lara);
        return TR2_INTERACT_NONE;
    }
    if (tr2->keyReplayChoice) {
        unsigned type = tr2->keyReplayChoice - 1u;
        tr2->keyReplayChoice = 0;
        game_tr2_ring_commit(type, tr2->keyPauseTicks);
        tr2->keyPauseTicks = 0;
        game_tr2_finish_puzzle(item, lara, type == 0);
        return TR2_INTERACT_NONE;
    }
    if (!tr2->selectedKey) {
        if (!game_tr2_ring_has_keys()) {
            puzzleRefuse(lara);
            return TR2_INTERACT_NONE;
        }
        return TR2_INTERACT_CHOOSE_KEY;
    }
    game_tr2_finish_puzzle(item, lara, false);
    return TR2_INTERACT_NONE;
}

/* tr2-bartoli-and-weapons.md §2: Detonator box (type 227), hard-bound to
 * Key 2 (ring 198). Wrong key or spent box falls through silently to solid
 * collision without refusal sound 2. */
uint32 game_tr2_finish_detonator(ItemObj *item, Lara *lara)
{
    GameTR2 *tr2 = gs->tr2;
    unsigned chosen = tr2->selectedKey;
    tr2->selectedKey = 0;
    if (chosen != 198u)
        return game_tr2_solid_contact(item, lara, true, false) == TR2_SOLID_BUMP ?
               TR2_INTERACT_BUMP : TR2_INTERACT_NONE;
    TR2RingEntry *entry = game_tr2_ring_find(198u);
    if (!entry || !entry->quantity)
        game_tr2_refuse_interaction(item, "selected key is not carried");
    game_tr2_ring_debit(entry);

    item->angle = vec3s_create(0, lara->angle.y, 0);
    if (lara->room != item->room) {
        Room_remove(lara->room, lara);
        Room_add(item->room, lara);
    }
    lara->pos = item->pos;
    lara->angle.x = 0;
    lara->angle.z = 0;
    unsigned base = gs->level.models[TR2_MODEL_LARA_EXTRA].animIndex;
    ItemObj_animSet(lara, base, true, 0);
    lara->goalState = 1;
    ItemObj_animProcess(lara, true);
    tr2->extraAnim = true;
    lara->extraL->weaponState = WEAPON_STATE_BUSY;
    item->flags = (item->flags & ~ITEM_FLAG_STATUS) | ITEM_FLAG_STATUS_ACTIVE;
    ItemObj_activateBase(item);
    return TR2_INTERACT_NONE;
}

uint32 __attribute__((optimize("Os"))) game_tr2_interact_detonator(ItemObj *item, Lara *lara)
{
    GameTR2 *tr2 = gs->tr2;
    const bool preselected = tr2->selectedKey != 0 &&
        !(lara->flags & ITEM_FLAG_GRAVITY) &&
        lara->waterState == WATER_STATE_ABOVE &&
        lara->extraL->weaponState == WEAPON_STATE_FREE;
    const bool eligible = !tr2->extraAnim &&
        (item->flags & ITEM_FLAG_STATUS) == 0 &&
        (freeAction(lara) || preselected) &&
        !(lara->flags & ITEM_FLAG_GRAVITY) &&
        lara->state == Lara_STATE_STOP;
    if (eligible) {
        vec3i d = interactionLocal(vec3i_sub(lara->pos, item->pos), 0, lara->angle.y);
        if (d.x >= -256 && d.x <= 256 && d.y >= -100 && d.y <= 100 &&
            d.z >= -256 && d.z <= 256 &&
            angleDistance(lara->angle.x, 0) <= 1820 &&
            angleDistance(lara->angle.z, 0) == 0) {
            if (tr2->keyReplayChoice) {
                unsigned type = tr2->keyReplayChoice - 1;
                tr2->keyReplayChoice = 0;
                game_tr2_ring_commit(type, tr2->keyPauseTicks);
                tr2->keyPauseTicks = 0;
                return game_tr2_finish_detonator(item, lara);
            }
            if (tr2->selectedKey)
                return game_tr2_finish_detonator(item, lara);
            if (game_tr2_ring_has_keys())
                return TR2_INTERACT_CHOOSE_KEY;
        }
    } else if ((tr2->keyReplayChoice || tr2->selectedKey) &&
               (item->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INACTIVE) {
        vec3i d = interactionLocal(vec3i_sub(lara->pos, item->pos), 0, lara->angle.y);
        if (d.x >= -256 && d.x <= 256 && d.y >= -100 && d.y <= 100 &&
            d.z >= -256 && d.z <= 256) {
            tr2->selectedKey = 0;
            tr2->keyReplayChoice = 0;
            tr2->keyPauseTicks = 0;
        }
    }
    return game_tr2_solid_contact(item, lara, true, false) == TR2_SOLID_BUMP ?
           TR2_INTERACT_BUMP : TR2_INTERACT_NONE;
}

void game_tr2_detonator_update(ItemObj *item)
{
    if ((item->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INACTIVE) {
        ItemObj_deactivateBase(item);
        return;
    }
    ItemObj_animProcess(item, true);
    unsigned elapsed = item->frameIndex -
                       tr64_u16(gs->level.anims[item->animIndex].frame_start);
    if (elapsed == 80u) {
        gs->playersExtra[0].camera.bounce = -150;
        soundPlay(105, NULL);
    }
}

uint32 __attribute__((optimize("Os"))) game_tr2_check_deferred(ItemObj *item, Lara *lara)
{
    const bool held = freeAction(lara);
    const bool grounded = held && !(lara->flags & ITEM_FLAG_GRAVITY) &&
                          lara->waterState == WATER_STATE_ABOVE;
    if (item->type == TR2_ITEM_ZIPLINE) {
        if (grounded && lara->state == Lara_STATE_STOP &&
            actionNear(item,lara,lara->pos,item->angle.y,256,100,256,512,0,4550,0)) {
            if (!TR64_RULE(WALL_HAZARDS))
                game_tr2_refuse_interaction(item,"mount");
            game_tr2_mount_zipline(item,lara);
        }
    } else if (item->type >= 201 && item->type <= 204) {
        const bool preselected=gs->tr2->selectedKey &&
            !(lara->flags&ITEM_FLAG_GRAVITY) && lara->waterState==WATER_STATE_ABOVE &&
            lara->extraL->weaponState==WEAPON_STATE_FREE;
        if ((grounded || preselected) && lara->state == Lara_STATE_STOP &&
            actionNear(item,lara,lara->pos,item->angle.y,200,0,312,512,1820,5460,1820))
            return useKeyhole(item,lara);
    } else if (tr2DeferredSphereType(item->type)) {
        if (item->animIndex != UINT16_MAX &&
            ItemObj_collideBounds(item,lara,&gs->cinfo) &&
            ItemObj_collideSpheres(item,lara))
            game_tr2_refuse_interaction(item,"body contact");
    } else if (held && ((item->type >= 135 && item->type <= 140) ||
                        (item->type >= 143 && item->type <= 151) ||
                        item->type == 152 ||
                        (item->type >= 195 && item->type <= 196) ||
                        item->type == 205 || item->type == 206)) {
        bool wet = lara->waterState == WATER_STATE_UNDER;
        if (((wet && lara->state == Lara_STATE_UW_TREAD) ||
             (lara->state == Lara_STATE_STOP && !(lara->flags & ITEM_FLAG_GRAVITY) &&
              (lara->waterState == WATER_STATE_ABOVE || lara->waterState == WATER_STATE_WADE))) &&
            pickupReach(item,lara,wet))
            game_tr2_refuse_interaction(item,"acquisition");
    }
    return TR2_INTERACT_NONE;
}
