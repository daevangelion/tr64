/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/game.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Ported from src/fixed/game.h, item.h, lara.h, inventory.h and enemy.h
 * at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a.
 * Keeps source constructor order and both deterministic RNG streams. */
#ifndef TR64_GAME_TR2_OBJECTS_H
#define TR64_GAME_TR2_OBJECTS_H

#include "game/common/internal.h"

#if TR64_HAS_SEQUEL
/* tr2-triggers-and-interaction, Reach and commitment; WALL.TR2 body clips. */
enum {
    SWITCH_TO_0 = 63, SWITCH_TO_1 = 64,
    PICKUP_SWIM = 130, KEY_USE = 131, PUZZLE_USE = 134, PICKUP_STAND = 135,
    PICKUP_SWIM_COMMIT = 18, PICKUP_STAND_COMMIT = 42,
    TR2_INTERACT_NONE = 0, TR2_INTERACT_BUMP = 1, TR2_INTERACT_CHOOSE_KEY = 2
};
enum {
    TR2_OPERA_INIT = 0,
    TR2_OPERA_UPDATE = 1,
    TR2_OPERA_COLLIDE = 2
};
typedef struct {
    uint32 op;
    ItemObj *self;
    Lara *lara;
} TR2OperaCall;
bool game_tr2_item_ticks(unsigned type);
uint16 game_tr2_placement_flags(const tr64_core_items_t *, unsigned index);
void game_tr2_load_wall_units(const tr64_level_t *);
void game_tr2_update_item(ItemObj *, Lara *, int32 listenerX, int32 listenerZ);
void game_tr2_update_helicopter(ItemObj *, const Lara *);
void game_tr2_update_campfire(ItemObj *, const Lara *);
uint32 game_tr2_boulder_update(ItemObj *, int32 listener_x, int32 listener_z);
void game_tr2_boulder_contact(ItemObj *, Lara *);
void game_tr2_spike_contact(ItemObj *, Lara *);
void game_tr2_blade_contact(ItemObj *, Lara *);
void game_tr2_blade_update(ItemObj *, Lara *);
uint32 game_tr2_statue_blade_contact(ItemObj *, Lara *);
void game_tr2_statue_blade_update(ItemObj *, Lara *);
void game_tr2_moving_contact(ItemObj *, Lara *);
void game_tr2_nearest_face(ItemObj *, Lara *);
void game_tr2_disc_launch(ItemObj *);
void game_tr2_disc_harm(ItemObj *, Lara *);
uint32 game_tr2_disc_update(ItemObj *);
void game_tr2_disc_removed(ItemObj *);
void game_tr2_disc_spark(struct Room *, vec3i);
void game_tr2_launcher_update(ItemObj *);
void game_tr2_rolling_harm(ItemObj *, Lara *);
void game_tr2_rolling_update(ItemObj *);
void game_tr2_spike_wall_harm(ItemObj *, Lara *);
void game_tr2_spike_wall_update(ItemObj *);
void game_tr2_mount_zipline(ItemObj *, Lara *);
void game_tr2_zipline_update(ItemObj *, Lara *);
void game_tr2_switch_update(ItemObj *);
void game_tr2_interact_pickup(ItemObj *, Lara *);
void game_tr2_interact_switch(ItemObj *, Lara *);
bool game_tr2_extra_switch_interact(ItemObj *, Lara *);
uint32 game_tr2_interact_puzzle(ItemObj *, Lara *);
void game_tr2_finish_puzzle(ItemObj *, Lara *, bool afterRing);
uint32 game_tr2_check_deferred(ItemObj *, Lara *);
void game_tr2_finish_keyhole(ItemObj *, Lara *);
uint32 game_tr2_interact_detonator(ItemObj *, Lara *);
uint32 game_tr2_finish_detonator(ItemObj *, Lara *);
void game_tr2_detonator_update(ItemObj *);
void game_tr2_grenade_update(ItemObj *);
void game_tr2_opera_init(void);
uint32 game_tr2_opera_update(ItemObj *, Lara *);
uint32 game_tr2_opera_collide(ItemObj *, Lara *);
void game_tr2_item_floor_ceiling(const ItemObj *, int32 x, int32 y, int32 z, int32 *floor, int32 *ceiling);
void game_tr2_window_shatter(ItemObj *);
void game_tr2_explosive_death(ItemObj *);
void game_tr2_clear_bodies(Lara *);
typedef enum { TR2_SOLID_MISS, TR2_SOLID_TOUCH, TR2_SOLID_BUMP } TR2SolidContact;
TR2SolidContact game_tr2_solid_contact(ItemObj *, Lara *, bool push, bool doorBump);
TR2RingEntry *game_tr2_ring_find(unsigned type);
unsigned game_tr2_ring_priority(unsigned type);
unsigned game_tr2_ring_remove(unsigned type);
void game_tr2_pickup_credit(const ItemObj *);
void game_tr2_level_script_select(const char *name);
void game_tr2_level_script_apply_stock(void);
const char *game_tr2_level_script_finish_label(void);
uint32_t game_tr2_level_script_title_id(void);
#if defined(TR64_HOST) || !TR64_PLAY || TR64_TEST
void game_tr2_validate_schema22(void);
#endif

static inline bool game_tr2_extra_switch_type(unsigned type)
{
    return type == TR2_ITEM_SMALL_SWITCH || type == TR2_ITEM_PUSH_BUTTON ||
           type == TR2_ITEM_UW_SWITCH;
}
static inline bool game_tr2_wall_creature_type(unsigned type)
{
    return type == TR2_ITEM_TIGER || type == TR2_ITEM_SPIDER ||
           type == TR2_ITEM_CROW || type == TR2_ITEM_REX;
}
static inline bool game_tr2_urban_creature_type(unsigned type)
{
    return type == TR2_ITEM_DOBERMAN ||
           (type >= TR2_ITEM_GUNMAN_1 && type <= TR2_ITEM_GUNMAN_3) ||
           type == TR2_ITEM_OPERA_BOSS ||
           type == TR2_ITEM_RAT || type == TR2_ITEM_THUG;
}
unsigned game_tr2_gunman_anim_base(unsigned type);
void game_tr2_urban_cargo_init(void);
void game_tr2_urban_cargo_release(ItemObj *corpse);
#if defined(TR64_HOST) || !TR64_PLAY || TR64_TEST
const TR2BoatHull *game_tr2_boat_hull(const ItemObj *);
#endif

static inline int32 q14(int64_t x) { return x >> 14; }
void game_tr2_block_occupancy(const ItemObj *, bool occupy);
static inline int32 tr2CollapseHeight(const ItemObj *item)
{
    return item->type == TR2_ITEM_COLLAPSE_HIGH ? 1024 : 512;
}
#endif

#endif
