/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h and src/fixed/nav.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "creature.h"
#include "objects.h"

#include "../enemy.h"

/* tr2-great-wall-creatures: Creature rules, Admission outcomes, Mood
 * transitions, Tiger, Small and giant spiders, Crow. */

static int16 creatureSector(Room **room, const vec3i *p, int32 height)
{
    uint16 n=game_tr2_room_sector(room,p->x,height,p->z)->boxIndex;
    return n==UINT16_MAX ? -1 : n;
}

static int16 itemBox(const ItemObj *item)
{
    Room *room=item->room;
    return creatureSector(&room,&item->pos,item->pos.y);
}

static int64_t squared(int64_t n) { return n*n; }

static int64_t cameraDistance(const ItemObj *item)
{
    const vec3i *camera=&gs->playersExtra[0].camera.view.pos, *p=&item->pos;
    return squared(((int64_t)p->x-camera->x)>>8) + squared(((int64_t)p->y-camera->y)>>8) +
           squared(((int64_t)p->z-camera->z)>>8);
}


bool game_tr2_creature_wake(ItemObj *item, bool force)
{
    if (game_tr2_creature_slot(item)) return true;
    int16 box=itemBox(item);
    if (box<0)
        sys_error("Tomb Raider II: creature type %u has no navigation box at wake",item->type);
    TR2Creature *slot=NULL;
    for (unsigned i=0;i<TR2_CREATURE_SLOTS;++i)
        if (!gs->tr2->creatures[i].item) { slot=gs->tr2->creatures+i; break; }
    if (!slot) {
        int64_t farthest=force ? 0 : cameraDistance(item);
        for (unsigned i=0;i<TR2_CREATURE_SLOTS;++i) {
            TR2Creature *probe=gs->tr2->creatures+i;
            int64_t distance=cameraDistance(probe->item);
            if (distance>farthest) { farthest=distance; slot=probe; }
        }
        if (!slot) return false;
        slot->item->flags=(slot->item->flags&~ITEM_FLAG_STATUS)|ITEM_FLAG_STATUS_INVISIBLE;
    }
    TR2RouteNode *route=slot->route;
    *slot=(TR2Creature){.route=route,.item=item,.maxTurn=182,.blockMask=BOX_OVERLAP_BLOCKED};
    switch (item->type) {
    case TR2_ITEM_TIGER: slot->step=256; slot->drop=-512; break;
    case TR2_ITEM_REX: slot->step=256; slot->drop=-512; slot->blockMask=BOX_OVERLAP_BLOCKABLE; break;
    case TR2_ITEM_SPIDER: slot->step=512; slot->drop=-1024; break;
    case TR2_ITEM_CROW: slot->step=20480; slot->drop=-20480; slot->fly=16; break;
    case TR2_ITEM_DOBERMAN:
    case TR2_ITEM_GUNMAN_1: case TR2_ITEM_GUNMAN_2: case TR2_ITEM_GUNMAN_3:
    case TR2_ITEM_RAT:
    case TR2_ITEM_THUG:
        slot->step=256; slot->drop=-512; break;
    case TR2_ITEM_OPERA_BOSS:
        slot->step=256; slot->drop=-512; slot->maxTurn=0; break;
    default: sys_error("Tomb Raider II: unknown creature species %u",item->type);
    }
    game_tr2_creature_nav_init(slot,box);
    return true;
}

static Interest interest(TR2Creature *ai,Lara *lara)
{
    ItemObj *item=ai->item;
    const int pivot=item->type==TR2_ITEM_TIGER ? 200 : item->type==TR2_ITEM_REX ? 1800 :
                    item->type==TR2_ITEM_DOBERMAN ? 300 :
                    (item->type>=TR2_ITEM_GUNMAN_1 && item->type<=TR2_ITEM_GUNMAN_3) ? 50 :
                    item->type==TR2_ITEM_RAT ? 50 :
                    0;
    vec3i front=game_tr2_heading_step(item->angle.y,pivot);
    int32 x=i32_sub(i32_sub(lara->pos.x,item->pos.x),front.x);
    int32 z=i32_sub(i32_sub(lara->pos.z,item->pos.z),front.z);
    int16 angle=wrap_i16(game_tr2_bearing(z,x)-item->angle.y);
    int16 box=itemBox(lara);
    const tr64_u16_be_t *zones=game_tr2_creature_zones(ai,gs->gSaveGame.flipped);
    bool same=box>=0 && ai->box>=0 && zones[ai->box].raw==zones[box].raw &&
        !(gs->boxFlags[box]&ai->blockMask) && ai->route[ai->box].generation!=(ai->search|0x8000);
    bool facing=abs(wrap_i16((int32)angle+item->angle.y+ANGLE_180-lara->angle.y))<ANGLE_90;
    return (Interest){(uint64_t)squared(x)+(uint64_t)squared(z),angle,box,same,abs(angle)<ANGLE_90,facing};
}

