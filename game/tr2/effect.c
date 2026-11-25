/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/object.h and src/fixed/item.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "game/common/internal.h"
#include "engine/core/log.h"
#line 5 "game/tr2_effects.c" /* Preserve the established assertion ID in effect(). */

/* Great Wall creatures, Q-T2K-06: zero terminates both lists. Stable
 * indices let a campfire remember its effect without owning a second pool. */
static TR2Effect *effect(uint8 index)
{
    ASSERT(index && index<=TR2_EFFECT_SLOTS);
    return gs->tr2->effects+index-1;
}

void game_tr2_effects_init(void)
{
    gs->tr2->effectFree=1;
    for (unsigned i=1;i<TR2_EFFECT_SLOTS;++i)
        effect(i)->next=i+1;
}

static uint8 allocate(TR2EffectKind kind,Room *room,vec3i pos)
{
    GameTR2 *tr2=gs->tr2;
    uint8 index=tr2->effectFree;
    if (!index) return 0;
    TR2Effect *fx=effect(index);
    tr2->effectFree=fx->next;
    *fx=(TR2Effect){.kind=kind,.room=room,.pos=pos,.next=tr2->effectActive};
    tr2->effectActive=index;
    return index;
}

static void retire(uint8 *link)
{
    GameTR2 *tr2=gs->tr2;
    uint8 index=*link;
    TR2Effect *fx=effect(index);
    *link=fx->next;
    fx->next=tr2->effectFree;
    tr2->effectFree=index;
}

void game_tr2_campfire_effect(const ItemObj *item,bool triggered)
{
    uint8 *handle=gs->tr2->campfireEffect+(item-gs->items);
    if (triggered) {
        if (!*handle) {
            *handle=allocate(TR2_FX_FLAME,item->room,item->pos);
            if (*handle) {
                TR2Effect *fx=effect(*handle);
                fx->asset.mesh=1; /* Marks a real campfire flame vs inert pool filler. */
                fx->speed=0;      /* Cooldown. */
                fx->descent=0;    /* Stationary (non-negative state). */
                fx->age=0;        /* Sprite frame. */
            }
        }
    } else if (*handle) {
        uint8 *link=&gs->tr2->effectActive;
        while (*link!=*handle) link=&effect(*link)->next;
        retire(link);
        *handle=0;
    }
}

/* tr2-great-wall-objects.md disc spark life: the pistol ricochet is
 * given 4; the disc spark is given 6. The shared update decrements.
 * The pistol roll draw is not one of the disc's stated draws. */
enum { TR2_RICOCHET_LIFE = 4, TR2_DISC_SPARK_LIFE = 6 };

void game_tr2_shot_effect(TR2EffectKind kind,Room *room,vec3i point,int16 speed,int16 yaw)
{
    uint8 index=allocate(kind,room,point);
    if (index) {
        TR2Effect *fx=effect(index);
        fx->speed=speed;
        fx->angle.y=yaw;
        if (kind==TR2_FX_RICOCHET) {
            fx->asset.spriteRoll=rand_draw();
            fx->age=TR2_RICOCHET_LIFE;
        }
    }
}

void game_tr2_disc_spark(Room *room, vec3i point)
{
    int32 draw;
    uint8 index;
    if (!gs->tr2->effectFree)
        return;
    draw = rand_logic();
    index = allocate(TR2_FX_RICOCHET, room, point);
    if (!index)
        return;
    effect(index)->speed = (int16)((draw * -3) / 32768);
    effect(index)->age = TR2_DISC_SPARK_LIFE;
}

/* Q-T2K-12: an item-phase blood effect gets its first of 24 updates
 * later on the same tick, unlike a shot made in Lara's phase. */
void game_tr2_bite_effect(const ItemObj *item,unsigned joint,vec3i offset,int16 extraYaw)
{
    if (!gs->tr2->effectFree) return;
    vec3i point=game_tr2_combat_joint(item,joint,offset,extraYaw);
    game_tr2_shot_effect(TR2_FX_BLOOD,item->room,point,item->hSpeed,item->angle.y);
}

