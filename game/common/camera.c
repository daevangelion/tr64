/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/camera.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Ported from src/fixed/camera.h
 * at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"

void Camera_clip(Camera *self, Location *loc)
{
    (void)self;
    loc->pos.y = checkHeight(loc->room, loc->pos.x, loc->pos.y, loc->pos.z);
    if (checkWall(loc->room, i32_sub(loc->pos.x, 255), loc->pos.y, loc->pos.z))
    {
        loc->pos.x = i32_add(loc->pos.x & (~1023), 255);
    }
    if (checkWall(loc->room, i32_add(loc->pos.x, 255), loc->pos.y, loc->pos.z))
    {
        loc->pos.x = i32_sub(loc->pos.x | 1023, 255);
    }
    if (checkWall(loc->room, loc->pos.x, loc->pos.y, i32_sub(loc->pos.z, 255)))
    {
        loc->pos.z = i32_add(loc->pos.z & (~1023), 255);
    }
    if (checkWall(loc->room, loc->pos.x, loc->pos.y, i32_add(loc->pos.z, 255)))
    {
        loc->pos.z = i32_sub(loc->pos.z | 1023, 255);
    }
    loc->room = Room_getRoom(loc->room, loc->pos.x, loc->pos.y, loc->pos.z);
}

Location Camera_getBestLocation(Camera *self, bool clip)
{
    int32 s;
    int32 c;
    sincos(self->targetAngle.x, s, c);
    int32 distH = i32_mul(self->targetDist, c) >> 14;
    int32 distV = i32_mul(self->targetDist, s) >> 14;
    Location best = Camera_getLocationForAngle(self, self->targetAngle.y, distH, distV);
    if (trace(&(self->target), &(best), 1))
        return best;
    if (clip && !vec3i_equal(best.pos, self->target.pos))
        return best;
    int32 dist = fastLength(i32_sub(self->target.pos.x, best.pos.x),
                            i32_sub(self->target.pos.z, best.pos.z));
    if (dist > 768)
        return best;
    int32 minDist = 2147483647;
    for (int32 i = 0; i < 4; i++)
    {
        Location tmpDest = Camera_getLocationForAngle(self, i32_mul(i, ANGLE_90), distH, distV);
        Location tmpView = self->view;
        if (!trace(&(self->target), &(tmpDest), 1) || !trace(&(tmpDest), &(tmpView), 0))
            continue;
        dist = fastLength(i32_sub(self->view.pos.x, tmpDest.pos.x),
                          i32_sub(self->view.pos.z, tmpDest.pos.z));
        if (dist < minDist)
        {
            minDist = dist;
            (best = tmpDest);
        }
    }
    return best;
}

Location Camera_getLocationForAngle(Camera *self, int32 angle, int32 distH, int32 distV)
{
    int32 s;
    int32 c;
    sincos(angle, s, c);
    Location res;
    res.pos.x = i32_sub(self->target.pos.x, i32_mul(distH, s) >> 14);
    res.pos.y = i32_add(self->target.pos.y, distV);
    res.pos.z = i32_sub(self->target.pos.z, i32_mul(distH, c) >> 14);
    res.room = self->target.room;
    return res;
}

void Camera_init(Camera *self, ItemObj *lara)
{
    ASSERT(lara->extraL);
    ;
    (self->target.pos = lara->pos);
    (self->target.pos.y = i32_sub(self->target.pos.y, 1024));
    self->target.room = lara->room;
    (self->view = self->target);
    (self->view.pos.z = i32_sub(self->view.pos.z, 100));
    self->targetDist = 1536;
    (self->targetAngle = vec3s_create(0, 0, 0));
    (self->angle = vec3s_create(0, 0, 0));
    self->laraItem = lara;
    self->lastItem = NULL;
    self->lookAtItem = NULL;
    self->speed = 1;
    self->timer = 0;
    self->index = -1;
    self->lastIndex = -1;
    self->bounce = 0;
    self->mode = CAMERA_MODE_FOLLOW;
    self->lastFixed = 0;
    self->center = 0;
}
#if !TR64_HAS_SEQUEL
#line 100 /* Preserve cart assertion IDs. */
#endif

