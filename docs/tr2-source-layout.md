# Tomb Raider II source layout

## Decision — 2026-11-22

1. Use `game/tr2/` only for sequel-specific implementations, with TR1's
   file naming and one translation unit per species. Leave shared engine
   and TR1 files in place, keep the `game_tr2_*` exports, and retain
   `game/tr2.h` as the rule and state boundary.
2. Keep the sequel navigation and creature kernels in `game/tr2/nav.c`
   and `game/tr2/enemy.c`. Sharing individual leaves is a separate
   decision, not part of this migration.
3. Cut over at `17b35105c3dfd8427e6c36ffa06f038bcef96a07`, after
   crows and the second play-cart candidate, but before interactions.
   Resolve the interactions merge once against this layout, then split
   its newly landed module mechanically by the mapping below.

## Inputs and scope

| Read-only input | Identity |
|---|---|
| `main`, planning worktree | `06d3088c421898c14e8f1e854c579a55f4e1ed50` (moves, wet camera and tiger/spider combat merged; fragment specification merged) |
| `m10-tr2-crows` | Ref at the same `06d3088`; read the **uncommitted** `git diff -- game` in `$TR64_WORK/wt/T2Combat`. Initial SHA-256 `be99a187a1dae032499671c9ebdddd9ca7768e6a0cac6aee7b88e74d175816de`; the later retained-sight hunk was read at `8e5e4d1555fa84ec8129735a4441af7331a81c225fd24f8a25d923c8b8ee0d68`. Treat as an inventory, not an accepted implementation. |
| Interactions candidate | `$TR64_WORK/evidence/m10/interact/interact-v4.patch`, base `1798ddcddc7c6bff918f0163dbf81b365cd028ba`, SHA-256 `257736e92be367706012e91647bda4703983f9bcc556c16d489f33b1da7364ea`. Read, not applied. |
| Play-cart candidate | `$TR64_WORK/evidence/m10/cart-play/cart-play.patch`, SHA-256 `84783148ecd2eaa7cf7ddcabd340fa782260c04f735bf35116b9604fae359cf6`, including its `docs/tr2-cart-play.md` hunk. Measurements in that document describe its stated pre-creature base, not the current play-cart work. |

