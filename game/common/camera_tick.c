/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/camera.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Ported from src/fixed/camera.h:410-554 at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Render matrix publication belongs to game_view, not simulation. */
#include "internal.h"
static void updateCombat(Camera *self, ItemObj *item)
{
    const ExtraInfoLara *e = item->extraL;
    Location best;
    ASSERT(item->type == ITEM_LARA);
    self->targetAngle.x = wrap_i16((uint32)item->angle.x + ANGLE(-10));
    self->targetAngle.y = item->angle.y;
    if (e->armR.target || e->armL.target)
    {
        int32 x = e->armR.angleAim.x + e->armL.angleAim.x;
        int32 y = e->armR.angleAim.y + e->armL.angleAim.y;
        if (e->armR.target && e->armL.target)
        {
            x >>= 1;
            y >>= 1;
        }
        self->targetAngle.x = wrap_i16((uint32)self->targetAngle.x + x);
        self->targetAngle.y = wrap_i16((uint32)self->targetAngle.y + y);
    }
    else
    {
        self->targetAngle.x = wrap_i16((uint32)self->targetAngle.x + e->head.angle.x + e->torso.angle.x);
        self->targetAngle.y = wrap_i16((uint32)self->targetAngle.y + e->head.angle.y + e->torso.angle.y);
    }
    self->targetDist = 2048;
    best = Camera_getBestLocation(self, true);
    Camera_move(self, &best, self->speed);
}

#if TR64_HAS_SEQUEL
#include "engine/core/log.h"

/* Trace metadata only. Never use this boundary to drive the simulation. */
void game_tr2_camera_end(unsigned kind,const char *reason)
{
    GameTR2 *tr2=gs->tr2;
    if (tr2->cameraUnavailableFrom)
        return;
    uint32 tick=gs->tick+1;
    tr2->cameraUnavailableFrom=tick;
    tr2->cameraUnavailableReason=reason;
    ++tr2->cameraChecks[kind];
    log_printf("tr2-camera-unavailable: tick=%u reason=%s\n",(unsigned)tick,reason);
}


/* tr2-camera.md, Aim point and modes; Move the eye and store the view. */
static void tr2CameraUpdate(Camera *c)
{
    Location previous=c->target;
    bool fixed=c->mode==CAMERA_MODE_FIXED || c->mode==CAMERA_MODE_OBJECT;
    bool fixedSubject=fixed && c->lookAtItem;
    ItemObj *item=fixedSubject ? c->lookAtItem : c->laraItem;
    const AABBs *box=ItemObj_getBoundingBox(item,true);
    int32 relative=fixedSubject ? (box->minY+box->maxY)/2
                                : box->minY+((box->maxY-box->minY)>>2);
    int32 y=i32_add(item->pos.y,relative);
    if (!fixed && c->lookAtItem && c->mode==CAMERA_MODE_FOLLOW)
        game_tr2_camera_subject(c,y);
    bool look=c->mode==CAMERA_MODE_LOOK;
    bool combat=c->mode==CAMERA_MODE_COMBAT;
    if (look || combat) {
        y=i32_sub(y,256);
        c->speed=look ? 4 : 8;
        c->targetDist=look ? 1536 : 2560;
    }
    bool changed=c->lastFixed!=fixedSubject;
    if (changed)
        c->speed=1;
    int32 dy=i32_sub(y,previous.pos.y);
    c->target=(Location){.room=item->room,
        .pos=vec3i_create(item->pos.x,
                         changed ? y : i32_add(previous.pos.y,(look || combat) ? dy>>2 : dy/4),
                         item->pos.z)};
    game_tr2_camera_move(c,&previous);
    if (fixed && c->timer>0 && --c->timer==0)
        c->timer=-1;

    c->lastFixed=fixedSubject;
    c->lastIndex=c->index;
    if (c->mode!=CAMERA_MODE_OBJECT || c->timer==-1) {
        c->mode=CAMERA_MODE_FOLLOW;
        c->speed=10;
        c->index=-1;
        c->lastItem=c->lookAtItem;
        c->lookAtItem=NULL;
        c->targetAngle.x=c->targetAngle.y=0;
        c->targetDist=1536;
        c->center=false;
        c->fullSlopes=false;
    }
    vec3i dir=vec3i_sub(c->target.pos,c->view.pos);
    c->angle.y=wrap_i16(game_tr2_bearing(dir.z,dir.x));
    c->angle.x=wrap_i16(game_tr2_bearing(game_tr2_horizontal_length(dir.x,dir.z),
                                         (int64_t)c->displayY-c->target.pos.y));
}
#else
#line 33 /* Preserve cart assertion IDs. */
#endif