void Camera_lookAt(Camera *self, int32 offset)
{
    int32 dx = i32_sub(self->lookAtItem->pos.x, self->laraItem->pos.x);
    int32 dz = i32_sub(self->lookAtItem->pos.z, self->laraItem->pos.z);
    int16 ay = ((int16)(i32_sub(phd_atan(dz, dx), self->laraItem->angle.y))) >> 1;
    if (abs(ay) >= (ANGLE(44)))
    {
        self->lookAtItem = NULL;
        return;
    }
    const AABBs *box = ItemObj_getBoundingBox(self->lookAtItem, 1);
    (offset = i32_sub(offset, i32_add(self->lookAtItem->pos.y, ((box->minY + box->maxY)) >> 1)));
    int16 ax =
        ((int16)(phd_atan(phd_sqrt(i32_add(i32_mul(dx, dx), i32_mul(dz, dz))), offset))) >> 1;
    if (ax < (ANGLE(-42)) || ax > (ANGLE(22)))
    {
        self->lookAtItem = NULL;
        return;
    }
    self->laraItem->extraL->head.angle.x =
        angleLerp(self->laraItem->extraL->head.angle.x, ax, (ANGLE(2)));
    self->laraItem->extraL->head.angle.y =
        angleLerp(self->laraItem->extraL->head.angle.y, ay, (ANGLE(2)));
    (self->laraItem->extraL->torso.angle = self->laraItem->extraL->head.angle);
    self->lookAtItem->flags |= ITEM_FLAG_ANIMATED;
    self->mode = CAMERA_MODE_LOOK;
}

void Camera_move(Camera *self, Location *to, int32 speed)
{
    Camera_clip(self, to);
    vec3i d = vec3i_sub(to->pos, self->view.pos);
    if (speed > 1)
    {
        if (speed == 8)
        {
            d.x >>= 3;
            d.y >>= 3;
            d.z >>= 3;
        }
        else
        {
            d.x /= speed;
            d.y /= speed;
            d.z /= speed;
        }
    }
    (self->view.pos = vec3i_add(self->view.pos, d));
    self->view.room = Room_getRoom(to->room, self->view.pos.x, self->view.pos.y, self->view.pos.z);
    const Sector *sector = Room_getSector(self->view.room, self->view.pos.x, self->view.pos.z);
    int32 floor =
        i32_sub(Sector_getFloor(sector, self->view.pos.x, self->view.pos.y, self->view.pos.z), 256);
    if (self->view.pos.y >= floor && to->pos.y >= floor)
    {
        trace(&(self->target), &(self->view), 1);
        self->view.room =
            Room_getRoom(self->view.room, self->view.pos.x, self->view.pos.y, self->view.pos.z);
    }
}

void Camera_toCombat(Camera *self)
{
    if (self->mode == CAMERA_MODE_FREE)
        return;
    if (self->mode == CAMERA_MODE_CUTSCENE)
        return;
    if (self->mode == CAMERA_MODE_LOOK)
        return;
    self->mode = CAMERA_MODE_COMBAT;
}

void Camera_updateFixed(Camera *self)
{
    const FixedCamera *cam = gs->level.cameras + self->index;
    Location best;
    best.pos = vec3i_create(tr64_i32(cam->x), tr64_i32(cam->y), tr64_i32(cam->z));
    best.room = gs->rooms + tr64_i16(cam->room_or_speed);
    self->lastFixed = 1;
    Camera_move(self, &(best), 1);
    if (self->timer != 0)
    {
        self->timer--;
        if (self->timer == 0)
        {
            self->timer = -1;
        }
    }
}

void Camera_updateFollow(Camera *self, ItemObj *item)
{
    if (self->targetAngle.x == 0)
    {
        self->targetAngle.x = (ANGLE(-10));
    }
    self->targetAngle.x = (((self->targetAngle.x + item->angle.x)) < (-(ANGLE(85)))
                               ? (-(ANGLE(85)))
                               : (((self->targetAngle.x + item->angle.x)) > ((ANGLE(85)))
                                      ? ((ANGLE(85)))
                                      : ((self->targetAngle.x + item->angle.x))));
    self->targetAngle.y += item->angle.y;
    Location best = Camera_getBestLocation(self, 0);
    Camera_move(self, &(best), self->lastFixed ? self->speed : 8);
}

void Camera_updateLook(Camera *self, ItemObj *item)
{
    ASSERT(item->type == ITEM_LARA);
    ;
    self->targetAngle.x =
        ((item->extraL->head.angle.x + item->extraL->torso.angle.x) + item->angle.x);
    self->targetAngle.y =
        ((item->extraL->head.angle.y + item->extraL->torso.angle.y) + item->angle.y);
    self->targetDist = self->lookAtItem ? 1536 : 768;
    Location best = Camera_getBestLocation(self, 1);
    Camera_move(self, &(best), self->speed);
}

