# Lara simulation gates

Use a full native PC pack containing `level1` and `gym` as `PACK`. No PC data
or cooked output is stored here. These are the same engine/build, with content
and scene metadata as inputs, not source-specific simulator variants.

```sh
rtk make host BUILD_DIR=$TR64_WORK/builds/Game/level1 TR64_LEVEL_PACK="$PACK"
rtk python3 test/game/fixtures.py --executable $TR64_WORK/builds/Game/level1/host/tr64-host --level level1 --out $TR64_WORK/tmp/Game/level1 --negative
rtk make host BUILD_DIR=$TR64_WORK/builds/Game/gym TR64_LEVEL_PACK="$PACK" TR64_RENDER_SCENE=test/game/gym.scene.json
rtk python3 test/game/fixtures.py --executable $TR64_WORK/builds/Game/gym/host/tr64-host --level gym --out $TR64_WORK/tmp/Game/gym --negative
```

The GYM metadata is mandatory for the existing bind-time posed-frame loader;
it is replaced by the real follow camera immediately after game start. It
never places Lara. Ordinary M1 camera/level checks remain strict.

`fixtures.py` verifies the unchanged oracle manifest/revision and source,
input and trace identities, then compares complete canonical records in the
controller order. Its negative changes only input tick31 and requires the
first difference there, persistent changed hashes, final position/rotation
changes, rejection by the cart hash predicate, and rejection of a missing field.

After each corresponding level build above, run the broader regression:

```sh
rtk proxy env PYTHONPATH=tools/cook rtk python3 test/game/random-inputs.py --executable $TR64_WORK/builds/Game/level1/host/tr64-host --level $TR64_DATA/pc/level1.phd --count 200 --seed 61 --out $TR64_WORK/tmp/Game/random-level1
rtk proxy env PYTHONPATH=tools/cook rtk python3 test/game/random-inputs.py --executable $TR64_WORK/builds/Game/gym/host/tr64-host --level $TR64_DATA/pc/gym.phd --count 200 --seed 61 --out $TR64_WORK/tmp/Game/random-gym
```

Set `PYTHONPATH` once as shown; no import-path mutation is performed. The
oracle and native game write to separate inherited trace pipes, not stdout
or trace files. The fixture gate also receives its native trace through a pipe;
only bounded process logs and small input/report files are retained. Exact byte
equality is the random gate's fast path; a differing record is parsed to report
the first tick/field. A native stop is accepted only for an explicit named
not-ported boundary, after every preceding record matched.
Unexpected exits, timeouts, malformed output and any divergent record fail.
Rejected placements are reported separately and do not consume the 200-case
quota. `--case INPUT.masks --count 0` runs a targeted 600-tick regression.

`ledges.py` enumerates raised adjacent source sectors, following horizontal
portals, stacked floors/ceilings and their slants. It exercises 240–320-tick
vault, jump/grab, handstand, diagonal-action and roll-action inputs at three
distances and three small facing offsets, plus explicit water-surface
climb-out inputs. Reports distinguish rejected placements, exact comparisons,
first differences and independent suspicious oracle vertical movements; no
trace files are written. Oracle steps over 1024 units fail independently of
port equality. Ending above an enumerated sector is also flagged for review:
item surfaces such as the room-14 bridges can legitimately raise Lara above
that underlying sector, so height-only flags are not assertions of a defect.
GYM water exits can reach the tutorial's level-end trigger after climbing out.
A matching 200-plus-tick prefix followed by both programs' explicit tutorial
transition boundary is reported as named-loud, not as a completed equal script.
Other unexpected exits remain failures.

```sh
rtk proxy env PYTHONPATH=tools/cook rtk python3 test/game/ledges.py --executable $TR64_WORK/builds/Game/level1/host/tr64-host --level $TR64_DATA/pc/level1.phd --rooms 14 --out $TR64_WORK/tmp/Game/ledges-room14
```

Omit `--rooms` to enumerate the whole source level. `--representative` selects
the first edge of each facing/height/clearance/slope/water class, reporting both
the enumerated and selected counts. Each level requires its matching native
scene metadata, as above. The first north-facing room-14
ledge is a regression for the end-of-climb animation offset: placing Lara
at `(14848,5888,47200)`, yaw `-32768`, holding `FORWARD+ACTION` must reach
`Y=4352` at tick 105, not jump to `Y=-28171`. The signed rotated Z operand
must be arithmetically shifted (OpenLara `src/fixed/item.h:323-327`).
The same placement also exercises cross-room drawing before the position snap:
at ticks 90–103, Lara's root remains in room 14 while the climb pose rises into
camera room 13. Her body must remain visible, not be cropped to room 14's portal
rectangle. This is a separate picture check, not a trace-only sweep claim.

`--game-trace-only` skips frame presentation during these controller-only
regressions. It calls the same native loader and `game_tick`, with no alternate
simulation; rendering and performance are tested through actual live frames.
For pictures, omit it and use `--game-shot FILE.ppm --game-shot-tick N` while
running the full input window. `routes.py` converts masks to ordinary v3 game
routes; run those with `test/run/scene.sh --ares --checks test/game/scene-checks.py`.
The cart observable compares every tick against a verified host trace, and
reports actual game tick time separately from the renderer's completed frame.

## Running a suite on the remote host

Every suite here (host build, oracle, directed scripts, campaigns) runs on
`<remote-host>` (12 cores) with one command; the tree, `$TR64_DATA` and the
published oracles in `$TR64_WORK/fixtures` follow, and the suite's own
`$TR64_WORK/...` paths resolve there unchanged:

```sh
rtk test/run/host-remote.sh --sync "$PACK" \
  --pre "make host BUILD_DIR=$TR64_WORK/builds/Game/level1 TR64_LEVEL_PACK=$PACK" \
  -- test/game/random-inputs.py --executable $TR64_WORK/builds/Game/level1/host/tr64-host \
     --level $TR64_DATA/pc/level1.phd --count 200 --seed 61 --out $TR64_WORK/tmp/Game/random-level1
```

Output streams back, the exit status is the suite's (a 200-case random run
printed byte-identical lines here and there). `--fetch PATH` brings an output
directory back; see the header of `test/run/host-remote.sh`.

## M3 integration inputs

