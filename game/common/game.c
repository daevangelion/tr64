/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/game.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Ported from src/fixed/game.h, item.h, lara.h, inventory.h and enemy.h
 * at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Keeps source constructor order and both deterministic RNG streams. */
#include "internal.h"
#include "save_world.h"
#include "save.h"
#include "tr64/res.h"
#if TR64_HAS_TR2
#include "tr64/code.h"
#include "game/tr2/objects.h"
#include "game/tr2/boat.h"
#include "game/tr2/creature.h"
#include "engine/core/log.h"
#include "object.h"

void game_tr2_validate_world_animations(void)
{
    (void)code_call("tr2_validate", TR2_VALIDATE_WORLD_ANIMATIONS);
}
#endif
GameState *gs;
static uint32 align4(uint32 n)
{
    return (n + 3u) & ~3u;
}
static void *part(uint8 **p, uint32 bytes)
{
    void *r = *p;
    *p += align4(bytes);
    return r;
}
static const void *table(uint32 kind)
{
    return tr64_level_table(gs->core, kind)->data;
}
int32 rand_logic(void)
{
    gs->gRandSeedLogic = 0x3039u + gs->gRandSeedLogic * 0x41c64e6du;
    return (gs->gRandSeedLogic >> 10) & 32767;
}
int32 rand_draw(void)
{
    gs->gRandSeedDraw = 0x3039u + gs->gRandSeedDraw * 0x41c64e6du;
    return (gs->gRandSeedDraw >> 10) & 32767;
}
static const struct { const char *name; int32 id,track; } campaign[] = {
    {"title",LVL_TR1_TITLE,4}, {"gym",LVL_TR1_GYM,-1},
    {"level1",LVL_TR1_1,5}, {"level2",LVL_TR1_2,5},
    {"level3a",LVL_TR1_3A,5}, {"level3b",LVL_TR1_3B,5},
    {"cut1",LVL_TR1_CUT_1,23}, {"level4",LVL_TR1_4,58}, {"level5",LVL_TR1_5,58},
    {"level6",LVL_TR1_6,58}, {"level7a",LVL_TR1_7A,57},
    {"level7b",LVL_TR1_7B,57}, {"cut2",LVL_TR1_CUT_2,25}, {"level8a",LVL_TR1_8A,58},
    {"level8b",LVL_TR1_8B,58}, {"level8c",LVL_TR1_8C,58},
    {"level10a",LVL_TR1_10A,57}, {"cut3",LVL_TR1_CUT_3,24},
    {"level10b",LVL_TR1_10B,59}, {"cut4",LVL_TR1_CUT_4,22},
    {"level10c",LVL_TR1_10C,59}
};
const char *game_level_name(int32 level)
{
    for(uint32 i=0;i<sizeof(campaign)/sizeof(campaign[0]);++i)
        if(campaign[i].id==level)return campaign[i].name;
#if TR64_HAS_TR2
    return game_tr2_level_name(level);
#else
    return NULL;
#endif
}
static int32 ambientTrack(int32 level)
{
    for(uint32 i=0;i<sizeof(campaign)/sizeof(campaign[0]);++i)
        if(campaign[i].id==level)return campaign[i].track;
#if TR64_HAS_TR2
    if(level>=TR2_LVL_BASE)return game_tr2_level_script_ambient_track(level);
#endif
    sys_error("game ambient level outside campaign");
}
void game_sndPlayTrack(int32 track)
{
    gs->gCurTrack = track;
    ++gs->musicSerial;
    gs->musicReason=TR64_GAME_MUSIC_DIRECT;
}
void game_music_request(tr64_game_music_request_t *out)
{
    if(!out)return;
    *out=gs ? (tr64_game_music_request_t){gs->gCurTrack,ambientTrack(gs->gLevelID),
                                       gs->musicSerial,gs->musicReason}
            : (tr64_game_music_request_t){-1,-1,0,TR64_GAME_MUSIC_DIRECT};
}
void game_init(void)
{
    gs = NULL;
}

static void initBase(ItemObj *self, Room *room)
{
    const Model *m = gs->level.models + self->type;
    self->angle.x = self->angle.z = 0;
    self->vSpeed = self->hSpeed = 0;
    self->nextItem = self->nextActive = NULL;
    self->animIndex = m->animIndex;
    self->frameIndex = tr64_u16(gs->level.anims[self->animIndex].frame_start);
    self->state = (uint8)tr64_u16(gs->level.anims[self->animIndex].state);
    self->nextState = self->goalState = self->state;
    self->extraL = NULL;
    self->extraE = NULL;
    self->health = -0x4000;
    self->hitMask = 0;
    self->visibleMask = UINT32_MAX;
    self->flags &= (ITEM_FLAG_ONCE | ITEM_FLAG_MASK);
    if (self->type != ITEM_BRIDGE_FLAT && self->type != ITEM_BRIDGE_TILT_1 &&
        self->type != ITEM_BRIDGE_TILT_2 && self->type != ITEM_TRAP_FLOOR)
        self->flags |= ITEM_FLAG_COLLISION;
    if (self->flags & ITEM_FLAG_ONCE)
        self->flags = (self->flags & ~ITEM_FLAG_ONCE) | ITEM_FLAG_STATUS_INVISIBLE;
    if (!encounterIsSource(self->type) && (self->flags & ITEM_FLAG_MASK) == ITEM_FLAG_MASK)
    {
        self->flags = (self->flags & ~ITEM_FLAG_MASK) | ITEM_FLAG_REVERSE;
        ItemObj_activateBase(self);
    }
    Room_add(room, self);
}

#if TR64_HAS_TR2
static void updateTR2Items(Lara *lara)
{
    bool haveListener = false;
    int32 listenerX = 0, listenerZ = 0;
    for (ItemObj *item = gs->firstActive; item; ) {
        /* Newly woken items are prepended; a self-removal or boat removal
         * cannot skip the item that was already next. */
        ItemObj *next = item->nextActive;
        gs->tr2->activeNext = next;
        if (TR64_RULE(INTERACTIONS) && item->type == 60 && !haveListener) {
            const Camera *c = &lara->extraL->camera;
            /* Q-INT-06: project the listener 610 units from the
             * previous raw eye and yaw, once for this active pass. */
            vec3i step = game_tr2_heading_step(c->angle.y,610);
            listenerX = i32_add(c->view.pos.x,step.x);
            listenerZ = i32_add(c->view.pos.z,step.z);
            haveListener = true;
        }
        game_tr2_update_item(item,lara,listenerX,listenerZ);
        item = gs->tr2->activeNext;
    }
}
#endif
#line 266 /* Keep assertion IDs in the original shared file. */
static bool joinPoseBounds(Room *room,vec3i p,int32 *floor,int32 *ceiling,int32 *water)
{
    if (!room)return false;
    const RoomInfo *info=room->info;
    int32 sx=(p.x-tr64_i32(info->x))>>10, sz=(p.z-tr64_i32(info->z))>>10;
    if (sx<0 || sz<0 || sx>=tr64_u16(info->x_sectors) || sz>=tr64_u16(info->z_sectors))
        return false;
    const Sector *s=Room_getSector(room,p.x,p.z);
    const FloorData *trigger=gs->gLastFloorData;
    uint16 slant=gs->gLastFloorSlant;
    *floor=Sector_getFloor(s,p.x,p.y,p.z);
    *ceiling=Sector_getCeiling(s,p.x,p.y,p.z);
    const Sector *surface=Room_getWaterSector(room,p.x,p.z);
    *water=!surface ? WALL : surface->roomAbove!=0xff ? surface->ceiling*256 :
        Sector_getCeiling(surface,p.x,p.y,p.z);
    gs->gLastFloorData=trigger;
    gs->gLastFloorSlant=slant;
    return *floor!=WALL && *ceiling!=WALL;
}
static bool joinPoseUsable(const JoinPose *pose)
{
    vec3i p=pose->at.pos;
    Room *room=pose->at.room;
    int32 floor,ceiling,water;
    if (!joinPoseBounds(room,p,&floor,&ceiling,&water))return false;
    bool wet=(tr64_u16(room->info->flags)&ROOM_FLAG_WATER_MASK)!=0;
    if (pose->water==WATER_STATE_UNDER)
        return wet && water!=WALL && p.y>=water &&
            p.y-ceiling>=LARA_HEIGHT_UW/2 && floor-p.y>=LARA_HEIGHT_UW/2 &&
            Room_getRoom(room,p.x,p.y,p.z)==room;
    if (pose->water==WATER_STATE_SURFACE)
        return water!=WALL && p.y==water+1 && ceiling<=p.y &&
            floor-p.y>=LARA_HEIGHT_SURF;
    unsigned ground=water!=WALL && floor-water>LARA_STEP_HEIGHT ? WATER_STATE_WADE : WATER_STATE_ABOVE;
    return pose->water==ground && (ground!=WATER_STATE_WADE || floor-water<=730) &&
        p.y==floor && p.y-ceiling>=LARA_HEIGHT && Room_getRoom(room,p.x,p.y-1,p.z)==room;
}
static bool nearestJoinPose(const Lara *partner,JoinPose *out)
{
    Room *room=partner->room;
    if (!room)return false;
    const RoomInfo *info=room->info;
    bool found=false;
    uint64_t best=UINT64_MAX;
    for (unsigned x=0;x<tr64_u16(info->x_sectors);++x)
        for (unsigned z=0;z<tr64_u16(info->z_sectors);++z) {
            vec3i p={tr64_i32(info->x)+(int32)x*1024+512,partner->pos.y,
                     tr64_i32(info->z)+(int32)z*1024+512};
            int32 floor,ceiling,water;
            if (!joinPoseBounds(room,p,&floor,&ceiling,&water))continue;
            unsigned medium=WATER_STATE_ABOVE;
            if (water!=WALL && floor-water>730) {
                medium=WATER_STATE_UNDER;
                p.y=X_MAX(X_MAX(water,ceiling)+LARA_HEIGHT_UW/2,X_MIN(p.y,floor-LARA_HEIGHT_UW/2));
            } else {
                p.y=floor;
                if (water!=WALL && floor-water>LARA_STEP_HEIGHT)medium=WATER_STATE_WADE;
            }
            int64_t dx=(int64_t)p.x-partner->pos.x,dy=(int64_t)p.y-partner->pos.y,dz=(int64_t)p.z-partner->pos.z;
            uint64_t distance=(uint64_t)(dx*dx)+(uint64_t)(dy*dy)+(uint64_t)(dz*dz);
            if (distance>=best)continue;
            /* A shaft has no standable surface in its top room. Its column
             * still owns the real floor reached through the vertical portal. */
            Room *landing=Room_getRoom(room,p.x,p.y-(medium!=WATER_STATE_UNDER),p.z);
            JoinPose candidate={{landing,p},partner->angle.y,medium};
            if (joinPoseUsable(&candidate)) {
                best=distance;
                *out=candidate;
                found=true;
            }
        }
    return found;
}
static void rememberJoinPose(const Lara *lara)
{
    if (!lara->room || lara->health<=0 || (lara->flags&ITEM_FLAG_GRAVITY))
        return;
    switch (lara->state) {
    case Lara_STATE_STOP: case Lara_STATE_WALK: case Lara_STATE_RUN:
    case Lara_STATE_TURN_LEFT: case Lara_STATE_TURN_RIGHT: case Lara_STATE_TURN_FAST:
    case Lara_STATE_WADE: case Lara_STATE_UW_TREAD: case Lara_STATE_UW_SWIM:
    case Lara_STATE_UW_DIVE: case Lara_STATE_UW_GLIDE:
    case Lara_STATE_SURF_TREAD: case Lara_STATE_SURF_SWIM: case Lara_STATE_SURF_BACK:
    case Lara_STATE_SURF_LEFT: case Lara_STATE_SURF_RIGHT: {
        JoinPose *saved=&gs->joinPose[Lara_slot(lara)];
        vec3i p=lara->pos;
        if (saved->at.room==lara->room && saved->water==lara->waterState &&
            saved->at.pos.x==p.x && saved->at.pos.y==p.y && saved->at.pos.z==p.z) {
            saved->yaw=lara->angle.y;
            break;
        }
        JoinPose pose={{lara->room,p},lara->angle.y,lara->waterState};
        if (joinPoseUsable(&pose))*saved=pose;
        break;
    }
    }
}
static void applyJoinMedium(Lara *lara,const JoinPose *pose)
{
    lara->waterState=pose->water;
    if (pose->water==WATER_STATE_UNDER)
        ItemObj_animSet(lara,Lara_ANIM_UNDERWATER,true,0);
    else if (pose->water==WATER_STATE_SURFACE) {
        ItemObj_animSet(lara,Lara_ANIM_SURF,true,0);
        lara->extraL->swimTimer=11;
    }
    gs->joinPose[Lara_slot(lara)]=*pose;
}