void game_tr2_spider_breakup(ItemObj *item)
{
    vec3i joints[32];
    unsigned count=game_tr2_joint_origins(item,joints), made=0, occupied=0;
    for (uint8 index=gs->tr2->effectActive;index;index=effect(index)->next) ++occupied;
    for (unsigned mesh=0;mesh<count;++mesh) {
        if (!(item->visibleMask&(UINT32_C(1)<<mesh))) continue;
        uint8 index=allocate(TR2_FX_SHARD,item->room,joints[mesh]);
        if (!index) break;
        TR2Effect *fx=effect(index);
        fx->asset.mesh=gs->level.models[item->type].start+mesh;
        fx->angle.y=wrap_i16(rand_logic()*2-32768);
        fx->speed=rand_logic()>>8;
        fx->descent=(-rand_logic())>>8;
        ++made;
    }
    log_printf("tr2-spider-breakup: tick=%u occupied=%u shards=%u\n",(unsigned)(gs->tick+1),occupied,made);
    soundPlay(349,&item->pos);
    ItemObj_remove(item);
}

/* tr2-oracle-trace.md, Wake and shared effect occupancy. */
void game_tr2_wake_effect(Room *room, vec3i pos, int16 yaw, int16 hullSpeed)
{
    uint8 index=allocate(TR2_FX_WAKE,room,pos);
    if (!index) return;
    TR2Effect *fx=effect(index);
    fx->angle.y=yaw;
    fx->speed=(int16)(hullSpeed>>2);
    fx->age=20;
    if (hullSpeed<64) {
        int32 draw=rand_draw();
        int32 mag=hullSpeed<0 ? -hullSpeed : hullSpeed;
        fx->descent=(int16)(((mag-64)*draw)>>15);
    } else {
        fx->descent=0;
    }
}

void game_tr2_splash_effect(Room *room, vec3i pos)
{
    (void)allocate(TR2_FX_SPLASH,room,pos);
}

void game_tr2_explosion_effect(Room *room, vec3i pos)
{
    (void)allocate(TR2_FX_EXPLOSION,room,pos);
}

void game_tr2_gun_flash_effect(Room *room, vec3i pos, int16 yaw)
{
    if (gs->level.models[TR2_MODEL_GUN_FLASH].count <= 0)
        return;
    uint8 index = allocate(TR2_FX_GUN_FLASH, room, pos);
    if (!index)
        return;
    TR2Effect *fx = effect(index);
    fx->angle.y = yaw;
    fx->age = 3;
    fx->asset.mesh = gs->level.models[TR2_MODEL_GUN_FLASH].start;
}

/* tr2-water-and-flares.md, Effect 3 emission and lifetime. */
void game_tr2_bubbles(const Lara *lara)
{
    int32 count=(rand_draw()*3)>>15;
    if (!count) return;
    soundPlay(37,&lara->pos);
    vec3i point=game_tr2_combat_joint(lara,14,vec3i_create(0,0,50),0);
    for (int32 i=0;i<count;++i) {
        uint8 index=allocate(TR2_FX_BUBBLE,lara->room,point);
        if (!index) break;
        TR2Effect *fx=effect(index);
        fx->speed=(int16)(10+((rand_draw()*6)>>15));
        fx->asset.spriteRoll=(uint16)((rand_draw()*3)>>15);
    }
}

void game_tr2_shatter(ItemObj *item, uint32 mask)
{
    if (!gs->level.models[item->type].count)
        return;
    vec3i joints[32];
    unsigned count=game_tr2_joint_origins(item,joints);
    for (unsigned mesh=0;mesh<count;++mesh) {
        uint32 bit=UINT32_C(1)<<mesh;
        if (!(mask&bit) || !(item->visibleMask&bit)) continue;
        uint8 index=allocate(TR2_FX_SHARD,item->room,joints[mesh]);
        if (!index) break;
        TR2Effect *fx=effect(index);
        fx->asset.mesh=gs->level.models[item->type].start+mesh;
        fx->angle.y=wrap_i16(rand_logic()*2-32768);
        fx->speed=rand_logic()>>8;
        fx->descent=(-rand_logic())>>8;
    }
}

