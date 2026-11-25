/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/game.h and level.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"
#include "tr64/code.h"
#include "save.h"
#include "save_world.h"

const tr64_game_settings_t *game_settings_view(void)
{
    return gs ? &gs->gSettings : NULL;
}
int game_settings_apply(const tr64_game_settings_t *settings)
{
    if(!gs || !settings || (uint32)settings->video_gamma>31 ||
       (uint32)settings->video_fog>31 || (uint32)settings->video_fps>1 ||
       (uint32)settings->video_vsync>1 || (uint32)settings->audio_sfx>1 ||
       (uint32)settings->audio_music>1 || (uint32)settings->controls_vibration>1 ||
       (uint32)settings->controls_swap>1 || (uint32)settings->video_filter>1 ||
       (uint32)settings->video_auto_dist>1 ||
       (uint32)settings->controls_abilities>TR64_LARA_ABILITIES_TR3)return 0;
    gs->gSettings=*settings;
    game_lara_abilities_refresh();
    return 1;
}
int32_t game_level_id(void)
{
    return gs ? gs->gLevelID : -1;
}
void game_inventory_fading(int32 frames)
{
    int32 step;
    if(!gs->brightness)return;
    step=frames*16;
    if(gs->brightness<0) {
        gs->brightness+=step;
        if(gs->brightness>0)gs->brightness=0;
    }
    if(gs->brightness>0) {
        gs->brightness-=step;
        if(gs->brightness<0)gs->brightness=0;
    }
    /* level.h:374 palette application belongs to the engine presentation. */
}

static void inventoryCall(GameInventoryCall *call)
{
    code_call("inv",(uint32)(uintptr_t)call);
    if(gs->stop.kind!=TR64_GAME_STOP_NONE)return;
    uint32 actions=call->actions;
    if(actions&GAME_INV_ACTION_RESET_STATS) {
        gs->gSaveGame.time=gs->gSaveGame.distance=0;
        gs->gSaveGame.mediUsed=gs->gSaveGame.ammoUsed=gs->gSaveGame.kills=0;
        gs->gSaveGame.pickups=gs->gSaveGame.secrets=0;
#if TR64_HAS_TR2
        if(gs->tr2) {
            gs->tr2->arms.ammoUsed=0;
            gs->tr2->ammoHits=0;
        }
#endif
    }
    if(actions&GAME_INV_ACTION_NEXT_LEVEL)gs->pendingLevel=call->nextLevel;
    if(actions&GAME_INV_ACTION_VIBRATE) {
        if(!eng_game_vibrate) {
            if(!TR64_RULE(FRESH_STOCK)) {
                game_stop_set(TR64_GAME_STOP_NOT_PORTED,-1,"game vibration backend not ported");
                return;
            }
        } else {
            eng_game_vibrate();
        }
    }
    if(actions&GAME_INV_ACTION_SETTINGS) {
        if(!eng_game_save_settings) {
            if(!TR64_RULE(FRESH_STOCK)) {
                game_stop_set(TR64_GAME_STOP_NOT_PORTED,-1,"game settings backend not ported");
                return;
            }
        } else {
            eng_game_save_settings();
        }
    }
    if(actions&GAME_INV_ACTION_SAVE) {
        if(!eng_game_save) {
            game_stop_set(TR64_GAME_STOP_NOT_PORTED,-1,"game save storage backend not ported");
            return;
        }
        SaveGame before=gs->gSaveGame;
        int result=eng_game_save();
        if(result==TR64_ENG_SAVE_FAILED)gs->gSaveGame=before;
        if(gs->stop.kind!=TR64_GAME_STOP_NONE)return;
        call->op=GAME_INV_SAVE_RESULT;
        call->saveResult=result;
        code_call("inv",(uint32)(uintptr_t)call);
    }
}
void game_inventory_open(Lara *lara,int32 page,int32 arg)
{
    GameInventoryCall call={0};
    call.op=GAME_INV_OPEN;
    call.lara=lara;
    call.page=page;
    call.arg=arg;
    inventoryCall(&call);
}
void game_inventory_update(int32 frames)
{
    GameInventoryCall call={0};
    call.op=GAME_INV_UPDATE;
    call.frames=frames;
    inventoryCall(&call);
}
#if TR64_HAS_TR2
bool game_tr2_ring_open(bool key_ring)
{
    if (!gs || !TR64_RULE(FRESH_STOCK) || gs->inventory.state!=INV_STATE_NONE)
        sys_error("Tomb Raider II: invalid inventory open");
    int32 page;
    if (gs->stop.kind==TR64_GAME_STOP_DEATH) {
        memset(&gs->stop,0,sizeof(gs->stop));
        gs->stop.next_level_id=-1;
        page=INV_PAGE_DEATH;
    } else if (gs->stop.kind==TR64_GAME_STOP_LEVEL_END && gs->tr2->finishRequested) {
        memset(&gs->stop,0,sizeof(gs->stop));
        gs->stop.next_level_id=-1;
        gs->tr2->finishRequested=false;
        gs->inventory.statsNextLevel=game_tr2_level_script_next_id();
        page=INV_PAGE_STATS;
    } else if (gs->gLevelID==TR2_LVL_TITLE || gs->tr2->startMenu) {
        page=INV_PAGE_TITLE;
    } else {
        if (gs->tr2->deathTicks>0 || gs->tr2->extraAnim) return false;
        if (key_ring && !game_tr2_ring_has_keys()) return false;
        page=key_ring ? INV_PAGE_USE : INV_PAGE_MAIN;
    }
    code_loading_begin();
    if (!code_is_loaded("inv")) code_load("inv");
    code_loading_end();
    Lara *lara=gs->players[0] ? gs->players[0] : gs->items;
    gs->tr2->ringInput=lara->input;
    gs->tr2->ringPreviousInput=lara->extraL->lastInput;
    lara->input=lara->extraL->lastInput=0;
    gs->pendingLevel=LVL_MAX;
    gs->inventory.optionsCount=0;
    gs->inventory.selDist=gs->inventory.selDistTarget=0;
    gs->inventory.selRotPre=gs->inventory.selRotPreTarget=0;
    gs->inventory.selRotX=gs->inventory.selRotXTarget=0;
    gs->inventory.selRotY=gs->inventory.selRotYTarget=0;
    gs->inventory.frameTarget=0;
    game_inventory_open(lara,page,0);
    return true;
}