static void initLara(Lara *self)
{
    unsigned slot = Lara_slot(self);
    ExtraInfoLara *e = gs->playersExtra + slot;
    Nav_Cell *cells = e->nav.cells;
    self->extraL = e;
    memset(e, 0, sizeof(*e));
    e->nav.cells = cells;
    e->hitQuadrant = -1;
    gs->gLaraHazardState[slot] = (LaraHazardState){.flameItemIndex = -1};
    e->weapon = e->goalWeapon = WEAPON_PISTOLS;
    Lara_setWeaponState(self, WEAPON_STATE_FREE);
    ItemObj_meshSwap(self, ITEM_LARA, UINT32_MAX);
#if TR64_HAS_TR2
    if (TR64_RULE(FRESH_START)) {
        /* tr2-lara-start.md, Lara before the first gameplay update.
         * The physical floor check belongs to the first update, not setup. */
        if (tr64_u16(self->room->info->flags) & ROOM_FLAG_WATER_MASK)
            sys_error("Tomb Raider II: underwater spawn is outside the dry Lara harness");
        ItemObj_animSet(self,Lara_ANIM_STAND,true,0);
        self->nextState = 0;
        self->flags &= ~ITEM_FLAG_COLLISION;
        self->roomFloor = Room_getSector(self->room,self->pos.x,self->pos.z)->floor * 256;
        self->health = LARA_MAX_HEALTH;
        self->timer = LARA_MAX_OXYGEN;
        gs->tr2->lastWeapon = 1; /* Selection, not a model number. */
        gs->tr2->backWeapon = 3; /* Shotgun back model, not selection 4. */
        gs->tr2->ammo[0] = 1000;
        gs->tr2->ammo[3] = 12;
        gs->tr2->waterDistance = 100; /* Established reset, before the first check. */
        Camera_init(&e->camera,self);
        return;
    }
#endif
    if (gs->gLevelID == LVL_TR1_GYM)
    {
        ItemObj_meshSwap(self, ITEM_LARA_SPEC, 0x3fff);
    }
    else
    {
        /* lara.h meshSwapPistols/meshSwapShotgun: holstered meshes. */
        ItemObj_meshSwap(self, ITEM_LARA, (1u << JOINT_ARM_R3) | (1u << JOINT_ARM_L3));
        ItemObj_meshSwap(self, ITEM_LARA_PISTOLS, (1u << JOINT_LEG_R1) | (1u << JOINT_LEG_L1));
        ItemObj_meshSwap(self, ITEM_LARA_SHOTGUN, 1u << JOINT_TORSO);
    }
    ItemObj_animSet(self, Lara_ANIM_STAND, true, 0);
    self->health = 1000;
    self->timer = 1800;
    self->flags |= ITEM_FLAG_SHADOW;
    Camera_init(&e->camera, self);
    e->healthTimer = 100;
    rememberJoinPose(self);
}
void game_init_item(ItemObj *self, Room *room)
{
    GameItemCall call = {0};
    initBase(self, room);
    if (self->type == ITEM_LARA)
        initLara(self);
#if TR64_HAS_TR2
    /* A second-game item spawned at run time (the flying disc) is set up
     * by its own spawner. Its type number is not a first-game id (61 is
     * the first game's door 5): it must never reach a first-game
     * constructor, on the host or on the cart (tr2-source-layout.md). */
    else if (TR64_SECOND_GAME)
        ;
#endif
    else
    {
        call.op = GAME_ITEM_CONSTRUCT;
        call.self = self;
        game_item_call(&call);
        /* item.h factory fallback constructs the plain base class. */
    }
}
typedef char InventorySlotCountMatches[((int)SLOT_MAX == (int)TR64_GAME_INVENTORY_SLOTS) ? 1 : -1];
enum {
    CARRY_COUNTS_BYTES=TR64_GAME_INVENTORY_SLOTS*sizeof(int32),
    CARRY_INVENTORY_BYTES=sizeof(GameInventory)-CARRY_COUNTS_BYTES
};
typedef char CarryGlobalsMatch[
    CARRY_INVENTORY_BYTES+sizeof(SaveGame)+sizeof(GameSettings)==TR64_GAME_CARRY_GLOBAL_BYTES ? 1 : -1];
typedef char CarryPointerWidth[sizeof(Lara*)==sizeof(int32) ? 1 : -1];
int game_inventory_capture(tr64_game_inventory_carry_t *out)
{
#if TR64_HAS_TR2
    if (gs && gs->tr2)
        return game_tr2_capture_carry(out);
#endif
    if (!gs || !out || !inventory_export_counts(out->counts))
        return 0;
    out->from_level_id = gs->gLevelID;
    out->camera_target[0] = gs->playersExtra[0].camera.target.pos.x;
    out->camera_target[1] = gs->playersExtra[0].camera.target.pos.y;
    out->camera_target[2] = gs->playersExtra[0].camera.target.pos.z;
    out->tick=gs->tick;
    out->transition=gs->stop.transition;
    memcpy(out->source_globals,(const uint8*)&gs->inventory+CARRY_COUNTS_BYTES,
           CARRY_INVENTORY_BYTES);
    const Lara *owner=gs->inventory.lara;
    if (owner && owner==gs->players[1]) {
        const int32 useSlot=SLOT_MAX;
        memcpy(out->source_globals+offsetof(GameInventory,useSlot)-CARRY_COUNTS_BYTES,
               &useSlot,sizeof(useSlot));
        owner=gs->players[0];
    }
    int32 actor=owner ? (int32)(owner-gs->items) : -1;
    memcpy(out->source_globals+offsetof(GameInventory,lara)-CARRY_COUNTS_BYTES,&actor,sizeof(actor));
    memcpy(out->source_globals+CARRY_INVENTORY_BYTES,&gs->gSaveGame,sizeof(SaveGame));
    memcpy(out->source_globals+CARRY_INVENTORY_BYTES+sizeof(SaveGame),
           &gs->gSettings,sizeof(GameSettings));
    return 1;
}
static void newInventory(void)
{
    /* inventory.h:210-211: initNewGame returns at once for a level the economy
     * does not admit (the cutscene levels), leaving the zeroed inventory. */
    if(game_is_cutscene_level(gs->gLevelID))return;
    memset(gs->inventory.counts,0,sizeof(gs->inventory.counts));
    gs->inventory.numKeys=0;
    gs->inventory.useSlot=SLOT_MAX;
    gs->inventory.counts[SLOT_PASSPORT]=gs->inventory.counts[SLOT_DETAIL]=1;
    gs->inventory.counts[SLOT_SOUND]=gs->inventory.counts[SLOT_CONTROLS]=1;
    if(gs->gLevelID==LVL_TR1_TITLE) {
        gs->inventory.counts[SLOT_HOME]=1;
        return;
    }
    gs->inventory.counts[SLOT_COMPASS]=1;
    if(gs->gLevelID!=LVL_TR1_GYM && gs->gLevelID!=LVL_TR1_10A) {
        gs->inventory.counts[SLOT_PISTOLS]=1;
        gs->inventory.counts[SLOT_AMMO_PISTOLS]=-1;
    }
}
typedef struct {
    GameInventory inventory;
    GameSettings settings;
    SaveGame header;
    uint32 tick;
    uint32 musicSerial;
    uint32 coreIdentity, textureIdentity;
    int32 pendingLevel;
    CollisionInfo cinfo;
} GamePreparation;
/* Resident, not in the paged `save` unit: levelStart calls it before any unit is loaded (a call into `save` from here
 * is an unloaded-code exception on the cart, which no host build can show). Level start only; every save and load
 * reuses gs->coreIdentity and gs->textureIdentity. */
