/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/object.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a (reach and alignment primitives). */
#include "objects.h"

TR2RingEntry *game_tr2_ring_find(unsigned type)
{
    for (unsigned i = 0; i < gs->tr2->carriedCount; ++i)
        if (gs->tr2->carried[i].type == type)
            return &gs->tr2->carried[i];
    return NULL;
}

/* Q-INT-02: insertion compares display priorities, not public IDs.
 * Main precedes key; items with equal priority retain insertion order. */
unsigned game_tr2_ring_priority(unsigned type)
{
    if (type == 121)
        return 0;
    if (type >= 157 && type <= 173 && type != 164) {
        static const uint8 priority[] = {
            1, 2, 3, 4, 7, 5, 6, /* guns */
            0, 2, 3, 4, 7, 5, 6, /* ammunition; 164 is not carried */
            11, 10, 8             /* small, large, flares */
        };
        return priority[type-157];
    }
    if (type >= 197 && type <= 200)
        return 101 + type - 197;
    if (type >= 178 && type <= 181)
        return 105 + 181 - type;
    if (type == 207 || type == 208)
        return 110 + 208 - type;
    sys_error("Tomb Raider II: unsupported inventory ring type %u", type);
    return 0;
}

static void ringCredit(unsigned type, unsigned quantity)
{
    TR2RingEntry *entry = game_tr2_ring_find(type);
    if (!entry) {
        GameTR2 *tr2 = gs->tr2;
        if (tr2->carriedCount == TR2_CARRIED_CAPACITY)
            sys_error("Tomb Raider II: inventory ring capacity");
        unsigned priority = game_tr2_ring_priority(type);
        entry = tr2->carried;
        TR2RingEntry *end = entry + tr2->carriedCount;
        while (entry < end && game_tr2_ring_priority(entry->type) <= priority)
            ++entry;
        if (entry < end)
            memmove(entry + 1, entry, (end-entry)*sizeof(*entry));
        ++tr2->carriedCount;
        *entry = (TR2RingEntry){type, 0};
    }
    if (quantity > (unsigned)UINT16_MAX - entry->quantity)
        sys_error("Tomb Raider II: inventory quantity overflow: type=%u", type);
    entry->quantity += quantity;
}

unsigned game_tr2_ring_remove(unsigned type)
{
    TR2RingEntry *entry = game_tr2_ring_find(type);
    if (!entry)
        return 0;
    unsigned quantity = entry->quantity;
    TR2RingEntry *last = gs->tr2->carried + --gs->tr2->carriedCount;
    if (entry < last)
        memmove(entry, entry + 1, (last-entry)*sizeof(*entry));
    return quantity;
}

/* tr2-triggers-and-interaction, Inventory and progress destinations;
 * tr2-great-wall-level, Loadout, dragons and reward (WALL campaign 1). */
/* Declarative per-level campaign script metadata (matching TOMBPC.DAT script
 * entries): fresh-entry stock overrides, three-dragon secret bonus, title ID,
 * and finish boundary label. Gameplay mechanics never branch on scriptIndex. */
enum {
    TR2_SECRET_GRENADE_MEDI = 0,
    TR2_SECRET_AUTO_AMMO_4 = 1,
    TR2_SECRET_SHOTGUN_AMMO_4 = 2,
    TR2_SECRET_UZI_AND_AMMO_4 = 3
};
typedef struct {
    const char *name;
    const char *finishLabel;
    int32_t levelId, nextLevelId;
    uint32_t titleID;
    int16_t ambientTrack;
    int16_t lethalBottomY;
    uint8_t secretsMax;
    uint8_t startFlares;
    uint8_t secretReward;
    uint8_t stripWeapons, stripAmmo;
    bool carryLeftFlareAnim;
} TR2LevelScript;

