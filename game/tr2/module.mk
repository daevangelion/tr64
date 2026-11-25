# SPDX-License-Identifier: BSD-2-Clause
# Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/game.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
# Tomb Raider II (TR64_GAME=2) specific objects, hazards, enemies, vehicles and TLB units.
TR64_TR2_SOURCES := \
 game/tr2/lara_weapon.c game/tr2/object_flare.c game/tr2/object_grenade.c \
 game/tr2/unit_tr2_ai.c game/tr2/enemy.c game/tr2/enemy_tiger.c \
 game/tr2/enemy_small_spider.c game/tr2/enemy_crow.c game/tr2/nav.c \
 game/tr2/enemy_motion.c game/tr2/effect.c game/tr2/object_dispatch.c \
 game/tr2/object_factory.c game/tr2/object_interact.c game/tr2/inventory_economy.c \
 game/tr2/object_trap.c game/tr2/hazard_boulder.c game/tr2/unit_tr2_wall.c \
 game/tr2/hazard_contact.c game/tr2/hazard_disc.c game/tr2/hazard_blade.c \
 game/tr2/hazard_wall.c game/tr2/object_zipline.c game/tr2/object_helicopter.c \
 game/tr2/hazard_flame.c game/tr2/unit_tr2_creatures.c game/tr2/enemy_rex.c \
 game/tr2/unit_tr2_boat.c game/tr2/boat.c game/tr2/unit_tr2_urban.c \
 game/tr2/enemy_urban.c game/tr2/unit_tr2_opera.c game/tr2/opera_traps.c \
 game/tr2/validate.c game/tr2/save.c
TR64_TR2_CODE_UNIT_FRAGMENTS += game/tr2/units.def
ifeq ($(TR64_GAME),2)
TR64_CART_SOURCES += $(TR64_TR2_SOURCES)
endif
TR64_HOST_SOURCES += $(TR64_TR2_SOURCES)