void game_save_identities(const tr64_level_t *core,uint32 *tables,uint32 *textures)
{
    uint32 crc = 0, i;
    for (i = 1; i <= TR64_CORE_TABLES; i++)
    {
        const tr64_level_table_t *t = tr64_level_table(core, i);
        crc = tr64_crc32(crc, t->base, t->bytes);
    }
    *tables = crc;
    *textures = tr64_crc32(0, core->textures, core->texture_bytes);
}
static void levelStart(const tr64_level_t *core,const char *name,
                       const tr64_game_inventory_carry_t *carry,const GamePreparation *prepare
#ifdef TR64_HOST
                       , bool tr2
#endif
                       )
{
#if TR64_HAS_TR2 && !defined(TR64_HOST)
    const bool tr2 = true;
#endif
    uint32 roomsCount, sectorsCount, boxesCount, camerasCount, bytes, i;
    uint8 *p;
    Sector *sectors;
    if (!core || !core->ready || !name)
        sys_error("game level not ready");
#if TR64_HAS_TR2
    if (tr2 ? core->game != 2 : core->game == 2)
        sys_error("game %s: Tomb Raider II play requires TR2 level data", name);
#else
    if (core->game==2)
        sys_error("game %s: Tomb Raider II gameplay is unsupported",name);
#endif
#if TR64_HAS_TR2
    if (tr2)
        tr64_level_validate_tr2_gameplay(core);
#endif
    if (gs && !prepare)
        sys_error("game level already started");
    roomsCount = tr64_level_table(core, TR64_CORE_rooms)->count;
#if TR64_HAS_TR2
    if (tr2)
        game_tr2_admit_counts(name, roomsCount,
                              tr64_level_table(core, TR64_CORE_items)->count);

#endif
    sectorsCount = tr64_level_table(core, TR64_CORE_sectors)->count;
    boxesCount = tr64_level_table(core, TR64_CORE_boxes)->count;
    camerasCount = tr64_level_table(core, TR64_CORE_cameras)->count;
#if TR64_HAS_TR2
    const unsigned modelSlots = tr2 ? TR2_PUBLIC_TYPES : ITEM_MAX;
    const unsigned navSlots = tr2 ? TR2_CREATURE_SLOTS : 8 + TR64_GAME_PLAYERS;
#else
    enum { modelSlots = ITEM_MAX, navSlots = 8 + TR64_GAME_PLAYERS };
#endif
    bytes = align4(sizeof(GameState)) + align4(roomsCount * sizeof(Room)) +
            align4(sectorsCount * sizeof(Sector)) + align4(256 * sizeof(ItemObj)) +
            align4(modelSlots * sizeof(Model)) +
            align4(navSlots * boxesCount * sizeof(Nav_Cell)) +
            align4(camerasCount * 2) + align4(boxesCount * 2);
#if TR64_HAS_TR2
    if (tr2)
        bytes += align4(sizeof(GameTR2));
#endif
    if(prepare && (!gs || gs->core!=core || gs->bytes!=bytes))
        sys_error("game save preparation changed Core/state layout");
    p = prepare ? (uint8 *)gs : eng_game_state_alloc(bytes);
    memset(p, 0, bytes);
    gs = part(&p, sizeof(GameState));
    gs->core = core;
    gs->bytes = bytes;
    gs->stop.next_level_id = -1;
    gs->pendingLevel = prepare ? prepare->pendingLevel : LVL_MAX;
    if(prepare) {
        gs->inventory=prepare->inventory;
        gs->gSettings=prepare->settings;
        gs->gSaveGame=prepare->header;
        gs->tick=prepare->tick;
        gs->musicSerial=prepare->musicSerial;
        gs->coreIdentity=prepare->coreIdentity;
        gs->textureIdentity=prepare->textureIdentity;
        /* The source's collision record is a global that startLevel never resets: what the last tick left in it
         * (`offset`, applied to Lara's position at the start of the next Lara update) outlives the load. Every
         * other global the source keeps across startLevel is written before it is read. */
        gs->cinfo=prepare->cinfo;
    } else if(!TR64_SECOND_GAME && carry) {
        gs->tick=carry->tick;
        memcpy(&gs->gSaveGame,carry->source_globals+CARRY_INVENTORY_BYTES,sizeof(SaveGame));
        memcpy(&gs->gSettings,carry->source_globals+CARRY_INVENTORY_BYTES+sizeof(SaveGame),
               sizeof(GameSettings));
    } else {
        gs->gSettings=(GameSettings){.video_fog=8,.video_fps=1,
            .audio_sfx=1,.audio_music=1,.controls_vibration=1,
            .controls_abilities=core->game==2 ? TR64_LARA_ABILITIES_TR2 : TR64_LARA_ABILITIES_TR1};
    }
    if(!prepare)
        game_save_identities(core,&gs->coreIdentity,&gs->textureIdentity);
    gs->rooms = part(&p, roomsCount * sizeof(Room));
    sectors = part(&p, sectorsCount * sizeof(Sector));
    gs->sectorState = sectors;
    gs->items = part(&p, 256 * sizeof(ItemObj));
    gs->level.models = part(&p, modelSlots * sizeof(Model));
#if TR64_HAS_TR2
    if (!tr2) {
#endif
    gs->playersExtra[0].nav.cells = part(&p, boxesCount * sizeof(Nav_Cell));
    for (i = 0; i < 8; ++i)
        gs->enemiesExtra[i].nav.cells = part(&p, boxesCount * sizeof(Nav_Cell));
#if TR64_HAS_TR2
    }
#endif
    gs->cameraFlags = part(&p, camerasCount * 2);
    gs->boxFlags = part(&p, boxesCount * 2);
#if TR64_HAS_TR2
    if (!tr2)
#endif
    gs->playersExtra[1].nav.cells = part(&p, boxesCount * sizeof(Nav_Cell));
#if TR64_HAS_TR2
    if (tr2) {
        gs->tr2 = part(&p, sizeof(GameTR2));
#ifdef TR64_HOST
        gs->tr2Rules = TR2_RULES_WALL;
#endif
        game_tr2_level_script_select(name);
        typedef char RouteCellFits[sizeof(TR2RouteNode)==sizeof(Nav_Cell)?1:-1];
        (void)sizeof(RouteCellFits);
        for (unsigned slot=0;slot<TR2_CREATURE_SLOTS;++slot)
            gs->tr2->creatures[slot].route=part(&p,boxesCount*sizeof(TR2RouteNode));
        /* Arms admission needs the Core animation counts initialized below. */
        gs->tr2->animCount = tr64_level_table(core,TR64_CORE_animations)->count;
    }
#endif
    gs->level.roomsCount = roomsCount;
    gs->level.boxesCount = boxesCount;
    gs->level.camerasCount = camerasCount;
    gs->level.itemsCount = tr64_level_table(core, TR64_CORE_items)->count;
    gs->level.framesCount = tr64_level_table(core, TR64_CORE_frames)->count;
    if (gs->level.itemsCount > 256)
        sys_error("game item capacity");
    gs->level.anims = table(TR64_CORE_animations);
    gs->level.animStates = table(TR64_CORE_states);
    gs->level.animRanges = table(TR64_CORE_ranges);
    gs->level.animCommands = table(TR64_CORE_commands);
    gs->level.animFrames = table(TR64_CORE_frames);
    gs->level.floors = table(TR64_CORE_floor_data);
    gs->level.nodes = table(TR64_CORE_bones);
    gs->level.cameras = table(TR64_CORE_cameras);
    gs->level.soundsInfo = table(TR64_CORE_sound_details);
    gs->level.soundMap = table(TR64_CORE_sound_map);
    gs->level.boxes = table(TR64_CORE_boxes);
    gs->level.overlaps = table(TR64_CORE_overlaps);
    gs->level.zones = table(TR64_CORE_zones);
    game_lara_abilities_init();
    gs->flow_camera[0] = gs->flow_camera[1] = -1;
    gs->effect.id = -1;
#if TR64_HAS_TR2
    if (tr2) {
        /* Trace spec, Headless start and sampling: harness seed, not a game rule. */
        gs->gLevelID = game_tr2_level_script_id();
        gs->gRandSeedLogic = gs->gRandSeedDraw = 3547461959u;
        int32 amb = game_tr2_level_script_ambient_track(gs->gLevelID);
        if (amb >= 0) game_sndPlayTrack(amb);
        else gs->gCurTrack = -1;
    } else {
#endif
    {
        for (i = 0; i < sizeof(campaign) / sizeof(campaign[0]); ++i)
            if (!strcmp(name, campaign[i].name))
                break;
        if (i == sizeof(campaign) / sizeof(campaign[0]))
            sys_error("game level %s outside TR1 campaign", name);
        gs->gLevelID = campaign[i].id;
        game_sndPlayTrack(campaign[i].track);
    }
    gs->gRandSeedLogic = 3;
    gs->gRandSeedDraw = 7;
#if TR64_HAS_TR2
    }
#endif
    gs->gMatrixPtr = gs->gMatrixStack;
    matrixSetIdentity_c();
    for (i = 0; i < sectorsCount; ++i)
    {
        const tr64_core_sectors_t *s = tr64_level_sectors(core, i);
        sectors[i] = (Sector){tr64_u16(s->floor_index),
                              tr64_u16(s->box_index),
                              s->room_below,
                              s->floor,
                              s->room_above,
                              s->ceiling};
    }
    for (i = 0; i < roomsCount; ++i)
    {
        Room *r = gs->rooms + i;
        r->info = tr64_level_rooms(core, i);
        r->source = i;
        r->sectors = sectors + tr64_u32(r->info->sector_start);
    }
#if TR64_HAS_TR2
    if (tr2) {
        code_loading_begin();
        if (gs->gLevelID != TR2_LVL_TITLE) {
            code_load("tr2_validate");
            if (TR64_RULE(WATER))
                (void)code_call("tr2_validate", TR2_VALIDATE_WATER_LINKS);
        }
    }
#endif
    for (i = 0; i < tr64_level_table(core, TR64_CORE_models)->count; ++i)
    {
        const tr64_core_models_t *m = tr64_level_models(core, i);
        uint32 type = tr64_u32(m->type);
#if TR64_HAS_TR2
        if (tr2 && tr64_u16(m->mesh_count) > INT8_MAX)
            continue;
#endif
        Model *out = gs->level.models + type;
        out->type = type;
        out->count = tr64_u16(m->mesh_count);
        out->start = tr64_u16(m->mesh_start);
        out->nodeIndex = tr64_u32(m->bone_offset) / 4;
        out->animIndex = tr64_u16(m->animation);
    }
    for (i = 0; i < tr64_level_table(core, TR64_CORE_sprite_sequences)->count; ++i)
    {
        const tr64_core_sprite_sequences_t *s = tr64_level_sprite_sequences(core, i);
        /* level.h:243: extra source sprite sequences are not gameplay models. */
        if (tr64_u32(s->type) >= (unsigned)modelSlots)
            continue;
#if TR64_HAS_TR2
        if (tr2 && tr64_i16(s->count) < INT8_MIN)
            sys_error("Tomb Raider II: gameplay sprite sequence exceeds model count");
#endif
        Model *m = gs->level.models + tr64_u32(s->type);
        m->type = tr64_u32(s->type);
        m->count = tr64_i16(s->count);
        m->start = tr64_u16(s->first);
    }
    for (i = 0; i < camerasCount; ++i)
        gs->cameraFlags[i] = tr64_u16(gs->level.cameras[i].flags);
    for (i = 0; i < boxesCount; ++i)
        gs->boxFlags[i] = tr64_u16(tr64_level_boxes(core, i)->overlap);
    for (i = 0; i < 256; ++i)
        ItemObj_resetSlot(gs->items + i);
    for (i = gs->level.itemsCount; i < 256; ++i)
        gs->items[i].nextItem = i + 1 < 256 ? gs->items + i + 1 : NULL;
    gs->firstFree = gs->level.itemsCount < 256 ? gs->items + gs->level.itemsCount : NULL;
    if(!TR64_SECOND_GAME && carry) {
        int32 actor;
        memcpy((uint8*)&gs->inventory+CARRY_COUNTS_BYTES,carry->source_globals,
               CARRY_INVENTORY_BYTES);
        memcpy(&actor,carry->source_globals+offsetof(GameInventory,lara)-CARRY_COUNTS_BYTES,
               sizeof(actor));
        if(actor< -1 || actor>=MAX_ITEMS)sys_error("game inventory carry actor");
        gs->inventory.lara=actor<0 ? NULL : gs->items+actor;
        if(carry->transition==TR64_GAME_TRANSITION_LOAD) {
            if(!inventory_import_counts(carry->counts))sys_error("game invalid inventory carry");
        } else if(!inventory_apply_level_transition(carry->from_level_id,gs->gLevelID,carry->counts))
            sys_error("game invalid inventory carry");
        if(carry->transition!=TR64_GAME_TRANSITION_LOAD &&
           carry->from_level_id==LVL_TR1_TITLE && gs->gLevelID!=LVL_TR1_TITLE) {
            memset(&gs->gSaveGame,0,sizeof(gs->gSaveGame));
            newInventory();
        }
    } else if(!prepare) {
        newInventory();
    }
    if(!prepare)gs->inventory.page=INV_PAGE_MAIN;
#if TR64_HAS_TR2
    uint32_t wakeMask[256 / 32] = {0};
    if (TR64_RULE(CREATURES) && gs->gLevelID != TR2_LVL_TITLE)
        (void)code_call("tr2_validate", TR2_VALIDATE_CREATURE_ASSETS);
#endif
    if(gs->gLevelID!=LVL_TR1_TITLE
#if TR64_HAS_TR2
       && gs->gLevelID!=TR2_LVL_TITLE
#endif
       )
    for (i = 0; i < gs->level.itemsCount; ++i)
    {
        const tr64_core_items_t *s = tr64_level_items(core, i);
#if TR64_HAS_TR2
        if (tr2 && tr64_u16(s->type) != ITEM_LARA) {
            unsigned type = tr64_u16(s->type);
            if (type >= TR2_PUBLIC_TYPES)
                sys_error("Tomb Raider II: placement type %u outside PC object domain", type);
            if (TR64_RULE(WORLD_ITEMS) || TR64_RULE(AUTOMATIC_GAZE)) {
                if (code_call("tr2_validate", TR2_VALIDATE_INIT_PLACEMENT | (i << 8)))
                    wakeMask[i >> 5] |= 1u << (i & 31);
            } else
                ++gs->tr2->inertByType[type];
            continue;
        }
#endif
        ItemObj *it = gs->items + i;
        Room *r = gs->rooms + tr64_u16(s->room);
        uint16 flags = tr64_u16(s->flags) | (tr64_level_quadrant(s->angle) << 14);
        int16 intensity = tr64_i16(s->intensity);
        it->type = tr64_u16(s->type);
        it->intensity = intensity < 0 ? 0 : intensity >> 5;
        it->pos =
            vec3i_create((int16)(tr64_i32(s->x) - tr64_i32(r->info->x)) + tr64_i32(r->info->x),
                         (int16)tr64_i32(s->y),
                         (int16)(tr64_i32(s->z) - tr64_i32(r->info->z)) + tr64_i32(r->info->z));
        it->angle.y = ((flags >> 14) - 2) * 16384;
        it->flags = flags;
#if TR64_HAS_TR2
        if (tr2) {
            if (gs->players[0])
                sys_error("Tomb Raider II: multiple Lara placements");
            /* Source placement controls all 32-bit axes and the raw yaw;
             * the TR1 options embedded in quadrant bits are not a TR2 ABI. */
            it->pos = vec3i_create(tr64_i32(s->x),tr64_i32(s->y),tr64_i32(s->z));
            it->angle.y = tr64_i16(s->angle);
            it->flags = 0; /* Lara is not an active world item, even at setup. */
            gs->tr2->laraFlags = TR64_RULE(WORLD_ITEMS) ? game_tr2_placement_flags(s, i) : tr64_u16(s->flags);
        }
#endif
        if (it->type == ITEM_LARA)
            gs->players[0] = it;
        game_init_item(it, r);
    }
#if TR64_HAS_TR2
    if (tr2) {
        if (gs->gLevelID == TR2_LVL_TITLE) {
            gs->items[0].extraL = gs->playersExtra;
            gs->playersExtra[0].camera.mode = CAMERA_MODE_FOLLOW;
            gs->playersExtra[0].camera.view.room = gs->level.roomsCount ? gs->rooms : NULL;
            if (carry)
                game_tr2_apply_carry(carry);
            code_loading_end();
            return;
        }
        /* The fixture resets gameplay RNG after the authored placements. */
        gs->gRandSeedLogic=3547461959u;
        if (TR64_RULE(ROOM_EFFECTS))
            game_tr2_effects_init();
        if (!gs->players[0])
            sys_error("game %s: Tomb Raider II Lara absent",name);
        if (TR64_RULE(SAVED_STATS)) {
            const vec3i *spawn = &gs->players[0]->pos;
            gs->tr2->distanceFrom[0] = spawn->x;
            gs->tr2->distanceFrom[1] = spawn->y;
            gs->tr2->distanceFrom[2] = spawn->z;
        }
        if (TR64_RULE(WORLD_ITEMS))
            game_tr2_validate_world_animations();
        if (TR64_RULE(ARMS))
            (void)code_call("tr2_validate", TR2_VALIDATE_ARMS);
        if (TR64_RULE(INTERACTIONS))
            (void)code_call("tr2_validate", TR2_VALIDATE_INTERACTIONS);
        (void)code_call("tr2_validate", TR2_VALIDATE_EXTRA_OBJECTS);
        /* Release tr2_validate before loading gameplay units so peak
         * code-unit frame residency stays well inside the 96 KiB pool. */
        code_unload("tr2_validate");
        game_tr2_load_wall_units(core);
        if (!prepare) {
            for (i = 0; i < gs->level.itemsCount; ++i)
                if (wakeMask[i >> 5] & (1u << (i & 31)))
                    game_tr2_activate_item(gs->items + i);
            if (TR64_RULE(WORLD_ITEMS))
                game_initial_world();
        }
        gs->tr2->vehicle = -1;
        gs->tr2->mineChain = 0;
        gs->tr2->extraAnim = false;
        gs->tr2->refusalValid = false;
        gs->tr2->pendingTrack = -1;
        gs->tr2->flipEffect = -1;
        gs->tr2->flipEffectTimer = 0;
        if (gs->tr2->boatUnit) {
            TR2BoatCall bcall = {.op = TR2_BOAT_INIT};
            (void)code_call("tr2_boat", (uint32_t)(uintptr_t)&bcall);
        }
        if (gs->tr2->operaUnit) {
            TR2OperaCall ocall = {.op = TR2_OPERA_INIT};
            (void)code_call("tr2_opera", (uint32_t)(uintptr_t)&ocall);
        }
        if (TR64_RULE(FRESH_STOCK)) {
            /* tr2-items-general: built-in new-game stock, not the
             * scripted three-dragon reward. */
            static const TR2RingEntry stock[TR2_CARRIED_START_COUNT] = {
                {121, 1}, {157, 1}, {158, 1}, {173, 2}, {172, 1}, {171, 1}
            };
            memcpy(gs->tr2->carried, stock, sizeof(stock));
            gs->tr2->carriedCount = TR2_CARRIED_START_COUNT;
            game_tr2_level_script_apply_stock();
            if (carry) {
                if (carry->transition == TR64_GAME_TRANSITION_TITLE)
                    gs->tr2->startMenu = true;
                game_tr2_apply_carry(carry);
            }
        }
        /* §9: world duplicate-weapon replacement runs before cargo binding. */
        if (!prepare)
            game_tr2_urban_cargo_init();
        code_loading_end();
        if (TR64_RULE(CAMERA_VIEW))
            Camera_update(&gs->playersExtra[0].camera); /* First eye over the live world. */
        return;
    }
#endif
    if(gs->gLevelID==LVL_TR1_TITLE) {
        gs->items[0].extraL=gs->playersExtra;
        gs->playersExtra[0].camera.mode=CAMERA_MODE_FOLLOW;
        if(!prepare) {
            game_load_item_units();
            game_inventory_open(gs->items,INV_PAGE_TITLE,0);
        }
    } else {
        if (!gs->players[0] && !game_is_cutscene_level(gs->gLevelID))
            sys_error("game Lara absent");
        if (!encounterPrecreateChildren())
            sys_error("game encounter child construction");
        if(!prepare) {
            if(!game_save_rebuild_world())sys_error("game initial derived world reconstruction");
            game_load_item_units();
        }
        if (game_is_cutscene_level(gs->gLevelID))
        {
            Camera *camera = &gs->playersExtra[0].camera;
            if (carry)
            {
                /* initCinematic does not set the sound listener (target.pos), which
                 * game_sound reads for attenuation: the source keeps the previous level's. */
                camera->target.pos = vec3i_create(carry->camera_target[0], carry->camera_target[1],
                                                  carry->camera_target[2]);
            }
            Camera_initCinematic(camera);
        }
    }
}
void game_level_start(const tr64_level_t *core, const char *name)
{
    levelStart(core,name,NULL,NULL
#ifdef TR64_HOST
               , false
#endif
               );
}
#if TR64_HAS_TR2
void game_tr2_start(const tr64_level_t *core, const char *name, unsigned schema)
{
    if (schema != 0 && schema != 20 && schema != 21 && schema != 22)
        sys_error("Tomb Raider II: unsupported trace schema %u",schema);
    if (schema && (schema == 22) != (name != NULL && strcmp(name, "boat") == 0))
        sys_error("Tomb Raider II: trace schema %u does not match level %s",
                  schema, name ? name : "(null)");
#ifdef TR64_HOST
    levelStart(core,name,NULL,NULL,true);
#else
    levelStart(core,name,NULL,NULL);
#endif
    gs->tr2->traceSchema=schema;
#if defined(TR64_HOST) || !TR64_PLAY || TR64_TEST
    if (schema == 22)
        game_tr2_validate_schema22();
#endif
}