static const TR2LevelScript tr2LevelScripts[] = {
    { "title",    "Tomb Raider II: title boundary",
      TR2_LVL_TITLE, TR2_LVL_WALL, TR64_STR_TR2_GENERIC, 64, 0, 0, 0, TR2_SECRET_GRENADE_MEDI, 1, 1, false },
    { "assault",  "Tomb Raider II: Lara's Home boundary",
      TR2_LVL_HOME, TR2_LVL_TITLE, TR64_STR_TR2_GENERIC, -1, 0, 0, 0, TR2_SECRET_GRENADE_MEDI, 1, 1, false },
    { "wall",     "Tomb Raider II: The Great Wall finish boundary",
      TR2_LVL_WALL, TR2_LVL_BOAT, TR64_STR_TR2_WALL, 33, 0, 3, 2, TR2_SECRET_GRENADE_MEDI, 0, 0, false },
    { "boat",     "Tomb Raider II: Venice finish boundary",
      TR2_LVL_BOAT, TR2_LVL_VENICE, TR64_STR_TR2_VENICE, -1, 0, 3, 1, TR2_SECRET_AUTO_AMMO_4, 0, 0, true },
    { "venice",   "Tomb Raider II: Bartoli's Hideout finish boundary",
      TR2_LVL_VENICE, TR2_LVL_OPERA, TR64_STR_TR2_GENERIC, -1, 0, 3, 2, TR2_SECRET_SHOTGUN_AMMO_4, 0, 0, false },
    { "opera",    "Tomb Raider II: Opera House finish boundary",
      TR2_LVL_OPERA, TR2_LVL_RIG, TR64_STR_TR2_GENERIC, 31, 0, 3, 2, TR2_SECRET_UZI_AND_AMMO_4, 0, 0, false },
    { "rig",      "Tomb Raider II: Offshore Rig finish boundary",
      TR2_LVL_RIG, TR2_LVL_PLATFORM, TR64_STR_TR2_GENERIC, 58, 0, 3, 2, TR2_SECRET_UZI_AND_AMMO_4, 1, 0, false },
    { "platform", "Tomb Raider II: Diving Area finish boundary",
      TR2_LVL_PLATFORM, TR2_LVL_UNWATER, TR64_STR_TR2_GENERIC, 58, 0, 3, 2, TR2_SECRET_GRENADE_MEDI, 0, 0, false },
    { "unwater",  "Tomb Raider II: 40 Fathoms finish boundary",
      TR2_LVL_UNWATER, TR2_LVL_KEEL, TR64_STR_TR2_GENERIC, 34, 0, 3, 2, TR2_SECRET_GRENADE_MEDI, 0, 0, false },
    { "keel",     "Tomb Raider II: Wreck of the Maria Doria finish boundary",
      TR2_LVL_KEEL, TR2_LVL_LIVING, TR64_STR_TR2_GENERIC, 31, 0, 3, 2, TR2_SECRET_GRENADE_MEDI, 0, 0, false },
    { "living",   "Tomb Raider II: Living Quarters finish boundary",
      TR2_LVL_LIVING, TR2_LVL_DECK, TR64_STR_TR2_GENERIC, 34, 0, 3, 2, TR2_SECRET_GRENADE_MEDI, 0, 0, false },
    { "deck",     "Tomb Raider II: The Deck finish boundary",
      TR2_LVL_DECK, TR2_LVL_SKIDOO, TR64_STR_TR2_GENERIC, 31, 0, 3, 2, TR2_SECRET_GRENADE_MEDI, 0, 0, false },
    { "skidoo",   "Tomb Raider II: Tibetan Foothills finish boundary",
      TR2_LVL_SKIDOO, TR2_LVL_MONASTRY, TR64_STR_TR2_GENERIC, 33, 0, 3, 2, TR2_SECRET_GRENADE_MEDI, 0, 0, false },
    { "monastry", "Tomb Raider II: Barkhang Monastery finish boundary",
      TR2_LVL_MONASTRY, TR2_LVL_CATACOMB, TR64_STR_TR2_GENERIC, -1, 0, 3, 2, TR2_SECRET_GRENADE_MEDI, 0, 0, false },
    { "catacomb", "Tomb Raider II: Catacombs of the Talion finish boundary",
      TR2_LVL_CATACOMB, TR2_LVL_ICECAVE, TR64_STR_TR2_GENERIC, 31, 0, 3, 2, TR2_SECRET_GRENADE_MEDI, 0, 0, false },
    { "icecave",  "Tomb Raider II: Ice Palace finish boundary",
      TR2_LVL_ICECAVE, TR2_LVL_EMPRTOMB, TR64_STR_TR2_GENERIC, 31, 0, 3, 2, TR2_SECRET_GRENADE_MEDI, 0, 0, false },
    { "emprtomb", "Tomb Raider II: Temple of Xian finish boundary",
      TR2_LVL_EMPRTOMB, TR2_LVL_FLOATING, TR64_STR_TR2_GENERIC, 59, 0, 3, 2, TR2_SECRET_GRENADE_MEDI, 0, 0, false },
    { "floating", "Tomb Raider II: Floating Islands finish boundary",
      TR2_LVL_FLOATING, TR2_LVL_XIAN, TR64_STR_TR2_GENERIC, 59, 9728, 3, 2, TR2_SECRET_GRENADE_MEDI, 0, 0, false },
    { "xian",     "Tomb Raider II: The Dragon's Lair finish boundary",
      TR2_LVL_XIAN, TR2_LVL_HOUSE, TR64_STR_TR2_GENERIC, 59, 0, 0, 2, TR2_SECRET_GRENADE_MEDI, 0, 0, false },
    { "house",    "Tomb Raider II: Home Sweet Home finish boundary",
      TR2_LVL_HOUSE, TR2_LVL_TITLE, TR64_STR_TR2_GENERIC, -1, 0, 0, 0, TR2_SECRET_GRENADE_MEDI, 1, 1, false },
    { NULL,       "Tomb Raider II: level finish boundary",
      TR2_LVL_GENERIC, TR2_LVL_TITLE, TR64_STR_TR2_GENERIC, -1, 0, 3, 2, TR2_SECRET_GRENADE_MEDI, 0, 0, false }
};