int32 checkHeight(Room *room, int32 x, int32 y, int32 z)
{
    Room *nextRoom = Room_getRoom(room, x, y, z);
    const Sector *sector = Room_getSector(nextRoom, x, z);
    int32 floor = Sector_getFloor(sector, x, y, z);
    int32 ceiling = Sector_getCeiling(sector, x, y, z);
    if (floor != (WALL) && ceiling != (WALL) && ceiling < floor)
    {
        if (i32_sub(y, 255) < ceiling && i32_add(y, 255) > floor)
            return (i32_add(floor, ceiling)) >> 1;
        if (i32_add(y, 255) > floor)
            return i32_sub(floor, 255);
        if (i32_sub(y, 255) < ceiling)
            return i32_add(ceiling, 255);
    }
    return y;
}

bool checkWall(Room *room, int32 x, int32 y, int32 z)
{
    Room *nextRoom = Room_getRoom(room, x, y, z);
    const Sector *sector = Room_getSector(nextRoom, x, z);
    int32 floor = Sector_getFloor(sector, x, y, z);
    int32 ceiling = Sector_getCeiling(sector, x, y, z);
    return (floor == (WALL) || ceiling == (WALL) || ceiling >= floor || y > floor || y < ceiling);
}

#if TR64_HAS_SEQUEL
/* tr2-lara-slide-height-and-gaze.md, Looking toward a sector's subject. */
void game_tr2_camera_subject(Camera *c,int32 laraAim)
{
    Lara *lara = c->laraItem;
    ItemObj *subject = c->lookAtItem;
    int32 dx = i32_sub(subject->pos.x,lara->pos.x);
    int32 dz = i32_sub(subject->pos.z,lara->pos.z);
    int16 yaw = wrap_i16((uint32)game_tr2_bearing(dz,dx) - (uint16)lara->angle.y) >> 1;
    if (abs(yaw) >= 9100) {
        c->lookAtItem = NULL;
        return;
    }
    const AABBs *box = ItemObj_getBoundingBox(subject,true);
    int32 midpoint = i32_add(subject->pos.y,(box->minY + box->maxY) / 2);
    int32 pitch = wrap_i16(game_tr2_bearing(game_tr2_horizontal_length(dx,dz),
                                           (int64_t)laraAim-midpoint)) >> 1;
    if (abs(pitch) >= 15470) {
        c->lookAtItem = NULL;
        return;
    }
    ExtraInfoLara *e = lara->extraL;
    e->head.angle.x = angleLerp(e->head.angle.x,pitch,728);
    e->head.angle.y = angleLerp(e->head.angle.y,yaw,728);
    e->torso.angle.x = e->head.angle.x;
    e->torso.angle.y = e->head.angle.y;
    subject->flags |= ITEM_FLAG_ANIMATED;
    c->mode = CAMERA_MODE_LOOK;
}

/* tr2-camera.md, Two different ways to keep a point clear. Tile sweeps
 * shorten a ray; the separate point operation uses navigation box margins. */
enum { CAM_X, CAM_Z };
enum { CAM_CLEAR, CAM_NEAR, CAM_FAR };
typedef struct { int32 floor,ceiling; uint16 box; } CameraRange;
typedef struct { int32 edge[2][2]; } CameraBox;

static int32 cameraAxis(vec3i p,int axis) { return axis==CAM_X ? p.x : p.z; }
static int32 *cameraComponent(vec3i *p,int axis) { return axis==CAM_X ? &p->x : &p->z; }
static int64_t cameraSquare(int32 n) { return (int64_t)n*n; }

static CameraRange cameraRange(Location *p,bool full)
{
    const Sector *s=game_tr2_room_sector(&p->room,p->pos.x,p->pos.y,p->pos.z);
    CameraRange r={.box=s->boxIndex};
    game_tr2_camera_heights(s,&p->pos,full,&r.floor,&r.ceiling);
    return r;
}

static bool cameraOutside(CameraRange r,int32 y)
{
    return r.floor==WALL || r.ceiling==WALL || y<r.ceiling || y>r.floor;
}

