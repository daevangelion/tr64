/* SPDX-License-Identifier: MIT */
/* Real production game/level replay, checking only public presentation queries. */
#ifdef TR64_HOST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tr64/code.h"
#include "tr64/eng.h"
#include "tr64/engine.h"
#include "tr64/game.h"
#include "tr64/level.h"
#include "tr64/mem.h"
#include "tr64/render.h"
#include "engine/core/log.h"
#include "engine/core/sys.h"

static tr64_game_drawable_t pieces[TR64_GAME_DRAW_CHUNK];
static uint32_t emitted_hash,trailer_count;
static void hash_trailer(void *context,const char *bytes,uint32_t count)
{
    (void)context;
    if(count!=20 || memcmp(bytes,",\"hash\":\"",9))return;
    uint32_t value=0;
    for(uint32_t i=9;i<17;++i) {
        unsigned char c=(unsigned char)bytes[i];
        if(c>='0' && c<='9')value=(value<<4)|(c-'0');
        else if(c>='a' && c<='f')value=(value<<4)|(c-'a'+10u);
        else sys_error("presentation proof malformed emitted hash");
    }
    emitted_hash=value;++trailer_count;
}
void tr64_level_reload_probe(const char *level)
{
    const char *path=getenv("TR64_PRESENTATION_INPUT");
    if(!path)sys_error("presentation proof requires input");
    FILE *in=fopen(path,"r");
    if(!in)sys_error("presentation proof input open");
    unsigned masks[600],count=0;int place=0;int32_t p[5]={0};char word[128];
    while(fscanf(in,"%127s",word)==1) {
        if(!strcmp(word,"start")) {
            if(count || place || fscanf(in,"%d%d%d%d%d",p,p+1,p+2,p+3,p+4)!=5)
                sys_error("presentation proof placement directive");
            place=1;
        } else {
            char *end;unsigned long mask=strtoul(word,&end,0);
            if(!*word || *end || word[0]=='-' || count==600 ||
               (mask&~(2u|4u|8u|16u|32u|64u|128u|256u|512u|2048u)))
                sys_error("presentation proof input mask");
            masks[count++]=(unsigned)mask;
        }
    }
    if(ferror(in) || fclose(in) || count!=600)sys_error("presentation proof needs 600 masks");
    mem_level_begin();code_loading_begin();tr64_render_scene_set_live(1);
    tr64_scene_load(level);code_loading_end();
    game_init();game_level_start(tr64_level_current(),level);
    if(place && (p[0]<0 || p[4]<-32768 || p[4]>32767 ||
       !game_place((uint32_t)p[0],p[1],p[2],p[3],(int16_t)p[4])))
        sys_error("presentation proof placement rejected");
    tr64_game_level_stats_t before,after;
    if(!game_stats_view(&before))sys_error("presentation proof missing initial stats");
    uint32_t events=0,posed=0,ammo=0;
    for(uint32_t tick=1;tick<=600;++tick) {
        game_tick((uint16_t)masks[tick-1]);
        const tr64_game_stop_t *stop=game_stop_info();
        if(!stop || stop->kind!=TR64_GAME_STOP_NONE)sys_error("presentation proof unexpected stop");
        uint32_t hash=game_state_hash(),n=game_pickup_event_count();
        if(tick==1) {
            game_trace_schema(3,hash_trailer,NULL);
            if(trailer_count!=1 || emitted_hash!=game_state_hash_schema(3) ||
               game_state_hash_schema(2)!=hash)
                sys_error("presentation proof hash query/serializer schema mismatch");
        }
        if(!game_stats_view(&after) || (n&255u)!=(uint8_t)(after.pickups-before.pickups))
            sys_error("presentation proof collection event/stat mismatch tick=%lu",(unsigned long)tick);
        before=after;
        for(uint32_t i=0;i<n;++i) {
            tr64_game_pickup_event_t event;
            if(!game_pickup_event(i,&event) || event.tick!=tick || event.type<0)
                sys_error("presentation proof invalid event snapshot");
            int origin=0;
            const tr64_level_t *core=tr64_level_current();
            for(uint32_t j=0;j<tr64_level_table(core,TR64_CORE_items)->count;++j) {
                const tr64_core_items_t *item=tr64_level_items(core,j);
                if(tr64_u16(item->type)==event.type &&
                   tr64_i32(item->x)==event.position[0] &&
                   tr64_i32(item->y)==event.position[1] &&
                   tr64_i32(item->z)==event.position[2])origin=1;
            }
            if(!origin)sys_error("presentation proof pickup origin differs from actual authored item");
            uint32_t need=game_pickup_item_view(event.type,1234,NULL,0);
            if(!need || need>TR64_GAME_DRAW_CHUNK)sys_error("presentation proof real pickup has no icon");
            memset(pieces,0xA5,sizeof(pieces));
            if(game_pickup_item_view(event.type,1234,pieces,need-1)!=need)
                sys_error("presentation proof icon capacity contract");
            const unsigned char *bytes=(const unsigned char *)pieces;
            for(uint32_t j=0;j<sizeof(pieces);++j)if(bytes[j]!=0xA5)
                sys_error("presentation proof insufficient icon capacity wrote");
            if(game_pickup_item_view(event.type,1234,pieces,TR64_GAME_DRAW_CHUNK)!=need)
                sys_error("presentation proof icon required count changed");
            for(uint32_t j=0;j<need;++j)
                if(pieces[j].kind!=TR64_GAME_DRAW_MESH || pieces[j].room!=UINT32_MAX ||
                   pieces[j].water || pieces[j].light_offset!=-32)
                    sys_error("presentation proof roomless icon metadata");
            ++events;posed+=need;
        }
        tr64_game_pickup_event_t sentinel,saved;
        memset(&sentinel,0xA5,sizeof(sentinel));saved=sentinel;
        if(game_pickup_event(n,&sentinel) || memcmp(&sentinel,&saved,sizeof(saved)))
            sys_error("presentation proof invalid event query wrote");
        tr64_game_menu_text_t label;
        uint32_t labels=game_hud_text_view(NULL,0);
        if(labels>1 || game_hud_text_view(&label,1)!=labels)
            sys_error("presentation proof ammo required count");
        if(labels) {
            if(label.kind!=TR64_GAME_TEXT_NUMBER || label.alignment!=TR64_GAME_TEXT_LEFT ||
               label.x!=212 || (label.y!=20 && label.y!=30) ||
               (label.suffix!=103 && label.suffix!=104 && label.suffix!=105))
                sys_error("presentation proof ammo raw glyph/position");
            ++ammo;
        }
        if(game_state_hash()!=hash)sys_error("presentation proof query changed game/RNG");
    }
    if(!events || !posed || !ammo)sys_error("presentation proof required actual pickup/icon/ammo coverage");
    log_printf("presentation proof PASS ticks=600 events=%lu bones=%lu ammo_ticks=%lu\n",
        (unsigned long)events,(unsigned long)posed,(unsigned long)ammo);
}
#endif
