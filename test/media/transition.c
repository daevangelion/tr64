/* SPDX-License-Identifier: MIT */
/* Host test of the video policy (engine/media/transition.c): the complete
 * sequence of a campaign from boot to END, step by step, against the order of
 * the source game's flow (OpenLara gameflow.h getGameVideo: GYM MANSION, 1 SNOW,
 * 4 LIFT, 8A VISION, 10A CANYON, 10B PYRAMID, CUT_4 PRISON, the title END after
 * the end level, CAFE at boot), plus the negatives: restarts and loads play
 * nothing, absent videos report nothing, an unknown level is fatal. argv[1] is
 * engine/in/play.c: its `levels[]` is parsed and every name in it must be known. */
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tr64/eng.h"
#include "tr64/video.h"
static unsigned played[64],count;
static int absent;
static jmp_buf fatal;
int tr64_video_play(unsigned video,uint32_t max_ticks,tr64_video_report_t *report)
{
 if(max_ticks || report || count>=64 || video>=TR64_VIDEO_COUNT) {
  fprintf(stderr,"FAIL: unexpected play call %u\n",video);exit(1);
 }
 if(absent)return TR64_VIDEO_ABSENT;
 played[count++]=video;return TR64_VIDEO_DONE;
}
void sys_error(const char *fmt,...)
{
 va_list ap;va_start(ap,fmt);fprintf(stderr,"fatal (expected): ");vfprintf(stderr,fmt,ap);
 va_end(ap);fputc('\n',stderr);longjmp(fatal,1);
}
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
#define NONE TR64_VIDEO_COUNT
/* One session from the title: new game through the last level to the end. The
 * level entered and the video that must play first (NONE: nothing). */
static const struct { const char *level;unsigned video; } campaign[]={
 {"level1",TR64_VIDEO_SNOW},{"level2",NONE},{"level3a",NONE},{"level3b",NONE},{"cut1",NONE},
 {"level4",TR64_VIDEO_LIFT},{"level5",NONE},{"level6",NONE},{"level7a",NONE},{"level7b",NONE},
 {"cut2",NONE},{"level8a",TR64_VIDEO_VISION},{"level8b",NONE},{"level8c",NONE},
 {"level10a",TR64_VIDEO_CANYON},{"cut3",NONE},{"level10b",TR64_VIDEO_PYRAMID},
 {"cut4",TR64_VIDEO_PRISON},{"level10c",NONE},{"title",TR64_VIDEO_END}
};
#define STEPS (sizeof(campaign)/sizeof(campaign[0]))
static unsigned run(const char *from,const char *to,int advance)
{
 unsigned n;count=0;n=tr64_video_transition(from,to,advance);CHECK(n==count);return n;
}
static int known(const char *level)
{
 if(setjmp(fatal))return 0;
 count=0;(void)tr64_video_transition("title",level,1);
 return 1;
}
/* play.c's levels[]: every name must be known to the policy, none may be missing
 * from the campaign below (gym aside, which is the home level). */
static void play_levels(const char *path)
{
 char text[8192];FILE *f=fopen(path,"rb");size_t n,seen=0;char *p,*end;
 CHECK(f);n=fread(text,1,sizeof(text)-1,f);fclose(f);text[n]=0;
 p=strstr(text,"levels[]={");CHECK(p);p+=strlen("levels[]={");end=strchr(p,'}');CHECK(end);
 while(p<end) {
  char *q;
  p=strchr(p,'"');if(!p || p>=end)break;
  q=strchr(p+1,'"');CHECK(q);*q=0;
  CHECK(known(p+1));
  if(seen>=1) CHECK(seen-1<STEPS && !strcmp(p+1,campaign[seen-1].level));
  else CHECK(!strcmp(p+1,"gym"));
  ++seen;p=q+1;
 }
 CHECK(seen==STEPS+1);
}
int main(int argc,char **argv)
{
 unsigned i,total=0;const char *previous="title";
 static const unsigned whole[]={
  TR64_VIDEO_ESCAPE,TR64_VIDEO_CORE,TR64_VIDEO_CAFE,TR64_VIDEO_SNOW,TR64_VIDEO_LIFT,
  TR64_VIDEO_VISION,TR64_VIDEO_CANYON,TR64_VIDEO_PYRAMID,TR64_VIDEO_PRISON,TR64_VIDEO_END
 };
 unsigned seen[64],seen_count=0;
 CHECK(argc==2);play_levels(argv[1]);
 /* Boot: publisher logo, studio logo, the intro before the title. */
 CHECK(run(NULL,"title",0)==3);
 for(i=0;i<3;++i)seen[seen_count++]=played[i];
 /* The campaign a step at a time; every cutscene level is an ordinary step. */
 for(i=0;i<STEPS;++i) {
  unsigned n=run(previous,campaign[i].level,1);
  if(campaign[i].video==NONE)CHECK(n==0);
  else {CHECK(n==1 && played[0]==campaign[i].video);seen[seen_count++]=played[0];++total;}
  previous=campaign[i].level;
 }
 /* One boot-to-END session shows exactly these ten, in this order. */
 CHECK(seen_count==sizeof(whole)/sizeof(whole[0]));
 for(i=0;i<seen_count;++i)CHECK(seen[i]==whole[i]);
 CHECK(total==7);
 /* PRISON is the video before the cutscene level, not before level10c. */
 CHECK(run("level10b","cut4",1)==1 && played[0]==TR64_VIDEO_PRISON);
 CHECK(run("cut4","level10c",1)==0);
 /* The home level: its video, none back to the title (the game was not ended). */
 CHECK(run("title","gym",1)==1 && played[0]==TR64_VIDEO_MANSION);
 CHECK(run("gym","title",1)==0);
 CHECK(run("level4","title",1)==0);
 /* A restart, a save load or a demo selection (advance 0) plays nothing, not
  * even the end video or a level's own video. */
 for(i=0;i<STEPS;++i)CHECK(run("level2",campaign[i].level,0)==0);
 CHECK(run("title","gym",0)==0);
 CHECK(run("level10c","title",0)==0);
 /* Boot into a level other than the title plays the logos only. */
 CHECK(run(NULL,"level1",0)==2 && played[0]==TR64_VIDEO_ESCAPE && played[1]==TR64_VIDEO_CORE);
 /* A pack without videos: every play reports absent, nothing is counted. */
 absent=1;
 CHECK(run(NULL,"title",0)==0);
 CHECK(run("level10b","cut4",1)==0);
 CHECK(run("level10c","title",1)==0);
 absent=0;
 /* An unknown level name is fatal by name, never silently videoless. */
 CHECK(!known("level9"));
 CHECK(!known("cut5"));
 puts("transition PASS: boot ESCAPE CORE CAFE, SNOW LIFT VISION CANYON PYRAMID PRISON(before cut4) END");
 return 0;
}