bool game_tr2_ring_update(uint16_t input)
{
    if (gs->inventory.state==INV_STATE_NONE)
        sys_error("Tomb Raider II: inventory update when closed");
    Lara *lara=gs->inventory.lara;
    lara->extraL->lastInput=lara->input;
    lara->input=input;
    game_inventory_update(1);
    if (gs->inventory.state!=INV_STATE_NONE && gs->stop.kind==TR64_GAME_STOP_NONE) return true;
    gs->inventory.state=INV_STATE_NONE;
    /* Lock contact must finish with the original gameplay input/edges. */
    lara->input=gs->tr2->ringInput;
    lara->extraL->lastInput=gs->tr2->ringPreviousInput;
    code_loading_begin();
    if (code_is_loaded("inv")) code_unload("inv");
    code_loading_end();
    if (gs->pendingLevel!=LVL_MAX && gs->stop.kind==TR64_GAME_STOP_NONE) {
        int32 target=gs->pendingLevel;
        gs->pendingLevel=LVL_MAX;
        game_stop_set(TR64_GAME_STOP_LEVEL_END,target,"Tomb Raider II: menu transition");
        gs->stop.transition = target==LVL_LOAD ? TR64_GAME_TRANSITION_LOAD :
            target==TR2_LVL_TITLE ? TR64_GAME_TRANSITION_TITLE :
            target==TR2_LVL_HOME ? TR64_GAME_TRANSITION_GYM :
            target==TR2_LVL_NEW_GAME ? TR64_GAME_TRANSITION_NEW_GAME :
            target==gs->gLevelID ? TR64_GAME_TRANSITION_RESTART :
            TR64_GAME_TRANSITION_CAMPAIGN;
    }
    return false;
}

unsigned game_tr2_ring_choice(void)
{
    if (gs->inventory.state!=INV_STATE_NONE)
        sys_error("Tomb Raider II: inventory choice before closing");
    return gs->inventory.useSlot==SLOT_MAX ? 0 : (unsigned)gs->inventory.useSlot;
}

static bool tr2SaveUnitBegin(void)
{
    bool loaded=code_is_loaded("save");
    if (!loaded) {
        code_loading_begin();
        if (code_is_loaded("front")) code_unload("front");
        code_load("save");
        code_loading_end();
    }
    return !loaded;
}

