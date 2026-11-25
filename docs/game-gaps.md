# Gaps between the reference game and the original

`game/` equals the fixed-point reference (OpenLara `src/fixed` at 2333adef, plus the oracle's repairs) tick for
tick. The reference itself leaves things out, so equality with it does not mean the game can be completed or is as
hard as the original. This file lists what the fixed game leaves out or simplifies for the 15 gameplay levels and
the GYM (PC data read through `tools/cook/src_pc.py`; the PlayStation levels carry the same item types: no
PlayStation-only type occurs in the 16 levels' item lists. `[NOT CHECKED]` marks a statement not verified against
the PlayStation files), what the original does (first from OpenLara's floating-point game, `src/*.h` outside
`src/fixed`, commit 2333adef; reconciled with `docs/specs/wall/drawbridge.md` and `objects-missing.md`; the other
specifications had not landed when this was written), and what it costs the player.

The split-screen extension applies the fixed game's repaired rules to both actors. Player-facing
differences from the floating-point reference are in [coop.md](coop.md); the source inventory and
primary-only exceptions (saves, statistics ownership, heavy triggers and cutscene levels) are in
[specs/coop.md](specs/coop.md).

Co-op implementation accounting for [the inventory](specs/coop.md#2-port-inventory):

| Site family | Treatment in the port and fixed-game oracle |
|---|---|
| Lara's constructor, clock, action masks, collision scratch, cameras, flow and hazards | Shared world runs once; actor 0 then actor 1 run if present. Camera, input/edges, effects on the actor, flow and surviving collision offset belong to their respective actors. Reused floor, target and matrix temporaries are not an alternate camera. |
| Inventory, pickups, key/puzzle holes, switches, blocks and Midas | Reservations prevent duplicate spending or collection. Leave/warp refuse after commit until the interaction settles. Only death retires an actor; its own sector and camera still own the authored linked action. The resident tick applies an owed Scion completion after paging has returned, at its animation event and with the ring closed. The death page waits; an authored level end discards pending work. |
| Enemy AI, encounters, contact and projectiles | Nearest living target, rather than the last collision actor. Pending hit masks are actor-indexed and selected for the current target. AI eviction uses the distance from the nearer of both cameras; Pierre stays visible through either. The doppelganger instead caches its wake target and reflects to the shooter. |
| Traps and progression | Per-actor collision and damage use the fixed rules. Flame and an active lightning strike select one living actor; lightning chooses the closest exposed actor. Slam latches the first bite; a projectile or lava particle consumed on contact cannot damage a second actor. A non-direct centaur blast checks both. Scion/Midas in-level cameras are independent; the Scion target shakes both. |
| World and sound | Room/box/flip overlays, fixed-camera one-shot flags, music, counters and level end remain shared. Heavy commands use Lara 1's camera. Flood follows the actor and is cleared on leave; the fixed sand effect is still anchored to Lara 1. One positional sound event uses the closer camera. |
| Legacy boundaries | Authored Lara, carry, true cutscene camera, V3, schemas 2–4 and statistics still use slot 0. No dynamic actor or owed action is serialized. The V3 puzzle check uses the original ACTIVE rule. `game_save_blocked()` diagnoses committed Lara 2 work or a pending dead actor separately; the passport shows “Cannot save now” without touching the save slot. |
| Engine, renderer and platform | Co-op pad polling, controller-indexed feedback, preview/split rendering and the persistent session are outside the game slice. |

Owed key/puzzle/Scion timers end at the authored animation command (bounded search: 4,095
simulation ticks). Pickups settle on the next simulation tick if still owed; the Midas exchange
has no deferred world write. A dead owner's switch or moving block releases the lock at its
ordinary ready/landed state or after 4,096 simulation ticks. If a switch never reaches its
ready state, the normal trigger rejects it rather than inventing an activation.

For the open set-piece rows: a doppelganger hit without a shooter falls back to its cached target (or
nearest if not awake); death debris stays visual-only; rolling blocks still do not fire a heavy command
on landing; flip-effect bounce and sand origin remain the fixed game's rules. None changes the solo
reference. The one shared cutscene-level timeline is separate from actor-local in-level cinematics.

Classes: **(a)** blocks completing a level; **(b)** changes gameplay materially (a hazard that does nothing,
different damage or timing); **(c)** cosmetic. "Port" is an estimate of lines of C99 in `game/` plus the
oracle hunk (about the same size in C++). Status: REPAIRED = oracle hunk, port change and directed script committed and run
(oracle equal to port tick for tick, with a negative control); the hunks are numbered in `test/oracle/README.md`.
Evidence classes: **play-reached** = a placement and inputs only; **placed-state** = a test-only directive (`give`, `item`,
`activate`; host test driver only, identical in oracle and port, listed in `test/oracle/README.md`); **by construction** = no run.

## 1. Summary

| Rank | Gap | Levels, items | Hunk | Proof script | Evidence class | Status |
|---|---|---|---|---|---|---|
| (a) | Drawbridge never lowers (no controller) and its floor rule looked at the wrong sectors | LEVEL8B items 19-22 | 17, 18 | `drawbridge.py` | play-reached: each switch lowers its bridge, a second press raises it, Lara crosses bridge 22 and takes artifact 15 | REPAIRED; door 68 is not opened by play (1b) |
| (a), probable | Cabin never falls: the map flip that follows the three fuses never happens | LEVEL10A item 84 | 19 | `cabin.py` | placed-state for the three fuses (`give 110 3`), the rest play-reached | REPAIRED; that the flipped rooms are needed is `[INFERENCE]`, see 2.2 |
| (b) | A collected pickup with a pickup trigger made the validator refuse every later door and flip, and the source's load linked it into its room again | LEVEL3A 33, 40, 53; 3B 20; 4 71; 8C 10, 54 | 14, 20 | `pickups.py cases relink` | play-reached (collection, save, load) | REPAIRED |
| (b) | Lava floor (floor-data function 5) does nothing: `room.h:934 // TODO lava` | LEVEL10A 225, 10B 386, 10C 200 sectors | 21 | `lava.py` | play-reached | REPAIRED |
| (b) | Lava emitters never spawn lava particles (item 176 is not constructed): the fountains are harmless | LEVEL10A 21-23, 34, 44, 45; 10B 12-14, 43, 88, 89, 122; 10C 10-12, 22, 35, 36, 44, 53, 76-79, 117 | 22 | `burn.py particles` | play-reached | REPAIRED |
| (b) | Flames: Lara burns 90 ticks at most; the original burns until she reaches water or dies | LEVEL6 flames 35-37, 41, 42; LEVEL10C 67-69, 72-75 (and lava burn) | 23 | `burn.py burn` | play-reached | REPAIRED |
| (b) | Falling ceiling hits for 1000 (kills at once); the original takes 300 per frame of contact | LEVEL3B 24-26, 38, 39, 68 | 24 (and 28) | `burn.py ceiling` | placed-state (`activate`): no play reaches a slab while it falls | REPAIRED |
| (b) | An item that starts invisible and is activated by a trigger stayed invisible: not drawn, skipped by the collision loop (so the slabs above never hurt, blocks 19 and 20 did not exist, the boat and two pickups never appeared) | the 11 non-enemy items with a trigger in the PC levels (the same 11 in the PlayStation levels): LEVEL3B 24, 25, 26, 38, 39, 68; LEVEL4 19, 20; LEVEL10A 2, 42, 82 | 28 | `invisible.py scan run` | placed-state (`activate`) | REPAIRED (7.13) |
| (b) | Rolling block never exists: its sectors are never solid, it never slides | LEVEL3B items 49, 50 | 25 | `movers.py block` | play-reached (the switch pressed by play) | REPAIRED |
| (b)/(c) | Moving bar (object 47) is inert and not solid | LEVEL4 75-78, LEVEL10A 12 | 26 | `movers.py bar` | LEVEL10A play-reached; LEVEL4's four keys are placed-state (`give`), put into the keyholes by play | REPAIRED |
| (b) | Thor's hammer never hurts Lara: its strike point lies behind it (in rock) and the 30-frame delay is never true | LEVEL4 63 | 27 | `strikes.py thor` | play-reached | REPAIRED |
| (b) | LEVEL10C lightning: the previous round said it cannot reach Lara (corrected in 7.14) | LEVEL10C 107 | none needed | `strikes.py lightning` | standing: play-reached; in a jump: placed-state (`activate`) | both rules can reach her, only in the air (1b.2) |
| (b) | Lara surfaced while still inside a water room: up to 255 units early (a teleport to the water level), under a rock ceiling (inside the rock) and with her swimming pitch and roll kept while she trod water, so she was drawn leaning back on the surface | every level with water (15 of the 16; LEVEL1 has none): GYM, LEVEL2 to LEVEL10C | 29 | `surface.py` | play-reached (placement and swim inputs) | REPAIRED (7.15) |
| (c) | Earthquake item (camera shake + sounds, consumes two RNG draws a tick in the original) | LEVEL10C 108 | not done | | | remains, 1b |
| (c) | Boat (animation plays once, then removes itself) | LEVEL10A 2 | not done (hunk 28 makes it appear) | | | remains, 1b |
| (c) | Animation effect 1 (floor shake) ignored | REX 3A, TRAP_CEILING 3B/10C, HAMMER_HANDLE 4, CABIN 10A, ADAM 10C | not done | | | remains, 1b |
| (c) | Block landing shake and sound (`object.h:1542 TODO EarthQuake + playSound 70`) | LEVEL10A pushable blocks | not done | | | remains, 1b |

Everything else a level needs is present. Trigger actions 11 (clear bodies), 12 (flyby) and 13 (cutscene) do not
occur in any of the 16 levels (the fixed game's no-ops cost the player nothing in TR1).

## 1b. What remains and why

1. **LEVEL8B door 68 (class (a)) is not opened by play and is not shown.** The four artifacts (15 to 18) sit on four decks, each
   lowered by its own switch (items 35, 45, 61, 69 in rooms 36, 41, 49, 57, on three floors). Each bridge is shown (all four lower
   and raise, `drawbridge.py`) and one artifact is shown (15, collected by play after crossing bridge 22, the validator verdict
   staying 1). The door needs masks 1, 2, 4 and 24 together. `drawbridge.py reach`: with any drop allowed and the decks lowered, the walkable
   regions are switch 35 (6 cells), switches 45 and 61 together (99 cells) and switch 69 (65 cells, reaching artifact 15 only); the near
   ends of decks 19, 20 and 21 and artifacts 16 to 18 are in no region. No single scripted walk chains the four legs. The tick limit is
   not the obstacle (LEVEL8B is set to 4096 ticks); the missing capability is moving Lara between the pit's levels (jumps, climbs). The
   alternative, the other three artifacts as already-collected state, needs a directive that unlinks a pickup, sets it inactive and
   ORs the door's mask; none of the directives (`start`, `give`, `item`, `activate`) says that, and a new one would be new machinery,
   so none was added. That door 68 then opens is `[INFERENCE]` from the trigger code that the other pickup doors run (`pickups.py`: LEVEL3A,
   3B, 4, 8C).
2. **LEVEL10C lightning 107.** The source's rule is a sphere: for an emitter with a single mesh it strikes Lara when the squared distance
   from her origin (her feet) to the emitter is under 1024 squared (`hazard_thor_lightning.h`, `EMPTY_RADIUS`). The emitter hangs
   1,792 above the room's floor, and 1,280 above the floor (-19,200) of the cell 512 west of it, which is the highest cell Lara stood on
   under it in the probes. Standing, she is out of range of the source's rule and of the specification's near test (`glossary.md`,
   "Near test": horizontal distance at most D, and the emitter's height between the top of her bounding box and the bottom of the box plus
   100): the top of a standing Lara's box is far below it. In a jump the near test can hold: a straight jump from that cell rises 954,
   and the top of her box (-873 in the animation frame's box) passes 25 above the emitter at the top of the jump, for the ticks 39 and 40
   of the jump. So the original's rule can reach her, but only at the top of a jump and only when the bolt switches on in those two ticks;
   the source's sphere reaches her for a longer part of the jump (while her origin is within 1024 of the emitter), a wider window than
   the specification's. That difference is left as it is (class (c): one bolt of 400 that needs a jump timed to the bolt, on no route).
   Shown: `strikes.py lightning`, `lightning-10c-jump`: oracle equal to port, one strike of 400 at tick 125 for a jump started at tick 91;
   standing (`lightning-10c-south`) no damage.
3. **Small numeric differences left on purpose** (6b): Pierre's `RETREAT_FRAMES` 12 against 10; drowning starts one frame earlier;
   the gym ending counter 90 against 120; the track-number rules of the music; the spike walk-speed and fall-kill rules and the T-rex
   bite (1000 against 10000, the same outcome at full health) were not compared further; the secrets total. None changes what a
   level needs.
4. **Cosmetic, not done:** the boat (LEVEL10A 2: it is drawn after hunk 28; its movement from the animation is not modelled), the
   earthquake (LEVEL10C 108: camera shake, sounds and two random draws a tick in the original), the floor shake of animation
   effect 1, and the block landing shake and sound. Each is a camera or sound effect; the earthquake also consumes two random draws a
   tick, so it would change the random stream of LEVEL10C after it fires.
5. **The ten flame effects on Lara's joints are not modelled.** The fixed game has one burning state (a flag and the 5-a-tick
   damage); the original spawns ten separate flame effects on her joints. Damage, duration and extinguishing are repaired (hunk 23),
   the drawing of the flames is not.

## 2. Item types

Counts are `levels: number of items` from the PC item lists (GYM has none but Lara). "Fixed" says what
`ItemObj::init` builds: a class with `update`/`collide`, a floor/ceiling rule in `getItemFloorCeiling`, or nothing
(plain `ItemObj`: all virtuals empty).

### 2.1 Present in the fixed game and not listed in the summary (controller, collision, floor where needed)

Enemies: wolf (L1, 2, 3A, 3B), bear (1, 2), bat (1, 2, 4, 5, 6), crocodile land/water (4-8A, 8B water), lion, lioness,
puma (4-8B), gorilla (4-7B), rat land/water (6, 7A, 7B), T-rex (3A), raptor (3A, 3B), mutants 1-3 (8A-10C), centaur
(8C, 10B), mummy (3B), Larson (3B, 8C), Pierre (4, 5, 7A, 7B), skater, cowboy, Mr T (10A), Natla (10C), Adam (10C),
doppelganger (10B). Each has an `update` with its AI. The skateboard (type 29) is not an item of its own in the
level data: it is drawn with the skate kid (`objects-missing.md` section 20), nothing to implement.
Traps: trap floor (falling block), swinging blade, spikes, boulder, dart emitter and dart, slam (teeth), Damocles
sword (4), Thor hammer handle and block (4), lightning (4, 10C), falling ceiling, flame emitter, flame, lava wedge,
lava emitter (shell only, see 2.2). Doors 1-8, trapdoors 1 and 2, switches, underwater switches, pushable blocks 1-4,
cogs (3A), puzzle/key holes, pickups, Midas hand (6), Scion pickups, target, holder (10B, 10C), crystals, view
targets, waterfalls, centaur statue (7B), mutant eggs (10B, 10C), muzzle flash, bullets, bubbles, splash, blood,
smoke, ricochet, sparkles, explosions. Bridges 68-70 (1, 3A, 8A) have no controller by design: the floor rule in
`getItemFloorCeiling` matches `objects-missing.md` section 1 (flat: `Y`; tilted: `Y + offset/4` or `/2`).
TRAP_DOOR_LOD (67) occurs in no level.

### 2.2 Gaps


The "Fixed game" column is the reference source as it was before the repair of that row (the repairs are in sections 1 and 7);
"If missing" is what the player would lose without it.

| Item (type) | Levels: indices | Fixed game | Original | If missing | Class | Port |
|---|---|---|---|---|---|---|
| Drawbridge (41) | 8B: 19, 20, 21, 22 | before hunks 17/18: plain `ItemObj`; floor rule `getDrawBridgeFloor` took item minus query (sectors behind the item) and tested `dz` for the 90/270 degree second sector | animation-driven item (OpenLara `objects.h:842`, `drawbridge.md`): goal 1 while triggered else 0, floor `Y` / ceiling `Y+256` on the two sectors in front while state 1; solid while state 0 | the deck never exists: the four artifacts (15-18, which open door 68, masks 1, 2, 4, 24) are unreachable, the level cannot end | (a) | 40, done |
| Cabin (162) | 10A: 84 | plain `ItemObj` | `objects.h:1463`, `objects-missing.md` 18: solid; each completed trigger advances one stage (3 stages: animations 304, 305, 306); in state 4 flip slot 3 is completed, the map flips, the item goes. Triggers: the three fuse holes 26, 27, 28 (room 30), each a key trigger with mask 31 | after three fuses nothing happens: the map never flips, the cabin never falls. The three pad trigger pairs that also flip slot 3 (rooms 28, 39, 42-44) only toggle; whether the level can then be ended is `[INFERENCE]`: the alternate rooms are 3-16, 4-83, 29-30, 45-90, 50-84, 87-88, 91-96, 92-98, 93-97, 94-95; the cabin's own room 94 has the alternate 95 | (a), probable | 35, done |
| Rolling block (52, named MOVING_BLOCK in the reference) | 3B: 49, 50 (switches 74 and 76 via triggers in rooms 43 and 53) | plain `ItemObj` | `objects.h:613`, `objects-missing.md` 3: its sector is solid (floor raised by 2048, a wall if that reaches the ceiling) from the start, a trigger sets goal 1 and opens the sector while it slides along its facing, at the end of the slide it closes the sector where it stopped and snaps to the sector centre | both blocks are absent: the doorways they close are open from tick 0 (easier), the cell where they stop is never solid, no heavy trigger ever fires from their arrival | (b), cannot block | 90 incl. world-state (flip, save) |
| Moving bar (47, MOVING_OBJECT) | 4: 75, 76, 77, 78 (heights 23296 .. 21760; the four keyhole triggers in room 42, key items 80-83); 10A: 12 (switch 5) | plain `ItemObj`; no collision | `objects.h:1406`, `objects-missing.md` 6: goal state 1 while triggered, 0 otherwise, moves with the animation's speed along its facing; solid (pushes Lara out of its box) | the four stacked bars that the four keys retract, and the 10A bar, are not solid and never move: Lara can walk through them (easier) | (b)/(c), cannot block | 25 |
| Boat (182) | 10A: 2 | plain `ItemObj` | `objects.h:1493`, `objects-missing.md` 19: states 1, 2, 3 in turn then removed; movement from its animation | not drawn moving; it is scenery | (c) | 15 |
| Earthquake (183) | 10C: 108 (pad triggers in rooms 23, 33, 50) | plain `ItemObj` | `objects.h:1579`, `objects-missing.md` 17: while triggered each frame camera bounce -150 with sound (1/128) or +50 with another sound (1/32) using the draw and logic RNGs | no shake, no sounds; the two RNG streams are not consumed (a trace-visible difference once built) | (c) | 15 |
| Lava emitter (177) / particle (176) | 10A: 21, 22, 23, 34, 44, 45; 10B: 12, 13, 14, 43, 88, 89, 122; 10C: 10, 11, 12, 22, 35, 36, 44, 53, 76, 77, 78, 79, 117 | the emitter is an empty `Object`; item 176 is never constructed | `objects-missing.md` 16: one particle per frame, random heading, speed 0-31, thrown up; Lara within 200 loses 10 per particle; each frame one sound | the fountains are harmless and silent | (b) | 60 |
| Lava floor (floor function 5) | 10A: 225 sectors, 10B: 386, 10C: 200 | `Sector::getTriggerFloorCeiling` and `checkTrigger` skip the entry (`room.h:196-200`, `932-940 // TODO lava`) | `objects-missing.md` 15.3: when Lara's origin is exactly on that sector's floor she is killed (health -1) and ignited | Lara can walk over lava | (b) | 30 |
| Flames and Lara on fire (178) | 6: 35-37, 41, 42; 10C: 67-69, 72-75; lava wedge, lava | flame emitter and flame exist (`hazard_flame.h`); `BURN_DAMAGE` 5 per tick, `BURN_DURATION_TICKS` 90 | `objects-missing.md` 15.2: heat 3 per frame within 600; burning 5 per frame, no timeout: until water or death | Lara's burning ends 90 ticks after ignition (at most 450 of 1000 health) instead of killing her | (b) | 5 |
| Falling ceiling (53, 54) | 3B: 24, 25, 26, 38, 39, 68 | `DAMAGE` 1000 per contact | `objects-missing.md` 7.2: 300 per frame while falling and touching | kills at once instead of in 1 to 4 frames (full health) | (b) | 3 |

Other types in the PHD item list (`GLYPHS`, `LARA_BRAID`, `LAVA_PARTICLE` as item, `FLAME` as item, skateboard): not
placed by any level.

## 3. Trigger types and actions

Counts of floor-data trigger entries with that type, over the 16 levels (type 7, combat, occurs in none beyond the
count shown as 0 here):

| Trigger type | Used | Fixed game |
|---|---|---|
| 0 trigger, 1 pad, 2 switch, 3 key, 4 pickup, 5 heavy, 6 antipad, 8 dummy | all but 7 | implemented (`game/trigger.c`); `TRIGGER_TYPE_COMBAT` is implemented but no level uses it |

| Action | Meaning | Levels | Fixed game | Player loses |
|---|---|---|---|---|
| 0 activate object | all | implemented | |
| 1 camera | all | implemented | |
| 2 flow (underwater current) | 3A, 4, 5, 6, 7A, 7B, 8C, 10A | implemented (`worldSetFlow`, `applyFlowCurrent`) | |
| 3 flip map, 4 flip on, 5 flip off | 3A-10B | implemented, with the world-state repairs (hunk 14) | |
| 6 look at | all | implemented | |
| 7 end level | 16 levels | implemented; three levels end through the animation effect (hunk 13) | |
| 8 soundtrack | all | implemented | |
| 9 effect | 3A (6), 6 (5, 7), 7A (6), 7B (6, 15), 8A (7, 8, 9), 8B (8), 10A (11), 10B (8, 16) | effects 5-9, 11, 15, 16 dispatched | |
| 10 secret | all but GYM | implemented (`gSaveGame.secrets`) | |
| 11 clear bodies, 12 flyby, 13 cutscene | none | no-ops | nothing |

Secrets per level in the data: 1: 3, 2: 3, 3A: 5, 3B: 3, 4: 4, 5: 3, 6: 3, 7A: 3 (numbers 0, 1, 3), 7B: 2, 8A: 3,
8B: 3, 8C: 1, 10A: 3, 10B: 3, 10C: 2. The fixed game sets bit `arg` and plays the secret jingle once per bit, and
the port mirrors the field (trace `save.secrets`). The statistics page (`game/stats_view.c`) is built from the
save header: not audited further here.

## 4. Floor-data functions

Types seen: 1 portal, 2 floor slope, 3 ceiling slope, 4 trigger (all 16 levels), 5 lava (10A, 10B, 10C). Bridges
and trapdoors are floors by the object rules in 2. Only function 5 is not implemented (2.2). No level has types 6
to 18.

## 5. Animation-command effects (`ANIM_CMD_EFFECT`)

PC data scan of every animation command list; the owner is the model whose animation range contains it.

| Effect | Meaning | Used by | Fixed game |
|---|---|---|---|
| 0 | rotate 180 | Lara (all levels) | implemented |
| 1 | floor shake | REX (3A), TRAP_CEILING (3B, 10C), HAMMER_HANDLE (4), CABIN (10A), ADAM (10C) | ignored (c) |
| 2 | Lara normal | Lara special (3A, 3B, 6) | implemented |
| 3 | bubbles | Lara (all) | implemented |
| 4 | finish level | LARSON (3B), Lara special (8C, 10B) | ignored by the reference; repaired by hunk 13 |
| 12 | hands free | Lara (all), Lara special | implemented |

Not used in any of the 16 levels: 5 earthquake, 6 flood, 7, 8 stairs to slope, 9 sand, 10, 11 explosion, 13 flip map,
14-17 gun effects as animation commands (the PC levels' flip effects of 5-9, 11, 15, 16 arrive as trigger action 9
instead, see 3), 18-32 (mesh swap, inventory, dynamic light, statue, boiler, assault course and the TR2 and TR3 ones).
Lara's own gun-draw effects are in her state machine.

## 6. Lara and enemies

| Item | Where | Effect |
|---|---|---|
| wall climbing | `lara.h:1601-1606` (`// TODO climb`), `c_climbCollide` commented out | the TR1 data carries no climb marker (floor functions 1 to 5 only); `[NOT CHECKED]` whether retail TR1 climbs by another rule; `lara-and-enemies.md` has not landed |
| long up-jump, side jump near walls | `lara.h:1907-1908` | `[NOT CHECKED]` |
| surface swim / wade exit | `lara.h:1811`, `2185` | water out below 128 only |
| pistols at start of LEVEL10A, shotgun on back | `lara.h:2809`, `2818` | 10A start loadout is set by `applyLevelTransition` (hunk 12) |
| enemy blocking by pushable blocks | `enemy.h:196, 230, 435` (`@TODO blocking`) | enemies' paths ignore a block that is moved into their way |
| enemy respawn, WILD flag, special flag | `enemy.h:673, 1193, 1213` | `[NOT CHECKED]` |
| mesh extra rotations in `getJoint` / `getSpheres` | `item.h:922, 991` | spheres of joints with extra rotations |
| door collision | `object.h:754` | doors collide through the sector change (`doorApplyCollision`), not the object: no difference in play |
| crystal collision | `object.h:850` | crystals are made invisible (`flags |= INVISIBLE // disable crystals for now`): the save crystal does not occur in the retail data |
| hair, footprints (TR2, TR3) | `item.h:419-420` | not TR1 |

## 6b. Implemented in the fixed game, different from the specification (class b unless stated)

Compared against `docs/specs/wall/{objects-missing,lara-and-enemies,triggers-and-effects}.md`. Rows marked REPAIRED were changed in
the oracle and the port by the hunk named; the others are not changed. "Fixed" is read from `src/fixed` (constants in `hazard_oracles/*.h`, `enemy.h`, `lara.h`).

| Subject | Fixed game | Specification | Notes |
|---|---|---|---|
| Falling ceiling (53/54) | 1000 per contact (kills from full health at once) | 300 per frame while falling and touching | REPAIRED, hunk 24 (health 1000, 700, 400, 100, dead over four ticks of contact; reached through hunk 28); `hazard_oracles/falling-ceiling.h` |
| Lara on fire | 5 per tick for `BURN_DURATION_TICKS` = 90, at most 450 | 5 per flame per frame until she is in water or dead (several flames add) | REPAIRED, hunk 23 (no timeout); the ten flame effects on her joints are not modelled (1b); `hazard_oracles/flame.h` |
| Flame heat | 3 per frame within 600 | 3 per frame within 600 | equal |
| Spikes | 15 | 15 (kills when falling onto them, nothing when slower than 30 on foot) | damage equal; the walk-speed and fall-kill rules were not compared |
| Teeth (slam) 400, pendulum 100, Damocles 100, dart 50, lightning 400, Thor kill, boulder 100 air / kill ground | same | same | equal |
| Tyrannosaurus bite | `DAMAGE_FATAL` 1000 (enemy.h:2116), body touch 1 walking / 10 running | bite 10000 (kills outright); body 1 / 10 | same outcome at maximum health 1000; differs only if health could exceed 1000 |
| Doppelganger | returns `10 *` the damage it takes (`DAMAGE_SCALE`) | 10 times | equal |
| Pierre | flees at 40 (`MIN_HEALTH`), `RETREAT_FRAMES` 12 | flees at 40, vanishes when out of the camera's sight for more than 10 frames | 12 against 10 `[the comparison of the condition itself is not done]` |
| Natla | `HALF_HEALTH` 200, `FAINT_FRAMES` 480, flying shot every 30, ground shot every 20 | 200, 480, 30, 20 | equal |
| Fall damage | dies above 154, else `(v - 140)^2 * 1000 / 196` | dies at 155 or more, else `1000 * e^2 / 196`, `e = v - 140` | equal |
| Drowning | oxygen counts down to 0, then 5 per frame (starting the frame oxygen is 0) | oxygen goes below 0 (stays at -1), then 5 per frame | the fixed game starts the loss one frame earlier (c) |
| Dart start | `getBlockOffset(angle, 412)`, y -512 | 412 behind the emitter's origin on its facing axis, y - 512 | equal `[sign convention not compared]` |
| Music, track 5 | played (`sndPlayTrack`) | ignored (OpenLara plays it as the Caves ambient) | the one level that triggers it is not identified here (c) |
| Music, gym ending (track 50 after its one-shot) | `++aggression > 90` ends the gym | counter reaches 120 | (c) |
| Music tracks 0, 1 and 64 or more | track 0 ignored; 1 and 64+ not ignored by `game_check_trigger` | all ignored | no level uses them `[NOT CHECKED]` |
| Flip effects 5-9, 11, 15, 16 | dispatched (`worldDispatchEffect`): timers of 5, 6, 7, 8, 9, 11, 15, 16 as in the specification's table, 10 not present | 0-16 | effect 10 (power up) occurs in no PC level |
| Secrets and statistics | secret bits, jingle once per bit; statistics page from the save header | `found` of `total` = distinct secret numbers in the floor data (LEVEL7A lists 0, 1, 3) | the total is not compared |
| Underwater to surface | fork: also surfaced inside a water room (see 7.15); now: only when her room is no longer a water room, pitch and roll 0, y at the surface + 1 | `lara-and-enemies.md` 1.3.1: the same, dive counter 11 | REPAIRED, hunk 29 |
| Water states: wading | `WATER_STATE_WADE` exists (`lara.h` updateWaterState: above to wade when the water over her feet is above 384 and the pool is not deeper than 730; wade to surface above 730; surface to wade; `c_checkWallUW`: swimming into a floor under 512 of water) with the wade state and animations; wade to surface, surface to wade and swimming into a shallow floor zero pitch and roll, above to wade and wade to above leave them (0 on both sides) | no wading state and no shallow-water state: a room is a water room or it is not (1.3.1) | **not changed; for Main to decide.** What the player sees in the reference: walking into a shallow pool (water 384 to 730 over her feet, `movement.py wadein` reaches it by play on LEVEL7B only) she keeps walking in the wade state and does not swim; in the original (as specified) a water room puts her in the underwater state and she swims `[INFERENCE from the 1.3.1 table: not run on the original]`. Removing it changes the traces of every script that enters shallow water and the transitions in `lara.h` that mention it. Orientation on every one of those transitions is already zero (7.15) |
| Pitch clamp underwater | pitch limited to +-85 degrees (`updateUnder`: `X_CLAMP(angle.x, ANGLE(-85), ANGLE(85))`), roll +-22 degrees | +-100 degrees for pitch (18200), roll +-22 degrees (1.3.1) | not changed; the effect is 15 degrees more nose-up range in the original (c) |
| Ceiling contact while swimming | `c_checkWallUW`: no pitch change when she touches a ceiling in a water room within 768 of the water level (the fork's "water lid" tweak, made for the branch hunk 29 removed); otherwise the original's 2 degrees a frame | 2 degrees a frame on floor or ceiling contact (1.3.1) | not changed; it only matters under a ceiling close to the surface; Main to decide whether to take it out with hunk 29 (c) |
| Sloped-floor height, positive slope | `Sector_getFloor` (`game/room.c`, from `room.h`): for a positive slope component the product with `(offset - 1023)` is shifted right by 2 (rounding toward minus infinity while negative) and then subtracted | not stated for the first game. The retail SECOND game (`docs/specs/wall/tr2-lara-slide-height-and-gaze.md`, established from its executable and reproduced from the text on two records) rounds the positive contribution the other way: 253 where this expression gives 254, so Lara stands one unit higher on such a slope there | **open, not changed** (found 2026-11-22 while comparing with the retail second game). The port equals its reference here, as it must; whether the retail first game rounds like the retail second is not established: a question for the wall side. If it does, the first game's Lara is one world unit (of 1,024 a sector) low on some positive slopes in the port and in OpenLara's fixed game alike (c) |

The fixed game and the specification also differ in what is not implemented: earthquake, boat, floor shake and block landing shake
(1b). The lava floor, lava particles, rolling block and moving bar are implemented by hunks 21, 22, 25 and 26.

## 6c. Drawbridge: where the specification and OpenLara's floating-point game differ

The committed drawbridge follows the specification on the three points Main named, because the fixed game's own
`getItemFloorCeiling` already does: floor, ceiling and solidity follow the item's current animation state (the
controller sets the goal from `isActive()` each frame; state 1 gives the floor and `y + 256` ceiling; solidity only
in state 0); the deck underside is `Y + 256` (OpenLara: `+ 64`); the floor has no tolerance of its own (OpenLara
adds 128 to the query height): `y <= Y` as the fixed game's rule has it.

## 6d. Do the hazards built by `hazardInitItem()` hurt Lara in the oracle?

`test/game/hazards.py ORACLE` (oracle only, placed starts, one instance per type; minimum health in 240 ticks from
1000). Hurt or killed means the controller's collision path runs; it is not a comparison with the port or a
playthrough.

| Hazard | Instance | Result |
|---|---|---|
| spikes | LEVEL3B 11, Lara on the item cell | killed |
| boulder | LEVEL3B 45, on its pad | killed |
| teeth (slam) | LEVEL7B 32, on its pad | 600 left (400 taken) |
| Damocles sword | LEVEL4 30, on the cell | killed |
| swinging blade | LEVEL10C 58, on its pad | killed |
| dart emitter | LEVEL1 5, on its pad | 250 left |
| flame emitter | LEVEL6 35, on its pad | 746 left |
| lava wedge | LEVEL10C 15, on its pad | killed |
| falling ceiling | LEVEL3B 24, 25, 26 (`burn.py ceiling`, `activate`) | 300 a tick of contact (hunk 24): health steps 300, 300, 300, 100 |
| lightning | LEVEL4 62 (pad run) | killed (other hazards of the route are not excluded) |
| Thor's hammer | LEVEL4 63 (`strikes.py thor`, on the landing pad) | killed at tick 83 (hunk 27); survives leaving the pad |
| lightning | LEVEL10C 107 (`strikes.py lightning`) | none standing; 400 once in a jump at the top (`lightning-10c-jump`, 7.14) |

## 7. Repairs so far

1. Hunks 17, 18 (drawbridge controller; floor rule): `test/game/drawbridge.py`. Each of the four switches lowers
   its bridge (animations 231 to 233); a second press raises it; the raised bridge pushes Lara; Lara placed at
   switch 69 pulls it, crosses bridge 22 and takes artifact 15. Not shown: door 68 opening. It needs all four
   artifacts, hence all four switches (rooms 36, 41, 49, 57 on three floors) in one scene of at most 600 ticks.
   What is shown: the world validator verdict stays 1 after the artifact's pickup trigger (the hunk 14 repair).
2. Hunk 19 (cabin): `test/game/cabin.py` inserts all three fuses by play (specification `objects-missing.md` section 18):
   Lara is placed at hole 26 with three given fuses (`give 110 3`), uses each from the ring and walks between the holes
   (hole 26 is accepted only because the source's int16 `abs` lets her placed yaw of exactly -32768 through its +-30 degree
   test; holes 27 and 28 take her standing on the item's +z side, facing +z, within 30 degrees, 327 units out). One script of
   1,016 ticks (LEVEL10A allows 4096), oracle and port equal tick for tick: the cabin goes through animations 303, 304, 305,
   306, 307 (states 0 to 4) at ticks 188, 551, 914, 949, the flip is applied at tick 950 (`save.flipped` 0 to 1, sector hash and
   flip hash change) and the cabin leaves the active list; the oracle without a cabin controller (m6-worldfix) differs at
   tick 188. What the flip opens, from the cooked data (`cabin.py rooms`): the portal graph loses exactly one pair of edges,
   room 30 to room 94 (the cabin shaft) and back, and gains none; the 98 rooms reachable from the level start are the same
   before and after, so the flip does not open a way to any new room. It changes room 30's six sectors over the shaft
   (cells x 3 to 4, z 4 to 6) from a hole (`roomBelow` 94) to floor at the room's own level, and the shaft room 94 to its
   alternate 95 (ceiling 256 lower, its rim sectors replaced). Room 30 is otherwise a pit room, so those six sectors become
   an island two sectors beyond the ledge; walking onto it needs a jump the driver does not make, so that is not shown by
   play. Whether LEVEL10A cannot be ended without the flip stays `[INFERENCE]` (no new room becomes reachable).
3. The spec says "state 0 raised, collision as a closed door". The fixed game's door collision (`Door::collide`) is
   a TODO; the drawbridge uses the push-out of `collidePush` as the specification states.
4. Hunk 20 (collected pickups after a save and load, class (b), found by `GameCov3`): the source's `ItemObj::load`
   linked every item into its room again, including a collected pickup with a pickup trigger under it (INACTIVE, unlinked),
   which could then be collected a second time (LEVEL3A 33, 40, 53; LEVEL3B 20; LEVEL4 71; LEVEL8C 10, 54). The load now
   leaves such a pickup unlinked, by the world validator's own condition. `test/game/pickups.py relink`: all seven
   cases equal between oracle and port, the pickup counter and inventory totals unchanged by a second ACTION after the
   load, the validator never refusing; the oracle without hunk 20 (`no-hunk-20` control) differs after the load in all seven (negative control).
   Not repaired and not shown by a run: LEVEL7A 46, 61, 67 and LEVEL8B 15 to 18 (no collecting placement; the same code
   path). No specification section is involved (a defect of the reference's save load, not original behaviour).
5. Door 68 of LEVEL8B (step 3): not opened by play and not shown. The artifacts 15 to 18 sit on four decks, each lowered by its
   own switch (items 35, 45, 61, 69 in rooms 36, 41, 49, 57). `drawbridge.py reach`: the driver's walking routes join only
   switch fronts 45 and 61 (24 cells); 35 and 69 reach no other, so one Lara cannot pull all four in one script. The tick
   limit is not the obstacle (LEVEL8B could be raised to 4096 like LEVEL10A); the missing capability is moving Lara between the
   pit's levels (jumps, climbs). The other way the brief allowed, the other three artifacts as already-collected state, needs a
   directive that unlinks a pickup, sets it INACTIVE and ORs the door's mask; the directives are `start`, `give TYPE COUNT`,
   `item INDEX HEALTH`, none of which says that, and a new one would be new machinery, so none was added. Shown: artifact 15
   collected by play (mask 1 of 31) and the world validator staying at 1 (`drawbridge.py cross-22`).
6. Hunk 21 (lava floor, `objects-missing.md` 15.3, `triggers-and-effects.md` 2.1, `lara-and-enemies.md` 1.4): `test/game/lava.py`.
   The cooked floor data holds 225, 386 and 200 sectors with a lava entry (LEVEL10A, 10B, 10C, as audited). In each level Lara
   placed on a non-lava neighbour runs onto a lava sector and her hit points become -1 (first dead tick 21, 31, 28); the control
   run onto another non-lava neighbour leaves her at 1,000; oracle and port equal tick for tick; on the oracle without hunk 21
   she survives the same run. Not modelled: the hit flag, and the ten separate flame effects on her joints (the fixed game has
   only a burning state, set and cleared at once as she is dead).
7. Hunk 22 (lava particles, `objects-missing.md` 16.1, 16.2): `burn.py particles`. Particles alive at once, most seen in a
   160-tick stand next to an emitter: LEVEL10A 29, 10B 25, 10C 57 (10C also shows Lara losing 10 hit points to a particle); oracle
   and port equal; the oracle without the hunk has none. No allocation: each particle is one slot of the 256-item pool
   (`ItemObj_add`) and an emitter makes none when it is full. Free slots at level start: LEVEL10A 155, 10B 64, 10C 127. A particle
   lives at most about 55 frames (thrown up at 163, fall speed +6 a frame), so each active emitter holds up to about 55 and
   the worst simultaneous count is bounded by the free slots, not by the emitters; 57 is one emitter's placement, not a bound
   over all of them. A full pool also starves the reference's other dynamic effects (blood, ricochets, darts).
8. Hunk 23 (burn without timeout, `objects-missing.md` 15.2): `burn.py burn`, LEVEL6 flame 35: Lara standing in the fire dies at
   tick 253; on the oracle with the 90-tick limit she dies at tick 319 (the heat of 3 a frame outlasts the burn). Oracle and port
   equal. Water extinguishes it by the existing surface/under test.
9. Hunk 24 (falling ceiling, 300 a frame, `objects-missing.md` section 7.2): `burn.py ceiling` with hunk 28 (7.13) making the triggered
   slab visible. LEVEL3B ceilings 24, 25, 26, each activated by the placed-state `activate INDEX 0` with Lara standing under it:
   her health falls in steps 300, 300, 300, 100 over four ticks of contact (ticks 28 to 31, 48 to 51, 31 to 34, dead on the last),
   oracle equal to port; the `no-hunk-24` control (`test/oracle/control.sh no-hunk-24`) takes 1000 at the first contact. Evidence
   class placed-state: no play reaches a slab while it falls (the Scion pickup holds Lara until about tick 230 and every slab has
   landed by tick 165; slabs 38 and 39 are in room 20 while Lara stands in its flip twin, room 46).
10. Hunk 25 (rolling block, LEVEL3B items 49, 50, `objects-missing.md` 3): `test/game/movers.py block`: both switches slide their block
    2048 units in 32 ticks (animation 244, 246, 245); the sector hash changes on the tick the slide begins and the tick it ends and
    nowhere else; pressing the switch twice returns the hash to its first value; Lara is stopped at the wall of the start cell and,
    after the slide, stands in the cell the block left and is stopped at the cell it arrived in (x = 68507); save and load after and
    during a slide, and the map flip that every LEVEL3B run takes at tick 1 (Lara starts on a flip pad), all equal between oracle and
    port. The same inputs on the previous oracle (m6-gaps2b) differ in all nine cases (negative control). No walk reaches a flip pad
    from the hall (BFS, 51 cells), so a flip after a slide is not shown. The wall branches of the raise rule never run (floor 34, ceiling 15).
11. Hunk 26 (moving bar, LEVEL4 items 75 to 78, LEVEL10A item 12, `objects-missing.md` 6): `movers.py bar`, oracle equal to port. LEVEL4:
    the four keys (`give`, placed-state) are put into keyholes 80 to 83 by play from the ring in one script of 1,882 ticks (`KEYS_4`,
    generated closed-loop on the oracle with `movers.py --regenerate keys-4`): each bar plays its animation (274, 276, 275, 36 frames) and
    Lara crosses the doorway; with all four closed she is stopped at the stack (x = 30892). LEVEL10A: the switch-5 bar slides from
    x = 52736 into room 10 (animations 235 to 239); run at from the east Lara is stopped at x = 53508. The bars are thin plates (70
    thick) across the doorway, so the doorway is crossed along x. Negative control: the oracle without hunk 26
    (`test/oracle/control.sh no-hunk-26`, because m6-gaps2b caps scenes at 600 ticks and the key script is 1,882): the bars never move and
    Lara walks through them (30,821 against 30,892; 49,336 against 53,508).
12. Hunk 27 (Thor's hammer, LEVEL4 item 63, `objects-missing.md` 12): the source struck 3072 toward +z for the 180-degree hammer (rock)
    and tested the level-wide `frameIndex <= 30`; now the pad cell (56, 39) is the strike point and the strike comes 31 ticks into the
    swing. `strikes.py thor`: standing on the pad she is killed at tick 83, running off it she survives, oracle equal to port; on the oracle
    without hunk 27 she survives the pad. LEVEL4 lightning 62 strikes for 400 near (health 1000, 600, 200, dead at tick 6) and not at 4,300.
13. Hunk 28 (items that start invisible, `triggers-and-effects.md` section 4, object action: an item that is not an enemy becomes
    status active when activated). Found while proving hunk 24: the source's trigger turned a status of 0 into active and left
    status invisible (level flag bit 8) alone, so a triggered slab was never drawn and Lara's collision loop skipped it. `worldTriggerActivate`
    (`room.h` in the oracle, `game/trigger.c` in the port) now makes an invisible, non-enemy item active; enemies keep their rule
    (`Enemy::update` takes a creature slot, then becomes active, and waits while none is free; the specification's). Scan
    (`invisible.py scan`, 16 PC and 16 PlayStation levels): 137 items start invisible, 22 are not enemies and all 22 have a trigger (11 in the PC
    levels, the same 11 in the PlayStation levels):

    | Item | Type | Level | What it does once visible |
    |---|---|---|---|
    | 24, 25, 26, 38, 39, 68 | falling ceiling | LEVEL3B | falls, 300 a tick of contact |
    | 19, 20 | pushable block | LEVEL4 | solid, pushable |
    | 2 | boat | LEVEL10A | drawn, boardable |
    | 42 | puzzle item 1 | LEVEL10A | pickup |
    | 82 | pistols | LEVEL10A | pickup |

    `invisible.py run`: each of the 11 is activated by the `activate` directive with Lara beside it (block 19 from 5,120 units east of it:
    no nearer cell gave a placement the scene accepts): oracle equal to port, status active, against status invisible on the `no-hunk-28`
    control. Effect on the existing traces is measured in `$TR64_WORK/evidence/m6/campaign-final/hunk28/` (summary in `test/oracle/README.md`).
14. LEVEL10C lightning (1b.2): `strikes.py lightning`: the emitter woken by `activate 107 0`, Lara on the cell 512 west of it jumping at
    tick 91: the bolt that switches on at tick 125 strikes her for 400 (oracle equal to port); standing she is not hurt. The earlier
    statement that the bolt cannot hurt her held for a standing Lara only.
15. The collision record across a load (found by `saves.py` in the closing campaign, repaired in `game/game.c`, not an oracle hunk): in
    the source the collision record `cinfo` is a global that `startLevel` never resets, so what the last tick left in it survives
    `gameLoad`, among it the pending `offset` that the next Lara update adds to her position. The port zeroed the whole game state on a
    load and so dropped it: LEVEL3A movement script `level3a-275-49eef15b65b7ffd9`, save at tick 53 and load at 54 (the tick Lara
    hits a wall in a jump), oracle `lara.pos[2]` 1125 against port 1121 from tick 55 on; saving at any other tick of that script was
    equal. `game_prepare_load` now carries `cinfo` across (the other globals the source keeps across `startLevel`, `tinfo`,
    `gLastFloorData`, `gLastFloorSlant`, `tmpBox` and `gCameraViewPos`, are written before they are read in the source's tick and are
    not carried). After: that script equal at save ticks 52 to 54, `saves.py` over the kept items and movement scripts (1,093 saves, 33
    refused on both sides) with no difference, `fixtures.py` LEVEL1 and GYM, schemas 3 and 4 (25 fixtures each), equal, the four
    cutscene actions (`cuts.py --schema 4`) equal. The oracle is the contract here; whether the original keeps a stale
    collision record across a load is not known.
16. Hunk 29 (the fork's early surfacing inside a water room, `lara-and-enemies.md` 1.3.1): reported from play, Lara surfaced at an angle. The
    fork (OpenLara 2f7f8869) added a branch to `updateWaterState` (UNDER) that put her on the surface while her room was still a water room
    (`waterDist < 256`, y moved to the surface, no reset of `angle.x`, `angle.z`); the original has no such case and the branch for the air
    room zeroes both. `test/oracle/README.md` hunk 29 has the measurements (the old oracle: pitch 15834 kept for 87 ticks of treading in
    the GYM swim fixture; 84 of 1,115 near-vertical ascents "surfaced" inside a rock ceiling). Chosen by measurement over adding the reset:
    removing the branch (the original rule) surfaces through the room crossing in 1,009 of 1,031 open cells in 14 levels and never under rock.
    Orientation on every water transition of `updateWaterState` against the specification: above to underwater, pitch -45 (swan dive, other)
    or -85 (fast dive), roll unchanged (equal); underwater to surface by the room crossing, 0 and 0 (equal after hunk 29; the branch
    removed was the only one without the reset); underwater to above water, 0 and 0 (equal); surface to above water, 0 and 0 (equal);
    surface to underwater by diving (`s_dive`), -45, roll unchanged and 0 on the surface (equal); climbing out (`c_checkWaterOut`), 0 and 0
    (equal); wading transitions (above to wade, wade to above, wade to surface, surface to wade, swimming into a shallow floor): not in the
    specification; wade to surface, surface to wade and swimming into a shallow floor zero both, above to wade and wade to above leave them (0 on both sides; the reference's own choice, 6b). `test/game/surface.py`: 198 surfacing
    scripts in 7 levels, oracle equal to port, pitch and roll 0 on every SURFACE tick, the `no-hunk-29` control keeps the pitch; also on a
    PlayStation-pack host (81 cases). Unchanged: when JUMP is held through the crossing she can dive again on the same tick (the
    reference's `s_dive` sink rule, `waterLevel - pos.y <= -100`; the old oracle does it too), and she stays under a ceiling that has no
    room above it.
