/* SPDX-License-Identifier: MIT */
/* Test-only link wrapper: drive weapon inputs, never arm state or counters. */
#include "game/internal.h"
#include <stdio.h>

static FILE *tape;
static uint8_t was_present[256];

void __real_game_tr2_update_arms(Lara *);

/* The test drives the pre-weapon creature inputs. Do not also run the
 * live controller/contact pipeline on those externally supplied poses. */
void __wrap_game_tr2_update_creature(ItemObj *item,Lara *lara) { (void)item; (void)lara; }
void __wrap_game_tr2_creature_contact(ItemObj *item,Lara *lara) { (void)item; (void)lara; }

static void take(int32 *words, size_t n)
{
    for (size_t i=0;i<n;++i) {
        uint8_t bytes[4];
        if (fread(bytes,1,sizeof(bytes),tape) != sizeof(bytes))
            sys_error("combat test: truncated input tape");
        words[i]=wrap_i32((uint32)bytes[0] | (uint32)bytes[1]<<8 |
                         (uint32)bytes[2]<<16 | (uint32)bytes[3]<<24);
    }
}

static void pose(ItemObj *item, const int32 *words)
{
    if (words[6]<0 || (unsigned)words[6]>=gs->level.roomsCount ||
        words[7]<0 || (unsigned)words[7]>=gs->tr2->animCount ||
        words[8]<0 || words[8]>INT16_MAX)
        sys_error("combat test: bad room, animation or frame");
    for (unsigned i=3;i<6;++i)
        if (words[i]<INT16_MIN || words[i]>INT16_MAX)
            sys_error("combat test: bad rotation");
    Room *room = gs->rooms+words[6];
    if (item->room != room) {
        if (item->room) Room_remove(item->room,item);
        Room_add(room,item);
    }
    item->pos=vec3i_create(words[0],words[1],words[2]);
    item->angle=vec3s_create(words[3],words[4],words[5]);
    item->animIndex=words[7];
    item->frameIndex=words[8];
}

void __wrap_game_tr2_update_arms(Lara *lara)
{
    if (!TR64_RULE(COMBAT))
        sys_error("combat test: use schema 21");
    if (!tape) {
        const char *path=getenv("TR64_COMBAT_TEST_TAPE");
        if (!path || !(tape=fopen(path,"rb")))
            sys_error("combat test: missing tape");
        int32 magic;
        take(&magic,1);
        if (magic!=0x43313254)
            sys_error("combat test: wrong tape version");
    }
    int32 header[13];
    take(header,13);
    const int32 count = header[12];
    if ((uint32)header[0]!=gs->tick+1 || count<0 || count>TR2_CREATURE_SLOTS ||
        header[10]<INT16_MIN || header[10]>INT16_MAX || header[11]<0 || header[11]>3)
        sys_error("combat test: bad tick, roster or Lara state");
    pose(lara,header+1);
    lara->health=header[10];
    lara->waterState=header[11];
    uint8_t present[256]={0};
    TR2Creature saved[TR2_CREATURE_SLOTS];
    memcpy(saved,gs->tr2->creatures,sizeof(saved));
    memset(gs->tr2->creatures,0,sizeof(gs->tr2->creatures));
    for (int32 i=0;i<count;++i) {
        int32 row[11];
        take(row,11);
        if (row[0]<0 || (unsigned)row[0]>=gs->level.itemsCount || present[row[0]] ||
            row[1]<-1 || row[1]>=TR2_CREATURE_SLOTS)
            sys_error("combat test: bad roster slot");
        ItemObj *item=gs->items+row[0];
        if (item->type!=TR2_ITEM_SPIDER && item->type!=TR2_ITEM_TIGER)
            sys_error("combat test: unexpected creature type");
        pose(item,row+2);
        if (!was_present[row[0]]) item->health=1; /* Eligibility only; hit state is not an input. */
        present[row[0]]=1;
        if (row[1]>=0) {
            TR2Creature *ai=gs->tr2->creatures+row[1];
            if (ai->item) sys_error("combat test: duplicate navigation slot");
            ai->item=item;
            ai->box=ai->targetBox=ai->requiredBox=ai->nextBox=ai->headBox=ai->tailBox=-1;
        }
    }
    for (unsigned i=0;i<gs->level.itemsCount;++i) {
        ItemObj *item=gs->items+i;
        if (was_present[i] && !present[i] && item->type==TR2_ITEM_SPIDER && item->room)
            ItemObj_remove(item);
    }
    memcpy(was_present,present,sizeof(present));
    __real_game_tr2_update_arms(lara);
    memcpy(gs->tr2->creatures,saved,sizeof(saved));
}

/* This harness does not have the pre-fire RNG state or creature health.
 * Run a separate, direct ray/spread test; never certify this tape as a trace. */
bool __wrap_game_tr2_ray_hit(const ItemObj *item, const vec3i *origin, int16 pitch, int16 yaw, vec3i *point)
{
    (void)item; (void)origin; (void)pitch; (void)yaw; (void)point;
    return false;
}

void __wrap_game_trace_tr2_end(tr64_game_writer_t write, void *ctx, uint32 hash)
{
    (void)write; (void)ctx; (void)hash;
    if (!tape || fgetc(tape)!=EOF || ferror(tape) || fclose(tape))
        sys_error("combat test: trailing tape or read error");
    tape=NULL;
}
