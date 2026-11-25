/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/common.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Ported field access from src/fixed/common.h at
 * 2333adef30ae2a5f3b32c5355e6e343d6aa0531a; canonical record order follows
 * test/oracle/oracle.cpp and test/oracle/README.md schema 2. */
#include "internal.h"
#include "build_id.h"
#if TR64_HAS_TR2 && (defined(TR64_HOST) || !TR64_PLAY || TR64_TEST)
#include "tr2/objects.h"
#endif
#if defined(TR64_HOST) || TR64_TEST
#define COOP_TRACE 1
#else
#define COOP_TRACE 0
#endif
#if !defined(TR64_HOST) && TR64_PLAY && !TR64_TEST && !TR64_CART_TR2
#define TRACE_SCHEMA2_ONLY 1
#else
#define TRACE_SCHEMA2_ONLY 0
#endif
typedef struct
{
    tr64_game_writer_t write;
    void *context;
    uint32 hash;
#if !TRACE_SCHEMA2_ONLY
    bool extended;
    bool world;
#endif
#if COOP_TRACE
    bool coop;
#endif
} Trace;
static void bytes(Trace *t, const char *s, uint32 n)
{
    uint32 i;
    if (t->write)
        t->write(t->context, s, n);
    for (i = 0; i < n; ++i)
        t->hash = (t->hash ^ (uint8)s[i]) * 16777619u;
}
static void text(Trace *t, const char *s)
{
    bytes(t, s, (uint32)strlen(s));
}
static void number(Trace *t, uint32 n)
{
    char b[10];
    uint32 i = 10;
    do
    {
        b[--i] = '0' + n % 10;
        n /= 10;
    } while (n);
    bytes(t, b + i, 10 - i);
}
static void integer(Trace *t, int32 n)
{
    if (n < 0)
    {
        text(t, "-");
        number(t, 0u - (uint32)n);
    }
    else
        number(t, (uint32)n);
}
static void vec(Trace *t, int32 x, int32 y, int32 z)
{
    text(t, "[");
    integer(t, x);
    text(t, ",");
    integer(t, y);
    text(t, ",");
    integer(t, z);
    text(t, "]");
}
static int32 itemIndex(const ItemObj *p)
{
    return p ? (int32)(p - gs->items) : -1;
}
static int32 roomIndex(const Room *p)
{
    return p ? (int32)(p - gs->rooms) : -1;
}
#define KEY(k) text(t, "\"" k "\":")
#define SCALAR(k, v)                                                                               \
    do                                                                                             \
    {                                                                                              \
        KEY(k);                                                                                    \
        integer(t, (v));                                                                           \
    } while (0)
#define UNSIGNED(k, v)                                                                             \
    do                                                                                             \
    {                                                                                              \
        KEY(k);                                                                                    \
        number(t, (v));                                                                            \
    } while (0)
#define VEC(k, v)                                                                                  \
    do                                                                                             \
    {                                                                                              \
        KEY(k);                                                                                    \
        vec(t, (v).x, (v).y, (v).z);                                                               \
    } while (0)