static unsigned quadrant(int64_t x,int64_t z)
{
    return x>0 ? (z>0 ? 2 : 3) : z>0;
}

static bool attractive(const ItemObj *item,const Lara *lara,int box,bool escape)
{
    const tr64_core_boxes_t *node=gs->level.boxes+box;
    int32 x0=tr64_i32(node->min_x),x1=tr64_i32(node->max_x),z0=tr64_i32(node->min_z),z1=tr64_i32(node->max_z);
    int64_t x=(int64_t)x0+(x1-x0)/2-lara->pos.x,z=(int64_t)z0+(z1-z0)/2-lara->pos.z;
    int64_t ix=(int64_t)item->pos.x-lara->pos.x,iz=(int64_t)item->pos.z-lara->pos.z;
    if (escape)
        return (llabs(x)>=5120 || llabs(z)>=5120) && ((x>0)==(ix>0) || (z>0)==(iz>0));
    unsigned boxSide=quadrant(x,z), selfSide=quadrant(ix,iz), facing=(lara->angle.y>>14)+2;
    return llabs(x)<=3072+x1-x0 && llabs(z)<=3072+z1-z0 && boxSide!=facing &&
           !(selfSide==facing && boxSide==(facing^2));
}

static bool viable(const TR2Creature *ai,int box,const tr64_u16_be_t *zones)
{
    if (box<0 || ai->box<0 || zones[box].raw!=zones[ai->box].raw || gs->boxFlags[box]&ai->blockMask)
        return false;
    const tr64_core_boxes_t *b=gs->level.boxes+box;
    const vec3i *p=&ai->item->pos;
    return p->x<=tr64_i32(b->min_x) || p->x>=tr64_i32(b->max_x)-1 ||
           p->z<=tr64_i32(b->min_z) || p->z>=tr64_i32(b->max_z)-1;
}

static void chooseMood(TR2Creature *ai,const Lara *lara,const Interest *p)
{
    bool violent=ai->item->type==TR2_ITEM_TIGER || ai->item->type==TR2_ITEM_REX ||
                 ai->item->type==TR2_ITEM_THUG || ai->item->type==TR2_ITEM_OPERA_BOSS;
    bool hit=(ai->item->flags&ITEM_FLAG_INJURED)!=0;
    unsigned before=ai->mood;
    const tr64_u16_be_t *zones=game_tr2_creature_zones(ai,gs->gSaveGame.flipped);
    if (ai->box>=0 && ai->route[ai->box].generation==(ai->search|0x8000)) ai->requiredBox=-1;
    if (before!=ATTACK && ai->targetBox>=0 && ai->requiredBox>=0 && !viable(ai,ai->targetBox,zones)) {
        ai->requiredBox=-1;
        if (p->sameZone) ai->mood=BORED;
    }
    if (lara->health<=0) ai->mood=BORED;
    else if (violent) switch (ai->mood) {
    case BORED: case STALK:
        if (p->sameZone) ai->mood=ATTACK;
        else if (hit) ai->mood=ESCAPE;
        break;
    case ATTACK: if (!p->sameZone) ai->mood=BORED; break;
    case ESCAPE: if (p->sameZone) ai->mood=ATTACK; break;
    }
    else switch (ai->mood) {
    case BORED: case STALK:
        if (hit && (rand_logic()<2048 || !p->sameZone)) ai->mood=ESCAPE;
        else if (p->sameZone) ai->mood=p->distance<3072*3072 || (ai->mood==STALK && ai->requiredBox<0) ? ATTACK : STALK;
        break;
    case ATTACK:
        if (hit && (rand_logic()<2048 || !p->sameZone)) ai->mood=ESCAPE;
        else if (!p->sameZone) ai->mood=BORED;
        break;
    case ESCAPE:
        if (p->sameZone && rand_logic()<256) ai->mood=STALK;
        break;
    }
    if (before!=ai->mood) {
        if (before==ATTACK && ai->targetBox>=0) game_tr2_creature_box_destination(ai,ai->targetBox);
        ai->requiredBox=-1;
    }
}