bool game_tr2_place(const int32_t p[5])
{
    /* tr2-oracle-trace.md: direct live sector, not Room_getRoom or the
     * horizontal border clamp. This does not touch the fresh camera. */
    if (gs->tick || p[3]<INT16_MIN || p[3]>INT16_MAX ||
        p[4]<0 || (uint32)p[4]>=gs->level.roomsCount)
        return false;
    Lara *lara=gs->players[0];
    Room *room=gs->rooms+p[4];
    int64_t x=(int64_t)p[0]-tr64_i32(room->info->x);
    int64_t z=(int64_t)p[2]-tr64_i32(room->info->z);
    int32 nx=tr64_u16(room->info->x_sectors),nz=tr64_u16(room->info->z_sectors);
    if ((tr64_u16(room->info->flags)&ROOM_FLAG_WATER_MASK) ||
        x<0 || x>=nx*1024 || z<0 || z>=nz*1024)
        return false;
    const Sector *sector=room->sectors+(x>>10)*nz+(z>>10);
    if (sector->floor==-127)
        return false;
    if (room!=lara->room) {
        Room_remove(lara->room,lara);
        Room_add(room,lara);
    }
    lara->pos=vec3i_create(p[0],p[1],p[2]);
    lara->angle=vec3s_create(0,p[3],0);
    lara->roomFloor=sector->floor*256;
    return true;
}
#endif
void game_level_start_transition(const tr64_level_t *core, const char *name,
                                const tr64_game_inventory_carry_t *carry)
{
    if (!carry)
        sys_error("game inventory carry missing");
    levelStart(core,name,carry,NULL
#ifdef TR64_HOST
               , core && core->game == 2
#endif
               );
}
#if TR64_HAS_TR2
void game_prepare_tr2_load(void)
{
    const tr64_level_t *core = gs->core;
    const char *name = game_tr2_level_name(gs->gLevelID);
    void (*chooseKey)(void *) = gs->tr2->chooseKey;
    void *chooseKeyContext = gs->tr2->chooseKeyContext;
    uint8_t traceSchema = gs->tr2->traceSchema;
    GamePreparation previous = {0};
    previous.inventory = gs->inventory;
    previous.settings = gs->gSettings;
    previous.tick = gs->tick;
    previous.musicSerial = gs->musicSerial;
    previous.coreIdentity = gs->coreIdentity;
    previous.textureIdentity = gs->textureIdentity;
    previous.pendingLevel = LVL_MAX;

    code_loading_begin();
    if (code_is_loaded("tr2_wall")) code_unload("tr2_wall");
    if (gs->tr2->aiUnit && code_is_loaded("tr2_ai")) code_unload("tr2_ai");
    if (gs->tr2->creatureUnit && code_is_loaded("tr2_creatures")) code_unload("tr2_creatures");
    if (gs->tr2->boatUnit && code_is_loaded("tr2_boat")) code_unload("tr2_boat");
    if (gs->tr2->urbanUnit && code_is_loaded("tr2_urban")) code_unload("tr2_urban");
    if (gs->tr2->operaUnit && code_is_loaded("tr2_opera")) code_unload("tr2_opera");
    code_loading_end();

    levelStart(core, name ? name : "generic", NULL, &previous
#ifdef TR64_HOST
               , true
#endif
               );
    gs->tr2->chooseKey = chooseKey;
    gs->tr2->chooseKeyContext = chooseKeyContext;
    gs->tr2->traceSchema = traceSchema;
}

