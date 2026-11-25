# SPDX-License-Identifier: BSD-2-Clause
# Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/game.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
# Top-level game module dispatcher: shared code lives in game/common/,
# per-game code lives in game/tr1/ .. game/tr5/.
ifeq ($(TR64_LEVEL_LOADER),1)
ifeq ($(TR64_GAME),3)
include game/tr3/module.mk
else
TR64_ENGINE_SOURCES += \
 game/common/game.c game/common/item.c game/common/lara.c game/common/lara_tr3.c \
 game/common/dispatch.c game/common/room.c game/common/static.c game/common/world.c \
 game/common/trigger.c game/common/camera.c game/common/camera_tick.c \
 game/common/animation.c game/common/pose.c game/common/matrix.c game/common/view.c \
 game/common/math.c game/common/tables.c game/common/sound.c game/common/trace.c \
 game/common/trace_prefix.c game/common/collision.c game/common/flow.c \
 game/common/lara_weapon.c game/common/lara_health.c game/common/effect.c \
 game/common/hit.c game/common/inventory_economy.c game/common/object_common.c \
 game/common/object_block.c game/common/object_trap.c game/common/world_state.c \
 game/common/stop.c game/common/cut.c game/common/frontend.c \
 game/common/lara_inventory.c game/common/save_world.c game/common/menu_text.c \
 game/common/stats_view.c game/common/pickup_events.c game/common/inventory.c \
 game/common/inventory-view.c game/common/save.c
TR64_TR1_PLAY_CODE_UNIT_FRAGMENTS += game/common/units-trace.def
include game/tr1/module.mk
include game/tr2/module.mk
TR64_HOST_SOURCES += plat/host/game-scene.c
endif
endif
