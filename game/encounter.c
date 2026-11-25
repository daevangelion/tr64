/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/encounter_spawn.h and encounter_spawners.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"
#include "enemy.h"

enum { ENCOUNTER_CHILD_NONE=0xffff, CENTAUR_STATUE_CHILD_ANIM=7, CENTAUR_STATUE_CHILD_FRAME=36 };
typedef struct {
    uint16 sourceIndex,childIndex;
    ItemType childType;
} EncounterLayoutRow;

static ItemType encounterSelectChild(ItemType sourceType,uint16 sourceFlags)
{
    if(sourceType==ITEM_CENTAUR_STATUE)return ITEM_CENTAUR;
    if(sourceType==ITEM_HAMMER_HANDLE)return ITEM_HAMMER_BLOCK;
    if(sourceType==ITEM_FLAME_EMITTER)return ITEM_FLAME;
    switch((sourceFlags>>9)&31) {
    case 1:return ITEM_MUTANT_2;
    case 2:return ITEM_CENTAUR;
    case 4:return ITEM_ADAM;
    case 8:return ITEM_MUTANT_3;
    default:return ITEM_MUTANT_1;
    }
}

static bool encounterCentaurStatueAnimationFits(void)
{
    const Model *model=gs->level.models+ITEM_CENTAUR;
    uint32 animIndex=(uint32)model->animIndex+CENTAUR_STATUE_CHILD_ANIM;
    if(!model->count || model->animIndex==0xffff ||
       animIndex>=tr64_level_table(gs->core,TR64_CORE_animations)->count)return false;
    const Anim *anim=gs->level.anims+animIndex;
    uint32 frame=(uint32)tr64_u16(anim->frame_start)+CENTAUR_STATUE_CHILD_FRAME;
    return frame<=0xffff && frame<=tr64_u16(anim->frame_end);
}

bool encounterResolvePreparedChild(const ItemObj *source,ItemType *expectedType,uint16 *expectedIndex)
{
    if(source<gs->items || source>=gs->items+gs->level.itemsCount)return false;
    uint16 sourceIndex=(uint16)(source-gs->items);
    const tr64_core_items_t *info=tr64_level_items(gs->core,sourceIndex);
    if(!encounterIsSource(tr64_u16(info->type)))return false;
    uint16 ordinal=0;
    for(uint16 i=0;i<sourceIndex;i++)
        if(encounterIsSource(tr64_u16(tr64_level_items(gs->core,i)->type)))ordinal++;
    *expectedIndex=(uint16)(gs->level.itemsCount+ordinal);
    *expectedType=encounterSelectChild(tr64_u16(info->type),tr64_u16(info->flags));
    return *expectedIndex<MAX_ITEMS;
}

