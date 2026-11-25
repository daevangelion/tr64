/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/object.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "objects.h"
#include "game/common/object.h"

/* Great Wall objects, Discs: I's dart emitter is the peer. The cited
 * differences are the latched firing loop, the exact-east spawn exception,
 * damage on the disc's next update, and removal on a missing floor. */

static bool firingFrame(const ItemObj *item)
{
    const Anim *anim = gs->level.anims + item->animIndex;
    return item->state == 1 && item->frameIndex == tr64_u16(anim->frame_start);
}

/* Heading table: 412 back, except exact east (16384) which has none.
 * Each new disc spends two presentation draws and no gameplay draw.
 * tr2-great-wall-objects.md:133: no free item slot creates no disc and
 * does not stop. The launcher tries again 30 ticks later. */
void game_tr2_disc_launch(ItemObj *launcher)
{
    vec3i pos;
    ItemObj *disc;
    if (!firingFrame(launcher))
        return;
    if (!gs->firstFree)
        return;
    (void)rand_draw();
    (void)rand_draw();
    pos = launcher->pos;
    pos.y = i32_sub(pos.y, 512);
    if (launcher->angle.y != ANGLE_90) {
        vec3i back = game_tr2_heading_step(launcher->angle.y, -412);
        pos.x = i32_add(pos.x, back.x);
        pos.z = i32_add(pos.z, back.z);
    }
    disc = ItemObj_add(TR2_ITEM_DISC, launcher->room, &pos, launcher->angle.y);
    if (!disc)
        return;
    disc->flags = (disc->flags & ~ITEM_FLAG_STATUS) | ITEM_FLAG_STATUS_ACTIVE;
    ItemObj_activateBase(disc);
    /* tr2-great-wall-objects.md:123,129. The spawn sample stores the
     * sector floor byte at this X/Z, shifted left by 8. The slope sample
     * replaces it on the first control, after the step. */
    disc->roomFloor = Room_getSector(disc->room, disc->pos.x, disc->pos.z)->floor * 256;
}

/* A fresh touch costs 50 on the disc's next update and does not consume it.
 * A disc touch allocates no blood. */
void game_tr2_disc_harm(ItemObj *item, Lara *lara)
{
    if (!item->hitMask)
        return;
    item->hitMask = 0;
    Lara_hit(lara, 50, &item->pos, 0);
}

/* Clip 306 moves 100 along its facing. The controller adds 4096 of roll.
 * tr2-great-wall-objects.md:129: after the step, the slope sample at or
 * below the disc ends it. 16384 does not. */
uint32 game_tr2_disc_update(ItemObj *item)
{
    if (item->state != 0)
        sys_error("Tomb Raider II: flying disc: unknown state %u", item->state);
    ItemObj_animProcess(item, true);
    item->angle.x = wrap_i16((int32)item->angle.x + 4096);
    ItemObj_updateRoom(item, 0);
    return item->roomFloor != 16384 && item->pos.y >= item->roomFloor;
}

/* tr2-great-wall-objects.md:129. Sound 258 always spends two presentation
 * draws. The spark's one gameplay draw is skipped only when no slot is
 * free. Sprite 238 is the only spark; there is no second one. */
void game_tr2_disc_removed(ItemObj *item)
{
    (void)rand_draw();
    (void)rand_draw();
    soundPlay(258, &item->pos);
    game_tr2_disc_spark(item->room, item->pos);
    ItemObj_remove(item);
}
/* Idle 307 stays goal 0 until this update. The firing clip then loops;
 * these banks do not request idle on sector exit. */
void game_tr2_launcher_update(ItemObj *item)
{
    if (item->state > 1)
        sys_error("Tomb Raider II: disc launcher: unknown state %u", item->state);
    if (item->state == 0)
        item->goalState = 1;
    ItemObj_animProcess(item, true);
}
