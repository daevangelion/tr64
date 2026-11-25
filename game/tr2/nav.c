/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/nav.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "game/common/internal.h"

/* tr2-great-wall-creatures, Route, waypoint and random choices. */
enum { ROUTE_BLOCKED = 0x8000, ROUTE_ID = 0x7fff };

const tr64_u16_be_t *game_tr2_creature_zones(const TR2Creature *ai, bool flipped)
{
    unsigned layer = ai->fly ? 4 : (ai->step >> 8)-1;
    return gs->level.zones+(layer+5u*flipped)*gs->level.boxesCount;
}

void game_tr2_creature_nav_init(TR2Creature *ai, int16 box)
{
    ai->box=box;
    ai->targetBox=ai->requiredBox=ai->nextBox=ai->headBox=ai->tailBox=-1;
    ai->search=ai->boxSearch=ai->zoneCount=0;
    const tr64_u16_be_t *normal=game_tr2_creature_zones(ai,false);
    const tr64_u16_be_t *alternate=game_tr2_creature_zones(ai,true);
    for (unsigned i=0;i<gs->level.boxesCount;++i)
        ai->route[i]=(TR2RouteNode){.link=-1,.queued=-1};
    for (unsigned i=0;i<gs->level.boxesCount;++i) {
        if (box>=0 && (normal[i].raw==normal[box].raw || alternate[i].raw==alternate[box].raw))
            ai->route[ai->zoneCount++].candidate=i;
    }
}

static int32 floorOf(int box)
{
    return tr64_i16(gs->level.boxes[box].floor);
}

static void expand(TR2Creature *ai, const tr64_u16_be_t *zones)
{
    if (ai->requiredBox>=0 && ai->requiredBox!=ai->targetBox) {
        int16 destination=ai->requiredBox;
        ai->targetBox=destination;
        TR2RouteNode *node=ai->route+destination;
        if (node->queued<0 && ai->tailBox!=destination) {
            node->queued=ai->headBox;
            if (ai->headBox<0) ai->tailBox=destination;
            ai->headBox=destination;
        }
        ai->search=(ai->search+1)&ROUTE_ID;
        node->generation=ai->search;
        node->link=-1;
    }
    unsigned zone=ai->headBox>=0 ? tr64_u16(zones[ai->headBox]) : 0;
    for (unsigned i=0;i<5;++i) {
        int16 head=ai->headBox;
        if (head<0) {
            ai->tailBox=-1;
            break;
        }
        TR2RouteNode *parent=ai->route+head;
        uint16_t generation=parent->generation;
        unsigned headWord=tr64_u16(gs->level.boxes[head].overlap);
        const tr64_u16_be_t *edge=headWord==TR64_BOX_NO_OVERLAP ? NULL :
                                  gs->level.overlaps+(headWord&TR64_BOX_INDEX_MASK);
        const int32 height=floorOf(head);
        while (edge) {
            unsigned word=tr64_u16(*edge++);
            if (word&TR64_BOX_OVERLAP_END) edge=NULL;
            int16 box=word&TR64_BOX_INDEX_MASK;
            if (tr64_u16(zones[box])!=zone)
                continue;
            int32 step=floorOf(box)-height;
            if (step<ai->drop || step>ai->step)
                continue;
            TR2RouteNode *child=ai->route+box;
            unsigned newer=generation&ROUTE_ID, older=child->generation&ROUTE_ID;
            if (newer<older || (newer==older &&
                ((generation&ROUTE_BLOCKED) || !(child->generation&ROUTE_BLOCKED))))
                continue;
            child->generation=generation;
            if (gs->boxFlags[box]&ai->blockMask)
                child->generation|=ROUTE_BLOCKED;
            if (!(child->generation&ROUTE_BLOCKED)) child->link=head;
            if (child->queued<0 && box!=ai->tailBox) {
                ai->route[ai->tailBox].queued=box;
                ai->tailBox=box;
            }
        }
        ai->headBox=parent->queued;
        parent->queued=-1;
    }
    ai->nextBox=ai->box<0 ? -1 : ai->route[ai->box].link;
    ai->boxSearch=ai->box<0 ? 0 : ai->route[ai->box].generation;
}