static int cameraSweep(const Location *from,Location *to,int axis,bool full)
{
    int32 start=cameraAxis(from->pos,axis), finish=cameraAxis(to->pos,axis);
    int32 distance=i32_sub(finish,start);
    if (!distance)
        return CAM_CLEAR;
    int other=axis^1,step=distance<0 ? -1 : 1;
    int64_t across=(int64_t)i32_sub(cameraAxis(to->pos,other),cameraAxis(from->pos,other))*1024/distance;
    int64_t vertical=(int64_t)i32_sub(to->pos.y,from->pos.y)*1024/distance;
    int32 boundary=step<0 ? start&~1023 : start|1023;
    int32 elapsed=i32_sub(boundary,start);
    Location near=*from;
    int32 *along=cameraComponent(&near.pos,axis),*side=cameraComponent(&near.pos,other);
    *along=boundary;
    *side=i32_add(*side,(across*elapsed)>>10);
    near.pos.y=i32_add(near.pos.y,(vertical*elapsed)>>10);
    while (step<0 ? *along>finish : *along<finish) {
        if (cameraOutside(cameraRange(&near,full),near.pos.y)) {
            *to=near;
            return CAM_NEAR;
        }
        Location far=near;
        *cameraComponent(&far.pos,axis)=i32_add(*along,step);
        if (cameraOutside(cameraRange(&far,full),far.pos.y)) {
            *to=near;
            return CAM_FAR;
        }
        near.room=far.room;
        *along=i32_add(*along,1024*step);
        *side=i32_add(*side,across*step);
        near.pos.y=i32_add(near.pos.y,vertical*step);
    }
    to->room=near.room;
    return CAM_CLEAR;
}

static int32 cameraSegment(int32 a,int32 b,int32 elapsed,int32 span)
{
    return i32_add(a,(int64_t)i32_sub(b,a)*elapsed/span);
}

bool game_tr2_sight(const Location *from,Location *to,bool full)
{
    int32 dx=i32_sub(to->pos.x,from->pos.x),dz=i32_sub(to->pos.z,from->pos.z);
    int first=cameraSquare(dx)<cameraSquare(dz) ? CAM_X : CAM_Z;
    bool clear=cameraSweep(from,to,first,full)==CAM_CLEAR;
    int last=cameraSweep(from,to,first^1,full);
    if (last==CAM_FAR)
        return false;
    CameraRange r=cameraRange(to,full);
    int32 y=to->pos.y,base=from->pos.y,plane;
    if (base<r.floor && r.floor<y)
        plane=r.floor;
    else if (base>r.ceiling && r.ceiling>y)
        plane=r.ceiling;
    else
        return clear && last==CAM_CLEAR;
    int32 span=i32_sub(y,base),elapsed=i32_sub(plane,base);
    to->pos.x=cameraSegment(from->pos.x,to->pos.x,elapsed,span);
    to->pos.z=cameraSegment(from->pos.z,to->pos.z,elapsed,span);
    to->pos.y=plane;
    return false;
}

static CameraBox cameraBox(uint16 index)
{
    const tr64_core_boxes_t *b=gs->level.boxes+index;
    /* Native box maxima are exclusive; the camera uses last coordinates. */
    return (CameraBox){{{tr64_i32(b->min_x),tr64_i32(b->max_x)-1},
                        {tr64_i32(b->min_z),tr64_i32(b->max_z)-1}}};
}

static int32 cameraHeight(CameraRange r,int32 y,int margin)
{
    if (r.floor==WALL || r.ceiling==WALL)
        return y;
    int32 lo=r.ceiling+margin,hi=r.floor-margin;
    if (lo>hi)
        return (lo+hi)>>1;
    return y<lo ? lo : y>hi ? hi : y;
}

static void cameraClearPoint(Location *p,int margin,bool vertical,bool full)
{
    CameraRange r=cameraRange(p,full);
    Location origin=*p;
    if (r.box==UINT16_MAX) {
        ++gs->tr2->cameraChecks[TR2_CAM_NO_BOX_POINT];
    } else {
        CameraBox box=cameraBox(r.box);
        for (int axis=CAM_Z;axis>=CAM_X;--axis) {
            int32 at=cameraAxis(origin.pos,axis);
            for (int side=0;side<2;++side) {
                int32 sign=side ? 1 : -1;
                int32 edge=i32_sub(box.edge[axis][side],sign*margin);
                if (side ? at<=edge : at>=edge)
                    continue;
                Location probe=origin;
                *cameraComponent(&probe.pos,axis)=i32_add(at,sign*margin);
                if (cameraOutside(cameraRange(&probe,full),probe.pos.y)) {
                    *cameraComponent(&p->pos,axis)=edge;
                    break;
                }
            }
        }
    }
    if (vertical)
        p->pos.y=cameraHeight(r,origin.pos.y,margin);
}