void game_tr2_level_script_select(const char *name)
{
    uint8_t idx = (uint8_t)(sizeof(tr2LevelScripts) / sizeof(*tr2LevelScripts) - 1u);
    if (name) {
        for (uint8_t i = 0; i < idx; ++i)
            if (strcmp(name, tr2LevelScripts[i].name) == 0) {
                idx = i;
                break;
            }
    }
    gs->tr2->scriptIndex = idx;
}

const char *game_tr2_level_name(int32_t level_id)
{
    unsigned count = (unsigned)(sizeof(tr2LevelScripts) / sizeof(*tr2LevelScripts));
    for (unsigned i = 0; i < count; ++i)
        if (tr2LevelScripts[i].name && tr2LevelScripts[i].levelId == level_id)
            return tr2LevelScripts[i].name;
    return NULL;
}

int32_t game_tr2_level_script_id(void)
{
    return tr2LevelScripts[gs->tr2->scriptIndex].levelId;
}

int32_t game_tr2_level_script_next_id(void)
{
    return tr2LevelScripts[gs->tr2->scriptIndex].nextLevelId;
}

int32_t game_tr2_level_script_ambient_track(int32_t level_id)
{
    unsigned count = (unsigned)(sizeof(tr2LevelScripts) / sizeof(*tr2LevelScripts));
    for (unsigned i = 0; i < count; ++i)
        if (tr2LevelScripts[i].levelId == level_id)
            return tr2LevelScripts[i].ambientTrack;
    return -1;
}

uint32_t game_tr2_level_script_secrets_max(void)
{
    return tr2LevelScripts[gs->tr2->scriptIndex].secretsMax;
}

int32_t game_tr2_level_script_lethal_y(void)
{
    return tr2LevelScripts[gs->tr2->scriptIndex].lethalBottomY;
}

