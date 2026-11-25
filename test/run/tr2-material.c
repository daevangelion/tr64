/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>
#include "engine/render/internal.h"
#include "plat/n64/gpu-ci4.h"
#include "engine/core/sys.h"
#include "engine/render/load.h"

/* This harness exercises the real cold bind without opening a device. */
void sys_error(const char *message,...) { fputs(message,stderr);abort(); }
void tr64_gpu_bind_textures(tr64_gpu_span_t tx) { (void)tx; }
static void w32(uint8_t *p,uint32_t v)
{ for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(24-8*i)); }
static void w16(uint8_t *p,unsigned v)
{ p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v; }
static void geo_header(uint8_t *p,const char *magic,unsigned bytes,unsigned batches)
{
 for(unsigned i=0;i<4;++i)p[i]=(uint8_t)magic[i];
 w32(p+4,1);w32(p+8,bytes);w32(p+12,1);
 w32(p+16,batches);w32(p+20,batches);w32(p+24,batches);w32(p+28,2*batches);
 w32(p+32,64);w32(p+36,96);w32(p+40,96+32*batches);
 w32(p+44,96+56*batches);w32(p+48,96+64*batches);
 w32(p+52,batches);w32(p+56,1);
}
static int test_bind(void)
{
 uint8_t rg[96] __attribute__((aligned(16)))={0};
 uint8_t mg[224] __attribute__((aligned(16)))={0};
 uint8_t tx[90] __attribute__((aligned(16)))={'T','X','6','4'};
 uint8_t work[64] __attribute__((aligned(16)));
 tr64_gpu_span_t textures={tx,sizeof(tx)},rooms={rg,sizeof(rg)},meshes={mg,sizeof(mg)};
 tr64_level_t level={.ready=1,.textures=tx,.texture_bytes=sizeof(tx)};
 tr64_core_rooms_t room={0};
 tr64_core_static_instances_t inst={.intensity={tr64_core_be16(6144)},
                                   .reserved={tr64_core_be16(4096)}};
 tr64_core_static_meshes_t def={0};
 tr64_core_mesh_pointers_t pointer={0};
 tr64_render_t r;
 tr64_render_error_t e;
#define TABLE(name, row) level.tables[TR64_CORE_##name-1]=(tr64_level_table_t){.data=(const void *)&row,.count=1,.stride=sizeof(row)}
 TABLE(rooms,room);TABLE(static_instances,inst);TABLE(static_meshes,def);TABLE(mesh_pointers,pointer);
#undef TABLE
 room.alternate_room.raw=tr64_core_be16(65535u);room.instance_count.raw=tr64_core_be32(1);
 def.flags.raw=tr64_core_be16(2);
 geo_header(rg,"RG64",sizeof(rg),0);geo_header(mg,"MG64",sizeof(mg),1);
 w16(mg+64+8,1);w16(mg+64+18,1024);w16(mg+64+20,1);
 w16(mg+96+6,1024);w16(mg+96+8,1);w16(mg+96+12,3);w32(mg+96+16,160);
 mg[128+3]=TR64_GPU_FLAT;w16(mg+128+6,1);
 mg[152]=0;mg[153]=1;mg[154]=2;mg[155]=255;
 w16(mg+160+8,1024);w16(mg+192+2,1);
 w32(tx+8,sizeof(tx));w32(tx+32,1);w32(tx+36,1);w32(tx+64,88);
#define BIND() tr64_render_bind_load(&r,&level,textures,rooms,meshes,work,sizeof(work),&e)
 if(BIND() || r.world_max[0]!=1024 || static_light_offset(r.static_shade_offset,&inst)!=2048)return 1;
 level.game=2;
 if(static_light_offset(r.static_shade_offset,&inst)!=2048)return 2;
 if(BIND() || static_light_offset(r.static_shade_offset,&inst)!=0)return 3;
 inst.x.raw=tr64_core_be32(262000);
 if(BIND()!=-1 || e.code!=TR64_RENDER_BOUNDS || e.section!=0)return 4;
 /* The unchecked object box used to hide the outlying validated batch. */
 w16(mg+64+18,0);w16(mg+64+20,0);
 if(BIND()!=-1 || e.code!=TR64_RENDER_BOUNDS || e.section!=TR64_RENDER_SECTION_MESHES ||
    e.offset!=76)return 5;
#undef BIND
 return 0;
}

int main(void)
{
 int result=test_bind();
 if(result) { fprintf(stderr,"TR2 material bind control failed: %d\n",result);return 1; }
 /* A 3x5 sprite at (6,7) loads CI4 nibbles 6..9 as I8 bytes 3..4,
  * but the drawable tile clamps to local texel (2,4), not (3,4). */
 const tr64_ci4_rect_t odd=tr64_ci4_rect(6,7,3,5), even=tr64_ci4_rect(6,7,4,5);
 if(odd.load_a!=0xf400c01cu || odd.load_b!=0x0101002cu ||
    odd.clamp_a!=0xf2000000u || odd.clamp_b!=0x00008010u)return 3;
 if(odd.load_a!=even.load_a || odd.load_b!=even.load_b ||
    even.clamp_b!=0x0000c010u)return 4;
 puts("TR2 bound static shade, enclosing mesh bounds and CI4 command controls PASS");
 return 0;
}