/* tr2-camera.md, State requests before dry motion. A state entered during
 * animation or collision does not make its request until the next update. */
void game_tr2_camera_request(Lara *lara)
{
    Camera *c=&lara->extraL->camera;
    switch (lara->state) {
    case Lara_STATE_HANG: case Lara_STATE_HANG_LEFT: case Lara_STATE_HANG_RIGHT:
        c->targetAngle.y=0; c->targetAngle.x=-10920; break;
    case Lara_STATE_REACH:
        c->targetAngle.y=15470; break;
    case Lara_STATE_SLIDE:
        c->targetAngle.x=-8190; c->fullSlopes=true; break;
    case Lara_STATE_JUMP_BACK:
        c->targetAngle.y=24570; break;
    case Lara_STATE_BLOCK_PUSH: case Lara_STATE_BLOCK_PULL:
        c->targetAngle.y=6370; c->targetAngle.x=-4550; c->center=true; break;
    case Lara_STATE_BLOCK_READY:
        c->targetAngle.y=13650; break;
    case Lara_STATE_PICKUP: case Lara_STATE_PICKUP_FLARE:
        c->targetAngle.y=lara->state==Lara_STATE_PICKUP ? -23660 : 23660;
        c->targetAngle.x=-2730; c->targetDist=1024; break;
    case Lara_STATE_SWITCH_DOWN: case Lara_STATE_SWITCH_UP:
        c->targetAngle.y=14560; c->targetAngle.x=-4550; c->targetDist=1024; c->speed=6; break;
    case Lara_STATE_USE_KEY: case Lara_STATE_USE_PUZZLE:
        c->targetAngle.y=-14560; c->targetAngle.x=-4550; c->targetDist=1024; break;
    case Lara_STATE_SPECIAL:
        c->targetAngle.y=30940; c->targetAngle.x=-4550; c->center=true; break;
    case Lara_STATE_WATER_OUT:
        c->center=true; break;
    case Lara_STATE_CLIMB_START:
        c->targetAngle.x=-3640; break;
    case Lara_STATE_CLIMB_UP:
        c->targetAngle.x=5460; break;
    case Lara_STATE_CLIMB_LEFT: case Lara_STATE_CLIMB_RIGHT:
        c->targetAngle.y=lara->state==Lara_STATE_CLIMB_LEFT ? -5460 : 5460;
        c->targetAngle.x=-2730; break;
    case Lara_STATE_CLIMB_END:
        c->targetAngle.y=-8190; c->center=true; break;
    case Lara_STATE_CLIMB_DOWN:
        c->targetAngle.x=-8190; break;
    case Lara_STATE_WADE:
        if (lara->health>0)
            c->targetAngle.x=TR2_CAMERA_WET_ELEVATION;
        break;
    case Lara_STATE_ZIPLINE:
        c->targetAngle.y=12740; break;
#if TR64_HAS_TR3
    default:
        game_tr3_camera_request_extra(lara); break;
#endif
    }
}

static int32 cameraCos(int32 turn) { return tr2_sin(turn+ANGLE_90); }
static int32 cameraProduct(int32 a,int32 b) { return (int64_t)a*b>>14; }

static Location cameraNominal(Location aim,int32 yaw,int32 pitch,int32 radius,int32 distance)
{
    aim.pos.x=i32_sub(aim.pos.x,cameraProduct(radius,tr2_sin(yaw)));
    aim.pos.z=i32_sub(aim.pos.z,cameraProduct(radius,cameraCos(yaw)));
    aim.pos.y=i32_add(aim.pos.y,cameraProduct(distance,tr2_sin(pitch)));
    return aim;
}

static bool cameraBoxContains(CameraBox box,vec3i p)
{
    return p.x>=box.edge[CAM_X][0] && p.x<=box.edge[CAM_X][1] &&
           p.z>=box.edge[CAM_Z][0] && p.z<=box.edge[CAM_Z][1];
}