void game_tr2_level_script_apply_stock(void)
{
    const TR2LevelScript *sc = tr2LevelScripts + gs->tr2->scriptIndex;
    TR2RingEntry *flares = game_tr2_ring_find(TR2_RING_FLARES);
    if (flares)
        flares->quantity = sc->startFlares;
    if (sc->stripWeapons) {
        for (unsigned g = 157u; g <= 163u; ++g)
            game_tr2_ring_remove(g);
        gs->tr2->lastWeapon = 0;
        gs->tr2->backWeapon = 0;
        gs->tr2->ammo[0] = 0;
    }
    if (sc->stripAmmo) {
        memset(gs->tr2->ammo, 0, sizeof(gs->tr2->ammo));
        for (unsigned a = 165u; a <= 170u; ++a)
            game_tr2_ring_remove(a);
    }
    if (sc->carryLeftFlareAnim && gs->players[0] &&
        gs->level.models[TR2_MODEL_FLARE_ARMS].animIndex != UINT16_MAX)
        gs->players[0]->extraL->armL.animIndex = gs->level.models[TR2_MODEL_FLARE_ARMS].animIndex;
}

const char *game_tr2_level_script_finish_label(void)
{
    return tr2LevelScripts[gs->tr2->scriptIndex].finishLabel;
}

uint32_t game_tr2_level_script_title_id(void)
{
    return tr2LevelScripts[gs->tr2->scriptIndex].titleID;
}

typedef struct {
    uint32_t magic;
    uint32_t ammo[7];
    TR2RingEntry carried[TR2_CARRIED_CAPACITY];
    GameSettings settings;
    uint8_t carriedCount, lastWeapon, backWeapon, pad;
} TR2CarryPayload;

enum { TR2_CARRY_MAGIC = 0x54523243u };

static void replaceWorldPickups(unsigned fromType, unsigned toType, const ItemObj *skip);

int game_tr2_capture_carry(tr64_game_inventory_carry_t *out)
{
    typedef char TR2CarryFits[sizeof(TR2CarryPayload) <= TR64_GAME_CARRY_GLOBAL_BYTES ? 1 : -1];
    (void)sizeof(TR2CarryFits);
    if (!gs || !gs->tr2 || !out)
        return 0;
    memset(out, 0, sizeof(*out));
    out->from_level_id = gs->gLevelID;
    out->tick = gs->tick;
    out->transition = gs->stop.transition;
    TR2CarryPayload p = {0};
    p.magic = TR2_CARRY_MAGIC;
    memcpy(p.ammo, gs->tr2->ammo, sizeof(p.ammo));
    memcpy(p.carried, gs->tr2->carried, sizeof(p.carried));
    p.settings = gs->gSettings;
    p.carriedCount = gs->tr2->carriedCount;
    p.lastWeapon = gs->tr2->lastWeapon;
    p.backWeapon = gs->tr2->backWeapon;
    memcpy(out->source_globals, &p, sizeof(p));
    return 1;
}

