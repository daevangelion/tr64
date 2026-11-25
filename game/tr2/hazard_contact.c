/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/object.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "objects.h"
#include "../object.h"

/* Great Wall Objects, Discs, blades and spikes: I's common bounds and
 * damage helpers, but II confirms sphere touch before the contact draw. */
void game_tr2_spike_contact(ItemObj *item, Lara *lara)
{
    if (!ItemObj_collideBounds(item,lara,&gs->cinfo) || !ItemObj_collideSpheres(item,lara))
        return;
    unsigned blood=rand_logic()>=24576;
    const bool airborne=(lara->flags&ITEM_FLAG_GRAVITY)!=0;
    if (!airborne && lara->hSpeed<30)
        return;
    const vec3i origin=lara->pos;
    const bool fatalDescent=airborne && lara->vSpeed>6;
    const int priorHP=lara->health;
    if (fatalDescent) {
        lara->health=-1;
        blood=20;
    } else {
        Lara_hit(lara,15,&item->pos,0);
    }
    /* Fatal burst origin: offsets use the contact origin, including Y
     * before alignment. The burst is attempted, then the pose follows. */
    while (blood--) {
        vec3i point=origin;
        point.x=i32_add(point.x,(rand_logic()-16384)/256);
        point.z=i32_add(point.z,(rand_logic()-16384)/256);
        point.y=i32_sub(point.y,rand_logic()>>6);
        int16 yaw=rand_logic();
        game_tr2_shot_effect(TR2_FX_BLOOD,item->room,point,20,yaw);
    }
    /* Q-HAZ-04: a lethal contact installs state 8 before Lara's floor
     * response, so that response writes air on this same tick. */
    if (fatalDescent || (priorHP>0 && lara->health<=0)) {
        lara->pos.y=item->pos.y;
        lara->flags&=~ITEM_FLAG_GRAVITY;
        ItemObj_animSet(lara,Lara_ANIM_DEATH_SPIKES,true,0);
    }
}

void game_tr2_blade_contact(ItemObj *item, Lara *lara)
{
    if (ItemObj_collideBounds(item,lara,&gs->cinfo))
        item->hitMask=ItemObj_collideSpheres(item,lara);
}

/* Great Wall Objects, Fixed guillotine: unlike I's swing blade, resolve
 * the preceding touch before clearing it and advancing this item. */
void game_tr2_blade_update(ItemObj *item, Lara *lara)
{
    if (item->state==2) {
        item->goalState=1;
        if (item->hitMask&2u) {
            Lara_hit(lara,100,&item->pos,0);
            lara->flags|=ITEM_FLAG_INJURED;
            for (unsigned i=0;i<2;++i) {
                vec3i point=lara->pos;
                point.z=i32_add(point.z,256-(rand_draw()>>6));
                point.y=i32_sub(item->pos.y,rand_draw()>>6);
                point.x=i32_add(point.x,256-(rand_draw()>>6));
                game_tr2_shot_effect(TR2_FX_BLOOD,lara->room,point,lara->hSpeed,lara->angle.y);
            }
        }
    } else if (Object_isActive(item)) {
        item->goalState=2;
    }
    item->hitMask=0;
    ItemObj_animProcess(item,true);
}

/* tr2-bartoli-and-weapons.md §3: Statue with knife blade (type 82).
 * Solid when inactive; trap sphere touch without push-out when active. */
uint32 game_tr2_statue_blade_contact(ItemObj *item, Lara *lara)
{
    unsigned status = item->flags & ITEM_FLAG_STATUS;
    if (status == ITEM_FLAG_STATUS_ACTIVE) {
        if (ItemObj_collideBounds(item, lara, &gs->cinfo))
            item->hitMask = ItemObj_collideSpheres(item, lara);
        return TR2_INTERACT_NONE;
    }
    if (status != ITEM_FLAG_STATUS_INVISIBLE)
        return game_tr2_solid_contact(item, lara, true, false) == TR2_SOLID_BUMP ?
               TR2_INTERACT_BUMP : TR2_INTERACT_NONE;
    return TR2_INTERACT_NONE;
}

void game_tr2_statue_blade_update(ItemObj *item, Lara *lara)
{
    if (item->state == 2 && (item->hitMask & 0x80u)) {
        Lara_hit(lara, 20, &item->pos, 0);
        lara->flags |= ITEM_FLAG_INJURED;
        int32 r1 = rand_logic();
        int32 r2 = rand_logic();
        int32 r3 = rand_logic();
        int32 r4 = rand_logic();
        vec3i point = vec3i_create(
            i32_add(lara->pos.x, (r1 - 16384) / 256),
            i32_sub(lara->pos.y, r3 / 44),
            i32_add(lara->pos.z, (r2 - 16384) / 256));
        int16 yaw = wrap_i16((int32)lara->angle.y + (r4 - 16384) / 8);
        game_tr2_shot_effect(TR2_FX_BLOOD, lara->room, point, lara->hSpeed, yaw);
    }
    if (Object_isActive(item) && item->state == 1)
        item->goalState = 2;
    else
        item->goalState = 1;
    item->hitMask = 0;
    ItemObj_animProcess(item, true);
}

/* Discs, rolling blades and spike walls: register the touch now and
 * place Lara on the nearest extended face. Harm waits for the item's
 * next update. */
void game_tr2_moving_contact(ItemObj *item, Lara *lara)
{
    uint32 mask;
    if (!ItemObj_collideBounds(item, lara, &gs->cinfo))
        return;
    mask = ItemObj_collideSpheres(item, lara);
    if (!mask)
        return;
    item->hitMask = mask;
    game_tr2_nearest_face(item, lara);
}