`campaign-pack.py` cooks GYM and the fifteen pinned `tr1_campaign.h` gameplay
levels into one full native PC pack. Run with `PYTHONPATH=tools/cook`; provide
`--textures` naming a measured immutable TX64 section tree, `--cache` naming
the compressor cache and `--out` outside the repository. It verifies source
and texture hashes, descriptor maps and every decoded Core record. ABI 3 packs
include mesh-slot collision spheres; older packs are rejected by the loader.
The producer also cooks and includes real SN64 for every campaign level; sound
is not an optional silent-pack mode. Its immutable content identity includes
sound/Core/geometry producers and all source/section hashes. A sound-enabled
engine requires this pack, not the preserved M3 evidence pack without SN64.

The resident `GameItemCall` packet selects construct/activate/update/collide/
hit/geometry operations without retained unit-code pointers. Constructors,
AI slot lifecycle, navigation, mutable box flags, collision helpers, inventory
counts and effect constructors stay resident. Eight enemy slots own eight
bytes per navigation cell per level box. Family updates and object controllers
are grouped into paged units by source-level co-occurrence. The SDK directory
currently admits 32 units; linked unit sizes, rather than source object
estimates, determine actual use of the unchanged 24-frame code pool.

Random scripts include one-tick WEAPON inputs and held ACTION inputs. Reports
include `silent_difference` and grouped `loud_reasons`; a failed comparison
retains its first error and current masks, never a trace file. A source-code
port or a successful build is not an oracle-equality claim: report each exercised
level/input domain separately. Inventory use-page and level-transition UI remain
named boundaries owned by M5.

The M5 menu contract keeps source mutable ring/options state resident (764-byte
`GameInventory`, plus 32-byte settings), with only numeric StringIDs and typed
statistics in the caller-owned `game_menu_view` snapshot. Ring bones are pulled
per item with `game_menu_item_view`. Resident `game_menu_text_view` supplies
StringIDs, raw glyphs/numbers and source positions without dispatch or formatted
statistics text. Its STATS_BLOCK uses the snapshot's typed statistics and a
16-pixel line height. Font widths are immutable StringID-indexed UI data.
Inventory operations use a resident operation packet; SAVE resumes the same
update tail after the actual storage callback, without adding a simulation
tick. Missing platform services remain named boundaries, not failed-save fakes.
`inv` and `save` are preloaded in the normal level loading scope.

`game_save` writes only source-V3 persistent fields into the explicit-endian
TRS3 envelope. A size query or insufficient buffer writes nothing; a successful
write commits the source save header. `game_load` validates before mutation,
reconstructs items/AI/navigation and derived sector/door/block/flip state, and
retains current inventory history and settings exactly as the source does.
There is no complete-state snapshot or compression path. Saves are at most
6,050 bytes across the sixteen playable PC levels. The real-engine
`save-measure.route` with `save-measure.c save-check.c` checks sizes, guards,
malformed-data nonmutation, repeated world rebuilding and save/load/save bytes.
The independent streamed schema-3 gate also compares actual save/load against
the oracle on all sixteen levels plus settings-transient and cross-level-load
history cases: 18 scripts, 601 records each. Four additional menu scripts cover
open/close, ring spin, passport pages and detail options. The unchanged 25 M2
fixtures and production-input/hash/missing-field negative controls remain equal.
These are exercised domains, not a claim that every possible menu path or the
stock-cart presentation/storage backend has been verified.

Schema 3 adds source inventory, options, settings and level fields; canonical
state hashes retain schema 2. The host driver accepts `--game-trace-schema 3`
and optional `--game-save-tick N --game-load-tick M`, with save before load and
both before that boundary's trace. The explicit save-I/O test adapter provides
the RAM slot; normal builds do not invent a storage backend.

Directed input gates may pass an `observe` callback to `compare_case`. It sees
only records already proven equal and accumulates bounded coverage metrics such
as actual damage, healing and fired shots. It cannot turn a different record
into a pass; callers keep coverage assertions separate from equality verdicts.

## Drawable producer

`game_view_camera(out)` publishes only camera and texture-animation clock state.
The renderer pulls `game_view_room(room, &cursor, buffer, capacity)` only for
rooms it needs. The caller allocates `TR64_GAME_DRAW_CHUNK` (64) drawables once:
4,352 bytes rather than a worst-case world list. Start each room cursor at zero;
`TR64_GAME_VIEW_END` terminates it. Chunks preserve complete, contiguous item
groups and stable `(instance_id, bone)` identities. An item larger than capacity
returns its required count without writing the buffer or cursor; a partially
filled chunk leaves the next item for the following call. Invisible and
intentionally undrawn items are skipped before matrix work. Lara, effects and
sprites are all linked through their owning rooms. Mesh slots retain Core
aliases and sprite resources are native TX64 descriptors.

`game_rest_item_view(coreItemIndex, buffer, capacity)` exposes a source Core
item's first model frame, without running its controller or claiming AI parity.
Non-model queries fail explicitly; intentionally undrawn source model types
return zero. Ordinary live views include armed Lara poses, generic extra bone
rotations and source owner-joint muzzle flashes. `texture_animation_step` counts
actual source rotations (first at simulation tick six), frozen while the
inventory controller owns the tick.

`game_hud_view(out)` is a read-only source `drawHUD` snapshot: signed health/air,
maxima 1000/1800, health visibility from `healthTimer || weapon READY`, and air
visibility for SURFACE/UNDER. Both bars are hidden while inventory owns drawing.
It allocates nothing, draws no RNG and invents neither flashing nor always-on
health. An unloaded/NULL query returns zero without writing output. The real
inventory-transition gate also exercises visibility combinations and checks that
querying the HUD leaves the simulation hash unchanged.

`game_hud_text_view` exposes the fixed source's N64 ammo-HUD policy from
`src/fixed/draw.h:1098-1125`, without changing simulation or oracle behavior.
It returns a raw numeric label at x=212, y=20 (30 with oxygen), with raw glyph
suffixes 103/104/105, only for a ready finite-ammo weapon outside inventory.
`game_stats_view` is resident and copies the same source statistics/title/secret
maximum used by the inventory snapshot, without dispatching the inventory unit.