/* tr2-death-save-and-finish.md §3: Inter-level carry, resets, and overrides. */
void game_tr2_apply_carry(const tr64_game_inventory_carry_t *carry)
{
    if (!gs || !gs->tr2 || !carry)
        return;
    TR2CarryPayload p;
    memcpy(&p, carry->source_globals, sizeof(p));
    if (p.magic != TR2_CARRY_MAGIC)
        return;
    gs->gSettings = p.settings;
    if (carry->transition != TR64_GAME_TRANSITION_CAMPAIGN)
        return;

    const TR2LevelScript *sc = tr2LevelScripts + gs->tr2->scriptIndex;
    GameTR2 *tr2 = gs->tr2;
    memset(tr2->carried, 0, sizeof(tr2->carried));
    tr2->carriedCount = 0;
    memset(tr2->ammo, 0, sizeof(tr2->ammo));
    ringCredit(TR2_RING_STOPWATCH, 1);

    /* Gun descriptors: ringGun -> (ringAmmo, ammoIdx, packSize, worldGun, worldAmmo). */
    static const struct {
        uint16_t ringGun, ringAmmo, worldGun, worldAmmo;
        uint8_t ammoIdx, packSize;
    } guns[6] = {
        {158, 165, 136, 143, 3, 12}, /* Shotgun */
        {159, 166, 137, 144, 1, 40}, /* Auto Pistols */
        {160, 167, 138, 145, 2, 80}, /* Uzis */
        {161, 168, 139, 146, 4,  3}, /* Harpoon Gun */
        {162, 169, 140, 147, 6, 40}, /* M16 */
        {163, 170, 141, 148, 5,  2}  /* Grenade Launcher */
    };

    bool owned[6] = {0};
    uint32_t bundles[6] = {0};
    uint32_t flares = 0, smallMedi = 0, largeMedi = 0;
    unsigned count = p.carriedCount <= TR2_CARRIED_CAPACITY ? p.carriedCount : TR2_CARRIED_CAPACITY;
    for (unsigned i = 0; i < count; ++i) {
        unsigned type = p.carried[i].type, qty = p.carried[i].quantity;
        if (!qty) continue;
        if (type == TR2_RING_FLARES) flares += qty;
        else if (type == TR2_RING_SMALL_MEDI) smallMedi += qty;
        else if (type == TR2_RING_LARGE_MEDI) largeMedi += qty;
        for (unsigned g = 0; g < 6; ++g) {
            if (type == guns[g].ringGun) owned[g] = true;
            else if (type == guns[g].ringAmmo) bundles[g] += qty;
        }
    }

    if (!sc->stripAmmo) {
        if (flares) ringCredit(TR2_RING_FLARES, flares);
        if (smallMedi) ringCredit(TR2_RING_SMALL_MEDI, smallMedi);
        if (largeMedi) ringCredit(TR2_RING_LARGE_MEDI, largeMedi);
    }

    if (sc->stripWeapons) {
        tr2->lastWeapon = 0;
        tr2->backWeapon = 0;
        if (gs->players[0] && gs->players[0]->extraL)
            gs->players[0]->extraL->weapon = gs->players[0]->extraL->goalWeapon = WEAPON_NONE;
        if (!sc->stripAmmo) {
            for (unsigned g = 0; g < 6; ++g) {
                uint32_t totalBundles = bundles[g];
                if (owned[g])
                    totalBundles += p.ammo[guns[g].ammoIdx] / guns[g].packSize;
                if (totalBundles)
                    ringCredit(guns[g].ringAmmo, totalBundles);
            }
        }
        return;
    }

    /* Retail PC start-slot rule: Pistols forced owned, reserve 1000, selected = Pistols (1). */
    ringCredit(TR2_RING_PISTOLS, 1);
    tr2->ammo[0] = 1000;
    tr2->lastWeapon = 1;
    tr2->backWeapon = 0;
    if (gs->players[0] && gs->players[0]->extraL)
        gs->players[0]->extraL->weapon = gs->players[0]->extraL->goalWeapon = WEAPON_PISTOLS;

    for (unsigned g = 0; g < 6; ++g) {
        if (owned[g]) {
            ringCredit(guns[g].ringGun, 1);
            if (!sc->stripAmmo)
                tr2->ammo[guns[g].ammoIdx] = p.ammo[guns[g].ammoIdx];
            replaceWorldPickups(guns[g].worldGun, guns[g].worldAmmo, NULL);
        } else if (!sc->stripAmmo && bundles[g]) {
            ringCredit(guns[g].ringAmmo, bundles[g]);
        }
    }
    /* §10 Back-weapon priority: Shotgun (3) > M16 (6) > Grenade (7) > Harpoon (8). */
    if (owned[0]) tr2->backWeapon = 3;
    else if (owned[4]) tr2->backWeapon = 6;
    else if (owned[5]) tr2->backWeapon = 7;
    else if (owned[3]) tr2->backWeapon = 8;
}

static void replaceWorldPickups(unsigned fromType, unsigned toType, const ItemObj *skip)
{
    for (unsigned i = 0; i < gs->level.itemsCount; ++i) {
        ItemObj *it = gs->items + i;
        if (it == skip || it->type != fromType)
            continue;
        if ((it->flags & ITEM_FLAG_STATUS) == ITEM_FLAG_STATUS_INVISIBLE &&
            (gs->unlinkedItems[i >> 5] & (1u << (i & 31))))
            continue;
        it->type = (uint16)toType;
    }
}

