/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/room.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Ported from src/fixed/room.h:872-1193 and lara.h:4575-4623 at
 * 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"
#if TR64_HAS_TR2
#include "tr2/objects.h"
#endif
/* Oracle hunk 28 (room.h worldTriggerActivate): what an ACTIVATE_OBJECT action does once the mask is complete and the item is not
 * yet active, also the host test directive `activate`. A non-enemy item becomes status active (triggers-and-effects.md section 4,
 * object action, "everything else"); the source did it only for a status of 0, so items that start invisible (level flag bit 8)
 * stayed invisible. Enemies keep their own rule (the creature slot). */
void worldTriggerActivate(ItemObj *item)
{
    ItemObj_activate(item);
    if (!(item->flags & ITEM_FLAG_STATUS) && (item->flags & ITEM_FLAG_ACTIVE))
        item->flags |= ITEM_FLAG_STATUS_ACTIVE;
    else if ((item->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INVISIBLE && (item->flags & ITEM_FLAG_ACTIVE) &&
             item->health == NOT_ENEMY)
        item->flags = (item->flags & ~ITEM_FLAG_STATUS) | ITEM_FLAG_STATUS_ACTIVE;
}
static int32 tutorial(ItemObj *lara, int32 track)
{
    if (!lara)
        return track;
    switch (track)
    {
    case 28:
        if ((gs->gSaveGame.tracks[track] & TRACK_FLAG_ONCE) && lara->state == Lara_STATE_JUMP_UP)
            track = 29;
        break;
    case 37:
    case 41:
        if (lara->state != Lara_STATE_HANG)
            track = 0;
        break;
    case 42:
        if ((gs->gSaveGame.tracks[track] & TRACK_FLAG_ONCE) && lara->state == Lara_STATE_HANG)
            track = 43;
        break;
    case 49:
        if (lara->state != Lara_STATE_SURF_TREAD)
            track = 0;
        break;
    case 50:
        if (gs->gSaveGame.tracks[track] & TRACK_FLAG_ONCE)
        {
            if (++lara->aggression > 90)
            {
                gs->pendingLevel=LVL_TR1_TITLE;
                return track;
            }
        }
        else
        {
            if (lara->state != Lara_STATE_WATER_OUT)
                track = 0;
            lara->aggression = 0;
        }
        break;
    }
    return track;
}
static void checkCamera(const FloorData *fd, Camera *camera)
{
    bool lookAt = false;
    for (;;)
    {
        uint16 cmd = tr64_u16(*fd++), arg = FD_ARGS(cmd);
        switch (FD_ACTION(cmd))
        {
        case TRIGGER_ACTION_ACTIVATE_CAMERA:
            cmd = worldActionEffectiveEnd(cmd, tr64_u16(*fd++));
            if (arg != camera->lastIndex)
            {
                camera->lookAtItem = NULL;
                return;
            }
            camera->index = arg;
            if (camera->timer < 0 || camera->mode == CAMERA_MODE_LOOK ||
                camera->mode == CAMERA_MODE_COMBAT)
            {
                camera->timer = -1;
                camera->lookAtItem = NULL;
                return;
            }
            camera->mode = CAMERA_MODE_FIXED;
            lookAt = true;
            break;
        case TRIGGER_ACTION_CAMERA_TARGET:
            if (camera->mode != CAMERA_MODE_LOOK && camera->mode != CAMERA_MODE_COMBAT)
                camera->lookAtItem = gs->items + arg;
            break;
        case TRIGGER_ACTION_FLYBY:
            cmd = worldActionEffectiveEnd(cmd, tr64_u16(*fd++));
            break;
        }
        if (cmd & 32768)
            break;
    }
    if (!lookAt && camera->lookAtItem && camera->lookAtItem != camera->lastItem &&
        (camera->lookAtItem->flags & ITEM_FLAG_ANIMATED))
        camera->lookAtItem = NULL;
}
#if TR64_HAS_TR2
/* tr2-triggers-and-interaction.md, Camera renewal details. Renewal
 * follows the raw action's end bit; execution follows the camera extra. */
static ItemObj *tr2CameraSubject(unsigned slot)
{
    ItemObj *item=gs->items+slot;
    if (item->room && item->animIndex!=UINT16_MAX)
        return item;
    ++gs->tr2->inertLookTargets;
    return NULL;
}

static bool tr2HasCameraOverride(const Camera *c)
{
    return c->mode==CAMERA_MODE_LOOK || c->mode==CAMERA_MODE_COMBAT;
}

static void tr2RenewView(const FloorData *fd,Camera *c)
{
    bool fixedSeen=false,fixedMatch=false;
    uint16 action;
    if (c->mode==CAMERA_MODE_OBJECT)
        return;
    do {
        action=tr64_u16(*fd++);
        unsigned arg=FD_ARGS(action),op=FD_ACTION(action)&TR64_TR2_FLOOR_ACTION_KIND_MASK;
        if (op==TR64_TR2_FLOOR_FIXED_ACTION) {
            ++fd;
            fixedSeen=true;
            fixedMatch=(int32)arg==c->lastIndex;
            if (fixedMatch) {
                c->index=arg;
                if (c->timer<0 || tr2HasCameraOverride(c)) {
                    c->timer=-1;
                    fixedMatch=false;
                } else {
                    c->mode=CAMERA_MODE_FIXED;
                }
            }
        } else if (op==TR64_TR2_FLOOR_TARGET_ACTION && !tr2HasCameraOverride(c)) {
            c->lookAtItem=tr2CameraSubject(arg);
        } else if (op>TR64_TR2_FLOOR_MAX_ACTION) {
            ++gs->tr2->cameraChecks[TR2_CAM_OPAQUE_RENEWAL];
        }
    } while (!FD_END(action));
    if (c->index<0 && c->timer>0)
        c->timer=-1; /* Only an evaluated trigger can expire an unselected view. */
    if (!fixedMatch &&
        (fixedSeen || (c->lookAtItem && c->lookAtItem!=c->lastItem &&
                       (c->lookAtItem->flags&ITEM_FLAG_ANIMATED))))
        c->lookAtItem=NULL;
}


/* tr2-triggers-and-interaction, Floor evaluation, Item masks and action
 * ordering. Controller-owned interactions and camera actions are separate.
 * Admission checked every instruction, extension and object reference. */
enum { TR2_TRIGGER_ANTITRIGGER = 9 };
static void tr2ItemAction(ItemObj *item, unsigned type, uint16 data)
{
    /* Lara is not an active world item. No Lara-target rule is cleared
     * for this harness; never feed the generic countdown into her air. */
    if (item->type == ITEM_LARA) {
        ++gs->tr2->inertLaraItemActions;
        return;
    }
    if (!item->room || (item->flags & (ITEM_FLAG_ONCE | TR2_FLAG_RETIRED)))
        return;
    unsigned timer = FD_TIMER(data);
    item->timer = timer == 1 ? 1 : timer * 30;
    unsigned mask = data & ITEM_FLAG_MASK;
    bool anti = type == TRIGGER_TYPE_ANTIPAD || type == TR2_TRIGGER_ANTITRIGGER;
    if (anti)
        item->flags &= ~mask;
    else if (type == TRIGGER_TYPE_SWITCH)
        item->flags ^= mask;
    else
        item->flags |= mask;
    bool complete = (item->flags & ITEM_FLAG_MASK) == ITEM_FLAG_MASK;
    if (FD_ONCE(data) && (complete || anti))
        item->flags |= ITEM_FLAG_ONCE;
    if (complete && !game_tr2_activate_item(item))
        ++gs->tr2->inertAction[TRIGGER_ACTION_ACTIVATE_OBJECT];
}

static bool tr2TriggerQualifies(unsigned type,const Lara *lara)
{
    if (!lara)
        return type==TRIGGER_TYPE_OBJECT;
    switch (type) {
    case TRIGGER_TYPE_PAD: case TRIGGER_TYPE_ANTIPAD:
        return lara->pos.y==lara->roomFloor;
    case TRIGGER_TYPE_COMBAT:
        return lara->extraL->weaponState==WEAPON_STATE_READY;
    case TRIGGER_TYPE_ACTIVATE: case TR2_TRIGGER_ANTITRIGGER:
        return true;
    default:
        return false;
    }
}

/* tr2-triggers-and-interaction, Floor evaluation, Camera renewal and
 * Action ordering: renew first, qualify the owner once, then act in order. */
static void tr2Actions(const FloorData *fd, Camera *c, Lara *lara)
{
    uint16 cmd = tr64_u16(*fd++);
    if (FD_FLOOR_TYPE(cmd) == FLOOR_TYPE_LAVA && !FD_END(cmd))
        cmd = tr64_u16(*fd++);
    if (FD_FLOOR_TYPE(cmd) != FLOOR_TYPE_TRIGGER)
        return;
    unsigned type = FD_TRIGGER_TYPE(cmd);
    uint16 data = tr64_u16(*fd++);
    if (TR64_RULE(CAMERA_VIEW))
        tr2RenewView(fd,c);
    bool timedRelease = false;
    if (TR64_TR2_FLOOR_CONTROLLER_MASK & (1u << type)) {
        if (!lara || !TR64_RULE(INTERACTIONS))
            return;
        uint16 owner = tr64_u16(*fd++);
        unsigned slot = FD_ARGS(owner);
        ItemObj *item = gs->items + slot;
        if (type == TRIGGER_TYPE_SWITCH) {
            if (!game_tr2_switch_settled(item,FD_TIMER(data)))
                return;
            timedRelease = item->state == 1 && FD_TIMER(data);
        } else if (type == TRIGGER_TYPE_PICKUP) {
            if (!game_tr2_pickup_type(item->type))
                game_tr2_refuse_interaction(item,"unsupported pickup trigger owner");
            if ((item->flags & ITEM_FLAG_STATUS) != ITEM_FLAG_STATUS_INVISIBLE ||
                !(gs->unlinkedItems[slot >> 5] & (1u << (slot & 31))))
                return;
            item->flags = (item->flags & ~ITEM_FLAG_STATUS) | ITEM_FLAG_STATUS_INACTIVE;
        } else {
            if (((item->type < 201 || item->type > 204) &&
                 (item->type < 182 || item->type > 189) &&
                 item->type != TR2_ITEM_DETONATOR) ||
                (item->flags & ITEM_FLAG_STATUS) != ITEM_FLAG_STATUS_ACTIVE ||
                lara->extraL->weaponState == WEAPON_STATE_BUSY)
                return;
            /* Key/puzzle is spent at entry; the floor list consumes the socket
             * or detonator only after the authored hands-free event. */
            item->flags = (item->flags & ~ITEM_FLAG_STATUS) | ITEM_FLAG_STATUS_INACTIVE;
        }
        if (FD_END(owner))
            return;
    } else if (!tr2TriggerQualifies(type,lara)) {
        return;
    }
    ItemObj *subject = NULL;
    int32_t candidateEffect = -1;
    bool hadFlipOp = false;
    do {
        cmd = tr64_u16(*fd++);
        unsigned op = FD_ACTION(cmd)&TR64_TR2_FLOOR_ACTION_KIND_MASK, arg = FD_ARGS(cmd);
        switch (op) {
        case TRIGGER_ACTION_ACTIVATE_OBJECT:
            if (TR64_RULE(ITEM_ACTIONS))
                tr2ItemAction(gs->items + arg, type, data);
            break;
        case TRIGGER_ACTION_ACTIVATE_CAMERA: {
            uint16 extra = tr64_u16(*fd++);
            cmd = worldActionEffectiveEnd(cmd,extra);
            if (!TR64_RULE(CAMERA_VIEW) || (gs->cameraFlags[arg] & FIXED_CAMERA_FLAG_ONCE))
                break;
            c->index = arg;
            if (tr2HasCameraOverride(c) || type == TRIGGER_TYPE_COMBAT || timedRelease ||
                (type != TRIGGER_TYPE_SWITCH && (int32)arg == c->lastIndex))
                break;
            c->timer = FD_TIMER(extra)*30;
            c->speed = FD_SPEED(extra)*8+1;
            if (FD_ONCE(extra))
                gs->cameraFlags[arg] |= FIXED_CAMERA_FLAG_ONCE;
            c->mode = lara ? CAMERA_MODE_FIXED : CAMERA_MODE_OBJECT;
            break;
        }
        case TRIGGER_ACTION_CAMERA_TARGET:
            if (TR64_RULE(CAMERA_VIEW))
                subject = tr2CameraSubject(arg);
            break;
        case TRIGGER_ACTION_END:
            /* Great Wall level, Exit: finish this tick, including Lara,
             * the camera and the saved clock, before stopping the host. */
            if (TR64_RULE(WALL_HAZARDS))
                gs->tr2->finishRequested=true;
            else if (TR64_RULE(ITEM_ACTIONS))
                ++gs->tr2->inertAction[op];
            break;
        case TRIGGER_ACTION_SOUNDTRACK:
            /* tr2-bartoli-and-weapons.md §6: op == 8 music/CD track action. */
            if (TR64_RULE(ITEM_ACTIONS) && arg > 1 && arg < 64) {
                uint16_t *flags = &gs->tr2->cdFlags[arg];
                uint16_t maskBits = data & ITEM_FLAG_MASK;
                bool skip = false;
                if (type != TRIGGER_TYPE_SWITCH) {
                    if (*flags & maskBits)
                        skip = true;
                    else if (FD_ONCE(data))
                        *flags |= maskBits;
                }
                if (!skip) {
                    unsigned timer = FD_TIMER(data);
                    if ((int16_t)arg == gs->tr2->pendingTrack) {
                        if (*flags & 0xffu) {
                            uint8_t cd = (uint8_t)((*flags & 0xffu) - 1u);
                            *flags = (uint16_t)((*flags & ~0xffu) | cd);
                            if (!cd) {
                                gs->tr2->pendingTrack = -1;
                                game_sndPlayTrack((int32)arg);
                            }
                        }
                    } else if (timer == 0) {
                        game_sndPlayTrack((int32)arg);
                    } else {
                        gs->tr2->pendingTrack = (int16_t)arg;
                        uint8_t cd = (uint8_t)((timer * 30u) & 0xffu);
                        *flags = (uint16_t)((*flags & ~0xffu) | cd);
                    }
                }
            }
            break;
        case TRIGGER_ACTION_EFFECT:
            if (TR64_RULE(ITEM_ACTIONS))
                candidateEffect = (int32_t)arg;
            break;
        case TRIGGER_ACTION_SECRET:
            /* tr2-bartoli-and-weapons.md §6: op == 10 is an inert no-op in TR2. */
            break;
        case TRIGGER_ACTION_CLEAR_BODIES:
            if (TR64_RULE(ITEM_ACTIONS))
                game_tr2_clear_bodies(lara);
            break;
        default:
            if (!TR64_RULE(ITEM_ACTIONS))
                break;
            if (op == TRIGGER_ACTION_FLOW && TR64_RULE(WATER))
                sys_error("Tomb Raider II: unsupported water movement: current sink");
            if (op == TRIGGER_ACTION_FLIP || op == TRIGGER_ACTION_FLIP_ON ||
                op == TRIGGER_ACTION_FLIP_OFF) {
                hadFlipOp = true;
                worldApplyFlipAction(op, arg, FD_MASK(data), FD_ONCE(data),
                                     type == TRIGGER_TYPE_SWITCH ? type : TRIGGER_TYPE_ACTIVATE);
            } else {
                ++gs->tr2->inertAction[op];
            }
            break;
        }
    } while (!FD_END(cmd));
    if (subject && (c->mode==CAMERA_MODE_FIXED || c->mode==CAMERA_MODE_OBJECT))
        c->lookAtItem = subject;
    bool didFlip = false;
    if (TR64_RULE(ITEM_ACTIONS))
        didFlip = worldApplyPendingFlip();
    if (candidateEffect >= 0 && (!hadFlipOp || didFlip)) {
        gs->tr2->flipEffect = (int16_t)candidateEffect;
        gs->tr2->flipEffectTimer = 0;
    }
}
#endif
void game_check_trigger(const FloorData *fd,ItemObj *lara)
{
    uint16 cmd, data, type;
    ItemObj *cameraItem = NULL, *switchItem = NULL;
    int32 effectID = -1;
    Camera *camera=lara ? &lara->extraL->camera : &gs->playersExtra[0].camera;
#if TR64_HAS_TR2
    if (TR64_RULE(CLIMB_WALL)) {
        /* The movement code sees the mark from the previous ordinary
         * evaluation. Terrain queries alone must not set this state. */
        unsigned face=0;
        if (lara) {
            gs->tr2->climbEnabled=false;
            face=1u<<(8+(((uint16)(lara->angle.y+8192))>>14));
        }
        while (fd) {
            uint16 word=tr64_u16(*fd);
            unsigned kind=word&TR64_TR2_FLOOR_KIND_MASK;
            if (kind==FLOOR_TYPE_TRIGGER)
                break;
            if (kind==FLOOR_TYPE_CLIMB && (word&face))
                gs->tr2->climbEnabled=true;
            if (FD_END(word)) {
                fd=NULL;
                break;
            }
            fd+=1+!!(TR64_TR2_FLOOR_PAYLOAD_MASK&(1u<<kind));
        }
    }
#endif
    if (!fd)
        return;
#if TR64_HAS_TR2
    if (TR64_SECOND_GAME) {
        ++gs->tr2->triggerCalls;
        if (TR64_RULE(CAMERA_VIEW) || TR64_RULE(ITEM_ACTIONS))
            tr2Actions(fd,camera,lara);
        return;
    }
#endif
    if ((tr64_u16(*fd) & 31) == FLOOR_TYPE_LAVA)
    {
        /* Oracle hunk 21 (room.h checkTrigger): the lava floor rule, was a TODO in the source. */
        if (lara)
            hazardLavaBurn(lara);
        if (tr64_u16(*fd) & 32768)
            return;
        ++fd;
    }
    cmd = tr64_u16(*fd++);
    data = tr64_u16(*fd++);
    type = FD_TRIGGER_TYPE(cmd);
    if (lara && gs->players[1]) {
        /* Do not refresh/activate an object's cameras while waiting for its
         * committed interaction; the same gate protects all later actions. */
        if ((type==TRIGGER_TYPE_SWITCH || type==TRIGGER_TYPE_KEY) &&
            Lara_triggerBlocked(gs->items+FD_ARGS(tr64_u16(*fd)),type))
            return;
        if (type==TRIGGER_TYPE_PICKUP &&
            ((gs->items[FD_ARGS(tr64_u16(*fd))].flags&ITEM_FLAG_STATUS)!=ITEM_FLAG_STATUS_INVISIBLE ||
             Lara_triggerBlocked(gs->items+FD_ARGS(tr64_u16(*fd)),type)))
            return;
    }
    if (camera->mode != CAMERA_MODE_OBJECT)
        checkCamera(fd, camera);
    if (!lara && type!=TRIGGER_TYPE_OBJECT)
        return;
    if (lara)
    {
        switch (type)
        {
        case TRIGGER_TYPE_PAD:
        case TRIGGER_TYPE_ANTIPAD:
            if (lara->pos.y != lara->roomFloor)
                return;
            break;
        case TRIGGER_TYPE_OBJECT:
        case TRIGGER_TYPE_DUMMY:
            return;
        case TRIGGER_TYPE_COMBAT:
            if (lara->extraL->weaponState != WEAPON_STATE_READY)
                return;
            break;
        case TRIGGER_TYPE_SWITCH:
            switchItem = gs->items + FD_ARGS(tr64_u16(*fd));
            if (!useSwitch(switchItem, FD_TIMER(data)))
                return;
            if (gs->players[1])game_interaction_triggered(switchItem);
            ++fd;
            break;
        case TRIGGER_TYPE_KEY:
            if (!useKey(gs->items + FD_ARGS(tr64_u16(*fd)), lara))
                return;
            if (gs->players[1])game_interaction_triggered(gs->items+FD_ARGS(tr64_u16(*fd)));
            ++fd;
            break;
        case TRIGGER_TYPE_PICKUP:
            if (!usePickup(gs->items + FD_ARGS(tr64_u16(*fd))))
                return;
            ++fd;
            break;
        }
    }
    for (;;)
    {
        uint16 action = tr64_u16(*fd++), arg = FD_ARGS(action);
        switch (FD_ACTION(action))
        {
        case TRIGGER_ACTION_ACTIVATE_OBJECT:
        {
            ItemObj *item = gs->items + arg;
            if (item->flags & ITEM_FLAG_ONCE)
                break;
            item->timer = FD_TIMER(data);
            if (item->timer != 1)
                item->timer *= 30;
            if (type == TRIGGER_TYPE_SWITCH)
                item->flags ^= data & ITEM_FLAG_MASK;
            else if (type == TRIGGER_TYPE_ANTIPAD)
                item->flags &= ~(data & ITEM_FLAG_MASK);
            else
                item->flags |= data & ITEM_FLAG_MASK;
            if ((item->flags & ITEM_FLAG_MASK) != ITEM_FLAG_MASK)
                break;
            if (data & 256)
                item->flags |= ITEM_FLAG_ONCE;
            if (item->flags & ITEM_FLAG_ACTIVE)
                break;
            worldTriggerActivate(item);
            break;
        }
        case TRIGGER_ACTION_ACTIVATE_CAMERA:
        {
            uint16 cam = tr64_u16(*fd++);
            action = worldActionEffectiveEnd(action, cam);
            if (gs->cameraFlags[arg] & FIXED_CAMERA_FLAG_ONCE)
                break;
            camera->index = arg;
            if (camera->mode == CAMERA_MODE_LOOK || camera->mode == CAMERA_MODE_COMBAT ||
                type == TRIGGER_TYPE_COMBAT)
                break;
            if (type == TRIGGER_TYPE_SWITCH && switchItem->state == 1 && FD_TIMER(data) != 0)
                break;
            if (type == TRIGGER_TYPE_SWITCH || camera->index != camera->lastIndex)
            {
                camera->timer = FD_TIMER(cam);
                if (camera->timer != 1)
                    camera->timer *= 30;
                if (FD_ONCE(cam))
                    gs->cameraFlags[arg] |= FIXED_CAMERA_FLAG_ONCE;
                camera->speed = ((FD_SPEED(cam)) << 3) + 1;
                camera->mode = lara ? CAMERA_MODE_FIXED : CAMERA_MODE_OBJECT;
            }
            break;
        }
        case TRIGGER_ACTION_CAMERA_TARGET:
            cameraItem = gs->items + arg;
            break;
        case TRIGGER_ACTION_SOUNDTRACK:
        {
            int32 track = tutorial(lara, arg);
            uint8 *flags;
            if (gs->stop.kind != TR64_GAME_STOP_NONE)
                return;
            if (!track)
                break;
            flags = gs->gSaveGame.tracks + track;
            if (*flags & TRACK_FLAG_ONCE)
                break;
            if (type == TRIGGER_TYPE_SWITCH)
                *flags ^= FD_MASK(data);
            else if (type == TRIGGER_TYPE_ANTIPAD)
                *flags &= ~FD_MASK(data);
            else
                *flags |= FD_MASK(data);
            if ((*flags & TRACK_FLAG_MASK) == TRACK_FLAG_MASK)
            {
                if (FD_ONCE(data))
                    *flags |= TRACK_FLAG_ONCE;
                game_sndPlayTrack(track);
            }
            else
                game_sndPlayTrack(-1);
            break;
        }
        case TRIGGER_ACTION_SECRET:
            if (!(gs->gSaveGame.secrets & (1u << arg)))
            {
                gs->gSaveGame.secrets |= 1u << arg;
                game_sndPlayTrack(13);
            }
            break;
        case TRIGGER_ACTION_FLYBY:
            action = worldActionEffectiveEnd(action, tr64_u16(*fd++));
            break;
        case TRIGGER_ACTION_CLEAR_BODIES:
        case TRIGGER_ACTION_CUTSCENE:
            break;
        case TRIGGER_ACTION_FLOW:
            if (arg < gs->level.camerasCount)
            {
                unsigned slot=lara ? Lara_slot(lara) : 0;
                gs->flow_camera[slot] = arg;
                gs->flow_active[slot] = true;
            }
            break;
        case TRIGGER_ACTION_FLIP:
        case TRIGGER_ACTION_FLIP_ON:
        case TRIGGER_ACTION_FLIP_OFF:
            worldApplyFlipAction(FD_ACTION(action), arg, FD_MASK(data), FD_ONCE(data) != 0, type);
            break;
        case TRIGGER_ACTION_END:
            game_complete_level(lara);
            break;
        case TRIGGER_ACTION_EFFECT:
            effectID = arg;
            break;
        }
        if (action & 32768)
            break;
    }
    {
        bool flipped = worldApplyPendingFlip();
        if (effectID >= 0)
            worldDispatchEffect(effectID, lara, flipped);
    }
    if (cameraItem && (camera->mode == CAMERA_MODE_FIXED || camera->mode == CAMERA_MODE_OBJECT))
        camera->lookAtItem = cameraItem;
}

enum { INTERACTION_LIMIT=4096 };

static bool switchType(unsigned type)
{
    return type==ITEM_SWITCH || type==ITEM_SWITCH_WATER;
}
static bool keyType(unsigned type)
{
    return (type>=ITEM_KEYHOLE_1 && type<=ITEM_KEYHOLE_4) ||
           (type>=ITEM_PUZZLEHOLE_1 && type<=ITEM_PUZZLEHOLE_DONE_4);
}
static bool movingBlock(const ItemObj *item)
{
    if (item->type<ITEM_BLOCK_1 || item->type>ITEM_BLOCK_4)return false;
    unsigned i=(unsigned)(item-gs->items);
    return (gs->coopBlocks[i/32]&(1u<<(i%32)))!=0;
}
static bool owedScion(const ItemObj *item)
{
    unsigned status=item->flags&ITEM_FLAG_STATUS;
    return (item->type==ITEM_SCION_PICKUP_HOLDER && gs->gLevelID==LVL_TR1_10B) ||
        (item->type==ITEM_SCION_PICKUP_QUALOPEC && gs->gLevelID==LVL_TR1_8C &&
         (status==ITEM_FLAG_STATUS_INVISIBLE || status==ITEM_FLAG_STATUS_INACTIVE));
}
bool game_interaction_busy(const Lara *actor)
{
    unsigned slot=Lara_slot(actor);
    if (gs->retired[slot].actor)return true;
    const ItemObj *item=gs->interaction[slot];
    if (!item)return false;
    unsigned status=item->flags&ITEM_FLAG_STATUS;
    if (owedScion(item) || movingBlock(item))return true;
    if (switchType(item->type))return status==ITEM_FLAG_STATUS_ACTIVE || status==ITEM_FLAG_STATUS_INACTIVE;
    if (keyType(item->type))return status==ITEM_FLAG_STATUS_ACTIVE;
    return (actor->health>0 && item->type==ITEM_MIDAS_HAND && actor->state==Lara_STATE_USE_MIDAS) ||
        (actor->state==Lara_STATE_PICKUP &&
         (status==ITEM_FLAG_STATUS_INVISIBLE || (actor->health>0 && status==ITEM_FLAG_STATUS_INACTIVE)));
}
int game_save_blocked(void)
{
    if (!gs)return 0;
#if TR64_HAS_TR2
    if (gs->tr2) {
        if (gs->gLevelID == TR2_LVL_TITLE || gs->gLevelID == TR2_LVL_HOME ||
            !gs->players[0] || gs->players[0]->health <= 0 ||
            gs->tr2->deathTicks > 0 || gs->tr2->extraAnim || gs->tr2->startMenu)
            return 1;
        return 0;
    }
#endif
    if (gs->retireLevel)return 1;
    for (unsigned slot=0;slot<TR64_GAME_PLAYERS;++slot) {
        const Lara *actor=gs->players[slot];
        if (gs->retired[slot].actor ||
            (gs->players[1] && (slot || actor->health<=0) && game_interaction_busy(actor)))return 1;
    }
    return 0;
}
void game_interaction_triggered(ItemObj *item)
{
    for (unsigned slot=0;slot<TR64_GAME_PLAYERS;++slot)
        if (gs->interaction[slot]==item)gs->interaction[slot]=NULL;
}
void game_interaction_completed_level(Lara *actor)
{
    unsigned slot=Lara_slot(actor);
    ItemObj *item=gs->interaction[slot];
    if (item && owedScion(item))gs->interaction[slot]=NULL;
}
void game_retire_interaction(Lara *actor)
{
    unsigned slot=Lara_slot(actor);
    ItemObj *item=gs->interaction[slot];
    RetiredAction *owed=&gs->retired[slot];
    if (!item || owed->actor)return;
    if (!game_interaction_busy(actor)) {
        gs->interaction[slot]=NULL;
        return;
    }
    owed->actor=actor;
    if (owedScion(item)) {
        gs->retireLevel=true;
        owed->ticks=ItemObj_ticksToInteractionEvent(&gs->interactionAnim[slot],true);
    } else if (keyType(item->type)) {
        owed->ticks=ItemObj_ticksToInteractionEvent(&gs->interactionAnim[slot],false);
    }
    else if (actor->state==Lara_STATE_PICKUP)owed->ticks=1;
    else owed->ticks=INTERACTION_LIMIT;
}

bool game_settle_interaction(Lara *actor)
{
    unsigned slot=Lara_slot(actor);
    RetiredAction *owed=&gs->retired[slot];
    if (!owed->actor)return false;
    actor=owed->actor;
    ItemObj *item=gs->interaction[slot];
    if (!item) {
        *owed=(RetiredAction){0};
        return false;
    }
    if (owedScion(item)) {
        if (owed->ticks)--owed->ticks;
        return true; /* The resident tick applies the due request. */
    }
    if (owed->ticks)--owed->ticks;
    unsigned type=item->type, status=item->flags&ITEM_FLAG_STATUS, trigger;
    if (switchType(type)) {
        if (status==ITEM_FLAG_STATUS_ACTIVE && owed->ticks)return true;
        trigger=TRIGGER_TYPE_SWITCH;
    } else if (keyType(type)) {
        if (owed->ticks)return true;
        trigger=TRIGGER_TYPE_KEY;
        if (type>=ITEM_PUZZLEHOLE_1 && type<=ITEM_PUZZLEHOLE_4)
            item->type+=ITEM_PUZZLEHOLE_DONE_1-ITEM_PUZZLEHOLE_1;
    } else if (status==ITEM_FLAG_STATUS_INVISIBLE && actor->state==Lara_STATE_PICKUP) {
        trigger=TRIGGER_TYPE_PICKUP;
    } else {
        if (movingBlock(item) && owed->ticks)return true;
        *owed=(RetiredAction){0};
        gs->interaction[slot]=NULL;
        return false;
    }
    /* The corpse never moved to another sector. Read this actor's actual
     * floor and keep the normal interpreter (including local cameras). */
    const vec3i p=actor->pos;
    const FloorData *saved=gs->gLastFloorData;
    uint16 slant=gs->gLastFloorSlant;
    Sector_getFloor(Room_getSector(actor->room,p.x,p.z),p.x,p.y,p.z);
    const FloorData *fd=gs->gLastFloorData, *cmd=fd;
    gs->gLastFloorData=saved;
    gs->gLastFloorSlant=slant;
    *owed=(RetiredAction){0};
    gs->interaction[slot]=NULL;
    if (cmd && (tr64_u16(*cmd)&31)==FLOOR_TYPE_LAVA)
        cmd=(tr64_u16(*cmd)&0x8000) ? NULL : cmd+1;
    if (cmd && FD_TRIGGER_TYPE(tr64_u16(cmd[0]))==trigger &&
        FD_ARGS(tr64_u16(cmd[2]))==(unsigned)(item-gs->items)) {
        uint8 hands=actor->extraL->weaponState;
        if (trigger==TRIGGER_TYPE_KEY)actor->extraL->weaponState=WEAPON_STATE_FREE;
        game_check_trigger(fd,actor);
        actor->extraL->weaponState=hands;
    }
    return true; /* Render the authored camera on this tick before death animation. */
}