Pickup notifications add an explicit presentation reference: non-fixed
OpenLara `src/lara.h:3197-3228`, `src/ui.h:766-778,811-864` and
`src/format.h:2574-2617`, all at
`2333adef30ae2a5f3b32c5355e6e343d6aa0531a`. The actual three fixed collection
sites publish immutable type/origin events after success and before removal.
`game_pickup_event_count` and indexed `game_pickup_event` expose that tick's
events, never reconstructed inventory-count differences. The bounded source
state costs 3,592 bytes; events are not saved and add no trace/hash fields.
`game_pickup_item_view` supplies the converted inventory model's first frame
with fixed -15-degree X tilt and caller Y rotation. UI owns source-derived
five-second timing, layout, quartic fade/exit and projection of the event origin.

Normal-only ring/icon meshes use the native MG64 neutral-darkness baseline
4096. Fixed inventory lighting supplies source ambient `255*32-4096 = 4064`;
the drawable carries the converted native delta `4064-4096 = -32`, not raw
ambient 4064. World lighting is unchanged. The actual icon query proof checks
this conversion; raw equality of source ambient and native delta is incorrect.

The real `presentation.route`/`presentation.c` probe replays the qualified
LEVEL3A shotgun pickup/use script: an actual collection event, copied authored
origin, roomless inventory bones, insufficient-capacity/nonmutation checks and
finite-ammo HUD output. It checks that presentation queries leave game/RNG hashes
unchanged. Use `TR64_PRESENTATION_INPUT` for the input and select
`test/game/presentation.c test/game/save-io.c` through the ordinary scene build.

Directed use-gate mapping (`inventory-use-inputs.py`, always schema 3):

| Case | PC level |
|---|---|
| shotgun | level3a |
| magnums | level6 |
| uzis | level8c |
| small-medikit-after-damage | level4 |
| big-medikit-after-damage | level8b |
| small-medikit-full-health | level1 |
| big-medikit-full-health | level1 |

```sh
rtk proxy env PYTHONPATH=tools/cook rtk python3 test/game/inventory-use-inputs.py --executable "$BIN" --oracle "$M5_ORACLE" --level $TR64_DATA/pc/level3a.phd --case shotgun --out $TR64_WORK/tmp/GameLead/shotgun.masks
```

## Frozen campaign differential batch

`campaign.py --pack PACK --executable BIN --out OUT --triggers INPUT_DIR`
compares one already-built immutable production executable across all sixteen
levels: 100 seeded 600-tick placed scripts and every matching source-trigger
input. Build BIN with the declared PACK and the explicit `save-io.c` test
provider; that adapter supplies real source menu save/settings services.
Use `PYTHONPATH=tools/cook`. The schema-3 alphabet includes one-tick SELECT,
navigation, ACTION, JUMP/cancel, WEAPON and inputs after falls/deaths.
`--legacy` additionally regenerates the unchanged original M3 seed/alphabet as
a separate column, so old named stops are checked rather than inferred from a
different input sample. No per-level build or copied executable is needed.

The batch continues after a first difference so every level gets its full
sample. `report.json` and `table.md` retain equal/named-loud/silent counts,
grouped full loud reasons, first tick/field and each input's identity/result.
Reference assertion boundaries and unexpected process failures are separate
counts, never equal passes. Initial production-path runs record state,
resident image and actually loaded unit bytes. The executable, declared pack
and oracle hashes must stay unchanged through the complete batch.

Flow navigation deliberately uses the source `WALL` sentinel (`-32512`) as
step height and its negation as drop height. Replacing these with ordinary
positive/negative sector heights changes the source navigation queue.

The batch's zero-tick initialization and controller comparisons are trace-only;
its state peak excludes caller-owned drawable and explicit rest-fixture scratch.
Full-frame memory and presentation are checked separately on the cart. Cart
route generation accepts every campaign name. Its reference path is made
absolute without dereferencing symlinks, allowing a live
`/proc/<producer-pid>/fd/<fd>` memory-backed trace to remain off disk until the
scene checker finishes.

Campaign publication checks `pack.level_layout(native)` only after Core, TX64,
RG64 and MG64 have all been concatenated for each level. The immutable pack
manifest records Core-tool, compressor and producer identities. Batch memory
metrics use the final pool row, not the initial boot row. A malformed record
reports which producer emitted it and retains both stderr tails, so a reference
assertion is not mistaken for a native field difference.

## Structured engine stops

After each `game_tick`, engine callers query `game_stop_info()` before emitting
a hash, trace or frame. `NONE`, `DEATH`, `LEVEL_END` and `NOT_PORTED` distinguish
normal simulation, the source death-page input boundary, source campaign
transition ids and named missing UI/controllers. The 112-byte resident DTO copies
the name rather than retaining unit rodata. A latched stop does not advance on
subsequent ticks. `death_ready` exposes the completed source death page without
changing the input-triggered simulation stop; live-engine automatic restart is
an explicit caller policy, not a second simulation path.

`game_level_stop()` is called between frames outside loading/dispatch/frame
scopes. It owns a balanced loading scope, unloads only game units, clears `gs`
and returns idle. The engine then releases renderer/resources and its level
arena; the game never resets engine pools.

Ordinary host/route test callers convert a stop to its original named fatal
boundary before writing another record. The serializer also rejects stopped
state before emitting any bytes. `--game-stop-report` exercises the engine
policy instead: report the typed stop, check its stability, release game units
and exit successfully. `stops.py --executable EXE --level PC_LEVEL --out REPORT
--case KIND:NEXT_ID:MASKS` compares that real production input's entire emitted
prefix against the oracle, then requires the default test policy to emit the
same prefix and named failure. All trace transport remains in pipes.

`cart-reference.py` first compares every host/oracle record through independent
pipes, validates their canonical hashes, and holds the verified trace in a
Linux `memfd` while `scene.sh --ares --checks test/game/scene-checks.py` runs.
The descriptor is closed after the checker finishes; only input identities and
trace digest metadata are retained. It explicitly disables the Expansion Pak.
Fixture process logs retain 64 KiB so the complete 600-tick hash stream remains
available to verifier-negative controls; random diagnostics retain an 8 KiB
tail. Neither path writes a JSONL trace to disk.

Pass `--fixtures` to the campaign batch to run both immutable M2 fixture sets
and their production-input/hash/missing-field negative controls using the
same already-built executable, without another pair of build trees.