void game_tr2_post_restore(void)
{
    GameTR2 *tr2 = gs->tr2;
    /* Reset sectors (respecting flipped room source) and boxFlags, then re-apply
     * resting pushblocks, closed doors, and intact breakable windows cleanly. */
    for (uint32 i = 0; i < gs->level.roomsCount; ++i) {
        Room *r = gs->rooms + i;
        const RoomInfo *srcInfo = tr64_level_rooms(gs->core, r->source);
        uint32 count = tr64_u16(srcInfo->x_sectors) * tr64_u16(srcInfo->z_sectors);
        uint32 start = tr64_u32(srcInfo->sector_start);
        for (uint32 j = 0; j < count; ++j) {
            const tr64_core_sectors_t *s = tr64_level_sectors(gs->core, start + j);
            r->sectors[j] = (Sector){tr64_u16(s->floor_index), tr64_u16(s->box_index),
                                     s->room_below, s->floor, s->room_above, s->ceiling};
        }
    }
    for (uint32 i = 0; i < gs->level.boxesCount; ++i)
        gs->boxFlags[i] = tr64_u16(tr64_level_boxes(gs->core, i)->overlap);
    if (TR64_RULE(WORLD_ITEMS))
        game_initial_world();

    /* Reactivate active creatures via tr2_ai and restore their 5 AI fields. */
    for (unsigned slot = 0; slot < TR2_CREATURE_SLOTS; ++slot)
        tr2->creatures[slot].item = NULL;
    if (tr2->aiUnit) {
        for (unsigned i = 0; i < tr2->savePost.creatureCount; ++i) {
            ItemObj *item = gs->items + tr2->savePost.creatures[i].itemIndex;
            if (!item->room)
                continue;
            TR2AiCall aiCall = {.op = TR2_AI_ACTIVATE, .self = item};
            if (code_call("tr2_ai", (uint32_t)(uintptr_t)&aiCall)) {
                for (unsigned slot = 0; slot < TR2_CREATURE_SLOTS; ++slot) {
                    if (tr2->creatures[slot].item == item) {
                        tr2->creatures[slot].jointYaw[0] = tr2->savePost.creatures[i].jointYaw[0];
                        tr2->creatures[slot].jointYaw[1] = tr2->savePost.creatures[i].jointYaw[1];
                        tr2->creatures[slot].maxTurn = tr2->savePost.creatures[i].maxTurn;
                        tr2->creatures[slot].flags = tr2->savePost.creatures[i].flags;
                        tr2->creatures[slot].mood = tr2->savePost.creatures[i].mood;
                        break;
                    }
                }
            }
        }
    }

    Lara *lara = gs->players[0];
    if (lara && lara->extraL) {
        Camera_init(&lara->extraL->camera, lara);
        if (tr2->extraAnim)
            lara->extraL->camera.mode = CAMERA_MODE_FIXED;
        else if (TR64_RULE(CAMERA_VIEW))
            Camera_update(&lara->extraL->camera);
        if (tr2->savePost.burning)
            game_tr2_attach_lara_flame(lara);
    }
    memset(&gs->stop, 0, sizeof(gs->stop));
    gs->stop.next_level_id = -1;
    ++gs->musicSerial;
    gs->musicReason = TR64_GAME_MUSIC_RESTORE;
    gs->inventory.page = INV_PAGE_MAIN;
    gs->inventory.state = INV_STATE_NONE;
}
#endif
void game_prepare_load(void)
{
    const tr64_level_t *core=gs->core;
    const char *name=game_level_name(gs->gLevelID);
    GamePreparation previous;
    previous.inventory=gs->inventory;
    if (previous.inventory.lara && previous.inventory.lara==gs->players[1]) {
        previous.inventory.lara=gs->players[0];
        previous.inventory.useSlot=SLOT_MAX;
    }
    previous.settings=gs->gSettings;
    previous.header=gs->gSaveGame;
    previous.tick=gs->tick;
    previous.musicSerial=gs->musicSerial;
    previous.coreIdentity=gs->coreIdentity;
    previous.textureIdentity=gs->textureIdentity;
    previous.pendingLevel=gs->stop.kind==TR64_GAME_STOP_NONE ? gs->pendingLevel : LVL_MAX;
    previous.cinfo=gs->cinfo;
    levelStart(core,name,NULL,&previous
#ifdef TR64_HOST
               , false
#endif
               );
}
void Lara_hit(Lara *self, int32 damage, const vec3i *point, int32 sound)
{
    (void)point;
    (void)sound;
    if (self->health <= 0 || damage <= 0)
        return;
    self->extraL->healthTimer = 40;
    self->health = self->health > damage ? self->health - damage : 0;
}
ItemObj *ItemObj_add(ItemType type, Room *room, const vec3i *pos, int32 angleY)
{
    ItemObj *self = gs->firstFree;
    if (!self)
        return NULL;
    gs->firstFree = self->nextItem;
    ItemObj_resetSlot(self);
    self->type = type;
    self->pos = *pos;
    self->angle.y = angleY;
    self->intensity = 128;
    game_init_item(self, room);
    return self;
}
uint32 game_player_mask(void)
{
    return gs ? (gs->players[0] != NULL) | ((gs->players[1] != NULL) << 1) : 0;
}
uint32 game_player_alive_mask(void)
{
    if (!gs)
        return 0;
    uint32 mask = 0;
    for (unsigned slot = 0; slot < TR64_GAME_PLAYERS; ++slot)
        if (gs->players[slot] && gs->players[slot]->health > 0)
            mask |= 1u << slot;
    return mask;
}
static bool playerCommandsAllowed(void)
{
    return gs && gs->players[0] && !game_is_cutscene_level(gs->gLevelID) &&
        gs->stop.kind==TR64_GAME_STOP_NONE;
}
static bool getJoinPose(const Lara *partner,JoinPose *pose)
{
    *pose=gs->joinPose[Lara_slot(partner)];
    if (joinPoseUsable(pose))return true;
    rememberJoinPose(partner);
    *pose=gs->joinPose[Lara_slot(partner)];
    if (joinPoseUsable(pose))return true;
    return nearestJoinPose(partner,pose);
}
int game_player_join(void)
{
    if (!playerCommandsAllowed() || gs->players[1] || !gs->firstFree ||
        gs->inventory.state!=INV_STATE_NONE)
        return -1;
    JoinPose pose;
    if (!getJoinPose(gs->players[0],&pose))return -1;
    Lara *lara=gs->firstFree;
    gs->players[1]=lara;
    ItemObj *next=lara->nextItem;
    ItemObj_resetSlot(lara);
    lara->nextItem=next;
    ItemObj_add(ITEM_LARA,pose.at.room,&pose.at.pos,pose.yaw);
    applyJoinMedium(lara,&pose);
    for (unsigned i=0;i<8;++i)
        if (gs->enemiesExtra[i].enemy) {
            gs->enemiesExtra[i].contact[0]=gs->enemiesExtra[i].enemy->hitMask;
            gs->enemiesExtra[i].contact[1]=0;
        }
    return 0;
}
void game_refresh_join_pose(Lara *actor)
{
    if (!actor)return;
    gs->joinPose[Lara_slot(actor)]=(JoinPose){0};
    rememberJoinPose(actor);
}
static void discardPlayerInteraction(Lara *lara)
{
    gs->interaction[Lara_slot(lara)]=NULL;
    gs->flow_camera[Lara_slot(lara)]=-1;
    gs->flow_active[Lara_slot(lara)]=false;
    if (gs->inventory.lara==lara)
        gs->inventory.useSlot=SLOT_MAX;
    if (gs->inventoryOpenLara==lara) {
        gs->inventoryOpenLara=NULL;
        gs->inventoryOpenPending=false;
    }
}
int game_player_leave(void)
{
    if (!gs || !gs->players[1])
        return TR64_GAME_LEAVE_REFUSED;
    Lara *lara=gs->players[1];
    if (game_interaction_busy(lara))return TR64_GAME_LEAVE_BUSY;
    if (gs->players[0]->health<=0)game_retire_interaction(gs->players[0]);
    discardPlayerInteraction(lara);
    hazardClearLaraBurn(lara);
    if (gs->inventory.lara==lara)
        gs->inventory.lara=gs->players[0];
    if (gs->effect.lara==lara)
        gs->effect.lara=NULL;
    if (gs->tinfo.target==lara)
        gs->tinfo.target=gs->players[0];
    for (unsigned i=0;i<8;++i) {
        ExtraInfoEnemy *e=&gs->enemiesExtra[i];
        if (!e->enemy)continue;
        if (e->target==lara)e->target=NULL;
        e->enemy->hitMask=e->contact[0];
        e->contact[0]=e->contact[1]=0;
    }
    Camera *camera=&gs->playersExtra[0].camera;
    if (camera->lookAtItem==lara)camera->lookAtItem=NULL;
    if (camera->lastItem==lara)camera->lastItem=NULL;
    ItemObj_remove(lara);
    gs->players[1]=NULL;
    gs->playersExtra[1].camera=(Camera){0};
    gs->joinPose[1]=(JoinPose){0};
    gs->secondaryCollisionOffset=(vec3i){0};
    return TR64_GAME_LEAVE_OK;
}
int game_player_warp(unsigned slot)
{
    if (!playerCommandsAllowed() || slot>=TR64_GAME_PLAYERS)
        return TR64_GAME_WARP_REFUSED;
    Lara *lara=gs->players[slot],*dest=gs->players[slot^1u];
    if (!lara)return TR64_GAME_WARP_REFUSED;
    if (game_interaction_busy(lara))return TR64_GAME_WARP_BUSY;
    if (!dest || dest->health<=0)return TR64_GAME_WARP_REFUSED;
    JoinPose pose;
    if (!getJoinPose(dest,&pose))return TR64_GAME_WARP_REFUSED;
    if (lara->health<=0) {
        if (!inventory_consumeCount(SLOT_MEDIKIT_BIG,1))
            return TR64_GAME_WARP_NO_MEDIPACK;
        gs->gSaveGame.mediUsed += 2; /* Source stats count half-medipack units. */
    }
    discardPlayerInteraction(lara);
    hazardClearLaraBurn(lara);
    ItemObj_removeOwnedFlashes(lara);
    for (unsigned i=0;i<8;++i)gs->enemiesExtra[i].contact[slot]=0;
    Room_remove(lara->room,lara);
    ItemObj_resetSlot(lara);
    lara->type=ITEM_LARA;
    lara->pos=pose.at.pos;
    lara->angle.y=pose.yaw;
    lara->intensity=128;
    game_init_item(lara,pose.at.room);
    applyJoinMedium(lara,&pose);
    if (slot)gs->secondaryCollisionOffset=(vec3i){0};
    else gs->cinfo.offset=(vec3i){0};
    return TR64_GAME_WARP_OK;
}
int game_place_player(unsigned slot,uint32 room,int32 x,int32 y,int32 z,int16 angle)
{
    Room *r;
    const Sector *s;
    Lara *lara;
    int32 floor, ceiling, sx, sz;
    if (!gs || slot>=TR64_GAME_PLAYERS || !(lara=gs->players[slot]) ||
        room >= gs->level.roomsCount)
        return 0;
    r = gs->rooms + room;
    sx = (x - tr64_i32(r->info->x)) >> 10;
    sz = (z - tr64_i32(r->info->z)) >> 10;
    if (sx < 0 || sx >= tr64_u16(r->info->x_sectors) || sz < 0 ||
        sz >= tr64_u16(r->info->z_sectors))
        return 0;
    if (Room_getRoom(r, x, y, z) != r)
        return 0;
    s = Room_getSector(r, x, z);
    floor = Sector_getFloor(s, x, y, z);
    ceiling = Sector_getCeiling(s, x, y, z);
    if (floor == -32512 || ceiling == -32512 || y < ceiling || y > floor)
        return 0;
    Room_remove(lara->room, lara);
    lara->pos = vec3i_create(x, y, z);
    lara->angle.y = angle;
    Room_add(r, lara);
    lara->health = 1000;
    Lara_updateWaterState(lara);
    Camera_init(&lara->extraL->camera, lara);
    game_refresh_join_pose(lara);
    return 1;
}
int game_place(uint32 room,int32 x,int32 y,int32 z,int16 angle)
{
    return game_place_player(0,room,x,y,z,angle);
}
bool Lara_claimBlocked(const Lara *lara,const ItemObj *item)
{
    return gs->players[1] && gs->interaction[Lara_slot(lara)^1u]==item;
}
bool Lara_triggerBlocked(const ItemObj *item,unsigned type)
{
    if (!gs->players[1])return false;
    for (unsigned slot=0;slot<TR64_GAME_PLAYERS;++slot) {
        if (gs->interaction[slot]!=item)continue;
        if (gs->retired[slot].actor)return true;
        if (type==TRIGGER_TYPE_SWITCH)
            return (item->flags&ITEM_FLAG_STATUS)!=ITEM_FLAG_STATUS_INACTIVE;
        if (type==TRIGGER_TYPE_KEY)
            return gs->players[slot]->extraL->weaponState!=WEAPON_STATE_FREE ||
                (item->type>=ITEM_PUZZLEHOLE_1 && item->type<=ITEM_PUZZLEHOLE_4);
    }
    return false;
}
void Lara_claim(Lara *lara,ItemObj *item)
{
    gs->interaction[Lara_slot(lara)]=item;
}
int32 Lara_selectedUse(const Lara *lara)
{
    return gs->players[1] && gs->inventory.lara!=lara ? SLOT_MAX : gs->inventory.useSlot;
}
static bool deathAnimationDone(Lara *lara)
{
    return (lara->state==Lara_STATE_DEATH || lara->state==Lara_STATE_DEATH_UW ||
            lara->state==Lara_STATE_DEATH_MIDAS) && ItemObj_animIsEnd(lara,0);
}
static void updatePlayerInput(Lara *lara,uint16 input)
{
    lara->extraL->lastInput=lara->input;
    lara->input=input;
}
static void simulatePlayer(Lara *lara,uint16 input,bool coop)
{
    ExtraInfoLara *e=lara->extraL;
#if TR64_CART_TR2
    /* The second game's cart has no party or retired-player dispatcher. */
    coop=false;
#else
    if (lara->health<=0 && (coop || gs->retired[Lara_slot(lara)].actor)) {
        if (coop)game_retire_interaction(lara);
        if (game_settle_interaction(lara)) {
            updatePlayerInput(lara,input);
            Camera_update(&e->camera);
            return;
        }
    }
#endif
    vec3i old=lara->pos;
    updatePlayerInput(lara,input);
#if TR64_HAS_TR2
    if (TR64_SECOND_GAME && (gs->tr2->boatTick || (lara->health <= 0 && !lara->visibleMask))) {
        gs->tr2->boatTick = false;
        if (TR64_RULE(WATER)) {
            int32 line = game_tr2_water_height(lara->room, lara->pos.x, lara->pos.z);
            gs->tr2->waterDistance = line == WALL ? -WALL : i32_sub(line, lara->pos.y);
            gs->tr2->waterTicks += line != WALL;
            if (lara->health > 0)
                game_tr2_water_air(lara);
        }
        if (TR64_RULE(CREATURES))
            lara->flags &= ~ITEM_FLAG_INJURED;
        if (gs->tr2->vehicle >= 0) {
            TR2BoatCall bcall = {.op = TR2_BOAT_CAMERA, .lara = lara};
            (void)code_call("tr2_boat", (uint32_t)(uintptr_t)&bcall);
        } else if (lara->health > 0) {
            gs->cinfo.trigger = NULL;
            gs->cinfo.radius = 100;
            gs->cinfo.pos = lara->pos;
            gs->cinfo.enemyPush = 1;
            gs->cinfo.enemyHit = 1;
            gs->cinfo.stopOnSlant = 0;
            gs->cinfo.stopOnLava = 0;
            Lara_updateObjectsCollision(lara);
        }
        if (TR64_RULE(SAVED_STATS)) {
            int32_t *previous = gs->tr2->distanceFrom;
            uint32 dx = (uint32)lara->pos.x - (uint32)previous[0];
            uint32 dy = (uint32)lara->pos.y - (uint32)previous[1];
            uint32 dz = (uint32)lara->pos.z - (uint32)previous[2];
            gs->gSaveGame.distance += phd_sqrt(dx*dx + dy*dy + dz*dz);
            previous[0] = lara->pos.x;
            previous[1] = lara->pos.y;
            previous[2] = lara->pos.z;
        }
        Camera_update(&e->camera);
        return;
    }
    if (TR64_SECOND_GAME && gs->tr2->extraAnim) {
        if (lara->state == 0) {
            /* tr2-bartoli-and-weapons.md §2: extra state 0 exits to standing
             * clip 103, frees hands, restores chase camera, and falls through
             * so Lara's collision evaluates the detonator's key trigger. */
            ItemObj_animSet(lara, Lara_ANIM_STAND_NORMAL, true, 0);
            gs->tr2->extraAnim = false;
            e->weaponState = WEAPON_STATE_FREE;
            e->camera.mode = CAMERA_MODE_FOLLOW;
        } else {
            uint16 savedInput = lara->input;
            lara->input &= (uint16)~IN_LOOK;
            Lara_updateLook(lara);
            lara->input = savedInput;
            if (TR64_RULE(WATER) && lara->health > 0)
                game_tr2_water_air(lara);
            if (TR64_RULE(CREATURES))
                lara->flags &= ~ITEM_FLAG_INJURED;
            ItemObj_animProcess(lara, true);
            gs->cinfo.trigger = NULL;
            ItemObj_updateRoom(lara, -762 / 2);
            if (TR64_RULE(SAVED_STATS)) {
                int32_t *previous = gs->tr2->distanceFrom;
                uint32 dx = (uint32)lara->pos.x - (uint32)previous[0];
                uint32 dy = (uint32)lara->pos.y - (uint32)previous[1];
                uint32 dz = (uint32)lara->pos.z - (uint32)previous[2];
                gs->gSaveGame.distance += phd_sqrt(dx*dx + dy*dy + dz*dz);
                previous[0] = lara->pos.x;
                previous[1] = lara->pos.y;
                previous[2] = lara->pos.z;
            }
            Camera_update(&e->camera);
            return;
        }
    }
#endif
#if TR64_HAS_TR2
    if (!TR64_SECOND_GAME)
#endif
    if (!coop) {
        if ((input & (IN_JUMP | IN_WEAPON)) == (IN_JUMP | IN_WEAPON))
            Lara_restore(lara);
        if (lara->health<=0 && gs->brightness==0 && gs->inventory.state==INV_STATE_NONE &&
            deathAnimationDone(lara))
            game_inventory_open(lara,INV_PAGE_DEATH,0);
        else if (ItemObj_isKeyHit(lara,IN_SELECT) && gs->brightness==0)
            game_inventory_open(lara,lara->health>0 ? INV_PAGE_MAIN : INV_PAGE_DEATH,0);
        if (gs->stop.kind!=TR64_GAME_STOP_NONE)return;
    }
    Lara_updateLook(lara);
#if TR64_HAS_TR2
    const bool tr2Water = TR64_RULE(WATER);
    /* Q-T2C-12: the entry mode requests elevation before the depth check. */
    if (TR64_RULE(WET_CAMERA) && lara->waterState==WATER_STATE_WADE)
        e->camera.targetAngle.x=TR2_CAMERA_WET_ELEVATION;
    if (tr2Water)
        game_tr2_water_status(lara);
    else
#endif
        Lara_updateWaterState(lara);
    hazardUpdateLaraBurn(lara);
#if TR64_HAS_TR2
    if (tr2Water)
        game_tr2_water_air(lara);
    else
#endif
    if (lara->health > 0)
    {
        if (lara->waterState == WATER_STATE_UNDER)
        {
            if (lara->timer > 0)
                --lara->timer;
            else
                Lara_hit(lara, 5, &lara->pos, 0);
        }
        else
            lara->timer = lara->timer + 10 < 1800 ? lara->timer + 10 : 1800;
    }
#if TR64_HAS_TR2
    const unsigned movementWater = lara->waterState;
    const bool stroke = tr2Water &&
                        (movementWater == WATER_STATE_SURFACE || movementWater == WATER_STATE_UNDER);
#endif
    switch (lara->waterState)
    {
    case WATER_STATE_ABOVE:
    case WATER_STATE_WADE:
        Lara_updateAbove(lara);
        break;
    case WATER_STATE_SURFACE:
#if TR64_HAS_TR2
        /* Surface control owns the request even on a subsequent dive. */
        if (TR64_RULE(WET_CAMERA))
            e->camera.targetAngle.x=TR2_CAMERA_WET_ELEVATION;
#endif
        Lara_updateSurface(lara);
        break;
    case WATER_STATE_UNDER:
        Lara_updateUnder(lara);
        break;
    }
    if (gs->stop.kind != TR64_GAME_STOP_NONE)
        return;
#if TR64_HAS_TR2
    uint32_t bubblesBefore = TR64_SECOND_GAME ? gs->tr2->arms.inertBubbleCues : 0;
    ItemObj_animProcess(lara, !stroke);
    if (TR64_SECOND_GAME && gs->tr2->arms.inertBubbleCues != bubblesBefore)
        game_tr2_bubbles(lara);
    if (stroke)
        game_tr2_water_move(lara);
#else
    ItemObj_animProcess(lara, true);
#endif
    game_lara_collision(lara);
    if (gs->stop.kind != TR64_GAME_STOP_NONE)
        return;
    int32 offset = lara->waterState == WATER_STATE_SURFACE ? 100
                 : lara->waterState == WATER_STATE_UNDER ? 0 : -762 / 2;
#if TR64_HAS_TR2
    /* The stroke that dove (or changed to a shore stance) still owns
     * this update's membership probe. */
    if (stroke)
        offset = movementWater == WATER_STATE_SURFACE ? 100 : 0;
#endif
    ItemObj_updateRoom(lara, offset);
    const Sector *s = Room_getSector(lara->room, lara->pos.x, lara->pos.z);
    if (s->floor == -127 && lara->waterState != WATER_STATE_UNDER &&
        lara->waterState != WATER_STATE_SURFACE
#if TR64_HAS_TR2
        && !(TR64_SECOND_GAME && gs->tr2->vehicle >= 0)
#endif
        )
    {
        lara->pos = old;
        ItemObj_updateRoom(lara, offset);
    }
#if TR64_HAS_TR2
    if (!TR64_RULE(SAVED_STATS))
#endif
    Lara_recordDistance(lara, &old);
#if TR64_HAS_TR2
    if (TR64_RULE(ARMS))
        game_tr2_update_arms(lara);
    else if (!TR64_SECOND_GAME)
#endif
    Lara_updateWeapon(lara);
#if TR64_HAS_TR2
    /* tr2-camera, Combat entry: the active arms handler requests the
     * view before floor renewal; accepted Look still wins. */
    if (TR64_RULE(COMBAT_CAMERA) && game_tr2_firearms_camera_requested())
        Camera_toCombat(&e->camera);
#endif
#if TR64_HAS_TR2
    if ((!coop || lara->health>0) && !(TR64_SECOND_GAME && gs->tr2->extraAnim))
        game_check_trigger(gs->cinfo.trigger,lara);
#else
    if (!coop || lara->health>0)game_check_trigger(gs->cinfo.trigger,lara);
#endif
    if (gs->stop.kind != TR64_GAME_STOP_NONE)
        return;
#if TR64_HAS_TR2
    /* tr2-oracle-trace, Live statistics: use the final position after
     * contact, weapons and floor actions; square and sum modulo 2^32. */
    if (TR64_RULE(SAVED_STATS)) {
        int32_t *previous = gs->tr2->distanceFrom;
        uint32 dx = (uint32)lara->pos.x - (uint32)previous[0];
        uint32 dy = (uint32)lara->pos.y - (uint32)previous[1];
        uint32 dz = (uint32)lara->pos.z - (uint32)previous[2];
        gs->gSaveGame.distance += phd_sqrt(dx*dx + dy*dy + dz*dz);
        previous[0] = lara->pos.x;
        previous[1] = lara->pos.y;
        previous[2] = lara->pos.z;
    }
#endif
    Camera_update(&e->camera);
    if (gs->stop.kind != TR64_GAME_STOP_NONE)
        return;
    if (lara->health > 0 && e->healthTimer > 0)
        --e->healthTimer;
    if (!TR64_SECOND_GAME)
        rememberJoinPose(lara);
    unsigned slot=Lara_slot(lara);
    if (lara->health>0 && (lara->state==Lara_STATE_USE_KEY || lara->state==Lara_STATE_USE_PUZZLE ||
        (coop && lara->state==Lara_STATE_PICKUP && gs->interaction[slot])))
        gs->interactionAnim[slot]=(InteractionAnim){lara->animIndex,lara->frameIndex,lara->state,lara->goalState};
    if (coop && lara->health<=0)game_retire_interaction(lara);
    if (gs->interaction[slot] && !gs->retired[slot].actor && gs->inventory.state==INV_STATE_NONE) {
        switch (lara->state) {
        case Lara_STATE_STOP:
            if (lara->health>0 && lara->goalState==Lara_STATE_BLOCK_READY)break;
            gs->interaction[slot]=NULL;
            break;
        case Lara_STATE_UW_TREAD:
            if (coop && game_interaction_busy(lara))break;
            gs->interaction[slot]=NULL;
            break;
        case Lara_STATE_PICKUP: case Lara_STATE_USE_KEY: case Lara_STATE_USE_PUZZLE:
        case Lara_STATE_USE_MIDAS: case Lara_STATE_SWITCH_UP: case Lara_STATE_SWITCH_DOWN:
        case Lara_STATE_BLOCK_READY: case Lara_STATE_BLOCK_PUSH: case Lara_STATE_BLOCK_PULL:
            if (lara->health>0)break;
            /* fall through */
        default: gs->interaction[slot]=NULL; break;
        }
    }
}
#if TR64_HAS_TR2
/* tr2-bartoli-and-weapons.md §6: active FlipEffect (op == 9) runs once per
 * tick during the post-camera sound-effects pass until cleared (-1). */
