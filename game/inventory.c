/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/inventory.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "inventory-private.h"
#include "ui_string_ids.h"

static const InvItem INV_SLOTS[SLOT_MAX] = {
    { ITEM_INV_LEADBAR        , SND_INV_SHOW      ,   256   ,   ANGLE(20)   ,   ANGLE(44)   ,   ANGLE(67)  ,   STR_LEAD_BAR      },
    { ITEM_INV_KEY_ITEM_1     , SND_INV_SHOW      ,   296   ,   ANGLE(20)   ,   ANGLE(15)   ,   ANGLE(0)   ,   STR_KEY           },
    { ITEM_INV_KEY_ITEM_2     , SND_INV_SHOW      ,   296   ,   ANGLE(20)   ,   ANGLE(15)   ,   ANGLE(0)   ,   STR_KEY           },
    { ITEM_INV_KEY_ITEM_3     , SND_INV_SHOW      ,   296   ,   ANGLE(20)   ,   ANGLE(15)   ,   ANGLE(0)   ,   STR_KEY           },
    { ITEM_INV_KEY_ITEM_4     , SND_INV_SHOW      ,   296   ,   ANGLE(20)   ,   ANGLE(15)   ,   ANGLE(0)   ,   STR_KEY           },
    { ITEM_INV_PUZZLE_4       , SND_INV_SHOW      ,   296   ,   ANGLE(20)   ,   ANGLE(15)   ,   ANGLE(0)   ,   STR_PUZZLE        },
    { ITEM_INV_PUZZLE_3       , SND_INV_SHOW      ,   296   ,   ANGLE(20)   ,   ANGLE(15)   ,   ANGLE(0)   ,   STR_PUZZLE        },
    { ITEM_INV_PUZZLE_2       , SND_INV_SHOW      ,   296   ,   ANGLE(20)   ,   ANGLE(15)   ,   ANGLE(0)   ,   STR_PUZZLE        },
    { ITEM_INV_PUZZLE_1       , SND_INV_SHOW      ,   296   ,   ANGLE(20)   ,   ANGLE(15)   ,   ANGLE(0)   ,   STR_PUZZLE        },
    { ITEM_INV_SCION          , SND_INV_SHOW      ,   296   ,   ANGLE(20)   ,   ANGLE(15)   ,   ANGLE(45)  ,   STR_SCION         },
    { ITEM_INV_COMPASS        , SND_INV_COMPASS   ,   512   ,   ANGLE(20)   ,   ANGLE(70)   ,   ANGLE(0)   ,   STR_COMPASS       },
    { ITEM_INV_PISTOLS        , SND_INV_WEAPON    ,   296   ,   ANGLE(17)   ,   ANGLE(20)   ,   ANGLE(0)   ,   STR_PISTOLS       },
    { ITEM_INV_AMMO_PISTOLS   , SND_INV_SHOW      ,   296   ,   ANGLE(17)   ,   ANGLE(20)   ,   ANGLE(0)   ,   STR_AMMO_PISTOLS  },
    { ITEM_INV_SHOTGUN        , SND_INV_WEAPON    ,   296   ,   ANGLE(17)   ,   ANGLE(20)   ,   ANGLE(0)   ,   STR_SHOTGUN       },
    { ITEM_INV_AMMO_SHOTGUN   , SND_INV_SHOW      ,   296   ,   ANGLE(17)   ,   ANGLE(20)   ,   ANGLE(0)   ,   STR_AMMO_SHOTGUN  },
    { ITEM_INV_MAGNUMS        , SND_INV_WEAPON    ,   296   ,   ANGLE(17)   ,   ANGLE(20)   ,   ANGLE(0)   ,   STR_MAGNUMS       },
    { ITEM_INV_AMMO_MAGNUMS   , SND_INV_SHOW      ,   296   ,   ANGLE(17)   ,   ANGLE(20)   ,   ANGLE(0)   ,   STR_AMMO_MAGNUMS  },
    { ITEM_INV_UZIS           , SND_INV_WEAPON    ,   296   ,   ANGLE(17)   ,   ANGLE(20)   ,   ANGLE(0)   ,   STR_UZIS          },
    { ITEM_INV_AMMO_UZIS      , SND_INV_SHOW      ,   296   ,   ANGLE(17)   ,   ANGLE(20)   ,   ANGLE(0)   ,   STR_AMMO_UZIS     },
    { ITEM_INV_MEDIKIT_BIG    , SND_INV_SHOW      ,   352   ,   ANGLE(20)   ,   ANGLE(44)   ,   ANGLE(22)  ,   STR_MEDI_BIG      },
    { ITEM_INV_MEDIKIT_SMALL  , SND_INV_SHOW      ,   216   ,   ANGLE(22)   ,   ANGLE(40)   ,   ANGLE(22)  ,   STR_MEDI_SMALL    },
    { ITEM_INV_PASSPORT       , SND_INV_SHOW      ,   384   ,   ANGLE(25)   ,   ANGLE(23)   ,   ANGLE(0)   ,   STR_GAME          },
    { ITEM_INV_DETAIL         , SND_INV_SHOW      ,   424   ,   ANGLE(22)   ,   ANGLE(36)   ,   ANGLE(0)   ,   STR_DETAIL        },
    { ITEM_INV_SOUND          , SND_INV_SHOW      ,   368   ,   ANGLE(26)   ,   ANGLE(12)   ,   ANGLE(0)   ,   STR_SOUND         },
    { ITEM_INV_CONTROLS       , SND_INV_CONTROLS  ,   352   ,   ANGLE(30)   ,   ANGLE(67)   ,   ANGLE(0)   ,   STR_CONTROLS      },
    { ITEM_INV_HOME           , SND_INV_HOME      ,   384   ,   ANGLE(25)   ,   ANGLE(23)   ,   ANGLE(0)   ,   STR_HOME          },
};
#if TR64_HAS_TR2
/* Public model IDs and ring destinations: tr2-triggers-and-interaction,
 * Pickups; WALL.TR2 and the campaign's key names (tr2-items-general).
 * Focus transforms inherit the equivalent item's TR1 presentation. */
