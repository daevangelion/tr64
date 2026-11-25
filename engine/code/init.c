/* SPDX-License-Identifier: MIT */
/* Boot-only unit. Its only address is installed by code_boot, called once;
 * its dispatch entry is removed before code_boot returns. No runtime caller. */
#include <stdint.h>
uint32_t tr64_unit_init(uint32_t value)
{
 static const char message[]="TR64 M0 boot-only initialization";
 uint32_t hash=value; unsigned i;
 for(i=0;i<sizeof(message)-1;++i)hash=hash*33u+(uint8_t)message[i];
 return hash;
}