static void tr2SaveUnitEnd(bool leased)
{
    if (!leased) return;
    code_loading_begin();
    if (code_is_loaded("save")) code_unload("save");
    code_loading_end();
}
#endif
void game_inventory_queue_open(Lara *lara,int32 page,int32 arg)
{
    if(gs->stop.kind!=TR64_GAME_STOP_NONE)return;
    if(gs->inventoryOpenPending)sys_error("game inventory duplicate queued open");
    gs->inventoryOpenPending=true;
    gs->inventoryOpenLara=lara;
    gs->inventoryOpenPage=page;
    gs->inventoryOpenArg=arg;
}
void game_inventory_flush_open(void)
{
    if(!gs->inventoryOpenPending || gs->stop.kind!=TR64_GAME_STOP_NONE)return;
    Lara *lara=gs->inventoryOpenLara;
    int32 page=gs->inventoryOpenPage,arg=gs->inventoryOpenArg;
    gs->inventoryOpenPending=false;
    gs->inventoryOpenLara=NULL;
    game_inventory_open(lara,page,arg);
}

uint32_t game_save(void *buffer,uint32_t capacity)
{
    GameSaveCall call={0};
    /* V3 records neither a pending authored action nor its remaining ticks. */
    if(!gs || game_save_blocked())return 0;
    call.op=buffer && capacity ? GAME_SAVE_WRITE : GAME_SAVE_SIZE;
    call.buffer=buffer;
    call.bytes=capacity;
#if TR64_HAS_TR2
    if(gs->tr2) {
        bool leased=tr2SaveUnitBegin();
        code_call("save",(uint32)(uintptr_t)&call);
        tr2SaveUnitEnd(leased);
        return call.result;
    }
#endif
    code_call("save",(uint32)(uintptr_t)&call);
    return call.result;
}
int game_load(const void *buffer,uint32_t bytes)
{
    GameSaveCall call={0};
    if(!gs)return 0;
#if TR64_HAS_TR2
    if(gs->tr2) {
        call.op=GAME_SAVE_LOAD;
        call.buffer=(void *)buffer;
        call.bytes=bytes;
        call.phase=GAME_SAVE_CHECK;
        bool leased=tr2SaveUnitBegin();
        code_call("save",(uint32)(uintptr_t)&call);
        tr2SaveUnitEnd(leased);
        if(!call.result)return 0;
        bool savedFlipped=call.result==2u;
        game_prepare_tr2_load();
        if(savedFlipped)game_world_flip(true);
        call.phase=GAME_SAVE_APPLY;
        leased=tr2SaveUnitBegin();
        code_call("save",(uint32)(uintptr_t)&call);
        tr2SaveUnitEnd(leased);
        if(!call.result)sys_error("Tomb Raider II: validated save reconstruction failed");
        game_tr2_post_restore();
        return 1;
    }
#endif
#if !TR64_CART_TR2
    GameSavePlan plan;
    call.op=GAME_SAVE_LOAD;
    call.buffer=(void *)buffer;
    call.bytes=bytes;
    call.phase=GAME_SAVE_CHECK;
    call.plan=&plan;
    code_call("save",(uint32)(uintptr_t)&call);
    if(!call.result || !game_save_validate_world(buffer,bytes,&plan))return 0;
    game_prepare_load();
    call.phase=GAME_SAVE_APPLY;
    code_call("save",(uint32)(uintptr_t)&call);
    if(!call.result || !game_save_rebuild_world() || !encounterRebuildListsV3())
        sys_error("game validated save reconstruction failed");
    uint16 used=encounterPreparedItemCount();
    for(uint16 i=0;i<used;++i)
        if(gs->items[i].flags&ITEM_FLAG_ACTIVE)
            encounterActivateLoadedItem(gs->items+i);
    call.phase=GAME_SAVE_FINISH;
    code_call("save",(uint32)(uintptr_t)&call);
    if(!call.result)sys_error("game validated save finalization failed");
    game_refresh_join_pose(gs->players[0]);
    ++gs->musicSerial;
    gs->musicReason=TR64_GAME_MUSIC_RESTORE;
    gs->inventory.page=INV_PAGE_MAIN;
    gs->inventory.state=INV_STATE_NONE;
    return 1;
#else
    return 0;
#endif
}

void game_complete_level(Lara *lara)
{
    if (gs->players[1] && (gs->pendingLevel!=LVL_MAX || gs->inventory.state!=INV_STATE_NONE))
        return;
    if (lara && gs->players[1])game_interaction_completed_level(lara);
    int32 next=game_next_level_id(),current=gs->gLevelID;
    if(lara && current!=LVL_TR1_TITLE && current!=LVL_TR1_GYM &&
       !game_is_cutscene_level(current) && next!=LVL_TR1_TITLE && next!=LVL_MAX &&
       !game_is_cutscene_level(next)) {
        gs->inventory.statsNextLevel=next;
        game_inventory_open(gs->players[1] ? gs->players[0] : lara,INV_PAGE_STATS,0);
    } else {
        gs->pendingLevel=next;
    }
}