## Inventory across a real level end

Before releasing a source-requested level, the engine calls
`game_inventory_capture(&carry)`. The 1,040-byte caller-owned in-process capsule
stays outside the old level/state pools. It carries counts, live ring history,
the source header/settings and diagnostic tick; it is never cartridge save data.
`game_level_start_transition(core,name,&carry)` uses the one game initializer
and applies the pinned `Inventory::applyLevelTransition` before constructors.
This includes weapon confiscation on entering LEVEL10A and HOME metadata.
Restart/start retain source global history; only TITLE to a new game resets the
header and initial loadout. `stop.transition` distinguishes campaign, restart,
new game, TITLE, GYM and LOAD. For a selected saved level, the caller sets
`carry.transition=TR64_GAME_TRANSITION_LOAD`: load preparation must not apply
campaign confiscation or a new-game reset. Debug fresh starts still use
`game_level_start`. Source cut ids are not silently skipped.

The real-engine gate loads LEVEL1, carried LEVEL2, carried LEVEL10A, fresh LEVEL1
and fresh GYM. It checks all 26 slots, quest totals, alias-safe source policy,
TITLE metadata, invalid-input nonmutation and complete releases:

```sh
rtk proxy env BUILD_DIR=$TR64_WORK/builds/GameLead/inventory-transition TR64_LEVEL_PACK="$PACK" TR64_PLAY=0 TR64_SCENE_TEST_SOURCES=test/game/inventory-transition.c rtk test/run/scene.sh --host test/game/inventory-transition.route
```

Targeted differential `--case` runs also record each input hash, status,
record count and named reason in `report.json` under `targeted`. Random quota
counters remain separate, so `--count 0` cannot hide targeted results.

The direct host driver uses the same optional sound lifecycle as common routes:
boot after GPU setup, level start after game placement, and one `snd_tick`
followed by `snd_pump` immediately after every actual game tick, including a
terminal tick. Stop-report diagnostics stop sound before releasing game state.
These hooks do not move sound selection or consume another game RNG draw;
decoder/mixer verification belongs to the sound module's actual audio gates.

## Source V3 save/load input comparisons

`test/game/save-io.c` is an explicit test provider, not a shipping storage
fallback. Its charged static 8 KiB slot stores the real `game_save` result;
`has_save` becomes true only after a successful write, and loads call the real
`game_load`. Settings saves copy the actual public eight-scalar settings view.
Vibration is counted and acknowledged without claiming hardware haptics.
The provider deliberately includes public `game.h`, not the weak declarations
in `eng.h`; its four `eng_game_*` definitions must link as strong symbols.
A provider conflict is an error, never a reason to suppress a linker diagnostic.

Use one soundful gameplay/TITLE pack and one production executable for all
cases. Select the provider only through the explicit scene-test source list:

```sh
rtk make host BUILD_DIR=$TR64_WORK/builds/Game/save-inputs TR64_PLAY=0 TR64_LEVEL_PACK="$PACK" TR64_SCENE_TEST_SOURCES=test/game/save-io.c
rtk python3 test/game/save-inputs.py --executable $TR64_WORK/builds/Game/save-inputs/host/tr64-host --matrix "$MATRIX" --oracle-report "$SOURCE_REPORT" --oracle "$ORACLE" --out $TR64_WORK/evidence/m5/native-save-inputs --negative
```

`MATRIX` names the published source `case-matrix.json`; `SOURCE_REPORT` pins
the real source executable and SHA-256 (`verification.json` or
`save-verification.json`). `ORACLE` explicitly selects the retained executable
if the report's original build-scratch path was cleaned; it must still match
the report's exact SHA-256. Inputs/goldens remain in
`$TR64_WORK/evidence/m5/oracle-45191e872b47fcd8587d5a66a3c10ec34f0c93656042ff70fa073b3ab5a0661b/`.
The newer source report is
`$TR64_WORK/evidence/m5/oracle-2f6e7ef59ea1cafc372cd41f35e828a9784003170f80386f8970a170d24cdbcd/verification.json`;
its retained executable is
`$TR64_WORK/fixtures/oracle/m5-source-save-2f6e7ef59ea1cafc372cd41f35e828a9784003170f80386f8970a170d24cdbcd/tr1-oracle`.
The matrix covers all sixteen gameplay levels and its awkward/history cases.
`--case level/name` limits a diagnostic run without claiming the complete gate.

The runner uses independent `stream.Run` pipes and compares all 601 canonical
schema-3 records, including their hashes and full menu/settings fields. It
reports the first differing tick and field; a nonzero process, short trace or
early EOF fails. Boundary zero is after initialization/placement; later
save/load boundaries follow the normal game tick and precede trace output.
The changed-production-input negative must differ at tick one.

Only a small report and the negative input are retained; no JSONL traces are
written. Native save-load-save byte stability is checked separately by
`save-check.c`; equality to an unsaved continuation is not the source V3
contract. The runner never builds per-level binaries or substitutes game state
for real save/load operations.

## Cart hash scenes with menu, save/load and level transition

