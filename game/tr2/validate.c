/* SPDX-License-Identifier: BSD-2-Clause */
/* Derived from OpenLara (c) 2018 Timur "XProger" Gagiev; see game/LICENSE-OPENLARA; src/fixed/item.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a. */
#include "flare.h"
#include "objects.h"
#include "game/common/object.h"
#include "tr64/mem.h"

static void validatePackedPose(PackedFrame key, unsigned clip, unsigned frame, unsigned joints)
{
    const tr64_u16_be_t *word = key.words + 9, *end = key.words + key.extent;
    for (unsigned joint = 0; joint < joints; ++joint) {
        word = game_tr2_step_packed(word, end, false);
        if (!word)
            sys_error("Tomb Raider II: packed rotations truncated: animation=%u frame=%u joint=%u",
                      clip, frame, joint);
    }
}

static void validateLaraPose(PackedFrame key, unsigned clip, unsigned frame)
{
    validatePackedPose(key, clip, frame, JOINT_MAX);
}

static void validateSphereKey(unsigned type, unsigned clip, unsigned frame, PackedFrame key)
{
    unsigned joint;
    const char *why = game_tr2_walk_packed_spheres(type, key, NULL, NULL, NULL, false, &joint);
    if (why) {
        if (type == ITEM_LARA)
            sys_error("Tomb Raider II: Lara %s: animation=%u frame=%u joint=%u",
                      why, clip, frame, joint);
        const char *name = tr2DoorType(type) ? "door" : game_tr2_inert_interaction(type);
        sys_error("Tomb Raider II: %s %s: type=%u animation=%u frame=%u joint=%u",
                  name, why, type, clip, frame, joint);
    }
}

/* A water search follows a vertical link without testing the actor's
 * Y first. Validate every whole source tile against the destination's
 * direct grid before any water search. The initial horizontal pass
 * can select a clamped border for coordinates outside its room; that
 * border must not have a vertical link unless it is direct-only. */
static void validateWaterLinks(void)
{
    for (unsigned i = 0; i < gs->level.roomsCount; ++i) {
        const Room *r = gs->rooms + i;
        const int32 ox = tr64_i32(r->info->x), oz = tr64_i32(r->info->z);
        const unsigned nx = tr64_u16(r->info->x_sectors), nz = tr64_u16(r->info->z_sectors);
        const Sector *s = r->sectors;
        for (unsigned x = 0; x < nx; ++x) {
            const int32 px = i32_add(ox, x * 1024);
            for (unsigned z = 0; z < nz; ++z, ++s) {
                if (s->roomAbove == NO_ROOM && s->roomBelow == NO_ROOM)
                    continue;
                const bool outerZ = z == 0 || z == nz - 1;
                const bool directOnly = outerZ && nx >= 3 && (x == 0 || x == nx - 1);
                const bool clamped = (outerZ || x == 0 || x == nx - 1) && !directOnly;
                const int32 pz = i32_add(oz, z * 1024);
                const uint8 links[] = {s->roomAbove, s->roomBelow};
                for (unsigned dir = 0; dir < 2; ++dir) {
                    unsigned next = links[dir];
                    if (next == NO_ROOM)
                        continue;
                    const RoomInfo *target = gs->rooms[next].info;
                    int32 dx = i32_sub(px, tr64_i32(target->x));
                    int32 dz = i32_sub(pz, tr64_i32(target->z));
                    if (clamped || dx < 0 || dz < 0 ||
                        dx > (tr64_u16(target->x_sectors) - 1) * 1024 ||
                        dz > (tr64_u16(target->z_sectors) - 1) * 1024)
                        sys_error("Tomb Raider II: water vertical link outside target grid: "
                                  "room=%u sector=%u,%u %s=%u", i, x, z, dir ? "below" : "above", next);
                }
            }
        }
    }
}

/* Level admission already bounded the clip, state, range and command
 * references. This worklist visits each reachable clip once, including
 * nonsequential links. A two-level index bounds lookup even on a sparse
 * 16-bit clip domain; the two bitsets together fit the I/O arena. */
typedef struct {
    uint32 *seen, *pending;
    uint32 groups[64];   /* One bit per nonempty pending word. */
    uint64_t top;        /* One bit per nonempty group. */
} TR2WorldAnimCheck;

static void tr2QueueWorldAnim(TR2WorldAnimCheck *q, unsigned clip)
{
    unsigned word = clip >> 5, group = word >> 5;
    uint32 bit = 1u << (clip & 31);
    if (q->seen[word] & bit)
        return;
    q->seen[word] |= bit;
    q->pending[word] |= bit;
    q->groups[group] |= 1u << (word & 31);
    q->top |= UINT64_C(1) << group;
}

/* Q-INT-03 and OpenLara's packed key layout: contact decodes one
 * rotation per mesh. Include a fractional tail's physical far key,
 * since the sampler reads it before deciding which key is nearest. */
static void validateContactClip(unsigned type, unsigned clip, bool advances)
{
    const char *name = tr2DoorType(type) ? "door" : game_tr2_inert_interaction(type);
    if (type == ITEM_LARA)
        name = "Lara";
    const Anim *a = gs->level.anims + clip;
    unsigned first = tr64_u16(a->frame_start), last = tr64_u16(a->frame_end);
    if (advances && last == UINT16_MAX)
        sys_error("Tomb Raider II: %s contact pose: type=%u animation=%u frame counter would wrap",
                  name, type, clip);
    unsigned extent = a->frame_size, base = tr64_u32(a->frame_offset) / 2;
    unsigned keys = advances ? 1 + (last - first + a->frame_rate - 1) / a->frame_rate : 1;
    if (extent < 9 || keys > (gs->level.framesCount - base) / extent)
        sys_error("Tomb Raider II: %s packed rotations truncated: type=%u animation=%u key storage",
                  name, type, clip);
    for (unsigned i = 0; i < keys; ++i) {
        PackedFrame key = {gs->level.animFrames + base + i * extent, extent};
        validateSphereKey(type, clip, first + i * a->frame_rate, key);
    }
}

static void validateContactLink(unsigned type, unsigned clip, unsigned next, unsigned frame)
{
    const Anim *a = gs->level.anims + next;
    if (frame < tr64_u16(a->frame_start) || frame > tr64_u16(a->frame_end))
        sys_error("Tomb Raider II: %s contact pose link outside clip: type=%u animation=%u target=%u/%u",
                  tr2DoorType(type) ? "door" : game_tr2_inert_interaction(type),
                  type, clip, next, frame);
}

static int slabFamily(unsigned type)
{
    return tr2BlockType(type) || tr2TrapdoorType(type) ||
           type == TR2_ITEM_COLLAPSE || type == TR2_ITEM_COLLAPSE_HIGH;
}

/* Great Wall objects: the block, hinge and tile consumers switch on a
 * closed state set and index the linked frame. Admission bounds the
 * bank index for every clip; it does not bound a sequel frame. */
static void validateSlabState(unsigned type, unsigned clip, unsigned state, int link)
{
    int bad = (tr2BlockType(type) && (state < 1 || state > 3)) ||
              (tr2TrapdoorType(type) && state > 1) ||
              ((type == TR2_ITEM_COLLAPSE || type == TR2_ITEM_COLLAPSE_HIGH) && state > 3);
    if (bad)
        sys_error("Tomb Raider II: item type %u animation %u has unsupported state%s",
                  type, clip, link ? " link" : "");
}

static void validateSlabFrame(unsigned type, unsigned clip, unsigned next, unsigned frame)
{
    const Anim *target;
    if (!slabFamily(type))
        return;
    if (next >= gs->tr2->animCount)
        sys_error("Tomb Raider II: item type %u animation %u links outside the clip bank",
                  type, clip);
    target = gs->level.anims + next;
    if (frame < tr64_u16(target->frame_start) || frame > tr64_u16(target->frame_end))
        sys_error("Tomb Raider II: %s animation %u frame link outside clip: target=%u/%u",
                  game_tr2_inert_interaction(type), clip, next, frame);
}

static const char *worldAnimName(unsigned type)
{
    if (type == TR2_ITEM_HELICOPTER)
        return "helicopter";
    if (type == TR2_ITEM_WALL_SWITCH)
        return "lever";
    if (tr2DoorType(type))
        return "door";
    const char *inert = game_tr2_inert_interaction(type);
    return inert ? inert : "world item";
}

/* Same destination-clip confinement as creatures and Lara, for every
 * animated world item. Contact types keep their existing message. */
static void confineWorldLink(unsigned type, unsigned clip, unsigned next, unsigned frame)
{
    const Anim *a = gs->level.anims + next;
    if (frame < tr64_u16(a->frame_start) || frame > tr64_u16(a->frame_end))
        sys_error("Tomb Raider II: %s animation link outside clip: type=%u animation=%u target=%u/%u",
                  worldAnimName(type), type, clip, next, frame);
}

