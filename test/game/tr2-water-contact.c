/* SPDX-License-Identifier: MIT */
/* Link a host with --wrap=ItemObj_collideRoom to control the otherwise
 * unrecorded middle-floor rise and flat frontal splat outcomes. */
#include "game/internal.h"

void __real_ItemObj_collideRoom(const ItemObj *, int32, int32);

void __wrap_ItemObj_collideRoom(const ItemObj *self, int32 height, int32 offset)
{
    __real_ItemObj_collideRoom(self, height, offset);
    if (gs->tick != 500 || self != gs->players[0] || self->state != Lara_STATE_WADE ||
        self->waterState != WATER_STATE_WADE || height != LARA_HEIGHT || offset != 0)
        return;
    const char *contact = getenv("TR64_WATER_TEST_CONTACT");
    if (!contact)
        return;
    CollisionInfo *c = &gs->cinfo;
    if (!strcmp(contact, "step-up")) {
        c->m.floor = -256;
        c->type = CT_NONE;
    } else if (!strcmp(contact, "splat")) {
        c->f.floor = -641;
        c->m.floor = 0;
        c->f.slantType = SLANT_NONE;
        c->type = CT_FRONT;
    } else
        sys_error("water contact control: unknown case %s", contact);
    c->offset = vec3i_create(0, 0, 0);
}
