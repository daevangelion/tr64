/* SPDX-License-Identifier: MIT */
/* Source-linked checks of private camera geometry and view publication.
 * FROZEN_V2 runs the same assertions against the original candidate. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include "game/common/camera.c"
#include "game/common/camera_tick.c"
#include "game/common/trigger.c"
#include "game/common/view.c"

#ifdef FROZEN_V2
#define game_tr2_bearing phd_atan
#endif

static GameState state;
static GameTR2 tr2;
GameState *gs=&state;

void sys_error(const char *fmt,...)
{
    va_list args;
    va_start(args,fmt);
    vfprintf(stderr,fmt,args);
    va_end(args);
    exit(1);
}

void sys_log_write(const char *bytes,uint32_t length)
{
    fwrite(bytes,1,length,stdout);
}

static tr64_u16_be_t word(unsigned value)
{
    return (tr64_u16_be_t){.raw=tr64_core_be16(value)};
}

static void check_geometry(const char *test)
{
    RoomInfo info[2]={0};
    Sector sectors[2][9]={0};
    Room rooms[2]={0};
    for (int i=0;i<2;++i) {
        info[i].x_sectors=info[i].z_sectors=word(3);
        rooms[i].info=&info[i];
        rooms[i].sectors=sectors[i];
        for (int j=0;j<9;++j) {
            sectors[i][j].floor=12;
            sectors[i][j].roomBelow=sectors[i][j].roomAbove=NO_ROOM;
            sectors[i][j].boxIndex=8;
        }
    }
    gs->rooms=rooms;
#if !defined(FROZEN_V2) && !defined(FROZEN_V3)
    if (!strcmp(test,"bounce")) {
        static tr64_u16_be_t frames[9];
        static Anim clip={.frame_rate=1,.frame_size=9};
        gs->level.anims=&clip;
        gs->level.animFrames=frames;
        gs->level.framesCount=9;
        tr2.animCount=1;
        for (int i=0;i<9;++i)
            sectors[0][i].boxIndex=UINT16_MAX;
        Lara lara={.room=rooms,.pos={1500,500,1800}};
        Camera c={.view={.room=rooms,.pos={1500,500,1600}},
                  .target={.room=rooms,.pos={1500,500,1800}},.speed=1,
                  .laraItem=&lara,.mode=CAMERA_MODE_FOLLOW,.targetDist=100};
        Camera control=c;
        gs->tick=6;
        uint32 rng=gs->gRandSeedLogic=123;
        tr2CameraUpdate(&control);
        int32 offsets[3];
        for (unsigned i=0;i<3;++i) {
            rng=rng*UINT32_C(0x41c64e6d)+UINT32_C(0x3039);
            offsets[i]=((int32)((rng>>10)&32767)-16384)*-10/32767;
        }
        c.bounce=-10;
        tr2CameraUpdate(&c);
        assert(gs->stop.kind==TR64_GAME_STOP_NONE && gs->gRandSeedLogic==rng);
        assert(!tr2.cameraUnavailableFrom && c.bounce==-5);
        assert(c.view.pos.x==control.view.pos.x+offsets[0] &&
               c.view.pos.y==control.view.pos.y+offsets[1] &&
               c.view.pos.z==control.view.pos.z+offsets[2]);
        assert(c.target.pos.x==control.target.pos.x &&
               c.target.pos.y==control.target.pos.y+offsets[0]+offsets[1] &&
               c.target.pos.z==control.target.pos.z+offsets[2]);
        c=control;
        Camera positive=c;
        positive.bounce=40;
        tr2CameraUpdate(&c);
        tr2CameraUpdate(&positive);
        assert(!positive.bounce && gs->gRandSeedLogic==rng);
        assert(positive.view.pos.y==c.view.pos.y+40 && positive.target.pos.y==c.target.pos.y+40);
        log_flush();
        return;
    }
#endif
    if (!strcmp(test,"vertical")) {
        /* Linked room tile (0,0): ordinary border selection would read (1,0). */
        sectors[0][3].floor=0;
        sectors[0][3].roomBelow=1;
        sectors[1][0].floor=8;
        sectors[1][0].boxIndex=7;
        Location sample={.room=rooms,.pos={100,100,100}};
        CameraRange range=cameraRange(&sample,true);
        printf("vertical: room=%td floor=%d box=%u\n",sample.room-rooms,range.floor,range.box);
        assert(sample.room==rooms+1 && range.floor==2048 && range.box==7);
    } else {
        Camera c={.view={.room=rooms,.pos={1500,20,1500}},
                  .target={.room=rooms,.pos={1500,300,1800}},.speed=1};
        cameraMoveEye(&c,c.view);
        tr64_game_view_t view={0};
        cameraView(&view,&c);
        printf("display: raw=%d display=%d\n",c.view.pos.y,view.camera_eye[1]);
        assert(c.view.pos.y==20 && view.camera_eye[1]==256);
    }
}

