/* SPDX-License-Identifier: MIT */
/* Plant a surface probe the level does not contain. TR64_CLIMB_CASE is
 * comma-separated key=value: h, type, yaw, weapon (free|flare|ready),
 * slant, side, ceiling, floor=wall, action=0. */
#include "game/common/internal.h"
#include <stdlib.h>

void __real_ItemObj_collideRoom(const ItemObj *self, int32 height, int32 offset);

static int take(const char **p, const char *key, long *out)
{
    size_t n = strlen(key);
    if (strncmp(*p, key, n) || (*p)[n] != '=')
        return 0;
    *p += n + 1;
    *out = strtol(*p, (char **)p, 10);
    if (**p == ',')
        ++*p;
    return 1;
}

void __wrap_ItemObj_collideRoom(const ItemObj *self, int32 height, int32 offset)
{
    __real_ItemObj_collideRoom(self, height, offset);
    const char *spec = getenv("TR64_CLIMB_CASE");
    const char *p = spec;
    CollisionInfo *c = &gs->cinfo;
    Lara *lara = (Lara *)self;
    long h = -1, type = CT_FRONT, yaw = 0, slant = SLANT_NONE, side = 0, ceiling = 0, action = 1;
    int wall = 0, ready = 0, flare = 0;
    if (!spec || self->state != Lara_STATE_SURF_SWIM || height != 800 || offset != LARA_HEIGHT_SURF)
        return;
    while (*p) {
        if (take(&p, "h", &h) || take(&p, "type", &type) || take(&p, "yaw", &yaw) ||
            take(&p, "slant", &slant) || take(&p, "side", &side) ||
            take(&p, "ceiling", &ceiling) || take(&p, "action", &action))
            continue;
        if (!strncmp(p, "floor=wall", 10)) {
            wall = 1;
            p += 10;
            if (*p == ',')
                ++p;
            continue;
        }
        if (!strncmp(p, "weapon=ready", 12)) {
            ready = 1;
            p += 12;
        } else if (!strncmp(p, "weapon=flare", 12)) {
            flare = 1;
            p += 12;
        } else if (!strncmp(p, "weapon=free", 11)) {
            p += 11;
        } else
            sys_error("climb control: unknown case %s", spec);
        if (*p == ',')
            ++p;
    }
    if (!action)
        lara->input &= ~IN_ACTION;
    if (ready) {
        lara->extraL->weapon = WEAPON_PISTOLS;
        lara->extraL->weaponState = WEAPON_STATE_READY;
    } else if (flare) {
        lara->extraL->weapon = TR2_WEAPON_FLARE;
        lara->extraL->weaponState = WEAPON_STATE_READY;
    }
    c->type = (uint8)type;
    c->pos = self->pos;
    c->offset = vec3i_create(0, 0, 0);
    c->f.floor = wall ? WALL : (int32)h - LARA_HEIGHT_SURF;
    c->f.ceiling = (int32)ceiling;
    c->f.slantType = (uint8)slant;
    c->m.ceiling = -LARA_STEP_HEIGHT;
    c->m.floor = 0;
    c->l.floor = c->f.floor;
    c->r.floor = c->f.floor + (int32)side;
    lara->angle.y = (int16)yaw;
}