static void updateTR2FlipEffect(Lara *lara)
{
    GameTR2 *tr2 = gs->tr2;
    if (tr2->flipEffect < 0)
        return;
    switch (tr2->flipEffect) {
    case 4:
        if (TR64_RULE(WALL_HAZARDS))
            tr2->finishRequested = true;
        break;
    case 5: {
        if (tr2->flipEffectTimer <= 120) {
            vec3i pos = lara->pos;
            pos.y = i32_add(lara->extraL->camera.target.pos.y,
                            abs((int32)tr2->flipEffectTimer - 30) * 100);
            soundPlay(79, &pos);
        } else {
            tr2->flipEffect = -1;
        }
        ++tr2->flipEffectTimer;
        break;
    }
    case 6:
        soundPlay(278, NULL);
        if (++tr2->flipEffectTimer > 30)
            tr2->flipEffect = -1;
        break;
    case 7:
        soundPlay(24, NULL);
        lara->extraL->camera.bounce = -350;
        tr2->flipEffect = -1;
        break;
    case 8:
        soundPlay(190, NULL);
        tr2->flipEffect = -1;
        break;
    case 9:
        soundPlay(191, NULL);
        tr2->flipEffect = -1;
        break;
    case 10:
        soundPlay(330, NULL);
        tr2->flipEffect = -1;
        break;
    case 11:
        soundPlay(105, NULL);
        lara->extraL->camera.bounce = -75;
        tr2->flipEffect = -1;
        break;
    case 12:
        lara->extraL->weaponState = WEAPON_STATE_FREE;
        break;
    case 13:
        worldRequestFlipToggle();
        worldApplyPendingFlip();
        tr2->flipEffect = -1;
        break;
    case 25:
        soundPlay(331, NULL);
        tr2->flipEffect = -1;
        break;
    case 27:
        soundPlay(338, NULL);
        tr2->flipEffect = -1;
        break;
    default:
        sys_error("Tomb Raider II: unsupported FlipEffect %d", tr2->flipEffect);
        break;
    }
}