static void requireStateByte(unsigned clip, unsigned state)
{
    if (state > UINT8_MAX)
        sys_error("Tomb Raider II: animation %u state %u does not fit", clip, state);
}

static unsigned soundMapCount(void)
{
    return tr64_level_table(gs->core, TR64_CORE_sound_map)->count;
}

static void admitSoundId(unsigned clip, int raw)
{
    unsigned id = (unsigned)((uint16)raw & 0x3fff);
    if (id >= soundMapCount())
        sys_error("Tomb Raider II: animation %u sound %u outside map", clip, id);
}

static void validateTR2WorldAnimations(uint32 *bits, unsigned words, unsigned type)
{
    memset(bits, 0, 2 * words * sizeof(*bits));
    TR2WorldAnimCheck q = {.seen = bits, .pending = bits + words};
    for (unsigned i = 0; i < gs->level.itemsCount; ++i) {
        const ItemObj *item = gs->items + i;
        if (type ? item->type == type :
            item->type == TR2_ITEM_HELICOPTER || (!TR64_RULE(INTERACTIONS) && tr2DoorType(item->type)))
            tr2QueueWorldAnim(&q, item->animIndex);
    }
    /* The disc and grenade projectile are spawned, never placed, so they start at the model. */
    if (type == TR2_ITEM_DISC || type == TR2_ITEM_GRENADE_PROJ) {
        const Model *model = gs->level.models + type;
        if (model->count > 0 && model->animIndex != UINT16_MAX)
            tr2QueueWorldAnim(&q, model->animIndex);
    }
    const bool contact = tr2DoorType(type) || tr2DeferredSphereType(type) ||
                         type == TR2_ITEM_WINDOW || type == TR2_ITEM_STATUE_BLADE ||
                         type == TR2_ITEM_DETONATOR ||
                         type == TR2_ITEM_SWING_SANDBAG || type == TR2_ITEM_SWING_CRATE ||
                         type == TR2_ITEM_DRAWBRIDGE || type == TR2_ITEM_FAN_PROP ||
                         type == TR2_ITEM_FAN_AIR || type == TR2_ITEM_FALLING_CEILING ||
                         type == TR2_ITEM_ROLLING_CRATE;
    while (q.top) {
        unsigned group = __builtin_ctzll(q.top);
        unsigned word = group * 32 + __builtin_ctz(q.groups[group]);
        unsigned bit = __builtin_ctz(q.pending[word]);
        unsigned clip = word * 32 + bit;
        if (!(q.pending[word] &= ~(1u << bit)) &&
            !(q.groups[group] &= ~(1u << (word & 31))))
            q.top &= ~(UINT64_C(1) << group);
        const Anim *anim = gs->level.anims + clip;
        requireStateByte(clip, tr64_u16(anim->state));
        if (contact)
            validateContactClip(type, clip, true);
        const bool zip = type == TR2_ITEM_ZIPLINE;
        unsigned maxState = type == TR2_ITEM_WALL_SWITCH || type == TR2_ITEM_BOULDER ||
                            type == TR2_ITEM_LAUNCHER ||
                            type == TR2_ITEM_SWING_SANDBAG || type == TR2_ITEM_SWING_CRATE ||
                            type == TR2_ITEM_DRAWBRIDGE || type == TR2_ITEM_ELEVATOR ||
                            type == TR2_ITEM_FAN_PROP || type == TR2_ITEM_FAN_AIR ? 1 :
                            type == TR2_ITEM_GUILLOTINE || type == TR2_ITEM_STATUE_BLADE ||
                            type == TR2_ITEM_FALLING_CEILING || type == TR2_ITEM_ROLLING_CRATE ||
                            zip || type == TR2_ITEM_ROLLING_BLADE ? 2 :
                            type == TR2_ITEM_DISC || type == TR2_ITEM_SPIKE_WALL ||
                            type == TR2_ITEM_DETONATOR || type == TR2_ITEM_GRENADE_PROJ ? 0 :
                            type == TR2_ITEM_GONDOLA ? 4 :
                            type == TR2_ITEM_BOAT ? 3 :
                            (type == TR2_ITEM_SMALL_SWITCH || type == TR2_ITEM_PUSH_BUTTON ||
                             type == TR2_ITEM_UW_SWITCH) ? 1 :
                            (type == TR2_ITEM_BIRDS || type == TR2_ITEM_WATERFALL ||
                             type == TR2_ITEM_RACE_CLOCK || type == TR2_ITEM_MINE ||
                             type == TR2_ITEM_WINDOW) ? 0 :
                            UINT16_MAX;
        /* Great Wall objects: block motion is authored as an end offset;
         * only the known landing/settling controllers use deactivation.
         * Stationary leaf/lever or direct helicopter drift. Allow authored
         * sounds and, on the lever alone, settlement. Sources: Ordinary
         * doors, Levers and button switches, and WALL.TR2 clip data.
         * The flying disc, rolling blade and rolling crate carry their step in the clip. */
        bool motion = type == 60 || type == TR2_ITEM_DISC ||
                      type == TR2_ITEM_ROLLING_BLADE || type == TR2_ITEM_ROLLING_CRATE;
        if ((!motion && tr64_i32(anim->speed)) ||
            (type != TR2_ITEM_ROLLING_BLADE && type != TR2_ITEM_ROLLING_CRATE && tr64_i32(anim->acceleration)))
            sys_error("Tomb Raider II: world animation %u uses unsupported clip motion", clip);
        if (tr64_u16(anim->state) > maxState)
            sys_error("Tomb Raider II: item type %u animation %u has unsupported state", type, clip);
        validateSlabState(type, clip, tr64_u16(anim->state), 0);
        unsigned offsets = 0;
        unsigned pos = tr64_u16(anim->command_offset);
        for (unsigned n = tr64_u16(anim->command_count); n; --n) {
            int32 op = tr64_i16(gs->level.animCommands[pos++]);
            if (op == ANIM_CMD_SOUND) {
                admitSoundId(clip, tr64_i16(gs->level.animCommands[pos + 1]));
                pos += 2;
            } else if (op == ANIM_CMD_KILL &&
                     (type == TR2_ITEM_WALL_SWITCH || type == TR2_ITEM_COLLAPSE ||
                      type == TR2_ITEM_COLLAPSE_HIGH || tr2BlockType(type) ||
                      type == TR2_ITEM_GONDOLA || type == TR2_ITEM_SMALL_SWITCH ||
                      type == TR2_ITEM_PUSH_BUTTON || type == TR2_ITEM_UW_SWITCH ||
                      type == TR2_ITEM_DRAWBRIDGE || type == TR2_ITEM_FAN_PROP ||
                      type == TR2_ITEM_FAN_AIR || type == TR2_ITEM_FALLING_CEILING ||
                      type == TR2_ITEM_ROLLING_CRATE))
                continue; /* Authored lever settlement, not a world removal. */
            else if (op == ANIM_CMD_OFFSET && tr2BlockType(type)) {
                /* Occupied and vacated support assumes the authored end
                 * move is one tile, not an arbitrary command offset. */
                int32 x = tr64_i16(gs->level.animCommands[pos]);
                int32 y = tr64_i16(gs->level.animCommands[pos + 1]);
                int32 z = tr64_i16(gs->level.animCommands[pos + 2]);
                int xTile = x == 1024 || x == -1024;
                int zTile = z == 1024 || z == -1024;
                if (y || xTile + zTile != 1 || (x && !xTile) || (z && !zTile))
                    sys_error("Tomb Raider II: push block animation %u end move is not one tile", clip);
                offsets++;
                pos += 3;
            }
            else if (op == ANIM_CMD_OFFSET && zip)
                pos += 3; /* Great Wall Objects, Zip line: authored lead-in move. */
            else if (op != ANIM_CMD_NONE)
                sys_error("Tomb Raider II: world animation %u uses unsupported command %ld", clip, (long)op);
        }
        if (tr2BlockType(type)) {
            unsigned state = tr64_u16(anim->state);
            if ((state == 1 && offsets) || ((state == 2 || state == 3) && offsets != 1))
                sys_error("Tomb Raider II: push block animation %u end move is not one tile", clip);
        }
        unsigned next = tr64_u16(anim->next_animation);
        unsigned frame = tr64_u16(anim->next_frame);
        validateSlabFrame(type, clip, next, frame);
        if (contact || zip)
            validateContactLink(type, clip, next, frame);
        if (!contact)
            confineWorldLink(type, clip, next, frame);
        tr2QueueWorldAnim(&q, next);
        const AnimState *state = gs->level.animStates + tr64_u16(anim->state_offset);
        for (unsigned n = tr64_u16(anim->state_count); n; --n, ++state) {
            requireStateByte(clip, tr64_u16(state->state));
            if (tr64_u16(state->state) > maxState)
                sys_error("Tomb Raider II: item type %u animation %u has unsupported state link", type, clip);
            validateSlabState(type, clip, tr64_u16(state->state), 1);
            const AnimRange *range = gs->level.animRanges + tr64_u16(state->range_offset);
            for (unsigned m = tr64_u16(state->range_count); m; --m, ++range) {
                next = tr64_u16(range->next_animation);
                frame = tr64_u16(range->next_frame);
                if (contact || zip)
                    validateContactLink(type, clip, next, frame);
                if (!contact)
                    confineWorldLink(type, clip, next, frame);
                validateSlabFrame(type, clip, next, frame);
                tr2QueueWorldAnim(&q, next);
            }
        }
    }
}