static void chooseDestination(TR2Creature *ai,const Lara *lara,const Interest *p)
{
    const tr64_u16_be_t *zones=game_tr2_creature_zones(ai,gs->gSaveGame.flipped);
    if (ai->mood==ATTACK) {
        ai->requiredBox=p->box;
        ai->destination[0]=lara->pos.x;
        ai->destination[1]=i32_add(lara->pos.y,(ai->fly && lara->waterState==WATER_STATE_ABOVE) ?
                                   ItemObj_getBoundingBox(lara,false)->minY : 0);
        ai->destination[2]=lara->pos.z;
    } else if (ai->zoneCount && !(ai->mood==STALK && ai->requiredBox>=0 && attractive(ai->item,lara,ai->requiredBox,false))) {
        int box=ai->route[(rand_logic()*ai->zoneCount)>>15].candidate;
        if (viable(ai,box,zones)) {
            bool stalk=lara->health>0 && attractive(ai->item,lara,box,false);
            switch (ai->mood) {
            case BORED:
                if (stalk || ai->requiredBox<0) game_tr2_creature_box_destination(ai,box);
                if (stalk) ai->mood=STALK;
                break;
            case STALK:
                if (stalk || ai->requiredBox<0) {
                    game_tr2_creature_box_destination(ai,box);
                    if (!stalk && !p->sameZone) ai->mood=BORED;
                }
                break;
            case ESCAPE:
                /* Draw the candidate above, but keep a pending request
                 * before either choice (OpenLara src/fixed/enemy.h, moodEscape). */
                if (ai->requiredBox>=0) break;
                if (attractive(ai->item,lara,box,true))
                    game_tr2_creature_box_destination(ai,box);
                else if (p->sameZone && stalk) {
                    game_tr2_creature_box_destination(ai,box);
                    ai->mood=STALK;
                }
                break;
            }
        }
    }
    if (ai->targetBox<0 && ai->box>=0) game_tr2_creature_box_destination(ai,ai->box);
    game_tr2_creature_waypoint(ai);
}

static int16 turnToward(TR2Creature *ai)
{
    ItemObj *item=ai->item;
    /* The spider and crow pass fixed caps; only the tiger changes
     * the cap stored in its brain (Great Wall creatures, species table). */
    int32 cap=item->type==TR2_ITEM_SPIDER ? 1456 : item->type==TR2_ITEM_CROW ? 546 : ai->maxTurn;
    if (!item->hSpeed || !cap) return 0;
    int32 dx=i32_sub(ai->waypoint[0],item->pos.x), dz=i32_sub(ai->waypoint[2],item->pos.z);
    int16 diff=wrap_i16(game_tr2_bearing(dz,dx)-item->angle.y);
    if (abs(diff)>ANGLE_90 && (uint64_t)squared(dx)+(uint64_t)squared(dz)<
        (uint64_t)squared(item->hSpeed*ANGLE_90/cap)) cap/=2;
    return X_MAX(-cap,X_MIN(cap,diff));
}

static void retireCreature(TR2Creature *ai)
{
    ItemObj *item=ai->item;
    ItemObj_deactivateBase(item);
    item->flags=(item->flags&~ITEM_FLAG_COLLISION)|TR2_FLAG_RETIRED;
    item->health=-16384;
    ai->item=NULL;
    game_tr2_urban_cargo_release(item);
    unsigned slot=(unsigned)(item-gs->items);
    if (slot<gs->level.itemsCount &&
        (gs->tr2->cleanupEligible[slot>>5]&(1u<<(slot&31))))
        gs->tr2->cleanupList[slot>>5]|=1u<<(slot&31);
}

uint32 game_tr2_ai_prepare(TR2AiCall *call)
{
    ItemObj *item=call->self;
    Lara *lara=call->lara;
    TR2Creature *ai=game_tr2_creature_slot(item);
    if (!ai) {
        if (!game_tr2_creature_wake(item,false)) return TR2_AI_RES_SKIP;
        ai=game_tr2_creature_slot(item);
        item->flags=(item->flags&~ITEM_FLAG_STATUS)|ITEM_FLAG_STATUS_ACTIVE;
    }
    call->ai=ai;
    if (item->health<=0) {
        item->flags&=~ITEM_FLAG_INJURED;
        return TR2_AI_RES_DEAD;
    }
    ai->box=itemBox(item);
    ai->enemy=lara;
    Interest p=interest(ai,lara);
    chooseMood(ai,lara,&p);
    chooseDestination(ai,lara,&p);
    const int16 turn=turnToward(ai);
    item->angle.y=wrap_i16((int32)item->angle.y+turn);
    call->interest=p;
    call->turn=turn;
    return TR2_AI_RES_ALIVE;
}

void game_tr2_ai_stride(TR2Creature *ai, int16 turn)
{
    ItemObj *item=ai->item;
    item->flags&=~ITEM_FLAG_INJURED;
    /* One mask per contact pass; only the preceding mask is an input. */
    item->hitMask=0;
    game_tr2_creature_stride(ai,turn);
}

void game_tr2_ai_death_stride(TR2Creature *ai, int16 turn)
{
    ItemObj *item=ai->item;
    game_tr2_creature_stride(ai,turn);
    if ((item->flags&ITEM_FLAG_STATUS)==ITEM_FLAG_STATUS_INACTIVE)
        retireCreature(ai);
}
