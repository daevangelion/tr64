/* SPDX-License-Identifier: MIT */
/* Host test of the video unit's arena (engine/media/film_arena.h, the code the
 * unit runs): the decoder's per-keyframe parameter-set churn must not grow the
 * arena, pictures keep their guard bands, and misuse stops by name. */
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/media/film_arena.h"
static jmp_buf fatal;
static char message[160];
void sys_error(const char *fmt,...)
{
 va_list ap;va_start(ap,fmt);vsnprintf(message,sizeof(message),fmt,ap);va_end(ap);longjmp(fatal,1);
}
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
#define PICTURE 115248u
static uint8_t memory[2u*1024*1024] __attribute__((aligned(16)));
#define FAILS(call,text) do{ if(!setjmp(fatal)){call;fprintf(stderr,"FAIL line %d: no fatal\n",__LINE__);exit(1);} \
 CHECK(strstr(message,text)); }while(0)
int main(void)
{
 film_arena_t a;void *base[13],*sps,*vui,*again;unsigned i,k;uint32_t peak;
 film_arena_init(&a,memory,sizeof(memory),4096);
 /* What the decoder holds for the whole film (13 allocations on the cart): a
  * decoder, the macroblock tables, two pictures with their guard bands. */
 base[0]=film_arena_alloc(&a,275088,16);
 base[1]=film_arena_alloc(&a,PICTURE,16);base[2]=film_arena_alloc(&a,PICTURE,16);
 for(i=3;i<13;++i)base[i]=film_arena_alloc(&a,48u+16u*i,16);
 /* Guard bands: nothing else lies within FILM_GUARD of a picture. */
 CHECK((uint8_t *)base[1]-(uint8_t *)base[0]-275088>=FILM_GUARD);
 CHECK((uint8_t *)base[2]-(uint8_t *)base[1]-PICTURE>=2u*FILM_GUARD);
 CHECK((uint8_t *)base[3]-(uint8_t *)base[2]-PICTURE>=FILM_GUARD);
 CHECK(a.live==13 && a.allocs==13 && ((uintptr_t)base[1]&15u)==0);
 /* One keyframe: parameter set allocated, found equal, freed (in the SDK's order). */
 sps=film_arena_alloc(&a,960,16);film_arena_free(&a,sps);
 peak=a.peak;
 /* The next 5,000 keyframes (the old arena overflowed after about 521). */
 for(k=0;k<5000;++k) {
  again=film_arena_alloc(&a,960,16);
  CHECK(again==sps);
  film_arena_free(&a,again);
 }
 CHECK(a.peak==peak && a.live==13 && a.allocs==13+5001);
 /* Interleaved sizes and out-of-order frees reuse exactly, never overlap. */
 sps=film_arena_alloc(&a,96,16);vui=film_arena_alloc(&a,960,16);
 film_arena_free(&a,sps);film_arena_free(&a,vui);
 CHECK(film_arena_alloc(&a,960,16)==vui && film_arena_alloc(&a,96,16)==sps);
 CHECK(film_arena_alloc(&a,112,16)!=sps);  /* a size never freed is a new block */
 film_arena_free(&a,sps);film_arena_free(&a,vui);
 /* Pictures are never reused: a freed guarded block stays dead. */
 film_arena_free(&a,base[1]);
 CHECK(film_arena_alloc(&a,PICTURE,16)!=base[1]);
 /* Misuse stops by name: double free, a pointer inside a block, one outside the arena. */
 FAILS(film_arena_free(&a,base[1]),"foreign");       /* double free */
 FAILS(film_arena_free(&a,(uint8_t *)base[0]+16),"foreign");
 FAILS(film_arena_free(&a,memory),"foreign");
 FAILS(film_arena_alloc(&a,0,16),"allocation request");
 FAILS(film_arena_alloc(&a,16,8192),"allocation request");
 /* Exhaustion is a named fatal, whichever limit comes first. */
 film_arena_init(&a,memory,sizeof(memory),0);
 FAILS(for(;;)(void)film_arena_alloc(&a,300000,16),"arena overflow");
 film_arena_init(&a,memory,sizeof(memory),0);
 FAILS(for(i=0;i<FILM_ARENA_BLOCKS+1;++i)(void)film_arena_alloc(&a,16u+16u*i,16),"block table full");
 puts("arena PASS: 5000 parameter-set cycles at constant peak, guards, reuse, misuse by name");
 return 0;
}
