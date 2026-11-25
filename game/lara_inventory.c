/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/inventory_economy.h, inventory.h and draw.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "internal.h"

/* inventory_economy.h:205-241. Suffixes are source font glyph IDs, not ASCII.
 * Counts are raw inventory units: shotgun ammo is not divided by six here.
 * The caller decides which slot is current and whether its ring is visible. */
bool inventory_countLabel(int32 slot, int16 *value, uint8 *suffix)
{
    ASSERT(value && suffix);
    *value = 0;
    *suffix = 0;
    if (!gs)
        return false;
#if TR64_HAS_TR2
    if(TR64_RULE(FRESH_STOCK)) {
        if(slot==157) return false; /* Unlimited pistols. */
        uint32 n = slot == 158 ? gs->tr2->ammo[3] :
                   slot == 159 ? gs->tr2->ammo[1] :
                   slot == 160 ? gs->tr2->ammo[2] :
                   slot == 163 ? gs->tr2->ammo[5] :
                   game_tr2_ring_quantity((unsigned)slot);
        /* Great Wall starting stock/shotgun: reserve is in pellet units;
         * retain the cart HUD's raw-unit convention for ammo labels. */
        *value=(int16)(n>INT16_MAX ? INT16_MAX : n);
        if(slot==158) *suffix=12;
        else if(slot==159) *suffix=13;
        else if(slot==160) *suffix=14;
        return slot==158 || slot==159 || slot==160 || slot==163 || n>1;
    }
#endif
#line 15
    ASSERT(slot >= 0 && slot < SLOT_MAX);
    int32 ammoSlot;
    switch (slot)
    {
    case SLOT_PISTOLS:
        return false;
    case SLOT_SHOTGUN:
        ammoSlot = SLOT_AMMO_SHOTGUN;
        *suffix = 12;
        break;
    case SLOT_MAGNUMS:
        ammoSlot = SLOT_AMMO_MAGNUMS;
        *suffix = 13;
        break;
    case SLOT_UZIS:
        ammoSlot = SLOT_AMMO_UZIS;
        *suffix = 14;
        break;
    default:
        *value = (int16)gs->inventory.counts[slot];
        return *value != -1 && *value > 1;
    }
    *value = (int16)gs->inventory.counts[ammoSlot];
    if (*value == -1)
        return false;
    return *value > 1 || *suffix != 0;
}

/* draw.h:1098-1124 (the source's N64 HUD policy). Rendering/formatting and
 * deciding where the label is drawn belong to engine UI, not the game. */
bool Lara_ammoLabel(const Lara *self, int16 *value, uint8 *suffix)
{
    ASSERT(value && suffix);
    *value = 0;
    *suffix = 0;
    if (!self || !self->extraL || self->extraL->weaponState != WEAPON_STATE_READY)
        return false;
#if TR64_HAS_TR2
    if(TR64_RULE(FRESH_STOCK)) {
        int32 slot = self->extraL->weapon == WEAPON_SHOTGUN ? 158 :
                     self->extraL->weapon == WEAPON_MAGNUMS ? 159 :
                     self->extraL->weapon == WEAPON_UZIS ? 160 : self->extraL->weapon == TR2_WEAPON_GRENADE ? 163 : 0;
        return slot != 0 && inventory_countLabel(slot, value, suffix);
    }
#endif
#line 52
    int32 slot;
    switch (self->extraL->weapon)
    {
    case WEAPON_PISTOLS: slot = SLOT_PISTOLS; break;
    case WEAPON_SHOTGUN: slot = SLOT_SHOTGUN; break;
    case WEAPON_MAGNUMS: slot = SLOT_MAGNUMS; break;
    case WEAPON_UZIS: slot = SLOT_UZIS; break;
    default: return false;
    }
    return inventory_countLabel(slot, value, suffix);
}

/* inventory.h:1877-1882 shows current health for a focused medikit in READY
 * or SHOW. It does not preview the health that using the medikit would grant. */
bool inventory_medikitHealth(const Lara *self, int32 slot, bool current, int32 *healthQ8)
{
    ASSERT(healthQ8);
    *healthQ8 = 0;
    if (!gs || !self || !current ||
        (gs->inventory.state != INV_STATE_READY && gs->inventory.state != INV_STATE_SHOW) ||
        (TR64_RULE(FRESH_STOCK) ? (slot != 171 && slot != 172) :
         (slot != SLOT_MEDIKIT_SMALL && slot != SLOT_MEDIKIT_BIG)))
        return false;
    /* health is int16, so this product fits int32 even for a dead actor. */
    *healthQ8 = (self->health * 256) / LARA_MAX_HEALTH;
    return true;
}
