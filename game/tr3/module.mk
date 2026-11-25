# SPDX-License-Identifier: BSD-2-Clause
# Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/game.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
# Tomb Raider III (TR64_GAME=3) unified onto the shared game/common/ Lara/animation/camera core.
TR64_ENGINE_SOURCES += \
 game/common/lara.c game/common/item.c game/common/animation.c game/common/pose.c \
 game/common/camera.c game/common/camera_tick.c game/common/static.c \
 game/common/matrix.c game/common/tables.c game/common/math.c
TR64_ENGINE_SOURCES += \
 game/tr3/floor.c game/tr3/world.c game/tr3/lara.c \
 game/tr3/jungle.c game/tr3/view.c game/tr3/ring.c
TR64_TR3_CODE_UNIT_FRAGMENTS += game/tr3/units.def engine/ui/front.def
TR64_HOST_SOURCES += game/tr3/host-scene.c