static void nav(Trace *t, const Nav *n)
{
    uint32 i;
    text(t, "{");
    UNSIGNED("cellsCount", n->cellsCount);
    text(t, ",");
    UNSIGNED("zoneType", n->zoneType);
    text(t, ",");
    UNSIGNED("weight", n->weight);
    text(t, ",");
    UNSIGNED("endBox", n->endBox);
    text(t, ",");
    UNSIGNED("nextBox", n->nextBox);
    text(t, ",");
    UNSIGNED("headBox", n->headBox);
    text(t, ",");
    UNSIGNED("tailBox", n->tailBox);
    text(t, ",");
    SCALAR("stepHeight", n->stepHeight);
    text(t, ",");
    SCALAR("dropHeight", n->dropHeight);
    text(t, ",");
    SCALAR("vSpeed", n->vSpeed);
    text(t, ",");
    UNSIGNED("mask", n->mask);
    text(t, ",");
    VEC("pos", n->pos);
    text(t, ",\"cells\":[");
    for (i = 0; i < gs->level.boxesCount; ++i)
    {
        const Nav_Cell *c = n->cells + i;
        if (i)
            text(t, ",");
        text(t, "[");
        number(t, c->boxIndex);
        text(t, ",");
        number(t, c->weight);
        text(t, ",");
        number(t, c->end);
        text(t, ",");
        number(t, c->next);
        text(t, "]");
    }
    text(t, "]}");
}
static void arm(Trace *t, const ExtraInfoLara_Arm *a)
{
    text(t, "{");
    VEC("angle", a->angle);
    text(t, ",");
    VEC("angleAim", a->angleAim);
    text(t, ",");
    SCALAR("anim", a->animIndex);
    text(t, ",");
    SCALAR("frame", a->frameIndex);
    text(t, ",");
    SCALAR("target", itemIndex(a->target));
    text(t, ",");
    SCALAR("aim", a->aim);
    text(t, ",");
    SCALAR("useBasis", a->useBasis);
    text(t, ",\"flash\":[");
    integer(t, a->flash.timer);
    text(t, ",");
    integer(t, a->flash.angle);
    text(t, ",");
    integer(t, a->flash.offset);
    text(t, ",");
    integer(t, a->flash.intensity);
    text(t, "]}");
}
static void item(Trace *t, const ItemObj *s, bool lara)
{
    text(t, "{");
    SCALAR("index", itemIndex(s));
    text(t, ",");
    SCALAR("type", s->type);
    text(t, ",");
    VEC("pos", s->pos);
    text(t, ",");
    VEC("rot", s->angle);
    text(t, ",");
    SCALAR("room", roomIndex(s->room));
    text(t, ",");
    SCALAR("anim", s->animIndex);
    text(t, ",");
    SCALAR("frame", s->frameIndex);
    text(t, ",");
    SCALAR("state", s->state);
    text(t, ",");
    SCALAR("goal", s->goalState);
    text(t, ",");
    SCALAR("next", s->nextState);
    text(t, ",");
    SCALAR("flags", s->flags);
    text(t, ",");
    SCALAR("health", s->health);
    text(t, ",");
    SCALAR("hSpeed", s->hSpeed);
    text(t, ",");
    SCALAR("vSpeed", s->vSpeed);
    text(t, ",");
    SCALAR("turnSpeed", s->turnSpeed);
    text(t, ",");
    SCALAR("timer", s->timer);
    text(t, ",");
    SCALAR("water", s->waterState);
    text(t, ",");
    SCALAR("mood", s->input);
    text(t, ",");
    SCALAR("headOffset", s->headOffset);
    text(t, ",");
    SCALAR("aggression", s->aggression);
    text(t, ",");
    SCALAR("intensity", s->intensity);
    text(t, ",");
    SCALAR("roomFloor", s->roomFloor);
    text(t, ",");
    UNSIGNED("hitMask", s->hitMask);
    text(t, ",");
    UNSIGNED("visibleMask", s->visibleMask);
    text(t, ",\"ai\":");
    {
        const ExtraInfoEnemy *e = NULL;
        uint32 slot;
        for (slot = 0; slot < 8; ++slot)
            if (s->extraE == gs->enemiesExtra + slot && gs->enemiesExtra[slot].enemy == s)
            {
                e = gs->enemiesExtra + slot;
                break;
            }
        if (!e)
            text(t, "null");
        else
        {
            text(t, "{");
            SCALAR("slot", slot);
            text(t, ",");
            SCALAR("rotHead", e->rotHead);
            text(t, ",");
            SCALAR("rotNeck", e->rotNeck);
            text(t, ",");
            SCALAR("maxTurn", e->maxTurn);
#if COOP_TRACE
            if (t->coop) {
                text(t,",");
                SCALAR("target",itemIndex(e->target));
                text(t,",\"contact\":[");
                number(t,e->contact[0]);text(t,",");number(t,e->contact[1]);text(t,"]");
            }
#endif
            text(t, ",\"nav\":");
            nav(t, &e->nav);
            text(t, "}");
        }
    }
    if (lara)
    {
        const ExtraInfoLara *e = s->extraL;
        uint32 i;
        text(t, ",");
        SCALAR("oxygen", s->timer);
        text(t, ",");
        SCALAR("weaponState", e->weaponState);
        text(t, ",");
        SCALAR("weapon", e->weapon);
        text(t, ",");
        SCALAR("goalWeapon", e->goalWeapon);
        text(t, ",");
        SCALAR("input", s->input);
        text(t, ",");
        SCALAR("swimTimer", e->swimTimer);
        text(t, ",");
        SCALAR("vSpeedHack", e->vSpeedHack);
        text(t, ",");
        SCALAR("moveAngle", e->moveAngle);
        text(t, ",");
        SCALAR("hitFrame", e->hitFrame);
        text(t, ",");
        SCALAR("hitTimer", e->hitTimer);
        text(t, ",");
        SCALAR("hitQuadrant", e->hitQuadrant);
        text(t, ",");
        SCALAR("hazardCause", e->hazardCause);
        text(t, ",");
        SCALAR("lastInput", e->lastInput);
        text(t, ",");
        SCALAR("healthTimer", e->healthTimer);
        text(t, ",");
        SCALAR("dozy", e->dozy);
        text(t, ",");
        VEC("head", e->head.angle);
        text(t, ",");
        VEC("torso", e->torso.angle);
        text(t, ",\"armR\":");
        arm(t, &e->armR);
        text(t, ",\"armL\":");
        arm(t, &e->armL);
        text(t, ",\"meshes\":[");
        for (i = 0; i < JOINT_MAX; ++i)
        {
            if (i)
                text(t, ",");
            number(t, e->meshes[i]);
        }
        text(t, "],\"nav\":");
        nav(t, &e->nav);
    }
    text(t, "}");
}
static void camera(Trace *t, const Camera *c)
{
    text(t, "{");
    VEC("pos", c->view.pos);
    text(t, ",");
    VEC("target", c->target.pos);
    text(t, ",");
    SCALAR("room", roomIndex(c->view.room));
    text(t, ",");
    SCALAR("targetRoom", roomIndex(c->target.room));
    text(t, ",");
    SCALAR("targetDist", c->targetDist);
    text(t, ",");
    VEC("targetAngle", c->targetAngle);
    text(t, ",");
    VEC("angle", c->angle);
    text(t, ",");
    SCALAR("laraItem", itemIndex(c->laraItem));
    text(t, ",");
    SCALAR("lastItem", itemIndex(c->lastItem));
    text(t, ",");
    SCALAR("lookAtItem", itemIndex(c->lookAtItem));
    text(t, ",");
    SCALAR("speed", c->speed);
    text(t, ",");
    SCALAR("timer", c->timer);
    text(t, ",");
    SCALAR("index", c->index);
    text(t, ",");
    SCALAR("lastIndex", c->lastIndex);
    text(t, ",");
    SCALAR("bounce", c->bounce);
    text(t, ",");
    SCALAR("mode", c->mode);
    text(t, ",");
    SCALAR("lastFixed", c->lastFixed);
    text(t, ",");
    SCALAR("center", c->center);
    text(t, "}");
}
/* A cutscene level has no Lara: only the cinematic camera's own fields are
 * traced (the rest of playersExtra[0].camera is never written there and keeps
 * whatever the previous level left: targetAngle x/z, angle z). `eye` is
 * what updateCinematic gave matrixSetView (src/fixed/camera.h:66). */
