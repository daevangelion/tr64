<!-- SPDX-License-Identifier: MIT -->
# Fixed-game behaviour oracle

This is the M2 prerequisite, not tr64's game implementation. It compiles
OpenLara `src/fixed` at **2333adef30ae2a5f3b32c5355e6e343d6aa0531a** with a
headless platform and a trace exporter. No OpenLara checkout is modified.
Main explicitly permitted consulting the pinned host tests
`tools/n64_magma/test_caves_worldsim.cpp` and `run_fixed_hazard_tests.py` for
host compilation and stubbing, not the old port's renderer or formats.

## Build and run

```sh
rtk test/oracle/build.sh
rtk python3 test/oracle/input.py test/oracle/scripts/run-forward.input $TR64_WORK/tmp/Oracle/run.masks
rtk $TR64_WORK/builds/Oracle/tr1-oracle --level $TR64_DATA/pc/level1.phd --input $TR64_WORK/tmp/Oracle/run.masks --ticks 150 --out $TR64_WORK/tmp/Oracle/run.jsonl
rtk test/oracle/run.sh m2-start --verify --update # explicit first schema-2 reference
rtk test/oracle/run.sh m2-start --verify          # require existing identities/traces
```

Requires Linux `g++` with the **32-bit C++ development libraries** (on this
host `g++-15-multilib`), Python 3, git, tar, patch, flock and coreutils.
`OPENLARA_SOURCE` overrides `$OPENLARA_SOURCE`;
`TR64_ORACLE_BUILD` overrides `$TR64_WORK/builds/Oracle`; `CXX` overrides
`g++`; `TR64_DATA` overrides `$TR64_DATA`; `PYTHON` overrides
`python3`. The program consumes trusted player data, not untrusted downloads.
It rejects non-PC headers and parser failures; the upstream bounded parser
is retained. The initial filename must name TITLE or a gameplay level from
`gLevelInfo`. Normal source transitions load the actual canonical, lowercased
PC sibling filename (for example `level2.phd`) from the initial file's parent
directory, using the source level ID/name table. Every loaded file is retained
in an owned per-level input vector, and `oracleLevelSize` is updated for that
file. Missing files, unsupported IDs and invalid PC files fail explicitly.
Campaign/cutscene transition policy remains the unmodified source's policy.
TITLE records use `lara:null,camera:null` in either schema; the source's menu
input dummy is never represented as a gameplay player.

`build.sh` exports only `src/fixed` and `LICENSE` from the pinned git object
into its private scratch directory, applies `test/oracle/*.patch` in shell
sort order, then compiles `oracle.cpp` and upstream `common.cpp`. It does not
use a dirty checkout's source files, network fetches, or any N64-port code.
The fixed tables, integer trigonometry, square root, matrix routines,
collision, Lara, objects, enemies, effects, camera, inventory and
`gameUpdate(1)` are upstream production implementations. It never calls
`gameRender()`.

The location-independent build key includes the pinned commit, compiler
identity, flags, and relative filenames plus contents of the builder,
exporter and patches. A matching executable is reused under a lock.
`build.sh --fresh` replaces only this job's export and recompiles it. The
manifest reads the revision from the completed build's `revision` file;
`build.sh` is the sole pin. It also records compiler, flags and binary SHA-256.
Use that build key as the source identity in manifests: aborting assertions
embed `__FILE__`, so rebuilding identical inputs in a different build directory
can change the binary SHA-256. Record the actual published binary hash as well.
The source export and temporary compiler files are disposable; fixtures are
not. No game data, traces, source export or executables belong in git.

For M5 menu and transition comparisons, select `--trace-schema 3`. Omission
still selects schema 2; neither mode adds input ticks or changes the 600-tick
scene limit. Build an extended oracle under a separate `TR64_ORACLE_BUILD`
directory and retain its executable/hash as a new immutable reference; do not
publish over the frozen M2/M3 oracle paths. Stream new traces through the
existing `test/game/stream.py` pipe helper or a memfd, not disk JSONL files.

The M6 build with hunk 14 repaired (a collected pickup is legitimately unlinked) and trace schema 4 is published as
`$TR64_WORK/fixtures/oracle/m6-worldfix-6cf35c0683029590fc80bf40ac0d4b4ae8d50e229ed3835e74c9caf64b2f2109/`
(build key `6cf35c06…`). Against the build below (`test/oracle/identity.py`, stdout, exit code and every trace byte)
it is identical on the 25 M2 fixtures (schema 2), 200 LEVEL1 schema-3 placed scripts (seed 61) and the four
cutscenes' full-length zero and random traces. It differs from it exactly where a collected pickup with a pickup
trigger is followed by a door or flip: `test/game/pickups.py` (LEVEL3A cogs, LEVEL3B Scion, LEVEL4 key 71,
LEVEL8C items 10 and 54 collected by play).

The M6 build with the repaired cutscene timer (hunk 15) and the 4096-tick scene cap for cutscenes and the
levels before them is published as
`$TR64_WORK/fixtures/oracle/m6-cuts2-dc9b46911684d8e5c122c36a4ccc36c0e4ede48107463ab1a92b47fc239a5554/`
(build key `dc9b4691…`). Against the build below it is byte-identical (stdout and trace) on the 25 M2 fixtures,
the four cutscenes' full-length zero and random-input traces, and 200 LEVEL1 plus 30 each LEVEL3B, LEVEL7B, LEVEL10A,
LEVEL10B and LEVEL8C schema-3 scripts.

The combined M6 build (cutscene levels, hunks 13 and 14, `give`/`item` directives) is published as
`$TR64_WORK/fixtures/oracle/m6-cuts-3a6da529429149c21565ea6b0d8eea84d7c47e3d8e2e4ea3fdc8597920d007f5/`
(build key `3a6da529…`; its manifest records the 25 M2 fixtures and 340 placed campaign scripts as
byte-identical to `m5-source-save-2f6e7ef5…`).