static void check_bearing(void)
{
    /* Exercise every table index/octant representable at scale 2048 in
     * the inherited safe input range, including axes and strict ties. */
    for (int signX=-1;signX<=1;signX+=2)
        for (int signZ=-1;signZ<=1;signZ+=2)
            for (int i=0;i<=2048;++i) {
                int x=signX*i,z=signZ*2048;
                assert(game_tr2_bearing(z,x)==phd_atan(z,x));
                assert(game_tr2_bearing(x,z)==phd_atan(x,z));
            }
    assert(game_tr2_bearing(0,0)==0);
    assert(wrap_i16(game_tr2_bearing(1048576,1048576))==8192);
    assert(wrap_i16(game_tr2_bearing(INT32_MIN,INT32_MIN))==-24576);
    assert(wrap_i16(game_tr2_bearing(INT32_MAX,INT32_MIN))==atanTable[2047]-16384);
#ifndef FROZEN_V2
    assert(game_tr2_horizontal_length(INT32_MIN,INT32_MIN)==3037000499u);
#endif
    puts("bearing: ordinary table/octant identity and full-range signed probes");
}

static void check_trigger(const char *test)
{
    uint16 flags[3]={0};
    gs->cameraFlags=flags;
    Lara lara={0};
#if !defined(FROZEN_V2) && !defined(FROZEN_V3)
    ItemObj unsettled={.type=TR2_ITEM_WALL_SWITCH};
    gs->items=&unsettled;
#endif
    Camera c={.mode=CAMERA_MODE_FOLLOW,.lastIndex=-1,.index=-1,.speed=10};
    if (!strcmp(test,"bit14")) {
        const FloorData trigger[]={word(0x8004),word(0),word(0xc402),word(0x8001)};
#if defined(FROZEN_V2) || defined(FROZEN_V3)
        tr2CameraActions(trigger,&c,&lara);
#else
        tr2Actions(trigger,&c,&lara);
#endif
        assert(c.mode==CAMERA_MODE_FIXED && c.index==2 && c.speed==1 && c.timer==30);
    } else {
        const FloorData trigger[]={word(0x8204),word(0),word(0),word(0x8402),word(0x8000)};
#if defined(FROZEN_V2) || defined(FROZEN_V3)
        tr2CameraActions(trigger,&c,&lara);
#else
        tr2Actions(trigger,&c,&lara);
#endif
        assert(c.mode==CAMERA_MODE_FOLLOW && c.index==-1 && c.timer==0);
    }
    printf("trigger: %s\n",test);
}

int main(int argc,char **argv)
{
    assert(argc==2);
    setvbuf(stdout,NULL,_IONBF,0);
    gs->tr2=&tr2;
    gs->tr2Rules=TR2_RULES_ALL;
#ifndef FROZEN_V2
    gs->tr2Rules|=TR2_RULES_CAMERA;
#endif
    if (!strcmp(argv[1],"bearing"))
        check_bearing();
    else if (!strcmp(argv[1],"vertical") || !strcmp(argv[1],"display"))
        check_geometry(argv[1]);
    else if (!strcmp(argv[1],"bit14") || !strcmp(argv[1],"interaction"))
        check_trigger(argv[1]);
#ifndef FROZEN_V2
    else if (!strcmp(argv[1],"fast-turn")) {
        for (int sign=-1;sign<=1;sign+=2) {
            Lara lara={.state=Lara_STATE_TURN_FAST,.goalState=Lara_STATE_TURN_FAST,
                       .health=1000,.turnSpeed=sign*900,.input=IN_LOOK};
            /* Look consumed the direction before this state handler. */
            Lara_s_STATE_TURN_FAST(&lara);
            int turned=angleLerp(lara.turnSpeed,0,364);
            printf("fast turn under Look: requested=%d damped=%d goal=%u\n",
                   lara.turnSpeed,turned,lara.goalState);
            assert(lara.turnSpeed==sign*1456 && turned==sign*1092 && lara.goalState==Lara_STATE_STOP);
        }
    }
#ifndef FROZEN_V3
    else if (!strcmp(argv[1],"projection")) {
        vec3i step=game_tr2_heading_step(2088,50);
        printf("heading projection: x=%d y=%d z=%d\n",step.x,step.y,step.z);
        assert(step.x==9 && step.y==0 && step.z==48);
    } else if (!strcmp(argv[1],"bounce")) {
        check_geometry(argv[1]);
    }
#endif
#endif
    else
        return 2;
    return 0;
}
