/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/inventory.h, common.cpp and level_stats.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "inventory-private.h"
static int32 getTitleStr(const GameInventory *self)
{
    switch (self->page) {
    case INV_PAGE_MAIN: return STR_INV_TITLE_MAIN;
    case INV_PAGE_USE: case INV_PAGE_KEYS: return STR_INV_TITLE_KEYS;
    case INV_PAGE_OPTIONS: return STR_INV_TITLE_OPTIONS;
    default: return STR_EMPTY;
    }
}
static int32 getItemStr(const GameInventory *self)
{
    const InvItem *item=Inventory_item(self->itemsList[self->itemIndex]);
#if TR64_HAS_TR2
    if (TR64_RULE(FRESH_STOCK)) {
        if (self->state == INV_STATE_SHOW && item->type == TR2_RING_PASSPORT) {
            if (self->passportPage == PASSPORT_PAGE_LOAD_GAME) return STR_LOAD_GAME;
            if (self->passportPage == PASSPORT_PAGE_SAVE_GAME)
                return self->page == INV_PAGE_DEATH ? STR_RESTART_LEVEL :
                    (self->page == INV_PAGE_TITLE || gs->gLevelID == TR2_LVL_HOME)
                        ? STR_START_GAME : STR_SAVE_GAME;
            if (self->passportPage == PASSPORT_PAGE_EXIT_TO_TITLE)
                return self->page == INV_PAGE_TITLE ? STR_EXIT_GAME : STR_EXIT_TO_TITLE;
        }
        return item->str;
    }
#endif
    int32 type=item->type;
#define LVLCHECK(L,T,S) if (gs->gLevelID == (L) && type == ITEM_INV_##T) return (S)
    LVLCHECK(LVL_TR1_2,KEY_ITEM_1,STR_KEY_SILVER);
    LVLCHECK(LVL_TR1_2,PUZZLE_1,STR_PUZZLE_GOLD_IDOL);
    LVLCHECK(LVL_TR1_3A,PUZZLE_1,STR_PUZZLE_COG);
    LVLCHECK(LVL_TR1_4,KEY_ITEM_1,STR_KEY_NEPTUNE);
    LVLCHECK(LVL_TR1_4,KEY_ITEM_2,STR_KEY_ATLAS);
    LVLCHECK(LVL_TR1_4,KEY_ITEM_3,STR_KEY_DAMOCLES);
    LVLCHECK(LVL_TR1_4,KEY_ITEM_4,STR_KEY_THOR);
    LVLCHECK(LVL_TR1_5,KEY_ITEM_1,STR_KEY_RUSTY);
    LVLCHECK(LVL_TR1_6,PUZZLE_1,STR_PUZZLE_GOLD_BAR);
    LVLCHECK(LVL_TR1_7A,KEY_ITEM_1,STR_KEY_GOLD);
    LVLCHECK(LVL_TR1_7A,KEY_ITEM_2,STR_KEY_SILVER);
    LVLCHECK(LVL_TR1_7A,KEY_ITEM_3,STR_KEY_RUSTY);
    LVLCHECK(LVL_TR1_7B,KEY_ITEM_1,STR_KEY_GOLD);
    LVLCHECK(LVL_TR1_7B,KEY_ITEM_2,STR_KEY_RUSTY);
    LVLCHECK(LVL_TR1_7B,KEY_ITEM_3,STR_KEY_RUSTY);
    LVLCHECK(LVL_TR1_8A,KEY_ITEM_1,STR_KEY_SAPPHIRE);
    LVLCHECK(LVL_TR1_8B,KEY_ITEM_1,STR_KEY_SAPPHIRE);
    LVLCHECK(LVL_TR1_8B,PUZZLE_2,STR_PUZZLE_SCARAB);
    LVLCHECK(LVL_TR1_8B,PUZZLE_3,STR_PUZZLE_HORUS);
    LVLCHECK(LVL_TR1_8B,PUZZLE_4,STR_PUZZLE_ANKH);
    LVLCHECK(LVL_TR1_8B,PUZZLE_1,STR_PUZZLE_HORUS);
    LVLCHECK(LVL_TR1_8C,KEY_ITEM_1,STR_KEY_GOLD);
    LVLCHECK(LVL_TR1_8C,PUZZLE_1,STR_PUZZLE_ANKH);
    LVLCHECK(LVL_TR1_8C,PUZZLE_2,STR_PUZZLE_SCARAB);
    LVLCHECK(LVL_TR1_10A,PUZZLE_1,STR_PUZZLE_FUSE);
    LVLCHECK(LVL_TR1_10A,PUZZLE_2,STR_PUZZLE_PYRAMID);
    LVLCHECK(LVL_TR1_EGYPT,KEY_ITEM_1,STR_KEY_GOLD);
    LVLCHECK(LVL_TR1_CAT,KEY_ITEM_1,STR_KEY_ORNATE);
#undef LVLCHECK
    if (self->state == INV_STATE_SHOW && type == ITEM_INV_PASSPORT) {
        if (self->passportPage == PASSPORT_PAGE_LOAD_GAME) return STR_LOAD_GAME;
        if (self->passportPage == PASSPORT_PAGE_SAVE_GAME)
            return self->page == INV_PAGE_DEATH ? STR_RESTART_LEVEL :
                self->page == INV_PAGE_TITLE ? STR_START_GAME : STR_SAVE_GAME;
        if (self->passportPage == PASSPORT_PAGE_EXIT_TO_TITLE)
            return self->page == INV_PAGE_TITLE ? STR_EXIT_GAME : STR_EXIT_TO_TITLE;
    }
    return item->str;
}
static int32 actionHint(const GameInventory *self)
{
#if TR64_HAS_TR2
    if (TR64_RULE(FRESH_STOCK)) {
        if (self->state != INV_STATE_SHOW || self->frameIndex != self->frameTarget ||
            self->itemsCount <= 0 || self->itemsList[self->itemIndex] != TR2_RING_PASSPORT)
            return TR64_GAME_MENU_ACTION_NONE;
        if (self->passportPage == PASSPORT_PAGE_SAVE_GAME) {
            if (self->page == INV_PAGE_DEATH || self->page == INV_PAGE_TITLE ||
                gs->gLevelID == TR2_LVL_HOME ||
                (self->optionsCount > 0 && self->options[self->optionIndex].str == STR_OK))
                return TR64_GAME_MENU_ACTION_NONE;
            return TR64_GAME_MENU_ACTION_SAVE_SLOT;
        }
        if (self->passportPage == PASSPORT_PAGE_LOAD_GAME && self->optionsCount > 0 &&
            self->options[self->optionIndex].value == LVL_LOAD)
            return TR64_GAME_MENU_ACTION_LOAD_SLOT;
        return TR64_GAME_MENU_ACTION_NONE;
    }
#endif
    if(self->state!=INV_STATE_SHOW || self->frameIndex!=self->frameTarget ||
       self->itemsCount<=0 || self->itemsList[self->itemIndex]!=SLOT_PASSPORT)
        return TR64_GAME_MENU_ACTION_NONE;
    if(self->passportPage==PASSPORT_PAGE_SAVE_GAME) {
        if(self->page==INV_PAGE_DEATH || self->page==INV_PAGE_TITLE ||
           (self->optionsCount>0 && self->options[self->optionIndex].str==STR_OK))
            return TR64_GAME_MENU_ACTION_NONE;
        return TR64_GAME_MENU_ACTION_SAVE_SLOT;
    }
    if(self->passportPage==PASSPORT_PAGE_LOAD_GAME && self->optionsCount>0 &&
       self->options[self->optionIndex].value==LVL_LOAD)
        return TR64_GAME_MENU_ACTION_LOAD_SLOT;
    return TR64_GAME_MENU_ACTION_NONE;
}
void Inventory_view(GameInventory *self,GameInventoryCall *call)
{
    GameInventoryView *out=call->view;
#line 76
    ASSERT(out);
    memset(out,0,sizeof(*out));
    out->state=self->state;
    out->page=self->page;
    out->backgroundKind=self->backgroundKind;
    out->height=self->height;
    out->radius=self->radius;
    out->pitch=self->pitch;
    out->rot=self->rot;
    out->rotItem=self->rotItem;
    out->selDist=self->selDist;
    out->selRotPre=self->selRotPre;
    out->selRotX=self->selRotX;
    out->selRotY=self->selRotY;
    out->itemIndex=self->itemIndex;
    out->frameIndex=self->frameIndex;
    out->frameTarget=self->frameTarget;
    out->itemVisMask=self->itemVisMask;
    out->passportPage=self->passportPage;
    out->actionHint=actionHint(self);
    (void)game_stats_view(&out->stats);
    if (self->page == INV_PAGE_END || self->page == INV_PAGE_STATS) return;

    ASSERT(self->itemsCount >= 0 && self->itemsCount <= SLOT_MAX);
    out->itemsCount=self->itemsCount;
    out->optionIndex=self->optionIndex;
    out->optionsCount=self->optionsCount;
    out->optionsWidth=self->optionsWidth;
    out->optionsHeight=self->optionsHeight;
    ASSERT(self->optionsCount >= 0 && self->optionsCount <= 32);
    for (int32 i=0;i<self->optionsCount;i++) out->options[i]=self->options[i];
    if (!self->itemsCount) return;
    ASSERT(self->itemIndex >= 0 && self->itemIndex < self->itemsCount);
    for (int32 i=0;i<self->itemsCount;i++) {
        int32 slot=self->itemsList[i];
        int32 type=Inventory_item(slot)->type;
        bool current=self->itemIndex == i;
        bool selected=current && (self->state == INV_STATE_SHOW || self->frameTarget != self->frameIndex);
#if TR64_HAS_TR2
        if (TR64_RULE(FRESH_STOCK)) {
            if (type == TR2_RING_PASSPORT && selected) type=TR2_RING_PASSPORT_OPEN;
        } else
#endif
            if (type == ITEM_INV_PASSPORT && !selected) type=ITEM_INV_PASSPORT_CLOSED;
        int32 animIndex=gs->level.models[type].animIndex;
#if TR64_HAS_TR2
        if (TR64_RULE(PACKED_POSE) &&
            (gs->level.models[type].count<=0 || (unsigned)animIndex>=gs->tr2->animCount))
            sys_error("Tomb Raider II: missing inventory pose for type %ld",(long)type);
#endif
        out->items[i].slot=slot;
        out->items[i].type=type;
        out->items[i].animIndex=animIndex;
        out->items[i].frameIndex=tr64_u16(gs->level.anims[animIndex].frame_start)+(selected ? self->frameIndex : 0);
        out->items[i].visibleMask=self->itemVisMask;
    }
    out->titleID=getTitleStr(self);
    out->itemID=getItemStr(self);
    bool labels=self->state == INV_STATE_READY || self->state == INV_STATE_SPIN ||
        self->state == INV_STATE_SELECT || self->state == INV_STATE_DESELECT || self->state == INV_STATE_SHOW;
    if (labels) {
        out->showTitle=self->state != INV_STATE_SHOW && self->state != INV_STATE_SELECT && self->state != INV_STATE_DESELECT;
        if (out->showTitle) {
            out->showUp=self->page == INV_PAGE_OPTIONS || (self->page == INV_PAGE_MAIN && self->numKeys);
            out->showDown=self->page == INV_PAGE_MAIN || self->page == INV_PAGE_KEYS;
        }
        out->showItem=self->frameIndex == self->frameTarget && self->state != INV_STATE_SPIN;
#if TR64_HAS_TR2
        if (TR64_RULE(FRESH_STOCK)) {
            if (out->showItem && self->state == INV_STATE_SHOW &&
                self->itemsList[self->itemIndex] == TR2_RING_PASSPORT &&
                (self->page == INV_PAGE_OPTIONS || self->page == INV_PAGE_DEATH || self->page == INV_PAGE_TITLE)) {
                bool hasSave = eng_game_has_save && eng_game_has_save();
                out->showRight=self->passportPage == PASSPORT_PAGE_LOAD_GAME || self->passportPage == PASSPORT_PAGE_SAVE_GAME;
                out->showLeft=self->passportPage == PASSPORT_PAGE_EXIT_TO_TITLE ||
                              (self->passportPage == PASSPORT_PAGE_SAVE_GAME && hasSave);
            }
        } else
#endif
        if (out->showItem && self->state == INV_STATE_SHOW && self->itemsList[self->itemIndex] == SLOT_PASSPORT &&
            (self->page == INV_PAGE_OPTIONS || self->page == INV_PAGE_DEATH || self->page == INV_PAGE_TITLE)) {
            out->showRight=self->passportPage == PASSPORT_PAGE_LOAD_GAME || self->passportPage == PASSPORT_PAGE_SAVE_GAME;
            out->showLeft=self->passportPage == PASSPORT_PAGE_SAVE_GAME || self->passportPage == PASSPORT_PAGE_EXIT_TO_TITLE;
        }
    }
    if (self->state == INV_STATE_READY || self->state == INV_STATE_SHOW) {
        int32 slot=self->itemsList[self->itemIndex];
        out->showCount=inventory_countLabel(slot,&out->countValue,&out->countSuffix);
        out->showHealth=inventory_medikitHealth(self->lara,slot,true,&out->healthQ8);
    }
}
