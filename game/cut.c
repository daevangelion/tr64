/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/camera.h, object.h, level.h and game.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Ported from src/fixed/camera.h:16-80 (initCinematic, updateCinematic),
 * object.h:811-835 (CinematicObject), level.h:396-402 (isCutsceneLevel),
 * game.h:696-699 and 1501-1504 at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * A cutscene level has no Lara: the active items animate, the level's
 * cinematic frame table moves the one camera (playersExtra[0].camera) and the
 * level ends with the table. Input has no effect. */
#include "internal.h"

bool game_is_cutscene_level(int32 level)
{
    return level == LVL_TR1_CUT_1 || level == LVL_TR1_CUT_2 ||
           level == LVL_TR1_CUT_3 || level == LVL_TR1_CUT_4;
}

void Camera_initCinematic(Camera *self)
{
    /* Repaired fork defect (test/oracle/README.md, hunk 15): the source keeps the previous
     * level's timer, -1 after LEVEL3B's fixed camera, so frame -1 (the palette tail) was read. */
    self->timer = 0;
    switch (gs->gLevelID)
    {
    case LVL_TR1_CUT_1:
        self->view.pos.x = 36668;
        self->view.pos.z = 63180;
        self->targetAngle.y = -ANGLE(128);
        break;
    case LVL_TR1_CUT_2:
        self->view.pos.x = 51962;
        self->view.pos.z = 53760;
        self->targetAngle.y = ANGLE_90 - 4;
        break;
    case LVL_TR1_CUT_3:
        self->targetAngle.y = ANGLE_90;
        break;
    case LVL_TR1_CUT_4:
        self->targetAngle.y = ANGLE_90;
        break;
    default:
        sys_error("game cinematic camera outside a cutscene level");
    }
}

void Camera_updateCinematic(Camera *self)
{
    const tr64_core_cinematic_frames_t *frame;
    uint32 count = tr64_level_table(gs->core, TR64_CORE_cinematic_frames)->count;
    int32 px, py, pz, dx, dy, dz, s, c, x, z;
    vec3i eye;
    Room *next;
    /* tr64_level_at rejects an index outside the table by name. */
    frame = tr64_level_cinematic_frames(gs->core, (uint32)self->timer++);
    px = tr64_i16(frame->camera_x);
    py = tr64_i16(frame->camera_y);
    pz = tr64_i16(frame->camera_z);
    dx = i32_sub(tr64_i16(frame->target_x), px);
    dy = i32_sub(tr64_i16(frame->target_y), py);
    dz = i32_sub(tr64_i16(frame->target_z), pz);
    anglesFromVector(dx, dy, dz, &self->angle.x, &self->angle.y);
    sincos(self->targetAngle.y, s, c);
    /* X_ROTXY(pz, px, s, c) */
    z = i32_sub(i32_mul(pz, c), i32_mul(px, s)) >> 14;
    x = i32_add(i32_mul(px, c), i32_mul(pz, s)) >> 14;
    pz = z;
    px = x;
    eye = vec3i_create(i32_add(px, self->view.pos.x), i32_add(py, self->view.pos.y),
                       i32_add(pz, self->view.pos.z));
    matrixSetView(&eye, self->angle.x, self->angle.y + self->targetAngle.y);
    next = Room_getRoom(self->view.room, eye.x, eye.y, eye.z);
    if (next)
        self->view.room = next;
    if ((uint32)self->timer >= count)
    {
        self->timer = (int32)count - 1;
        gs->pendingLevel = gs->gLevelID + 1;
    }
}

void Cut_construct(ItemObj *self)
{
    self->angle.y = 0;
    if (self->type == ITEM_CUT_1)
    {
        Camera *camera = &gs->playersExtra[0].camera;
        camera->view.pos = self->pos;
        camera->view.room = self->room;
    }
    self->flags |= ITEM_FLAG_SHADOW;
    ItemObj_activate(self);
}

void Cut_update(ItemObj *self)
{
    if (self->type != ITEM_CUT_4)
    {
        const Camera *camera = &gs->playersExtra[0].camera;
        self->pos = camera->view.pos;
        self->angle.y = camera->targetAngle.y;
    }
    ItemObj_animProcess(self, true);
}

/* The cutscene half of the source's updateItems/updateSimulationTicks; the
 * active items have already been updated by the caller. */
void game_cut_tick(void)
{
    Camera_updateCinematic(&gs->playersExtra[0].camera);
    ++gs->gSaveGame.time;
    worldUpdateEffects();
    ++gs->textureTicks;
}

int game_cutscene_active(void)
{
    return gs && game_is_cutscene_level(gs->gLevelID);
}

/* The player's skip ends the level through the request the cinematic makes
 * itself when its frame table runs out (camera.h updateCinematic), so the
 * next tick latches the usual LEVEL_END with the cutscene's own next id. */
int game_cutscene_skip(void)
{
    if (!game_cutscene_active() || gs->stop.kind != TR64_GAME_STOP_NONE)
        return 0;
    gs->pendingLevel = gs->gLevelID + 1;
    return 1;
}