The M6 build with hunks 1 to 28 (the lava floor and particles, burning without a timeout, the falling ceiling, the rolling block, the
moving bar, Thor's hammer and the activation of items that start invisible, with the `activate` directive) is published as
`$TR64_WORK/fixtures/oracle/m6-gaps3-df1a5edb84e60caf4f790cfa908f0970d2cd7da45446ef011332cd777958c1ba/`
(build key `df1a5edb…`, binary SHA-256 `447bede8…`; manifest `manifest.json`, schema 4). Against `m6-gaps2b-8521f6f8…`
(`identity.py`: stdout, exit code and every trace byte) it is identical on the 25 M2 fixtures, 100 LEVEL1 schema-3 scripts (seed 61) and the
four cutscenes' zero and random traces. It differs where the hunks 25 to 28 change what is in the world: hunks 25 and 26 on LEVEL3B and
LEVEL10A (rolling-block and moving-bar cells solid), hunk 28 on the triggered invisible items (LEVEL3B ceilings 24, 25, 26, 38, 39, 68,
LEVEL4 blocks 19 and 20, LEVEL10A boat 2, puzzle item 42 and pistols 82). Measured on 187 trigger inputs of those three levels
(`test/game/hunk28.py`) and 300 menu scripts (`identity.py campaign --levels level3b,level4,level10a`): the hunk-28 control
(`no-hunk-28`) differs from the published oracle on 17 trigger inputs (ceilings 38 and 39, pistols 82 become active at the trigger; Lara's
health and trace unchanged) and on 2 menu scripts (LEVEL10A cases 97 and 98, pistols 82); the other 7 of the 9 menu scripts that differ from
`m6-gaps2b` are hunks 25 and 26 (LEVEL3B 27, 30, 51, 52, 81, 99, LEVEL10A 13). The table is in
`$TR64_WORK/evidence/m6/campaign-final/hunk28/README.md`; the other 13 levels have no triggered invisible non-enemy item
(`test/game/invisible.py scan`), so hunk 28 cannot change their traces.

The M6 build with hunk 29 (the fork's early surfacing inside a water room removed: Lara leaves the underwater state only when her room is
no longer a water room, pitch and roll zero) is published as
`$TR64_WORK/fixtures/oracle/m6-surf-84d4dce044027baf280f3a5902c7b9d8da740c1e56723ccfa6160c93ddc506d1/`
(build key `84d4dce0…`, binary SHA-256 `8049194a…`; schema 4). Against `m6-gaps3-df1a5edb…` (`identity.py`: stdout, exit code and every trace
byte) it is identical on 23 of the 25 M2 fixtures and on the four cutscenes' full-length zero and random traces (8 cases); it differs on
`GYM/swim` from tick 144 (`lara.pos[1]` 3585 against 3782: the old build had surfaced her at tick 144, the new one is still under water)
and `GYM/underwater-start` from tick 95 (3585 against 3797). Those two stored traces in `$TR64_WORK/fixtures/oracle/m2-start` were
regenerated with `test/oracle/fixtures.py m2-start --verify --update` (the other 23 stored traces came out byte-identical, and so did
their inputs). A port without hunk 29's port change fails exactly those two in `test/game/fixtures.py --level gym` (tick 144).

The co-op build (hunk 30, schema 5) is published at
`$TR64_WORK/fixtures/oracle/m9-coop-623a2b41d51d8c5aaa1489b312f2b482330d3330d375b9cc9d687ae414a3ed85/`.
Binary SHA-256: `4e1018d84576368513945afba0017125298dd1cc65e5600c3d72ef2f5ee07d16`.
Its manifest includes six `identity.py` reports against `m6-surf`: each of schemas 2, 3 and 4
is byte-identical on the 25 M2 fixtures and on both full-length input streams of each of the four
cutscenes. The old fixture bundle and old oracle publications were not changed.

The reviewed co-op build (hunk 31) is published at
`$TR64_WORK/fixtures/oracle/m9-coop-review-d2ad3fc9f2fc66b02b59aa6b19af5de8c8b8886f5df0c70e07b87f48eae0d768/`.
Binary SHA-256: `6526ad8fa929e53ec92aafd60fc6b7c86447760635b6ec6345f044b3c21f807d`.
Its six identity reports match the original co-op publication on the same fixtures and cutscenes.

The deferred-interaction build (hunk 32) is published at
`$TR64_WORK/fixtures/oracle/m9-coop-settled-5a455a3905802ed120322ffd4fa0dd88e679a0d5724fa4f9ed40c558f2a93ba3/`.
Binary SHA-256: `fc01ebdf9445c95b0c09d8831099d3540f2d55b1b7197a59f636875f00738c0c`.
Its six identity reports match the reviewed co-op oracle on the same fixtures and cutscenes.

The actor-ownership build (hunk 33) is published at
`$TR64_WORK/fixtures/oracle/m9-coop-wait-for-actor-d87bc059c19b5bcbbb00acf38e4569aeb80ece2db41b38555494038990d38eb2/`.
Binary SHA-256: `ca985ff1b4837fb425228d783cffb0ac1ac94a9a432e613db0d8051000b1ba24`.
Its six trace identity reports use the hunk-31 publication as the comparison, before the V3 puzzle changes.

The passport build (hunk 34) is published at
`$TR64_WORK/fixtures/oracle/m9-coop-passport-5094f1a470676724cc521b8409d9a1cbf13adb301419db825c351bab7a8c785f/`.
Binary SHA-256: `a98eff3f367758691d684e3d3f71b57ade7e14e5c290dd037ed3145dfd266b09`.
Its six trace identity reports compare against hunk 33.

### Integer and platform contract

Flags: `-m32 -std=c++11 -O2 -fwrapv -fno-strict-aliasing
-ffunction-sections -fdata-sections -D__TR64_ORACLE__`, link
`-Wl,--gc-sections`.

* `-m32` enforces the ILP32 pointer/int/long ABI, including item pointer
  differences and layouts. Compile-time assertions require 32-bit pointers
  and ints, 16-bit shorts and arithmetic right shift of negative integers.
* A port must reproduce 32-bit two's-complement wrapping on signed
  arithmetic overflow **and left shifts**, plus arithmetic signed right
  shift. `-fwrapv` preserves the fixed game's intended machine arithmetic;
  `-fno-strict-aliasing` preserves its packed-word/pointer access. Unsigned
  shifts followed by explicit signed conversion are the portable C99 way
  to reproduce overflowing signed shifts, not relying on C undefined
  behaviour. No replacement floating-point math is used.
  Independent review observed undefined signed overflow/left shifts
  without wrapping flags in `src/fixed/common.cpp:93,98` (LCG),
  `src/fixed/camera.h:113,181` and room code. All 25 schema-2 reference
  traces were byte-identical across g++ `-O0`, `-O2` with
  `-fno-lifetime-dse`, `-O3`, clang++ `-O2`, and `-O1` without `-fwrapv`.
  Commands and per-variant results are retained outside git in
  `$TR64_WORK/fixtures/oracle/m2-start/compiler-matrix.json`.
  Those observations are not permission to omit defined wrapping in the port.
* `CPU_BIG_ENDIAN` is deliberately **not** defined on this little-endian
  host. It describes native memory byte order, not the desired simulation.
  PC data is little-endian. In upstream `DECODE_ANGLES`, `matrixFrame_c` and
  `matrixFrameLerp`, native word packing plus the corresponding endian
  branch produces the same signed component values as a big-endian host
  with `CPU_BIG_ENDIAN`. Setting that macro on x86 would corrupt the
  parser and packed animation words, not emulate MIPS. Records serialize
  numeric fields, never raw C++ structs, bitfields, pointers or padding.
  This is the intended 32-bit machine-integer contract; a native MIPS
  trace comparison remains the M2 host/cart gate, not a claim made here.
* A new `__TR64_ORACLE__` platform branch avoids pretending to be Windows,
  GBA or the old N64 port. All macro-dependent choices introduced by the
  patch are: `USE_DIV_TABLE`; `MODEHW`; 320x240 frame constants;
  `USE_FMT=LVL_FMT_PHD`; standard integer/allocation headers; the existing
  pointer-safe `ADDR_ALIGN4` branch and standard placement new;
  `int2str` via `sprintf`; aborting `ASSERT`; and the input-adapter
  branches in `Lara::updateInput`. START/free camera is rejected, because
  that debug camera reads hardware InputKey bits rather than InputState.
  The shared free-camera input suppression remains upstream verbatim.
  No `USE_ASM`, `USE_MATRIX_INT16`, `ROM_READ`, `HIDE_CORPSES`, platform
  save/demo hooks or autopilot is enabled. `MATRIX_FIXED_SHIFT` stays 0.
* Consequently `soundPlay` runs its full gain/chance/pitch/sample-selection
  logic and consumes the production draw RNG before the sample backend is
  stubbed. The Windows branch would skip it; that branch is not selected.

### Complete patch hunk inventory

Nine source defects are repaired explicitly with Main's approval (the last four, hunks 13 to 16, are listed below; the first five as before): an unused,
indeterminate target-angle component is initialised; the collision iterator
preserves its next room-list item across a callback that may remove the current
item; non-model pickups skip mesh-animation processing; V3 child validation uses
authored species flags rather than a saved parent's mutable trigger flags; and
normal level transitions call the existing inventory-transition helper.
These are not trace-schema changes. The remaining hunks adapt the platform
without changing physics, animation, AI or camera gameplay rules.
The following is every `host.patch` hunk, by upstream location:

1. `common.h:31`: add the headless platform definitions listed above.
2. `common.h:334`: select the existing pointer-safe alignment macro and
   standard placement-new header for Linux C++ compilation.
3. `common.h:357`: select the existing `sprintf` integer-to-string adapter.
4. `common.h:381`: turn empty assertions into fail-closed diagnostics;
   none of upstream's assertion expressions has side effects.
5. `lara.h:2836`: bypass hardware-button translation with supplied action
   masks. This is an input-adapter change inside the controller header.
6. `lara.h:2930`: close that adapter branch **before** the unchanged shared
   free-camera suppression; guard only hardware START translation. Neither
   shared free-camera suppression nor the later toggle logic is changed.
   START is rejected at the executable and script boundaries.
7. `lara.h:3890`: zero-initialise `weaponTrackTargets`' local `angleAim`.
   `anglesFromVector` writes only x/y, but the whole vector is copied into
   both arms and exported. Its unused z previously leaked indeterminate
   stack contents (first observed on the LEVEL1 wolf combat window, tick
   230); weapon aiming reads only x/y. The native port also sets z to zero.
8. `lara.h:361-376`: snapshot `nextItem` before calling an item's collision
   callback, then advance the saved pointer. `Bullet::impact` removes its item;
   `ItemObj::remove` clears its room and reuses `nextItem` as a free-list link.
   Advancing the mutated link previously revisited freed bullets and called
   `initExplosionSlot` with a null room (LEVEL8C tick 234 and LEVEL10C tick 593).
   The native port makes the identical iterator correction; neither side hides
   a null-room failure. The original scene placements are retained.
9. `object.h:984`: give `Pickup` its own update that animates only positive-count
   mesh models. A sprite or missing-model pickup has no mesh animation to process.
   The non-fixed controller's `controller.h:1264-1272` likewise returns before
   animation for a non-model entity (consulted as a fact with Main's permission;
   no non-fixed code is copied). LEVEL10A's triggered pistols item 82 has no
   model 84; inherited `Object::update` previously asserted in `animProcess` in
   both oracle and port. This guard is pickup-specific, including the derived
   Qualopec pickup: invalid enemy or trap models still fail. No substitute model,
   animation, pickup count, trigger state or asset data is invented.
10. `level.h:291`: replace a fictitious 2 GiB input bound with the actual
   file length and make a failed PC parse fatal.
11. `game.h:294-355`: pass the candidate's level ID to precreated-child
   validation and select its child type using that level's authored item flags.
   Precreation clears `ITEM_FLAG_MASK` on the runtime parent, so its saved flags
   are not the original species selector: LEVEL10B's small egg item 22 saves
   flags 36 but was authored with flags 512 (mutant 2); LEVEL10C's big egg item
   103 saves flags 32572 but was authored with flags 2048 (Adam). The original
   validator selected mutant 1 and refused these otherwise valid source saves.
   The host caches immutable `level.itemsInfo[].flags`, keyed by level, when a
   real save succeeds; this remains available when loading from TITLE or another
   level. It is input metadata, not runtime state or an extra save payload.
   Missing metadata fails validation. Source index, child index, type, size,
   room bounds and full payload validation remain unchanged. The native port
   must use the same authored flags from immutable Core item metadata.
12. `game.h:2337`: call the existing
   `inventory.applyLevelTransition(previousLevel, gNextLevel)` in the normal
   `gameUpdate` transition path, immediately before
   `gameApplySelectedNewGameLoadout` and normal level preparation. Previously
   only the regression path reached this helper. Its existing authored rules
   maintain HOME/compass metadata and confiscate weapons/ammunition on entry
   to LEVEL10A. This repair adds only that call: other inventory, settings and
   globals survive restart/start as before; no new resets are introduced.
   Existing TITLE-to-new-game loadout initialisation remains unchanged.

13. `item.h:411` (`ANIM_CMD_EFFECT`): add `case FX_FINISH_LEVEL: completeLevel(type == ITEM_LARA ? this : NULL)`.
   The fixed game ignores the command (`default: ;`) although three animations carry it
   (PC data: LEVEL3B animation 230 frame 428, LEVEL8C animation 171 frame 65, LEVEL10B animation 171
   frame 90), so those levels could not end. Reference for the intended behaviour: the non-fixed
   game's `controller.h:1173` (`TR::Effect::FINISH_LEVEL: game->loadNextLevel()`), OpenLara 2333adef.
   It takes the END trigger's path (`completeLevel`, hence the statistics page where the next level is
   not a cutscene, `applyLevelTransition`, `nextLevel(id+1)`). No other animation in the 20 PC level
   files carries the effect; a script that never reaches those frames is trace-identical.
   LEVEL8C: Lara's Scion pickup animation (170/171, f65) ends the level (LEVEL10A follows).
   LEVEL10B: the same Lara special animation ends it (CUT4 follows). LEVEL3B: it is the Larson
   (item 30) death animation, not Lara's.