static void requireHazardState(unsigned clip, unsigned state, const char *name)
{
    if (tr64_u16(gs->level.anims[clip].state) != state)
        sys_error("Tomb Raider II: %s animation %u has unsupported state", name, clip);
}

static bool hasPlacedType(unsigned type)
{
    for (unsigned i = 0; i < gs->level.itemsCount; ++i)
        if (gs->items[i].type == type)
            return true;
    return false;
}

/* Validate hazard clip banks for whichever hazard families are actually
 * placed in this level (disc is spawned by disc launcher). */
static void validateHazardBanks(void)
{
    const struct { unsigned type; unsigned placedType; unsigned clips; const char *name; } rows[] = {
        { TR2_ITEM_DISC, TR2_ITEM_LAUNCHER, 1, "flying disc" },
        { TR2_ITEM_LAUNCHER, TR2_ITEM_LAUNCHER, 2, "disc launcher" },
        { TR2_ITEM_ROLLING_BLADE, TR2_ITEM_ROLLING_BLADE, 4, "rolling blade" },
        { TR2_ITEM_SPIKE_WALL, TR2_ITEM_SPIKE_WALL, 1, "spike wall" }
    };
    for (unsigned i = 0; i < 4; ++i) {
        if (!hasPlacedType(rows[i].placedType))
            continue;
        const Model *model = gs->level.models + rows[i].type;
        unsigned base, end;
        if (model->count <= 0 || model->animIndex == UINT16_MAX)
            sys_error("Tomb Raider II: %s model has no clip bank", rows[i].name);
        base = model->animIndex;
        end = game_tr2_model_bank_end(base);
        if (end < base + rows[i].clips)
            sys_error("Tomb Raider II: %s model has fewer than %u clips", rows[i].name, rows[i].clips);
    }
    if (hasPlacedType(TR2_ITEM_LAUNCHER)) {
        requireHazardState(gs->level.models[TR2_ITEM_LAUNCHER].animIndex, 0, "disc launcher");
        requireHazardState(gs->level.models[TR2_ITEM_LAUNCHER].animIndex + 1, 1, "disc launcher");
        requireHazardState(gs->level.models[TR2_ITEM_DISC].animIndex, 0, "flying disc");
    }
    if (hasPlacedType(TR2_ITEM_ROLLING_BLADE)) {
        requireHazardState(gs->level.models[TR2_ITEM_ROLLING_BLADE].animIndex + 3, 1, "rolling blade");
        if (tr64_u16(gs->level.anims[gs->level.models[TR2_ITEM_ROLLING_BLADE].animIndex + 3].frame_start) != 83)
            sys_error("Tomb Raider II: rolling blade turn pose outside its clip");
    }
    if (hasPlacedType(TR2_ITEM_SPIKE_WALL))
        requireHazardState(gs->level.models[TR2_ITEM_SPIKE_WALL].animIndex, 0, "spike wall");
}

static void validateWorldAnimations(void)
{
    unsigned words = (gs->tr2->animCount + 31) / 32;
    tr64_mem_stats_t io;
    tr64_mem_stats(TR64_MEM_io, &io);
    if (io.used || words > io.budget / (2 * sizeof(uint32)))
        sys_error("Tomb Raider II: world animation validation scratch refused");
    uint32 *bits = tr64_mem_alloc(TR64_MEM_io, 2 * words * sizeof(*bits));
    validateTR2WorldAnimations(bits, words, 0);
    /* Bound animated world items. Adding a row confines that type,
     * requires its state ids to fit a byte, and admits its sound ids.
     * The disc, launcher, rolling blade and spike wall advance under
     * WALL_HAZARDS; their bank length and named poses are checked
     * separately. Campfire does not walk clips. */
    static const struct { unsigned first, last; int hazards; } animated[] = {
        { TR2_ITEM_HELICOPTER, TR2_ITEM_HELICOPTER, 0 },
        { TR2_ITEM_WALL_SWITCH, TR2_ITEM_WALL_SWITCH, 0 },
        { TR2_ITEM_BOULDER, TR2_ITEM_BOULDER, 0 },
        { TR2_ITEM_DOOR_FIRST, TR2_ITEM_DOOR_LAST, 0 },
        { TR2_ITEM_BLOCK_FIRST, TR2_ITEM_BLOCK_LAST, 0 },
        { TR2_ITEM_TRAPDOOR_FIRST, TR2_ITEM_TRAPDOOR_LAST, 0 },
        { TR2_ITEM_COLLAPSE, TR2_ITEM_COLLAPSE, 0 },
        { TR2_ITEM_COLLAPSE_HIGH, TR2_ITEM_COLLAPSE_HIGH, 0 },
        { TR2_ITEM_DETONATOR, TR2_ITEM_DETONATOR, 0 },
        { TR2_ITEM_GUILLOTINE, TR2_ITEM_GUILLOTINE, 1 },
        { TR2_ITEM_STATUE_BLADE, TR2_ITEM_STATUE_BLADE, 1 },
        { TR2_ITEM_ZIPLINE, TR2_ITEM_ZIPLINE, 1 },
        { TR2_ITEM_DISC, TR2_ITEM_DISC, 1 },
        { TR2_ITEM_LAUNCHER, TR2_ITEM_LAUNCHER, 1 },
        { TR2_ITEM_ROLLING_BLADE, TR2_ITEM_ROLLING_BLADE, 1 },
        { TR2_ITEM_SPIKE_WALL, TR2_ITEM_SPIKE_WALL, 1 },
    };
    if (TR64_RULE(INTERACTIONS)) {
        for (unsigned n = 0; n < sizeof animated / sizeof *animated; ++n) {
            if (animated[n].hazards && !TR64_RULE(WALL_HAZARDS))
                continue;
            for (unsigned type = animated[n].first; type <= animated[n].last; ++type) {
                if (!tr2DoorType(type)) {
                    validateTR2WorldAnimations(bits, words, type);
                    continue;
                }
                for (unsigned i = 0; i < gs->level.itemsCount; ++i)
                    if (gs->items[i].type == type && gs->items[i].animIndex != UINT16_MAX) {
                        validateTR2WorldAnimations(bits, words, type);
                        break;
                    }
            }
        }
        if (TR64_RULE(WALL_HAZARDS))
            validateHazardBanks();
        {
            static const unsigned extraAnimated[] = {
                TR2_ITEM_BOAT, TR2_ITEM_GONDOLA, TR2_ITEM_WINDOW,
                TR2_ITEM_SMALL_SWITCH, TR2_ITEM_PUSH_BUTTON, TR2_ITEM_UW_SWITCH,
                TR2_ITEM_BIRDS, TR2_ITEM_RACE_CLOCK, TR2_ITEM_MINE, TR2_ITEM_WATERFALL,
                TR2_ITEM_SWING_SANDBAG, TR2_ITEM_DRAWBRIDGE, TR2_ITEM_ELEVATOR,
                TR2_ITEM_FAN_PROP, TR2_ITEM_FALLING_CEILING, TR2_ITEM_ROLLING_CRATE,
                TR2_ITEM_FAN_AIR, TR2_ITEM_SWING_CRATE
            };
            for (unsigned n = 0; n < sizeof extraAnimated / sizeof *extraAnimated; ++n)
                if (hasPlacedType(extraAnimated[n]))
                    validateTR2WorldAnimations(bits, words, extraAnimated[n]);
            if (gs->level.models[TR2_ITEM_GRENADE_PROJ].count > 0)
                validateTR2WorldAnimations(bits, words, TR2_ITEM_GRENADE_PROJ);
        }
        /* Doors may share clips but have different mesh counts. Check
         * the reachable graph once per placed type, not per instance.
         * Non-advancing hazards only evaluate the placed key. Boulder
         * and, under WALL_HAZARDS, the guillotine are already walked
         * from the table above. */
        uint32 poses[(TR2_PUBLIC_TYPES + 31) / 32] = {0};
        for (unsigned i = 0; i < gs->level.itemsCount; ++i) {
            const ItemObj *item = gs->items + i;
            unsigned type = item->type;
            if (item->animIndex == UINT16_MAX ||
                !(tr2DoorType(type) || tr2DeferredSphereType(type)))
                continue;
            uint32 bit = 1u << (type & 31);
            if (poses[type >> 5] & bit)
                continue;
            poses[type >> 5] |= bit;
            if (tr2DoorType(type))
                validateTR2WorldAnimations(bits, words, type);
            else if (type != TR2_ITEM_BOULDER &&
                     !(TR64_RULE(WALL_HAZARDS) &&
                       (type == TR2_ITEM_GUILLOTINE || type == TR2_ITEM_DISC ||
                        type == TR2_ITEM_ROLLING_BLADE || type == TR2_ITEM_SPIKE_WALL)))
                validateContactClip(type, item->animIndex, false);
        }
    }
    tr64_mem_reset(TR64_MEM_io);
}