bool encounterPrecreateChildren(void)
{
    EncounterLayoutRow rows[MAX_ITEMS];
    uint32 rowCount=0;
    for(uint32 i=0;i<gs->level.itemsCount;i++) {
        const tr64_core_items_t *info=tr64_level_items(gs->core,i);
        if(!encounterIsSource(tr64_u16(info->type)))continue;
        if(gs->level.itemsCount+rowCount>=MAX_ITEMS)return false;
        rows[rowCount].sourceIndex=(uint16)i;
        rows[rowCount].childIndex=(uint16)(gs->level.itemsCount+rowCount);
        rows[rowCount].childType=encounterSelectChild(tr64_u16(info->type),tr64_u16(info->flags));
        rowCount++;
    }
    for(uint32 i=0;i<rowCount;i++)
        if(gs->items[rows[i].sourceIndex].type==ITEM_FLAME_EMITTER)
            gs->items[rows[i].sourceIndex].headOffset=ENCOUNTER_CHILD_NONE;
    ItemObj *freeItem=gs->firstFree;
    for(uint32 i=0;i<rowCount;i++) {
        const EncounterLayoutRow *row=rows+i;
        const ItemObj *source=gs->items+row->sourceIndex;
        if(source->type!=tr64_u16(tr64_level_items(gs->core,row->sourceIndex)->type) ||
           !source->room || source->headOffset!=ENCOUNTER_CHILD_NONE ||
           freeItem!=gs->items+row->childIndex ||
           (source->type==ITEM_CENTAUR_STATUE && !encounterCentaurStatueAnimationFits()))return false;
        freeItem=freeItem->nextItem;
    }
    for(uint32 i=0;i<rowCount;i++) {
        const EncounterLayoutRow *row=rows+i;
        ItemObj *source=gs->items+row->sourceIndex;
        ItemObj *slot=gs->firstFree;
        if(!slot)return false;
        ItemObj *nextFree=slot->nextItem;
        ItemObj_resetSlot(slot);
        slot->nextItem=nextFree;
        uint32 savedLogicSeed=gs->gRandSeedLogic;
        ItemObj *child=ItemObj_add(row->childType,source->room,&source->pos,source->angle.y);
        gs->gRandSeedLogic=savedLogicSeed;
        if(!child || child!=gs->items+row->childIndex || child->type!=row->childType)return false;
        child->pos=source->pos;
        child->angle=source->angle;
        child->flags=(child->flags&~(ITEM_FLAG_ACTIVE|ITEM_FLAG_STATUS))|ITEM_FLAG_STATUS_INACTIVE;
        child->extraE=NULL;
        child->nextActive=NULL;
        if(source->type==ITEM_FLAME_EMITTER)child->headOffset=row->sourceIndex;
        if(source->type==ITEM_CENTAUR_STATUE) {
            if(child->type!=ITEM_CENTAUR || !encounterCentaurStatueAnimationFits())return false;
            child->animIndex=gs->level.models[ITEM_CENTAUR].animIndex+CENTAUR_STATUE_CHILD_ANIM;
            const Anim *anim=gs->level.anims+child->animIndex;
            child->frameIndex=tr64_u16(anim->frame_start)+CENTAUR_STATUE_CHILD_FRAME;
            child->state=(uint8)tr64_u16(anim->state);
            child->nextState=child->goalState=child->state;
        }
        source->headOffset=row->childIndex;
        source->flags&=~ITEM_FLAG_MASK;
    }
    return true;
}

void Encounter_construct(ItemObj *self)
{
    self->headOffset=ENCOUNTER_CHILD_NONE;
    self->timer=0;
    if(self->type==ITEM_MUTANT_EGG_SMALL || self->type==ITEM_MUTANT_EGG_BIG)
        self->visibleMask=0xff0001ffu;
    if(!(self->flags&ITEM_FLAG_ACTIVE))ItemObj_activateBase(self);
}

void Encounter_activate(ItemObj *self)
{
    self->flags|=ITEM_FLAG_ONCE;
    if(!(self->flags&ITEM_FLAG_ACTIVE))ItemObj_activateBase(self);
}

uint16 encounterPreparedItemCount(void)
{
    uint32 used=gs->level.itemsCount;
    for(uint32 i=0;i<gs->level.itemsCount;++i)
        if(encounterIsSource(tr64_u16(tr64_level_items(gs->core,i)->type)))
            ++used;
    return used<=MAX_ITEMS ? (uint16)used : 0;
}
bool encounterRebuildListsV3(void)
{
    uint16 used=encounterPreparedItemCount();
    if(used<gs->level.itemsCount || used>MAX_ITEMS)return false;
    gs->firstActive=NULL;
    for(uint16 i=0;i<used;++i)gs->items[i].nextActive=NULL;
    for(uint16 i=used;i<MAX_ITEMS;++i) {
        gs->items[i].room=NULL;
        gs->items[i].nextActive=NULL;
        gs->items[i].nextItem=i+1<MAX_ITEMS ? gs->items+i+1 : NULL;
    }
    gs->firstFree=used<MAX_ITEMS ? gs->items+used : NULL;
    return true;
}
void encounterActivateLoadedItem(ItemObj *item)
{
    if(encounterIsSource(item->type))ItemObj_activateBase(item);
    else ItemObj_activate(item);
}
