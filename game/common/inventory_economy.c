/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/inventory.h and inventory_economy.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"

typedef struct { int32 weaponSlot, ammoSlot, ammoPerPickup, ammoPerShot; } WeaponInventoryRule;
static bool weaponRule(Weapon weapon, WeaponInventoryRule *rule)
{
    switch (weapon) {
    case WEAPON_PISTOLS: *rule = (WeaponInventoryRule){SLOT_PISTOLS,SLOT_AMMO_PISTOLS,-1,1}; return true;
    case WEAPON_SHOTGUN: *rule = (WeaponInventoryRule){SLOT_SHOTGUN,SLOT_AMMO_SHOTGUN,12,6}; return true;
    case WEAPON_MAGNUMS: *rule = (WeaponInventoryRule){SLOT_MAGNUMS,SLOT_AMMO_MAGNUMS,50,1}; return true;
    case WEAPON_UZIS: *rule = (WeaponInventoryRule){SLOT_UZIS,SLOT_AMMO_UZIS,100,1}; return true;
    default: return false;
    }
}
static bool binarySlot(int32 slot)
{
    return slot == SLOT_COMPASS || slot == SLOT_PISTOLS || slot == SLOT_SHOTGUN ||
        slot == SLOT_MAGNUMS || slot == SLOT_UZIS || slot == SLOT_PASSPORT ||
        slot == SLOT_DETAIL || slot == SLOT_SOUND || slot == SLOT_CONTROLS || slot == SLOT_HOME;
}
static bool validCount(int32 slot, int32 count)
{
    return slot >= 0 && slot < SLOT_MAX &&
        ((count == -1 && slot == SLOT_AMMO_PISTOLS) ||
         (count >= 0 && count <= 32767 && (!binarySlot(slot) || count <= 1)));
}
bool inventory_setCount(int32 slot, int32 count)
{
    if (slot < 0 || slot >= SLOT_MAX || (count == -1 ? slot != SLOT_AMMO_PISTOLS : count < 0 || count > 32767) ||
        !validCount(slot, gs->inventory.counts[slot])) return false;
    int32 value = binarySlot(slot) && count > 0 ? 1 : count;
    if (slot < SLOT_COMPASS) gs->inventory.numKeys += value - gs->inventory.counts[slot];
    gs->inventory.counts[slot] = value;
    return true;
}
bool inventory_addCount(int32 slot, int32 count)
{
    if (slot < 0 || slot >= SLOT_MAX || count <= 0 || !validCount(slot, gs->inventory.counts[slot])) return false;
    int32 current = gs->inventory.counts[slot];
    if (current == -1) return slot == SLOT_AMMO_PISTOLS;
    if (count > 32767 - current) return false;
    return inventory_setCount(slot, current + count);
}
bool inventory_consumeCount(int32 slot, int32 count)
{
    if (slot < 0 || slot >= SLOT_MAX || count <= 0 || !validCount(slot, gs->inventory.counts[slot])) return false;
    int32 current = gs->inventory.counts[slot];
    if (current == -1) return slot == SLOT_AMMO_PISTOLS;
    if (current < count) return false;
    gs->inventory.counts[slot] = current - count;
    if (slot < SLOT_COMPASS) gs->inventory.numKeys -= count;
    return true;
}
bool inventory_hasAmmo(Weapon weapon)
{
    WeaponInventoryRule r;
    if (!weaponRule(weapon, &r)) return false;
    int32 count = gs->inventory.counts[r.ammoSlot];
    return count == -1 || count >= r.ammoPerShot;
}
bool inventory_canSelectWeapon(Weapon weapon)
{
    WeaponInventoryRule r;
    return weaponRule(weapon, &r) && gs->inventory.counts[r.weaponSlot] == 1 && inventory_hasAmmo(weapon);
}
bool inventory_consumeAmmo(Weapon weapon, int32 *units)
{
    WeaponInventoryRule r;
    *units = 0;
    if (!weaponRule(weapon, &r) || gs->inventory.counts[r.weaponSlot] != 1 ||
        !inventory_consumeCount(r.ammoSlot, r.ammoPerShot)) return false;
    *units = r.ammoPerShot;
    return true;
}
static bool pickupWeapon(ItemType type, Weapon *weapon, bool *ownership)
{
    switch (type) {
    case ITEM_PISTOLS: case ITEM_INV_PISTOLS: *weapon=WEAPON_PISTOLS; *ownership=true; return true;
    case ITEM_AMMO_PISTOLS: case ITEM_INV_AMMO_PISTOLS: *weapon=WEAPON_PISTOLS; *ownership=false; return true;
    case ITEM_SHOTGUN: case ITEM_INV_SHOTGUN: *weapon=WEAPON_SHOTGUN; *ownership=true; return true;
    case ITEM_AMMO_SHOTGUN: case ITEM_INV_AMMO_SHOTGUN: *weapon=WEAPON_SHOTGUN; *ownership=false; return true;
    case ITEM_MAGNUMS: case ITEM_INV_MAGNUMS: *weapon=WEAPON_MAGNUMS; *ownership=true; return true;
    case ITEM_AMMO_MAGNUMS: case ITEM_INV_AMMO_MAGNUMS: *weapon=WEAPON_MAGNUMS; *ownership=false; return true;
    case ITEM_UZIS: case ITEM_INV_UZIS: *weapon=WEAPON_UZIS; *ownership=true; return true;
    case ITEM_AMMO_UZIS: case ITEM_INV_AMMO_UZIS: *weapon=WEAPON_UZIS; *ownership=false; return true;
    default: return false;
    }
}
bool inventory_addPickup(ItemType type)
{
    Weapon weapon;
    bool ownership;
    if (pickupWeapon(type, &weapon, &ownership)) {
        WeaponInventoryRule r;
        if (!weaponRule(weapon,&r) || !validCount(r.weaponSlot,gs->inventory.counts[r.weaponSlot]) ||
            !validCount(r.ammoSlot,gs->inventory.counts[r.ammoSlot])) return false;
        if (weapon == WEAPON_PISTOLS) {
            gs->inventory.counts[r.weaponSlot]=1;
            gs->inventory.counts[r.ammoSlot]=-1;
            return true;
        }
        int32 current=gs->inventory.counts[r.ammoSlot];
        if (r.ammoPerPickup <= 0 || r.ammoPerPickup > 32767-current) return false;
        gs->inventory.counts[r.weaponSlot]=ownership ? 1 : gs->inventory.counts[r.weaponSlot];
        gs->inventory.counts[r.ammoSlot]=current+r.ammoPerPickup;
        return true;
    }
    int32 slot;
    switch(type) {
    case ITEM_MEDIKIT_SMALL: case ITEM_INV_MEDIKIT_SMALL: slot=SLOT_MEDIKIT_SMALL; break;
    case ITEM_MEDIKIT_BIG: case ITEM_INV_MEDIKIT_BIG: slot=SLOT_MEDIKIT_BIG; break;
    case ITEM_PUZZLE_1: case ITEM_INV_PUZZLE_1: slot=SLOT_PUZZLE_1; break;
    case ITEM_PUZZLE_2: case ITEM_INV_PUZZLE_2: slot=SLOT_PUZZLE_2; break;
    case ITEM_PUZZLE_3: case ITEM_INV_PUZZLE_3: slot=SLOT_PUZZLE_3; break;
    case ITEM_PUZZLE_4: case ITEM_INV_PUZZLE_4: slot=SLOT_PUZZLE_4; break;
    case ITEM_KEY_ITEM_1: case ITEM_INV_KEY_ITEM_1: slot=SLOT_KEY_ITEM_1; break;
    case ITEM_KEY_ITEM_2: case ITEM_INV_KEY_ITEM_2: slot=SLOT_KEY_ITEM_2; break;
    case ITEM_KEY_ITEM_3: case ITEM_INV_KEY_ITEM_3: slot=SLOT_KEY_ITEM_3; break;
    case ITEM_KEY_ITEM_4: case ITEM_INV_KEY_ITEM_4: slot=SLOT_KEY_ITEM_4; break;
    case ITEM_LEADBAR: case ITEM_INV_LEADBAR: slot=SLOT_LEADBAR; break;
    case ITEM_SCION_PICKUP_QUALOPEC: case ITEM_SCION_PICKUP_DROP: case ITEM_SCION_PICKUP_HOLDER: case ITEM_INV_SCION: slot=SLOT_SCION; break;
    case ITEM_INV_COMPASS: slot=SLOT_COMPASS; break;
    case ITEM_INV_PASSPORT: slot=SLOT_PASSPORT; break;
    case ITEM_INV_DETAIL: slot=SLOT_DETAIL; break;
    case ITEM_INV_SOUND: slot=SLOT_SOUND; break;
    case ITEM_INV_CONTROLS: slot=SLOT_CONTROLS; break;
    case ITEM_INV_HOME: slot=SLOT_HOME; break;
    default: return false;
    }
    return inventory_addCount(slot,1);
}
bool inventory_applyItem(ItemObj *hole)
{
    int32 slot;
    if (hole->type >= ITEM_PUZZLEHOLE_1 && hole->type <= ITEM_PUZZLEHOLE_4)
        slot = SLOT_PUZZLE_1 - (hole->type-ITEM_PUZZLEHOLE_1);
    else if (hole->type >= ITEM_KEYHOLE_1 && hole->type <= ITEM_KEYHOLE_4)
        slot = SLOT_KEY_ITEM_1 + (hole->type-ITEM_KEYHOLE_1);
    else return false;
    if (gs->inventory.useSlot != slot || !inventory_consumeCount(slot,1)) return false;
    gs->inventory.useSlot=SLOT_MAX;
    return true;
}
static bool inventory_admittedLevel(int32 level)
{
    switch (level) {
    case LVL_TR1_TITLE: case LVL_TR1_GYM:
    case LVL_TR1_1: case LVL_TR1_2: case LVL_TR1_3A: case LVL_TR1_3B:
    case LVL_TR1_4: case LVL_TR1_5: case LVL_TR1_6: case LVL_TR1_7A:
    case LVL_TR1_7B: case LVL_TR1_8A: case LVL_TR1_8B: case LVL_TR1_8C:
    case LVL_TR1_10A: case LVL_TR1_10B: case LVL_TR1_10C:
        return true;
    default:
        return false;
    }
}
bool inventory_export_counts(int32 *out)
{
    if (!out) return false;
    for (int32 slot=0; slot<SLOT_MAX; slot++)
        if (!validCount(slot,gs->inventory.counts[slot])) return false;
    for (int32 slot=0; slot<SLOT_MAX; slot++)
        out[slot]=gs->inventory.counts[slot];
    return true;
}
bool inventory_import_counts(const int32 *counts)
{
    if(!counts)return false;
    for (int32 slot=0; slot<SLOT_MAX; slot++)
        if (!validCount(slot,counts[slot])) return false;

    gs->inventory.numKeys=0;
    for (int32 slot=0; slot<SLOT_MAX; slot++) {
        int32 value=counts[slot];
        gs->inventory.counts[slot]=0;
        if (!inventory_setCount(slot,value)) return false;
    }
    return true;
}
bool inventory_apply_level_transition(int32 from,int32 to,const int32 *counts)
{
    /* inventory.h:237-242 returns without changing anything when either level
     * is not admitted, which is the case for the four cutscene levels; the
     * counts are the ones the previous level exported. */
    if(game_is_cutscene_level(from) || game_is_cutscene_level(to))
        return (game_is_cutscene_level(from) || inventory_admittedLevel(from)) &&
               (game_is_cutscene_level(to) || inventory_admittedLevel(to)) &&
               inventory_import_counts(counts);
    if(!inventory_admittedLevel(from) || !inventory_admittedLevel(to) ||
       !inventory_import_counts(counts))return false;
    /* inventory.h:237-256 changes these metadata slots only; quest clearing
     * is a distinct source operation and must not be folded into carryover. */
    if (!inventory_setCount(SLOT_PASSPORT,1) ||
        !inventory_setCount(SLOT_DETAIL,1) ||
        !inventory_setCount(SLOT_SOUND,1) ||
        !inventory_setCount(SLOT_CONTROLS,1) ||
        !inventory_setCount(SLOT_HOME,to == LVL_TR1_TITLE ? 1 : 0) ||
        !inventory_setCount(SLOT_COMPASS,to == LVL_TR1_TITLE ? 0 : 1))
        return false;
    if (to == LVL_TR1_10A)
        for (int32 slot=SLOT_PISTOLS; slot<=SLOT_AMMO_UZIS; slot++)
            if (!inventory_setCount(slot,0)) return false;
    return true;
}