static void validateLaraLink(unsigned bodyEnd, unsigned clip, unsigned dst, unsigned frame)
{
    unsigned extraStart = gs->extraAnims.tr3Start;
    bool inBody = dst < bodyEnd || (extraStart >= bodyEnd && dst >= extraStart && dst < gs->tr2->animCount);
    if (!inBody ||
        frame < tr64_u16(gs->level.anims[dst].frame_start) ||
        frame > tr64_u16(gs->level.anims[dst].frame_end))
        sys_error("Tomb Raider II: Lara animation %u has unsupported body link %u/%u",
                  clip, dst, frame);
}

typedef enum { LARA_ARM, LARA_BODY, CREATURE_BODY, REX_BODY } AnimationCommands;

static void validateAnimationCommands(const Anim *a, unsigned clip, AnimationCommands owner)
{
    static const uint8 arguments[] = {0, 3, 2, 0, 0, 2, 2};
    unsigned pos = tr64_u16(a->command_offset);
    for (unsigned n = tr64_u16(a->command_count); n; --n) {
        unsigned op = tr64_i16(gs->level.animCommands[pos++]);
        if (owner == REX_BODY && op == ANIM_CMD_EFFECT &&
            tr64_i16(gs->level.animCommands[pos + 1]) == FX_FLOOR_SHAKE) {
            pos += arguments[op];
            continue;
        }
        if ((owner == CREATURE_BODY || owner == REX_BODY) && (op == ANIM_CMD_EMPTY || op == ANIM_CMD_EFFECT))
            sys_error("Tomb Raider II: creature animation %u incompatible command %u", clip, op);
        if (op == ANIM_CMD_EFFECT) {
            unsigned effect = (uint16)tr64_i16(gs->level.animCommands[pos + 1]);
            if (TR64_RULE(WATER))
                effect &= 0x3fff;
            bool supported = effect == TR2_ANIM_TURN || effect == TR2_ANIM_BUBBLES ||
                             effect == TR2_ANIM_HANDS_FREE ||
                             (TR64_RULE(WATER) && (effect == TR2_ANIM_RESERVED_16 ||
                                                   effect == TR2_ANIM_RESERVED_17));
            if (owner != LARA_BODY || !supported)
                sys_error("Tomb Raider II: Lara animation %u unsupported effect %u", clip, effect);
        }
        if (op == ANIM_CMD_SOUND)
            admitSoundId(clip, tr64_i16(gs->level.animCommands[pos + 1]));
        /* Native Core admission already verified command IDs and spans. */
        pos += arguments[op];
    }
}

/* A body clip follows transitions within its authored bank. An arm
 * clip is selected directly, never advancing through those links. */
static void validateLaraClip(unsigned clip, unsigned bodyEnd)
{
    const Anim *a = gs->level.anims + clip;
    requireStateByte(clip, tr64_u16(a->state));
    unsigned first = tr64_u16(a->frame_start), last = tr64_u16(a->frame_end);
    unsigned rate = a->frame_rate;
    if (last == UINT16_MAX)
        sys_error("Tomb Raider II: Lara animation %u frame counter would wrap", clip);
    validateAnimationCommands(a, clip, bodyEnd ? LARA_BODY : LARA_ARM);
    if (bodyEnd) {
        validateLaraLink(bodyEnd, clip, tr64_u16(a->next_animation), tr64_u16(a->next_frame));
        const AnimState *state = gs->level.animStates + tr64_u16(a->state_offset);
        for (unsigned n = tr64_u16(a->state_count); n; --n, ++state) {
            requireStateByte(clip, tr64_u16(state->state));
            const AnimRange *range = gs->level.animRanges + tr64_u16(state->range_offset);
            for (unsigned m = tr64_u16(state->range_count); m; --m, ++range)
                validateLaraLink(bodyEnd, clip, tr64_u16(range->next_animation), tr64_u16(range->next_frame));
        }
    }
    if (bodyEnd && TR64_RULE(INTERACTIONS)) {
        validateContactClip(ITEM_LARA, clip, true);
        return;
    }
    for (unsigned frame = first; frame <= last; frame += rate)
        validateLaraPose(game_tr2_packed_key(a, clip, frame), clip, frame);
}

/* Authored PC banks end at the next model's start. At admission prove
 * that every reachable body pose supplies the rotations sphere queries read. */
static void validateCreatureLink(unsigned base, unsigned end, unsigned clip, unsigned target, unsigned frame)
{
    if (target < base || target >= end || frame == UINT16_MAX)
        sys_error("Tomb Raider II: creature animation %u leaves body bank at %u/%u", clip, target, frame);
    const Anim *a = gs->level.anims + target;
    if (frame < tr64_u16(a->frame_start) || frame > tr64_u16(a->frame_end))
        sys_error("Tomb Raider II: creature animation %u links outside authored pose %u/%u", clip, target, frame);
}

static void validateCreatureAssets(void)
{
    static const struct { unsigned type, biteJoint; const char *name; } species[] = {
        {TR2_ITEM_SPIDER, TR2_SPIDER_BITE_JOINT, "small spider"},
        {TR2_ITEM_CROW, TR2_CROW_BITE_JOINT, "crow"},
        {TR2_ITEM_TIGER, TR2_TIGER_BITE_JOINT, "tiger"},
        {TR2_ITEM_REX, 13, "Tyrannosaurus"},
        {TR2_ITEM_DOBERMAN, 24, "Doberman"},
        {TR2_ITEM_GUNMAN_1, 0, "gunman"},
        {TR2_ITEM_GUNMAN_2, 0, "gunman"},
        {TR2_ITEM_GUNMAN_3, 0, "gunman"},
        {TR2_ITEM_OPERA_BOSS, 10, "Opera boss"},
        {TR2_ITEM_RAT, 6, "canal rat"},
        {TR2_ITEM_THUG, 10, "club thug"}
    };
    for (unsigned i = 0; i < sizeof(species) / sizeof(*species); ++i) {
        const Model *model = gs->level.models + species[i].type;
        if (model->count <= 0) continue;
        if ((unsigned)model->count <= species[i].biteJoint)
            sys_error("Tomb Raider II: %s bite joint %u outside its model meshes", species[i].name, species[i].biteJoint);
        unsigned base = model->animIndex;
        if (model->type >= TR2_ITEM_GUNMAN_1 && model->type <= TR2_ITEM_GUNMAN_3)
            base = game_tr2_gunman_anim_base(model->type);
        unsigned end = game_tr2_model_bank_end(base);
        if (base >= end || model->count > 32)
            sys_error("Tomb Raider II: invalid creature body bank for %u", model->type);
        if (model->type == TR2_ITEM_CROW) {
            if (base + TR2_CROW_FALL_CLIP >= end)
                sys_error("Tomb Raider II: crow fall animation outside its body bank");
            if (base + TR2_CROW_PERCH_CLIP >= end)
                sys_error("Tomb Raider II: crow perch animation outside its body bank");
        } else if (model->type == TR2_ITEM_TIGER && base + TR2_TIGER_DEATH_CLIP >= end) {
            sys_error("Tomb Raider II: tiger death animation outside its body bank");
        } else if (model->type == TR2_ITEM_SPIDER && base + TR2_SPIDER_LEAP_CLIP >= end) {
            sys_error("Tomb Raider II: small spider leap animation outside its body bank");
        } else if (model->type == TR2_ITEM_DOBERMAN && base + 13u >= end) {
            sys_error("Tomb Raider II: Doberman death animation outside its body bank");
        } else if (model->type >= TR2_ITEM_GUNMAN_1 && model->type <= TR2_ITEM_GUNMAN_3 && base + 21u >= end) {
            sys_error("Tomb Raider II: gunman death animation outside its body bank");
        } else if (model->type == TR2_ITEM_OPERA_BOSS && base + 32u >= end) {
            sys_error("Tomb Raider II: Opera boss death animation outside its body bank");
        } else if (model->type == TR2_ITEM_RAT && base + 9u >= end) {
            sys_error("Tomb Raider II: canal rat death animation outside its body bank");
        } else if (model->type == TR2_ITEM_THUG && base + 30u >= end) {
            sys_error("Tomb Raider II: club thug climb/death animation outside its body bank");
        }
        unsigned depth = 1; /* The sphere cursor pushes its base matrix. */
        for (unsigned mesh = 1; mesh < (unsigned)model->count; ++mesh) {
            uint32 flags = tr64_i32(gs->level.nodes[(model->nodeIndex + mesh - 1) * 4]);
            if ((flags & NODE_FLAG_POP) && --depth == 0)
                sys_error("Tomb Raider II: creature skeleton pops its base for %u", model->type);
            if ((flags & NODE_FLAG_PUSH) && ++depth >= sizeof(gs->gMatrixStack) / sizeof(*gs->gMatrixStack))
                sys_error("Tomb Raider II: creature skeleton exceeds matrix stack for %u", model->type);
        }
        for (unsigned clip = base; clip < end; ++clip) {
            const Anim *a = gs->level.anims + clip;
            requireStateByte(clip, tr64_u16(a->state));
            unsigned first = tr64_u16(a->frame_start), last = tr64_u16(a->frame_end);
            if (last == UINT16_MAX)
                sys_error("Tomb Raider II: creature animation %u frame counter would wrap", clip);
            validateAnimationCommands(a, clip, model->type == TR2_ITEM_REX ? REX_BODY : CREATURE_BODY);
            validateCreatureLink(base, end, clip, tr64_u16(a->next_animation), tr64_u16(a->next_frame));
            const AnimState *state = gs->level.animStates + tr64_u16(a->state_offset);
            for (unsigned n = tr64_u16(a->state_count); n; --n, ++state) {
                requireStateByte(clip, tr64_u16(state->state));
                const AnimRange *range = gs->level.animRanges + tr64_u16(state->range_offset);
                for (unsigned m = tr64_u16(state->range_count); m; --m, ++range)
                    validateCreatureLink(base, end, clip, tr64_u16(range->next_animation), tr64_u16(range->next_frame));
            }
            for (unsigned frame = first; frame <= last; frame += a->frame_rate)
                validatePackedPose(game_tr2_packed_key(a, clip, frame), clip, frame, model->count);
            if ((last - first) % a->frame_rate) {
                ItemObj pose = {.animIndex = clip, .frameIndex = last};
                validatePackedPose(game_tr2_packed_frames(&pose).b, clip, last, model->count);
            }
        }
    }
}