void game_tr2_pickup_credit(const ItemObj *item)
{
    switch (item->type) {
    case 137:
        if (game_tr2_ring_find(159)) {
            gs->tr2->ammo[1] += 40;
        } else {
            unsigned carriedBundles = game_tr2_ring_remove(166);
            ringCredit(159, 1);
            gs->tr2->ammo[1] += 40 + 40 * carriedBundles;
            replaceWorldPickups(137, 144, item);
        }
        break;
    case 138:
        if (game_tr2_ring_find(160)) {
            gs->tr2->ammo[2] += 80;
        } else {
            unsigned carriedBundles = game_tr2_ring_remove(167);
            ringCredit(160, 1);
            gs->tr2->ammo[2] += 80 + 80 * carriedBundles;
            replaceWorldPickups(138, 145, item);
        }
        break;
    case 141:
        if (game_tr2_ring_find(163)) {
            gs->tr2->ammo[5] += 2;
        } else {
            unsigned carriedBundles = game_tr2_ring_remove(170);
            ringCredit(163, 1);
            gs->tr2->ammo[5] += 2 + 2 * carriedBundles;
            replaceWorldPickups(141, 148, item);
        }
        break;
    case 143:
        if (game_tr2_ring_find(158)) gs->tr2->ammo[3] += 12;
        else ringCredit(165, 1);
        break;
    case 144:
        if (game_tr2_ring_find(159)) gs->tr2->ammo[1] += 40;
        else ringCredit(166, 1);
        break;
    case 145:
        if (game_tr2_ring_find(160)) gs->tr2->ammo[2] += 80;
        else ringCredit(167, 1);
        break;
    case 148:
        if (game_tr2_ring_find(163)) gs->tr2->ammo[5] += 2;
        else ringCredit(170, 1);
        break;
    case 149: case 150: ringCredit(item->type + 22, 1); break;
    case 151: ringCredit(TR2_RING_FLARES, 6); break;
    case 174: case 175: case 176: case 177: ringCredit(item->type + 4, 1); break;
    case 193: case 194: ringCredit(item->type + 4, 1); break;
    case 190: case 191: case 192: {
        unsigned before = gs->gSaveGame.secrets, after = before | (1u << (item->type - 190));
        game_sndPlayTrack(47);
        if (before != 7 && after == 7) {
            unsigned reward = tr2LevelScripts[gs->tr2->scriptIndex].secretReward;
            if (reward == TR2_SECRET_AUTO_AMMO_4) {
                if (game_tr2_ring_find(159)) gs->tr2->ammo[1] += 160;
                else ringCredit(166, 4);
            } else if (reward == TR2_SECRET_SHOTGUN_AMMO_4) {
                if (game_tr2_ring_find(158)) gs->tr2->ammo[3] += 48;
                else ringCredit(165, 4);
            } else if (reward == TR2_SECRET_UZI_AND_AMMO_4) {
                if (game_tr2_ring_find(160)) {
                    gs->tr2->ammo[2] += 320;
                } else {
                    unsigned carriedBundles = game_tr2_ring_remove(167);
                    ringCredit(160, 1);
                    gs->tr2->ammo[2] += 400 + 80 * carriedBundles;
                    replaceWorldPickups(138, 145, NULL);
                }
            } else {
                if (game_tr2_ring_find(163)) {
                    gs->tr2->ammo[5] += 4;
                } else {
                    unsigned carriedBundles = game_tr2_ring_remove(170);
                    ringCredit(163, 1);
                    gs->tr2->ammo[5] += 6 + 2*carriedBundles;
                }
                ringCredit(171, 1);
            }
        }
        gs->gSaveGame.secrets = after;
        break;
    }
    }
}

unsigned game_tr2_ring_quantity(unsigned type)
{
    const TR2RingEntry *entry=game_tr2_ring_find(type);
    return entry ? entry->quantity : 0;
}

bool game_tr2_ring_has_keys(void)
{
    const GameTR2 *tr2=gs->tr2;
    for (unsigned i=tr2->carriedCount;i>0;--i) {
        const TR2RingEntry *e=tr2->carried+i-1;
        if (game_tr2_ring_priority(e->type)<100) break;
        if (e->quantity) return true;
    }
    return false;
}