14. `object.h:1684-1686` (`worldBuildRoomPairs`, the item-list check): exempt an INACTIVE pickup from
   "an item that has a room is in some room's item list unless it is INVISIBLE". **Repaired defect,
   replacing the earlier hunk that only dropped `ASSERT(applied)` in `Door::action`** (which kept the
   refusal and merely stopped the oracle from aborting on it). The check refuses every door update
   (`Door::action` through `worldDoorDerivedStateMatches` and `doorApplyCollision`, object.h:757-764)
   and every map flip (`worldApplyPendingFlip` restores the flip maps when `worldRebuildDerivedStateForFlip`
   fails its preflight, world_state.h:309-316; assertions are empty in release builds, so the update is
   silently dropped). The source itself reaches the refused state: `Pickup::collide`
   (object.h:1024-1026, 1063-1065) and `ScionQualopecPickup::collide` (progression.h:115-117) call
   `Room::remove` (which keeps `item->room`) and set INVISIBLE; a pickup trigger under the item
   (`room.h:991-994` -> `usePickup` -> `Pickup::use`, object.h:989-996) then turns INVISIBLE into
   INACTIVE while the item stays unlinked. From then on every door update and every map flip of the
   level is refused. This is **not** the Scion's doing alone and not "until the level ends": it is any
   collected pickup that has a pickup trigger under it. PC data (floor-data scan, `test/game/pickups.py scan`;
   the PlayStation data has the same triggers on the same items): LEVEL3A cogs 33, 40, 53; LEVEL3B Scion
   20 (its trigger opens door 32 and requests flip 1, so the flip is lost and the door animates open but
   stays solid; rooms 10 and 11, Larson's trigger, are reachable only after that flip, so LEVEL3B cannot
   be finished by play); LEVEL4 key 71 (exit door 90 is refused); LEVEL7A keys 46, 61, 67; LEVEL8B
   artifacts 15 to 18 (door 68); LEVEL8C items 10 and 54 (door 56). A save load relinks every item
   (`item.h:1226-1228`) and so ended the refusal (and also relinked the INACTIVE pickup: hunk 20).
   Every site in `src/fixed` that unlinks an item was checked: `ItemObj::remove` (item.h:573-600; clears
   `room` in the same call), moves between rooms (`room->remove(this); next->add(this)`, atomic),
   `resetLara`, the save load's relink, the two pickup `collide` functions, `ScionTarget::update`
   (progression.h:236: INVISIBLE first, so already exempt) and its load (progression.h:278). The only
   state that is unlinked, has a room and is not INVISIBLE is a collected pickup after its trigger. The
   hunk exempts exactly that: type in the `Pickup`/`ScionQualopecPickup` set (PISTOLS..MEDIKIT_BIG,
   PUZZLE_1..4, LEADBAR, KEY_ITEM_1..4, SCION_PICKUP_QUALOPEC, SCION_PICKUP_DROP) **and** status
   INACTIVE. Any other item with a room that is in no list still refuses, `Door::action`'s
   `ASSERT(applied)` is restored, and the oracle fails closed on a real refusal again. The port has the
   same condition (`unlinkedPickupIsCollected`, `game_world_items_linked`, `game/world.c`).

15. `camera.h:16` (`Camera::initCinematic`): add `timer = 0`. The source starts a cutscene with the
   previous level's `camera.timer` and `updateCinematic` indexes the cinematic frame table with it
   (`level.cameraFrames[timer++]`, unchecked). LEVEL3B ends with `timer = -1` (Larson's
   "camera 6" fixed camera has expired), so the first CUT1 tick read `cameraFrames[-1]`: the frames
   are copied into an arena right after the 768-byte palette (`fmt/phd.h:1198-1217`), so that
   record is the last 16 palette bytes of `cut1.phd`, each `(byte << 2) & 255`, read as
   `{target, pos, fov, roll}` = `{21560,15448,26724, 28748,22648,-31620, -31644, 27788}`; the
   oracle's eye on that tick was `(43908, 27512, 105296)`, exactly that frame rotated into the
   cutscene. Every other observed entry (LEVEL7B, LEVEL10A, LEVEL8C, LEVEL10B endings) had
   `timer = 0`, so they are unchanged; a cutscene played from its own start has `timer = 0`
   already. The repaired game shows frame 0 on the first cutscene tick and the cutscene lasts
   exactly its table length. The port sets the same field in `Camera_initCinematic`
   (`game/cut.c`); the carry capsule no longer holds the timer.

16. `enemy.h:98-102` (`Enemy::setExtra`): a doppelganger (`ITEM_DOPPELGANGER`) whose sector has no box skips
   `Nav::init` and leaves its nav state cleared: `zoneType = ZONE_GROUND_1` (its `stepHeight` is 256),
   `weight = 0`, `endBox = nextBox = headBox = tailBox = NO_BOX`, `mask = BOX_OVERLAP_BLOCKED`, every
   `cells[i]` (`i < boxesCount`) `end = next = NO_BOX`, `weight = 0`, and `cellsCount = 0` (the trace's
   `nav` object shows `cellsCount: 0`, `endBox`/`nextBox`/`headBox`/`tailBox` 65535). Every other enemy
   keeps `ASSERT(sector->boxIndex != NO_BOX)`. The defect: the doppelganger stands at the mirror image of
   Lara about the room centre (`enemy.h:913-918`), which is over a wall sector with no box (LEVEL10B room 57,
   `roomFloor = -32512`) for the first ~230 ticks after Lara steps off the pad; saving then and loading
   restores it ACTIVE with no `extraE`, `encounterActivateLoadedItem` calls `enable` and `setExtra`, and the
   assertion stopped the oracle at `enemy.h:101` (every save tick 10..230 of
   `test/game/saves.py`'s LEVEL10B script). A release build (empty `ASSERT`) runs `Nav::init(NO_BOX)`,
   which reads `zones[0xFFFF]`, 128 KB past the zone table. The port does the same in `Enemy_setExtra`
   (`game/enemy.c`); it used to stop with `game assertion game/enemy.c:179`.

17. `object.h` (new `DrawBridge : Object`, after `TrapDoor`) and `item.h` (`INIT_ITEM( DRAWBRIDGE , DrawBridge )`):
   **repaired omission**. `ItemObj::init` never constructed a controller for `ITEM_DRAWBRIDGE` (LEVEL8B items 19 to 22),
   so a switch made the item active but it stayed in state 0, animation 230 frame 0, for ever: the artifacts over the
   pit could not be reached and the level could not be completed. The controller follows the floating-point game's
   `Drawbridge` (OpenLara `src/objects.h:842-858` at 2333adef) and `docs/specs/wall/drawbridge.md` sections 3 and 5:
   every frame goal state 1 while `isActive()` and goal 0 otherwise, then `animProcess()`; while the current state is
   0 (raised) its collision is a closed door's (`updateHitMask`, then `collidePush`, which also reacts Lara's hit
   while the item is moving, `state != goalState`); in state 1 it has none, the existing `getItemFloorCeiling`
   case provides the floor and ceiling. Animation 230/231 (state 0, lowering, 21 frames), 232/233 (state 1); the
   bridge is a floor from the tick the state-1 animation takes over (tick 69 after the switch press at tick 47).
   Where `docs/specs/wall/drawbridge.md` and the floating-point game differ this follows the specification (it
   also matches the fixed `getItemFloorCeiling`): floor, ceiling and solidity use the item's CURRENT animation state,
   not the trigger state; the deck underside is `Y + 256` (not `+ 64`); the floor rule adds no tolerance to the
   queried height (OpenLara adds 128).
