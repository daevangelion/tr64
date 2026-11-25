# SPDX-License-Identifier: BSD-2-Clause
# Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/game.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
# Tomb Raider I (TR64_GAME=1) specific objects, hazards, enemies and TLB units.
TR64_TR1_SOURCES := \
 game/tr1/object_interact.c game/tr1/object_dispatch.c game/tr1/object_factory.c \
 game/tr1/object_progression.c game/tr1/hazard_dispatch.c game/tr1/hazard_boulder.c \
 game/tr1/hazard_contact.c game/tr1/hazard_thor_lightning.c game/tr1/hazard_flame.c \
 game/tr1/enemy.c game/tr1/nav.c game/tr1/encounter.c game/tr1/enemy_board.c \
 game/tr1/enemy_wolf.c game/tr1/enemy_bear.c game/tr1/enemy_bat.c \
 game/tr1/enemy_crocodile.c game/tr1/enemy_lion.c game/tr1/enemy_gorilla.c \
 game/tr1/enemy_rat.c game/tr1/enemy_rex.c game/tr1/enemy_raptor.c \
 game/tr1/enemy_mutant.c game/tr1/enemy_centaur.c game/tr1/enemy_mummy.c \
 game/tr1/enemy_larson.c game/tr1/enemy_pierre.c game/tr1/enemy_skater.c \
 game/tr1/enemy_cowboy.c game/tr1/enemy_mrt.c game/tr1/enemy_natla.c \
 game/tr1/enemy_adam.c game/tr1/enemy_doppelganger.c game/tr1/enemy_encounter.c \
 game/tr1/unit_peru.c game/tr1/unit_greece.c game/tr1/unit_dinosaurs.c \
 game/tr1/unit_atlanteans.c game/tr1/unit_gunmen.c game/tr1/unit_henchmen.c
TR64_ENGINE_SOURCES += $(TR64_TR1_SOURCES)
TR64_TR1_CODE_UNIT_FRAGMENTS += game/tr1/units.def
