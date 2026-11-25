/* SPDX-License-Identifier: MIT */
/* Test-only overlay on the production UI pass. Glyphs remain the level data's
 * own sprite sequence; no captured image or alternate font is used. */
#include "engine/ui/ui.h"
#include "engine/core/sys.h"
void tr64_ui_test_overlay(void)
{
 if(!tr64_ui_text(0,160,"Tomb Raider",TR64_UI_CENTRE) ||
    !tr64_ui_text(0,180,"0123456789 p",TR64_UI_CENTRE))
  sys_error("UI font specimen failed");
}