18. `item.h:825-839` (`getDrawBridgeFloor`): **repaired defects**, two, in the one function. (a) The offsets were
   `item minus query`, which puts the two sectors the rule covers behind the item; the level's trigger entries and
   `docs/specs/wall/drawbridge.md` section 4.1 put them in front (facing 0: z-1 and z-2, 90: x-1, x-2, 180: z+1, z+2,
   270: x+1, x+2; the oracle's `angle.y` is the level angle as stored), so with the original rule the bridge never
   provided a floor even in state 1; the offsets are now `query minus item`. (b) The 90 and 270 degree rows tested
   `dz == -2` / `dz == 2` for the second sector, which cannot hold with `dz == 0` (their second sector was never a
   floor). Verified in LEVEL8B: with only the controller, Lara placed on the east ledge stood stuck at the deck's edge
   (floor not found); with both repairs she crosses item 22's deck at y = -3072 and takes artifact 15
   (`test/game/drawbridge.py`). The port: `ItemObj_getDrawBridgeFloor`, `DrawBridge_update`, `DrawBridge_collide`
   (`game/item.c`, `game/object_trap.c`, `game/object_dispatch.c`).

19. `object.h` (new `Cabin : Object`) and `item.h` (`INIT_ITEM( CABIN , Cabin )`): **repaired omission**. No controller
   was built for `ITEM_CABIN` (LEVEL10A item 84, Natla's Mines), so the three fuses (puzzle holes 26, 27, 28, key
   triggers of mask 31 that each list the cabin) did nothing. Follows OpenLara `src/objects.h:1463-1490` (2333adef)
   and `docs/specs/wall/objects-missing.md` section 18: when the item's code bits are complete each tick, states 0, 1
   and 2 set goal state + 1 (animations 304 to 306) and the code bits are cleared (so each completed mask advances
   one stage); in state 4 flip slot 3 is set to the full mask and active, the map flip is requested and applied,
   and the item is made invisible, not collidable and deactivated (the reference's "removed"; the pool slot is kept so
   saves and the world validator see an ordinary invisible item). It is a solid (`collideDefault`). The floor-shake
   animation effect (effect 1) stays ignored. Port: `Cabin_update` (`game/object_trap.c`) and the dispatch entries.

20. `item.h:1226-1228` (`ItemObj::load`): **repaired defect**. The load unlinks the item and links it into the saved room
   again, for every item. A collected pickup that has a pickup trigger under it is INACTIVE and in no room list (hunk 14);
   the load linked it again, `Pickup::collide` found it (only INVISIBLE items are skipped) and Lara collected it a second
   time: pickup counter and inventory one more (LEVEL3A 33, 40, 53; LEVEL3B 20; LEVEL4 71; LEVEL8C 10, 54, found by
   `GameCov3`, shown by `test/game/pickups.py relink`). The load now links an item that is INACTIVE and of a pickup type
   (hunk 14's condition, evaluated on the saved flags) not at all: it unlinks it, keeping `room` as `Pickup::collide` leaves
   it. Other items, INVISIBLE collected pickups (no trigger under them) included, are linked as before, so every other
   save comparison is unchanged. Port: `game/save.c` (the item record's apply), predicate `game_pickup_collected`
   (`game/world.c`, shared with the validator's exemption).

21. `room.h:932-934` (`checkTrigger`, the `FLOOR_TYPE_LAVA` entry, a `// TODO lava` in the source) and `hazard_lara.h`
   (new `hazardLavaBurn`): **repaired omission**. Lara walked over lava in LEVEL10A (225 sectors with a lava entry in their floor
   data), 10B (386) and 10C (200). A lava entry anywhere in a sector's chain now burns her when she stands on the floor
   (`docs/specs/wall/objects-missing.md` 15.3, `triggers-and-effects.md` 2.1): nothing if her hit points are already below 0,
   if her y is not her recorded floor (`roomFloor`), or if the sector's real floor read at y = 32000 is not that floor
   (`gLastFloorData` and the slant are preserved around that read); otherwise hit points -1, `hazardCause` lava and the
   burning state set (cleared by the existing update on the next tick as she is dead). The hit flag and the ten separate
   flame effects of the specification are not modelled: the fixed game has no flame effects on Lara, only the burning
   state. Port: `hazardLavaBurn` (`game/lara_health.c`), called from `game_check_trigger` (`game/trigger.c`).
22. `object.h` (new `LavaParticle`; `LavaEmitter::update`) and `item.h` (`INIT_ITEM( LAVA_PARTICLE , LavaParticle )`):
   **repaired omission**. The 26 lava emitters (LEVEL10A 6, 10B 7, 10C 13) produced nothing. Specification sections 16.1 and
   16.2, numbers as written there: once active an emitter makes one particle a frame at its position with a random heading
   over the full circle (`RAND_LOGIC(0x10000)`), horizontal speed 0 to 31, fall speed -(0 to 163), sprite frame 0 to 4
   (`RAND_DRAW`) and plays sound 149; a particle moves by its speed along its heading, its fall speed rises by 6, y changes
   by it; at or below the floor or above the ceiling of the room it is in it is removed; else if it is near Lara within 200
   (circle and both axes, vertical at most 3,072, between the top of her box and its bottom + 100) she loses 10 hit points
   and it is removed; else its room is updated. A particle is one slot of the item pool (`ItemObj::add`, no other
   allocation); when the pool is full the emitter makes none that frame. Port: `LavaEmitter_update`, `LavaParticle_update`
   (`game/object_trap.c`), dispatch and factory entries.
23. `hazard_lara.h` (`hazardUpdateLaraBurn`): **specification change**, authorised by Main. The source stopped burning
   Lara after `BURN_DURATION_TICKS` = 90 ticks; the specification (15.2) has no timeout, she burns until she reaches water
   (the existing surface/under test) or dies. The countdown and its clear are removed; `burnTicks` keeps the value set on
   ignition. Port: `hazardUpdateLaraBurn` (`game/lara_health.c`).
24. `hazard_ceiling.h` (`TrapCeiling::collide`): **specification change**, authorised by Main. The source killed Lara at the
   first contact (`hazardKillLara`, 1,000); the specification (section 12) takes 300 hit points per frame of contact:
   `hazardDamageLara(lara, 300, HAZARD_HIT_CEILING, pos)`. Port: `Hazard_collide` (`game/hazard_dispatch.c`).
25. `object.h` (new `movingBlockAdjust`, `movingBlockFloor`, `MovingBlock : Object`; the world rebuild's
   `worldIsFloorBlockItem`, `worldBlockAffectsFloor`, `worldApplyBlockFloor`) and `item.h` (`INIT_ITEM( MOVING_BLOCK ,
   MovingBlock )`): **repaired omission**. `ItemObj::init` built no controller for `ITEM_MOVING_BLOCK`, the rolling block
   of LEVEL3B (items 49 and 50, switches 74 and 76): its cell was never solid and it never slid. Specification
   `docs/specs/wall/objects-missing.md` sections 2.1 and 3, floating-point game OpenLara `src/objects.h:613-669`
   (2333adef). The cell is raised by 2048 (8 clicks) from the first tick: `worldRebuildDerivedState` (level start, map
   flip, save load) now treats the rolling block like a pushable block (`worldBlockAffectsFloor`: not invisible and
   not in state 2, the moving state; its status stays active, so the status is not read), with the specification's raise
   rule (the ceiling `C` of the sector found 2048 + 1024 above: a wall gets `C - 8`, else the floor value drops by 8
   and the cell becomes a wall on reaching `C`; restore is the mirror at `y + 2048 - 1024`; for the two blocks, floor 34 and
   ceiling 15, the wall branches never run). The controller: triggered in state 0 the goal is 1 and the start cell is
   restored; not triggered in state 1 the goal is 0 and the cell is restored; the slide (animations 246 and 247, 64 units
   a frame along its facing for 32 frames) ends with the kill command (status inactive), then the block stays in the
   active list (status active), snaps to the centre of its sector and raises that cell. No heavy trigger is evaluated
   (specification; the floating-point game evaluates one). Not modelled: the block as a solid for Lara or enemies
   (neither source says it is one) and enemy box blocking. Port: `MovingBlock_update` (`game/object_block.c`),
   `game_moving_block_sector` (`game/world.c`), `game_initial_world` and `game_save_rebuild_world`
   (`game/save_world.c`).
26. `object.h` (new `MovingBar : Object`) and `item.h` (`INIT_ITEM( MOVING_OBJECT , MovingBar )`): **repaired omission**.
   No controller was built for `ITEM_MOVING_OBJECT` (LEVEL4 items 75 to 78, the four bars the keyholes 80 to 83 retract;
   LEVEL10A item 12, switch 5): inert, not solid. Specification section 6 and OpenLara `src/objects.h:1406-1425`: every
   frame while active the goal state is 1 while triggered and 0 otherwise, then `animProcess()` (the animation's speed
   moves the item along its facing, so the LEVEL10A bar travels from x = 52736 to about 49600 over 130 ticks and
   changes room 13 to 10), then its room follows its position (portals and above/below, as `Room::getRoom`); its
   collision is `collideDefault` (a standard solid: `updateHitMask`, `collidePush`). Port: `MovingBar_update`
   (`game/object_block.c`), `Object_collideDefault` in the dispatch. The geometry, measured on the LEVEL4 bars (first
   frame box x -820..820, y -415..-255, z -510..-440 in the item's frame, angle 16384): each bar is a plate 160 high and
   70 thick across the doorway, 475 toward -x of the item's x, so the doorway is crossed along x, not z; the four are
   512 apart in height. `collidePush` pushes Lara out sideways, so a plate stops her only head on.
27. `hazard_thor_lightning.h` (`ThorHandle::impact`): **repaired source defects**, authorised by Main. The hammer could
   never hurt Lara: (a) the strike point was `pos + getBlockOffset(angle.y, +3072)`; `getBlockOffset`'s axes are the
   other way round (angle 0 gives -z), so for the 180 degree hammer (LEVEL4 item 63) the point lay 3072 toward +z, in
   the rock behind it, and the specification (section 12: 3072 ahead in the facing, 180 = -z) puts it on the pad cell
   (56, 39): the distance is now negative; (b) the 30-frame delay compared the level-wide frame number
   (`frameIndex <= 30`, never true after the first frames), so the strike came on the first tick of the swing; it now
   counts frames into the animation (`frameIndex - frameBegin <= 30`). Port: `ThorHandle_update`
   (`game/hazard_thor_lightning.c`).
28. `room.h` (new `worldTriggerActivate`, used by `checkTrigger`'s ACTIVATE_OBJECT action and by the `activate`
   directive): **repaired source defect**, authorised by Main. After `activate()` the source turned only a status of 0
   into active, so an item that starts invisible (bit 8 of its level flags) stayed invisible once a trigger had
   activated it: not drawn, and skipped by Lara's collision loop. The specification (`triggers-and-effects.md` section 4,
   object action: a non-enemy item that is activated becomes status active) makes it active. Now a status of
   invisible becomes active when the item is a non-enemy (`health == NOT_ENEMY`); enemies keep their own rule, which is
   the specification's (`Enemy::update` takes a creature slot, then becomes active; it waits, still in the active list,
   while none is free). Items it concerns (the 11 non-enemy instances with a trigger in the PC levels, the same 11 in the
   PlayStation levels; `test/game/invisible.py scan`): the six falling ceilings of LEVEL3B (24, 25, 26, 38, 39, 68; they
   now fall as solid-while-touching slabs and hurt at 300 a frame, hunk 24), the blocks 19 and 20 of LEVEL4, the boat 2,
   the puzzle item 42 and the pistols 82 of LEVEL10A. Port: `worldTriggerActivate` (`game/trigger.c`). Negative control:
   `test/oracle/control.sh no-hunk-28`.
29. `lara.h:3104-3120` (`Lara::updateWaterState`, `WATER_STATE_UNDER`): **repaired defect of the fork** (OpenLara commit 2f7f8869 added
   the branch). The fork surfaced Lara while her room was still a water room: when `waterLevel != WALL && waterDist < 256` she went to
   the surface state, `pos.y -= waterDist - 1`, animation `ANIM_SURF`, with no reset of pitch and roll. The original has no such
   case (`docs/specs/wall/lara-and-enemies.md` 1.3.1: while her room is a water room she stays underwater whatever her height; she
   surfaces only once her position has crossed into the air room above and the water height is defined and within 256, with pitch and
   roll 0). Measured on the oracle before the repair (`$TR64_WORK/evidence/m6/surface/README.md`: 1,517 open surface cells of 14
   levels, a near-vertical ascent from each, m6-gaps3 against the repaired build; `test/game/surface.py` is the committed test):
   (1) every surfacing kept the swimming pitch (up to 85 degrees nose up) and roll while she trod water, since on the surface only the
   roll decays (`angle.x` is never touched); (2) she surfaced early, up to 255 units under the water level, a teleport to it; (3) where
   the water room has no room above, the sector's water level is its ceiling (`ItemObj::getWaterLevel`), so swimming against a rock
   ceiling within 256 of it "surfaced" her inside the rock (y above the ceiling): 84 of the 1,115 ascents that surfaced (LEVEL10B room
   29: y = 5,558 under a ceiling of 5,632). The hunk restores the original line
   `if (ROOM_FLAG_WATER(room->info->flags) || extraL->dozy) break;` and removes the branch; the branches below it already zero `angle.x`
   and `angle.z`. With it the same ascents surface through the room crossing in 1,009 of the 1,031 open cells, at the water level + 1,
   pitch and roll 0, a few ticks later; the other 22 do not surface within the 330 ticks (re-dive and drift along the floor, or pressed
   against a wall at less than 35 degrees pitch where `c_checkWallUW` stops her: no state fault). Rejected alternative: keep the branch
   and add the reset (built and measured: pitch and roll zero, but (2) and (3) remain). Port: `Lara_updateWaterState` (`game/lara.c`).
   Negative control: `test/oracle/control.sh no-hunk-29` (the fork's branch, no reset). Effect on existing traces: `GYM/swim` differs
   from tick 144 and `GYM/underwater-start` from tick 95 (she is still under water at the tick the fork surfaced her); nothing else in
   the 25 fixtures or the cutscenes.
30. `party.patch` (after `host.patch`): adds the second actor, its input/camera/flow/hazard state and join/leave/warp commands to the pinned fixed game. `docs/specs/coop.md` records the reference rules; `docs/coop.md` lists deliberate differences. The authored actor, shared world, inventory, save and cutscene paths remain single-copy.
31. `review.patch` (after `party.patch`): commit borrowed interaction state on owner retirement, gate key/puzzle and switch triggers until ready, cache stable join and warp poses (including water), and select one nearest eligible flame/lightning target. The changes preserve the original execution path when no second actor is present.
32. `settled.patch` (after `review.patch`): retire work without advancing objects out of order; replay the linked floor action when its original switch or hands-free frame is due, and hold a Scion completion request until the ring is closed. V3 recognizes consumed puzzle meshes, without changing bytes in an existing save. Co-op pushable blocks settle against the live destination floor; joining/warping revalidates cached positions and media against the active room set.
33. `wait-for-actor.patch` (after `settled.patch`): reject leave/warp while the actor owes a committed interaction. Death keeps that actor and runs only her own sector's trigger at the authored readiness tick. The ring pauses the countdown, the death page waits for it, and a level end drops old-level work. Reinitialize the pose cache after load or placement. Restore the legacy V3 puzzle rule from before hunk 32.
34. `zzz-passport.patch` (after `wait-for-actor.patch`): the passport uses one distinct blocked-save message when Lara 2 still owns committed work or a dead Lara owes an action. It does not touch checkpoint storage. The successful and ordinary failure paths, including the original GBA warning options, are unchanged.

### Control oracles

`test/oracle/control.sh NAME` builds the current oracle with `controls/NAME.patch` applied on top (`no-hunk-20`: the
load links a collected pickup into its room again; `no-hunk-24`: the falling ceiling kills at the first contact;
`no-hunk-26`: the moving bar has no controller; `no-hunk-28`: a triggered item stays invisible; `no-hunk-29`: the fork's
surfacing branch inside a water room, with no reset of pitch and roll). A control is a negative
control for a directed script, never a published fixture; the script names it on its command line.

## Cutscene levels

`--level cutN.phd` starts a cutscene level (no Lara). `--ticks` may be up to 4096 for a cutscene start
and for the four levels before one (LEVEL3B, LEVEL7B, LEVEL10A, LEVEL10B: a scene can cross the
transition and play the cinematic out; 600 otherwise), and for LEVEL4 and LEVEL8B (four keys into four keyholes; four
switches, bridges and artifacts: `oracle.cpp` `longScene`, `plat/host/game-scene.c`); the cinematic tables are 1600, 1000, 400 and 1890 frames. Their records carry
`"lara":null` and a camera object `{pos,room,targetAngleY,angle:[x,y],timer,eye}`: `pos` is the
camera origin, `eye` is `gCameraViewPos` after `updateCinematic`, and the remaining
`playersExtra[0].camera` fields (targetAngle x/z, angle z, mode, target ...) are never written in a
cutscene and keep the previous level's values, so they are not exported. `camera.timer` is also
reset to 0 when the cinematic camera starts (hunk 15); the cutscene's sound listener
(`camera.target.pos`) is inherited from the previous level and carried in the capsule.

## Input protocol

The executable's input is whitespace-separated unsigned decimal or `0x` /
`0X` hexadecimal **OpenLara `InputState`**, not platform `InputKey`, masks.
Leading zeros remain decimal (`010` means ten, not eight). Signs, `0b`,
`0o`, missing hex digits and non-digit suffixes fail. There must be exactly
`N` masks; the first applies to tick 1. Zero releases all buttons. Undefined
bits and START (1024) fail. At most 600 ticks (20 seconds) are admitted (4096 for a cutscene level, the four before one, LEVEL4 and LEVEL8B, see Cutscene levels).

| Name in scripts | Mask | OpenLara field |
|---|---:|---|
| NONE | 0 | no input |
| LEFT | 2 | IN_LEFT |
| RIGHT | 4 | IN_RIGHT |
| FORWARD | 8 | IN_UP |
| BACK | 16 | IN_DOWN |
| JUMP | 32 | IN_JUMP |
| WALK | 64 | IN_WALK |
| ACTION | 128 | IN_ACTION |
| WEAPON | 256 | IN_WEAPON |
| LOOK | 512 | IN_LOOK |
| START | 1024 | **unsupported**: debug free camera, rejected |
| SELECT | 2048 | IN_SELECT (inventory) |
| ROLL | 24 | IN_UP \| IN_DOWN |

`input.py` accepts `hold FORWARD 90`, `hold FORWARD+JUMP 30`, `tap JUMP`,
blank lines and `#` comments. `tap` is one tick and does not insert a hidden
release tick: write `hold NONE 1` if one is needed. BACK without WALK is the
fixed game's back-hop; side-steps are WALK+LEFT/RIGHT.

### Framed co-op input and schema 5

Put `coop 1` first. After the optional prelude, give two masks per tick (slot 0, slot 1). No legacy
line is inferred to be co-op from its field count; `start`, `give`, `item` and `activate` are unchanged.
Slot-1 input is ignored while absent. Put commands before the masks in ascending tick and file order:

```
join TICK
leave TICK
warp TICK SLOT
place TICK SLOT ROOM X Y Z ANGLE_Y
health TICK SLOT VALUE
hit TICK SLOT ITEM_INDEX DAMAGE
heavy TICK ROOM X Y Z
flip TICK
pit TICK ROOM X Z STEPS
join-refused TICK
leave-result TICK EXPECTED
warp-result TICK SLOT EXPECTED
save-blocked TICK 0_OR_1
```

Commands run before their tick (0 before the initial record). A refused warp consumes no tick or stock.
Join and leave must succeed; `place` must pass the legacy geometry check. `health` writes only a present
actor's health. `hit` supplies the shooter to the real weapon-hit path against one authored item (no aiming,
trigger pull or ammo spent). `heavy` submits the authored sector trigger at that point with no actor, using
the same path as a heavy object. These are placed-state evidence and need `test/game/directives.c` in the host build.
`flip` runs the real flip transaction between command dispatch and the next actor update; `pit` lowers one
live test sector by `STEPS` × 256. These isolate changed geometry between caching/commit and use without
editing a retail asset. Block translation, gravity and floor rebuild still use the production controllers.
The result probes assert the actual command return value without replacing it. Leave BUSY is 1;
warp BUSY is 3. `save-blocked` also checks that committed Lara 2 work or a pending death refuses the save;
it does not change the underlying V3 validator when no co-op action is owed.

`--trace-schema 5` (native `--game-trace-schema 5`) adds `present` and `alive` bitmasks. Instead of top-level
`lara` and `camera`, `players` has two entries, null when absent; `cinematic` holds the cutscene camera or null.
Each actor entry has `slot`, `lara`, `camera`, `flow` `[camera,active]`, `burn` `[ticks,cooldown,flameIndex,flags]`,
and `interaction` (reserved item index or -1). `ringOwner`, `useOwner` (-1 when inactive), `deathReady`,
`pendingLevel`, `completionOwed` (authored event/ring), and `retiredActions` (`[item,actorItem,ticks]`)
prefix the shared `blocks` snapshots (`[item,room,[x,y,z],status,floor]`), `effect` (`[id,timer,actorItemIndex,bounce]`)
and `pickups` (awards as `[itemType,slot]`). Occupied enemy AI records carry the last target item index and
both actors' pending `contact` masks.
The save, inventory, settings, world and RNG streams are emitted once in schema-4 order.
Schemas 2–4 and the default schema-2 state hash are unchanged.
The native schema-5 exporter is compiled only in host and `TR64_TEST` builds.
The native `--game-save-out /proc/self/fd/N` (with `test/game/save-io.c`) captures the host's bytes over an
inherited descriptor, equivalent to `--save-out` for the oracle's own format.

`test/game/coop.py` compares the full schema-5 traces of both production games and requires each case's
negative control to fail the named behavior check.
Build the host with `TR64_SCENE_TEST_SOURCES='test/game/directives.c test/game/save-io.c'`, then run
`rtk python3 test/game/coop.py --executable HOST --oracle ORACLE` with `PYTHONPATH=test/game:tools/cook`.
`test/game/coop_review.py` takes the same flags and adds separate regression and control runs for interaction
interruption, pose/medium, camera arbitration, the last shared shell and nearest lightning damage.
`test/game/coop_settle.py` takes the same flags: living leave/warp report BUSY and can be retried;
death uses the actual actor's sector, including multi-link items. It also covers both Scion paths,
block landing, immediate join after load, and the legacy LEVEL6 checkpoint decisions and bytes.
`test/game/coop_passport.py` uses the same flags and drives the real ring and passport, compares the old
save bytes after refusal, then saves and loads after the interaction settles. An idle partner is the control.

### Optional scene placement

Both readable scripts and compiled masks may begin with one directive:

```
start ROOM X Y Z ANGLE_Y
```

All five fields are signed decimal integers; coordinates are absolute
world units, `Y` increases downwards, room is a zero-based source room
index, and angle is the signed 16-bit turn (`0` = +Z, `16384` = +X,
`-16384` = -X, `-32768` = -Z). The directive consumes **no tick** and is
included in the input file's SHA-256. Omitting it preserves the retail
Lara start. Main authorized placed scenes for fall/swim; the original
level-start fixtures are unchanged.

Exactly reproduce this setup in a C99 port:

1. Call the normal level start (`gameInit` -> `startLevel` -> `gameLoadLevel`),
   including every item's normal constructor, initial inventory, derived
   room/door state, and the fixed clock seed policy below.
2. Reject a room outside the loaded array or coordinates outside its X/Z
   sector grid. Require `rooms[ROOM].getRoom(X,Y,Z) == &rooms[ROOM]`:
   a point resolving through a portal, room above or room below is rejected,
   not silently re-linked on tick 1. Then reject `WALL` floor/ceiling or Y
   outside the sector's computed ceiling/floor. Full-body/static-mesh
   clearance remains the ordinary game's collision responsibility.
3. Call upstream `resetLara(0, ROOM, (X,Y,Z), ANGLE_Y)` to unlink/place/link
   Lara and restore full health. Other state remains the normal level-start
   state, including stand animation, full oxygen and zero speed. Its
   temporary camera fields are replaced in step 5 by the game's initializer.
4. Call Lara's production `updateWaterState()` **once**, without an input
   tick. Its sector/water-depth logic derives the initial state, animation,
   pitch, room and any splash/effect/RNG changes. For a deep underwater
   start, it adds 100 to Y, sets the water-fall/dive animation and pitch,
   and enters UNDER. Surface starts are not forced: the normal UNDER ->
   SURFACE transition occurs on subsequent production ticks. Do not assign
   a water enum or choose a captured animation/frame manually.
5. Call `players[0]->extraL->camera.init(players[0])`. This uses Lara's
   resulting room and position: target is Lara minus 1024 in Y, view is
   target minus 100 in Z, follow mode and camera timers are initialized by
   the production routine. Thus tick 0 never retains the old room's camera.
6. Emit tick 0. Each subsequent mask goes through `gameUpdate(1)`.

The swim scene starts on the room-13 pool rim, performs a real swan dive
into water room 14, swims, pitches upwards with BACK, surfaces and treads.
The separate `underwater-start` scene starts in room 14 to exercise water
state derivation before tick 0. The LEVEL1 fall starts on a room-8 ledge
and falls onto the lower source floor, without a synthetic damage call.

### Optional placed-state setup: `give`, `item` and `activate` (test-only)

Beside `start`, a script or mask file may carry any number of

```
give TYPE COUNT      # TYPE: source ItemType of a pickup (126 lead bar, 129 key 1, 85 shotgun ...), COUNT 1..1000
item INDEX HEALTH    # INDEX: pool slot < itemsCount (the trace `index`), HEALTH: int16
activate INDEX TIMER # INDEX as above, TIMER 0..127: the trigger's timer field
```

before the first mask, in any order relative to `start`. They consume no tick
and are applied after the `start` placement and before tick 0. `give` calls the
real `inventory.addPickup(TYPE)` COUNT times (so weapon pickups also grant their
ammunition and keys land in their slots) and fails if the source refuses.
`item` writes that one item's `health` and nothing else; no state, animation or
AI is touched. `activate` does what the ACTIVATE_OBJECT trigger action (`room.h`, `checkTrigger`) does to the item
with every mask bit set: `timer = TIMER` (times 30 unless 1), the mask bits set, `activate()` unless it is already
active, status active; it fails on an item already used once. It exists for what no play reaches: the falling
ceilings of LEVEL3B (`burn.py ceiling`), whose only trigger is the Scion's pickup trigger, with Lara held in the pickup
animation until every slab has landed. In the oracle `give`, then `item`, then `activate` are applied in that order;
in the port in file order (a script that mixes them lists them in that order). The native host driver (`plat/host/game-scene.c`, with
`test/game/directives.c` linked through `TR64_SCENE_TEST_SOURCES`; never part of
the cart or a shipping build) implements the same two directives. With neither
directive present the oracle is trace-identical to the previous builds.
**Paths reached through them are placed-state evidence, not play-reached
evidence**: report them separately from scripts that obtain the item or the
health loss by play.

## Trace format (schema 2)

ASCII JSON Lines, LF terminated, no spaces. There are `N+1` records:
initial tick 0, then one post-update record for each tick 1..N. Properties
and arrays are serialized in the exact order listed below; all values are
decimal integers except `null` AI and the eight-character lowercase hash.
Boolean fields are integers 0/1, never JSON booleans. A vector is `[x,y,z]`;
an angle is the signed 16-bit turn, not degrees. No padding or pointers
are serialized. All pointer references are item/room slot numbers, or -1
for null. Schema 1 references are intentionally incompatible.

Top-level property order:

```
schema,tick,lara,camera,active,save,inventory,rng,hash
```

`schema` is 2. `tick` is the update count. `active` is every item on
`ItemObj::sFirstActive`, in the **real linked-list/update order**, not sorted.
Lara is normally updated separately and is not duplicated there. Inactive
items are absent. `rng` is `[gRandSeedLogic,gRandSeedDraw]` as unsigned
32-bit bit patterns. Every `lara`/`active[]` item starts with these keys:

```
index,type,pos,rot,room,anim,frame,state,goal,next,flags,health,hSpeed,vSpeed,
turnSpeed,timer,water,mood,headOffset,aggression,intensity,roomFloor,hitMask,visibleMask,ai
```

These map directly to `ItemObj` except `index` (slot in the whole pool),
`room` (room slot), `anim=animIndex`, `frame=frameIndex`,
`goal=goalState`, `next=nextState`, `water=waterState`.
`timer` is the source union (oxygen for Lara, radius for enemies);
`mood` is the same union as Lara's input; `aggression` aliases her gymTimer.
`hitMask` and `visibleMask` are unsigned 32-bit; other flags/enums/indices
are non-negative source values, positions/angles/health/speeds/timers
retain their source signedness. Water is ABOVE=0, WADE=1, SURFACE=2, UNDER=3.

`ai` is null unless the item's `extraE` is an occupied entry of
`enemiesExtra[]` belonging to this item. An occupied AI object has keys
`slot,rotHead,rotNeck,maxTurn,nav`, directly from that entry (`slot` is its
enemy-pool index). Unused `_reserved` bytes are not meaningful state.

Lara then appends the following keys, in this order:

```
oxygen,weaponState,weapon,goalWeapon,input,swimTimer,vSpeedHack,moveAngle,
hitFrame,hitTimer,hitQuadrant,hazardCause,lastInput,healthTimer,dozy,
head,torso,armR,armL,meshes,nav
```

`oxygen`/`input` are the ItemObj fields. Other scalar names are the same
fields in `ExtraInfoLara`. `head`/`torso` are their `.angle` vectors.
`meshes` is the `JOINT_MAX` mesh-index array in joint order. Each arm has
keys `angle,angleAim,anim,frame,target,aim,useBasis,flash`: direct fields
except `anim=animIndex`, `frame=frameIndex`, `target` is an item slot.
`flash` is `[timer,angle,offset,intensity]` from the arm's flash structure.

Every `nav` object (Lara or enemy) has these keys:

```
cellsCount,zoneType,weight,endBox,nextBox,headBox,tailBox,stepHeight,
dropHeight,vSpeed,mask,pos,cells
```

They map directly to `Nav`. `zoneType,weight,endBox,nextBox,headBox,tailBox,mask`
are unsigned 32-bit; heights/speed are signed. `cells` contains exactly
`level.boxesCount` arrays, by box-array index, each
`[boxIndex,weight,end,next]`. `cellsCount` is the source candidate-list count,
**not** the array's serialized length (search also indexes cells by box).

`camera` key order:

```
pos,target,room,targetRoom,targetDist,targetAngle,angle,laraItem,lastItem,
lookAtItem,speed,timer,index,lastIndex,bounce,mode,lastFixed,center
```

These are `ExtraInfoLara::camera` fields, except `pos=view.pos`,
`target=target.pos`, `room=view.room`, `targetRoom=target.room`. The three
item references are slot numbers. The remaining integers/booleans retain
their direct field values; no interpolation/presentation rounding occurs.

`save` emits every `gSaveGame` field in structure order:

```
version,dataSize,itemCount,level,track,secrets,pickups,time,distance,
randSeedLogic,randSeedDraw,mediUsed,ammoUsed,kills,flipped,tracks,invSlots
```

The uint32 time/distance/seeds/data size are unsigned decimal; `track`
is signed. `tracks` and `invSlots` each have 64 entries, including flags
and stored inventory values. These are stored save-header fields; the
current seeds are separately in `rng` and **live** inventory counts are
in `inventory`, whose key order is `counts,numKeys,useSlot,state,page`.
`counts` contains all `SLOT_MAX` signed counts in `InvSlot` enum order
(including the infinite-ammo sentinel). Other keys are direct live
`inventory` fields. Schema 2 does not synthesize a save or mutate it.

To compute `hash`, serialize the entire record **without the hash
property**, including its final `}`, but excluding LF. Apply FNV-1a-32 to
those exact bytes: start `2166136261`, for each byte `h = (h ^ byte) *
16777619` modulo 2^32. Remove that final `}`, append
`,"hash":"%08x"}` and LF. It is a record hash, not a cumulative hash and
not a substitute for field-by-field comparison. A C99 comparator should
parse properties and report the first differing tick/field; canonical
serialization also permits byte comparison. The manifest adds SHA-256
for complete traces, inputs, source levels and the tool/binary.

## Extended menu and transition trace (schema 3)

`--trace-schema 3` preserves schema 2's field order and values, with the
following additions. The top-level order is:

```
schema,tick,level,lara,camera,active,save,inventory,settings,rng,hash
```

`level` is the live numeric `gLevelID`, not the stored save-header level.
In TITLE, the source has no gameplay Lara: `lara` and `camera` are both
`null`. Its menu-input dummy is not exported as a fabricated player. A placed
gameplay start still requires a real Lara. Cross-level records retain the
scene's global tick numbering while exporting the newly loaded level's state.

After the existing inventory fields `counts,numKeys,useSlot,state,page`,
schema 3 appends these direct numeric source fields in this exact order:

```
timer,nextState,frameIndex,frameTarget,height,radius,selDist,pitch,rot,rotItem,
selRotPre,selRotX,selRotY,heightTarget,heightInc,radiusTarget,radiusInc,
pitchTarget,pitchInc,rotTarget,rotInc,rotItemInc,selDistTarget,selDistInc,
selRotPreTarget,selRotPreInc,selRotXTarget,selRotXInc,selRotYTarget,selRotYInc,
itemsList,itemsCount,itemIndex,nextIndex,optionIndex,optionsCount,optionsWidth,
optionsHeight,itemVisMask,passportPage,statsNextLevel,options
```

`itemsList` contains all 26 slots, including retained entries past `itemsCount`.
`options` contains all 32 entries, including retained entries past
`optionsCount`; each entry is `[type,str,value]`, with numeric source enum IDs.
`itemVisMask` is unsigned 32-bit. Renderer/background pointers, the inventory
Lara pointer, unused MG passport-level selection, and formatted `statsText`
are excluded. The trace serializer does not format UI strings or create saves.

`settings` is an eight-element numeric array in this order:

```
video_gamma,video_fog,video_fps,video_vsync,audio_sfx,audio_music,
controls_vibration,controls_swap
```

The canonical FNV-1a rule above applies to the entire selected schema record,
including every added field. Schema 2 remains the default byte-compatible
fixture format; schema 3 is a separate explicit comparison contract.


## World trace (schema 4)

`--trace-schema 4` is schema 3 plus one top-level property, `world`, between `settings` and `rng`:

```
schema,tick,level,lara,camera,active,save,inventory,settings,world,rng,hash
```

Neither earlier schema recorded sector, door or box state (only `save.flipped` showed a flip), so a refused
or wrongly applied door update was invisible to every comparison. `world` is `null` in TITLE, otherwise
`[sectors,boxes,flips,pairs]`, four unsigned decimals:

* `sectors`: FNV-1a-32 (start 2166136261, `h = (h ^ byte) * 16777619`) over the byte stream, 16-bit values
  little-endian: for every logical room `r` in `0..roomsCount-1` the index of the `RoomInfo` bound to it
  (`rooms[r].info - level.roomsInfo`; this is the alternate's index while the map is flipped), then for
  each of its `xSectors * zSectors` sectors of `rooms[r].sectors` (the *live*, door-closed, block-lowered
  or flipped data): `floorIndex` u16, `boxIndex` u16, `roomBelow` u8, `floor` u8, `roomAbove` u8,
  `ceiling` u8.
* `boxes`: the same hash over `level.boxes[b].overlap` (u16) for `b < boxesCount` (live blocked/blockable
  bits).
* `flips`: the same hash over the ten flip maps, `gFlipMapState[i].mask` u8, `.flags` u8.
* `pairs`: 1 when the source's own validator `worldBuildRoomPairs` accepts the world, 0 when it refuses
  (the item-list check that refuses every door update and map flip, hunk 14). The port reports
  `game_world_items_linked()`; every other check of the validator is a data-structure check that holds
  for all 21 PC levels.

The port computes the same four values in `game/trace.c` (`world`) from its live sectors, box flags and
flip maps. The hash of the whole record covers `world`; schemas 2 and 3 are byte-identical to the earlier
oracles, `--trace-schema` omitted still selects schema 2. A `world` hash that differs between the programs
names the family (`world[0]` sectors, `[1]` boxes, `[2]` flips, `[3]` verdict); finding the sector needs
a debugger (the first such difference, LEVEL3B tick 114, was a real port defect: an open door re-wrote a
flipped room's cell from the wrong room's data because the port lacked `Door::action`'s early return
when the sectors already match, object.h:757-759).

## Real source save/load boundaries

The host adapters implement an in-memory save file: a copied source `SaveGame`
header and an owned, bounded copy of exactly `dataSize` payload bytes.
`osCheckSave()` is false until a real `osSaveGame` call has succeeded.
`osLoadGame` checks the requested capacity before copying the saved header and
payload back. The pinned `gameSave()`, `gameLoad()`, persistent V3 fields and
world-state rederivation bodies are retained; only the explicitly approved
authored-species validation defect above is corrected. No full-state snapshot
or replacement save serializer is used.

Optional `--save-tick N` and `--load-tick M` call those real source functions.
Boundaries range from zero through `--ticks`: boundary zero follows normal
initialisation/placement, and each later boundary follows `gameUpdate(1)`.
At either boundary, save runs first, then load, then the trace is emitted.
A refused source operation fails with a diagnostic naming the tick and level;
there is no synthetic success or direct game-state assignment. Without these
directives the old 25 gameplay fixtures retain their default schema-2 bytes.
Ordinary menu-driven source save/load calls use the same real memory store.

Optional `--save-out /proc/self/fd/N` (or `/dev/fd/N`) exports the last
successfully stored save at normal scene completion, using the source's
existing `saveEnvelopeSerializeV3`: its actual ILP32 host `SaveGame` header
followed by its actual payload. Only inherited descriptor paths are accepted.
No save bytes are fabricated if no save occurred; an unsuccessful scene is
not required to produce this optional output. Capture bytes in a pipe or
memfd. A bounded `save-report` line on stderr records each successful save's
scene tick, byte count and FNV-1a fingerprint over the header plus payload;
consumers can calculate SHA-256 directly from the exported bytes.

These source envelope bytes are not claimed to equal the native port's
explicit big-endian container byte-for-byte. Compare persistent semantics and
post-`gameSave`/`gameLoad` tick records against the source, and check the
native save-load-save bytes for native round-trip stability. The source save
format deliberately omits transient state; equality to an unsaved simulation
continuation is not a save/load acceptance criterion.

Source V3 does not save settings or all inventory-ring transients. In-process
loads retain the current settings and eight ring fields: `height,heightTarget,
itemIndex,itemVisMask,itemsCount,itemsList,nextIndex,rotItemInc`; normal level
preparation determines the others and `gameLoad` finally resets page and state.
Compare runs with identical menu history, or carry those source-defined fields
on the native side. A fresh boot plus load is not the same history as opening
menus and then loading; do not manufacture extra V3 fields to hide that fact.

## Fixture and verification contract

`run.sh m2-start` writes fixtures and `manifest.json` under
`$TR64_WORK/fixtures/oracle/m2-start/`. Each of LEVEL1/GYM has idle 60,
run-forward 150, walk 90, left/right turns 60 each, jump-forward 90,
back-hop 60, side-left/right 60 each, roll 60 and wall 450 ticks. There are
also the LEVEL1 placed fall (120), GYM dive/swim (230) and GYM underwater
start (155). These are scene windows, not playthroughs. Neither LEVEL1
nor the gym from-level-start movement windows reaches water. No claim is
made that all routes from the original start have been searched.
Existing references are immutable by default. The runner stages all new
files in private build scratch. Before publishing it compares the previous
manifest's schema, tool hash, build key, built revision, scene set, per-level
hash and per-input hash, checks stored files still match, and requires every
regenerated trace hash to match. A mismatch fails naming what changed and
requires explicit `--update`; `--update` deliberately replaces references
only after generation and requested verification succeed. Failed runs do
not overwrite previous fixtures. A fixture lock serializes publishers.

`--verify` runs each fixture twice, then recompiles a fresh pinned export
and runs every fixture again, requiring byte-identical traces. It checks
idle frames with constant position/rotation and STOP state throughout,
STOP -> RUN state and changing animation, WALK moving
less than RUN over equal ticks, opposite turn signs, upward airborne jumps
and landing, backward back-hop displacement, opposite lateral steps, a
half-turn roll, stopped wall movement, damaging falls, swim and underwater
setup. All gate checks raise explicitly (also under Python `-O`).
The negative control changes only input tick 31 of LEVEL1/run-forward
(adds LEFT), runs the same executable/parser path, compares complete trace
lengths, requires records 0..30 identical and hashes 31..150 different,
and also requires changed rotation and final position.
Production protocol probes require decimal/hex agreement and rejection of
START, malformed integer spellings and wrong-room scene placements.
It does not merely hash the changed input file or run a test-only simulator.

## Deliberate exclusions and feedback caveats

* No pixels, visibility traversal, draw calls, lighting or audio mixing.
  Render backend calls are no-ops and output-only sphere visibility is
  false; game draw routines are never invoked. Camera follow/collision
  still runs in Lara's real update. The independent review found no
  visibility feedback into logic, no per-frame render RNG draw, and no
  caller using soundPlay's return value. This is nevertheless not a
  rendering or audio-playback parity gate.
* `soundPlay` itself and its RNG consumption are preserved. Sample play
  returns null; no sample channels advance. Music play records the track,
  but a started track never finishes except through an explicit stop.
  Thus music completion, ambient restart timing and any tutorial/cutscene
  logic depending on real playback duration are **not** covered. This is
  not a complete audio-equivalent world state. No test disables the game
  sound setting to hide RNG differences.
* The clock always returns 1 ms: `startLevel` seeds logic=3 and draw=7
  before item initialization (which can consume them). The renderer's
  initial random table is initialized normally, then the level reseeds.
  No wall clock, platform input, vibration, loading screen, video or settings
  file is used. Save tests use the real source V3 memory store described above,
  not an external platform save file.
* The immutable 25-fixture M2 bundle does not itself claim inventory, death-menu,
  campaign, cutscene, enemy-combat or save/load coverage. Those require separate
  streamed input cases; the old bundle is not rewritten for new oracle features.
  Cross-level loads use the actual canonical sibling PC files or fail with the
  missing filename. Neither trace schema is a complete save-state: inactive
  items, all room topology/trigger state and effect-specific extra payloads are
  not all serialized. Equal records do not prove equal complete simulation state.
* **PlayStation files: no.** The pinned `src/fixed/fmt/` has a bounded PHD
  reader and a PKD reader but no `psx.h`, although dormant format bits and
  a conditional include remain in `level.h`. The platform selects only
  PHD; passing a retail PSX level is rejected by its non-0x20 header before
  the PC parser. The separate cook reader is not silently substituted.

The exporter and patch carry BSD-2-Clause notices because they use
OpenLara declarations, call order and host harness patterns. The full
notice is in `LICENSE-OPENLARA` here; `game/LICENSE-OPENLARA` is the canonical
project notice named by provenance policy. Build/input/fixture orchestration
is original MIT code. All retail-derived artifacts remain outside git.