`cart-reference.py` runs schema-3 scenes: the oracle and the host executable are
compared record by record in pipes, the verified hash stream is held in a
`memfd`, and the same masks run as a route on ares at 4 MiB. The route selects
`hash-schema 3`, so the cart's per-tick `game_state_hash_schema(3)` must equal the
oracle record's hash over the whole inventory/options/settings/save state.
`test/game/cart_actions.c` is the scene's `tr64_scene_game_boundary` (tick 0
after placement, then after each ordinary `game_tick`): it calls the real
`game_save`/`game_load` through the save-io provider at `--save-tick/--load-tick`
(written to the scene build's `generated/cart_scene.h`), and on a `LEVEL_END`
stop it captures the carry capsule, releases the Core, loads the next one,
starts it through `game_level_start_transition` and returns 1 so the engine
replays the same input once. It never ticks, renders or pumps sound. The bind-time
pose names only the first Core (`scene-meta.json` is generated from the `start`
line); later Cores are loaded in live-renderer mode. The route also requires the
fixture's `game-service:` log lines at the oracle's transition/save/load ticks, in
order (one `observe log`; a route admits only one). `reference.json` records a
coverage summary from the verified trace: first ring open/close tick, minimum and
final health, `mediUsed`, every inventory count change.

```sh
rtk python3 test/game/cart-reference.py --executable "$HOST_EXE" --oracle "$ORACLE" --pack "$PACK" \
  --build-dir "$CART_BUILD" --out "$OUT" --schema 3 --level $TR64_DATA/pc/level4.phd --masks level4-medikit-menu.masks
```

Add `--save-tick N --load-tick M` for a save/load scene. `HOST_EXE` must be a host
build with `TR64_SCENE_TEST_SOURCES=test/game/save-io.c` and the same `PACK`. Set
`ARES_TIMEOUT` (default 90 s) high enough for a loaded host. The three retained
scenes are `level4` open ring/use small medikit/close, `level1` wolf-and-drawn-weapon
save at 100/load at 200, and `level1` settings change then walk into the end trigger
(LEVEL1 to LEVEL2 with carry); inputs and the measured summary are under
`$TR64_WORK/evidence/m5/cart-scenes/`.

## PC and PlayStation levels under the same game

The oracle reads PC levels only. `editions.py` runs two production host
executables built from identical sources, one with a PC-cooked pack and one with
a PlayStation-cooked pack, on the same inputs and compares every schema-3 record
(streamed, drained to EOF, nothing stored). Inputs are the frozen campaign
report's cases (all equal to the oracle on the PC pack) and the qualified/matrix
save-load cases, with the real `save-io.c` provider. Startup logs give each Core
table's count/bytes/hash on both sides so a difference can be tied to a table.
`--self-test` exercises the verdict logic; `--negative` requires a changed input to
be rejected at its first effect.

Measured result (frozen executables and packs named in
`$TR64_WORK/evidence/m5/editions-core-packs/report.json`; 3,529 cases: the
3,383 oracle-equal campaign scripts, 128 qualified save/load cells and the 18
source-matrix cases): 3,306 equal, 223 different, 0 errors. The 223 are all
explained by named table entries: all 222 level8a cases (PlayStation inserts one
`ITEM_MUTANT_3` at `items[33]`: Lara is item 72 vs 73 at tick 0, and the extra
`enemy.c` constructor takes one more `rand_logic()` draw, 15 vs 14, so the
logic RNG differs from tick 0), and level10a `random-90` at tick 351
(`camera.pos[1]`, 2 units: room 90 sector 30 has a sloped ceiling in the PC level,
`floor_data[8127..8130]` with ceiling -28, and a flat -32 ceiling with no floor
data in the PlayStation level; the first differing camera `checkWall` query is
`x=76829 y=-7504 z=48606`, ceiling -7802 vs -8192). Lara never differs there.
`editions.py` does not normalise anything; the level8a analysis
(`level8a-normalized.py` beside the report) drops `rng`/`hash`, maps the PS item
index and removes the inserted item, and finds 95/222 cases fully equal; the
others first differ at items that draw from `rand_logic()` (ricochet effect 94,
crocodiles, puma, mutant) or at the save header's item count.

## Cutscene levels (CUT1..CUT4)

`game/cut.c` ports `Camera::initCinematic/updateCinematic`, `CinematicObject` and the cutscene half of
`updateItems`: no Lara, the active CUT items animate, the Core's `cinematic_frames` table moves
`playersExtra[0].camera` (target/camera XYZ; the table's FOV and roll are not used by the fixed
game) and `nextLevel(id+1)` is requested when the table ends; input has no effect (no inventory, no
`updateInput`). Music is the level ambient 23, 25, 24, 22 (CUT1..CUT4); `applyLevelTransition` and
`initNewGame` are no-ops for ids the economy does not admit (the cutscenes). The in-level
`CAMERA_MODE_CUTSCENE` (Scion/Midas) no longer stops by name: the view stays where the trigger
put it. `cuts.py` (needs the 21-level pack from `campaign-pack.py`) compares the cutscenes with the
oracle: `full` (each cut from its start for its whole table, zero and random input; the last record
is the next campaign level's first tick), `enter` (LEVEL7B and LEVEL10A placed on an END trigger:
600 ticks across the transition), `finish` (LEVEL8C and LEVEL10B at the Scion pickup that plays
the `FX_FINISH_LEVEL` animation), `larson` (LEVEL3B to CUT1 to LEVEL4, below) and `negative`.
`Camera_initCinematic` sets `camera.timer = 0` (oracle hunk 15: the source kept the previous level's
timer, -1 after LEVEL3B, and read `cameraFrames[-1]`, the palette tail); the carry capsule holds only
the sound listener (`camera.target.pos`), which `game_sound` reads in a cutscene.

LEVEL3B ends only through Larson's death animation (`FX_FINISH_LEVEL`). Larson (item 30, flags 256,
hidden) is activated by one ACTIVATE trigger: room 10 at (43520, 58880); that trigger type has no
height condition, so it fires on the first tick Lara's centre is in the pool column (room 10 is the
water under the room-11 floor opening). `larson` places Lara in room 10 below it, with `item 30 1`
(placed-state evidence: his health 1), swims out, draws and shoots him; he dies at tick 382, LEVEL3B
ends at tick 530, CUT1 runs its 1599 further ticks and LEVEL4 follows: 2284 records equal. This path
needs `test/game/directives.c` linked beside `save-io.c`. Scenes of the four levels before a cutscene
(and of the cutscenes) may have up to 4096 ticks in the host driver and the oracle (600 otherwise).
The cart carries a cutscene as an ordinary schema-3 hash scene (`cart-reference.py` with `--scene-meta` naming
the cutscene, since a cutscene has no `start`): CUT3 for 400 ticks, whose last tick crosses into LEVEL10B through
`cart_actions.c`'s transition, and for 399 ticks (a picture of the cutscene itself).
`negative` requires the port one tick short, a changed input and the wrong cutscene's level to be
rejected.
```sh
rtk proxy env PYTHONPATH=tools/cook rtk python3 test/game/cuts.py --executable HOST --oracle ORACLE --out OUT full|enter|finish|larson|negative
```

## Directed scenarios for what random walking never reaches

