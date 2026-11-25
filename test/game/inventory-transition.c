/* SPDX-License-Identifier: MIT */
/* Real Core/game/renderer release and restart through the normal engine build. */
#include <string.h>
#include "game/common/internal.h"
#include "tr64/eng.h"
#include "tr64/render.h"
#include "tr64/code.h"
#include "tr64/mem.h"
#include "tr64/engine.h"
#include "engine/core/sys.h"
#include "tr64/route.h"
#include "engine/core/log.h"

static unsigned loads;
static void start(const char *name,const tr64_game_inventory_carry_t *carry)
{
    mem_level_begin();
    code_loading_begin();tr64_scene_load(name);code_loading_end();
    if(carry)game_level_start_transition(tr64_level_current(),name,carry);
    else game_level_start(tr64_level_current(),name);
    ++loads;
}
static void release(void)
{
    tr64_mem_stats_t state;
    tr64_render_scene_release();game_level_stop();eng_level_release();
    tr64_mem_stats(TR64_MEM_state,&state);
    if(gs || game_stop_info() || state.used)
        sys_error("inventory-transition release retained state");
}
static void expect(const tr64_game_inventory_carry_t *carry,int32 to)
{
    int32 keys=0;
    for(int32 slot=0;slot<SLOT_MAX;++slot) {
        int32 wanted=carry->counts[slot];
        if(slot>=SLOT_PASSPORT && slot<=SLOT_CONTROLS)wanted=1;
        if(slot==SLOT_HOME)wanted=to==LVL_TR1_TITLE;
        if(slot==SLOT_COMPASS)wanted=to!=LVL_TR1_TITLE;
        if(to==LVL_TR1_10A && slot>=SLOT_PISTOLS && slot<=SLOT_AMMO_UZIS)wanted=0;
        if(gs->inventory.counts[slot]!=wanted)
            sys_error("inventory-transition slot %ld got %ld expected %ld",(long)slot,
                      (long)gs->inventory.counts[slot],(long)wanted);
        if(slot<SLOT_COMPASS)keys+=wanted;
    }
    if(gs->inventory.numKeys!=keys)sys_error("inventory-transition quest counter");
}
static void hud_view(void)
{
    tr64_game_hud_view_t hud;
    Lara *lara=gs->players[0];ExtraInfoLara *extra=lara->extraL;
    int32 water=lara->waterState,timer=extra->healthTimer,weapon=extra->weaponState;
    int32 inv=gs->inventory.state;uint32 hash=game_state_hash();
    if(!game_hud_view(&hud) || game_hud_view(NULL) ||
       hud.health!=lara->health || hud.health_max!=1000 ||
       hud.air!=lara->timer || hud.air_max!=1800)
        sys_error("HUD source values");
    gs->inventory.state=INV_STATE_NONE;extra->healthTimer=0;
    extra->weaponState=WEAPON_STATE_FREE;lara->waterState=WATER_STATE_ABOVE;
    game_hud_view(&hud);
    if(hud.show_air || hud.show_health)sys_error("HUD hidden source bars");
    lara->waterState=WATER_STATE_SURFACE;extra->healthTimer=1;
    game_hud_view(&hud);
    if(!hud.show_air || !hud.show_health)sys_error("HUD surface/timed health bars");
    lara->waterState=WATER_STATE_UNDER;extra->healthTimer=0;
    extra->weaponState=WEAPON_STATE_READY;
    game_hud_view(&hud);
    if(!hud.show_air || !hud.show_health)sys_error("HUD underwater/ready health bars");
    gs->inventory.state=INV_STATE_READY;game_hud_view(&hud);
    if(hud.show_air || hud.show_health)sys_error("HUD world bars while inventory open");
    lara->waterState=water;extra->healthTimer=timer;extra->weaponState=weapon;
    gs->inventory.state=inv;
    if(game_state_hash()!=hash)sys_error("HUD query changed simulation or RNG");
    log_printf("HUD view PASS source_values=1 visibility=1 read_only=1\n");
}
void tr64_level_reload_probe(const char *requested)
{
    tr64_game_inventory_carry_t carry,bad,out,untouched;
    GameInventory before;
    if(strcmp(requested,"level1"))sys_error("inventory-transition needs level1 first");
    memset(&out,0x6d,sizeof(out));untouched=out;
    game_init();
    if(game_inventory_capture(&out) || memcmp(&out,&untouched,sizeof(out)))
        sys_error("inventory-transition unloaded capture wrote output");
    tr64_render_scene_set_live(1);
    start("level1",NULL);
    hud_view();
    for(int32 slot=0;slot<SLOT_MAX;++slot) {
        int32 value=slot+1;
        if(slot==SLOT_COMPASS || slot==SLOT_PISTOLS || slot==SLOT_SHOTGUN ||
           slot==SLOT_MAGNUMS || slot==SLOT_UZIS || slot>=SLOT_PASSPORT)value=1;
        if(slot==SLOT_AMMO_PISTOLS)value=-1;
        if(!inventory_setCount(slot,value))sys_error("inventory-transition seed slot");
    }
    if(game_inventory_capture(NULL) || !game_inventory_capture(&carry) ||
       carry.from_level_id!=LVL_TR1_1)sys_error("inventory-transition capture");
    release();
    start("level2",&carry);expect(&carry,LVL_TR1_2);
    if(gs->inventory.useSlot!=SLOT_MAX)sys_error("inventory-transition stale use slot");
    before=gs->inventory;
    for(int32 slot=0;slot<SLOT_MAX;++slot) {
        bad=carry;bad.counts[slot]=32768;
        if(inventory_apply_level_transition(LVL_TR1_1,LVL_TR1_2,bad.counts) ||
           memcmp(&before,&gs->inventory,sizeof(before)))
            sys_error("inventory-transition invalid count mutated state");
    }
    if(inventory_apply_level_transition(99,LVL_TR1_2,carry.counts) ||
       inventory_apply_level_transition(LVL_TR1_1,99,carry.counts) ||
       inventory_apply_level_transition(LVL_TR1_1,LVL_TR1_2,NULL) ||
       memcmp(&before,&gs->inventory,sizeof(before)))
        sys_error("inventory-transition invalid level/null mutated state");
    gs->inventory.counts[SLOT_MEDIKIT_BIG]=-1;
    out=untouched;
    if(game_inventory_capture(&out) || memcmp(&out,&untouched,sizeof(out)))
        sys_error("inventory-transition invalid capture wrote output");
    gs->inventory=before;
    gs->inventory.useSlot=SLOT_KEY_ITEM_2;
    if(!inventory_apply_level_transition(LVL_TR1_2,LVL_TR1_TITLE,gs->inventory.counts))
        sys_error("inventory-transition alias rejected");
    expect(&carry,LVL_TR1_TITLE);
    if(gs->inventory.useSlot!=SLOT_KEY_ITEM_2)
        sys_error("inventory-transition unexpectedly cleared selected quest item");
    if(!inventory_apply_level_transition(LVL_TR1_TITLE,LVL_TR1_2,gs->inventory.counts))
        sys_error("inventory-transition title return rejected");
    expect(&carry,LVL_TR1_2);
    game_tick(0);
    if(game_stop_info()->kind!=TR64_GAME_STOP_NONE || !game_inventory_capture(&carry) ||
       carry.from_level_id!=LVL_TR1_2)sys_error("inventory-transition carried game tick");
    release();
    start("level10a",&carry);expect(&carry,LVL_TR1_10A);release();
    start("level1",NULL);
    for(int32 slot=0;slot<SLOT_COMPASS;++slot)
        if(gs->inventory.counts[slot])sys_error("inventory-transition fresh quest leak");
    if(gs->inventory.counts[SLOT_MEDIKIT_BIG] || gs->inventory.counts[SLOT_MEDIKIT_SMALL] ||
       gs->inventory.counts[SLOT_SHOTGUN] || gs->inventory.counts[SLOT_PISTOLS]!=1 ||
       gs->inventory.counts[SLOT_AMMO_PISTOLS]!=-1)
        sys_error("inventory-transition fresh loadout inherited counts");
    release();start("gym",NULL);
    for(int32 slot=SLOT_PISTOLS;slot<=SLOT_AMMO_UZIS;++slot)
        if(gs->inventory.counts[slot])sys_error("inventory-transition fresh gym weapons");
    release();
    log_printf("inventory-transition PASS loads=%u slots=%u invalid_counts=%u quest_preserved=1 ten_a_weapons_reset=1 fresh_reset=1\n",
               loads,(unsigned)SLOT_MAX,(unsigned)SLOT_MAX);
    uint16_t *pixels=mem_borrow_boot_card();tr64_boot_card(pixels);sys_present(pixels);
    log_flush();
}
