/* SPDX-License-Identifier: MIT */
/* Host decoder behind engine/media/film.h. The SDK's H.264 decoder is RSP code
 * and cannot run here, so the stream the engine reads through engine/io (every
 * byte, CRC-checked) is written to a file and decoded by FFmpeg; the pictures
 * are then presented through the same path as the cart's. Host-only test aid:
 * it proves stream delivery, timing logic, arena accounting and the colour
 * conversion's output, not the SDK decoder or its cost. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "engine/media/film.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
#define PICTURE_BYTES (TR64_WIDTH*TR64_HEIGHT*3u/2u)
extern void sys_host_clock_skip(uint32_t us); /* plat/host/sys.c */
static tr64_film_memory_t arena;
static uint8_t *picture;
static FILE *frames;
static char directory[256],stream_path[300],raw_path[300];
static uint32_t count,index,taken;
static uint16_t pixels[TR64_WIDTH*TR64_HEIGHT];
static int clamp(int v) { return v<0 ? 0 : v>255 ? 255 : v; }
static void rgb(const uint8_t *yuv,unsigned x,unsigned y,int *r,int *g,int *b)
{
 int luma=yuv[y*TR64_WIDTH+x];
 int u=yuv[TR64_WIDTH*TR64_HEIGHT+(y/2)*(TR64_WIDTH/2)+x/2]-128;
 int v=yuv[TR64_WIDTH*TR64_HEIGHT*5/4+(y/2)*(TR64_WIDTH/2)+x/2]-128;
 /* BT.709 full range, 10-bit fixed point. */
 *r=clamp(luma+((1613*v)>>10));
 *g=clamp(luma-((192*u+479*v)>>10));
 *b=clamp(luma+((1900*u)>>10));
}
static int wanted(uint32_t picture_index)
{
 const char *list=getenv("TR64_VIDEO_DUMP_FRAMES");
 while(list && *list) {
  char *end;unsigned long n=strtoul(list,&end,10);
  if(end==list)break;
  if(n==picture_index)return 1;
  list=*end==',' ? end+1 : end;
 }
 return 0;
}
static void dump(uint32_t picture_index)
{
 const char *where=getenv("TR64_VIDEO_DUMP");
 char path[400];FILE *f;unsigned x,y;
 if(!where || !wanted(picture_index))return;
 snprintf(path,sizeof(path),"%s/film-%05lu.ppm",where,(unsigned long)picture_index);
 f=fopen(path,"wb");
 if(!f)sys_error("film host: cannot write %s",path);
 fprintf(f,"P6\n%d %d\n255\n",TR64_WIDTH,TR64_HEIGHT);
 for(y=0;y<TR64_HEIGHT;++y)for(x=0;x<TR64_WIDTH;++x) {
  int r,g,b;uint8_t out[3];
  rgb(picture,x,y,&r,&g,&b);out[0]=(uint8_t)r;out[1]=(uint8_t)g;out[2]=(uint8_t)b;
  fwrite(out,1,3,f);
 }
 if(fclose(f))sys_error("film host: write failed %s",path);
}
int tr64_film_decoder_open(const tr64_film_memory_t *memory,const tr64_film_source_t *source,
 tr64_film_info_t *info)
{
 char command[900];uint8_t chunk[4096];FILE *out;int n;long bytes;
 const char *tmp=getenv("TMPDIR");
 snprintf(directory,sizeof(directory),"%s/tr64-film-XXXXXX",tmp && *tmp ? tmp : "/tmp");
 if(!mkdtemp(directory))sys_error("film host: temp directory");
 snprintf(stream_path,sizeof(stream_path),"%s/stream.h264",directory);
 snprintf(raw_path,sizeof(raw_path),"%s/pictures.yuv",directory);
 arena=*memory;
 out=fopen(stream_path,"wb");
 if(!out)sys_error("film host: cannot write stream");
 while((n=source->read(source->context,chunk,sizeof(chunk)))>0)
  if(fwrite(chunk,1,(size_t)n,out)!=(size_t)n)sys_error("film host: stream write");
 if(n<0 || fclose(out))sys_error("film host: stream read failed");
 snprintf(command,sizeof(command),"ffmpeg -v error -y -f h264 -i '%s' -f rawvideo '%s'",stream_path,raw_path);
 if(system(command))sys_error("film host: ffmpeg decode failed (is ffmpeg installed?)");
 frames=fopen(raw_path,"rb");
 if(!frames || fseek(frames,0,SEEK_END) || (bytes=ftell(frames))<0 || bytes%PICTURE_BYTES || !bytes)
  sys_error("film host: decoded picture file");
 rewind(frames);count=(uint32_t)(bytes/PICTURE_BYTES);index=taken=0;
 picture=memory->alloc(memory->context,PICTURE_BYTES,16);
 if(!picture)sys_error("film host: arena");
 info->width=TR64_WIDTH;info->height=TR64_HEIGHT;info->fps_x1000=TR64_FILM_FPS*1000u;
 return 1;
}
int tr64_film_decoder_step(void) { return 0; }
int tr64_film_decoder_next(void)
{
 if(index>=count)return 0;
 if(fread(picture,1,PICTURE_BYTES,frames)!=PICTURE_BYTES)sys_error("film host: picture read");
 taken=index++;return 1;
}
void tr64_film_decoder_present(tr64_film_service_t service,void *context)
{
 unsigned x,y;
 (void)service;(void)context;
 for(y=0;y<TR64_HEIGHT;++y)for(x=0;x<TR64_WIDTH;++x) {
  int r,g,b;rgb(picture,x,y,&r,&g,&b);
  pixels[y*TR64_WIDTH+x]=(uint16_t)((r>>3)<<11|(g>>3)<<6|(b>>3)<<1|1);
 }
 dump(taken);
 sys_present(pixels);
}
void tr64_film_decoder_close(void)
{
 if(!frames)return;
 fclose(frames);frames=NULL;
 arena.free(arena.context,picture);picture=NULL;
 unlink(stream_path);unlink(raw_path);rmdir(directory);
}
void tr64_film_idle(uint32_t us) { sys_host_clock_skip(us); }
