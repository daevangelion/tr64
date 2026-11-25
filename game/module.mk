# SPDX-License-Identifier: BSD-2-Clause
# Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/game.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
# Simulation port: src/fixed/{common.cpp,common.h,item.h,room.h,lara.h,camera.h,game.h}
# at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a; native build registration is new.
ifeq ($(TR64_LEVEL_LOADER),1)
TR64_ENGINE_SOURCES += game/game.c game/item.c game/lara.c game/lara_tr3.c game/dispatch.c \
 game/room.c game/static.c game/world.c game/trigger.c game/camera.c \
 game/camera_tick.c game/animation.c game/pose.c game/matrix.c game/view.c game/math.c \
 game/tables.c game/sound.c game/trace.c game/trace_prefix.c game/collision.c game/flow.c
TR64_ENGINE_SOURCES += game/lara_weapon.c game/lara_health.c game/effect.c game/hit.c \
 game/inventory_economy.c game/object_common.c game/object_interact.c \
 game/object_block.c game/object_trap.c game/object_dispatch.c game/object_factory.c \
 game/object_progression.c game/hazard_dispatch.c game/hazard_boulder.c \
 game/hazard_contact.c game/hazard_thor_lightning.c game/hazard_flame.c game/world_state.c \
 game/enemy.c game/nav.c game/encounter.c game/enemy_board.c \
 game/enemy_wolf.c game/enemy_bear.c game/enemy_bat.c game/enemy_crocodile.c \
 game/enemy_lion.c game/enemy_gorilla.c game/enemy_rat.c game/enemy_rex.c \
 game/enemy_raptor.c game/enemy_mutant.c game/enemy_centaur.c game/enemy_mummy.c \
 game/enemy_larson.c game/enemy_pierre.c game/enemy_skater.c game/enemy_cowboy.c \
 game/enemy_mrt.c game/enemy_natla.c game/enemy_adam.c game/enemy_doppelganger.c \
 game/enemy_encounter.c
TR64_ENGINE_SOURCES += game/unit_peru.c game/unit_greece.c game/unit_dinosaurs.c \
 game/unit_atlanteans.c game/unit_gunmen.c game/unit_henchmen.c
TR64_ENGINE_SOURCES += game/stop.c game/cut.c game/frontend.c game/lara_inventory.c game/save_world.c
TR64_ENGINE_SOURCES += game/menu_text.c game/stats_view.c game/pickup_events.c
TR64_ENGINE_SOURCES += game/inventory.c game/inventory-view.c game/save.c
TR64_TR1_CODE_UNIT_FRAGMENTS += game/units.def
TR64_TR1_PLAY_CODE_UNIT_FRAGMENTS += game/units-trace.def
TR64_TR2_CODE_UNIT_FRAGMENTS += game/units-tr2.def
TR64_TR2_SOURCES := game/tr2/lara_weapon.c game/tr2/object_flare.c game/tr2/object_grenade.c \
 game/unit_tr2_ai.c game/tr2/enemy.c game/tr2/enemy_tiger.c game/tr2/enemy_small_spider.c game/tr2/enemy_crow.c \
 game/tr2/nav.c game/tr2/enemy_motion.c game/tr2/effect.c \
 game/tr2/object_dispatch.c game/tr2/object_factory.c game/tr2/object_interact.c \
 game/tr2/inventory_economy.c game/tr2/object_trap.c game/tr2/hazard_boulder.c game/unit_tr2_wall.c \
 game/tr2/hazard_contact.c game/tr2/hazard_disc.c game/tr2/hazard_blade.c game/tr2/hazard_wall.c game/tr2/object_zipline.c \
 game/tr2/object_helicopter.c game/tr2/hazard_flame.c game/unit_tr2_creatures.c game/tr2/enemy_rex.c \
 game/unit_tr2_boat.c game/tr2/boat.c game/unit_tr2_urban.c game/tr2/enemy_urban.c \
 game/unit_tr2_opera.c game/tr2/opera_traps.c \
 game/tr2/validate.c game/tr2/save.c
ifeq ($(TR64_GAME),2)
TR64_CART_SOURCES += $(TR64_TR2_SOURCES)
endif
TR64_HOST_SOURCES += plat/host/game-scene.c $(TR64_TR2_SOURCES)
endif
