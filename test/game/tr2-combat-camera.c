/* SPDX-License-Identifier: MIT */
/* Link to the normal host build objects except plat/host/main.o. */
#include "game/common/internal.h"
#include <stdio.h>

static void expect(bool condition,const char *message)
{
    if (!condition) {
        fprintf(stderr,"FAIL: %s\n",message);
        exit(1);
    }
}

int main(void)
{
    static GameState state;
    GameTR2 tr2={0};
    gs=&state;
    gs->tr2=&tr2;
    gs->tr2Rules=TR2_RULES_ALL | TR2_RULE_COMBAT_CAMERA;
    RoomInfo info={0};
    memcpy(&info.x_sectors,"\0\12",2);
    memcpy(&info.z_sectors,"\0\12",2);
    Sector sectors[100];
    for (unsigned i=0;i<100;++i)
        sectors[i]=(Sector){.boxIndex=UINT16_MAX,.roomAbove=255,.roomBelow=255,.floor=20,.ceiling=-20};
    Room room={.info=&info,.sectors=sectors};
    gs->rooms=&room;
    ExtraInfoLara pose={0};
    Lara lara={.angle={17000,32000,0},.pos={5120,512,5120},.room=&room,.extraL=&pose};
    ItemObj other={.pos={8192,768,6656}};
    pose.head.angle=vec3s_create(-17000,-32000,0);
    pose.armR.target=&other;
    pose.armR.angleAim=vec3s_create(-17000,17152,0); /* Un-eased absolute sight (0,-quarter turn). */
    Camera *c=&pose.camera;
    const Location aim={.pos={5120,0,5120},.room=&room};
    *c=(Camera){.view=aim,.target=aim,.targetDist=2560,.laraItem=&lara,
                .speed=1,.mode=CAMERA_MODE_COMBAT,.followRadius=1536,.center=true};
    game_tr2_camera_move(c,&aim);
    expect(c->view.pos.x==7680 && c->view.pos.y==0 && c->view.pos.z==5120,
           "selected weapon sight sets the eye immediately despite both arm locks being clear");
    expect(c->target.pos.x==5120 && c->target.pos.y==0 && c->target.pos.z==5120 && c->target.room==&room,
           "Combat holds Lara's X/Z and live room, not the weapon's selected item");
    expect(c->followRadius==1536,"Combat does not replace the retained Follow radius");

    pose.armR.target=NULL;
    pose.armR.aim=true;
    c->view=aim;
    game_tr2_camera_move(c,&aim);
    expect(c->view.pos.x==5120 && c->view.pos.y==0 && c->view.pos.z==2560,
           "clearing selection uses standing gaze, independent of an arm flag");

    pose.head.angle.x=-17000-8192;
    lara.waterState=WATER_STATE_UNDER;
    tr2.waterDistance=-1024;
    c->view=aim;
    game_tr2_camera_move(c,&aim);
    expect(c->view.pos.x==5120 && c->view.pos.y==-512 && c->view.pos.z==4609,
           "underwater crossing clips the nominal ray against current Y plus stored water distance");
    puts("PASS selected un-eased sight, selection loss, retained Follow radius and underwater Combat plane");
    return 0;
}
