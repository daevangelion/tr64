/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/inventory.h and draw.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "inventory-private.h"
#include "ui_string_ids.h"

static bool optionsVisible(const tr64_game_menu_view_t *v)
{
    return v->state==INV_STATE_SHOW && v->frameIndex==v->frameTarget;
}
static void text(tr64_game_menu_text_t **out,int kind,int alignment,
                 int x,int y,int32 value,int suffix,int lineHeight)
{
    *(*out)++=(tr64_game_menu_text_t){value,(int16)x,(int16)y,
        (uint8)kind,(uint8)alignment,(uint8)suffix,(uint8)lineHeight};
}
uint32_t game_menu_text_view(const tr64_game_menu_view_t *v,
 const uint16_t *widths,uint32_t widthCount,tr64_game_menu_text_t *buffer,
 uint32_t capacity)
{
    if(!v || v->state==INV_STATE_NONE)return 0;
    uint32 required;
    bool stats=v->page==INV_PAGE_STATS;
#if TR64_HAS_TR2
    stats=stats || (v->stats.titleID>=TR64_STR_TR2_WALL && optionsVisible(v) &&
        (unsigned)v->itemIndex<TR64_GAME_MENU_ITEMS &&
        v->itemIndex<v->itemsCount && v->items[v->itemIndex].type==TR2_RING_STOPWATCH);
#endif
    if(v->page==INV_PAGE_END)required=STR_ALPHA_END_6-STR_ALPHA_END_1+1;
    else if(stats)required=1;
    else {
        required=!!v->showTitle+!!v->showItem+!!v->showCount+
            2u*!!v->showUp+2u*!!v->showDown+!!v->showLeft+!!v->showRight;
        if(optionsVisible(v))for(int32 i=0;i<v->optionsCount;++i) {
            int32 type=v->options[i].type;
            required+=(type==OPT_SWITCH || type==OPT_VALUE) ? 2u :
                type==OPT_BUTTON || type==OPT_BAR || type==OPT_TEXT;
        }
    }
    if(!buffer || capacity<required)return required;
    tr64_game_menu_text_t *out=buffer;
    if(v->page==INV_PAGE_END) {
        for(int32 id=STR_ALPHA_END_1;id<=STR_ALPHA_END_6;++id)
            text(&out,TR64_GAME_TEXT_STRING,TR64_GAME_TEXT_CENTER,
                 0,48+(id-STR_ALPHA_END_1)*16,id,0,0);
        return required;
    }
    if(stats) {
        text(&out,TR64_GAME_TEXT_STATS_BLOCK,TR64_GAME_TEXT_CENTER,0,48,0,0,16);
        return required;
    }
    if((v->showLeft || v->showRight) &&
       (!widths || v->itemID<0 || (uint32)v->itemID>=widthCount))
        sys_error("game menu passport string width unavailable");
    if(v->showCount)text(&out,TR64_GAME_TEXT_NUMBER,TR64_GAME_TEXT_LEFT,
                         192,208,v->countValue,v->countSuffix ? v->countSuffix+91 : 0,0);
    if(optionsVisible(v)) {
        int32 y=(240-v->optionsHeight)/2-12+2;
        for(int32 i=0;i<v->optionsCount;++i) {
            const tr64_game_menu_option_t *opt=v->options+i;
            switch(opt->type) {
            case OPT_SPACE:y-=10;break;
            case OPT_BUTTON:case OPT_TEXT:
                text(&out,TR64_GAME_TEXT_STRING,TR64_GAME_TEXT_CENTER,0,y+16,opt->str,0,0);
                break;
            case OPT_BAR:case OPT_SWITCH:case OPT_VALUE:
                text(&out,TR64_GAME_TEXT_STRING,TR64_GAME_TEXT_RIGHT,-168,y+16,opt->str,0,0);
                if(opt->type==OPT_SWITCH)
                    text(&out,TR64_GAME_TEXT_STRING,TR64_GAME_TEXT_CENTER,
                         44,y+16,opt->value ? STR_ON : STR_OFF,0,0);
                else if(opt->type==OPT_VALUE) {
                    int32 valStr = opt->value == 1 ? TR64_STR_ABILITIES_TR2 :
                                   opt->value == 2 ? TR64_STR_ABILITIES_TR3 :
                                                     TR64_STR_ABILITIES_TR1;
                    text(&out,TR64_GAME_TEXT_STRING,TR64_GAME_TEXT_CENTER,
                         44,y+16,valStr,0,0);
                }
                break;
            default:break;
            }
            y+=18;
        }
    }
    if(v->showTitle)text(&out,TR64_GAME_TEXT_STRING,TR64_GAME_TEXT_CENTER,0,20,v->titleID,0,0);
    if(v->showUp) {
        text(&out,TR64_GAME_TEXT_GLYPH,TR64_GAME_TEXT_LEFT,4,20,80,0,0);
        text(&out,TR64_GAME_TEXT_GLYPH,TR64_GAME_TEXT_RIGHT,-6,20,80,0,0);
    }
    if(v->showDown) {
        text(&out,TR64_GAME_TEXT_GLYPH,TR64_GAME_TEXT_LEFT,4,235,81,0,0);
        text(&out,TR64_GAME_TEXT_GLYPH,TR64_GAME_TEXT_RIGHT,-6,235,81,0,0);
    }
    if(v->showItem)text(&out,TR64_GAME_TEXT_STRING,TR64_GAME_TEXT_CENTER,0,232,v->itemID,0,0);
    if(v->showRight)text(&out,TR64_GAME_TEXT_GLYPH,TR64_GAME_TEXT_LEFT,
                         (320+widths[v->itemID])/2+4,232,109,0,0);
    if(v->showLeft)text(&out,TR64_GAME_TEXT_GLYPH,TR64_GAME_TEXT_LEFT,
                        (320-widths[v->itemID])/2-18,232,108,0,0);
    return required;
}

uint32_t game_hud_text_view_player(unsigned slot,tr64_game_menu_text_t *buffer,uint32_t capacity)
{
    int16 value;uint8 suffix;
    if(!gs || slot>=TR64_GAME_PLAYERS || gs->inventory.state!=INV_STATE_NONE ||
       !Lara_ammoLabel(gs->players[slot],&value,&suffix))return 0;
    if(!buffer || !capacity)return 1;
    const Lara *lara=gs->players[slot];
    int32 y=4+16; /* A ready weapon also makes drawHUD's health bar visible. */
    if(lara->waterState==WATER_STATE_SURFACE || lara->waterState==WATER_STATE_UNDER)y+=10;
    *buffer=(tr64_game_menu_text_t){value,212,(int16)y,
        TR64_GAME_TEXT_NUMBER,TR64_GAME_TEXT_LEFT,suffix ? (uint8)(suffix+91) : 0,0};
    return 1;
}
uint32_t game_hud_text_view(tr64_game_menu_text_t *buffer,uint32_t capacity)
{
    return game_hud_text_view_player(0,buffer,capacity);
}