static bool updateShard(TR2Effect *fx,const Lara *lara)
{
    fx->angle.x=wrap_i16(fx->angle.x+910);
    fx->angle.z=wrap_i16(fx->angle.z+1820);
    fx->pos=vec3i_add(fx->pos,game_tr2_heading_step(fx->angle.y,fx->speed));
    fx->descent=wrap_i16(fx->descent+6);
    fx->pos.y=i32_add(fx->pos.y,fx->descent);
    Room *room=fx->room;
    const Sector *s=game_tr2_room_sector(&room,fx->pos.x,fx->pos.y,fx->pos.z);
    if (!(tr64_u16(fx->room->info->flags)&ROOM_FLAG_WATER_MASK) &&
         (tr64_u16(room->info->flags)&ROOM_FLAG_WATER_MASK))
        allocate(TR2_FX_SPLASH,fx->room,fx->pos);
    int32 ceiling=Sector_getCeiling(s,fx->pos.x,fx->pos.y,fx->pos.z);
    if (fx->pos.y<ceiling) {
        fx->pos.y=ceiling;
        fx->descent=wrap_i16(-fx->descent);
    }
    /* Q-T2K-11: use the same linked sector after clipping the ceiling.
     * WALL is a signed floor, not infinite fall space for a fragment. */
    if (fx->pos.y>=Sector_getFloor(s,fx->pos.x,fx->pos.y,fx->pos.z))
        return false;
    if (fx->pos.x==lara->pos.x && fx->pos.z==lara->pos.z) {
        int64_t y=(int64_t)fx->pos.y-lara->pos.y;
        if (y>=-3072 && y<=3072) {
            const AABBs *pose=ItemObj_getBoundingBox(lara,true);
            if (y>=pose->minY && y<=pose->maxY+100) return false;
        }
    }
    fx->room=room;
    return true;
}

/* tr2-bartoli-and-weapons.md §5: stationary campfire proximity/ignition
 * and attached flame-on-Lara joint tracking + strict water extinguish. */
static bool updateFlame(TR2Effect *fx, Lara *lara)
{
    if (fx->descent >= 0 && !fx->asset.mesh) {
        soundPlay(150, &fx->pos);
        return true;
    }
    int frames = gs->level.models[TR2_SPRITE_FLAME].count;
    if (frames < 0 && ++fx->age >= (unsigned)(-frames))
        fx->age = 0;
    if (fx->descent < 0) {
        unsigned joint = (unsigned)(-1 - fx->descent);
        vec3i offset = fx->descent == -1 ? vec3i_create(0, 0, -100) : vec3i_create(0, 0, 0);
        if (joint < JOINT_MAX)
            fx->pos = game_tr2_combat_joint(lara, joint, offset, 0);
        if (fx->room != lara->room)
            fx->room = lara->room;
        int32 waterH = game_tr2_water_height(fx->room, fx->pos.x, fx->pos.z);
        if (waterH != WALL && fx->pos.y > waterH) {
            fx->descent = 0;
            gs->gLaraHazardState[0].burnTicks = 0;
            return false;
        }
        soundPlay(150, &fx->pos);
        lara->health = X_MAX(INT16_MIN, lara->health - 7);
        lara->flags |= ITEM_FLAG_INJURED;
        if (lara->health > 0 && lara->extraL)
            lara->extraL->healthTimer = 40;
        return true;
    }
    soundPlay(150, &fx->pos);
    if (fx->speed > 0) {
        --fx->speed;
        return true;
    }
    int64_t dx = (int64_t)fx->pos.x - lara->pos.x;
    int64_t dz = (int64_t)fx->pos.z - lara->pos.z;
    int64_t dy = (int64_t)fx->pos.y - lara->pos.y;
    if (dx >= -600 && dx <= 600 && dz >= -600 && dz <= 600 &&
        dx * dx + dz * dz <= 360000 && dy >= -3072 && dy <= 3072) {
        const AABBs *pose = ItemObj_getBoundingBox(lara, true);
        if (dy >= pose->minY && dy <= pose->maxY + 100) {
            lara->health = X_MAX(INT16_MIN, lara->health - 5);
            lara->flags |= ITEM_FLAG_INJURED;
            if (lara->health > 0 && lara->extraL)
                lara->extraL->healthTimer = 40;
            if (dx * dx + dz * dz < 202500) {
                fx->speed = 100;
                if (!gs->gLaraHazardState[0].burnTicks) {
                    uint8 attached = allocate(TR2_FX_FLAME, lara->room, lara->pos);
                    if (attached) {
                        TR2Effect *af = effect(attached);
                        af->asset.mesh = 1;
                        af->descent = -1;
                        af->speed = 0;
                        af->age = 0;
                        gs->gLaraHazardState[0].burnTicks = 1;
                    }
                }
            }
        }
    }
    return true;
}

