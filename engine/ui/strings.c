/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include "tr64/game.h"
#include "engine/ui/ui.h"
#include "ui_string_ids.h"
#include "ui_string_data.h"
const char *tr64_ui_string(unsigned id)
{
#if TR64_HAS_TR2
 /* TR2 display names: tr2-items-general and source campaign data. */
 static const char *const tr2_names[]={
  "The Great Wall","Guardhouse Key","Flares","Automatic Pistols","Harpoon Gun",
  "M16","Grenade Launcher","Automatic Pistol Ammo","Harpoons","M16 Ammo",
  "Grenades","Quest Item","Venice","Tomb Raider II"
 };
 if(id>=TR64_STR_TR2_WALL && id<TR64_STR_TR2_WALL+(unsigned)(sizeof(tr2_names)/sizeof(*tr2_names)))
  return tr2_names[id-TR64_STR_TR2_WALL];
#endif
#if TR64_GAME == 3
 static const char *const tr3_names[]={
  "Lara's House","Jungle","Temple Ruins","The River Ganges","Caves of Kaliya",
  "Desert Eagle","MP5","Harpoon Gun","Rocket Launcher","Grenade Launcher",
  "Flares","Save Crystal","Indra Key","Tomb Raider III"
 };
 if(id>=TR64_STR_TR3_HOUSE && id<TR64_STR_TR3_HOUSE+(unsigned)(sizeof(tr3_names)/sizeof(*tr3_names)))
  return tr3_names[id-TR64_STR_TR3_HOUSE];
#endif
 if(id>=TR64_UI_STRING_COUNT)return 0;
 return (const char *)string_bytes+string_offsets[id];
}
uint32_t tr64_ui_string_bytes(void)
{ return (uint32_t)(sizeof(string_bytes)+sizeof(string_offsets)+sizeof(string_widths)); }
const uint16_t *tr64_ui_string_widths(uint32_t *count)
{
 if(count)*count=TR64_UI_STRING_COUNT;
 return string_widths;
}
int tr64_ui_text_id(int x,int y,unsigned id,unsigned align)
{
 const char *text=tr64_ui_string(id);
 return text && tr64_ui_text(x,y,text,align);
}