/* tr2-lara-arms.md:85 names goal 4 and the two ranges that choose the clip. */
enum { TR2_SHOTGUN_EXIT_GOAL = 4 };

static const char *shotgunClipName(unsigned rel)
{
    static const char *const name[] = { "aim", "draw", "recoil", "stow", "lower" };
    return rel < SHOTGUN_CLIPS ? name[rel] : "clip";
}

/* True when the next runtime step fires a state-change range, not the end link. */
static bool shotgunRangeFires(const ItemObj *at, uint8 goal, unsigned *dest)
{
    ItemObj copy = *at;
    copy.goalState = goal;
    const Anim *anim = gs->level.anims + copy.animIndex;
    unsigned before = copy.frameIndex;
    copy.frameIndex++;
    ItemObj_animChange(&copy, anim);
    if (copy.animIndex == at->animIndex && copy.frameIndex == (uint16)(before + 1))
        return false;
    *dest = copy.animIndex;
    return true;
}

/* One start frame. persist re-applies the goal after every end link, which
 * is what a holster request does. A named destination must be arrived at.
 * An authored destination is honoured when a range for its state fires
 * while the request is still active. */
static bool shotgunWalk(unsigned base, unsigned fromRel, unsigned toRel, unsigned kind, bool named, unsigned start, bool persist)
{
    unsigned from = base + fromRel;
    unsigned target = base + toRel;
    uint8 goal;
    if (kind == TR2_SG_EXIT)
        goal = game_tr2_shotgun_clip_state(from);
    else if (!game_tr2_shotgun_goal(fromRel, toRel, kind, start, &goal))
        return false;
    ItemObj probe = {
        .animIndex = (uint16)from,
        .frameIndex = (uint16)start,
        .state = game_tr2_shotgun_clip_state(from),
        .goalState = kind == TR2_SG_EXIT ? game_tr2_shotgun_clip_state(from) : goal,
        .type = TR2_MODEL_SHOTGUN
    };
    for (unsigned step = 0; step < 64; ++step) {
        bool active = persist || probe.animIndex == from;
        if (active && kind != TR2_SG_EXIT) {
            unsigned dest = 0;
            if (shotgunRangeFires(&probe, goal, &dest))
                return !named || dest == target;
        }
        game_tr2_shotgun_checking = true;
        ItemObj_animProcess(&probe, false);
        game_tr2_shotgun_checking = false;
        if (persist)
            probe.goalState = goal;
        if (named && probe.animIndex == target)
            return true;
        if (!persist && probe.animIndex != from)
            return false;
    }
    return false;
}

/* One shared step from a recoil frame under a fixed goal. */
static void shotgunOneStep(unsigned base, unsigned frame, uint8 goal, unsigned *anim, unsigned *landed)
{
    ItemObj probe = {
        .animIndex = (uint16)(base + SHOTGUN_RECOIL),
        .frameIndex = (uint16)frame,
        .state = game_tr2_shotgun_clip_state(base + SHOTGUN_RECOIL),
        .goalState = goal,
        .type = TR2_MODEL_SHOTGUN
    };
    game_tr2_shotgun_checking = true;
    ItemObj_animProcess(&probe, false);
    game_tr2_shotgun_checking = false;
    *anim = probe.animIndex;
    *landed = probe.frameIndex;
}

/* tr2-lara-arms.md:85. Relative frames 1..12 stay on recoil. The step
 * that leaves relative 12, and the step that starts at absolute 60,
 * land on aim 222/0. No other frame of this goal leaves the clip,
 * apart from the lower window checked with the other request. */
static bool shotgunExitAim(unsigned base)
{
    unsigned first = tr64_u16(gs->level.anims[base + SHOTGUN_RECOIL].frame_start);
    unsigned last = tr64_u16(gs->level.anims[base + SHOTGUN_RECOIL].frame_end);
    unsigned aim = base + SHOTGUN_AIM;
    unsigned aimStart = tr64_u16(gs->level.anims[aim].frame_start);
    unsigned recoil = base + SHOTGUN_RECOIL;
    unsigned exitAt = first + 12u;
    for (unsigned rel = 1; rel <= 12; ++rel) {
        ItemObj probe = {
            .animIndex = (uint16)recoil,
            .frameIndex = (uint16)(first + rel),
            .state = game_tr2_shotgun_clip_state(recoil),
            .goalState = TR2_SHOTGUN_EXIT_GOAL,
            .type = TR2_MODEL_SHOTGUN
        };
        bool arrived = false;
        for (unsigned step = 0; step < 16 && !arrived; ++step) {
            unsigned before = probe.frameIndex;
            unsigned beforeAnim = probe.animIndex;
            game_tr2_shotgun_checking = true;
            ItemObj_animProcess(&probe, false);
            game_tr2_shotgun_checking = false;
            probe.goalState = TR2_SHOTGUN_EXIT_GOAL;
            if (beforeAnim == recoil && before == exitAt)
                arrived = probe.animIndex == aim && probe.frameIndex == aimStart;
            else if (probe.animIndex != recoil)
                return false;
        }
        if (!arrived)
            return false;
    }
    unsigned anim = 0, landed = 0;
    shotgunOneStep(base, first + 13u, TR2_SHOTGUN_EXIT_GOAL, &anim, &landed);
    if (anim != aim || landed != aimStart)
        return false;
    for (unsigned frame = first + 1u; frame <= last; ++frame) {
        if (frame == exitAt || frame == first + 13u)
            continue;
        shotgunOneStep(base, frame, TR2_SHOTGUN_EXIT_GOAL, &anim, &landed);
        if (anim != recoil)
            return false;
    }
    return true;
}

/* The update that starts at absolute 47 selects lower 226/0. */
static bool shotgunExitLower(unsigned base)
{
    unsigned first = tr64_u16(gs->level.anims[base + SHOTGUN_RECOIL].frame_start);
    unsigned lower = base + SHOTGUN_LOWER;
    unsigned lowerStart = tr64_u16(gs->level.anims[lower].frame_start);
    unsigned anim = 0, landed = 0;
    shotgunOneStep(base, first, TR2_SHOTGUN_EXIT_GOAL, &anim, &landed);
    return anim == lower && landed == lowerStart;
}

/* Goal 0 at the first frame loops this clip. It must not select aim. */
static bool shotgunGoal0Loops(unsigned base)
{
    unsigned first = tr64_u16(gs->level.anims[base + SHOTGUN_RECOIL].frame_start);
    unsigned anim = 0, landed = 0;
    shotgunOneStep(base, first, 0, &anim, &landed);
    (void)landed;
    return anim == base + SHOTGUN_RECOIL;
}