void Camera_update(Camera *self)
{
    if (self->mode == CAMERA_MODE_CUTSCENE)
    {
        /* camera.h:414-418: the view stays where the cutscene trigger put it;
         * matrixSetView output is published by game_view (view.c). */
        return;
    }
    if (self->mode == CAMERA_MODE_FREE)
    {
        game_stop_set(TR64_GAME_STOP_NOT_PORTED, -1, "game camera mode outside M2");
        return;
    }
#if TR64_HAS_SEQUEL
    if (TR64_RULE(CAMERA_VIEW)) {
        tr2CameraUpdate(self);
        return;
    }
#else
#line 47 /* Preserve cart assertion IDs. */
#endif
    bool isFixed = 0;
    ItemObj *item = self->laraItem;
    if (self->lookAtItem && (self->mode == CAMERA_MODE_FIXED || self->mode == CAMERA_MODE_OBJECT))
    {
        isFixed = 1;
        item = self->lookAtItem;
    }
    ASSERT(item);
    ;
    self->target.room = item->room;
    self->target.pos.x = item->pos.x;
    self->target.pos.z = item->pos.z;
    const AABBs *box = ItemObj_getBoundingBox(item, 1);
    int32 y = item->pos.y;
    if (isFixed)
    {
        (y = i32_add(y, ((box->minY + box->maxY)) >> 1));
    }
    else
    {
        (y = i32_add(y, (box->maxY + (((box->minY - box->maxY) * 3) >> 2))));
    }
    if (!isFixed && self->lookAtItem)
    {
        Camera_lookAt(self, y);
    }
    if (self->mode == CAMERA_MODE_LOOK || self->mode == CAMERA_MODE_COMBAT)
    {
        (y = i32_sub(y, 256));
        if (self->lastFixed)
        {
            self->target.pos.y = 0;
            self->speed = 1;
        }
        else
        {
            (self->target.pos.y =
                 i32_add(self->target.pos.y, (i32_sub(y, self->target.pos.y)) >> 2));
            self->speed = (self->mode == CAMERA_MODE_LOOK) ? 4 : 8;
        }
    }
    else
    {
        if (self->center)
        {
            int32 dx = ((box->minX + box->maxX)) >> 1;
            int32 dz = ((box->minZ + box->maxZ)) >> 1;
            int32 s;
            int32 c;
            sincos(item->angle.y, s, c);
            {
                int32 _x = ((i32_sub(i32_mul(dz, c), i32_mul(dx, s))) >> 14);
                int32 _y = ((i32_add(i32_mul(dx, c), i32_mul(dz, s))) >> 14);
                dz = _x;
                dx = _y;
            };
            (self->target.pos.x = i32_add(self->target.pos.x, dx));
            (self->target.pos.z = i32_add(self->target.pos.z, dz));
        }
        self->lastFixed = (((int32)(self->lastFixed)) ^ ((int32)(isFixed))) != 0;
        if (self->lastFixed)
        {
            self->target.pos.y = y;
            self->speed = 1;
        }
        else
        {
            (self->target.pos.y =
                 i32_add(self->target.pos.y, (i32_sub(y, self->target.pos.y)) >> 2));
        }
    }
    switch (self->mode)
    {
    case CAMERA_MODE_FOLLOW:
        Camera_updateFollow(self, item);
        break;
    case CAMERA_MODE_COMBAT:
        updateCombat(self, item);
        break;
    case CAMERA_MODE_LOOK:
        Camera_updateLook(self, item);
        break;
    default:
        Camera_updateFixed(self);
    }
    self->lastFixed = isFixed;
    self->lastIndex = self->index;
    if (self->mode != CAMERA_MODE_OBJECT || self->timer == -1)
    {
        self->mode = CAMERA_MODE_FOLLOW;
        self->index = -1;
        self->lastItem = self->lookAtItem;
        self->lookAtItem = NULL;
        self->targetAngle.x = 0;
        self->targetAngle.y = 0;
        self->targetDist = 1536;
        self->center = 0;
    }
    if (self->bounce > 0)
    {
        (self->view.pos.y = i32_add(self->view.pos.y, self->bounce));
        (self->target.pos.y = i32_add(self->target.pos.y, self->bounce));
        self->bounce = 0;
    }
    else if (self->bounce < 0)
    {
        int32 shake = i32_mul(i32_sub(rand_logic(), 0x4000), self->bounce) / 0x7FFF;
        (self->view.pos.x = i32_add(self->view.pos.x, shake));
        (self->target.pos.y = i32_add(self->target.pos.y, shake));
        shake = i32_mul(i32_sub(rand_logic(), 0x4000), self->bounce) / 0x7FFF;
        (self->view.pos.y = i32_add(self->view.pos.y, shake));
        (self->target.pos.y = i32_add(self->target.pos.y, shake));
        shake = i32_mul(i32_sub(rand_logic(), 0x4000), self->bounce) / 0x7FFF;
        (self->view.pos.z = i32_add(self->view.pos.z, shake));
        (self->target.pos.z = i32_add(self->target.pos.z, shake));
        self->bounce += 5;
    }
    vec3i dir = vec3i_sub(self->target.pos, self->view.pos);
    anglesFromVector(dir.x, dir.y, dir.z, &(self->angle.x), &(self->angle.y));
    /* matrixSetView output is published by game_view (view.c). */
}