void game_tr2_tick(uint32 mask)
{
    ASSERT(gs && TR64_RULE(HARNESS) &&
           (!(mask & ~TR2_INPUT_MASK) || mask==TR64_GAME_SELECT));
    /* tr2-oracle-trace: the standalone 2048 records the last control sample
     * before the native main ring. The next update carries its choice. */
    if (!gs)
        return;
    gs->soundCount=0;
    if (gs->stop.kind!=TR64_GAME_STOP_NONE)
        return;
    GameTR2 *probe = gs->tr2;
    /* Great Wall creatures, Lara's ordinary death boundary. The
     * final record is still a gameplay update before the retry menu. */
    if (TR64_RULE(CREATURES) && probe->deathMenuPending) {
        game_stop_set(TR64_GAME_STOP_DEATH, -1, "Tomb Raider II: Lara died");
        gs->stop.death_ready = 1;
        return;
    }
    if (TR64_RULE(CREATURES) && (probe->deathTicks>300 || (probe->deathTicks>60 && mask)))
        probe->deathMenuPending=true;
    probe->input = mask;
    probe->dynLightCount = 0;
    gs->pickupCount = 0;
    probe->flareTicks += !!(mask & TR2_IN_FLARE);
    probe->drawTicks += !!(mask & IN_WEAPON);
    probe->rollTicks += !!(mask & TR2_IN_ROLL);
    probe->sidestepTicks += !!(mask & (TR2_IN_STEP_LEFT | TR2_IN_STEP_RIGHT));
    /* Schema 20 describes the fixture's held controls, not the sampled
     * decoder's cleaned ones. Preserve mask above, cancel LR here. */
    if ((mask & (IN_LEFT | IN_RIGHT)) == (IN_LEFT | IN_RIGHT))
        mask &= ~(IN_LEFT | IN_RIGHT);
    probe->boatInput = (uint16)(mask & (IN_LEFT | IN_RIGHT | IN_UP | IN_DOWN |
                       IN_JUMP | IN_WALK | IN_ACTION | IN_LOOK));
    probe->boatTick = probe->vehicle >= 0;
    uint32 steps=mask&(TR2_IN_STEP_LEFT|TR2_IN_STEP_RIGHT);
    if (TR64_RULE(DRY_SIDESTEP)) {
        /* The fixture's Walk+side alias is a gait, not a turn request. */
        uint32 turn=mask&(IN_LEFT|IN_RIGHT);
        if (!steps && turn && (mask&IN_WALK) &&
            !(mask&(IN_UP|IN_DOWN|IN_JUMP|TR2_IN_ROLL))) {
            steps=turn==IN_LEFT ? TR2_IN_STEP_LEFT : TR2_IN_STEP_RIGHT;
            mask&=~turn;
        }
        probe->sidestepInput=steps;
    } else if (steps) {
        if (steps == (TR2_IN_STEP_LEFT | TR2_IN_STEP_RIGHT) ||
            (mask & (IN_LEFT | IN_RIGHT | IN_UP | IN_DOWN | IN_JUMP | IN_LOOK | TR2_IN_ROLL)))
            ++probe->ambiguousInputTicks;
        else
            mask |= IN_WALK | (steps == TR2_IN_STEP_LEFT ? IN_LEFT : IN_RIGHT);
    }
    if (!TR64_RULE(ROLL_CONTROL) && (mask & TR2_IN_ROLL))
        mask |= IN_UP | IN_DOWN;
    if (TR64_RULE(WORLD_ITEMS))
        updateTR2Items(gs->players[0]);
    if (TR64_RULE(ROOM_EFFECTS)) {
        const FloorData *floorData=gs->gLastFloorData;
        uint16 slant=gs->gLastFloorSlant;
        game_tr2_run_effects(gs->players[0]);
        gs->gLastFloorData=floorData;
        gs->gLastFloorSlant=slant;
    }
    {
        int32 lethalY = game_tr2_level_script_lethal_y();
        if (lethalY && gs->players[0]->health > 0 && gs->players[0]->pos.y >= lethalY) {
            gs->players[0]->health = -1;
            if (probe->deathTicks == 0)
                game_sndPlayTrack(-1);
            probe->deathTicks = 270;
        }
    }
    if (TR64_RULE(CREATURES) && gs->players[0]->health<=0) {
        gs->players[0]->health=-1;
        if (probe->deathTicks == 0)
            game_sndPlayTrack(-1);
        ++probe->deathTicks;
        if (!gs->players[0]->visibleMask)
            ++probe->deathTicks;
    }
    simulatePlayer(gs->players[0], mask & (IN_LEFT | IN_RIGHT | IN_UP | IN_DOWN |
                   IN_JUMP | IN_WALK | IN_ACTION | IN_LOOK), false);
    if (TR64_RULE(ITEM_ACTIONS))
        updateTR2FlipEffect(gs->players[0]);
    ++gs->gSaveGame.time;
    ++gs->tick;
    /* Shared texture clock. OpenLara src/level.h:25, 2292-2294: the step is
     * 10/30 s, halved for the first game only. view.c applies that period. */
    ++gs->textureTicks;
    if (TR64_RULE(WALL_HAZARDS) && probe->finishRequested) {
        game_stop_set(TR64_GAME_STOP_LEVEL_END, game_tr2_level_script_next_id(),
                      "%s", game_tr2_level_script_finish_label());
        gs->stop.transition = TR64_GAME_TRANSITION_CAMPAIGN;
    }
}