static bool shotgunRequestHonoured(unsigned base, const TR2ShotgunReq *req, unsigned fromRel)
{
    if (req->kind == TR2_SG_LOOP0)
        return shotgunGoal0Loops(base);
    if ((req->from == req->to || fromRel == req->to) && req->kind != TR2_SG_EXIT)
        return true;
    if (req->kind == TR2_SG_CITED4)
        return req->to == SHOTGUN_AIM ? shotgunExitAim(base) : shotgunExitLower(base);
    const Anim *anim = gs->level.anims + base + fromRel;
    unsigned first = tr64_u16(anim->frame_start), last = tr64_u16(anim->frame_end);
    bool named = req->cite != 0;
    /* Holster is requested on whatever frame the clip is on. Every frame
     * must honour it. A known start (aim at the draw link, recoil at
     * relative 0, a cited clip at its start) is one walk from that frame,
     * and the first range that fires is the one that counts. */
    if (req->from == TR2_SHOTGUN_ANY) {
        for (unsigned frame = first; frame <= last; ++frame)
            if (!shotgunWalk(base, fromRel, req->to, req->kind, named, frame, true))
                return false;
        return true;
    }
    unsigned start = first;
    if (fromRel == SHOTGUN_AIM)
        start = tr64_u16(gs->level.anims[base + SHOTGUN_DRAW].next_frame);
    return shotgunWalk(base, fromRel, req->to, req->kind, named, start, false);
}

/* tr2-lara-arms: the five long-gun poses use the ordinary item
 * transition machine, not the pistol/flare independent frame counter.
 * Cited spans: draw 13-46, recoil 47-79, stow 80-113
 * (tr2-lara-arms 78-83; tr2-lara-ground-and-weapons 156-158). Aim ends
 * on the frame before that draw; its start is the level record.
 * The lower clip's span is the level record, which has no cited endpoints. */
static void validateShotgunBank(void)
{
    static const uint16 citedStart[3] = { 13, 47, 80 };
    static const uint16 citedEnd[3] = { 46, 79, 113 };
    const Model *model = gs->level.models + TR2_MODEL_SHOTGUN;
    unsigned base = model->animIndex, end = base + SHOTGUN_CLIPS;
    if (model->count != JOINT_MAX || end > gs->tr2->animCount)
        sys_error("Tomb Raider II: invalid shotgun animation bank");
    for (unsigned type = 0; type < TR2_PUBLIC_TYPES; ++type) {
        const Model *other = gs->level.models + type;
        if (type != TR2_MODEL_SHOTGUN && other->count > 0 &&
            other->animIndex >= base && other->animIndex < end)
            sys_error("Tomb Raider II: shotgun poses overlap model %u", type);
    }
    for (unsigned clip = base; clip < end; ++clip) {
        const Anim *a = gs->level.anims + clip;
        unsigned k = clip - base;
        unsigned first, last;
        if (k == SHOTGUN_LOWER || k == SHOTGUN_AIM) {
            first = tr64_u16(a->frame_start);
            last = tr64_u16(a->frame_end);
            if (a->frame_rate != 1 || last < first || last == UINT16_MAX)
                sys_error("Tomb Raider II: invalid shotgun clip %u", clip);
            if (k == SHOTGUN_AIM && last + 1u != 13u)
                sys_error("Tomb Raider II: shotgun aim does not meet the draw clip");
        } else {
            first = k == SHOTGUN_DRAW ? citedStart[0] : k == SHOTGUN_RECOIL ? citedStart[1] : citedStart[2];
            last = citedEnd[k - 1];
            if (a->frame_rate != 1 || tr64_u16(a->frame_start) != first ||
                tr64_u16(a->frame_end) != last)
                sys_error("Tomb Raider II: invalid shotgun clip %u", clip);
        }
        (void)game_tr2_shotgun_clip_state(clip);
        validateAnimationCommands(a, clip, CREATURE_BODY);
        validateCreatureLink(base, end, clip, tr64_u16(a->next_animation), tr64_u16(a->next_frame));
        const AnimState *state = gs->level.animStates + tr64_u16(a->state_offset);
        for (unsigned n = tr64_u16(a->state_count); n; --n, ++state) {
            const AnimRange *r = gs->level.animRanges + tr64_u16(state->range_offset);
            for (unsigned count = tr64_u16(state->range_count); count; --count, ++r)
                validateCreatureLink(base, end, clip, tr64_u16(r->next_animation), tr64_u16(r->next_frame));
        }
        for (unsigned f = first; f <= last; ++f)
            validateLaraPose(game_tr2_packed_key(a, clip, f), clip, f);
    }
    const Anim *draw = gs->level.anims + base + SHOTGUN_DRAW;
    const Anim *recoil = gs->level.anims + base + SHOTGUN_RECOIL;
    if (tr64_u16(draw->next_animation) != base + SHOTGUN_AIM ||
        tr64_u16(draw->next_frame) != tr64_u16(gs->level.anims[base + SHOTGUN_AIM].frame_start))
        sys_error("Tomb Raider II: shotgun draw does not enter the aim clip");
    if (tr64_u16(recoil->next_animation) != base + SHOTGUN_RECOIL ||
        tr64_u16(recoil->next_frame) != tr64_u16(recoil->frame_start))
        sys_error("Tomb Raider II: shotgun recoil does not return to its first frame");
    unsigned reqCount = 0;
    const TR2ShotgunReq *reqs = game_tr2_shotgun_reqs(&reqCount);
    for (unsigned i = 0; i < reqCount; ++i) {
        const TR2ShotgunReq *req = reqs + i;
        unsigned from = req->from == TR2_SHOTGUN_ANY ? 0 : req->from;
        unsigned last = req->from == TR2_SHOTGUN_ANY ? SHOTGUN_CLIPS : from + 1;
        for (; from < last; ++from) {
            if (shotgunRequestHonoured(base, req, from))
                continue;
            if (req->kind == TR2_SG_CITED4 && req->to == SHOTGUN_AIM)
                sys_error("Tomb Raider II: shotgun recoil release has no range to aim");
            if (req->kind == TR2_SG_CITED4)
                sys_error("Tomb Raider II: shotgun recoil has no range to lower");
            if (req->kind == TR2_SG_LOOP0)
                sys_error("Tomb Raider II: shotgun recoil goal 0 selects aim");
            sys_error("Tomb Raider II: shotgun %s has no range for state %u",
                      shotgunClipName(from), game_tr2_shotgun_clip_state(base + req->to));
        }
    }
}

/* Authored ranges of the auxiliary PC arm models, not Lara's body clip.
 * The arm handlers step a separate sequence, as specified by the wall. */
