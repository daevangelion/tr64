/* SPDX-License-Identifier: MIT */
/* What a save slot must match to be loadable by this cart: the level's
 * identity in the pack it carries. The pack directory already holds, per level,
 * the CRC32 of its section table (kind, destination, ROM offset, sizes, codec
 * and the CRC of every block), so one 48-byte read per level at boot gives a
 * content identity without loading any level. It is stricter than the game's
 * own load check (core tables + textures): a recook that changes anything of
 * the level, or moves it inside the pack, makes older saves of it unusable.
 * The game's check after the level is loaded stays the exact and final one. */
#include <string.h>
#include "engine/save/save.h"
#include "engine/core/sys.h"
#include "engine/core/log.h"
#include "tr64/io.h"
#include "tr64/level.h"
#include "tr64/limits.h"
#include "tr64/pack.h"
#include "tr64/res.h"
enum { IDENTITIES=32,DIRECTORY_ENTRY=48,DIRECTORY_CRC=40,DIRECTORY_GAME=44 };
static struct { uint32_t name,identity; } table[IDENTITIES];
static unsigned count;
static void service(void)
{
 if(tr64_io_frame_bytes()>=TR64_IO_FRAME)tr64_io_frame();
 tr64_io_pump();
}
static uint32_t name_hash(const char *name)
{ return tr64_crc32(0,(const uint8_t *)name,(uint32_t)strlen(name)); }
void tr64_save_identity_bind(const char *const *names,unsigned names_count)
{
 tr64_pack_t pack;
 uint32_t bytes=tr64_level_pack_bind_streams();
 count=0;
 tr64_load_scope_begin();
 tr64_pack_mount_catalog(&pack,bytes);
 while(!tr64_pack_poll(&pack))service();
 for(uint32_t i=0;i<pack.levels;++i) {
  const uint8_t *entry=pack.directory+i*DIRECTORY_ENTRY;
  for(unsigned j=0;j<names_count;++j) {
   if(strcmp((const char *)entry,names[j]))continue;
#if TR64_CART_TR2
   if(tr64_be32(entry+DIRECTORY_GAME)!=2u) {
    log_printf("save: %s is render-only; not offered for load\n",names[j]);
    break;
   }
#elif TR64_GAME == 3
   if(tr64_be32(entry+DIRECTORY_GAME)!=3u) {
    log_printf("save: %s is render-only; not offered for load\n",names[j]);
    break;
   }
#elif defined(TR64_HOST)
   uint32_t entry_game=tr64_be32(entry+DIRECTORY_GAME);
   if(entry_game && !(TR64_GAME==2 && entry_game==2u)) {
    log_printf("save: %s is render-only; not offered for load\n",names[j]);
    break;
   }
#else
   if(tr64_be32(entry+DIRECTORY_GAME)) {
    log_printf("save: %s is render-only; not offered for load\n",names[j]);
    break;
   }
#endif
   if(count==IDENTITIES)sys_error("save identity table full");
   uint32_t crc=tr64_be32(entry+DIRECTORY_CRC);
   table[count].name=name_hash(names[j]);table[count].identity=crc ? crc : 1;++count;
  }
 }
 tr64_pack_unmount(&pack);
 tr64_load_scope_end();
 log_printf("save: pack identity levels=%u of %u\n",count,names_count);log_flush();
}
uint32_t tr64_save_level_identity(int32_t level_id)
{
 const char *name=game_level_name(level_id);
 if(!name)return 0;
 uint32_t hash=name_hash(name);
 for(unsigned i=0;i<count;++i)if(table[i].name==hash)return table[i].identity;
 return 0;
}