Scope of the inventory: gameplay functions, private helpers and tables
that define these slices; format, tool and platform boundaries are listed
where they already own sequel code. Rows can group a set of symbols with
the same destination. None of the retail executable, wall-side working
files or disassembly is an input. Sources and traces follow
[PROVENANCE §2a](PROVENANCE.md#2a-the-wall).

## What the first game actually does

| Source family | Responsibility and naming |
|---|---|
| `enemy_<species>.c` | Each distinct controller owns its pose/state choices and species-specific timings, attacks, death and special behaviour. E.g. `enemy_bat.c` uses private `Bat_updateState` / `Bat_updateBase` and public `tr64_unit_enemy_bat`; `enemy_rex.c` and `enemy_raptor.c` are separate even though both belong to `enemy_dinosaurs`. Variants parametrized by one controller need not be separate species files. `enemy_board.c`, `enemy_encounter.c` and `encounter.c` are auxiliaries, not a second AI runtime. |
| `enemy.c`, `enemy.h`, `nav.c` | Resident policy and data: construct/activation, AI slots, target/mood, turn/navigation, common damage/contact; `Nav_init`, `Nav_search`, `Nav_getWaypoint` do route work. Species call `Enemy_updateBegin`/`Enemy_updateEnd` or smaller helpers; common data and declarations are not placed in a regional unit. `types.h` holds `Nav`, `Nav_Cell`, `ExtraInfoEnemy`, `TargetInfo`. |
| `unit_<group>.c`, `units.def`, `module.mk`, `dispatch.c` | Entries use `tr64_unit_<unit>(uint32)` with `GameItemCall`, by operation and actual item type. `unit_peru.c` delegates bat, bear and wolf within **the same unit**; some single-species entries are in the species file. `units.def` selects object-file sections: the wrapper and the actual controllers have distinct translation units but land in one paging group. `dispatch.c` maps item types to groups and loads required enemy groups after placement, keeps common objects/inventory/save available, and leaves shared constructors/activation resident where appropriate. No fake cross-unit direct call; `code_call` uses the registered entry under a level/frame lease. |
| `lara.c`, `lara_weapon.c`, `lara_health.c` | One state/locomotion implementation and dispatch; weapons own selection, aim/fire, draw and stow; health owns damage, burn and recovery. `lara_inventory.c`, `inventory_economy.c`, `inventory*.c` and `pickup_events.c` are actor transaction, accounting, presentation and event paths, not pickups' in-room controllers. |
| `object_*.c`, `hazard_*.c` | `object_factory`/`object_dispatch` bind types to operations and door updates call `game_door_action`; `object_interact` has pickup, switch and holes; `object_block` push blocks; `object_trap` trap families; `object_progression` Midas/Scion. Hazards use named families: `hazard_boulder`, `hazard_flame`, `hazard_contact`, `hazard_thor_lightning` and their dispatch. `world.c` owns live room/box/door/flip mutations; `trigger.c` executes floor programs. A controller invokes those owners rather than maintaining its own sector graph. |
| `effect.c`, `hit.c`, `pose.c`, `animation.c` | Effects including bullets, muzzle flashes and transient visuals; hit geometry and mesh spheres; pose transforms and packed animation/commands, respectively. Room lookup, math, item motion, view/trace and input stay in their existing families. |

## Namespace

Put **only edition-exclusive implementations** in `game/tr2/`, with the
same basenames as the root convention: `enemy_tiger.c`, `enemy_small_spider.c`,
`enemy_crow.c`, `enemy.c`, `nav.c`, `effect.c`, `lara_weapon.c`,
`object_interact.c`, `object_trap.c`, `hazard_boulder.c`, etc. Keep the
shared engine and the existing TR1 files in `game/` where they are. When
TR2's rat or T. rex is admitted, use `game/tr2/enemy_rat.c` and
`game/tr2/enemy_rex.c`; the identical basename is a domain classification,
not permission to use TR1 rules or raw IDs. A giant spider is not the
small spider controller. Preserve the named refusals until those species
are implemented; there are no empty controller stubs in this migration.

The directory is a source namespace, not a C/linker namespace. Keep
existing exported `game_tr2_*` names during the mechanical migration;
new page entries must be globally game-qualified (`tr64_unit_tr2_<group>`)
and new external species ops explicitly TR2-qualified. Private names can
stay `static`. Linker unit patterns must match the full `game/tr2/…`
object path, not `*enemy_rex.o`; do not change root object order, section
patterns, `#line`/assertion IDs, existing `__FILE__` signatures or TR1
source paths in the shipping link. A bare `#include "enemy.h"` is not a
licence to shadow the root header: use explicit root paths for shared
headers and an edition-qualified private header if needed. `game/tr2.h`
remains the rule/ID/state boundary, not a home for new function bodies.

### As built at the cutover

The landed play cart uses the `tr2_creatures` lease for `tr2/enemy.c`,
`tr2/nav.c` and all three species, with the existing
`unit_tr2_creatures.c` entry. `tr2/enemy_motion.c` is an intentional
exception to the first game's basenames: the Bored-spider diagnostic
link wraps `game_tr2_creature_stride` at the caller boundary. Keep
stride/contact in that separate object and the same unit; consider
folding it into `tr2/enemy.c` when that test seam is next changed.
`object_factory.c` keeps the resident type/slot lookups and crow's
placement RNG, which cannot acquire a paged species dependency.

`tr2_wall` keeps its original entry, with `tr2/object_helicopter.c` and
`tr2/hazard_flame.c` in the same unit. The campfire's range and controller
are in the latter, while `game_tr2_campfire_effect` stays with the effect
pool and its stable handles in resident `tr2/effect.c`. Door update on
this base remains resident in `tr2/object_dispatch.c`; the proposed
interactions cart migration is not part of the cutover. The factory
owns placed flags and defaults; `game.c` still owns initialization and
active-list iteration, including saving `nextActive` before dispatch.

## Move/split inventory: merged gameplay files

Targets below use the accepted namespace. “Sequel” means retain a
separate sequel body in this migration; “shared candidate” means a
*possible later* same-semantics helper after a dedicated equivalence
check. Even obvious shared primitives are not factored into a new
runtime algorithm in the mechanical change.

| Present file / functions and local data | Target and treatment |
|---|---|
| Resident `game.c`: `game_tr2_creature_type`, `game_tr2_creature_slot`; `tr2_creatures.c`: `creatureSector`, `itemBox`, `squared`, `cameraDistance`, `game_tr2_creature_wake`, `retireCreature` | Type and bound-slot lookups are resident in `tr2/object_factory.c`; the remaining functions are in `tr2/enemy.c` within `tr2_creatures`. Wake remains callable through the unit's existing operation. No new slot allocation strategy or paged function pointer. |
| `tr2_creatures.c`: `Interest`, mood enum, `interest`, `quadrant`, `attractive`, `viable`, `chooseMood`, `chooseDestination`, `strike`, `turnToward`, `game_tr2_update_creature` pre/postamble and current dispatch | `tr2/enemy.c`, with only the necessary private declarations shared by the controllers. Preserve `wake → target → mood → destination → turn → species → clear hit/injured → stride`, including the separate death paths. See core comparison below. `strike` is common to sequel species, not a TR1 `Enemy_bite` alias. Keep the current function/operation boundary across extraction. |
| `tr2_creatures.c`: tiger state enum, `tiger`, tiger branch of `game_tr2_update_creature` death (+11 and neutral joint), its pivot/turn/radius/health/step/drop values | `tr2/enemy_tiger.c` for the controller and death; immutable traits may be placed with the controller **only** if the chosen resident/paged boundary leaves required classification/defaults resident. `TIGER_DIE` and state numbers stay local. No polygon/model table changes. |
| `tr2_creatures.c`: `spider`, spider death branch of `game_tr2_update_creature`; `tr2_creature_motion.c`: spider rounded-height rejection and pose reset; `tr2_effects.c`: `game_tr2_spider_breakup` | `tr2/enemy_small_spider.c` for controller and death orchestration. Keep the height/pose correction at the same point in common stride; keep `game_tr2_spider_breakup` initially in `tr2/effect.c` with its private pool allocator/iteration, invoked by the species death handler. The shared shard tick is not a species callback. No duplicate allocation rules, RNG changes or stride implementations. |
| `tr2_creature_motion.c`: `sectorAt`, `sectorBox`, `floorOf`, `terrainFits`, `marginFits`, `radiusFor`, `corner`, `moveRoom`, `game_tr2_creature_stride`, `fly` | `tr2/enemy_motion.c`, in `tr2_creatures`. These are the common sequel creature kernel, not one implementation per species. Keep the live sector probes, tile rollback including its axis choice, radius and corner ties, turn kick, item-list ordering and ground/fly room sample. |
| `tr2_creature_motion.c`: `contact`, `game_tr2_creature_contacts` | `tr2/enemy_motion.c`. Until interactions lands, keep the existing pass and loop order. The packed-sphere kernel remains in `pose.c`/`hit.c`. |
| `tr2_creature_nav.c`: eight function bodies, `Corridor`, `ROUTE_BLOCKED`/`ROUTE_ID` | `tr2/nav.c` in `tr2_creatures`. The exhaustive function mapping and potential common pieces are below. |
| `tr2_arms.c`: pistol aim/draw/holster enum and `pistolClip`, `pistolCanDraw`, `setPistolPose`, `setPairedLook`, `clearPistolAim`, `drawPistols`, `holsterPistols`, `pistolOrigin`, `pistolAimAt`, `selectPistolTarget`, `updatePistolAim`, `game_tr2_pistol_ray_angles`, `firePistol`, `readyPistols`; `game_tr2_firearms_camera_requested`, `game_tr2_update_arms`, `game_tr2_validate_arms` | `tr2/lara_weapon.c` beside the root `lara_weapon.c`. Keep arm-handler ownership of Combat request and shot ordering. Common `game_weapon_aim`, `weaponAim`, `game_weapon_spread`, `Lara_weaponGetAimPoint` and the inventory/weapon selection owners stay in the root files. The arms validator belongs to the weapon asset bank; any transfer into the cold `load` lease is a placement decision, not a change to which clips it checks. |
| `tr2_arms.c`: flare pose/age enum, `flareClip`, `flareStock`, `setFlarePose`, `flareOverlay`, `flareLight`, `advanceHeldFlare`, `drawFlare`, `releaseFlare`, `finishFlareStow`, `stowFlare` | Held/throw state in `tr2/lara_weapon.c`, as above; leave the existing pose query in `pose.c`. Keep `flareStock`'s caller semantics when interactions switches it to `carriedCount`; do not install a parallel stock counter. |
| `tr2_arms.c`: `game_tr2_update_world_flare` | `tr2/object_flare.c`, the loose item's world update, not the held arms tick. **Resident on this cut**: it is spawned by Lara and cannot depend on a placed-item-only lease until the cook and lifetime cover that path. In-flight use of the private burn-age constant is a mechanical header/declaration choice, not a second rule. |
| `tr2_effects.c`: `effect`, `game_tr2_effects_init`, `allocate`, `retire`, `game_tr2_shot_effect`, `updateShard`, `game_tr2_run_effects` | `tr2/effect.c` resident: fixed pool, stable handles and shared lifetimes. Flame/splash/blood/ricochet/shard dispatch remains in the original iteration and allocation order. TR1 `effect.c` is the placement precedent, **not** a replacement: it uses `ItemObj` and different owner/lifetime rules. |
| `tr2_effects.c`: `game_tr2_campfire_effect`; `unit_tr2_wall.c`: `tr2CampfireInRange`, `updateTR2Helicopter` | `tr2/hazard_flame.c` for the paged campfire update and range; `tr2/object_helicopter.c` for the paged helicopter. The campfire still calls `game_tr2_campfire_effect` in resident `tr2/effect.c`. |
| `game.c`: `tr2Ticks`, `game_tr2_activate_item`, `tr2PlacementFlags`, `initTR2Placement`, `updateTR2Items` | `tr2/object_dispatch.c` has resident `game_tr2_item_ticks`, activation, the split-out per-item update (including the existing resident door branch) and the selection pass. `tr2/object_factory.c` owns flags and item placement. `game.c` retains top-level placement order, active walk/saved `nextActive`, dispatch phases and `stock[]`/level initialization. The pre-cutover cart's `code_call` boundaries are unchanged. |

### Core: what is a rule difference, not just a different location?

| Sequel core routine | TR1 peer and decision |
|---|---|
| `game_tr2_creature_slot`, `game_tr2_creature_wake`, `cameraDistance`, `retireCreature` | `Enemy_enable`, `Enemy_setExtra`, `Enemy_disable`, `Enemy_cameraDistance`: similar free-slot/strict farther-camera selection, but TR1 has eight slots, two-camera minimum in co-op and `extraE` ownership. Sequel has five slots, one raw camera position, separate `TR2Creature`/route arena and status/retirement effects. Keep the whole sequel path for the move. Possible shared candidate: only distance of one camera after auditing width, shift and overflow; don't merge slot logic based on resemblance. |
| `creatureSector`, `itemBox`, `interest` | `Enemy_updateTargetInfo` / `Enemy_checkZone`: TR1 reads sectors from current rooms, co-op targets and `fastLength`; TR2 resolves linked rooms, computes squared distance and body-relative signed bearing, accounts for pivot, target flags and blocked generation. Different rules; reuse existing room/bearing primitives, not the TR1 target updater. |
| `quadrant`, `attractive`, `viable` | `Enemy_checkStalk`, `Enemy_checkEscape`, `Enemy_checkZoneBox`: sign/quadrant and escape shapes overlap (shared candidates), but exact rectangle endpoints, stalk expansion by candidate dimensions, blocking and accepted zones differ. Keep small helpers local to the sequel policy. |
| `chooseMood`, `chooseDestination` | `Enemy_getMoodWild`/`Normal`, `Enemy_updateMood`, `Enemy_mood*`, `Enemy_getRandomBox`: same roles, but different mood numbering, source fields, route viability, conditional draws/short circuit and destination state. Crow's draft also changes pending Escape request handling. Keep separate; preserve PRNG ordering, do not “make consistent” with TR1. |
| `turnToward`, `strike`, update prologue/epilogue | `Enemy_updateNavigation`, `Enemy_bite`, `Enemy_updateBegin`/`End`: shared candidates are limited arithmetic/primitive calls; turn uses squared distance and explicit species caps, strike uses a hit-mask/latch and species damage, and lifecycle/pending contacts have different ordering. Keep sequel bodies; dispatch one species, then advance exactly once. |
| Motion `sectorAt`, `sectorBox`, `floorOf`, `moveRoom`, `corner`, `terrainFits`, `marginFits`, `radiusFor`, `game_tr2_creature_stride`, proposed `fly` | `Enemy_updateLocation`, `ItemObj_updateRoom`: use the same low-level room/box storage but sequel uses pose-top vs stored-floor samples, nav-box margins, diagonal resolution and old-coordinate rollback, different peer scan and amphibious/flying height. The TR1 floor-follow cap and no-op-or-remove/add room membership are possible common leaves; do not move code into one combined `Enemy_updateLocation` now. |
| Motion `contact`, `game_tr2_creature_contacts` | `Enemy_collide` and `Lara_updateObjectsCollision` share existing collision services. Different bounds/masks, push distances and reaction phase, including crow candidate inclusive contact and interactions' door contact order. Preserve pass composition in final merged baseline; any extraction of the common nearby-room iteration is separate from unifying controllers. |

### Navigation, function by function

Checked against all of `game/nav.c`, `Enemy_zoneTable`/`Enemy_boxOverlap`
in `enemy.h`, `Enemy_setNextBox` in `enemy.c`, and the sequel node/brain
records in `tr2.h`. `Nav_Cell` is not interchangeable with `TR2RouteNode`
just because both have a link, a queue and a weight. `Nav` keeps unsigned
sentinels and broader queue/control fields; the sequel uses signed
−1 indices, stored generation/blocked bits and additional trace-visible
box/required/destination/waypoint fields.

| `tr2_creature_nav.c` symbol | Closest TR1 code | Ownership after move; commonality decision |
|---|---|---|
| `game_tr2_creature_zones` | `Enemy_zoneTable`, `Enemy_getZones`, zone selection in `Nav_init` | `tr2/nav.c`. Same packed base-plus-stride access, different stride/layer policy: sequel uses five per flip and fly layer 4 or `(step >> 8)-1`; TR1 chooses `ZONE_GROUND_1`, `ZONE_GROUND_2` or `ZONE_FLY` and strides by `ZONE_MAX`. The address calculation could later share an inline reader taking an already-selected layer and stride; do not broaden TR1's zone schema. |
| `game_tr2_creature_nav_init` | `Nav_init` | `tr2/nav.c`. Both reset nodes then collect the union of normal and alternate matching zones, in index order. Sequel also sets live box, required/target/link/head/tail and generation, uses its own zone layer and leaves an empty list for a negative box. TR1 initializes mask/type and asserts a nonempty list. Same invariant only on the valid-box domain; a later *mechanical* extraction of the zone-match predicate is plausible, not a common initializer. |
| `floorOf` | Signed box floor reads inline in `Nav_search`, `Nav_getWaypoint`, `Enemy_setNextBox` | Identical read for a valid index. Keep file-local in `tr2/nav.c` (and the independent motion helper) for this change; an inline signed floor accessor is a safe candidate after checking bit-identical TR1 objects. Not worth an exported function or copying boxes. |
| `expand` (target insertion) | Initial queue work in `Nav_getWaypoint` | `tr2/nav.c`. Same front insertion of a new target in outline; current sequel generation explicitly wraps to 15 bits, stores parent link sentinel and writes `nextBox`/`boxSearch` snapshots. TR1 `weight++` and u16 cell weight have different rollover/blocked-bit behaviour. No common queue initializer in this move. |
| `expand` (traversal); `ROUTE_BLOCKED`/`ROUTE_ID` | `Nav_search`, five-step loop of `Nav_getWaypoint` | `tr2/nav.c`. Same reverse search skeleton, selected zone, height filter and 0x8000 blocked propagation; use intact native overlap arrays, not copied edges. Sequel recognizes `TR64_BOX_NO_OVERLAP` in the **authored** overlap word, takes blockage from live `boxFlags`, processes up to five nodes and can clear stale tail on encountering empty head; TR1 indexes via the live overlap word and its caller stops on empty head. Equal-generation/link rules and rollover/queue observable state must remain as written. Maybe later share an edge decoder or per-edge eligibility predicate with explicit policy; not `Nav_search` reuse now. |
| `bounds`, `Corridor` | Tile bounds in `Nav_getWaypoint` | `tr2/nav.c`. Sequel uses Z then X and cooked max minus one; TR1 reconstructs whole-tile bounds (`max+1` rounded by tile then minus one) and tests X before Z. Reusing a physical-box descriptor may be safe for validated alignment; replacing either read or changing axis order is not a mechanical extraction. |
| `inset` | Repeated +/-512 clamps in `Nav_getWaypoint` | `tr2/nav.c`. Common candidate on the same inclusive bounds; preserve **ordered** low test then high test, even if a corridor is narrow. Not a reason to share waypoint selection. |
| `game_tr2_creature_box_destination` | `Enemy_setNextBox` | `tr2/nav.c`. Sequel draws **Z then X**, scales before multiplying, applies the -384 offset for flyers and writes `requiredBox` and three persistent destination coordinates. TR1 draws **X then Z**, multiplies before shifting and uses -384 for non-fly, writing `nextBox`/`pos`. Keep exact draw order, arithmetic and signed endpoints; no shared random-destination body. |
| `game_tr2_creature_waypoint` | `Nav_getWaypoint` after its search | `tr2/nav.c`. Same corridor idea, different Z-first clipping/corner ties, accumulated height incl. fly -1024, stale-link guard and final waypoint retention. TR2 can draw on still-open axes on exhaustion and keeps deterministic RNG order; TR1 clamps the fallback and does not draw there. They cannot share by just replacing four bounds. Preserve early outcomes and do not add RNG calls to TR1. |

Decision: do not put a second complete `Nav_*` implementation under
unqualified TR1 symbols, but also do not combine these kernels now. Each
is a resident policy over the common cooked world; `TR64_RULE` belongs
at actual shared entry sites, not as a predicate per edge in a hybrid
search. The later equivalence targets are the small leaves above, not a
migration requirement. Preserve schema-21 brain fields explicitly; a
route-only position match would not catch a stale queue or extra draw.

## In-place inventory: shared owners and tables

The table covers phase-specific helpers in shared files, including helpers
without `tr2` in the name. The target is the same path unless noted. These
are not files to move just to reduce the `TR64_RULE` count.

| Present → target | Functions/tables and boundary |
|---|---|
| `game/lara.c` → same | Keep all common `Lara_*` state handlers and `tr2RecoverGroundStop`, `tr2RunStepUp`, `tr2RunSplat`; `tr2Cardinal`, `tr2NearCardinal`, `tr2ClimbHand`, `tr2HandAgreement`, `tr2ClimbPair`, `tr2GroundClimb`, `tr2AirCatch`, `tr2LadderFloor`, `tr2LadderReleased`, `tr2LadderSideRequest`, `tr2LadderStance`, `tr2LadderVertical`, `tr2LadderEdge`, `tr2LadderSide`, `tr2HangClear`, `tr2HangGrip`, `tr2HangPullUp` with the state/height enums and `ClimbHand`; `tr2SlideDirection`, `tr2Wading`, `tr2LookTurn`, `tr2RunJumpArmed`, `tr2StandingTurn`, `tr2LookYawDenied`, `tr2SurfaceControls`, `tr2RoomWet`, `tr2WaterFloor`, `game_tr2_water_status`, `game_tr2_water_air`, `game_tr2_water_move`, `tr2CollideSurface`, `tr2CollideUnder`. The Lara control/collision dispatch tables remain in `dispatch.c`. `lara_health.c` is the health policy owner; drowning and movement contact keep their current call order. |
| `game/game.c` → same | `game_tr2_start`, `game_tr2_place`, `game_tr2_tick`, `game_tr2_report`; sequel allocation/route arena, new-game seeds and ordering in `levelStart`/`simulatePlayer`; ordinary RNG functions and engine lifecycle. Session dispatch is not a species module. Assembly/cart-specific binding remains with the play-cart patch, not a new host-only referee. |
| `game/animation.c` → same | `TR2WorldAnimCheck`, `tr2QueueWorldAnim`, `game_tr2_validate_world_animations`; `AnimationCommands`, `validateLaraLink`, `validateAnimationCommands` and local `arguments[]`, `game_tr2_validate_lara_clip`, `validateCreatureLink`, `game_tr2_validate_creature_assets` and `species[]`; `tr2LaraEffect`, `refusedKey`, `packedClip`, `packedKey`, `game_tr2_packed_frames`; branches of `getFrames`, `ItemObj_getBoundingBox`, `ItemObj_animCmd`. Keep folded TR1 calls/ordering and known physical far-key spans; added crow ID in `species[]` stays at validation. |
| `game/pose.c` → same; `game/hit.c` → same | `packedRotationWidth`, `packedAngles`, `tr2FrameRotate`, `skipPackedRotations`, `game_tr2_validate_packed_pose`, `game_tr2_validate_lara_pose`, `ItemObj_getJoint`; `flareBoneTranslate`, `flareTorso`, `blendFlareTorso`, `game_tr2_flare_hand`; `combatRotatePair`, `combatRotate`, `PackedSphereCursor`, `beginSpheres`, `nextCombatJoint`, `game_tr2_joint_origins`, `nextSphere`, `worldSpheres`, `game_tr2_creature_touch`, `game_tr2_sphere_entry`, `game_tr2_ray_hit`. The last set is a sequel collision/pose pipeline in its existing owner, not part of the species split. Keep interactions' separate contact sphere caller as merged; shared packed decode does not make creature/door hit policy equivalent. |
| `game/camera.c`, `camera_tick.c` → same | `game_tr2_camera_subject`, `cameraAxis`, `cameraComponent`, `cameraSquare`, `cameraRange`, `cameraOutside`, `cameraSweep`, `cameraSegment`, `game_tr2_sight`, `cameraBox`, `cameraHeight`, `cameraClearPoint`, `game_tr2_camera_request`, `cameraCos`, `cameraProduct`, `cameraNominal`, `cameraBoxContains`, `cameraFollowCandidate`, `cameraClipSegment`, `cameraLookCandidate`, `cameraCandidate`, `cameraOrbit`, `cameraEase`, `cameraMoveEye`, `cameraCombatAngles`, `cameraCombatWater`, `game_tr2_camera_move`, `game_tr2_camera_end`, `tr2CameraUpdate`; camera axis/range/result types. Preserve common sight consumers and the resident camera's calls, not a second copy for firearms. |
| `game/item.c`, `math.c`, `constants.h`, `tables.c`, `room.c`, `world.c`, `collision.c` → same | `game_tr2_heading_step`, `tr2_sin` and `game_tr2_bearing`/`game_tr2_horizontal_length` stay with shared motion/math; the sequel folds angles before indexing `gSinCosTable` and shares `atanTable`/`atanOctant`, not another trig table. `verticalSector`, `game_tr2_room_sector`, `game_tr2_validate_water_links`, `game_tr2_water_height`, `game_tr2_camera_heights` and shared sector/floor/collision entry points stay with room geometry; `initialTR2World` and door, box, flip, pickup/link branches stay with live world state. `Lara_updateObjectsCollision` stays the owner of collision phase and order. |
| `game/trigger.c` → same | `tr2CameraSubject`, `tr2HasCameraOverride`, `tr2RenewView`, `tr2CameraTriggerQualifies`, `tr2CameraActions`, `tr2ItemAction`, `tr2ItemActions`, `TR2_TRIGGER_ANTITRIGGER` and `game_check_trigger`; replace only by the already-merged interaction parser if present at cutover. Floor action grammar/renewal reads a different extent from execution; not an `object_` table to copy. |
| `game/trace.c` → same | `arm20`, `item20`, `brain21`, `lara20`, `camera20`, `world20`, `inventory20`, `game_trace_tr2`, `game_trace_tr2_end`; `weapon20[]`, camera mode mapping, availability and producer strings, FNV order, saved stats and wire IDs. `plat/host/game-scene.c` retains `tr2_mask`, `tr2_place_line`, `tr2_output`, `tr2_scene`; `tools/tr2-compare.py`, `tools/tr2-producer-profiles.json`, `test/game/tr2-*` stay in test/tooling; update compile-source references only. No producer rename for a file move. |
| `game/tr2.h`, `internal.h`, `types.h`, `constants.h` → same shared boundaries | Input/ID enum and `tr2DoorType`, all `TR2_RULE*`, camera/weapon constants, `TR2Arms`, `TR2EffectKind`/`TR2Effect` and free/active/campfire arrays, `TR2RouteNode`/`TR2Creature` and five slots, `TR2RingEntry`, `GameTR2` and host declarations. Leave sizes, offsets and initialization unchanged in step 2. Move only new private non-ABI declarations needed by splits; retarget comments citing consumers. Do not turn host/session IDs into equivalent TR1 numbers. |
| `engine/res/validate.c` and generated cooker/native format tables → same | `tr2_action`, `tr2_renewal`, shared `actions`/`floors`/`load_validate_core`, topology (`Axis`, `Footprint`, `band`, `tile_at`, `horizontal_room`, `room_walk_band` including cycle table, `room_walk`, `room_walks`), `tr64_level_validate_tr2_gameplay` and their scratch helpers. Keep render/asset admission and format/sound/game-tag selection at their existing engine consumer boundaries. Generated floor masks belong to `tools/cook`/native admission, not to a new gameplay format copy. No engine/renderer file migration. |

## In-flight code: add the following rows only after it lands

**Crows.** `crow`, `crowDeath`, `CROW_*` → `tr2/enemy_crow.c`. The perch
placement (+14, yaw draw) currently inline in `game.c:initTR2Placement`
is a species-init operation; leave its call at the same point in the
placement loop, with the after-placement RNG reset still at `levelStart`.
The health, step/drop/fly, radius and turn caps join the same residency
rules as tiger/spider, not a second set of crowd/world loops. Crow wake
and retirement switch cases go with the `tr2/enemy.c` dispatch and crow
controller; crow `fly` is a shared motion path, not a separate file for
one currently flying species. Put the new `chooseDestination` Escape
precedence and inclusive `contact` change in the same sequel kernel as
those existing functions; keep fragment `updateShard`/splash changes in
`tr2/effect.c`. `species[]` in `animation.c` and host schema/controls stay
in their owners. The later `pistolAimAt` signature/retention edit and
its `selectPistolTarget`/`updatePistolAim` callers still go to
`tr2/lara_weapon.c`; the camera consumes but does not own that state.
Re-enumerate this draft after acceptance; do not freeze
this moving worktree into the runtime migration.

**Interactions v4.** This is a split by ownership, not a rewrite of the
interaction parser or use of the currently inert hazards.

| Present `tr2_interact.c` symbols | Target |
|---|---|
| `game_tr2_pickup_type`, `pickupReach`, `alignPickup`, `interactPickup`, `interactSwitch`, `game_tr2_switch_update`, `game_tr2_switch_settled`, `useKeyhole`, `actionNear`, `freeAction`, `interactionLocal`, `q14`, `angleDistance`; switch, pickup, key clips and commit constants | `tr2/object_interact.c`. Same cluster as TR1 pickup/switch/hole. Keep private reach helpers near these callers and needed deferred checks; share a private prototype only when the file split requires it. The switch timer's execution point stays with the floor parser/hook. The public pickup classification must remain resident or be callable under the load lease when owner validation runs. |
| `ringFind`, `ringPriority`, `priority[]`, `ringCredit`, `ringRemove`, `pickupCredit`; `game_tr2_confirm_key`, `game_tr2_key_pending` | Resident `tr2/inventory_economy.c`, preserving the host-visible key-confirmation names. `useKeyhole` still applies the debit, pause ticks and pose in their original order. Key UI, selection and save support are not implemented by this split. If a new cross-file helper is needed, export the existing primitive, not a second copy of the ring or a replacement algorithm. |
| `validateOwners`, `game_tr2_validate_interactions`, `required[]`, floor owner scratch maps | Cold gameplay-admission side of `tr2/object_interact.c` in the host, or the play-cart's existing load/validation owner if already leased there at the integration base. Do not make `load` call a level-selected object unit (or make pre-lease resident binding call into paged object code). Keep the authored-floor scan and all rejection predicates intact. |
| `doorContact`, `game_tr2_item_surface` | `tr2/object_trap.c`: door leaf contact and trapdoor/collapsing-tile floor/ceiling adapter. `world.c` retains door room/sector/box effects. `hit.c` owns packed spheres; door bump aggregation stays in `collision.c`. `q14` is a trivial calculation used by both families; expose one private inline if splitting needs it. |
| `game_tr2_boulder_update` | `tr2/hazard_boulder.c`, preserving listener input, camera request, heavy floor actions and terminal refusal. Not the TR1 boulder controller and not a new camera owner. |
| `game_tr2_interact`, `checkDeferred`, `game_tr2_inert_interaction`, `game_tr2_refuse_interaction` | `tr2/object_dispatch.c` for routing/inert labels/refusal bookkeeping; deferred reach checks may stay with `object_interact` to avoid exporting helpers solely for diagnosis. No dummy implementations for push block, zip line, other switch, trap or pickup types. |
| Other v4 hunks | `animation.c`: `validateContactClip`, `validateContactLink`, `validateTR2WorldAnimations`, `game_tr2_validate_world_animations` and its clip/pose census remain animation admission. `hit.c`: `walkPackedSpheres`, `game_tr2_validate_sphere_key`, `ItemObj_collideSpheres`; `pose.c`: `game_tr2_step_packed`, `game_tr2_rotate_packed` and their decoder stay geometry. `collision.c`: `tr2ObjectsCollision` and door bump order; `trigger.c`: `tr2TriggerQualifies`, `tr2Actions` and renewal/mask dispatch; `camera.c` / `camera_tick.c`: bounce and listener, not a second camera. `game.c`, `item.c`, `lara.c`, `room.c`, `world.c`: keep existing in-place tick, stop, state, floor and lifetime changes. `tr2.h`: interaction rules, IDs, `tr2DeferredSphereType`, ring/door/clock state; `trace.c`: `doorGaps[]`/schema/stats remain wire ownership. `tr2_arms.c`'s `flareStock` hunk follows the arms destination. Host key-confirmation grammar, test, profile and comparator changes stay in their files. |

## Interactions: what actually differs from TR1?

This audit compares the first-game engine at `daf50c7` with the **read-only**
`m10-interact` integration candidate `313ca2cd4f33fdd4ab7a4e157cc2fd6fe8f50f70`.
`game/tr2_interact.c` is 618 lines there (the runtime first landed on
that branch at `a406759`). The comparisons below concern **the shipped
TR1 port and this TR2 candidate**, not an assertion that every detail of
the TR1 retail executable is represented by our TR1 controller. The
candidate implements only the admitted Great Wall cases. A refusal or
host menu adapter is not a retail rule.

**Short answer.** Ordinary pickups, the plain lever, and the sector
door are largely familiar operations. Acquire at the authored hand
frame, show a notice, hide/unlink the placement, and evaluate a
pickup-owned trigger once; use a lever, wait for its settle, and toggle
the target's mask; animate a door against the trigger and mutate the
world cells. What's different is the edition's *binding*, the exact
reach/pose and ordering around those operations, the credit and ring
representation, and a physical **door leaf** controller our TR1
dispatch does not have. The sector door kernel and several helpers are
already shared. Neither reusing the TR1 controllers unmodified nor
keeping every TR2 helper forever is the right answer.

### Binding and the counterfactual

- **TR1:** `game.c:initBase` / `game_init_item` use the TR1 placement and
  model slot. `dispatch.c:game_item_call` selects the TR1 effect/enemy/
  object construction and active/collision operations. `object_factory.c`
  and `object_dispatch.c:Object_update` / `Object_collide` classify
  `ITEM_*` IDs, with `object_common.c:Object_isPickup` and
  `hazard_dispatch.c` as helpers. `inventory_economy.c:inventory_addPickup`
  and `inventory_applyItem` map those IDs to `SLOT_*`; those are not
  model or item indices.
- **TR2 candidate:** `game.c:initTR2Placement` uses the raw TR2 model
  slot (265 public types vs TR1's 191) and binds sequel flags and status. `tr2Ticks` and
  `game_tr2_activate_item` admit known TR2 updaters (including type 104
  lever, 60 boulder and doors 106–113); `updateTR2Items` dispatches them.
  In `collision.c`, `tr2ObjectsCollision` comes through the sequel
  rules and calls `game_tr2_interact`/creature contact rather than the
  TR1 `Object_collide`. `tr2DoorType`, `game_tr2_pickup_type` and the
  sequel switch/hole predicates bind the raw numbers; `tr2Actions`
  separately interprets *floor item references* as placement indices.
  Neither model type nor inventory-ring type can substitute for those
  indices. Cart registration and unit lookup don't translate IDs.

| Actual Great Wall content | If **raw TR2 IDs** were passed to TR1 | Even with type translation, if the TR1 **bodies** were used unchanged |
|---|---|---|
| Lever slot 4, type **104** | TR1 `ITEM_INV_AMMO_SHOTGUN`, not `ITEM_SWITCH` (**55**). `Object_isPickup` does not admit that inventory-model ID as a placed pickup; no ordinary lever dispatch. Conversely TR2 **55** is a collapsing tile, not a lever. | `Switch_collide` would miss valid approaches at e.g. local Z 292–311 or X 201–220, and start via Lara's `ItemObj_animSkip` rather than first frames of 63/64. It does not immediately advance the lever on the press. Its `Switch_update` is otherwise the same once active. The `switch-door` press/settle/camera ticks would not be preserved. |
| Doors slots 0/3/6, type **106**, reverse door 72, type **107** | TR1 inventory Uzi ammo / inventory explosives, not door types **57–64**. They don't get normal TR1 door initialization or controller dispatch; aliases in the other direction include TR2 spikes **59**, boulder **60**, disc **61** and launcher **62**. | A mapped door can reuse sector/box open/close machinery, but TR1 `Object_update` advances animation **before** applying a matching-state gate. The candidate tests/applies the matching entry state **before** advancing; `rusty-key` opens its pose at 746 and unblocks at 747 ([I:117](specs/wall/tr2-triggers-and-interaction.md#keys-and-puzzle-sockets)). Furthermore TR1 `Object_collide` explicitly returns for doors, so mapped leaves would not give the masks, pushed positions or bumps of the three door-contact routes. Closed-sector collision is not a substitute for moving geometry. |
| Ammo **143/144**, medikits **149/150**, flare box **151**, dragons **190–192**, keys **193/194**, locks **201/202** | Examples: 143/144 are TR1 Scion pedestal/drop, 149 unused, 150 the inventory Scion, 151 explosion, 190 glyphs; 191 and above are outside the TR1 item domain. Using TR1 loader admission can reject the level rather than emit *any* of these incorrect interactions; forcing a raw ID into its dispatch is not a fallback. | Ordinary reach/commit mostly transfers, but a forward approach at local Z +101…+256 would miss TR1 dry reach. The wrong credit would lose box-of-six flares, bundle-vs-reserve ammo, dragon bits/chime/reward or proper key entries. Running the TR1 hole after inventory/ID translation would still use the TR1 ring selection flow and fail to reproduce the recorded pause/confirmation as such. A generic pickup cannot stand in for the Gold Dragon's **separate** pickup-owned trigger; the pool tiger wakes on sector entry, not when the key is taken. |

These are code-path consequences, not an experiment run with the wrong
engine. Item numbers come from TR1 `game/internal.h`,
[TR2 public slots](specs/tr2-tr3-differences.md#32-tr2-complete-slot-list-rs-1512)
and [Great Wall placements](specs/wall/tr2-great-wall-objects.md#authored-checkpoints)
(O:15–22).

### Routine-by-routine comparison

Wall citations use lines in `daf50c7`: **I** =
[tr2-triggers-and-interaction.md](specs/wall/tr2-triggers-and-interaction.md),
**O** = [tr2-great-wall-objects.md](specs/wall/tr2-great-wall-objects.md),
**G** = [tr2-lara-ground-and-weapons.md](specs/wall/tr2-lara-ground-and-weapons.md),
**L** = [tr2-great-wall-level.md](specs/wall/tr2-great-wall-level.md),
**C** = [tr2-camera.md](specs/wall/tr2-camera.md), **T** =
[tr2-oracle-trace.md](specs/wall/tr2-oracle-trace.md). Ranges in the first
column are `game/tr2_interact.c` at `313ca2c`. “Parameter” means the
idea and control structure are inherited, not that two current
fixed-point implementations produce the same bytes on all inputs.

| TR2 routine(s) | TR1 peer and rule comparison | Verdict after the mechanical cutover |
|---|---|---|
| `game_tr2_pickup_type` (13–18) | `Object_isPickup` (`object_common.c`): **same classification role, different parameters** (I:75–86). Candidate whitelist is narrower than retail and excludes loose flare/world gun variants; it is admission too. | Use the first game's classifier interface with separate data/edition branch only if callers distinguish retail classification from the *admitted* allowlist. Do not broaden admission or alias IDs. |
| `validateOwners`, `game_tr2_validate_interactions`, `required[]` (22–99) | TR1 Core floor/spans admission and `Object_construct` are peers only in role. **No matching TR1 operation** for a scratch scan of authored owner records behind live closed doors plus a TR2 pose/commit allowlist. I:16,65–66,108,131 describe the data; they do **not** require this port validation algorithm. | Keep separate cold validation bound to the admitted TR2 assets, with existing native format primitives. No gameplay refactor needed. |
| `q14`, `interactionLocal`, `angleDistance` (103–117) | Signed fixed-point math and `Object_checkLimit` inverse rotation/angle gate: **same intent**. Inputs differ (TR2 temporary yaw/pitch, no roll in these callers); `tr2_sin` folds before quantization, and TR2 explicitly specifies inclusive edges (I:57,65–66; G §Projecting speed along a heading). TR1 uses the matrix and a different signed-angle expression. | Share limit testing or a parameterized local transform only after exact arithmetic/range checks; do not replace a math body solely because both say Q14. Keep the `TR64_RULE` distinction at that primitive, not per item. |
| `pickupReach` (119–127), `actionNear` (503–513) | `Object_checkLimit` plus `LIMIT_PICKUP` / `LIMIT_PICKUP_UW` / `LIMIT_HOLE` / `LIMIT_BLOCK`: **same with parameters** (I:57,65–66,104,121; O:65 and §Zip line). Dry X ±256, Y ±100, rear Z −256 and pitch ±1820 are familiar; front Z is +256 instead of +100. Underwater ±512 position, ±8190 angles and −4550 reference pitch correspond to TR1 ±512, `ANGLE(45)` and `ANGLE(-25)`. TR2 builds a temporary comparison pose without rotating the placed object as TR1 `Pickup_collide` does. `actionNear` also accepts a pre-step point and alternate yaw for the block gate. | A shared pose/box gate with explicit source point, temporary pose and edition-specific bounds is viable. Keep which actor/medium/pose is eligible at the controller. Don't silently add or delete item rotation writes. |
| `alignPickup` (129–161) | `ItemObj_moveTo` / `ItemObj_getRelative` (`object_common.c`, `item.c`): **same dry snap rule** (0,0,−100), but writes differ. **Same underwater approach shape/parameters**, (0,−200,−350) and limits of 16/364 (I:65–69); candidate uses upward-rounded integer length and signed division vs TR1's shifted reciprocal table and fixed-point products. | Share dry snap once exact placement and yaw/roll writes are proven. For underwater keep edition-specific stepping arithmetic behind a shared alignment interface, or separate if joining it would add a second pass/check. |
| `freeAction` (264–267) | The conjunction already in `Switch_collide` and dry `Pickup_collide`: **same rule**. II also requires free hands when initiating submerged ordinary pickup (I:65–69); TR1 `Pickup_collide`'s wet branch here checks Action but not free hands. | Reuse the predicate where eligible; don't apply it retroactively to the **commit** phase or TR1 wet entry. |
| `interactPickup` (269–294) | `Pickup_collide` (`object_interact.c`), events in `pickup_events.c`. **Inherited** staged pickup (stand/tread, reach, align, then authored commit, no held Action needed then; notify, unlink and keep hidden identity). **Changed** dry gravity gate, direct choice of 135/+42 or 130/+18 instead of TR1 `ItemObj_animSkip` and distance from end (−23/−14), wet hands occupancy, no ordinary pickup tally, edition-specific credit (I:65–86). The first game's ordinary play has no wade, though its port's pickup predicate already includes that water-state value. | Strong shared-controller candidate with explicit `TR64_RULE` pose/entry/credit/tally variants; preserve existing `game_pickup_notify` already used by both. Do not infer that “+42” equals “end−23” for arbitrary clips. |
| `ringFind`, `ringPriority`, `ringCredit`, `ringRemove` (163–227) | `inventory_economy.c:inventory_setCount`/`inventory_addCount`/`inventory_consumeCount`, `inventory.c:setPage`, display helpers in `lara_inventory.c`. **Different representation and order**: TR1 fixed counts and preordered present slots on opening (including filtering by page); TR2 persistent ordered public entries, stable insertion after equal priority, compaction, unchanged saved *ordinal* selections, separate reserve (I:75–98). | Separate body in TR2 inventory economy. Can share a checked quantity delta only if the domains and sentinels stay distinct. Don't replace the TR1 slot ABI, TR1 use-page preselection, or the TR2 insertion order. |
| `pickupCredit` (231–262) | `inventory_addPickup`, TR1 secret in `trigger.c`, bespoke Midas/Scion in `object_progression.c`. **Same +1** for ordinary medikit/key counting, **different sinks and rewards** (I:49,75–88,98; L:47–52). Shotgun in Wall gives 12 reserve units if owned, one ordered ammo bundle otherwise; auto pistols give 40, flare box gives six stock. A dragon sets one bit and chimes; distinct third pays the launcher/ammo/small-medikit bundle, not a ring dragon. TR1's `TRIGGER_ACTION_SECRET` awards via floor action instead. Scion's special pose/camera is not a dragon implementation. Candidate does not implement all retail acquisition/type conversions in I:77–86. | Keep edition-specific credit and progression; share award-independent pickup mechanics. Runtime-typed data should describe admitted credits, not force TR1 inventory IDs/semantics into TR2. |
| `interactSwitch` (298–318) | `Switch_collide` (`object_interact.c`). **Inherited** Action/free stand/ready lever, snap yaw without translation for plain lever and invert state intent; **parameter and phase differences**: TR1 X ±200, Z 312–512 vs II ±220 and 292–512, II explicit gravity gate and 63/64 first-frame entry, immediately advance the lever **without** setting its own full mask, timing sampled in I:121–131. Other dry variants have different snaps; II type 105 isn't admitted here (I:134–140). | Share plain dry lever state/entry pipeline with per-game reach/pose/advance-at-press and claim/activation policy. Keep variant policies separate: do not dispatch one-shot buttons or underwater levers to the Great Wall plain body. |
| `game_tr2_switch_update` (320–328) | **Same body** as TR1 `Switch_update`: restore all five own mask bits, consult shared `Object_isActive`, request state 1 and zero timer if expired, animate (I:123,140). | First fold. One implementation, no behavior rule switch in the body; expose it only under valid lifetimes. TR1's `object_interact.o` lies in `objects`: a resident or other-unit TR2 call needs a link-safe shared placement or the same unit, not a direct reference to that TR1 overlay. |
| `game_tr2_switch_settled` (330–343) | `object_common.c:useSwitch`: **same timer/phase rule**, inactive status, state 0 timed return, 1 is one tick, >1 seconds (I:26,140). Different binding/type check and tail: TR2 uses `ItemObj_deactivateBase`, TR1's `ItemObj_deactivate` has hooks. | Share status/timer transition with edition-specific binding/deactivation at the edge. Preserve the switch op that does not re-arm fixed view on timed return; that belongs to floor actions, not switch tick. |
| `game_tr2_confirm_key`, `game_tr2_key_pending` (515–526), `useKeyhole` (530–561) | `Hole_apply` / `Hole_collide`, `Lara_selectedUse`, `inventory_openUse`/`setPage`/selection, `inventory_applyItem`. **Inherited** reach (±200, Y 0, Z 312–512 and ±1820/5460/1820), matching debit *at pose entry*, align +362 for key/+327 for puzzle, don't fire list during busy pose (I:104–108,114–117). **Different** slot/transaction storage and animation selection. TR1 requires authored stand clip and can use an already selected key without pressing Action. TR2's confirm/pending have **no TR1 host peer**: they represent the native confirmation and clock pause without simulating the menu (T:18,155). The candidate deliberately refuses several carried keys, wrong key and spent socket; retail may select among keys and sound refusal (I:106). TR1 preselects the mapped hole slot on opening the use page, but still takes a user choice. | Share reach and successful align/enter-state transaction with explicit edition hooks only after testing. Keep TR2 ring/host pause and validation separate, not automatically selecting a key and not replacing the TR1 UI. Puzzle use in this candidate only refuses. |
| `game_tr2_item_surface` (422–438) | `item.c:ItemObj_getItemFloorCeiling`, `ItemObj_getTrapDoorFloor`, `object_trap.c:TrapFloor_update`. **Same** two intact falling-tile states and Y−512/underside+256 with a floor reference; II coverage derives from the linked floor program for both planes (O:30–46), not a geometric post-filter. Candidate reuses TR1 trapdoor footprint query but **refuses** a contributing closed slab; no TR2 trapdoor/collapse controller yet. | Share the exact falling slab height/state helper and plane substitution, keep the edition-specific floor-program/linked-room selection and unsupported activation separate. The trapdoor's footprint is already shared; this guard can't be folded into a functioning trapdoor policy. |
| `doorContact` (442–501) | TR1 **door** collision callback is empty; peers are generic `ItemObj_collideBounds`, `ItemObj_updateHitMask`, `ItemObj_collidePush` and enemy/hazard contact. **Inherited geometry shape:** strict Y, inclusive expanded XZ, nearest key/spheres and nearest face. **Different policy/details** (G:106–110): bounds miss retains touch, priority is low X/high X/**high Z/low Z** (TR1 generic push uses low Z first on Z tie); probe tests **Lara**, TR1 `ItemObj_collidePush` calls `ItemObj_collideRoom(self,...)`; TR2 restores the recorded gap settings, accepts room-only membership at Y−10, and moving leaf may bump even if blocked without a timed hit. TR1 generic enemy bump writes `hitTimer=5`. | Share broad-phase and parameterized exact geometry only. Keep TR2 door contact and bump policy as a caller, not generalized enemy/hazard damage; don't enable leaf push on TR1 doors. |
| `game_tr2_boulder_update` (347–384) | `hazard_boulder.c:TrapBoulder_update`/`collide`, `TrapBoulder_resetOrigin` (`object_factory.c`); `hazard_contact.c` has slam, sword, ceiling, not the boulder. **Same motion outline**, floor settle within 256, room change and heavy floor evaluation; **different sequencing and parameters** (O:113–133): candidate advances authored pose/motion first, samples stored floor, emits sound 147 and writes shake from previous **sound listener**, strict horizontal distance <10240, scale 40, last qualifying write wins; evaluates the stored trigger before 384-ahead test. TR1 hand-integrates, changes roll state at floor, tests 512 ahead and tile-centres on stop, then heavy/effect animation; no such listener shake in its update. Candidate refuses terminal/release/reset; O:121,133 describe the later retail harm/reset, not implemented here. | Keep separate TR2 controller. Share only proven leaves (heavy trigger service, floor-settle substep if identical, camera shake apply as its own audit). TR1 hazard dispatch or its fatal/airborne hit code is not TR2's active-sphere rule. |
| `game_tr2_inert_interaction`, `game_tr2_refuse_interaction`, `checkDeferred` (386–418,563–605) | Registry/default-stop in `object_dispatch.c` and guards in `Block_collide`, `Hole_collide`, `TrapSlam_update`, `TrapSwingBlade_collide`, `TrapBoulder_collide`, `Pickup_collide`; **not matching implementations**. Block grasp uses a pre-step stand origin and permits held Forward/Back (O:65), unlike TR1's input veto; offered block movement still refuses. Deferred contact uses bounds/spheres to reject rather than apply damage; zip mount, loose flare, nonmatching variants and puzzle remain refused. Labels, count/tick and refusal shape are harness policy, not wall-specified deaths or stock. | Keep the admission/diagnostic dispatch separate. Future object controllers replace individual refusals only when the cited behavior has its own evidence. Don't count the registry as an implementation of those traps. |
| `game_tr2_interact` (607–618) | `Object_collide` plus `Hazard_collide`: **same dispatch shape, different tables, available callbacks and return contract**. TR1 returns void; candidate returns a door bump to the common contact pass. See binding table, O:137–143, G:110. | Keep edition-aware registration/dispatch. Reuse individual named operations, not TR1's raw-ID switch or a post-hoc synthesized bump. |

**What one pickup Action takes.** Action opens the candidate's pose on
the first eligible tested placement; it is not the acquire command.
At the commit-frame visit, each in-reach eligible ordinary placement
can be credited and unlinked, without Action or free hands. `interactPickup`
does not store an owning pickup ID, cancel the pose on success, or break
the room walk; stacked eligible pickups could both commit in one frame
if both are still reached. TR1 `Pickup_collide` has the same per-item
shape; its `Lara_claim*` machinery matters for co-op, not a rule that
one held Action always means one item. A type-4 floor action consumes
**one named vanished placement** once, not everything credited by that
pose (I:65–73). Existing published examples show individual pickups;
the multi-placement outcome above is a code deduction, not a new retail
claim. Don't add a one-item latch or assert stacked retail parity without
a specified discriminator.

### Owning files around the 618-line module

| Candidate hunk | First-game peer, rules and decision |
|---|---|
| `world.c:unlinkedPickupIsCollected`; existing `initialTR2World`, `doorRefs`, `refsMatch`, `applyRefs`, `game_door_action` | **Already shared** sector/box refs, closure and restoration including alternate rooms. Unlinked **acknowledged** pickups need the edition classification (I:71–73); this is one in-place rule branch. Door update callback timing differs as above. O:26 also describes I's **retail/reference** 2048 proximity exception and II's closed-state behavior. Don't assign that exception to `daf50c7`'s TR1 `Object_update` or `game_door_action`: neither body tests that proximity. Keep current TR1 bytes, not a claimed retail repair. |
| `collision.c:tr2ObjectsCollision`, `Lara_updateObjectsCollision`, and `game.c:updateTR2Items` leaf-mask clear | Both games' nearby-room walk is per room-item order, collision flag/non-invisible, strict per-axis <4096; both save a successor before callbacks. Candidate's direct portal walk avoids borrowed scratch and handles relinking around Lara; it merges door/creature bump and active leaves clear touch before contact. O:137–143 calls for pass-start room selection with authored order **without deduplication**; candidate's `seen[]` deduplicates. Mark it as candidate policy on that input and preserve it in relocation; do not claim equivalence or fix it in the move. Health is sampled at pass entry. Share prefilters and, if proven, traversal later; keep unit boundary and reaction order. |
| `item.c:ItemObj_animProcess` door end-goal | TR1 normally adopts the new authored state as the non-Lara goal at an end transition. TR2 retains the pending door goal (e.g. close requested while the opening clip still runs, G:118–120). Keep the cited conditional at the existing animation transition, separate from mask clearing and door geometry. |
| `hit.c:walkPackedSpheres`, `game_tr2_validate_sphere_key`, `ItemObj_getSpheres`, `ItemObj_collideSpheres` | TR1 mesh-sphere walker already chooses nearest authored key and strict overlap. **Same rule at that level**; sequel packs rotations differently and the new route uses bounded private stack, positive radii and wider exact overlap rather than wrapped squared distance (G:106). Admission is not an additional gameplay collision. Keep edition decode/admission distinct; bounded sphere overlap and common traversal leaves are candidates after proving rounding, transform origin and stack depth. |
| `room.c:Sector_getTriggerFloorCeiling` call to `game_tr2_item_surface` | TR1 asks `ItemObj_getItemFloorCeiling` through its floor parser. **Same dispatch role**, edition floor codes/link rules/slots bind the candidate query; floor data must name the slab, not merely the item overlapping the world coordinates (I:22,44; O:38). I:22 also allows the controlling item itself to contribute; this candidate only calls the sequel surface hook for object **actions**, not controller words. Preserve and report the scope in the mechanical move; share the height operation, not parser layout or current refusal. |
| `trigger.c:tr2TriggerQualifies`, `tr2Actions`, `tr2ItemAction`, `tr2RenewView` | `game_check_trigger`, `checkCamera`, `usePickup`, `useKey`, `useSwitch`. **Inherited** pre-eligibility renewal, switch and once owner transitions, pad/ready/heavy gates, mask add/XOR/clear and object timer `0 / 1 / seconds`; **different** action grammar/end word split, antitrigger/one-shot lock on incomplete anti mask, TR2 camera 1→30, candidate pickup requires unlinked+invisible and key after not-Busy (TR1 `useKey` demands Free), timed-release no camera in both, different soundtrack/secret/effect rules (I:11–18,26–53,73,108,117,140). Keep edition parser/owner policy and phase ordering; share mask/timer/once transitions as explicit rules, not one decoded switch over raw words from both games. Unimplemented sequel action kinds still remain named/inert as currently specified. |
| `game.c` listener/clock and `camera.c`/`camera_tick.c` bounce | No TR1 boulder listener shake producer in `hazard_boulder.c`. A TR1 camera bounce consumer exists; C:130–146 and O:115–119 bind II offsets and RNG/order. Candidate's camera listener uses the admitted viewport projection (610) and prior view; the stats/key pause wiring belongs to tick/host transaction, not either door or pickup. Preserve keys, animation advance, active order and camera invocation points. |

**How much of the 618 lines?** Counted physical lines in the pinned
file. Removing blank/comment-only lines leaves 553, including 546 lines
in 29 function definitions (signatures and braces counted), and seven
include/enum lines. Counting **whole function groups**, not copied text:

| Group | Function-definition lines | What that count says |
|---|---:|---|
| Direct shared rule (`freeAction`, `game_tr2_switch_update`) | 13 | Same predicate or equivalent body already exists in TR1 code. |
| TR1-shaped geometry and transactions | 227 | `q14`, transform/angle/reach, alignment, ordinary pickup, dry lever entry and settle, slab query, door contact/nearest-face geometry, key-use transaction. These adapt existing rules but include substantial new phases, numeric parameters and refusal paths. |
| Sequel accounting and boulder | 128 | Ring/credit routines (92) and boulder update (36): some familiar operations inside, but new storage, rules or sequencing prevent reuse of the whole body. |
| Type binding, validators, refusal/dispatch and host key adapter | 178 | Not 178 lines of new retail behavior. |

Thus **240/546 (44%)** of function-definition lines sit in routines
that restate or adapt an existing TR1 operation; only **13 lines**
are in routines classified as an exact inherited predicate/body by this
strict tally. This is not a claim that 227 lines are duplicate C or can
all be deleted; conversely a familiar operation inside an otherwise new
routine (e.g. floor settle, +1 medikit credit) makes the direct
whole-routine count a lower bound on shared *rules*. Counts exclude all
the separate shared-file hunks above.

### Cutover verdict

**Move and split first**, without merging bodies. Afterwards, try each
fold below as its own trace- and shipping-identity change, in this order:

1. One shared active plain-switch tick; then same settled timer/mask
   and acknowledged-pickup transition with explicitly different
   eligibility and deactivation. Preserve overlay leases on each step.
2. Lift the genuinely common reach/shape, dry alignment and pickup
   commit/unlink mechanics and use *cited* `TR64_RULE` choices for boxes,
   changed authored frame selection, dispatch/credit, tally and phases.
   Then consider the matching-key successful use and plain lever
   entry pipelines. Keep wrong/multiple-key host refusals and UI out.
3. Consider shared nearest-key bounds/strict-sphere and edge-selection
   or slab-plane primitives only under explicit rounding, ties, accessor
   and room/lease contracts. Keep the sequel door leaf writer/bump,
   boulder, ring/order/dragon progression, unsupported guard registry,
   host key-confirmation adapter and floor parser policy separate.
   Leave already-shared sector mutation, trigger order and transient
   effects in their owners.

For **each** fold use its own pristine before/after candidate, all
published TR2 traces byte-identical including unsealed prefixes, and
the same TR1 shipping `.text` gate (plus actual overlay refs where
applicable). Fixes for wall/candidate gaps are separate changes; they
cannot hide inside a reported byte-neutral fold.

## Cartridge constraints and staging order

D17 selects one game per VR4300 link while the host keeps both. Follow
[code residency](architecture.md#6-code-residency),
[the image census](tr2-cart-image.md) and the frozen **play-cart proposal**
above, not the old host's `TR64_HOST` guards as a cart design. The census
and live proposal are different builds: e.g. the census's pruned folded
measurement is 618,264 B resident and still has an invalid load-validator
edge; the earlier playable proposal reports 444,728 B resident and 11/24
code frames at load overlap, with no live creature support at its cited
base. Do not quote either as a current creature budget.

* Keep animation/poses, common enemy/nav and room/collision kernels,
  effect allocation, Lara/arms/camera, per-game state and binding resident
  unless the play-cart owner has measured a different **valid** lease.
  `GameTR2`, brains, arrays, live flags, lists and counters are writable:
  no copies or pointers into overlay data as persistent state.
* Compile each species and pageable object in its own object. Group
  those objects in a thin `unit_<group>.c` and game-specific units
  fragment, with `tr64_unit_tr2_<group>` and type/operation dispatch.
  The current candidate's `unit_tr2_wall.c` contains both controllers and
  routing; split its helicopter/fire bodies out but leave that unit's
  identity, selector, operation set and lease intact on the first move.
  Decide whether tiger/spider/crow are one group or several from measured
  co-occurrence, loaded page rounding and TLB peaks—not filenames or a
  per-species minimum of one page. Match the eventual cart source lists
  in `game/module.mk`; TR1 cart sees none of these objects or descriptors,
  the host sees both, and the TR2 cart sees the admitted sequel subset.
* A paged controller can call resident primitives; resident dispatch
  must use `code_call`, and a unit must not directly call a different
  unit. Be especially careful with key payouts, boulder/heavy triggers,
  creature hits/activation, shard/splash generation and joint tests. A
  trigger can re-enter item dispatch on the same tick: check the final
  leases and avoid adding cross-unit calls disguised as callbacks.
  Pre-lease placement metadata and validators must not require controller
  code; keep cold validation in its existing `load` lease when the cart
  patch supplies it. Don't move a person’s closure or change the order
  of calls just to silence `refs-check`.
* Type-to-unit selection starts with placed roots **plus spawn closure**,
  not raw ID intersections across games. Block/lever/door and boulder
  controller families deserve individually selectable objects; campfire
  effects use the shared effect pool, loose flare is a dynamic root, and
  disc launcher → disc is a future required edge, not a new capability
  here. Make the existing unit generator/cook check actual per-level
  pages, pairs/entries, and concurrent load/front/inventory/save leases;
  measure state/level high-water, resident/hot size and 4 MiB scenes.
  Don't alter pool rows or add an allow-listed relocation as part of the
  mechanical split.

**One cutover, one interactions forward-port.** The crows and play-cart
candidate first land under their original paths. The cutover moves the
sequel sources and updates all cart source and full-path unit selectors
at once; interactions merges across it and keeps its rule changes
separate from its own subsequent file split. A new residency or unit
selection decision requires independent review.

## Required proof of the mechanical change

1. Pin the integration base, final patch and toolchain/pack identities.
   Build a pristine before and after sequentially in the same assigned
   scratch budget. No legacy real-world build remap, route fixture edits,
   trace writer edits, producer change, altered flags, imports from wall
   tools, altered diagnostics contract, opportunistic bug repairs or
   feature gates. Preserve includes/build identity notices as required
   by provenance; in particular splitting code which calls `ASSERT`
   must not accidentally change observable source IDs or cart strings.
   Leave order of each update, operands, predicates and RNG draw intact.
2. Enumerate **every** published TR2 manifest at cutover, including root
   schema 20 (`--game-tr2`) and `schema21/` (`--game-tr2-schema21`);
   retain both when they cover the same route.
   Bind each to input and pack SHA, placement, mode, and any key-ring
   confirmation event. At this inventory point there are 53 root
   manifests and 10 schema-21 manifests; do not assume the list will
   stay fixed. Also run the explicit historical/baseline lane where it
   is a supported harness mode and run the slices' injected controls.
   The accepted merged host, not retail equality, is the move baseline.
3. For every successful route, compare the *complete* before/after port
   traces as bytes, including row and footer hashes, producer, RNG streams,
   active list order and brain queue/route data. Don't project 21 to 20
   or feed the old artifact in as the new result. Validate existing
   comparator/profile separately; a known retail delta is allowed only
   if the port output itself is unchanged. For an existing unsealed
   refusal, require same nonzero exit, same stable refusal kind/reason
   and first affected tick, same last stored tick and byte-identical
   prefix, and no footer; distinguish currently unregistered routes from
   accepted routes. Unregistered/unsupported manifest is never quietly
   discarded. Lastingly incomplete gameplay is not a relocation defect
   or a reason to relax the gate.
4. TR1 must retain shipping `.text` bytes (same PC campaign/video pack,
   selector and toolchain), existing shipped controller object code and
   defined symbols; compare `.text` and unit code/section inventory to
   the pristine cutover base, not the pre-memory-split symbol list.
   Gate the 25 solo cases in schemas 2–5, native admission, hot layout and
   size. Check no sequel symbol/unit unexpectedly links into the TR1
   cart. Run `make provcheck`; syntax and traces alone are not proof of
   page lifetime.
5. For the TR2 cart that actually lands: same play-scene and host state
   hashes before/after, both game selector builds and existing refs/map
   and frame/TLB peak gates, with positive and negative unit-selection
   controls and a 4 MiB scene that actually calls a selected controller.
   Preserve the shipped pack, tested route inputs, before/after logs and
   linked map evidence outside git. No claim of a verified current cart
   or of successful code migration is made by this document.