#define TR2_ITEM(ID,SND,D,P,X,Y,STR) {ID,SND,D,ANGLE(P),ANGLE(X),ANGLE(Y),STR}
#define TR2_TOOL(ID,STR) TR2_ITEM(ID,SND_INV_SHOW,296,20,15,0,STR)
#define TR2_GUN(ID,STR) TR2_ITEM(ID,SND_INV_WEAPON,296,17,20,0,STR)
#define TR2_AMMO(ID,STR) TR2_ITEM(ID,SND_INV_SHOW,296,17,20,0,STR)
static const InvItem tr2Items[] = {
    TR2_ITEM(TR2_RING_STOPWATCH,SND_INV_COMPASS,512,20,70,0,TR64_STR_STOPWATCH),
    TR2_ITEM(TR2_RING_PASSPORT,SND_INV_SHOW,384,25,23,0,STR_GAME),
    TR2_ITEM(TR2_RING_HOME,SND_INV_HOME,384,25,23,0,STR_HOME),
    TR2_ITEM(TR2_RING_DETAIL,SND_INV_SHOW,424,22,36,0,STR_DETAIL),
    TR2_ITEM(TR2_RING_SOUND,SND_INV_SHOW,368,26,12,0,STR_SOUND),
    TR2_ITEM(TR2_RING_CONTROLS,SND_INV_CONTROLS,352,30,67,0,STR_CONTROLS),
    TR2_GUN(157,STR_PISTOLS), TR2_GUN(158,STR_SHOTGUN),
    TR2_GUN(159,TR64_STR_TR2_AUTOMAGS), TR2_GUN(160,STR_UZIS),
    TR2_GUN(161,TR64_STR_TR2_HARPOON), TR2_GUN(162,TR64_STR_TR2_M16),
    TR2_GUN(163,TR64_STR_TR2_GRENADE),
    TR2_AMMO(165,STR_AMMO_SHOTGUN), TR2_AMMO(166,TR64_STR_TR2_AMMO_AUTOMAGS),
    TR2_AMMO(167,STR_AMMO_UZIS), TR2_AMMO(168,TR64_STR_TR2_AMMO_HARPOON),
    TR2_AMMO(169,TR64_STR_TR2_AMMO_M16), TR2_AMMO(170,TR64_STR_TR2_AMMO_GRENADE),
    TR2_ITEM(171,SND_INV_SHOW,216,22,40,22,STR_MEDI_SMALL),
    TR2_ITEM(172,SND_INV_SHOW,352,20,44,22,STR_MEDI_BIG),
    TR2_TOOL(173,TR64_STR_TR2_FLARES), TR2_TOOL(178,STR_PUZZLE),
    TR2_TOOL(179,STR_PUZZLE), TR2_TOOL(180,STR_PUZZLE), TR2_TOOL(181,STR_PUZZLE),
    TR2_TOOL(197,TR64_STR_TR2_GUARDHOUSE), TR2_TOOL(198,STR_KEY_RUSTY),
    TR2_TOOL(199,STR_KEY), TR2_TOOL(200,STR_KEY),
    TR2_TOOL(207,TR64_STR_TR2_QUEST), TR2_TOOL(208,TR64_STR_TR2_QUEST)
};
#undef TR2_AMMO
#undef TR2_GUN
#undef TR2_TOOL
#undef TR2_ITEM
#endif
const InvItem *Inventory_item(int32 slot)
{
#if TR64_HAS_TR2
    if (TR64_RULE(FRESH_STOCK)) {
        for (unsigned i=0;i<sizeof(tr2Items)/sizeof(tr2Items[0]);++i)
            if (tr2Items[i].type==slot) return tr2Items+i;
        sys_error("Tomb Raider II: inventory display type %ld",(long)slot);
    }
#endif
#line 36
    ASSERT(slot >= 0 && slot < SLOT_MAX);
    return INV_SLOTS+slot;
}
#if TR64_HAS_TR2
#define RING_ITEM(slot) Inventory_item(slot)
#else
#define RING_ITEM(slot) (INV_SLOTS+(slot))
#endif