/* Bounds on axes in Z, X order. All upper bounds are inclusive. */
typedef struct { int32 lo[2], hi[2]; } Corridor;
static Corridor bounds(int box)
{
    const tr64_core_boxes_t *b=gs->level.boxes+box;
    return (Corridor){ {tr64_i32(b->min_z),tr64_i32(b->min_x)},
                       {tr64_i32(b->max_z)-1,tr64_i32(b->max_x)-1} };
}

static int32 inset(int32 n,int32 low,int32 high)
{
    return n<low+512 ? low+512 : n>high-512 ? high-512 : n;
}

void game_tr2_creature_box_destination(TR2Creature *ai,int box)
{
    Corridor b=bounds(box);
    int32 z=rand_logic(), x=rand_logic();
    ai->destination[0]=b.lo[1]+512+x*((b.hi[1]-b.lo[1]+1-1024)>>15);
    ai->destination[1]=floorOf(box)-(ai->fly?384:0);
    ai->destination[2]=b.lo[0]+512+z*((b.hi[0]-b.lo[0]+1-1024)>>15);
    ai->requiredBox=box;
}

void game_tr2_creature_waypoint(TR2Creature *ai)
{
    const tr64_u16_be_t *zones=game_tr2_creature_zones(ai,gs->gSaveGame.flipped);
    expand(ai,zones);
    const vec3i start=ai->item->pos;
    const int32 relative[2]={start.z,start.x};
    int32 point[2]={start.z,start.x};
    int32 y=start.y;
    int box=ai->box;
    if (box<0 || ai->targetBox<0) return;
    Corridor intersection=bounds(box), b=intersection;
    enum { OPEN_ALL=15, CORNER=16 };
    unsigned open=OPEN_ALL;
    /* Usable links may predate this generation. A level's box count
     * bounds a malformed or stale chain without unbounded search. */
    for (unsigned n=0;n<gs->level.boxesCount;++n) {
        b=bounds(box);
        int32 height=floorOf(box)-(ai->fly?1024:0);
        if (y>height) y=height;
        if (relative[0]>=b.lo[0] && relative[0]<=b.hi[0] &&
            relative[1]>=b.lo[1] && relative[1]<=b.hi[1])
            intersection=b;
        else for (unsigned axis=0;axis<2;++axis) {
            unsigned other=axis^1;
            bool low=relative[axis]<b.lo[axis], high=relative[axis]>b.hi[axis];
            if (!low && !high) continue;
            unsigned side=1u<<(2*axis+high);
            if ((open&side) && relative[other]>=b.lo[other] && relative[other]<=b.hi[other]) {
                const int32 edge=low ? b.lo[axis]+512 : b.hi[axis]-512;
                if ((low && point[axis]<edge) || (high && point[axis]>edge)) point[axis]=edge;
                if (open&CORNER) goto store;
                if (intersection.lo[other]<b.lo[other]) intersection.lo[other]=b.lo[other];
                if (intersection.hi[other]>b.hi[other]) intersection.hi[other]=b.hi[other];
                open=side;
            } else if (open!=side) {
                point[axis]=low ? intersection.hi[axis]-512 : intersection.lo[axis]+512;
                if (open!=OPEN_ALL) goto store;
                open=CORNER;
            }
        }
        if (box==ai->targetBox) {
            for (unsigned axis=0;axis<2;++axis) {
                if (open&(3u<<(2*axis))) point[axis]=ai->destination[axis?0:2];
                else if (!(open&CORNER)) point[axis]=inset(point[axis],b.lo[axis],b.hi[axis]);
            }
            y=ai->destination[1];
            goto store;
        }
        int next=ai->route[box].link;
        if (next<0 || gs->boxFlags[next]&ai->blockMask) break;
        box=next;
    }
    for (unsigned axis=0;axis<2;++axis) {
        if (open&(3u<<(2*axis)))
            point[axis]=b.lo[axis]+512+((int64_t)rand_logic()*(b.hi[axis]-b.lo[axis]-1024)>>15);
        else if (!(open&CORNER)) point[axis]=inset(point[axis],b.lo[axis],b.hi[axis]);
    }
    y=floorOf(box)-(ai->fly?384:0);
store:
    ai->waypoint[0]=point[1]; ai->waypoint[1]=y; ai->waypoint[2]=point[0];
}
