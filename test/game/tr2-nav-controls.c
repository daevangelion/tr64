/* SPDX-License-Identifier: MIT */
/* Link only game/tr2/nav.c. The oversized backing arrays keep
 * the rejected v2 reads observable without accessing unowned memory. */
#include "game/common/internal.h"
#include <stdio.h>

static GameState state;
GameState *gs=&state;
static tr64_u16_be_t overlaps[16384],zones[16386];
static tr64_core_boxes_t boxes[2];
static uint16 flags[2];
static TR2RouteNode route[2];
static ItemObj item={.pos={512,0,512}};
int32 rand_logic(void) { return 22845; }

int main(int argc,char **argv)
{
    if (argc!=2) return 2;
    TR2Creature ai={.item=&item,.route=route,.step=256,.drop=-512};
    gs->level.boxesCount=2; gs->level.boxes=boxes; gs->level.zones=zones;
    gs->level.overlaps=overlaps; gs->boxFlags=flags;
    const bool noList=!strcmp(argv[1],"sentinel"), flagged=!strcmp(argv[1],"edge");
    if (noList || flagged) {
        for (unsigned i=0;i<2;++i)
            boxes[i].max_x.raw=boxes[i].max_z.raw=tr64_core_be32(1024);
        /* The only valid chain is at offset 0. Bit 14 of a neighbour
         * word is not an index bit. Ignore all words after its end. */
        boxes[0].overlap.raw=tr64_core_be16(UINT16_MAX);
        flags[0]=UINT16_MAX;
        flags[1]=noList ? UINT16_MAX : 0;
        boxes[1].overlap.raw=tr64_core_be16(flags[1]);
        overlaps[0].raw=tr64_core_be16(0xc000);
        overlaps[16383].raw=tr64_core_be16(0x8000);
        zones[16384].raw=tr64_core_be16(1);
        game_tr2_creature_nav_init(&ai,0);
        ai.requiredBox=1;
        game_tr2_creature_waypoint(&ai);
        int expected=noList ? -1 : 1;
        printf("%s: expected link %d, got %d\n",argv[1],expected,ai.nextBox);
        return ai.nextBox!=expected || ai.headBox!=-1 || ai.tailBox!=-1;
    }
    if (strcmp(argv[1],"width")) return 2;
    const int width=200*1024;
    boxes[0].max_x.raw=boxes[0].max_z.raw=tr64_core_be32(width);
    game_tr2_creature_nav_init(&ai,0);
    ai.requiredBox=ai.targetBox=1;
    game_tr2_creature_waypoint(&ai);
    int32 expected=512+(22845LL*(width-1025)>>15);
    printf("width: expected waypoint %d, got %d/%d\n",expected,ai.waypoint[0],ai.waypoint[2]);
    return ai.waypoint[0]!=expected || ai.waypoint[2]!=expected;
}