/* tr2-camera.md, Shared blocked-eye procedure for Follow and Look. */
static Location cameraFollowCandidate(Location to,vec3i from,CameraBox box,int axis,int edge,int side,
                                      int64_t radiusSquared)
{
    int other=axis^1;
    int32 start=cameraAxis(from,axis),cross=cameraAxis(from,other);
    int32 near=box.edge[axis][edge],opposite=box.edge[axis][edge^1];
    int32 chosen=box.edge[other][side],alternate=box.edge[other][side^1];
    int64_t n2=cameraSquare(i32_sub(near,start)),s2=cameraSquare(i32_sub(chosen,cross));
    uint64_t a=(uint64_t)n2+s2;
    int32 *x=cameraComponent(&to.pos,axis),*z=cameraComponent(&to.pos,other);
    if (a>(uint64_t)radiusSquared) {
        *x=near;
        if (n2<=radiusSquared)
            *z=i32_add(cross,(side?1:-1)*(int32)phd_sqrt(radiusSquared-n2));
        return to;
    }
    /* tr2-camera.md, Follow candidate: the 341 threshold is squared. */
    if (a>341*341) {
        *x=near; *z=chosen;
        return to;
    }
    uint64_t b=(uint64_t)n2+cameraSquare(i32_sub(alternate,cross));
    if (b>(uint64_t)radiusSquared) {
        *x=near;
        if (n2<=radiusSquared)
            *z=i32_add(cross,(side?-1:1)*(int32)phd_sqrt(radiusSquared-n2));
        return to;
    }
    uint64_t c=(uint64_t)s2+cameraSquare(i32_sub(opposite,start));
    if (c>(uint64_t)(2*radiusSquared)) {
        if (s2<=2*radiusSquared) {
            *z=chosen;
            *x=i32_add(start,(edge?-1:1)*(int32)phd_sqrt(2*radiusSquared-s2));
        }
        return to;
    }
    *x=c>=b ? opposite : near;
    *z=c>=b ? chosen : alternate;
    return to;
}

static void cameraClipSegment(Location *to,vec3i from,int axis,int32 edge)
{
    int32 start=cameraAxis(from,axis),end=cameraAxis(to->pos,axis);
    if (end==start)
        return;
    int32 elapsed=i32_sub(edge,start),span=i32_sub(end,start);
    int32 *cross=cameraComponent(&to->pos,axis^1);
    *cross=cameraSegment(cameraAxis(from,axis^1),*cross,elapsed,span);
    to->pos.y=cameraSegment(from.y,to->pos.y,elapsed,span);
    *cameraComponent(&to->pos,axis)=edge;
}

static Location cameraLookCandidate(Location to,vec3i from,CameraBox box,int axis,int edge,int side)
{
    int32 near=box.edge[axis][edge],start=cameraAxis(from,axis);
    if (edge ? start<near : start>=near)
        cameraClipSegment(&to,from,axis,near);
    int other=axis^1;
    int32 chosen=box.edge[other][side],cross=cameraAxis(from,other),end=cameraAxis(to.pos,other);
    if (side ? cross<chosen && end>chosen : cross>chosen && end<chosen)
        cameraClipSegment(&to,from,other,chosen);
    return to;
}

static Location cameraCandidate(Location to,vec3i from,CameraBox box,int axis,int edge,int side,
                                int64_t radiusSquared,bool look)
{
    return look ? cameraLookCandidate(to,from,box,axis,edge,side)
                : cameraFollowCandidate(to,from,box,axis,edge,side,radiusSquared);
}

