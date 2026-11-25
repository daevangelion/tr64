/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/enemy.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "game/common/internal.h"
#include "game/common/enemy.h"

/* Shared lifecycle must be resident: activation can be called while a
 * different unit is running; common enemy death also deactivates the board. */
ItemObj *Skater_getBoard(ItemObj *self)
{
    for(ItemObj *item=self->room->firstItem;item;item=item->nextItem)
        if(item->type==ITEM_SKATEBOARD && (void*)item->extraL==(void*)self)return item;
    for(ItemObj *item=gs->firstActive;item;item=item->nextActive)
        if(item->type==ITEM_SKATEBOARD && (void*)item->extraL==(void*)self)return item;
    return NULL;
}

ItemObj *Skater_ensureBoard(ItemObj *self)
{
    ItemObj *board=Skater_getBoard(self);
    if(board)return board;
    board=ItemObj_add(ITEM_SKATEBOARD,self->room,&self->pos,self->angle.y);
    if(!board)return NULL;
    board->extraL=(ExtraInfoLara*)self;
    board->flags&=~ITEM_FLAG_COLLISION;
    ItemObj_activateBase(board);
    return board;
}

void Skater_syncBoard(ItemObj *self,ItemObj *board)
{
    if(!board)return;
    if(board->room!=self->room) {
        Room_remove(board->room,board);
        Room_add(self->room,board);
    }
    int32 animOffset=self->animIndex-gs->level.models[self->type].animIndex;
    int32 boardAnim=gs->level.models[ITEM_SKATEBOARD].animIndex+animOffset;
    int32 frameOffset=self->frameIndex-tr64_u16(gs->level.anims[self->animIndex].frame_start);
    board->pos=self->pos;
    board->angle=self->angle;
    board->animIndex=(uint16)boardAnim;
    board->frameIndex=(uint16)(tr64_u16(gs->level.anims[boardAnim].frame_start)+frameOffset);
    board->state=self->state;
    board->goalState=self->goalState;
}

void Skater_activate(ItemObj *self)
{
    Enemy_activate(self);
    if(!(self->flags&ITEM_FLAG_ACTIVE))return;
    ItemObj *board=Skater_ensureBoard(self);
    if(!board)return;
    board->flags&=~ITEM_FLAG_STATUS;
    if(!(board->flags&ITEM_FLAG_ACTIVE))ItemObj_activateBase(board);
    Skater_syncBoard(self,board);
}

void Skater_deactivate(ItemObj *self)
{
    ItemObj *board=Skater_getBoard(self);
    if(board) {
        ItemObj_deactivateBase(board);
        board->flags=(board->flags&~ITEM_FLAG_STATUS)|ITEM_FLAG_STATUS_INVISIBLE;
    }
    ItemObj_deactivateBase(self);
}