static bool contains(const GameInventory *self,int32 slot)
{
    return slot >= 0 && slot < SLOT_MAX && self->counts[slot] != 0;
}
static int32 remapHoleToSlot(int32 type)
{
    if (type >= ITEM_PUZZLEHOLE_1 && type <= ITEM_PUZZLEHOLE_4)
        return SLOT_PUZZLE_1-(type-ITEM_PUZZLEHOLE_1);
    if (type >= ITEM_KEYHOLE_1 && type <= ITEM_KEYHOLE_4)
        return SLOT_KEY_ITEM_1+type-ITEM_KEYHOLE_1;
    return SLOT_MAX;
}
static bool keyHit(const GameInventory *self,int32 key)
{
    return ItemObj_isKeyHit(self->lara,key);
}
static void setState(GameInventory *self,int32 state,int32 nextState,int32 timer)
{
    self->state=state;
    self->nextState=nextState;
    self->timer=timer;
    self->rotItemInc=-self->rotItem/timer;
}
static void setHeight(GameInventory *self,int32 target)
{
    self->heightTarget=target;
    self->heightInc=(target-self->height)/self->timer;
}
static void setRadius(GameInventory *self,int32 target)
{
    self->radiusTarget=target;
    self->radiusInc=(target-self->radius)/self->timer;
}
static void setPitch(GameInventory *self,int32 target)
{
    self->pitchTarget=target;
    self->pitchInc=(target-self->pitch)/self->timer;
}
static void setRot(GameInventory *self,int32 delta,int32 target)
{
    self->rotTarget=target;
    self->rotInc=delta/self->timer;
}
static int32 getAnimLength(const GameInventory *self)
{
#if TR64_HAS_TR2
    int32 type=Inventory_item(self->itemsList[self->itemIndex])->type;
    if (TR64_RULE(FRESH_STOCK)) {
        if (type == TR2_RING_PASSPORT) return FRAME_PASSPORT;
        if (gs->level.models[type].count<=0 || gs->level.models[type].animIndex>=gs->tr2->animCount)
            sys_error("Tomb Raider II: missing inventory animation for type %ld",(long)type);
    } else
#else
    int32 type=INV_SLOTS[self->itemsList[self->itemIndex]].type;
#endif
#if TR64_HAS_TR2
    {
#endif
    if (type == ITEM_INV_PASSPORT) return FRAME_PASSPORT;
    if (type == ITEM_INV_COMPASS) return FRAME_COMPASS;
#if TR64_HAS_TR2
    }
#endif
    const Anim *anim=gs->level.anims+gs->level.models[type].animIndex;
    return tr64_u16(anim->frame_end)-tr64_u16(anim->frame_start);
}
static void setSelection(GameInventory *self,bool selected)
{
    int32 slot=self->itemsList[self->itemIndex];
    if (selected) {
        const InvItem *item=RING_ITEM(slot);
        self->selDistTarget=item->selDist;
        self->selRotPreTarget=item->selRotPre;
        self->selRotXTarget=item->selRotX;
        self->selRotYTarget=item->selRotY;
    } else {
        self->selDistTarget=self->selRotPreTarget=0;
        self->selRotXTarget=self->selRotYTarget=0;
    }
    self->selDistInc=(self->selDistTarget-self->selDist)/self->timer;
    self->selRotPreInc=(self->selRotPreTarget-self->selRotPre)/self->timer;
    self->selRotXInc=(self->selRotXTarget-self->selRotX)/self->timer;
    self->selRotYInc=(self->selRotYTarget-self->selRotY)/self->timer;
    self->frameTarget=selected ? getAnimLength(self) : 0;
    self->itemVisMask=UINT32_MAX;
#if TR64_HAS_TR2
    if (TR64_RULE(FRESH_STOCK)) {
        if (selected && slot == TR2_RING_PASSPORT) {
            bool hasSave = eng_game_has_save && eng_game_has_save();
            self->passportPage = (self->page == INV_PAGE_TITLE && hasSave)
                ? PASSPORT_PAGE_LOAD_GAME : PASSPORT_PAGE_SAVE_GAME;
            self->frameTarget += self->passportPage * 5;
        }
        return;
    }
#endif
    if (!TR64_RULE(FRESH_STOCK) && selected && slot == SLOT_PASSPORT) {
        self->passportPage=(self->page == INV_PAGE_OPTIONS || self->page == INV_PAGE_DEATH)
            ? PASSPORT_PAGE_SAVE_GAME : PASSPORT_PAGE_LOAD_GAME;
        self->frameTarget+=self->passportPage*5;
    }
}
static void addSlot(GameInventory *self,int32 slot)
{
    if (contains(self,slot)) self->itemsList[self->itemsCount++]=slot;
}
#if TR64_HAS_TR2
static void tr2AddModel(GameInventory *self,uint16 type)
{
    const Model *m=gs->level.models+type;
    if (m->count>0 && (unsigned)m->animIndex<gs->tr2->animCount)
        self->itemsList[self->itemsCount++]=type;
}
static int32 tr2Page(GameInventory *self,int32 page)
{
    const bool keys=page==INV_PAGE_KEYS || page==INV_PAGE_USE;
    if (page==INV_PAGE_TITLE) {
        static const uint16 tr2Title[5]={
            TR2_RING_PASSPORT,TR2_RING_CONTROLS,TR2_RING_DETAIL,TR2_RING_SOUND,TR2_RING_HOME
        };
        for (unsigned i=0;i<5;++i) tr2AddModel(self,tr2Title[i]);
        return 0;
    }
    if (page==INV_PAGE_DEATH) {
        tr2AddModel(self,TR2_RING_PASSPORT);
        return 0;
    }
    if (page==INV_PAGE_OPTIONS) {
        static const uint16 tr2Opts[4]={
            TR2_RING_DETAIL,TR2_RING_PASSPORT,TR2_RING_SOUND,TR2_RING_CONTROLS
        };
        for (unsigned i=0;i<4;++i) tr2AddModel(self,tr2Opts[i]);
        return 0;
    }
    self->numKeys=0;
    for (unsigned i=0;i<gs->tr2->carriedCount;++i) {
        const TR2RingEntry *entry=gs->tr2->carried+i;
        if (!entry->quantity) continue;
        const bool onKeys=game_tr2_ring_priority(entry->type)>=100;
        self->numKeys+=onKeys;
        if (onKeys==keys) self->itemsList[self->itemsCount++]=entry->type;
    }
    /* Medipacks (Q-CARTUI-01): the main ring always opens at zero. */
    int32 saved=keys ? gs->tr2->keySelection : 0;
    return saved>=0 && saved<self->itemsCount ? saved : 0;
}
#endif
static void setPage(GameInventory *self,int32 page)
{
    self->page=page;
    self->itemsCount=0;
#if TR64_HAS_TR2
    if (TR64_RULE(FRESH_STOCK)) {
        self->itemIndex=self->nextIndex=tr2Page(self,page);
    } else
#endif
    {
    switch (page) {
    case INV_PAGE_TITLE: case INV_PAGE_DEATH: case INV_PAGE_OPTIONS:
        addSlot(self,SLOT_PASSPORT);
        addSlot(self,SLOT_CONTROLS);
        addSlot(self,SLOT_DETAIL);
        addSlot(self,SLOT_SOUND);
        if (page == INV_PAGE_TITLE) addSlot(self,SLOT_HOME);
        break;
    case INV_PAGE_MAIN:
        addSlot(self,SLOT_COMPASS);
        if (gs->gLevelID != LVL_TR1_GYM) {
            addSlot(self,SLOT_PISTOLS);
            addSlot(self,SLOT_SHOTGUN);
            addSlot(self,SLOT_MAGNUMS);
            addSlot(self,SLOT_UZIS);
            addSlot(self,SLOT_MEDIKIT_BIG);
            addSlot(self,SLOT_MEDIKIT_SMALL);
        }
        break;
    case INV_PAGE_USE: case INV_PAGE_KEYS:
        for (int32 slot=SLOT_LEADBAR;slot<=SLOT_SCION;slot++) addSlot(self,slot);
        break;
    default: break;
    }
    self->itemIndex=self->nextIndex=0;
    }
    if (self->itemsCount > 0)
        setRot(self,ANGLE_180,self->itemIndex*ANGLE_360/self->itemsCount);
    self->rot=wrap_i16(self->rotTarget-ANGLE_180);
}
static void closeInventory(GameInventory *self)
{
    setState(self,INV_STATE_CLOSING,INV_STATE_NONE,16);
    setHeight(self,-1536);
    setRadius(self,0);
    setRot(self,ANGLE_180,self->rot-ANGLE_180);
}
static void updateVisMask(GameInventory *self)
{
#if TR64_HAS_TR2
    if (TR64_RULE(FRESH_STOCK) ? self->itemsList[self->itemIndex] != TR2_RING_PASSPORT
                               : self->itemsList[self->itemIndex] != SLOT_PASSPORT) return;
#else
    if (self->itemsList[self->itemIndex] != SLOT_PASSPORT) return;
#endif
    self->itemVisMask=(1u<<0)|(1u<<1)|(1u<<4);
    if (self->frameIndex <= 14) self->itemVisMask|=(1u<<2)|(1u<<6);
    else if (self->frameIndex < 19) self->itemVisMask|=(1u<<2)|(1u<<3)|(1u<<6);
    else if (self->frameIndex == 19) self->itemVisMask|=(1u<<3)|(1u<<6);
    else if (self->frameIndex < 24) self->itemVisMask|=(1u<<3)|(1u<<5)|(1u<<6);
    else if (self->frameIndex < 29) self->itemVisMask|=(1u<<3)|(1u<<5);
}
static bool animate(GameInventory *self,int32 frames)
{
    if (self->frameIndex == self->frameTarget) return false;
    if (self->frameIndex < self->frameTarget) {
        self->frameIndex+=frames;
        if (self->frameIndex >= self->frameTarget) {
            self->frameIndex=self->frameTarget;
            updateVisMask(self);
            return false;
        }
    }
    if (self->frameIndex > self->frameTarget) {
        self->frameIndex-=frames;
        if (self->frameIndex <= self->frameTarget) {
            self->frameIndex=self->frameTarget;
            updateVisMask(self);
            return false;
        }
    }
    updateVisMask(self);
    return true;
}
static void addOption(GameInventory *self,int32 type,int32 str,int32 value)
{
#line 194
    ASSERT(self->optionsCount < 32);
    GameInventoryOption *option=self->options+self->optionsCount++;
    option->type=type;
    option->str=str;
    option->value=value;
}
static void optionSpace(GameInventory *self)
{
    self->optionsHeight+=8;
    addOption(self,OPT_SPACE,STR_EMPTY,0);
}
static void optionLine(GameInventory *self,int32 type,int32 str,int32 value)
{
    self->optionsHeight+=18;
    addOption(self,type,str,value);
}
static void Inventory_initOptions(GameInventory *self)
{
    self->optionsCount=self->optionIndex=0;
    self->optionsWidth=216;
    self->optionsHeight=4;
    int32 slot=self->itemsList[self->itemIndex];
#if TR64_HAS_TR2
    if (TR64_RULE(FRESH_STOCK)) {
        if (slot==TR2_RING_PASSPORT) {
            if (self->passportPage==PASSPORT_PAGE_LOAD_GAME &&
                eng_game_has_save && eng_game_has_save()) {
                optionLine(self,OPT_BUTTON,STR_CURRENT_POSITION,LVL_LOAD);
                self->optionIndex=0;
            }
            return;
        }
        if (slot==TR2_RING_DETAIL) slot=SLOT_DETAIL;
        else if (slot==TR2_RING_SOUND) slot=SLOT_SOUND;
        else if (slot==TR2_RING_CONTROLS) slot=SLOT_CONTROLS;
        else return;
    }
#endif
    switch (slot) {
    case SLOT_PASSPORT:
        if (self->passportPage == PASSPORT_PAGE_LOAD_GAME) {
            optionLine(self,OPT_BUTTON,STR_TR1_LEVEL1,LVL_TR1_1);
            optionLine(self,OPT_BUTTON,STR_TR1_LEVEL2,LVL_TR1_2);
            if(!eng_game_has_save) {
                game_stop_set(TR64_GAME_STOP_NOT_PORTED,-1,"game save metadata backend not ported");
                return;
            }
            if (eng_game_has_save()) {
                optionSpace(self);
                optionLine(self,OPT_BUTTON,STR_CURRENT_POSITION,LVL_LOAD);
                self->optionIndex=self->optionsCount-1;
            }
        }
        break;
    case SLOT_DETAIL:
        optionLine(self,OPT_BAR,STR_OPT_DETAIL_GAMMA,gs->gSettings.video_gamma*16);
        optionLine(self,OPT_BAR,STR_OPT_DETAIL_FOG,gs->gSettings.video_fog*16);
        optionLine(self,OPT_SWITCH,STR_OPT_DETAIL_FPS,gs->gSettings.video_fps);
        optionLine(self,OPT_SWITCH,STR_OPT_DETAIL_VSYNC,gs->gSettings.video_vsync);
        optionLine(self,OPT_SWITCH,STR_OPT_DETAIL_FILTER,gs->gSettings.video_filter);
        optionLine(self,OPT_SWITCH,TR64_STR_OPT_DETAIL_AUTO_DIST,gs->gSettings.video_auto_dist);
        break;
    case SLOT_SOUND:
        optionLine(self,OPT_SWITCH,STR_OPT_SOUND_SFX,gs->gSettings.audio_sfx);
        optionLine(self,OPT_SWITCH,STR_OPT_SOUND_MUSIC,gs->gSettings.audio_music);
        break;
    case SLOT_CONTROLS:
        optionLine(self,OPT_SWITCH,STR_OPT_CONTROLS_VIBRATION,gs->gSettings.controls_vibration);
        optionLine(self,OPT_SWITCH,STR_OPT_CONTROLS_SWAP,gs->gSettings.controls_swap);
        optionLine(self,OPT_VALUE,TR64_STR_OPT_CONTROLS_ABILITIES,gs->gSettings.controls_abilities);
        break;
    default: break;
    }
}
static void onOption(GameInventory *self)
{
    if (self->optionsCount == 0) return;
    if (keyHit(self,IN_DOWN)) {
        for (int32 i=0;i<self->optionsCount;i++) {
            if (++self->optionIndex >= self->optionsCount) self->optionIndex=0;
            int32 type=self->options[self->optionIndex].type;
            if (type == OPT_SPACE || type == OPT_TEXT) continue;
            break;
        }
    }
    if (keyHit(self,IN_UP)) {
        for (int32 i=0;i<self->optionsCount;i++) {
            if (--self->optionIndex < 0) self->optionIndex=self->optionsCount-1;
            int32 type=self->options[self->optionIndex].type;
            if (type == OPT_SPACE || type == OPT_TEXT) continue;
            break;
        }
    }
    GameInventoryOption *opt=self->options+self->optionIndex;
    if (opt->type == OPT_BAR) {
        if (keyHit(self,IN_LEFT)) {
            opt->value-=16;
            if (opt->value < 0) opt->value=0;
        }
        if (keyHit(self,IN_RIGHT)) {
            opt->value+=16;
            if (opt->value > 256) opt->value=256;
        }
    } else if (opt->type == OPT_SWITCH && (keyHit(self,IN_LEFT) || keyHit(self,IN_RIGHT))) {
        opt->value=!opt->value;
    } else if (opt->type == OPT_VALUE) {
        if (keyHit(self,IN_LEFT))
            opt->value = (opt->value + 2) % 3;
        else if (keyHit(self,IN_RIGHT))
            opt->value = (opt->value + 1) % 3;
    }
}
static void nextLevel(GameInventoryCall *call,int32 level)
{
    call->actions|=GAME_INV_ACTION_NEXT_LEVEL;
    call->nextLevel=level;
}
static void onPassport(GameInventory *self,GameInventoryCall *call)
{
    if (self->passportPage == PASSPORT_PAGE_SAVE_GAME && self->optionsCount > 0 &&
        self->options[self->optionIndex].str == STR_OK) {
        if (keyHit(self,IN_ACTION) || keyHit(self,IN_JUMP)) {
            self->frameTarget=0;
            self->nextState=INV_STATE_CLOSE;
        }
        return;
    }
    if (self->page == INV_PAGE_OPTIONS || self->page == INV_PAGE_DEATH) {
        bool change=false;
        if (self->passportPage == PASSPORT_PAGE_LOAD_GAME && keyHit(self,IN_RIGHT)) {
            self->passportPage=PASSPORT_PAGE_SAVE_GAME;
            self->frameTarget+=5;
            change=true;
        } else if (self->passportPage == PASSPORT_PAGE_SAVE_GAME && keyHit(self,IN_RIGHT)) {
            self->passportPage=PASSPORT_PAGE_EXIT_TO_TITLE;
            self->frameTarget+=5;
            change=true;
        } else if (self->passportPage == PASSPORT_PAGE_EXIT_TO_TITLE && keyHit(self,IN_LEFT)) {
            self->passportPage=PASSPORT_PAGE_SAVE_GAME;
            self->frameTarget-=5;
            change=true;
        } else if (self->passportPage == PASSPORT_PAGE_SAVE_GAME && keyHit(self,IN_LEFT)) {
            self->passportPage=PASSPORT_PAGE_LOAD_GAME;
            self->frameTarget-=5;
            change=true;
        }
        if (change) {
            Inventory_initOptions(self);
            if(gs->stop.kind!=TR64_GAME_STOP_NONE)return;
            soundPlay(SND_INV_PAGE,NULL);
        }
    }
    if (!keyHit(self,IN_ACTION)) return;
    if (self->page == INV_PAGE_DEATH && self->passportPage == PASSPORT_PAGE_SAVE_GAME) {
        nextLevel(call,gs->gLevelID);
        self->frameTarget=0;
        self->nextState=INV_STATE_CLOSE;
        return;
    }
    if (self->passportPage == PASSPORT_PAGE_LOAD_GAME) {
        if (self->optionsCount <= 0) return;
        nextLevel(call,self->options[self->optionIndex].value);
        self->frameTarget=0;
        self->nextState=INV_STATE_CLOSE;
    }
    if (self->passportPage == PASSPORT_PAGE_SAVE_GAME) {
        call->actions|=GAME_INV_ACTION_SAVE;
        return;
    }
    if (self->passportPage == PASSPORT_PAGE_EXIT_TO_TITLE) {
        self->frameTarget=0;
        if (self->page == INV_PAGE_TITLE) self->nextState=INV_STATE_READY;
        else {
            nextLevel(call,LVL_TR1_TITLE);
            self->nextState=INV_STATE_CLOSE;
        }
    }
}
static void onDetail(GameInventory *self,GameInventoryCall *call)
{
    int32 value=self->options[self->optionIndex].value;
    int32 *setting=NULL;
    switch (self->optionIndex) {
    case OPT_ID_GAMMA:
        value>>=4;
        setting=&gs->gSettings.video_gamma;
        break;
    case OPT_ID_FOG:
        value>>=4;
        if (value > 16) value=16;
        setting=&gs->gSettings.video_fog;
        break;
    case OPT_ID_FPS: setting=&gs->gSettings.video_fps; break;
    case OPT_ID_VSYNC: setting=&gs->gSettings.video_vsync; break;
    case OPT_ID_FILTER: setting=&gs->gSettings.video_filter; break;
    case OPT_ID_AUTO_DIST: setting=&gs->gSettings.video_auto_dist; break;
    default: return;
    }
    if (*setting != value) {
        *setting=value;
        call->actions|=GAME_INV_ACTION_SETTINGS;
    }
}
static void onSound(GameInventory *self,GameInventoryCall *call)
{
    int32 value=self->options[self->optionIndex].value;
    int32 *setting=NULL;
    if (self->optionIndex == OPT_ID_SFX) setting=&gs->gSettings.audio_sfx;
    if (self->optionIndex == OPT_ID_MUSIC) setting=&gs->gSettings.audio_music;
    if (setting && *setting != value) {
        *setting=value;
        call->actions|=GAME_INV_ACTION_SETTINGS;
    }
}
static void onControls(GameInventory *self,GameInventoryCall *call)
{
    int32 value=self->options[self->optionIndex].value;
    if (self->optionIndex == OPT_ID_RUMBLE && gs->gSettings.controls_vibration != value) {
        gs->gSettings.controls_vibration=value;
        call->actions|=GAME_INV_ACTION_VIBRATE|GAME_INV_ACTION_SETTINGS;
    }
    if (self->optionIndex == OPT_ID_SWAP && gs->gSettings.controls_swap != value) {
        gs->gSettings.controls_swap=value;
        call->actions|=GAME_INV_ACTION_SETTINGS;
    }
    if (self->optionIndex == OPT_ID_ABILITIES && gs->gSettings.controls_abilities != value) {
        gs->gSettings.controls_abilities=value;
        game_lara_abilities_refresh();
        call->actions|=GAME_INV_ACTION_SETTINGS;
    }
}
#if TR64_HAS_TR2
static void onTR2Passport(GameInventory *self,GameInventoryCall *call)
{
    if (self->passportPage == PASSPORT_PAGE_SAVE_GAME && self->optionsCount > 0 &&
        self->options[self->optionIndex].str == STR_OK) {
        if (keyHit(self,IN_ACTION) || keyHit(self,IN_JUMP)) {
            self->frameTarget=0;
            self->nextState=INV_STATE_CLOSE;
        }
        return;
    }
    bool hasSave = eng_game_has_save && eng_game_has_save();
    bool change=false;
    if (self->passportPage == PASSPORT_PAGE_LOAD_GAME && keyHit(self,IN_RIGHT)) {
        self->passportPage=PASSPORT_PAGE_SAVE_GAME;
        self->frameTarget+=5;
        change=true;
    } else if (self->passportPage == PASSPORT_PAGE_SAVE_GAME && keyHit(self,IN_RIGHT)) {
        self->passportPage=PASSPORT_PAGE_EXIT_TO_TITLE;
        self->frameTarget+=5;
        change=true;
    } else if (self->passportPage == PASSPORT_PAGE_EXIT_TO_TITLE && keyHit(self,IN_LEFT)) {
        self->passportPage=PASSPORT_PAGE_SAVE_GAME;
        self->frameTarget-=5;
        change=true;
    } else if (self->passportPage == PASSPORT_PAGE_SAVE_GAME && hasSave && keyHit(self,IN_LEFT)) {
        self->passportPage=PASSPORT_PAGE_LOAD_GAME;
        self->frameTarget-=5;
        change=true;
    }
    if (change) {
        Inventory_initOptions(self);
        if (gs->stop.kind != TR64_GAME_STOP_NONE) return;
        soundPlay(SND_INV_PAGE,NULL);
    }
    onOption(self);
    if (!keyHit(self,IN_ACTION)) return;
    if (self->passportPage == PASSPORT_PAGE_LOAD_GAME) {
        if (self->optionsCount <= 0) return;
        nextLevel(call,self->options[self->optionIndex].value);
        self->frameTarget=0;
        self->nextState=INV_STATE_CLOSE;
        return;
    }
    if (self->passportPage == PASSPORT_PAGE_SAVE_GAME) {
        if (self->page == INV_PAGE_DEATH) {
            nextLevel(call,gs->gLevelID);
            self->frameTarget=0;
            self->nextState=INV_STATE_CLOSE;
            return;
        }
        if (self->page == INV_PAGE_TITLE || gs->gLevelID == TR2_LVL_HOME) {
            nextLevel(call,TR2_LVL_NEW_GAME);
            self->frameTarget=0;
            self->nextState=INV_STATE_CLOSE;
            return;
        }
        call->actions|=GAME_INV_ACTION_SAVE;
        return;
    }
    if (self->passportPage == PASSPORT_PAGE_EXIT_TO_TITLE) {
        self->frameTarget=0;
        if (self->page == INV_PAGE_TITLE) self->nextState=INV_STATE_READY;
        else {
            nextLevel(call,TR2_LVL_TITLE);
            self->nextState=INV_STATE_CLOSE;
        }
    }
}
static bool onTR2Item(GameInventory *self,GameInventoryCall *call)
{
    const int32 type=self->itemsList[self->itemIndex];
    if (type==TR2_RING_PASSPORT) {
        if (self->page != INV_PAGE_DEATH &&
            !(self->passportPage == PASSPORT_PAGE_SAVE_GAME && self->optionsCount > 0 &&
              self->options[self->optionIndex].str == STR_OK) &&
            (keyHit(self,IN_JUMP) || keyHit(self,IN_SELECT))) {
            self->frameTarget=0;
            self->nextState=INV_STATE_READY;
            return false;
        }
        onTR2Passport(self,call);
        return self->nextState==INV_STATE_NONE && !(call->actions & GAME_INV_ACTION_SAVE);
    }
    if (keyHit(self,IN_JUMP) || keyHit(self,IN_SELECT)) {
        self->frameTarget=0;
        self->nextState=INV_STATE_READY;
        return false;
    }
    if (type==TR2_RING_DETAIL || type==TR2_RING_SOUND || type==TR2_RING_CONTROLS) {
        onOption(self);
        if (type==TR2_RING_DETAIL) onDetail(self,call);
        else if (type==TR2_RING_SOUND) onSound(self,call);
        else onControls(self,call);
        return true;
    }
    if (type==TR2_RING_HOME) {
        if (keyHit(self,IN_ACTION)) {
            nextLevel(call,TR2_LVL_HOME);
            self->nextState=INV_STATE_CLOSE;
        }
    } else if (type==TR2_RING_STOPWATCH) {
        if (keyHit(self,IN_ACTION)) {
            self->frameTarget=0;
            self->nextState=INV_STATE_CLOSE;
        }
    } else if (type>=165 && type<=170) {
        /* Picked up before its gun: the bundle is on the ring and has no use. */
        game_stop_set(TR64_GAME_STOP_NOT_PORTED,-1,
                      "Tomb Raider II: ammunition bundle has no admitted use");
    } else if ((type>=157 && type<=163) || (type>=171 && type<=173) ||
               game_tr2_ring_priority(type)>=100) {
        self->useSlot=type;
        self->nextState=INV_STATE_CLOSE;
    }
    return self->nextState==INV_STATE_NONE;
}
#endif
static bool onItem(GameInventory *self,GameInventoryCall *call)
{
#if TR64_HAS_TR2
    if (TR64_RULE(FRESH_STOCK)) return onTR2Item(self,call);
#endif
    onOption(self);
    if (self->lara->input & (IN_JUMP|IN_SELECT)) {
        self->frameTarget=0;
        self->nextState=INV_STATE_READY;
    }
    int32 slot=self->itemsList[self->itemIndex];
    switch (slot) {
    case SLOT_LEADBAR: case SLOT_KEY_ITEM_1: case SLOT_KEY_ITEM_2: case SLOT_KEY_ITEM_3:
    case SLOT_KEY_ITEM_4: case SLOT_PUZZLE_4: case SLOT_PUZZLE_3: case SLOT_PUZZLE_2:
    case SLOT_PUZZLE_1: case SLOT_SCION: case SLOT_MEDIKIT_BIG: case SLOT_MEDIKIT_SMALL:
        self->nextState=INV_STATE_CLOSE;
        self->useSlot=slot;
        break;
    case SLOT_PISTOLS: case SLOT_SHOTGUN: case SLOT_MAGNUMS: case SLOT_UZIS: {
        int32 weapon=slot == SLOT_PISTOLS ? WEAPON_PISTOLS : slot == SLOT_SHOTGUN ? WEAPON_SHOTGUN :
            slot == SLOT_MAGNUMS ? WEAPON_MAGNUMS : WEAPON_UZIS;
        if (inventory_canSelectWeapon(weapon)) {
            self->nextState=INV_STATE_CLOSE;
            self->useSlot=slot;
        }
        break;
    }
    case SLOT_COMPASS:
        if (self->lara->input & IN_ACTION) {
            self->frameTarget=0;
            self->nextState=INV_STATE_CLOSE;
        }
        break;
    case SLOT_PASSPORT: onPassport(self,call); break;
    case SLOT_DETAIL: onDetail(self,call); break;
    case SLOT_SOUND: onSound(self,call); break;
    case SLOT_CONTROLS: onControls(self,call); break;
    case SLOT_HOME:
        if (self->lara->input & IN_ACTION) {
            self->nextState=INV_STATE_CLOSE;
            self->useSlot=slot;
            nextLevel(call,LVL_TR1_GYM);
        }
        break;
    default: break;
    }
    return self->nextState == INV_STATE_NONE;
}
static void finishShow(GameInventory *self)
{
    if (self->nextState != INV_STATE_NONE && self->frameIndex == self->frameTarget) {
        setState(self,INV_STATE_DESELECT,self->nextState,8);
        setSelection(self,false);
    }
}
static void saveResult(GameInventory *self,int result)
{
    if (result == TR64_ENG_SAVE_OK) {
        self->frameTarget=0;
        self->nextState=INV_STATE_CLOSE;
    } else {
        self->optionsHeight=4;
        self->optionsCount=0;
        optionSpace(self);
        if (result == TR64_ENG_SAVE_BLOCKED) {
            optionLine(self,OPT_TEXT,TR64_STR_CANNOT_SAVE_NOW,0);
        } else {
            optionLine(self,OPT_TEXT,STR_GBA_SAVE_WARNING_1,0);
            optionLine(self,OPT_TEXT,STR_GBA_SAVE_WARNING_2,0);
            optionLine(self,OPT_TEXT,STR_GBA_SAVE_WARNING_3,0);
        }
        optionSpace(self);
        self->optionIndex=self->optionsCount;
        optionLine(self,OPT_BUTTON,STR_OK,0);
    }
    finishShow(self);
}
void Inventory_update(GameInventory *self,GameInventoryCall *call,int32 frames)
{
    game_inventory_fading(frames);
    if (self->page == INV_PAGE_END || self->page == INV_PAGE_STATS) {
        if (keyHit(self,IN_ACTION) || keyHit(self,IN_JUMP) || keyHit(self,IN_SELECT)) {
            if (self->page == INV_PAGE_STATS) call->actions|=GAME_INV_ACTION_RESET_STATS;
            nextLevel(call,self->page == INV_PAGE_STATS ? self->statsNextLevel : LVL_TR1_TITLE);
            self->state=INV_STATE_NONE;
        }
        return;
    }
    if (self->state != INV_STATE_SHOW)
        self->rotItem=wrap_i16(i32_add(self->rotItem,i32_mul(self->rotItemInc,frames)));
    if (self->timer > 0) {
        self->timer-=frames;
        self->height=i32_add(self->height,i32_mul(self->heightInc,frames));
        self->radius=i32_add(self->radius,i32_mul(self->radiusInc,frames));
        self->pitch=wrap_i16(i32_add(self->pitch,i32_mul(self->pitchInc,frames)));
        self->rot=wrap_i16(i32_add(self->rot,i32_mul(self->rotInc,frames)));
        self->selDist=i32_add(self->selDist,i32_mul(self->selDistInc,frames));
        self->selRotPre=wrap_i16(i32_add(self->selRotPre,i32_mul(self->selRotPreInc,frames)));
        self->selRotX=wrap_i16(i32_add(self->selRotX,i32_mul(self->selRotXInc,frames)));
        self->selRotY=wrap_i16(i32_add(self->selRotY,i32_mul(self->selRotYInc,frames)));
        if (self->timer <= 0) {
            self->timer=0;
            self->state=self->nextState;
            self->nextState=INV_STATE_NONE;
            self->height=self->heightTarget;
            self->radius=self->radiusTarget;
            self->pitch=wrap_i16(self->pitchTarget);
            self->rot=wrap_i16(self->rotTarget);
            self->selDist=self->selDistTarget;
            self->selRotPre=wrap_i16(self->selRotPreTarget);
            self->selRotX=wrap_i16(self->selRotXTarget);
            self->selRotY=wrap_i16(self->selRotYTarget);
            self->heightInc=self->radiusInc=self->pitchInc=self->rotInc=0;
            self->rotItem=0;
            self->rotItemInc=512;
            self->selDistInc=self->selRotPreInc=self->selRotXInc=self->selRotYInc=0;
            self->itemIndex=self->nextIndex;
        }
    }
    switch (self->state) {
    case INV_STATE_CLOSE:
        closeInventory(self);
        break;
    case INV_STATE_READY:
        if ((self->lara->input & IN_LEFT) && self->itemsCount > 1) {
            soundPlay(SND_INV_SPIN,NULL);
            self->nextIndex=self->itemIndex+1;
            if (self->nextIndex >= self->itemsCount) self->nextIndex-=self->itemsCount;
            setState(self,INV_STATE_SPIN,INV_STATE_READY,12);
            setRot(self,ANGLE_360/self->itemsCount,self->nextIndex*ANGLE_360/self->itemsCount);
        } else if ((self->lara->input & IN_RIGHT) && self->itemsCount > 1) {
            soundPlay(SND_INV_SPIN,NULL);
            self->nextIndex=self->itemIndex-1;
            if (self->nextIndex < 0) self->nextIndex+=self->itemsCount;
            setState(self,INV_STATE_SPIN,INV_STATE_READY,12);
            setRot(self,-ANGLE_360/self->itemsCount,self->nextIndex*ANGLE_360/self->itemsCount);
        } else if (self->lara->input & IN_UP) {
            if (self->page == INV_PAGE_OPTIONS || (self->page == INV_PAGE_MAIN && self->numKeys > 0)) {
                setState(self,INV_STATE_CLOSING,self->page == INV_PAGE_OPTIONS ? INV_STATE_PAGE_MAIN : INV_STATE_PAGE_KEYS,12);
                setRadius(self,0);
                setRot(self,ANGLE_180,self->rot-ANGLE_180);
                setPitch(self,ANGLE_45);
            }
        } else if (self->lara->input & IN_DOWN) {
            if (self->page == INV_PAGE_KEYS || self->page == INV_PAGE_MAIN) {
                setState(self,INV_STATE_CLOSING,self->page == INV_PAGE_KEYS ? INV_STATE_PAGE_MAIN : INV_STATE_PAGE_OPTIONS,12);
                setRadius(self,0);
                setRot(self,ANGLE_180,self->rot-ANGLE_180);
                setPitch(self,-ANGLE_45);
            }
        } else if ((keyHit(self,IN_SELECT) || keyHit(self,IN_JUMP)) && self->page != INV_PAGE_TITLE) {
            soundPlay(SND_INV_HIDE,NULL);
            self->useSlot=SLOT_MAX;
            closeInventory(self);
        } else if ((self->lara->input & IN_ACTION) &&
                   (!TR64_RULE(FRESH_STOCK) || self->itemsCount)) {
            soundPlay(RING_ITEM(self->itemsList[self->itemIndex])->snd,NULL);
            setState(self,INV_STATE_SELECT,INV_STATE_SHOW,8);
            setSelection(self,true);
            Inventory_initOptions(self);
        }
        break;
    case INV_STATE_DEATH:
        soundPlay(RING_ITEM(self->itemsList[self->itemIndex])->snd,NULL);
        setState(self,INV_STATE_SELECT,INV_STATE_SHOW,8);
        setSelection(self,true);
        Inventory_initOptions(self);
        break;
    case INV_STATE_SHOW:
        self->rotItem=0;
        if (animate(self,frames)) break;
        bool itemReady=onItem(self,call);
        if(gs->stop.kind!=TR64_GAME_STOP_NONE)return;
        if (!itemReady) {
            if (call->actions & GAME_INV_ACTION_SAVE) return;
            finishShow(self);
        }
        break;
    case INV_STATE_PAGE_MAIN: case INV_STATE_PAGE_KEYS: case INV_STATE_PAGE_OPTIONS: {
        int32 page=self->state == INV_STATE_PAGE_MAIN ? INV_PAGE_MAIN :
            self->state == INV_STATE_PAGE_KEYS ? INV_PAGE_KEYS : INV_PAGE_OPTIONS;
        self->pitch=wrap_i16(-self->pitch);
        setState(self,INV_STATE_OPENING,INV_STATE_READY,12);
        setRadius(self,INV_RING_RADIUS);
        setPitch(self,0);
        setPage(self,page);
        break;
    }
    default: break;
    }
}
static void openInventory(GameInventory *self,GameInventoryCall *call)
{
    self->lara=call->lara;
    self->backgroundKind=call->page == INV_PAGE_TITLE ? 2 : 1;
    call->backgroundKind=self->backgroundKind;
    call->actions|=GAME_INV_ACTION_BACKGROUND;
    self->height=INV_CAMERA_HEIGHT;
    self->pitch=call->page == INV_PAGE_TITLE ? 1024 : 0;
    self->radius=0;
    self->frameIndex=0;
    self->useSlot=SLOT_MAX;
    self->itemVisMask=UINT32_MAX;
    if (call->page == INV_PAGE_END || call->page == INV_PAGE_STATS) {
        if (call->page == INV_PAGE_END) {
            game_sndPlayTrack(3);
            soundPlay(SND_HEALTH,NULL);
        }
        self->page=call->page;
        self->state=INV_STATE_READY;
        return;
    }
    setState(self,INV_STATE_OPENING,call->page == INV_PAGE_DEATH ? INV_STATE_DEATH : INV_STATE_READY,16);
    setHeight(self,-256);
    setRadius(self,INV_RING_RADIUS);
    setPage(self,call->page);
    if (!TR64_RULE(FRESH_STOCK) && call->page == INV_PAGE_USE) {
        int32 slot=remapHoleToSlot(call->arg);
        int32 index=0;
        for (int32 i=0;i<self->itemsCount;i++) {
            if (self->itemsList[i] == slot) {
                index=i;
                break;
            }
        }
        self->itemIndex=self->nextIndex=index;
        setRot(self,ANGLE_180,self->itemIndex*ANGLE_360/self->itemsCount);
        self->rot=wrap_i16(self->rotTarget-ANGLE_180);
    }
    soundPlay(SND_INV_SHOW,NULL);
    Inventory_update(self,call,1);
}
uint32 tr64_unit_inv(uint32 arg)
{
    GameInventoryCall *call=(GameInventoryCall *)(uintptr_t)arg;
#line 624
    ASSERT(call);
    call->actions=0;
    switch (call->op) {
    case GAME_INV_OPEN: openInventory(&gs->inventory,call); break;
    case GAME_INV_UPDATE: Inventory_update(&gs->inventory,call,call->frames); break;
    case GAME_INV_SAVE_RESULT: saveResult(&gs->inventory,call->saveResult); break;
    case GAME_INV_VIEW: Inventory_view(&gs->inventory,call); break;
    default: sys_error("game inventory unit operation %u",(unsigned)call->op);
    }
    return call->actions;
}
