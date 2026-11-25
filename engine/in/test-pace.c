/* SPDX-License-Identifier: MIT */
/* The accumulator against a scripted clock: what a frame of a given length is followed by. The model
 * loop is the play loop's shape (wait while nothing is due; run the due ticks; draw, which costs the
 * scripted time), with a 50 us poll like a spinning loop on the cart. */
#include <stdio.h>
#include <stdlib.h>
#include "engine/in/pace.h"
#include "engine/core/frame.h"

#define CHECK(c) do { if(!(c)) { fprintf(stderr,"pace test line %d: %s\n",__LINE__,#c);exit(1); } } while(0)
enum { POLL_US=50,MAX_FRAMES=4096 };
typedef struct { uint32_t now,ticks[MAX_FRAMES],frames,run,start; } model_t;
/* One frame: wait for a tick, run what is due, draw for `cost_us`. Returns ticks run (0..4). */
static uint32_t frame(model_t *m,tr64_pace_t *p,uint32_t cost_us)
{
 uint32_t due;
 for(;;) { m->now+=POLL_US;due=tr64_pace_due(p,m->now);if(due)break; }
 m->ticks[m->frames++]=due;m->run+=due;m->now+=cost_us;
 return due;
}
static uint32_t ms(uint32_t n) { return n*1000u; }
/* Ticks the clock has owed since `since`, to the tick (30 per second exactly). */
static uint32_t owed(uint32_t now,uint32_t since,uint32_t first) { return (uint32_t)(((uint64_t)(now-since)*TR64_TICK_HZ)/1000000u)+first; }
static void begin(model_t *m,tr64_pace_t *p,uint32_t at)
{ m->now=m->start=at;m->frames=m->run=0;tr64_pace_reset(p,at,1); }
static uint32_t hist(const model_t *m,uint32_t from,uint32_t n,uint32_t want)
{ uint32_t i,c=0;for(i=from;i<from+n;++i)c+=m->ticks[i]==want;return c; }
/* Run `frames` frames of `cost` ms; no tick is ever lost: ticks run + dropped == owed by the clock. */
static void steady(model_t *m,tr64_pace_t *p,uint32_t cost,uint32_t frames)
{ uint32_t i;for(i=0;i<frames;++i)frame(m,p,ms(cost)); }
static void conserved(const model_t *m,const tr64_pace_t *p)
{
 /* The last frame began at p->last; every tick up to it was run or dropped. */
 const uint32_t due=owed(p->last,m->start,1);
 CHECK(m->run==p->ticks && p->ticks+p->dropped==due);
}
/* The legacy loop (engine/core/frame.h, one tick per due frame, phase kept) for the negative control. */
static uint32_t legacy_ticks(const uint32_t *cost_ms,uint32_t count,uint32_t seconds_us)
{
 uint32_t now=0,phase=now-TR64_FRAME_US,ticks=0,i=0;
 while((uint32_t)(now-0)<seconds_us && i<count) {
  now+=POLL_US;
  if(tr64_frame_due(&phase,now)) { ++ticks;now+=ms(cost_ms[i++]); }
 }
 return ticks;
}
int main(void)
{
 model_t *m=calloc(1,sizeof(*m));tr64_pace_t p;uint32_t i,t;
 CHECK(m);
 /* 20 ms frames are faster than a tick: one tick each, paced by waiting, 30 a second. */
 begin(m,&p,1000);steady(m,&p,20,300);
 CHECK(hist(m,0,300,1)==300 && p.dropped==0);conserved(m,&p);
 CHECK(p.ticks==300);
 /* 33 ms frames sit just under a tick: still one each. */
 begin(m,&p,5000);steady(m,&p,33,300);CHECK(hist(m,0,300,1)==300 && p.dropped==0);conserved(m,&p);
 /* 50 ms: one tick, then two, alternating: 1.5 a frame = 30 a second. */
 begin(m,&p,0);steady(m,&p,50,200);
 CHECK(hist(m,0,200,1)+hist(m,0,200,2)==200 && hist(m,0,200,1)>=95 && hist(m,0,200,2)>=95 && p.dropped==0);
 for(i=1;i<200;++i)CHECK(!(m->ticks[i]==2 && m->ticks[i-1]==2));
 conserved(m,&p);
 /* 100 ms: three a frame. */
 begin(m,&p,123456);steady(m,&p,100,60);CHECK(m->ticks[0]==1 && hist(m,1,59,3)==59 && p.dropped==0);conserved(m,&p);
 /* 133 ms: four, the bound, nothing dropped yet. */
 begin(m,&p,0);steady(m,&p,133,40);CHECK(hist(m,1,39,3)+hist(m,1,39,4)==39 && hist(m,1,39,4)>=35 && p.dropped==0);conserved(m,&p);
 /* 400 ms: twelve due, four run, eight dropped and counted, every frame. */
 begin(m,&p,0);steady(m,&p,400,10);
 CHECK(m->ticks[0]==1 && hist(m,1,9,4)==9 && p.dropped>=71 && p.dropped<=73);conserved(m,&p);
 CHECK(p.histogram[4]==9 && p.frames==10);
 /* Mixture of frame costs: ticks follow the clock whatever the order. */
 {
  static const uint32_t mix[]={20,50,100,33,20,50,50,100,33,20,70,20,20,90,45};
  begin(m,&p,77);for(t=0;t<40;++t)for(i=0;i<sizeof(mix)/sizeof(mix[0]);++i)frame(m,&p,ms(mix[i]));
  CHECK(p.dropped==0);conserved(m,&p);
  CHECK(p.histogram[1]+p.histogram[2]*2+p.histogram[3]*3+p.histogram[4]*4==p.ticks);
 }
 /* A stall the loop does not know of (900 ms in a frame): one bounded burst of four, 23 dropped, and
  * the very next frame is ordinary. */
 begin(m,&p,0);steady(m,&p,20,30);frame(m,&p,ms(900));
 CHECK(frame(m,&p,ms(20))==4 && p.dropped>=22 && p.dropped<=24);
 for(i=0;i<20;++i)CHECK(frame(m,&p,ms(20))==1);
 conserved(m,&p);
 /* The same stall followed by a restart (what a load, a video or a save does): no burst at all. */
 begin(m,&p,0);steady(m,&p,20,30);m->now+=ms(900);tr64_pace_restart(&p,m->now);
 CHECK(p.restarts==1 && p.dropped==0);
 for(i=0;i<20;++i)CHECK(frame(m,&p,ms(20))==1);
 CHECK(p.dropped==0);
 /* Waiting: 32 ms after a restart nothing is due, 34 ms after, one is. */
 tr64_pace_restart(&p,5000);
 CHECK(tr64_pace_due(&p,5000+ms(32))==0 && tr64_pace_due(&p,5000+ms(33))==0 && tr64_pace_due(&p,5000+33334)==1);
 /* Exactly 30 ticks in one second, polled at every microsecond, from any origin (also across the
  * 32-bit clock wrap). */
 {
  static const uint32_t origin[]={0,1,999999,0xfffffe00u,0xffffff00u};
  for(t=0;t<sizeof(origin)/sizeof(origin[0]);++t) {
   uint32_t now=origin[t],n=0,k;tr64_pace_reset(&p,now,0);
   for(k=0;k<3000000;++k) { ++now;n+=tr64_pace_due(&p,now); }
   CHECK(n==90 && p.dropped==0);
  }
 }
 /* An interval beyond the clamp (a long stall) is charged as such, not as a wrapped small one. */
 tr64_pace_reset(&p,0,0);CHECK(tr64_pace_due(&p,0x80000000u)==4 && p.dropped>1000);
 /* Negative control: the legacy loop (one tick per frame, phase kept) cannot follow the clock. */
 {
  static const uint32_t cost[]={50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50};
  const uint32_t seconds=2000000u,old=legacy_ticks(cost,sizeof(cost)/sizeof(cost[0]),seconds);
  begin(m,&p,0);while(m->now<seconds)frame(m,&p,ms(50));
  printf("pace negative control: 50 ms frames for 2 s: legacy loop %lu ticks, accumulator %lu, clock owes %lu\n",
   (unsigned long)old,(unsigned long)p.ticks,(unsigned long)owed(m->now,0,1));
  CHECK(old+8<owed(seconds,0,1));         /* the legacy loop falls well short of 60 ticks... */
  CHECK(p.ticks+3>=owed(seconds,0,1) && p.ticks<=owed(m->now,0,1)); /* ...the accumulator does not */
 }
 printf("pace PASS: 20/33/50/100/133/400 ms frames and mixtures follow the 30 Hz clock; catch-up bounded at %d ticks with drops counted; restart after a stall leaves no burst; exact 30 ticks per second across the clock wrap\n",TR64_PACE_MAX_TICKS);
 free(m);return 0;
}