void game_tr2_attach_lara_flame(ItemObj *lara)
{
    if (!lara || !lara->room)
        return;
    uint8 attached = allocate(TR2_FX_FLAME, lara->room, lara->pos);
    if (attached) {
        TR2Effect *af = effect(attached);
        af->asset.mesh = 1;
        af->descent = -1;
        af->speed = 0;
        af->age = 0;
        gs->gLaraHazardState[0].burnTicks = 1;
    }
}

void game_tr2_run_effects(const Lara *lara)
{
    /* New effects belong to next tick if made in this pass. If the
     * current head made a splash, find its new predecessor before retiring. */
    uint8 *link=&gs->tr2->effectActive;
    for (uint8 index=*link;index;) {
        TR2Effect *fx=effect(index);
        uint8 next=fx->next;
        bool active;
        switch (fx->kind) {
        case TR2_FX_SHARD: active=updateShard(fx,lara); break;
        case TR2_FX_BLOOD:
            fx->pos=vec3i_add(fx->pos,game_tr2_heading_step(fx->angle.y,fx->speed));
            active=++fx->age<24;
            break;
        case TR2_FX_SPLASH: active=++fx->age<9; break;
        case TR2_FX_RICOCHET: active=fx->age!=0 && --fx->age!=0; break;
        case TR2_FX_FLAME: active=updateFlame(fx,gs->players[0]); break;
        case TR2_FX_WAKE:
            /* tr2-oracle-trace.md, Wake retirement. */
            if (fx->age==0 || --fx->age==0 || fx->descent>0) {
                active=false;
            } else {
                fx->pos=vec3i_add(fx->pos,game_tr2_heading_step(fx->angle.y,fx->speed));
                if (fx->descent!=0) {
                    fx->pos.y=i32_add(fx->pos.y,fx->descent);
                    fx->descent=wrap_i16(fx->descent+6);
                }
                active=true;
            }
            break;
        case TR2_FX_EXPLOSION: {
            /* §10: 2 ticks per sprite frame (28 ticks for 14 frames):
             * odd ticks emit (12, 10); even ticks advance frame and emit (13, 11)
             * until all frames are exhausted (retiring with no light on final tick). */
            int seqLen = gs->level.models[TR2_SPRITE_EXPLOSION].count < 0
                             ? -gs->level.models[TR2_SPRITE_EXPLOSION].count : 14;
            ++fx->age;
            if (fx->age & 1u) {
                game_tr2_add_dyn_light(fx->pos, 12, 10);
                active = true;
            } else if ((int)(fx->age >> 1) >= seqLen) {
                active = false;
            } else {
                game_tr2_add_dyn_light(fx->pos, 13, 11);
                active = true;
            }
            break;
        }
        case TR2_FX_GUN_FLASH:
            active = fx->age != 0 && --fx->age != 0;
            break;
        case TR2_FX_BUBBLE: {
            fx->pos.y=i32_sub(fx->pos.y,fx->speed);
            fx->angle.x=wrap_i16(fx->angle.x+1638);
            fx->angle.z=wrap_i16(fx->angle.z+2366);
            fx->pos.x=i32_add(fx->pos.x,(11*tr2_sin(fx->angle.x))>>14);
            fx->pos.z=i32_add(fx->pos.z,(8*tr2_sin(fx->angle.z+ANGLE_90))>>14);
            Room *room=fx->room;
            const Sector *s=game_tr2_room_sector(&room,fx->pos.x,fx->pos.y,fx->pos.z);
            int32 ceil=Sector_getCeiling(s,fx->pos.x,fx->pos.y,fx->pos.z);
            if (!(tr64_u16(room->info->flags)&ROOM_FLAG_WATER_MASK) ||
                ceil==WALL || fx->pos.y<=ceil) {
                active=false;
            } else {
                fx->room=room;
                active=true;
            }
            break;
        }
        default: sys_error("Tomb Raider II: invalid room-effect type %u",fx->kind);
        }
        if (active) link=&fx->next;
        else {
            while (*link!=index) link=&effect(*link)->next;
            retire(link);
        }
        index=next;
    }
}
