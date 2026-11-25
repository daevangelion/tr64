/* SPDX-License-Identifier: MIT */
#include "engine/ui/frontend.h"
/* The diagnostic host has no Joybus controller/accessory device. Zero reports
 * absent optional hardware, exactly like a cart port without a Rumble Pak. */
int tr64_ui_backend_rumble(unsigned slot,int active)
{ (void)slot;(void)active;return 0; }