static void cinematicCamera(Trace *t)
{
    const Camera *c = &gs->playersExtra[0].camera;
    text(t, "{");
    VEC("pos", c->view.pos);
    text(t, ",");
    SCALAR("room", roomIndex(c->view.room));
    text(t, ",");
    SCALAR("targetAngleY", c->targetAngle.y);
    text(t, ",\"angle\":[");
    integer(t, c->angle.x);
    text(t, ",");
    integer(t, c->angle.y);
    text(t, "],");
    SCALAR("timer", c->timer);
    text(t, ",");
    VEC("eye", gs->gCameraViewPos);
    text(t, "}");
}
static void save(Trace *t, const SaveGame *s)
{
    uint32 i;
    text(t, "{");
    UNSIGNED("version", s->version);
    text(t, ",");
    UNSIGNED("dataSize", s->dataSize);
    text(t, ",");
    SCALAR("itemCount", s->itemCount);
    text(t, ",");
    SCALAR("level", s->level);
    text(t, ",");
    SCALAR("track", s->track);
    text(t, ",");
    SCALAR("secrets", s->secrets);
    text(t, ",");
    SCALAR("pickups", s->pickups);
    text(t, ",");
    UNSIGNED("time", s->time);
    text(t, ",");
    UNSIGNED("distance", s->distance);
    text(t, ",");
    UNSIGNED("randSeedLogic", s->randSeedLogic);
    text(t, ",");
    UNSIGNED("randSeedDraw", s->randSeedDraw);
    text(t, ",");
    SCALAR("mediUsed", s->mediUsed);
    text(t, ",");
    SCALAR("ammoUsed", s->ammoUsed);
    text(t, ",");
    SCALAR("kills", s->kills);
    text(t, ",");
    SCALAR("flipped", s->flipped);
    text(t, ",\"tracks\":[");
    for (i = 0; i < 64; ++i)
    {
        if (i)
            text(t, ",");
        number(t, s->tracks[i]);
    }
    text(t, "],\"invSlots\":[");
    for (i = 0; i < 64; ++i)
    {
        if (i)
            text(t, ",");
        number(t, s->invSlots[i]);
    }
    text(t, "]}");
}
#if !TRACE_SCHEMA2_ONLY
/* Schema 4 `world`: the definition in test/oracle/oracle.cpp (worldHashes), over the live
 * sectors of every logical room, the live box overlap flags and the ten flip maps, then the
 * world validator's verdict (the item-list check of worldBuildRoomPairs; see game/world.c). */
