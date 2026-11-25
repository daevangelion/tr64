/* SPDX-License-Identifier: MIT */
#include <string.h>
#include "engine/save/game.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "tr64/eng.h"
#include "tr64/mem.h"
#include "tr64/res.h"
#if TR64_CART_TR2
#include "tr64/code.h"
static void tr2_medium_begin(void)
{
 code_loading_begin();
 if(code_is_loaded("front"))code_unload("front");
 if(!code_is_loaded("medium"))code_load("medium");
 code_loading_end();
}
static void tr2_medium_end(void)
{
 code_loading_begin();
 if(code_is_loaded("medium"))code_unload("medium");
 code_loading_end();
}
#endif
static uint8_t *workspace;
static unsigned selected;
static int save_confirmed,load_confirmed,strict;
static unsigned saves;
static int last_status=TR64_SAVE_UNAVAILABLE;
static void put32(uint8_t *p,uint32_t value)
{ p[0]=value>>24;p[1]=value>>16;p[2]=value>>8;p[3]=value; }
static int settings_load(void)
{
 uint32_t bytes=0;int status=tr64_save_settings_read(workspace,TR64_SAVE_REGION_BYTES,&bytes);
 if(status)return status;
 if(bytes!=32 && bytes!=40 && bytes!=44)return TR64_SAVE_CORRUPT;
 unsigned words=bytes/4u;
 uint32_t v[11]={0};for(unsigned i=0;i<words;++i)v[i]=tr64_be32(workspace+4*i);
 for(unsigned i=0;i<words;++i) {
  uint32_t maxv = i<2 ? 31u : (i==10 ? 2u : 1u);
  if(v[i]>maxv)return TR64_SAVE_CORRUPT;
 }
 if(words<11) {
#if TR64_GAME == 3
  v[10]=TR64_LARA_ABILITIES_TR3;
#elif TR64_HAS_TR2
  v[10]=(TR64_CART_TR2 || game_level_id()>=100) ? TR64_LARA_ABILITIES_TR2 : TR64_LARA_ABILITIES_TR1;
#else
  v[10]=TR64_LARA_ABILITIES_TR1;
#endif
 }
 tr64_game_settings_t settings={(int32_t)v[0],(int32_t)v[1],(int32_t)v[2],(int32_t)v[3],
  (int32_t)v[4],(int32_t)v[5],(int32_t)v[6],(int32_t)v[7],(int32_t)v[8],(int32_t)v[9],(int32_t)v[10]};
 return game_settings_apply(&settings)?TR64_SAVE_OK:TR64_SAVE_CORRUPT;
}
int tr64_save_game_start(void)
{
 if(workspace || game_level_id()<0)sys_error("save state workspace start order");
 workspace=tr64_mem_alloc(TR64_MEM_state,TR64_SAVE_REGION_BYTES);
#if TR64_CART_TR2
 tr2_medium_begin();
#else
 tr64_save_unit_load();
#endif
 last_status=tr64_save_scan();
 if(!last_status) {
  int status=settings_load();
  if(status!=TR64_SAVE_EMPTY)last_status=status;
 }
#if TR64_CART_TR2
 tr2_medium_end();
#endif
 log_printf("save: bind status=%d slots=%u workspace=%u\n",last_status,tr64_save_any(),TR64_SAVE_REGION_BYTES);
 return last_status;
}
void tr64_save_game_stop(void) { workspace=NULL; }
void tr64_save_game_confirm(unsigned slot,int load)
{
 if(slot>=TR64_SAVE_SLOTS)sys_error("save selected slot range");
 selected=slot;
 if(load) { load_confirmed=1;save_confirmed=0; }
 else { save_confirmed=1;load_confirmed=0; }
}
void tr64_save_game_confirm_tick(void) { save_confirmed=0; }
void tr64_save_game_confirm_reset(void) { save_confirmed=load_confirmed=0; }
int tr64_save_game_take_load_confirmation(void)
{ int confirmed=load_confirmed;load_confirmed=0;return confirmed; }
void tr64_save_game_require_confirmation(void) { strict=1; }
unsigned tr64_save_game_selected(void) { return selected; }
int tr64_save_game_status(void) { return last_status; }
unsigned tr64_save_game_saves(void) { return saves; }
int eng_game_has_save(void) { return tr64_save_usable_any(); }
int eng_game_save(void)
{
 if(!workspace)sys_error("save state workspace unavailable");
 /* A gameplay refusal is distinct from SRAM failure and does not consume
  * a picker confirmation or read/write the medium. */
 if(game_save_blocked())return TR64_ENG_SAVE_BLOCKED;
 /* Before game_save, which commits the game's header: a save nobody confirmed
  * in the picker (no slot was chosen for it) writes nothing and changes nothing. */
 if(strict && !save_confirmed) {
  last_status=TR64_SAVE_ARGUMENT;
  log_printf("save: refused, no slot confirmed in the picker\n");
  return TR64_ENG_SAVE_FAILED;
 }
 save_confirmed=0;
 const uint32_t begun=sys_time_us();
 uint32_t bytes=game_save(workspace,TR64_SAVE_PAYLOAD_BYTES);
 const uint32_t serialized=sys_time_us();
 if(!bytes || bytes>TR64_SAVE_PAYLOAD_BYTES) { last_status=TR64_SAVE_SPACE;return TR64_ENG_SAVE_FAILED; }
 tr64_save_info_t info={.level_id=game_level_id(),.identity=tr64_save_level_identity(game_level_id())};
 if(!game_stats_view(&info.stats))sys_error("save source statistics view unavailable");
#if TR64_CART_TR2
 tr2_medium_begin();
#endif
 last_status=tr64_save_write(selected,&info,workspace,bytes);
#if TR64_CART_TR2
 tr2_medium_end();
#endif
 /* The whole save runs inside one game tick: its duration is what the audio buffers must outlast. */
 log_printf("save: slot=%u bytes=%lu status=%d serialize_us=%lu write_us=%lu\n",selected,(unsigned long)bytes,last_status,
  (unsigned long)(serialized-begun),(unsigned long)(sys_time_us()-serialized));
 if(last_status==TR64_SAVE_OK)++saves;
 return last_status==TR64_SAVE_OK ? TR64_ENG_SAVE_OK : TR64_ENG_SAVE_FAILED;
}
void eng_game_save_settings(void)
{
 const tr64_game_settings_t *s=game_settings_view();
#if TR64_GAME == 3
 if(!workspace)return;
#elif TR64_HAS_TR2
 if(!workspace && (TR64_CART_TR2 || game_level_id()>=100))return;
#endif
 if(!workspace || !s)sys_error("save settings workspace/view unavailable");
 const uint32_t values[11]={(uint32_t)s->video_gamma,(uint32_t)s->video_fog,
  (uint32_t)s->video_fps,(uint32_t)s->video_vsync,(uint32_t)s->audio_sfx,
  (uint32_t)s->audio_music,(uint32_t)s->controls_vibration,(uint32_t)s->controls_swap,
  (uint32_t)s->video_filter,(uint32_t)s->video_auto_dist,(uint32_t)s->controls_abilities};
 for(unsigned i=0;i<11;++i)put32(workspace+4*i,values[i]);
#if TR64_CART_TR2
 tr2_medium_begin();
#endif
 last_status=tr64_save_settings_write(workspace,44);
#if TR64_CART_TR2
 tr2_medium_end();
#endif
 log_printf("save: settings status=%d\n",last_status);
}
int tr64_save_game_load(void)
{
 if(!workspace)sys_error("save load workspace unavailable");
 tr64_save_info_t info;
#if TR64_CART_TR2
 tr2_medium_begin();
#endif
 last_status=tr64_save_read(selected,workspace,TR64_SAVE_REGION_BYTES,&info);
#if TR64_CART_TR2
 tr2_medium_end();
#endif
 if(last_status)return 0;
 /* The slot is judged by the game's own check; whatever it rejects stays
  * FOREIGN in the picker until the slot is written again. */
 if(info.level_id!=game_level_id() || !info.identity || info.identity!=tr64_save_level_identity(info.level_id)) {
  last_status=TR64_SAVE_FOREIGN;tr64_save_refuse(selected);return 0;
 }
 if(!game_load(workspace,info.bytes)) { last_status=TR64_SAVE_FOREIGN;tr64_save_refuse(selected);return 0; }
 return 1;
}