Run all of these with `PYTHONPATH=tools/cook:test/game`, one production host executable
(built with `TR64_SCENE_TEST_SOURCES="test/game/save-io.c test/game/directives.c"`) and an
oracle that has the `give`/`item` directives (`test/oracle/README.md`). `directed.py` is the
shared driver: it writes one `start` placement plus masks, runs oracle and port on it, and compares
every schema-3 record through pipes (nothing is stored). The oracle (40 ms a run) is also a
simulator: scripts are built closed-loop from its real state (walk to a point, pick an inventory
item through the ring, wait for an animation), then compared from tick 0. `DIRECTED_SAVE=DIR`
keeps each compared script, `DIRECTED_SAVE_ONLY=1` writes them without comparing (the result status is `saved`, never `equal`: exporting it by accident once made a whole schema-4 rerun compare nothing), and
`coverage.py replay [--schema N] [--saves]` runs saved scripts (with a real save and load at three points if `--saves`) through a gcov build (`HOST_CC='cc --coverage -fno-inline
-fno-inline-functions-called-once'`); `coverage.py report` prints line/function coverage of `game/`.

| Script | Situations |
|---|---|
| `blocks.py` | every BLOCK_1..4 item, each open side, four distances, 12 schedules: latch, push, pull, push-release, push-pull, walk-in |
| `items.py` | every KEYHOLE/PUZZLEHOLE (matching item, ring or ACTION-opened USE page, twice, wrong item, none) and the Midas hand (death by standing on it, lead bar from the USE page, none) |
| `combat.py` | every enemy type (wolf, bear, bat, crocodile, lions, puma, gorilla, rats, rex, raptor, the three mutants, centaur, mummy, Adam), cowboy, Mr T, skater, doppelganger, Larson, Pierre, Natla, scion target: pad-woken, Lara walked to them through the level's sector centres (`directed.route`), each weapon through the ring (Lara_changeWeapon), several health presets; LEVEL3B Larson by `cuts.py larson`'s route, shot at and not |
| `movement.py` | ledge hangs with shimmy (HANG_LEFT/RIGHT), long falls (screaming), swan dives (FAST_DIVE), shallow-water cells; `wadein`: from dry ground into a pool whose top slab is 384..730 deep (WATER_STATE_WADE, reached by play on LEVEL7B only); `restore`: `Lara_restore` (JUMP+WEAPON while dead) |
| `saves.py` | replays kept scripts (`DIRECTED_SAVE=DIR`) with the real source `gameSave`/`gameLoad` at boundaries chosen where Lara's state changes (oracle `--save-tick/--load-tick`, native `--game-save-tick/--game-load-tick`); an oracle assertion on load is an `oracle_boundary`, not a comparison. `--sweep SCRIPT --step N` saves at every N-th tick of one script (the LEVEL10B doppelganger script: every save tick 5..595 equal after oracle hunk 16); `--cells LEVEL:CATEGORY,...` saves where `hanging`, `boulder-rolling` or `enemy-dead` first hold in a kept script (the matrix cells the schema-3 matrix left open) |
| `trigger-inputs.py` | `campaign.py --triggers DIR` inputs: for every item type a floor-data trigger activates, up to three sectors, Lara placed 1,024..3,072 units before each, running over it, drawing and firing; `<level>-type<ITEM_TYPE>-<n>.masks` |
| `switches.py` | every SWITCH and SWITCH_WATER item, and trigger sectors that fire boulders, ceilings, Thor's hammer, trapdoors, doors, flames |
| `pickups.py` | the pickups that have a pickup trigger under them (LEVEL3A cogs, LEVEL3B Scion, LEVEL4 key 71, LEVEL7A keys, LEVEL8B artifacts, LEVEL8C items 10 and 54): collected by play, then the trigger's door/flip; `scan` re-derives the set from the floor data and the LEVEL3B room graph; `negative` needs a port built with the old refusal; `relink` saves and loads after the collection and presses ACTION again (oracle hunk 20): the source's load linked every item into its room again, so each triggered pickup that play collects (LEVEL3A 33, 40, 53, LEVEL3B 20, LEVEL4 71, LEVEL8C 10, 54) reappeared and could be collected a second time; now the pickup counter and the inventory totals stay as they were at the save, the world validator never refuses, ordinary pickups behave as before, and the negative control is the oracle without hunk 20 (`--old-oracle`, `test/oracle/control.sh no-hunk-20`: the published m6-gaps1 differs on LEVEL3B from tick 0 because of the rolling block, hunk 25), which must differ after the load (the same scripts, all seven differ); `--no-save` is the control without the load |
| `drawbridge.py` | LEVEL8B's four drawbridges (oracle hunks 17, 18): each switch lowers its bridge (state 0 to 1, animations 231 to 233), a second press raises it, the raised bridge is a solid, and Lara placed at switch 69 pulls it, crosses bridge 22 and takes artifact 15; schema 4, oracle against port; `negative` runs the same scripts against the previous oracle (`--old-oracle`), which must differ. No single 600-tick scene can lower all four bridges (the switches are on other floors), so door 68 (masks 24, 2, 4, 1 of the four pickup triggers) is not opened by play; `reach` prints which switch fronts the driver can walk between: only 45 and 61, so door 68 stays unopened by play (see `docs/game-gaps.md` 7.5) |
| `cabin.py` | LEVEL10A's cabin (oracle hunk 19): three fuses inserted by play in one 1,016-tick script, the cabin through animations 303 to 307, the flip applied, equal oracle against port, negative control against the oracle with no cabin; `rooms` prints the portal graph before and after the flip (one edge pair, 30 to 94, removed; the same 98 rooms reachable) |
| `lava.py` | lava floors (oracle hunk 21): in LEVEL10A, 10B and 10C a lava sector with a standable non-lava neighbour is found in the cooked floor data (225, 386, 200 sectors), Lara placed on the neighbour runs onto it and dies (hit points -1), a control run onto another non-lava neighbour leaves her at 1,000, oracle against port equal tick for tick; the oracle without hunk 21 (`--old-oracle`) lets her survive |
| `burn.py` | lava particles (hunk 22): LEVEL10A/10B/10C emitters, particles alive at once, 10 hit points on contact, oracle equal to port, none on the oracle without the hunk; burning with no timeout (hunk 23) at LEVEL6's flame emitter 35: she dies at tick 253 instead of 319; falling ceiling at 300 a frame (hunk 24, with hunk 28 making the triggered slab visible): LEVEL3B ceilings 24, 25, 26 activated by the placed-state `activate` directive (no play reaches a slab as it falls: the Scion's pickup holds Lara until every slab has landed) with Lara standing under each: health in steps 300, 300, 300, 100 over four ticks of contact, equal to the port, the `no-hunk-24` control (`--control-oracle`) takes 1,000 in one |
| `movers.py` | the rolling block (hunk 25, LEVEL3B items 49 and 50) and the moving bar (hunk 26, LEVEL4 75 to 78, LEVEL10A 12): slides, sector-hash changes at the start and end of a slide, toggle back, solid start cells, a 574-tick walk through the cell the block left and into the cell it arrived in, save and load after and during a slide, the map flip with the blocks at their start, keys 1 to 4 into the LEVEL4 keyholes and the bars' animation, walls of bars, the LEVEL10A bar sliding into room 10; the block cases against the previous oracle (`--old-oracle`, m6-gaps2b) and the bar cases against the oracle without hunk 26 (`--bar-control-oracle`, `test/oracle/control.sh no-hunk-26`: m6-gaps2b caps scenes at 600 ticks and LEVEL4's key script is 1,882) as the negative control (`negative` runs the block and the bar cases); the bar plates are thin (70 thick, across the doorway; see oracle hunk 26); `reach` shows no walk from the hall to a flip pad |
| `invisible.py` | items that start invisible and are trigger-activated (oracle hunk 28): `scan` lists them in the 16 PC and 16 PlayStation levels (137 items, 22 non-enemy, all with a trigger); `run` activates each non-enemy PC instance with the placed-state directive `activate` and Lara beside it: oracle equal to port and status active, against status invisible on the `no-hunk-28` control |
| `hunk28.py` | the traces on which two oracles differ, by item (`--old OLD --new NEW --inputs DIR ...`): for oracle host.patch hunk 28 on the `trigger-inputs.py` inputs of LEVEL3B, LEVEL4 and LEVEL10A (`$TR64_WORK/evidence/m6/campaign-final/hunk28/`) |
| `strikes.py` | Thor's hammer (hunk 27: the strike point and the 30-frame delay) on LEVEL4's pad: killed on it, survives leaving the zone; the LEVEL4 lightning (400 a bolt near, none far) and the LEVEL10C lightning (1,792 above the floor: standing she is not hurt; woken by `activate 107 0`, a jump started at tick 91 puts her origin 838 below it when the bolt switches on at tick 125: one strike of 400, oracle equal to port) |
| `surface.py` | surfacing from inside a water room (oracle hunk 29: the fork's early surfacing branch, which left pitch and roll set and also "surfaced" her under rock ceilings, is removed): in GYM, LEVEL2, LEVEL3A, LEVEL5, LEVEL6, LEVEL8B and LEVEL10A (LEVEL1 has no water room: its room flags are 0 and 256) 6 open surface cells per level (water-room sector with an air room above, 1,500 or more deep), dived into by placement, swum up at three pitches (30, 60, 85 degrees) with no roll, LEFT or RIGHT for the last 14 ticks (roll), closed-loop on the oracle and cut at its first SURFACE tick, then 30 ticks of tread, 20 of surface swim, 30 of tread. Per case: oracle equal to port tick for tick; every SURFACE record has pitch (`rot[0]`) and roll (`rot[2]`) 0; the tick before the first SURFACE tick has a non-zero pitch or roll; the `no-hunk-29` control (`--control-oracle`, `test/oracle/control.sh no-hunk-29`) shows a non-zero pitch on its first SURFACE tick and differs from the port. `--ps-executable HOSTPS` (a host built with the PlayStation pack, same sources) adds the zero check on its own trace and equality with the PC port. Cases for which the closed loop finds no surfacing in 330 ticks are reported `no-script`, not passed |
| `lara-states.py` | which Lara states have any animation in the Lara models of the 16 levels (the rest cannot be reached in Tomb Raider 1) |

### Trace schema 4 (the `world` field) and the pickup/door repair

Oracle and port also speak schema 4 (`--trace-schema 4`, `--game-trace-schema 4`, see
`test/oracle/README.md`): schema 3 plus `world = [sectors, boxes, flips, pairs]`, hashes of the live
sectors of every room, the live box flags and the flip maps, and the oracle validator's verdict. The
directed scripts (`directed.py`) compare schema 3 unless `TR64_TRACE_SCHEMA=4` is set (with `--oracle` the
m6-worldfix build; `pickups.py` always runs schema 4); `campaign.py --schema 4`, `cuts.py --schema 4` and `fixtures.py --schema 3|4 --oracle ORACLE`
(which streams the oracle's trace of every fixture input instead of reading the stored schema-2 file) take
it too. A collected pickup with a pickup trigger under it used to make the source refuse every later door
update and map flip (`test/oracle/README.md`, hunk 14); `pickups.py` is the test of that repair, with a
negative control: the same inputs on a port built with the old refusal must differ, first at `world[3]`.
`make host HOST_CC="cc -DTR64_GAME_VERIFY_LINK_BITS"` builds a port that recomputes the item-list check by
walking every room list on each door update and stops if it disagrees with the unlink bits that replace
that walk.

Evidence class matters. Scripts that need an item the start does not grant use `give` (items:
a key or puzzle pickup is 11k to 100k units from its hole, 275 to 2,500 ticks against the
600-tick limit; the Midas lead bar is 71k away) and low health uses `item`; both are placed-state
evidence, not play-reached. Block, hang, fall, wade, switch, pad and full-health combat scripts
use only placement and inputs. LEVEL10A confiscates the pistols on entry, so its pistol cases
also `give 84 1`.

`campaign.py --levels gym,level1` limits the batch to a subset.

## PlayStation shipping pack (21 levels, effects, music, 10 videos)

The same four composers make the PlayStation pack from the PlayStation data alone (levels, sound
effects, title picture and the 17-level music pack of Core's `psx-campaign-spu.pack`; the videos
from the disc's STR files):

```sh
export PYTHONPATH=tools/cook TMPDIR=$TR64_WORK/tmp/<you>
python3 test/game/campaign-pack.py --source psx --textures $TR64_WORK/evidence/m1-tex2/sections \
  --title-image $TR64_WORK/evidence/m5-ui/title/title-psx.ui64 \
  --title-image-manifest $TR64_WORK/evidence/m5-ui/title/title-psx.ui64.json \
  --cache $TR64_WORK/cache --out OUT/campaign21-psx-spu.pack
python3 test/game/campaign-music.py --campaign OUT/campaign21-psx-spu.pack --campaign-report OUT/campaign21-psx-spu.json \
  --music-pack $TR64_WORK/builds/Core/music-campaign/psx-campaign-spu.pack --cache $TR64_WORK/cache \
  --out OUT/psx-campaign21-spu.pack
python3 tools/cook/n64_video.py --source psx --cue DISC.cue --spu-encoder SPUENC --out VIDEOS   # ten STR files
python3 tools/cook/pack_add_video.py --base OUT/psx-campaign21-spu.pack --video-catalog VIDEOS/<key>/manifest.json \
  --cache $TR64_WORK/cache --out OUT/psx-campaign21-video.pack
```

`campaign-pack.py --source psx` cooks TITLE, the sixteen levels and CUT1..CUT4 from `PSXDATA/*.PSX`
with the PlayStation Core tables, geometry, CLUT textures (the pinned `m1-tex2` TX64 sections) and the
raw SPU ADPCM sound (SN64 encoding 3), never a PC section; `campaign-music.py` reads the
source from the campaign report and accepts only that source's sound encodings (PC: PCM8/PCM16, PlayStation:
SPU ADPCM) and records where the music came from. The disc's CUT1..CUT4 are ordinary level files like
the PC ones: the same four cinematic frame tables (1600, 1000, 400 and 1890 frames, equal to the PC
tables), items and cameras; their sound effect banks are empty (see `docs/formats/n64-video.md`
"PlayStation pack").
`n64_video.py --source psx` needs the BIN/CUE on this host; a sparse BIN holding only the ISO
directory sectors and the ten `/FMV` extents works (`dd` the sectors from the disc; the tool reads nothing else).
The disc has no ESCAPE film (the PC logo), so a PlayStation ROM plays CORE and no ESCAPE: the shipping gate
takes `TR64_VIDEO_ABSENT=ESCAPE tools/ship/ship.sh check` (`boot.py --video-absent ESCAPE`: each named film must log
`result=absent`, any other absent film fails the gate). `cuts.py` runs unchanged on a host executable built with the
PlayStation pack: the oracle still reads PC data and the pack supplies the native side.

`directed.py` takes `TR64_TRACE_SCHEMA=4` (m6-worldfix oracle, `--oracle`) and returns `oracle_boundary` when the
source asserts. `Script.walk_route(goal)` walks through standable sector centres (`route`: breadth-first, height
step at most 800); `walk_to` no longer counts turning on the spot as a stall.

Unreachable in the fixed source, established from the source and the oracle (not by lack of trying):
`ItemObj_getDrawBridgeFloor` needs a drawbridge item in state 1; `ITEM_DRAWBRIDGE` has no `INIT_ITEM` (`item.h`
1820 commented out) and the plain `ItemObj::update` is empty, so nothing ever changes its state: in 48 LEVEL8B
switch scripts all four drawbridges (items 19..22) become active and stay in state 0, anim 230, frame 0.

## Second-game variant packs and the ring latch control

The Great Wall controls (`tr2-interact-test.py`, `tr2-combat-review-test.py`, `tr2-arms-test.py`,
`tr2-climb-mark-test.py`, `tr2-floor-admission-test.py`, `tr2-hazards-test.py`, `tr2-water-test.py`,
`tr2-water-review-test.py`) rebuild `wall.pack` with a changed Core table. A level's sound section
(SN64) is never optional: the host and the cart bind it before a level's audio starts, and a pack
without it stops with `audio level start before sound readiness`. Build every variant pack with
`wall_sections(tables)` from `tr2-item-actions-test.py`; it keeps the Core tables, the texture and the
sound section, re-creating the sound section's partition from its own sample table. Do not filter a
pack's sections by hand. Variant packs still differ from the shipping pack: rooms and meshes are
omitted, and Core and texture are stored uncompressed with the default chunking. Host behaviour is
unaffected (traces of the unchanged tables are byte-identical to `wall.pack`'s), but compare pack
bytes with that in mind.

`PYTHONPATH=tools/cook python3 test/game/tr2-sound-partition-test.py WALL_PACK` rebuilds a pack from its
own sections through `wall_sections` and requires every sound entry (kind, destination, sizes, codec,
CRC, digest) to equal the shipped one (all 624 for `wall.pack`), so a change of the cooker's cut rule is
caught; a control with a uniform 4096-byte partition must differ.

`test/game/tr2-ring-latch-test.py [--game N] HOST_BUILD_DIR WALL_PACK FIXTURES SCRATCH` links
`test/game/tr2-rex-control.c` (modes `ring-busy`, `ring-flare`, `ring-land`) around the objects of an
existing `make host` build and runs the three ring gun latch cases: pistols chosen with busy hands
(`pickup`); a flare chosen over a pending pistol choice while gun access is closed (`schema21/pool-swim`);
pistols chosen with the flare control held in the same tick on dry land, where the choice must win
(`stand`). Each case has a mutant: a scratch copy of `game/tr2/lara_weapon.c` with the busy clause, the
flare branch, the clearing of the pending choice, or the gun-access condition removed, which must stop by
name; an unmodified scratch copy must pass all three. The script refuses to run when that copy's section
sizes differ from the build's own object (wrong `--game` or flags). No tracked file is changed.
`tr2-interact-test.py`'s `run_block_floor_opened` needs a host built with
`TR64_SCENE_TEST_SOURCES=test/game/directives.c`.

`tr2-arms-test.py` wakes its synthetic tiger with a combat trigger (it fires on the tick the pistols are
ready), because a woken tiger moves at once; the placements come from Lara's recorded position on that tick
(in cone, hidden behind the platform edge for line of sight, yaw left/right outside 60°, and pitch high in the
open sky room above for the pitch cone). Passing `--cone-mutants HOST_BUILD_DIR [--game N]` also links
mutated scratch copies of `game/tr2/lara_weapon.c` (unmodified, no line of sight, no yaw cone, no pitch cone,
and 90° pitch cone) to prove each guard independently fails by name.