static Location cameraOrbit(const Camera *c,Location aim,Location to,int32 radius,bool full,bool look)
{
    game_tr2_sight(&aim,&to,full);
    CameraRange body=cameraRange(&aim,full),eye=cameraRange(&to,full);
    /* Implementers' choice: use an authored endpoint box if the target
     * has none; with neither, retain the terrain-shortened endpoint. */
    if (body.box==UINT16_MAX)
        ++gs->tr2->cameraChecks[TR2_CAM_NO_BOX_ORBIT];
    if (body.box==UINT16_MAX && eye.box==UINT16_MAX)
        return to;
    CameraBox box=cameraBox(body.box!=UINT16_MAX ? body.box : eye.box);
    if (eye.box!=UINT16_MAX && !cameraBoxContains(box,to.pos))
        box=cameraBox(eye.box);
    bool blocked[2][2];
    for (int axis=CAM_X;axis<=CAM_Z;++axis) {
        int32 tile=cameraAxis(to.pos,axis)&~1023;
        for (int side=0;side<2;++side) {
            Location p=to;
            int32 probe=i32_add(tile,side ? 1024 : -1);
            *cameraComponent(&p.pos,axis)=probe;
            CameraRange r=cameraRange(&p,full);
            blocked[axis][side]=cameraOutside(r,p.pos.y);
            int32 *bound=&box.edge[axis][side];
            if (blocked[axis][side])
                *bound=probe;
            else if (r.box!=UINT16_MAX) {
                int32 next=cameraBox(r.box).edge[axis][side];
                if (side ? next>*bound : next<*bound)
                    *bound=next;
            }
            *bound=i32_add(*bound,side ? -256 : 256);
        }
    }
    int first=cameraSquare(i32_sub(to.pos.x,aim.pos.x))>=cameraSquare(i32_sub(to.pos.z,aim.pos.z)) ? CAM_X : CAM_Z;
    for (int pass=0;pass<2;++pass) {
        int axis=first^pass,other=axis^1;
        int32 here=cameraAxis(to.pos,axis),cross=cameraAxis(aim.pos,other);
        for (int edge=0;edge<2;++edge) {
            if (!blocked[axis][edge] || (edge ? here<=box.edge[axis][edge] : here>=box.edge[axis][edge]))
                continue;
            /* First-axis preference uses the old eye; the fallback
             * axis uses the endpoint shortened by the initial ray. */
            int side=cameraAxis(pass ? to.pos : c->view.pos,other)>=cross;
            int64_t r2=cameraSquare(radius);
            Location answer=cameraCandidate(to,aim.pos,box,axis,edge,side,r2,look);
            if (!game_tr2_sight(&aim,&answer,full))
                answer=cameraCandidate(to,aim.pos,box,axis,edge,side^1,r2,look);
            answer.room=Room_getRoom(to.room,answer.pos.x,answer.pos.y,answer.pos.z);
            return answer;
        }
    }
    return to;
}

static int32 cameraEase(int32 from,int32 to,int32 divisor)
{
    return i32_add(from,i32_sub(to,from)/divisor);
}

/* tr2-camera.md, Move the eye and store the view. Room membership uses
 * the display-clamped height, but the next step and trace keep raw Y. */
static void cameraMoveEye(Camera *c,Location goal)
{
    Location eye={.room=goal.room,
        .pos=vec3i_create(cameraEase(c->view.pos.x,goal.pos.x,c->speed),
                          cameraEase(c->view.pos.y,goal.pos.y,c->speed),
                          cameraEase(c->view.pos.z,goal.pos.z,c->speed))};
    CameraRange r=cameraRange(&eye,true);
    if (eye.pos.y>=r.floor-256 && goal.pos.y>=r.floor-256) {
        game_tr2_sight(&c->target,&eye,true);
        r=cameraRange(&eye,true);
    }
    /* tr2-camera, Bounce and shake (Q-T2C-05): preserve the band
     * measured before the raw offset and refresh only the eye's room. */
    if (c->bounce) {
        if (c->bounce > 0) {
            eye.pos.y = i32_add(eye.pos.y,c->bounce);
            c->target.pos.y = i32_add(c->target.pos.y,c->bounce);
            c->bounce = 0;
        } else {
            int32 x = ((int32)rand_logic()-16384)*c->bounce/32767;
            int32 y = ((int32)rand_logic()-16384)*c->bounce/32767;
            int32 z = ((int32)rand_logic()-16384)*c->bounce/32767;
            eye.pos = vec3i_add(eye.pos,vec3i_create(x,y,z));
            c->target.pos.y = i32_add(c->target.pos.y,x+y);
            c->target.pos.z = i32_add(c->target.pos.z,z);
            c->bounce += 5;
        }
    }
    c->displayY=cameraHeight(r,eye.pos.y,256);
    eye.room=Room_getRoom(eye.room,eye.pos.x,c->displayY,eye.pos.z);
    c->view=eye;
}

/* Q-T2C-13: selection, not eased arm pose or lock flags, owns sight.
 * Both pistol arms retain the same raw sight for the selected item. */
static vec3s cameraCombatAngles(const Lara *lara)
{
    const ExtraInfoLara *e=lara->extraL;
    const ExtraInfoLara_Arm *selected=e->armR.target ? &e->armR : e->armL.target ? &e->armL : NULL;
    int32 yaw=selected ? selected->angleAim.y : e->head.angle.y+e->torso.angle.y;
    int32 pitch=selected ? selected->angleAim.x : e->head.angle.x+e->torso.angle.x;
    return vec3s_create(wrap_i16(lara->angle.x+pitch),wrap_i16(lara->angle.y+yaw),0);
}

