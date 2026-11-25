/* SPDX-License-Identifier: MIT */
/* Ported from MIT ac3n64 04134cc. */
#include <string.h>
#include "tr64/engine.h"
#include "tr64/mem.h"
#include "tr64/io.h"
#include "tr64/res.h"
#include "engine/core/log.h"
#include "engine/core/sys.h"
static tr64_state_t state;
static void be32(uint8_t *p, uint32_t x)
{ p[0]=(uint8_t)(x>>24); p[1]=(uint8_t)(x>>16); p[2]=(uint8_t)(x>>8); p[3]=(uint8_t)x; }
void tr64_eng_boot(void)
{
 sys_memory_boot();
 tr64_eng_start(0);
 tr64_mem_log(); log_printf("tr64 boot ok\n"); log_flush();
}
void tr64_eng_start(uint32_t seed)
{ memset(&state,0,sizeof(state)); state.seed=seed; }
void tr64_eng_tick(tr64_input_t input)
{
 uint8_t bytes[4]; unsigned i;
 state.input=input;
 bytes[0]=(uint8_t)(input.buttons>>8); bytes[1]=(uint8_t)input.buttons;
 bytes[2]=(uint8_t)input.x; bytes[3]=(uint8_t)input.y;
 state.input_crc=tr64_crc32(state.input_crc,bytes,4);
 ++state.tick;
 tr64_io_frame();
 for (i=0;i<64;++i) tr64_io_pump();
 tr64_scene_tick(input);
}
uint32_t tr64_state_hash(const tr64_state_t *s)
{
 uint8_t p[16];
 be32(p,s->tick); be32(p+4,s->seed); be32(p+8,s->input_crc);
 p[12]=(uint8_t)(s->input.buttons>>8); p[13]=(uint8_t)s->input.buttons;
 p[14]=(uint8_t)s->input.x; p[15]=(uint8_t)s->input.y;
 return tr64_crc32(0,p,sizeof(p));
}
const tr64_state_t *tr64_eng_state(void) { return &state; }
void *eng_game_state_alloc(uint32_t bytes)
{ return tr64_mem_alloc(TR64_MEM_state,bytes); }