void game_tr2_position(int32_t xyz[3])
{
    const vec3i *p=&gs->players[0]->pos;
    xyz[0]=p->x;xyz[1]=p->y;xyz[2]=p->z;
}
#ifdef TR64_HOST

void game_tr2_report(void)
{
    const GameTR2 *p = gs->tr2;
    log_printf("tr2-inert: trigger-checks=%u dynamic-floor-queries=%u climb-face-queries=%u "
               "sound-requests=%u flare-held-ticks=%u draw-held-ticks=%u water-plane-queries=%u "
               "ambiguous-input-ticks=%u\n",(unsigned)p->triggerCalls,(unsigned)p->dynamicFloorCalls,
               (unsigned)p->climbQueries,(unsigned)p->soundCalls,(unsigned)p->flareTicks,
               (unsigned)p->drawTicks,(unsigned)p->waterTicks,(unsigned)p->ambiguousInputTicks);
    log_printf("tr2-controls: dedicated-roll-held-ticks=%u dedicated-sidestep-held-ticks=%u\n",
               (unsigned)p->rollTicks,(unsigned)p->sidestepTicks);
    log_printf("tr2-campfire: in-range-ticks=%u\n",
               (unsigned)p->campfireRangeTicks);
    log_printf("tr2-inert-lara-item-actions: count=%u\n",(unsigned)p->inertLaraItemActions);
    log_printf("tr2-inert-animation-bubbles: count=%u\n",(unsigned)p->arms.inertBubbleCues);
    for (unsigned type = 0; type < TR2_PUBLIC_TYPES; ++type) {
        if (p->inertByType[type]) {
            const char *name = TR64_RULE(INTERACTIONS) ? game_tr2_inert_interaction(type) : NULL;
            if (name)
                log_printf("tr2-inert-item: %s type=%u count=%u\n",name,type,(unsigned)p->inertByType[type]);
            else
                log_printf("tr2-inert-item: type=%u count=%u\n",type,(unsigned)p->inertByType[type]);
        }
    }
    if (TR64_RULE(INTERACTIONS))
        log_printf("tr2-interactions: pickup-commits=%u switch-uses=%u\n",
                   (unsigned)p->pickupCommits,(unsigned)p->switchUses);
    for (unsigned op = 0; op < 32; ++op)
        if (p->inertAction[op])
            log_printf("tr2-inert-floor-action: opcode=%u count=%u\n",op,(unsigned)p->inertAction[op]);
    log_printf("tr2-inert-look-targets: count=%u\n",(unsigned)p->inertLookTargets);
}
#endif
#endif
static void simulateTick(const uint16 inputs[TR64_GAME_PLAYERS],bool coop)
{
    for (ItemObj *it=gs->firstActive;it;)
    {
        ItemObj *next = it->nextActive;
        GameItemCall call = {0};
        call.op = GAME_ITEM_UPDATE;
        call.self = it;
        game_item_call(&call);
        if (gs->blockTrigger) {
            const FloorData *trigger=gs->blockTrigger;
            gs->blockTrigger=NULL;
            game_check_trigger(trigger,NULL);
        }
        if (gs->stop.kind != TR64_GAME_STOP_NONE)
            return;
        it = next;
    }
    if (game_is_cutscene_level(gs->gLevelID))
    {
        game_cut_tick();
        return;
    }
    simulatePlayer(gs->players[0],inputs[0],coop);
    if (gs->stop.kind!=TR64_GAME_STOP_NONE)return;
    if (coop) {
        if (gs->inventory.state==INV_STATE_NONE && gs->pendingLevel==LVL_MAX) {
            /* Offset is the collision output that survives an actor update. */
            vec3i offset=gs->cinfo.offset;
            gs->cinfo.offset=gs->secondaryCollisionOffset;
            simulatePlayer(gs->players[1],inputs[1],true);
            gs->secondaryCollisionOffset=gs->cinfo.offset;
            gs->cinfo.offset=offset;
            if (gs->stop.kind!=TR64_GAME_STOP_NONE)return;
        } else {
            updatePlayerInput(gs->players[1],inputs[1]);
        }
    }
    ++gs->gSaveGame.time;
    worldUpdateEffects();
    ++gs->textureTicks;
}
static void openPartyMenu(const uint16 inputs[TR64_GAME_PLAYERS])
{
    if (gs->brightness!=0 || gs->pendingLevel!=LVL_MAX)return;
    bool dead=game_player_alive_mask()==0;
    if (dead && game_save_blocked())return;
    Lara *owner=NULL;
    for (unsigned slot=0;slot<TR64_GAME_PLAYERS;++slot)
        if ((inputs[slot]&IN_SELECT) && !(gs->players[slot]->input&IN_SELECT)) {
            owner=gs->players[slot];
            break;
        }
    if (dead && deathAnimationDone(gs->players[0]) && deathAnimationDone(gs->players[1]))
        owner=gs->players[0];
    if (owner) {
        updatePlayerInput(gs->players[0],inputs[0]);
        updatePlayerInput(gs->players[1],inputs[1]);
        game_inventory_open(owner,dead ? INV_PAGE_DEATH : INV_PAGE_MAIN,0);
    }
}
unsigned game_menu_player(void)
{
    return gs && gs->inventory.state!=INV_STATE_NONE && gs->players[1] &&
        gs->inventory.lara==gs->players[1];
}
void game_tick_players(const uint16 input_mask[TR64_GAME_PLAYERS])
{
    if(!gs)sys_error("game tick before level");
    gs->soundCount=0;
    gs->pickupCount=0;
    if(gs->stop.kind!=TR64_GAME_STOP_NONE)return;
    bool coop=gs->players[1]!=NULL;
    for (unsigned slot=0;slot<1u+(unsigned)coop;++slot) {
        uint16 input=input_mask[slot];
        if(input&~(IN_LEFT|IN_RIGHT|IN_UP|IN_DOWN|IN_JUMP|IN_WALK|IN_ACTION|
                   IN_LOOK|IN_WEAPON|IN_SELECT)) {
            game_stop_set(TR64_GAME_STOP_NOT_PORTED,-1,"game input not ported: mask=%u",
                          (unsigned)input);
            return;
        }
    }
    if(gs->gCurTrack<0) {
        int32 ambient=ambientTrack(gs->gLevelID);
        if(ambient>=0)game_sndPlayTrack(ambient);
    }
    if (coop)
        for (unsigned slot=0;slot<TR64_GAME_PLAYERS;++slot)
            if (gs->players[slot]->health<=0)
                game_retire_interaction(gs->players[slot]);
    bool menuAtStart=gs->inventory.state!=INV_STATE_NONE;
    if (coop && !menuAtStart)openPartyMenu(input_mask);
    if(menuAtStart) {
        Lara *lara=gs->inventory.lara;
        if(!lara || !lara->extraL)sys_error("game inventory missing input actor");
        updatePlayerInput(lara,input_mask[Lara_slot(lara)]);
        if (coop) {
            unsigned other=Lara_slot(lara)^1u;
            updatePlayerInput(gs->players[other],input_mask[other]);
        }
        game_inventory_update(1);
        if(gs->stop.kind!=TR64_GAME_STOP_NONE)return;
        if(gs->inventory.page!=INV_PAGE_TITLE && gs->inventory.state==INV_STATE_NONE &&
           ((coop && lara->health<=0) || Lara_useItem(lara,gs->inventory.useSlot)))
            gs->inventory.useSlot=SLOT_MAX;
    }
    if(gs->inventory.page!=INV_PAGE_TITLE && gs->inventory.state==INV_STATE_NONE &&
       gs->pendingLevel==LVL_MAX && !(coop && menuAtStart))
        simulateTick(input_mask,coop);
    if (gs->retireLevel && gs->stop.kind==TR64_GAME_STOP_NONE &&
        gs->inventory.state==INV_STATE_NONE && gs->pendingLevel==LVL_MAX) {
        for (unsigned slot=0;slot<TR64_GAME_PLAYERS;++slot)
            if (gs->retired[slot].actor && gs->retired[slot].ticks==0) {
                Lara *actor=gs->retired[slot].actor;
                gs->retireLevel=false;
                gs->retired[slot]=(RetiredAction){0};
                gs->interaction[slot]=NULL;
                game_complete_level(actor);
                break;
            }
    }
    if(gs->stop.kind!=TR64_GAME_STOP_NONE)return;
    gs->stop.death_ready=gs->inventory.page==INV_PAGE_DEATH &&
        gs->inventory.state==INV_STATE_SHOW &&
        gs->inventory.frameIndex==gs->inventory.frameTarget;
    if(gs->pendingLevel!=LVL_MAX && gs->inventory.state==INV_STATE_NONE) {
        game_stop_set(TR64_GAME_STOP_LEVEL_END,gs->pendingLevel,
                      "game level transition UI not ported");
        gs->stop.transition=gs->pendingLevel==LVL_LOAD ? TR64_GAME_TRANSITION_LOAD :
            gs->pendingLevel==LVL_TR1_TITLE ? TR64_GAME_TRANSITION_TITLE :
            gs->pendingLevel==LVL_TR1_GYM ? TR64_GAME_TRANSITION_GYM :
            gs->pendingLevel==gs->gLevelID ? TR64_GAME_TRANSITION_RESTART :
            gs->gLevelID==LVL_TR1_TITLE ? TR64_GAME_TRANSITION_NEW_GAME :
            TR64_GAME_TRANSITION_CAMPAIGN;
        return;
    }
    ++gs->tick;
}
void game_set_extra_input_player(unsigned slot, uint32_t mask)
{
    if (gs && slot < TR64_GAME_PLAYERS)
        gs->extraInput[slot] = mask;
}
void game_set_extra_input(uint32_t mask)
{
    game_set_extra_input_player(0, mask);
}
void game_tick(uint16 input)
{
    game_tick_players((const uint16[TR64_GAME_PLAYERS]){input, 0});
}
