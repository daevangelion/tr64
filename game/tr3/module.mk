# SPDX-License-Identifier: BSD-2-Clause
# Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/game.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
# Tomb Raider III (TR64_GAME=3) unified onto the shared TR1-TR3 Lara/animation/camera core.
ifeq ($(TR64_LEVEL_LOADER),1)
TR64_ENGINE_SOURCES += game/lara.c game/item.c game/animation.c game/pose.c game/camera.c game/camera_tick.c game/static.c game/matrix.c game/tables.c game/math.c
TR64_ENGINE_SOURCES += game/tr3/floor.c game/tr3/world.c game/tr3/lara.c game/tr3/jungle.c game/tr3/view.c game/tr3/ring.c
TR64_TR3_CODE_UNIT_FRAGMENTS += game/units-tr3.def engine/ui/front.def
TR64_HOST_SOURCES += game/tr3/host-scene.c
endif
