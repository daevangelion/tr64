/* SPDX-License-Identifier: MIT */
/* Explicit TR64_TEST scene probe for hud-underwater.route + --render-measure.
 * Drive the existing HUD test hook with >64 KiB of glyph records, after the
 * real one-view world. Each of the 11 calls queues 400 visible glyphs; no
 * capacity knobs or synthetic record reservations are in the renderer. */
#include "engine/core/sys.h"
#include "engine/ui/ui.h"
void tr64_ui_test_overlay(void)
{
 tr64_ui_glyph_t glyphs[400];
 if(tr64_ui_text_glyphs(12,140,"A",TR64_UI_LEFT,glyphs,400)!=1)
  sys_error("glyph drain probe: missing font glyph");
 for(unsigned i=1;i<400;++i)glyphs[i]=glyphs[0];
 for(unsigned i=0;i<11;++i)tr64_ui_backend_glyphs(glyphs,400,tr64_ui_font_sprite);
}