void game_tr2_ring_debit(TR2RingEntry *entry)
{
    if (entry->quantity>1)
        --entry->quantity;
    else
        game_tr2_ring_remove(entry->type);
}

void game_tr2_ring_commit(unsigned type,uint32_t pause_ticks)
{
    if (!gs || !TR64_RULE(FRESH_STOCK) || gs->inventory.state!=INV_STATE_NONE)
        sys_error("Tomb Raider II: invalid inventory choice");
    gs->gSaveGame.time+=pause_ticks; /* tr2-oracle-trace, native ring pauses. */
    if (!type) return;

    GameTR2 *tr2=gs->tr2;
    unsigned ordinal=0,priority=game_tr2_ring_priority(type);
    const bool keys=priority>=100;
    TR2RingEntry *entry=NULL;
    for (unsigned i=0;i<tr2->carriedCount;++i) {
        TR2RingEntry *e=tr2->carried+i;
        if (!e->quantity || (game_tr2_ring_priority(e->type)>=100)!=keys) continue;
        if (e->type==type) { entry=e; break; }
        ++ordinal;
    }
    if (!entry) sys_error("Tomb Raider II: selected ring item %u is not carried",type);
    if (keys) {
        if ((type < 197 || type > 200) && (type < 178 || type > 181))
            sys_error("Tomb Raider II: carried item %u has no admitted use", type);
        tr2->keySelection=ordinal;
        tr2->selectedKey=type;
        return;
    }
    tr2->mainSelection=ordinal;
    switch (type) {
    case 157: case 158: case 159: case 160: case 163: {
        unsigned weapon = type == 157 ? WEAPON_PISTOLS :
                          type == 158 ? WEAPON_SHOTGUN :
                          type == 159 ? WEAPON_MAGNUMS :
                          type == 160 ? WEAPON_UZIS : TR2_WEAPON_GRENADE;
        if (!game_tr2_request_ring_weapon(weapon))
            sys_error("Tomb Raider II: ineligible weapon %u selection", type);
        break;
    }
    case TR2_RING_FLARES:
        /* tr2-first-levels §1.3: a ring flare is the same request as the
         * dedicated control, not a stock debit or a second flare tick. */
        tr2->ringFlare=true;
        break;
    case TR2_RING_SMALL_MEDI: case TR2_RING_LARGE_MEDI:
        (void)Lara_useItem(gs->players[0], (int32)type);
        break;
    default:
        sys_error("Tomb Raider II: carried item %u has no admitted use", type);
    }
}

void game_tr2_bind_key_ring(void (*choose)(void *),void *context)
{
    if (!gs || !TR64_RULE(INTERACTIONS))
        sys_error("Tomb Raider II: key ring binding before level");
    gs->tr2->chooseKey=choose;
    gs->tr2->chooseKeyContext=context;
}

void game_tr2_route_update(uint32_t input,unsigned command,uint32_t pause_ticks)
{
    if (!command) {
        if (pause_ticks) sys_error("Tomb Raider II: pause without a ring event");
        game_tr2_tick(input);
        return;
    }
    const unsigned type=command&511u;
    GameTR2 *tr2=gs->tr2;
    if (!(command&TR2_RING_EVENT) || command>=2*TR2_RING_EVENT_KEY)
        sys_error("Tomb Raider II: invalid ring event");
    if (command&TR2_RING_EVENT_KEY) {
        if (!(input&IN_ACTION) || tr2->keyReplayChoice || tr2->selectedKey ||
            (type && (type<197 || type>200) && (type<178 || type>181)))
            sys_error("Tomb Raider II: invalid key confirmation");
        tr2->keyReplayChoice=type+1;
        tr2->keyPauseTicks=pause_ticks;
    } else {
        game_tr2_ring_commit(type,pause_ticks);
    }
    game_tr2_tick(input);
    if (tr2->keyReplayChoice)
        sys_error("Tomb Raider II: key confirmation found no usable keyhole");
}