static void validateArms(void)
{
    static const uint8 pistolEnds[] = {0, PISTOL_DRAW_START, PISTOL_UNHOLSTER, PISTOL_RECOIL,
                                       PISTOL_RECOIL + PISTOL_RECOIL_TICKS};
    static const uint8 flareEnds[] = {FLARE_REST, 1, FLARE_DRAW_START, FLARE_IGNITION,
                                      FLARE_RAISE, FLARE_RAISE_END + 1};
    const unsigned types[] = {TR2_MODEL_PISTOL_ARMS, TR2_MODEL_FLARE_ARMS};
    const uint8 *ends[] = {pistolEnds, flareEnds};
    const unsigned count[] = {sizeof(pistolEnds), sizeof(flareEnds)};
    /* WALL.TR2's Lara body bank starts at zero and ends before the
     * first auxiliary pistol clip. Include the direct body selections. */
    unsigned bodyEnd = gs->level.models[TR2_MODEL_PISTOL_ARMS].animIndex;
    if (gs->level.models[ITEM_LARA].animIndex || gs->level.models[ITEM_LARA].count != JOINT_MAX ||
        bodyEnd <= Lara_ANIM_UW_ROLL || bodyEnd >= gs->tr2->animCount)
        sys_error("Tomb Raider II: invalid Lara body animation bank");
    if (bodyEnd <= Lara_ANIM_SURF_STAND ||
        tr64_u16(gs->level.anims[Lara_ANIM_WATER_OUT].state) != Lara_STATE_WATER_OUT ||
        tr64_u16(gs->level.anims[Lara_ANIM_SURF_OUT].state) != Lara_STATE_WATER_OUT ||
        tr64_u16(gs->level.anims[Lara_ANIM_SURF_STAND].state) != Lara_STATE_WATER_OUT)
        sys_error("Tomb Raider II: water exit clip does not carry the climb state");
    if (TR64_RULE(WALL_HAZARDS) &&
        (bodyEnd <= TR2_LARA_ZIP_MOUNT ||
         tr64_u16(gs->level.anims[TR2_LARA_ZIP_MOUNT].state) != Lara_STATE_HANG_UP ||
         tr64_u16(gs->level.anims[Lara_ANIM_DEATH_BOULDER].state) != Lara_STATE_SPECIAL ||
         tr64_u16(gs->level.anims[Lara_ANIM_DEATH_SPIKES].state) != Lara_STATE_DEATH))
        sys_error("Tomb Raider II: invalid Lara hazard or zip-line clip");
    for (unsigned clip = 0; clip < bodyEnd; ++clip)
        validateLaraClip(clip, bodyEnd);
    if (gs->extraAnims.tr3Start >= bodyEnd) {
        for (unsigned clip = gs->extraAnims.tr3Start; clip < gs->tr2->animCount; ++clip)
            validateLaraClip(clip, bodyEnd);
    }

    for (unsigned i = 0; i < 2; ++i) {
        const Model *m = gs->level.models + types[i];
        unsigned base = m->animIndex, clips = count[i] - 1;
        if (m->count != JOINT_MAX || base >= gs->tr2->animCount || clips > gs->tr2->animCount - base)
            sys_error("Tomb Raider II: invalid auxiliary arm model %u", types[i]);
        for (unsigned k = 0; k < clips; ++k) {
            const Anim *a = gs->level.anims + base + k;
            if (a->frame_rate != 1 || tr64_u16(a->frame_start) != ends[i][k] ||
                tr64_u16(a->frame_end) != ends[i][k + 1] - 1u)
                sys_error("Tomb Raider II: invalid authored arm range %u", base + k);
            validateLaraClip(base + k, 0);
        }
    }
    validateShotgunBank();
    const Model *world = gs->level.models + TR2_ITEM_LOOSE_FLARE;
    if (world->count != 1 || world->animIndex >= gs->tr2->animCount ||
        FLARE_BODY_THROW >= gs->tr2->animCount)
        sys_error("Tomb Raider II: invalid loose flare model or body throw clip");
    const Anim *toss = gs->level.anims + FLARE_BODY_THROW;
    unsigned first = tr64_u16(toss->frame_start);
    if (first > UINT16_MAX - FLARE_BODY_THROW_FINISH ||
        tr64_u16(toss->frame_end) < first + FLARE_BODY_THROW_FINISH)
        sys_error("Tomb Raider II: invalid body throw range");
}

/* Core admission already bounded instruction spans and item indices.
 * Read the authored programs even when a door hides their live sector. */
static void validateOwners(void)
{
    uint32 bound[MAX_ITEMS / 32] = {0};
    unsigned words = tr64_level_table(gs->core, TR64_CORE_floor_data)->count;
    unsigned bytes = (words + 7) / 8;
    tr64_mem_stats_t io;
    tr64_mem_stats(TR64_MEM_io, &io);
    if (io.used || bytes > io.budget)
        sys_error("Tomb Raider II: interaction floor validation scratch refused");
    uint8 *seen = tr64_mem_alloc(TR64_MEM_io, bytes);
    memset(seen, 0, bytes);
    unsigned count = tr64_level_table(gs->core, TR64_CORE_sectors)->count;
    for (unsigned i = 0; i < count; ++i) {
        unsigned index = tr64_u16(tr64_level_sectors(gs->core, i)->floor_index);
        if (!index)
            continue;
        const FloorData *fd = gs->level.floors + index;
        uint16 header;
        do {
            unsigned word = fd - gs->level.floors;
            unsigned bit = 1u << (word & 7);
            if (seen[word >> 3] & bit)
                break;
            seen[word >> 3] |= bit;
            header = tr64_u16(*fd++);
            unsigned kind = header & TR64_TR2_FLOOR_KIND_MASK;
            if (TR64_TR2_FLOOR_PAYLOAD_MASK & (1u << kind))
                ++fd;
            if (kind != FLOOR_TYPE_TRIGGER)
                continue;
            ++fd;
            unsigned type = FD_TRIGGER_TYPE(header);
            if (TR64_TR2_FLOOR_CONTROLLER_MASK & (1u << type)) {
                uint16 owner = tr64_u16(*fd++);
                unsigned slot = FD_ARGS(owner);
                const ItemObj *item = gs->items + slot;
                if (type == TRIGGER_TYPE_PICKUP && !game_tr2_pickup_type(item->type))
                    game_tr2_refuse_interaction(item, "unsupported pickup trigger owner");
                if (type == TRIGGER_TYPE_SWITCH && item->type == TR2_ITEM_WALL_SWITCH &&
                    !FD_END(owner))
                    bound[slot >> 5] |= 1u << (slot & 31);
                if (FD_END(owner))
                    continue;
            }
            uint16 action;
            do {
                action = tr64_u16(*fd++);
                if ((FD_ACTION(action) & TR64_TR2_FLOOR_ACTION_KIND_MASK) == TRIGGER_ACTION_ACTIVATE_CAMERA)
                    action = tr64_u16(*fd++);
            } while (!FD_END(action));
        } while (!FD_END(header));
    }
    tr64_mem_reset(TR64_MEM_io);
    for (unsigned i = 0; i < gs->level.itemsCount; ++i)
        if (gs->items[i].type == TR2_ITEM_WALL_SWITCH && !(bound[i >> 5] & (1u << (i & 31))))
            game_tr2_refuse_interaction(gs->items + i, "wall switch has no controller trigger");
}

static void validateInteractions(void)
{
    validateOwners();
    static const struct { uint16 clip, state, commit; } required[] = {
        {SWITCH_TO_0, Lara_STATE_SWITCH_DOWN, 0},
        {SWITCH_TO_1, Lara_STATE_SWITCH_UP, 0},
        {PICKUP_SWIM, Lara_STATE_PICKUP, PICKUP_SWIM_COMMIT},
        {KEY_USE, Lara_STATE_USE_KEY, 0},
        {PUZZLE_USE, Lara_STATE_USE_PUZZLE, 80},
        {PICKUP_STAND, Lara_STATE_PICKUP, PICKUP_STAND_COMMIT}
    };
    for (unsigned i = 0; i < sizeof(required) / sizeof(*required); ++i) {
        unsigned clip = required[i].clip;
        if (clip >= gs->tr2->animCount)
            sys_error("Tomb Raider II: missing Lara interaction animation %u", clip);
        const Anim *anim = gs->level.anims + clip;
        if (tr64_u16(anim->state) != required[i].state ||
            tr64_u16(anim->frame_start) + required[i].commit > tr64_u16(anim->frame_end))
            sys_error("Tomb Raider II: invalid Lara interaction animation %u", clip);
    }
}

static bool admittedPlacementType(unsigned type)
{
    if (type == ITEM_LARA || tr2DoorType(type) || game_tr2_item_ticks(type) ||
        game_tr2_creature_type(type) || game_tr2_pickup_type(type) ||
        tr2BlockType(type) || type == TR2_ITEM_SPIKES ||
        type == TR2_ITEM_CAMERA_TARGET || type == TR2_ITEM_WINDOW ||
        (type >= 182 && type <= 189) || (type >= 201 && type <= 204))
        return true;
    switch (type) {
    case 147: case 195: /* Pickups not yet pickup-admitted. */
        return true;
    default:
        return false;
    }
}

/* Generic TR2 object-type admission driven purely by placed types (never
 * level name/ID or hardcoded item slot indices). */