static uint32 hashByte(uint32 h, uint8 b) { return (h ^ b) * 16777619u; }
static uint32 hashWord(uint32 h, uint16 v) { return hashByte(hashByte(h, (uint8)(v & 255)), (uint8)(v >> 8)); }
static void world(Trace *t)
{
    uint32 w[4], h = 2166136261u, r, k, n;
    text(t, ",\"world\":");
    if (gs->gLevelID == LVL_TR1_TITLE)
    {
        text(t, "null");
        return;
    }
    for (r = 0; r < gs->level.roomsCount; ++r)
    {
        const Room *room = gs->rooms + r;
        h = hashWord(h, (uint16)room->source);
        n = tr64_u16(room->info->x_sectors) * tr64_u16(room->info->z_sectors);
        for (k = 0; k < n; ++k)
        {
            const Sector *s = room->sectors + k;
            h = hashWord(h, s->floorIndex);
            h = hashWord(h, s->boxIndex);
            h = hashByte(h, s->roomBelow);
            h = hashByte(h, (uint8)s->floor);
            h = hashByte(h, s->roomAbove);
            h = hashByte(h, (uint8)s->ceiling);
        }
    }
    w[0] = h;
    h = 2166136261u;
    for (k = 0; k < gs->level.boxesCount; ++k)
        h = hashWord(h, gs->boxFlags[k]);
    w[1] = h;
    h = 2166136261u;
    for (k = 0; k < 10; ++k)
    {
        h = hashByte(h, gs->flipMaps[k].mask);
        h = hashByte(h, gs->flipMaps[k].flags);
    }
    w[2] = h;
    w[3] = game_world_items_linked();
    text(t, "[");
    number(t, w[0]);
    text(t, ",");
    number(t, w[1]);
    text(t, ",");
    number(t, w[2]);
    text(t, ",");
    number(t, w[3]);
    text(t, "]");
}
static void extendedInventory(Trace *t)
{
    const GameInventory *inv=&gs->inventory;
#define IV(name) do { text(t,","); SCALAR(#name,inv->name); } while(0)
    IV(timer);IV(nextState);IV(frameIndex);IV(frameTarget);
    IV(height);IV(radius);IV(selDist);IV(pitch);IV(rot);IV(rotItem);
    IV(selRotPre);IV(selRotX);IV(selRotY);
    IV(heightTarget);IV(heightInc);IV(radiusTarget);IV(radiusInc);
    IV(pitchTarget);IV(pitchInc);IV(rotTarget);IV(rotInc);IV(rotItemInc);
    IV(selDistTarget);IV(selDistInc);IV(selRotPreTarget);IV(selRotPreInc);
    IV(selRotXTarget);IV(selRotXInc);IV(selRotYTarget);IV(selRotYInc);
    text(t,",\"itemsList\":[");
    for(uint32 i=0;i<SLOT_MAX;++i) {
        if(i)text(t,",");
        integer(t,inv->itemsList[i]);
    }
    text(t,"]");
    IV(itemsCount);IV(itemIndex);IV(nextIndex);IV(optionIndex);
    IV(optionsCount);IV(optionsWidth);IV(optionsHeight);
    text(t,",");UNSIGNED("itemVisMask",inv->itemVisMask);
    IV(passportPage);IV(statsNextLevel);
    text(t,",\"options\":[");
    for(uint32 i=0;i<32;++i) {
        if(i)text(t,",");
        vec(t,inv->options[i].type,inv->options[i].str,inv->options[i].value);
    }
    text(t,"]");
#undef IV
}
static void settings(Trace *t)
{
    const GameSettings *s=&gs->gSettings;
    text(t,",\"settings\":[");
    integer(t,s->video_gamma);text(t,",");integer(t,s->video_fog);
    text(t,",");integer(t,s->video_fps);text(t,",");integer(t,s->video_vsync);
    text(t,",");integer(t,s->audio_sfx);text(t,",");integer(t,s->audio_music);
    text(t,",");integer(t,s->controls_vibration);text(t,",");integer(t,s->controls_swap);
    text(t,"]");
}
#endif
#if COOP_TRACE
static void party(Trace *t)
{
    text(t,",");SCALAR("present",game_player_mask());
    text(t,",");SCALAR("alive",game_player_alive_mask());
    text(t,",\"players\":[");
    for (unsigned slot=0;slot<TR64_GAME_PLAYERS;++slot) {
        if (slot)text(t,",");
        if (!gs->players[slot]) { text(t,"null");continue; }
        text(t,"{");SCALAR("slot",slot);
        text(t,",\"lara\":");item(t,gs->players[slot],true);
        text(t,",\"camera\":");camera(t,&gs->playersExtra[slot].camera);
        text(t,",\"flow\":[");integer(t,gs->flow_camera[slot]);text(t,",");
        number(t,gs->flow_active[slot]);text(t,"]");
        const LaraHazardState *burn=&gs->gLaraHazardState[slot];
        text(t,",\"burn\":[");
        number(t,burn->burnTicks);text(t,",");number(t,burn->damageCooldown);
        text(t,",");integer(t,burn->flameItemIndex);text(t,",");number(t,burn->flags);
        text(t,"],");SCALAR("interaction",itemIndex(gs->interaction[slot]));
        text(t,"}");
    }
    text(t,"],\"cinematic\":");
    if (game_is_cutscene_level(gs->gLevelID))cinematicCamera(t);
    else text(t,"null");
    text(t,",");SCALAR("ringOwner",gs->inventory.state==INV_STATE_NONE ? -1 : (int32)game_menu_player());
    text(t,",");SCALAR("useOwner",gs->inventory.useSlot==SLOT_MAX ||
                        !gs->inventory.lara ? -1 : (int32)Lara_slot(gs->inventory.lara));
    text(t,",");SCALAR("deathReady",gs->stop.death_ready);
    text(t,",");SCALAR("pendingLevel",gs->pendingLevel);
    text(t,",");SCALAR("completionOwed",gs->retireLevel);
    text(t,",\"retiredActions\":[");
    bool retiredComma=false;
    for (unsigned i=0;i<TR64_GAME_PLAYERS;++i) {
        const RetiredAction *a=gs->retired+i;
        if (!a->actor)continue;
        if (retiredComma)text(t,",");
        retiredComma=true;
        text(t,"[");integer(t,itemIndex(gs->interaction[i]));text(t,",");
        integer(t,itemIndex(a->actor));text(t,",");number(t,a->ticks);text(t,"]");
    }
    text(t,"]");
    text(t,",\"blocks\":[");
    bool comma=false;
    for (unsigned i=0;i<gs->level.itemsCount;++i) {
        const ItemObj *b=gs->items+i;
        if (b->type<ITEM_BLOCK_1 || b->type>ITEM_BLOCK_4 || !b->room)continue;
        if (comma)text(t,",");
        comma=true;
        text(t,"[");number(t,i);text(t,",");integer(t,(int32)(b->room-gs->rooms));
        text(t,",");vec(t,b->pos.x,b->pos.y,b->pos.z);text(t,",");number(t,b->flags&ITEM_FLAG_STATUS);
        text(t,",");integer(t,Room_getSector(b->room,b->pos.x,b->pos.z)->floor*256);text(t,"]");
    }
    text(t,"]");
    text(t,",\"effect\":[");
    integer(t,gs->effect.id);text(t,",");number(t,gs->effect.timer);
    text(t,",");integer(t,itemIndex(gs->effect.lara));text(t,",");integer(t,gs->effect.cameraBounce);
    text(t,"],\"pickups\":[");
    for (unsigned i=0;i<gs->pickupCount;++i) {
        if (i)text(t,",");
        uint16 type=gs->pickupType[i];
        text(t,"[");number(t,type&0xff);text(t,",");number(t,type>>8);text(t,"]");
    }
    text(t,"]");
}
#endif
static uint32 record(Trace *t)
{
    ItemObj *s;
    uint32 i;
    if (!gs || (!gs->players[0] && gs->gLevelID!=LVL_TR1_TITLE &&
                !game_is_cutscene_level(gs->gLevelID)))
        sys_error("game trace before level");
    if (gs->stop.kind != TR64_GAME_STOP_NONE)
        sys_error("%s", gs->stop.name);
#if TRACE_SCHEMA2_ONLY
    text(t, "{\"schema\":2,\"tick\":");
    number(t, gs->tick);
    {
#else
#if COOP_TRACE
    if (t->coop)text(t,"{\"schema\":5,\"tick\":");
    else
#endif
    text(t,t->world ? "{\"schema\":4,\"tick\":" :
          t->extended ? "{\"schema\":3,\"tick\":" : "{\"schema\":2,\"tick\":");
    number(t, gs->tick);
    if(t->extended) { text(t,",");SCALAR("level",gs->gLevelID); }
#if COOP_TRACE
    if (t->coop)party(t);
    else {
#else
    {
#endif
#endif
        text(t,",\"lara\":");
        if(gs->players[0])item(t,gs->players[0],true);
        else text(t,"null");
        text(t,",\"camera\":");
        if(gs->players[0])camera(t,&gs->playersExtra[0].camera);
        else if(game_is_cutscene_level(gs->gLevelID))cinematicCamera(t);
        else text(t,"null");
    }
    text(t, ",\"active\":[");
    for (s = gs->firstActive, i = 0; s; s = s->nextActive, ++i)
    {
        if (i)
            text(t, ",");
        item(t, s, false);
    }
    text(t, "],\"save\":");
    save(t, &gs->gSaveGame);
    text(t, ",\"inventory\":{\"counts\":[");
    for (i = 0; i < SLOT_MAX; ++i)
    {
        if (i)
            text(t, ",");
        integer(t, gs->inventory.counts[i]);
    }
    text(t, "],");
    SCALAR("numKeys", gs->inventory.numKeys);
    text(t, ",");
    SCALAR("useSlot", gs->inventory.useSlot);
    text(t, ",");
    SCALAR("state", gs->inventory.state);
    text(t, ",");
    SCALAR("page", gs->inventory.page);
#if !TRACE_SCHEMA2_ONLY
    if(t->extended)extendedInventory(t);
#endif
    text(t,"}");
#if !TRACE_SCHEMA2_ONLY
    if(t->extended)settings(t);
    if(t->world)world(t);
#endif
    text(t,",\"rng\":[");
    number(t, gs->gRandSeedLogic);
    text(t, ",");
    number(t, gs->gRandSeedDraw);
    text(t, "]");
    return (t->hash ^ (uint8)'}') * 16777619u;
}
#if TRACE_SCHEMA2_ONLY
uint32 game_state_hash(void)
{
    Trace t = {NULL, NULL, 2166136261u};
    return record(&t);
}
uint32_t tr64_unit_trace(uint32_t argument)
{
    (void)argument;
    return game_state_hash();
}
#else
uint32 game_state_hash_schema(uint32 schema)
{
    if(schema<2 || schema>4+COOP_TRACE)sys_error("game hash schema");
    Trace t = {NULL,NULL,2166136261u,schema>=3,schema>=4
#if COOP_TRACE
               ,schema==5
#endif
    };
    return record(&t);
}
uint32 game_state_hash(void)
{
    return game_state_hash_schema(2);
}
static void trace_end(tr64_game_writer_t write, void *context, uint32 hash)
{
    static const char hex[] = "0123456789abcdef";
    char end[] = ",\"hash\":\"00000000\"}\n";
    for (uint32 i = 0; i < 8; ++i)
        end[9 + i] = hex[(hash >> (28 - i * 4)) & 15];
    if (write)
        write(context, end, 20);
}
#endif

#if TR64_HAS_TR2
#include "engine/core/log.h"
/* Record order and projections: docs/specs/wall/tr2-oracle-trace.md,
 * Record fields. Missing state is encoded here, never by reading the oracle. */
static void arm20(Trace *t, const ExtraInfoLara_Arm *a)
{
    text(t,"{"); VEC("angle", a->angle);
    text(t,","); SCALAR("anim", a->animIndex);
    text(t,","); SCALAR("frame", a->frameIndex);
    text(t,","); SCALAR("aim", a->aim);
    text(t,","); SCALAR("flash", a->flash.timer);
    text(t,"}");
}

static void item20(Trace *t, const ItemObj *s)
{
    text(t,"{"); SCALAR("index", itemIndex(s));
    text(t,","); SCALAR("type", s->type);
    text(t,","); VEC("pos", s->pos);
    text(t,","); VEC("rot", s->angle);
    text(t,","); SCALAR("room", roomIndex(s->room));
    text(t,","); SCALAR("anim", s->animIndex);
    text(t,","); SCALAR("frame", s->frameIndex);
    text(t,","); SCALAR("state", s->state);
    text(t,","); SCALAR("goal", s->goalState);
    text(t,","); SCALAR("next", s->nextState);
    text(t,","); UNSIGNED("flags", s->extraL ? gs->tr2->laraFlags : s->flags & 0xff00u);
    text(t,","); SCALAR("health", s->health);
    text(t,","); SCALAR("hSpeed", s->hSpeed);
    text(t,","); SCALAR("vSpeed", s->vSpeed);
    text(t,","); SCALAR("timer", s->extraL ? gs->tr2->laraItemTimer : s->timer);
    text(t,","); SCALAR("roomFloor", s->roomFloor);
    text(t,","); UNSIGNED("hitMask", s->hitMask);
    text(t,","); UNSIGNED("visibleMask", s->visibleMask);
    text(t,","); SCALAR("status", (s->flags & ITEM_FLAG_STATUS) >> 3);
    text(t,","); SCALAR("gravity", !!(s->flags & ITEM_FLAG_GRAVITY));
    text(t,","); SCALAR("wasHit", !!(s->flags & ITEM_FLAG_INJURED));
    text(t,","); SCALAR("collidable", !!(s->flags & ITEM_FLAG_COLLISION));
}

/* Schema 21 appends a brain only while the active item has a slot. */
static void brain21(Trace *t, unsigned slot, const TR2Creature *ai)
{
    text(t,",\"ai\":{"); UNSIGNED("slot", slot);
    text(t,","); SCALAR("enemy", itemIndex(ai->enemy));
    text(t,","); SCALAR("mood", ai->mood);
    text(t,",\"jointYaw\":["); integer(t,ai->jointYaw[0]); text(t,","); integer(t,ai->jointYaw[1]); text(t,"]");
    text(t,","); SCALAR("maxTurn", ai->maxTurn);
    text(t,","); UNSIGNED("flags", ai->flags);
    text(t,","); SCALAR("box", ai->box);
    text(t,","); SCALAR("targetBox", ai->targetBox);
    text(t,","); SCALAR("requiredBox", ai->requiredBox);
    text(t,","); SCALAR("nextBox", ai->box == -1 ? -1 : ai->nextBox);
    text(t,","); SCALAR("headBox", ai->headBox);
    text(t,","); SCALAR("tailBox", ai->tailBox);
    text(t,","); UNSIGNED("search", ai->search);
    text(t,","); UNSIGNED("boxSearch", ai->box == -1 ? 0 : ai->boxSearch);
    text(t,","); SCALAR("step", ai->step);
    text(t,","); SCALAR("drop", ai->drop);
    text(t,","); SCALAR("fly", ai->fly);
    text(t,","); UNSIGNED("blockMask", ai->blockMask);
    text(t,","); SCALAR("zoneCount", ai->zoneCount);
    text(t,",\"destination\":[");
    for (unsigned i=0;i<3;++i) {
        if (i) text(t,",");
        integer(t,ai->targetBox == -1 ? 0 : ai->destination[i]);
    }
    text(t,"],\"waypoint\":[");
    for (unsigned i=0;i<3;++i) {
        if (i) text(t,",");
        integer(t,ai->targetBox == -1 ? 0 : ai->waypoint[i]);
    }
    text(t,"]}");
}


static void lara20(Trace *t, const Lara *s)
{
    const ExtraInfoLara *e = s->extraL;
    const GameTR2 *probe = gs->tr2;
    /* The shared weapon state is zero-based at pistols; the wire isn't. */
    static const uint8 weapon20[] = {
        [WEAPON_NONE]=0, [WEAPON_PISTOLS]=1, [WEAPON_MAGNUMS]=2,
        [WEAPON_UZIS]=3, [WEAPON_SHOTGUN]=4, [TR2_WEAPON_FLARE]=8
    };
    item20(t,s);
    text(t,","); SCALAR("water", s->waterState);
    text(t,","); SCALAR("oxygen", s->timer);
    text(t,","); SCALAR("weaponState", e->weaponState);
    text(t,","); SCALAR("weapon", weapon20[e->weapon]);
    text(t,","); SCALAR("goalWeapon", weapon20[e->goalWeapon]);
    text(t,","); SCALAR("lastWeapon", probe->lastWeapon);
    text(t,","); UNSIGNED("input", probe->input);
    text(t,","); SCALAR("swimTimer", e->swimTimer);
    text(t,","); SCALAR("moveAngle", e->moveAngle);
    text(t,","); SCALAR("turnSpeed", s->turnSpeed);
    text(t,","); SCALAR("hitFrame", e->hitFrame);
    text(t,","); SCALAR("hitTimer", e->hitTimer);
    text(t,","); SCALAR("hitQuadrant", e->hitQuadrant);
    text(t,","); VEC("head", e->head.angle);
    text(t,","); VEC("torso", e->torso.angle);
    text(t,","); SCALAR("flareAge", probe->arms.flareAge);
    text(t,","); SCALAR("flareFrame", probe->arms.flareFrame);
    text(t,","); SCALAR("flareLeft", probe->arms.flareLeft);
    text(t,",\"flareRight\":0");
    text(t,","); SCALAR("burning", gs->gLaraHazardState[0].burnTicks != 0);
    text(t,","); SCALAR("waterDistance", probe->waterDistance);
    text(t,","); SCALAR("climbing", probe->climbEnabled);
    text(t,","); SCALAR("vehicle", probe->vehicle);
    text(t,","); SCALAR("weaponItem",itemIndex(probe->shotgunPose));
    text(t,","); SCALAR("backWeapon", probe->backWeapon);
    text(t,","); SCALAR("target", itemIndex(e->armR.target ? e->armR.target : e->armL.target));
    text(t,",\"armR\":"); arm20(t,&e->armR);
    text(t,",\"armL\":"); arm20(t,&e->armL);
    text(t,",\"ammo\":[");
    for (unsigned i = 0; i < 7; ++i) {
        if (i) text(t,",");
        number(t,probe->ammo[i]);
    }
    text(t,"]}");
}

static void camera20(Trace *t, const Camera *c)
{
    static const uint8 mode[] = {
        [CAMERA_MODE_FOLLOW]=0, [CAMERA_MODE_FIXED]=1, [CAMERA_MODE_LOOK]=2,
        [CAMERA_MODE_COMBAT]=3, [CAMERA_MODE_CUTSCENE]=4, [CAMERA_MODE_OBJECT]=5
    };
    text(t,"{"); VEC("pos", c->view.pos);
    text(t,","); VEC("target", c->target.pos);
    text(t,","); SCALAR("room", roomIndex(c->view.room));
    text(t,","); SCALAR("targetRoom", roomIndex(c->target.room));
    text(t,","); SCALAR("targetDist", c->targetDist);
    text(t,","); SCALAR("targetAngle", c->targetAngle.y);
    text(t,","); SCALAR("angle", c->angle.y);
    text(t,","); SCALAR("targetElevation", c->targetAngle.x);
    text(t,","); SCALAR("lookAtItem", itemIndex(c->lookAtItem));
    text(t,","); SCALAR("lastItem", itemIndex(c->lastItem));
    text(t,","); SCALAR("speed", c->speed);
    text(t,","); SCALAR("timer", c->timer);
    text(t,","); SCALAR("index", c->index);
    text(t,","); SCALAR("lastIndex", c->lastIndex);
    text(t,","); SCALAR("bounce", c->bounce);
    text(t,","); SCALAR("mode", mode[c->mode]);
    text(t,","); SCALAR("lastFixed", c->lastFixed);
    text(t,"}");
}

static void world20(Trace *t)
{
    uint32 h = 2166136261u, pairs = h, boxes = h, flips = h;
    for (unsigned r = 0; r < gs->level.roomsCount; ++r) {
        const Room *room = gs->rooms + r;
        pairs = hashWord(pairs,tr64_i16(room->info->alternate_room));
        unsigned n = tr64_u16(room->info->x_sectors) * tr64_u16(room->info->z_sectors);
        for (unsigned i = 0; i < n; ++i) {
            const Sector *s = room->sectors + i;
            h = hashWord(h,s->floorIndex);
            h = hashWord(h,s->boxIndex);
            h = hashByte(h,s->roomBelow);
            h = hashByte(h,s->floor);
            h = hashByte(h,s->roomAbove);
            h = hashByte(h,s->ceiling);
        }
    }
    for (unsigned i = 0; i < gs->level.boxesCount; ++i)
        boxes = hashWord(boxes,gs->boxFlags[i]);
    /* Schema 20 uses the source trigger-word bit positions, not the
     * compact runtime's derived completeness bit. */
    flips = hashWord(hashWord(flips,gs->gSaveGame.flipped),0);
    for (unsigned i = 0; i < 10; ++i) {
        const FlipMapState *slot = gs->flipMaps + i;
        uint16 flags = (slot->mask << 9) | ((slot->flags & 2) ? ITEM_FLAG_ONCE : 0);
        flips = hashWord(hashWord(flips,flags),0);
    }
    text(t,"["); number(t,h); text(t,","); number(t,boxes);
    text(t,","); number(t,flips); text(t,","); number(t,pairs); text(t,"]");
}

static void inventory20(Trace *t)
{
    const GameTR2 *tr2 = gs->tr2;
    bool haveEntry = false;
    text(t,"{\"items\":[");
    if (TR64_RULE(FRESH_STOCK))
        for (unsigned i = 0; i < tr2->carriedCount; ++i) {
            const TR2RingEntry *entry = tr2->carried + i;
            if (!entry->quantity)
                continue;
            text(t,haveEntry ? ",[" : "["); number(t,entry->type);
            text(t,","); number(t,entry->quantity); text(t,"]");
            haveEntry = true;
        }
    text(t,"],"); SCALAR("mainSelection",tr2->mainSelection);
    text(t,","); SCALAR("keySelection",tr2->keySelection);
    text(t,"}");
}

#if defined(TR64_HOST) || !TR64_PLAY || TR64_TEST
/* Schema-22 Venice retail oracle slot anchors live strictly in trace.c;
 * gameplay code never references level-specific item indices. */
enum {
    SCHEMA22_BOAT_0 = 26, SCHEMA22_BOAT_1 = 91,
    SCHEMA22_RACE_LEAF_0 = 73, SCHEMA22_RACE_LEAF_1 = 74, SCHEMA22_RACE_CLOCK = 76,
    SCHEMA22_MINE_FIRST = 106, SCHEMA22_MINE_COUNT = 6
};

void game_tr2_validate_schema22(void)
{
    if (gs->level.itemsCount <= SCHEMA22_MINE_FIRST + SCHEMA22_MINE_COUNT - 1u)
        sys_error("Tomb Raider II: schema-22 item table too short for oracle slots");
    static const struct { unsigned slot, type; const char *name; } anchors[] = {
        {SCHEMA22_BOAT_0, TR2_ITEM_BOAT, "boat 26"},
        {SCHEMA22_BOAT_1, TR2_ITEM_BOAT, "boat 91"},
        {SCHEMA22_RACE_LEAF_0, 110, "race leaf 73"},
        {SCHEMA22_RACE_LEAF_1, 111, "race leaf 74"},
        {SCHEMA22_RACE_CLOCK, TR2_ITEM_RACE_CLOCK, "race clock 76"},
    };
    for (unsigned i = 0; i < sizeof(anchors) / sizeof(*anchors); ++i) {
        const ItemObj *it = gs->items + anchors[i].slot;
        if (it->type != anchors[i].type)
            sys_error("Tomb Raider II: schema-22 malformed %s (slot=%u type=%u)",
                      anchors[i].name, anchors[i].slot, (unsigned)it->type);
        if (it->animIndex == UINT16_MAX)
            sys_error("Tomb Raider II: schema-22 %s has no animation", anchors[i].name);
    }
    for (unsigned i = 0; i < SCHEMA22_MINE_COUNT; ++i) {
        unsigned slot = SCHEMA22_MINE_FIRST + i;
        const ItemObj *it = gs->items + slot;
        if (it->type != TR2_ITEM_MINE)
            sys_error("Tomb Raider II: schema-22 malformed mine slot %u (type=%u)",
                      slot, (unsigned)it->type);
        if (it->animIndex == UINT16_MAX)
            sys_error("Tomb Raider II: schema-22 mine slot %u has no animation", slot);
    }
}

static void venice22(Trace *t)
{
    static const unsigned boatSlots[2] = {SCHEMA22_BOAT_0, SCHEMA22_BOAT_1};
    static const unsigned leafSlots[2] = {SCHEMA22_RACE_LEAF_0, SCHEMA22_RACE_LEAF_1};
    const GameTR2 *tr2 = gs->tr2;

    text(t,",\"boats\":[");
    for (unsigned i = 0; i < 2; ++i) {
        if (i) text(t,",");
        unsigned slot = boatSlots[i];
        const TR2BoatHull *h = game_tr2_boat_hull(gs->items + slot);
        text(t,"{"); UNSIGNED("index", slot);
        text(t,","); UNSIGNED("present", (h && h->present) ? 1u : 0u);
        if (!h || !h->present) {
            text(t,",\"item\":null,\"hull\":null}");
        } else {
            text(t,",\"item\":"); item20(t, gs->items + slot); text(t,"}");
            text(t,",\"hull\":{"); SCALAR("turn", h->turn);
            text(t,",\"bowVSpeed\":["); integer(t, h->bowVSpeed[0]);
            text(t,","); integer(t, h->bowVSpeed[1]); text(t,"],");
            SCALAR("bank", h->bank);
            text(t,","); SCALAR("terrainYaw", h->terrainYaw);
            text(t,","); SCALAR("supportY", h->supportY);
            text(t,"}}");
        }
    }
    text(t,"],\"race\":{");
    const ItemObj *clock = gs->items + SCHEMA22_RACE_CLOCK;
    SCALAR("timer", clock->timer);
    text(t,","); UNSIGNED("latch", (clock->flags & ITEM_FLAG_MASK) >> 9);
    text(t,","); UNSIGNED("active", !!(clock->flags & ITEM_FLAG_ACTIVE));
    text(t,",\"leaves\":[");
    for (unsigned i = 0; i < 2; ++i) {
        if (i) text(t,",");
        unsigned slot = leafSlots[i];
        const ItemObj *leaf = gs->items + slot;
        text(t,"{"); UNSIGNED("index", slot);
        text(t,","); SCALAR("timer", leaf->timer);
        text(t,","); UNSIGNED("latch", (leaf->flags & ITEM_FLAG_MASK) >> 9);
        text(t,","); UNSIGNED("active", !!(leaf->flags & ITEM_FLAG_ACTIVE));
        text(t,"}");
    }
    text(t,"]},\"mines\":{");
    UNSIGNED("chain", tr2->mineChain ? 1u : 0u);
    text(t,",\"items\":[");
    for (unsigned i = 0; i < SCHEMA22_MINE_COUNT; ++i) {
        if (i) text(t,",");
        unsigned slot = SCHEMA22_MINE_FIRST + i;
        const ItemObj *mine = gs->items + slot;
        text(t,"{"); UNSIGNED("index", slot);
        text(t,","); UNSIGNED("spent", !!(mine->flags & ITEM_FLAG_ONCE));
        text(t,","); UNSIGNED("active", !!(mine->flags & ITEM_FLAG_ACTIVE));
        text(t,"}");
    }
    text(t,"]},");
    unsigned used = 0;
    for (uint8 idx = tr2->effectActive; idx; idx = tr2->effects[idx - 1].next)
        ++used;
    UNSIGNED("effectsUsed", used);
}
#endif

static uint32_t tr2Record(tr64_game_writer_t write, void *context)
{
    Trace state = {.write=write, .context=context, .hash=2166136261u};
    Trace *t = &state;
    ASSERT(gs && gs->tr2);
    unsigned schema = gs->tr2->traceSchema;
#if defined(TR64_HOST) || !TR64_PLAY || TR64_TEST
    bool combat = schema >= 21;
    text(t, schema == 22 ? "{\"schema\":22," : schema == 21 ? "{\"schema\":21," : "{\"schema\":20,");
    UNSIGNED("tick", gs->tick);
    text(t, schema == 22 ? ",\"level\":2,\"lara\":" : ",\"level\":1,\"lara\":");
#else
    bool combat = schema == 21;
    text(t,combat ? "{\"schema\":21," : "{\"schema\":20,"); UNSIGNED("tick", gs->tick);
    text(t,",\"level\":1,\"lara\":");
#endif
    lara20(t,gs->players[0]);
    text(t,",\"camera\":"); camera20(t,&gs->playersExtra[0].camera);
    text(t,",\"active\":[");
    for (const ItemObj *s = gs->firstActive; s; s = s->nextActive) {
        if (s != gs->firstActive) text(t,",");
        item20(t,s);
        if (combat) {
            for (unsigned slot=0;slot<TR2_CREATURE_SLOTS;++slot)
                if (gs->tr2->creatures[slot].item == s) {
                    brain21(t,slot,gs->tr2->creatures+slot);
                    break;
                }
        }
        text(t,"}");
    }
    text(t,"],\"save\":{"); UNSIGNED("secrets", gs->gSaveGame.secrets);
    text(t,","); UNSIGNED("time", gs->gSaveGame.time);
    text(t,","); UNSIGNED("distance", gs->gSaveGame.distance);
    text(t,","); UNSIGNED("kills", gs->gSaveGame.kills);
    text(t,","); UNSIGNED("ammoUsed", gs->tr2->arms.ammoUsed);
    text(t,","); UNSIGNED("ammoHits", gs->tr2->ammoHits);
    text(t,","); UNSIGNED("mediUsed", gs->gSaveGame.mediUsed);
    text(t,"},\"inventory\":"); inventory20(t);
    text(t,",\"world\":"); world20(t);
    text(t,",\"rng\":["); number(t,gs->gRandSeedLogic);
    text(t,","); number(t,gs->gRandSeedDraw); text(t,"]");
#if defined(TR64_HOST) || !TR64_PLAY || TR64_TEST
    if (schema == 22)
        venice22(t);
#endif
    return hashByte(t->hash,'}');
}

uint32_t game_tr2_state_hash(void)
{
    return tr2Record(NULL,NULL);
}

#ifdef TR64_HOST
void game_trace_tr2(tr64_game_writer_t write, void *context)
{
    ASSERT(gs && gs->tr2);
    if (TR64_RULE(CAMERA_VIEW)) {
        GameTR2 *probe=gs->tr2;
        for (unsigned i=TR2_CAM_NO_BOX_POINT;i<=TR2_CAM_NO_BOX_ORBIT;++i) {
            uint32 count=probe->cameraChecks[i],old=probe->cameraNoBoxReported[i];
            if (count!=old) {
                log_printf("tr2-camera: no-box-%s tick=%u count=%u\n",
                           i==TR2_CAM_NO_BOX_POINT ? "point" : "orbit",
                           (unsigned)gs->tick,(unsigned)(count-old));
                probe->cameraNoBoxReported[i]=count;
            }
        }
    }
    trace_end(write,context,tr2Record(write,context));
}

/* Port-only footer; the individual retail-compatible records above are
 * unchanged. Both the coverage and its association with the rows are hashed. */
void game_trace_tr2_end(tr64_game_writer_t write, void *context, uint32 recordsHash)
{
    if (TR64_RULE(CAMERA_VIEW)) {
        const uint32 *counts=gs->tr2->cameraChecks;
        log_printf("tr2-camera: no-box-point=%u no-box-orbit=%u opaque-renewal=%u\n",
                   (unsigned)counts[TR2_CAM_NO_BOX_POINT],(unsigned)counts[TR2_CAM_NO_BOX_ORBIT],
                   (unsigned)counts[TR2_CAM_OPAQUE_RENEWAL]);
    }
    static const char unavailable[] =
        "\"lara.timer\":[0,null],"
        "\"lara.vehicle\":[0,null]";
    static const char unavailable22[] =
        "\"lara.timer\":[0,null]";
    static const char hex[] = "0123456789abcdef";
    char digest[8];
    Trace state = {.write=write, .context=context, .hash=2166136261u};
    Trace *t = &state;
    text(t,gs->tr2->traceSchema == 22
         ? "{\"schema\":22,\"producer\":\"tr64-tr2-venice-boat\",\"unavailable\":{"
         : gs->tr2->traceSchema == 21
         ? "{\"schema\":21,\"producer\":\"tr64-tr2-shotgun-rex-v2\",\"unavailable\":{"
         : "{\"schema\":20,\"producer\":\"tr64-tr2-shotgun-rex20-v2\",\"unavailable\":{");
    text(t,gs->tr2->traceSchema == 22 ? unavailable22 : unavailable);
    text(t,"}");
    if (TR64_RULE(CAMERA_VIEW)) {
        const GameTR2 *tr2=gs->tr2;
        text(t,",\"coverageEnds\":{");
        if (tr2->cameraUnavailableFrom && tr2->cameraUnavailableFrom<=gs->tick) {
            text(t,"\"camera\":{"); UNSIGNED("lastTick",tr2->cameraUnavailableFrom-1);
            text(t,",\"reason\":\""); text(t,tr2->cameraUnavailableReason); text(t,"\"}");
        }
        text(t,"}");
    }
    text(t,",\"recordsHash\":\"");
    for (unsigned i = 0; i < 8; ++i)
        digest[i] = hex[(recordsHash >> (28 - i * 4)) & 15];
    bytes(t,digest,sizeof(digest));
    text(t,"\"");
    trace_end(write,context,hashByte(t->hash,'}'));
}
#endif
#line 955
#endif
#if !TRACE_SCHEMA2_ONLY
void game_trace_schema(uint32 schema,tr64_game_writer_t write,void *context)
{
    if(schema<2 || schema>4+COOP_TRACE)sys_error("game trace schema");
    Trace t = {write,context,2166136261u,schema>=3,schema>=4
#if COOP_TRACE
               ,schema==5
#endif
    };
    trace_end(write, context, record(&t));
}
void game_trace(tr64_game_writer_t write,void *context)
{
    game_trace_schema(2,write,context);
}
#endif
