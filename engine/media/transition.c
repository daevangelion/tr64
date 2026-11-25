/* SPDX-License-Identifier: MIT */
/* Which video the original game shows where (docs/formats/n64-video.md). Pure
 * policy over level names, so a host test can assert the whole campaign. */
#include <string.h>
#include "engine/core/sys.h"
#include "tr64/video.h"
#define NONE TR64_VIDEO_COUNT
/* Every level of the play loop, in campaign order, with the video its start
 * shows. Derived from the source game's flow (OpenLara src/gameflow.h
 * getGameVideo, played when the level starts and only when the level changes):
 * TR1 GYM MANSION, 1 SNOW, 4 LIFT, 8A VISION, 10A CANYON, 10B PYRAMID,
 * CUT_4 PRISON (the cutscene level, so before cut4 and not before 10C);
 * TITLE shows END when the game was ended (10C, the end level, goes to the
 * title) and CAFE at boot, the title never otherwise. The other cutscene
 * levels (CUT_1 to CUT_3) and all other levels have none. An unknown level
 * name is fatal: the table must name every level the play loop can run. */
static const struct { const char *level;unsigned video; } before[]={
 {"gym",TR64_VIDEO_MANSION},{"level1",TR64_VIDEO_SNOW},{"level2",NONE},{"level3a",NONE},
 {"level3b",NONE},{"cut1",NONE},{"level4",TR64_VIDEO_LIFT},{"level5",NONE},{"level6",NONE},
 {"level7a",NONE},{"level7b",NONE},{"cut2",NONE},{"level8a",TR64_VIDEO_VISION},
 {"level8b",NONE},{"level8c",NONE},{"level10a",TR64_VIDEO_CANYON},{"cut3",NONE},
 {"level10b",TR64_VIDEO_PYRAMID},{"cut4",TR64_VIDEO_PRISON},{"level10c",NONE},{"title",NONE}
};
unsigned tr64_video_transition(const char *from,const char *to,int advance)
{
 unsigned i,played=0;
 if(!from) {
  /* Boot: publisher logo, studio logo, then the intro before the title. */
  played+=tr64_video_play(TR64_VIDEO_ESCAPE,0,NULL)!=TR64_VIDEO_ABSENT;
  played+=tr64_video_play(TR64_VIDEO_CORE,0,NULL)!=TR64_VIDEO_ABSENT;
  if(!strcmp(to,"title"))played+=tr64_video_play(TR64_VIDEO_CAFE,0,NULL)!=TR64_VIDEO_ABSENT;
  return played;
 }
 if(!advance)return 0;
 if(!strcmp(from,"level10c") && !strcmp(to,"title"))
  return tr64_video_play(TR64_VIDEO_END,0,NULL)!=TR64_VIDEO_ABSENT;
 for(i=0;i<sizeof(before)/sizeof(before[0]);++i)
  if(!strcmp(to,before[i].level)) {
   if(before[i].video!=NONE)played+=tr64_video_play(before[i].video,0,NULL)!=TR64_VIDEO_ABSENT;
   return played;
  }
 sys_error("video: transition to unknown level %s",to);
}