static void validateExtraObjects(void)
{
    if (hasPlacedType(TR2_ITEM_BOAT)) {
        const Model *laraBoat = gs->level.models + TR2_MODEL_LARA_BOAT;
        if (laraBoat->count != JOINT_MAX || laraBoat->animIndex == UINT16_MAX ||
            game_tr2_model_bank_end(laraBoat->animIndex) < laraBoat->animIndex + 19u)
            sys_error("Tomb Raider II: invalid Lara boat animation bank");
        const Model *boatModel = gs->level.models + TR2_ITEM_BOAT;
        if (boatModel->count <= 0 || boatModel->animIndex == UINT16_MAX ||
            game_tr2_model_bank_end(boatModel->animIndex) < boatModel->animIndex + 18u)
            sys_error("Tomb Raider II: invalid speedboat animation bank");
    }
    if (hasPlacedType(TR2_ITEM_DETONATOR)) {
        const Model *laraExtra = gs->level.models + TR2_MODEL_LARA_EXTRA;
        if (laraExtra->count != JOINT_MAX || laraExtra->animIndex == UINT16_MAX ||
            game_tr2_model_bank_end(laraExtra->animIndex) < laraExtra->animIndex + 2u)
            sys_error("Tomb Raider II: invalid Lara extra animation bank");
        unsigned base = laraExtra->animIndex;
        if (tr64_u16(gs->level.anims[base].state) != 0 ||
            tr64_u16(gs->level.anims[base + 1u].state) != 1)
            sys_error("Tomb Raider II: invalid Lara detonator extra clips");
        validateLaraClip(base, 0);
        validateLaraClip(base + 1u, 0);
    }
    if (hasPlacedType(137u)) {
        if (gs->level.models[TR2_MODEL_AUTO_ARMS].count != JOINT_MAX)
            sys_error("Tomb Raider II: invalid Auto Pistols arm model");
        if (gs->level.models[144u].count == 0)
            sys_error("Tomb Raider II: missing Auto Pistols ammo model");
    }
    if (hasPlacedType(138u)) {
        if (gs->level.models[TR2_MODEL_UZI_ARMS].count != JOINT_MAX)
            sys_error("Tomb Raider II: invalid Uzis arm model");
        if (gs->level.models[145u].count == 0)
            sys_error("Tomb Raider II: missing Uzis ammo model");
    }
    if (hasPlacedType(141u) || gs->level.models[TR2_MODEL_GRENADE].count > 0) {
        const Model *gm = gs->level.models + TR2_MODEL_GRENADE;
        if (gm->count != JOINT_MAX || gm->animIndex == UINT16_MAX ||
            game_tr2_model_bank_end(gm->animIndex) < gm->animIndex + 6u)
            sys_error("Tomb Raider II: invalid Grenade Launcher arm model");
        for (unsigned k = 0; k < 6u; ++k) {
            const Anim *a = gs->level.anims + gm->animIndex + k;
            validateAnimationCommands(a, gm->animIndex + k, CREATURE_BODY);
            unsigned first = tr64_u16(a->frame_start), last = tr64_u16(a->frame_end);
            for (unsigned f = first; f <= last; f += a->frame_rate)
                validateLaraPose(game_tr2_packed_key(a, gm->animIndex + k, f), gm->animIndex + k, f);
        }
        if (gs->level.models[TR2_ITEM_GRENADE_PROJ].count <= 0)
            sys_error("Tomb Raider II: missing Grenade projectile model");
    }
    for (unsigned i = 0; i < gs->level.itemsCount; ++i) {
        const ItemObj *it = gs->items + i;
        if (!admittedPlacementType(it->type))
            sys_error("Tomb Raider II: unsupported placement type %u (item=%u)",
                      (unsigned)it->type, i);
        if (it->type == TR2_ITEM_WINDOW && it->animIndex == UINT16_MAX)
            sys_error("Tomb Raider II: breakable window %u has no animation", i);
    }
}

static uint32_t initPlacement(unsigned index)
{
    ItemObj *item = gs->items + index;
    const tr64_core_items_t *s = tr64_level_items(gs->core, index);
    unsigned type = tr64_u16(s->type);
    /* Q-T2K-10: type 88 shootable scenery is not yet admitted; type 72
     * breakable windows are admitted and handled by tr2_urban. */
    if (TR64_RULE(COMBAT) && type == 88)
        sys_error("Tomb Raider II: unsupported shootable scenery type %u",type);
    Room *room = gs->rooms + tr64_u16(s->room);
    const Model *model = gs->level.models + type;
    item->type = type;
    item->pos = vec3i_create(tr64_i32(s->x), tr64_i32(s->y), tr64_i32(s->z));
    item->angle.y = tr64_i16(s->angle);
    item->animIndex = UINT16_MAX;
    if (model->count > 0) {
        unsigned startAnim = model->animIndex;
        if (type >= TR2_ITEM_GUNMAN_1 && type <= TR2_ITEM_GUNMAN_3)
            startAnim = game_tr2_gunman_anim_base(type);
        if (startAnim != UINT16_MAX)
            ItemObj_animSet(item, startAnim, true, 0);
    }
    if (type==TR2_ITEM_CROW) {
        /* Great Wall creatures: authored perch and a draw in placement order. */
        if (item->animIndex==UINT16_MAX)
            sys_error("Tomb Raider II: crow placement has no body bank");
        ItemObj_animSet(item,model->animIndex+TR2_CROW_PERCH_CLIP,true,0);
        item->angle.y=wrap_i16(item->angle.y+((rand_logic()-16384)>>1));
    }
    if (type==TR2_ITEM_OPERA_BOSS) {
        /* tr2-opera-house.md §9: base clip + 3 (state 2) and ±45° yaw jitter. */
        if (item->animIndex==UINT16_MAX)
            sys_error("Tomb Raider II: Opera boss placement has no body bank");
        ItemObj_animSet(item,model->animIndex+3u,true,0);
        item->angle.y=wrap_i16(item->angle.y+((rand_logic()-16384)>>1));
    }
    if (TR64_RULE(WALL_HAZARDS) && type==TR2_ITEM_GUILLOTINE) {
        /* tr2-first-levels, Guillotine blades: the fixed blade starts at
         * animation offset 2, current state 1. The bank ends at the next
         * model's start (game_tr2_model_bank_end), not the level total. */
        unsigned base=model->animIndex,end=game_tr2_model_bank_end(base);
        if (item->animIndex==UINT16_MAX || end<base+3u)
            sys_error("Tomb Raider II: guillotine model has fewer than three clips");
        unsigned clip=base+2u;
        if (tr64_u16(gs->level.anims[clip].state)!=1)
            sys_error("Tomb Raider II: guillotine idle clip outside its body bank");
        ItemObj_animSet(item,clip,true,0);
    }
    if (TR64_RULE(WALL_HAZARDS) && type==TR2_ITEM_STATUE_BLADE) {
        /* tr2-bartoli-and-weapons.md §3: Statue with knife blade starts at
         * model base animation + 3, first frame, current state 1. */
        unsigned base=model->animIndex,end=game_tr2_model_bank_end(base);
        if (item->animIndex==UINT16_MAX || end<base+4u)
            sys_error("Tomb Raider II: knife statue model has fewer than four clips");
        unsigned clip=base+3u;
        if (tr64_u16(gs->level.anims[clip].state)!=1)
            sys_error("Tomb Raider II: knife statue idle clip outside its body bank");
        ItemObj_animSet(item,clip,true,0);
    }
    if (!TR64_RULE(WORLD_ITEMS)) {
        /* An authored pose for the camera's inert subjects, as before. */
        item->room = room;
        ++gs->tr2->inertByType[type];
        return 0;
    }
    if ((game_tr2_item_ticks(type) || tr2BlockType(type) ||
         (TR64_RULE(WALL_HAZARDS) && type == TR2_ITEM_SPIKES)) && item->animIndex == UINT16_MAX)
        sys_error("Tomb Raider II: no animation for placed type %u", type);
    item->roomFloor = Room_getSector(room, item->pos.x, item->pos.z)->floor * 256;
    item->visibleMask = UINT32_MAX;
    item->health = type==TR2_ITEM_TIGER ? 20 : type==TR2_ITEM_SPIDER ? 5 :
                   type==TR2_ITEM_CROW ? 15 : type==TR2_ITEM_REX ? 100 :
                   type == TR2_ITEM_DOBERMAN ? 10 :
                   (type >= TR2_ITEM_GUNMAN_1 && type <= TR2_ITEM_GUNMAN_3) ? 25 :
                   type == TR2_ITEM_OPERA_BOSS ? 150 :
                   type == TR2_ITEM_RAT ? 4 :
                   type == TR2_ITEM_THUG ? 27 :
                   -16384;
    item->flags = (game_tr2_placement_flags(s, index) & 0xff00u) | ITEM_FLAG_COLLISION;
    if ((item->flags & ITEM_FLAG_ONCE) || game_tr2_creature_type(type))
        item->flags = (item->flags & ~ITEM_FLAG_ONCE) | ITEM_FLAG_STATUS_INVISIBLE;
    Room_add(room, item);
    uint32_t activate = 0;
    if ((item->flags & ITEM_FLAG_MASK) == ITEM_FLAG_MASK) {
        item->flags = (item->flags & ~ITEM_FLAG_MASK) | ITEM_FLAG_REVERSE;
        activate = 1;
    }
    if (!game_tr2_item_ticks(type) && !tr2BlockType(type) &&
        !(TR64_RULE(INTERACTIONS) && game_tr2_pickup_type(type)))
        ++gs->tr2->inertByType[type];
    return activate;
}

uint32_t tr64_unit_tr2_validate(uint32_t op)
{
    switch (op & 0xffu) {
    case TR2_VALIDATE_WATER_LINKS: validateWaterLinks(); break;
    case TR2_VALIDATE_CREATURE_ASSETS: validateCreatureAssets(); break;
    case TR2_VALIDATE_WORLD_ANIMATIONS: validateWorldAnimations(); break;
    case TR2_VALIDATE_ARMS: validateArms(); break;
    case TR2_VALIDATE_INTERACTIONS: validateInteractions(); break;
    case TR2_VALIDATE_INIT_PLACEMENT: return initPlacement(op >> 8);
    case TR2_VALIDATE_EXTRA_OBJECTS: validateExtraObjects(); break;
    default: sys_error("Tomb Raider II: invalid validate op %lu", (unsigned long)op);
    }
    return 0;
}