/* Q-T2C-12: clip only a below-to-above Combat ray at the sampled plane. */
static void cameraCombatWater(const Lara *lara,const Location *aim,Location *eye)
{
    int32 distance=gs->tr2->waterDistance;
    if (lara->waterState!=WATER_STATE_UNDER || distance==-WALL)
        return;
    int32 plane=i32_add(lara->pos.y,distance);
    if (aim->pos.y<=plane || eye->pos.y>=plane)
        return;
    int32 elapsed=i32_sub(plane,aim->pos.y),span=i32_sub(eye->pos.y,aim->pos.y);
    eye->pos.x=cameraSegment(aim->pos.x,eye->pos.x,elapsed,span);
    eye->pos.z=cameraSegment(aim->pos.z,eye->pos.z,elapsed,span);
    eye->pos.y=plane;
}

void game_tr2_camera_move(Camera *c,const Location *previousTarget)
{
    ItemObj *lara=c->laraItem;
    Location *aim=&c->target;
    bool look=c->mode==CAMERA_MODE_LOOK;
    bool combat=c->mode==CAMERA_MODE_COMBAT;
    bool fixed=c->mode==CAMERA_MODE_FIXED || c->mode==CAMERA_MODE_OBJECT;
    bool full=c->fullSlopes;
    if (!look && !combat && (!fixed || !c->lookAtItem) && c->center) {
        const AABBs *box=ItemObj_getBoundingBox(lara,true);
        int32 center=(box->minZ+box->maxZ)/2;
        aim->pos.x=i32_add(aim->pos.x,cameraProduct(center,tr2_sin(lara->angle.y)));
        aim->pos.z=i32_add(aim->pos.z,cameraProduct(center,cameraCos(lara->angle.y)));
    }
    Location desired;
    if (fixed) {
        CameraRange r=cameraRange(aim,full);
        full |= aim->pos.y>r.floor;
        const FixedCamera *view=gs->level.cameras+c->index;
        desired=(Location){.room=gs->rooms+tr64_i16(view->room_or_speed),
            .pos=vec3i_create(tr64_i32(view->x),tr64_i32(view->y),tr64_i32(view->z))};
        if (!game_tr2_sight(aim,&desired,full))
            cameraClearPoint(&desired,256,false,full);
    } else {
        int32 yaw=lara->angle.y+c->targetAngle.y,pitch=lara->angle.x+c->targetAngle.x;
        if (look) {
            const ExtraInfoLara *e=lara->extraL;
            yaw=lara->angle.y+e->head.angle.y+e->torso.angle.y;
            pitch=lara->angle.x+e->head.angle.x+e->torso.angle.x;
            int32 along=cameraProduct(-512,tr2_sin(pitch));
            Location displaced=*aim;
            displaced.pos.x=i32_add(aim->pos.x,cameraProduct(along,tr2_sin(lara->angle.y)));
            displaced.pos.z=i32_add(aim->pos.z,cameraProduct(along,cameraCos(lara->angle.y)));
            if (!cameraOutside(cameraRange(&displaced,full),displaced.pos.y))
                *aim=displaced;
            cameraClearPoint(aim,306,true,full);
            aim->room=Room_getRoom(aim->room,aim->pos.x,aim->pos.y,aim->pos.z);
        } else if (combat) {
            vec3s angle=cameraCombatAngles(lara);
            pitch=angle.x;
            yaw=angle.y;
        } else {
            pitch=pitch<-15470 ? -15470 : pitch>15470 ? 15470 : pitch;
            CameraRange r=cameraRange(aim,full);
            full |= aim->pos.y>r.floor;
        }
        int32 radius=cameraProduct(c->targetDist,cameraCos(pitch));
        desired=cameraNominal(*aim,yaw,pitch,radius,c->targetDist);
        if (combat)
            cameraCombatWater(lara,aim,&desired);
        else if (!look)
            c->followRadius=radius;
        desired=cameraOrbit(c,*aim,desired,combat ? c->followRadius : radius,full,look);
        if (look) {
            aim->pos.x=cameraEase(previousTarget->pos.x,aim->pos.x,c->speed);
            aim->pos.z=cameraEase(previousTarget->pos.z,aim->pos.z,c->speed);
        }
    }
    cameraMoveEye(c,desired);
}
#endif
