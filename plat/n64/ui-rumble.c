/* SPDX-License-Identifier: MIT */
#include <joypad.h>
#include "engine/ui/frontend.h"
int tr64_ui_backend_rumble(unsigned slot,int active)
{
 const joypad_port_t port=slot?JOYPAD_PORT_2:JOYPAD_PORT_1;
 if(!joypad_get_rumble_supported(port))return 0;
 joypad_set_rumble_active(port,active!=0);return 1;
}
