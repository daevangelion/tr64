/* SPDX-License-Identifier: MIT */
/* Production UI primitive probe: constants are chosen from the documented
 * TITLE lightmap measurement before capture, never from expected pixels. */
#include "engine/ui/ui.h"
#include "engine/core/sys.h"
void tr64_ui_test_overlay(void)
{
 for(unsigned row=0;row<3;++row)for(unsigned c=0;c<32;++c) {
  uint16_t colour=(uint16_t)(c<<11|c<<6|c<<1|1);
  tr64_ui_backend_rect(c*10,80+row*20,10,20,colour);
 }
 tr64_ui_shade(0,100,320,20,25);
 tr64_ui_shade(0,120,320,20,27);
 tr64_ui_bar(12,150,100,128,TR64_UI_HEALTH);
 if(!tr64_ui_text(4,190,"Shade 25 / 27",TR64_UI_LEFT))
  sys_error("UI shade specimen font");
}
