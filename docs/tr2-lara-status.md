# M10: Great Wall dry Lara, host only

Reference: the 12 published PC Great Wall schema-20 routes in
`$TR64_WORK/fixtures/oracle-wall/tr2/`. Take the rule from the cited
specification or authored data, not from the result column. No game input,
pack, ROM or runtime trace is committed. Replay uses the `wall.pack` from
[M10 step 1](tr2-wall-measurement.md), SHA-256
`d5e1cb90646be0bdb94545e369eca0c118a5684a68cbcc9e2c454de73a59a02e`.

## Before: TR1 Lara rules on the TR2 pack

The `--game-tr2-baseline` flag runs the existing TR1 Lara state, movement,
collision and camera update on TR2 data, including the **correct** authored
packed-frame stride in both modes. The harness holds incompatible item,
floor-action, weapon, water and sound subsystems inert. Both modes preserve
raw input while cancelling opposed horizontal directions and translating lone
sidesteps; baseline maps dedicated Roll to the TR1 forward+back chord.
This replaces the original invalid-stride comparison (`before/`).

Counts include tick 0. `P/E` means consecutive equal records from tick 0 /
equal records anywhere; one number means those counts are equal. These are
**known-field** counts, not complete-family PASS: an unavailable field can
never produce a strict equal tick. `–` means the whole family is unavailable.
`RNG` is the pair, while `RNG0` is gameplay only.

| Route | Records | Lara | Camera | Active | Save | Inventory | World | RNG | RNG0 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| stand | 301 | 0 | – | – | 301 | – | 0 | 2 | 301 |
| turn | 178 | 0 | – | – | 178 | – | 0 | 2 | 178 |
| movement | 224 | 0 | – | – | 95 | – | 0 | 2 | 224 |
| jump | 226 | 0 | – | – | 161 | – | 0 | 2 | 226 |
| roll | 184 | 0 | – | – | 184 | – | 0 | 2 | 184 |
| travel | 320 | 0 | – | – | 320 | – | 0 | 2 | 320 |
| grab-attempt | 252 | 0 | – | – | 206 | – | 0 | 2 | 252 |
| flare | 127 | 0 | – | – | 127 | – | 0 | 2 | 127 |
| pistols | 116 | 0 | – | – | 76 | – | 0 | 2 | 76 |
| tiger-cave | 464 | 0 | – | – | 464 | – | 0 | 2 | 397 |
| pool-wade | 840 | 0 | – | – | 582 | – | 0 | 2 | 397 |
| pool-swim | 659 | 0 | – | – | 465 | – | 0 | 2 | 430 |

First `lara` difference in all twelve was tick 0, `lara.next`: retail 0,
port 1 from TR1's model-initialized queued state. Independently from queued
state, startup and equipment fields: position, orientation, state, goal,
clip, frame, speeds and gravity matched on every `stand` and `turn` record.
In particular TR1 already reproduced the entire opening fall and slide; the
writer's equipment/constructor differences do not imply locomotion differed
at spawn. Evidence: `before-v5/` under `$TR64_WORK/evidence/m10/lara/`.

## After: grounded dry Lara rules

The parser validated the manifest and exact canonical record bytes for all
12 assigned routes. Evidence: `after-v5/*.compare.json` in the same evidence
root. The table uses the same known-field convention as before. The fixture
directory also gained `flare-held` while this work ran; the final replay
included it (166 records; first Lara difference at tick 36; camera look
subjects, gameplay RNG and comparable statistics match all 166).

| Route | Records | Lara | Camera | Active | Save | Inventory | World | RNG | RNG0 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| stand | 301 | 301 | 301 | – | 301 | – | 0 | 2 | 301 |
| turn | 178 | 178 | 178 | – | 178 | – | 0 | 2 | 178 |
| movement | 224 | 224 | 224 | – | 224 | – | 0 | 2 | 224 |
| jump | 226 | 226 | 226 | – | 226 | – | 0 | 2 | 226 |
| roll | 184 | 184 | 184 | – | 184 | – | 0 | 2 | 184 |
| travel | 320 | 320 | 320 | – | 320 | – | 0 | 2 | 320 |
| grab-attempt | 252 | 252 | 252 | – | 252 | – | 0 | 2 | 252 |
| flare | 127 | 36 | 127 | – | 127 | – | 0 | 2 | 127 |
| pistols | 116 | 46 | 116 | – | 76 | – | 0 | 2 | 76 |
| tiger-cave | 464 | 464 | 464 | – | 464 | – | 0 | 2 | 397 |
| pool-wade | 840 | 568 | 840 | – | 582 | – | 0 | 2 | 397 |
| pool-swim | 659 | 464 | 659 | – | 465 | – | 0 | 2 | 430 |

**Strict acceptance is not met.** Every comparable Lara field and gameplay
RNG state agree at every tick on all seven required routes. The comparator
returned 0 on each with the nine unavailable Lara paths excluded by the
reviewed profile (`after-v5/`). The strict Lara count is **zero**
on all routes, even when the serialized defaults happen to agree.
`grab-attempt` is a rejection; it does not demonstrate an ordinary held
grab or a ladder mount.
The two comparable camera fields (`lookAtItem`, `lastItem`) also match
every record on these seven routes (1,685 total), including `travel`:
`lastItem` is item 102 at ticks 204–247 and clears at 248. No comparable
camera mismatch was found on the other five routes either.

### First differences (retail → port)

| Families/routes | First comparable difference or unavailable state | Rule needed |
|---|---|---|
| Lara / flare | 36 `lara.weaponState`: 2 → 0 | [Water and flares §Flares](specs/wall/tr2-water-and-flares.md#flares-new-in-ii), ignition. |
| Lara / pistols | 46 `lara.weaponState`: 2 → 0 | [Ground and weapons §Frames for drawing, firing and holstering](specs/wall/tr2-lara-ground-and-weapons.md#frames-for-drawing-firing-and-holstering). |
| Lara / pool-wade | 568 `lara.goal`: 2 → 21 | [Water and flares §Wading, swimming, ledge exits and reversal](specs/wall/tr2-water-and-flares.md#wading-swimming-ledge-exits-and-reversal). |
| Lara / pool-swim | 464 `lara.anim`: 178 → 0 | Same water specification, entry from dry run. |
| Camera / all | Look subjects compare; motion/view state is unavailable from tick 0. The inherited v4/v5 eye differs at tick 0 (`stand` `camera.pos[0]`: 64490 → 62976); the withdrawn v3 motion experiment first differed at tick 54 (62050 → 61952). | [Opening view and renewal](specs/wall/tr2-lara-start.md#camera-seed-placement-and-the-opening-view), [trigger camera rules](specs/wall/tr2-triggers-and-interaction.md#item-masks-timers-and-action-ordering) and Q-T2L-07. |
| Active / all | Unavailable at 0; serialized list length 2 → 0 | [Startup active order](specs/wall/tr2-lara-start.md#which-things-move-first), and subsequent item controllers. |
| Inventory / all | Unavailable at 0; serialized ring length 6 → 0 | [Startup equipment and reserves](specs/wall/tr2-lara-start.md#lara-before-the-first-gameplay-update), then ring operations. |
| World / all | 0 `world[0]`: 2415482401 → 669297626 | [Trace §Record fields](specs/wall/tr2-oracle-trace.md#record-fields), live initial doors and movable block, not a constant digest. |
| Save / pistols | 76 `save.ammoUsed`: 2 → 0 | Paired-pistol volleys in Ground and weapons. |
| Save / pool-wade | 582 `save.distance`: 17704 → 17713 | The first differing position after water entry; Water and flares. |
| Save / pool-swim | 465 `save.distance`: 18747 → 18751 | First movement in wade; Water and flares. |
| Save / all | `save.ammoHits` unavailable even where the other counters agree | [Trace §Record fields](specs/wall/tr2-oracle-trace.md#record-fields) defines this field but no directed hit is published. |
| RNG pair / all | 2 `rng[1]`: 2506421620 → 3547461959 | [Trace §Headless start and sampling](specs/wall/tr2-oracle-trace.md#headless-start-and-sampling); differing draw/audio cadence, Q-T2L-08. |
| RNG0 / pistols | 76 `rng[0]`: 1162535651 → 3547461959 | Issued shots and independent spread draws in Ground and weapons. |
| RNG0 / tiger-cave, pool-wade, pool-swim | 397, 397, 430 respectively: 2860386973 → 3547461959 | [Great Wall creature rules](specs/wall/tr2-great-wall-creatures.md), including the tiger's actual controller. No synthetic draws are inserted. |

The writer emits schema-20 rows plus one **port-only footer**: exact
producer name and unavailable paths/ranges, FNV-1a over its row stream, and
the footer's hash. The comparator requires `--producer` so the test
invocation, not the file, chooses the expected profile. It rejects missing,
altered, misbound, mislabelled, or validly resealed incompatible footers by
checking coverage against [`tools/tr2-producer-profiles.json`](../tools/tr2-producer-profiles.json).
Strict counts never include unavailable fields; `knownEqual` requires an
actual comparison. To opt into the reviewed projection, pass
`--accept-profile-exclusions`; it cannot waive a numeric mismatch.
The complete Lara fallback list is `timer`, flare age/frame/left/right,
`climbing`, `vehicle`, `weaponItem`, and `target`; the baseline has additional
stock, tick-0 water metadata, and look-subject exclusions. The common
exclusions are for unimplemented semantics, not numerical mismatches:
camera eye/target motion, view sequencing and effects are unsupported;
the sequel inventory ring and its selections, item activation, the hits
counter and flip slots are not updated. The v4 reviewer observed all nine
excluded Lara values, `save.ammoHits`, `world[2]`, and both inventory
selection defaults happen to agree across the 1,685 records. This does
not establish those implementations. Active and inventory contents
differ on all 1,685; many serialized camera motion values also differ.

### Rules and version selection

The rule register is [game/tr2.h](../game/tr2.h). It lists each named bit,
its source section and its consumers. `levelStart` in
[`game/game.c:363–451`](../game/game.c#L363) admits version 2 only for the
explicit Great Wall host mode. It validates the floor programs and room
chains at line 383 and binds `tr2Rules` at line 451, before constructing Lara.
`TR64_RULE(name)` in [`game/internal.h:821–835`](../game/internal.h#L821) is the single gameplay
predicate; it folds to false on the current TR1 cart.
The common Lara/input, collision, animation-command, room and camera
machinery is shared; separate helpers handle the rules that need more
than a small change to the inherited procedure. There is no TR2 cart in
this slice.

* **Representation:** OpenLara `src/animation.h:99–105,120–145` and
  `src/format.h:2666–2710` establish frame stride, successor lookup and
  tagged rotation words. Both keys are validated before an address is
  formed; the pose consumes the selected key's explicit extent. Valid
  authored destinations outside a clip's declared range retain the
  storage key they address; only an out-of-storage key is refused.
  See [Authored animation links](specs/wall/tr2-lara-start.md#authored-animation-links-and-the-flare-arm-q-t2l-06).
  The overlapping run-jump transition windows already use the first
  authored eligible range, so the common state dispatch remains shared.
* **Start:** [Lara start](specs/wall/tr2-lara-start.md) establishes the
  coarse floor seed, source position/heading and raw flags, dry stand,
  cleared `next`/collision marker, stock and reserve reset, first fall
  threshold. A water-room spawn is refused.
* **Dry controls, falls and ledges:** Ground and weapons §§Ground, slopes
  and airtime; Ordinary vault, grab and shimmy. These cover the dedicated
  roll, run-jump arming, the vertical compression choice, accepted run
  step rise, ordinary stand ceiling restoration, frontal jump clip
  selection, side/back clearance, lethal speed and ordinary airborne reach.
  The revised [dry landing and frontal obstruction](specs/wall/tr2-lara-start.md#standing-to-fall-then-landing)
  rules supply the reaction state and same-tick grounded advance. Standing
  and running use the same authored landing dispatch, not route-specific
  logic.
  Ground turning/damping and the event-driven launch were inherited.
  Midair twist requests, and pitch reversal in the specific authored
  air/underwater clips, cite [Air reversal and Underwater Roll](specs/wall/tr2-water-and-flares.md#wading-swimming-ledge-exits-and-reversal).
* **Slides, slopes and look subject:** [Slide, height and gaze](specs/wall/tr2-lara-slide-height-and-gaze.md)
  establishes the opposing-input slide-jump veto, downhill versus facing
  and side deflection, vertical link sector choice, signed per-axis height
  rounding, keyframe bound interpolation, and automatic gaze/slew/decay.
  The gaze reads an authored inert subject: it does not activate or move
  the helicopter. Only ordinary auto-Look nomination and Lara's head/torso
  response are implemented. Camera seeding, fixed actions/renewal and the
  eye mover from v3 were withdrawn rather than claiming camera semantics.
  The licensed I wall baseline reportedly rounds these slopes as II does;
  **the existing TR1 port is not changed**, since its fixed-point oracle
  uses the existing negative-product-before-subtraction order.
* **Isolation:** Item types are counted individually as inert. The
  diagnostics also count ignored floor geometry/action opcodes, climb-mark
  queries, unhandled look targets, held flare/draw ticks, water-plane
  queries, untested mixed sidestep inputs, and suppressed sound requests.
  Logical sound requests do not call the mixer. Play, saving and restore
  remain refused; the host uses the native level loader but binds no
  renderer or game sound playback.
  The native floor validator runs before admitting sequel gameplay. Its
  generated grammar is shared with the source cook, and admission checks
  payload spans, terminators and item/camera references before use.
  It also checks for horizontal and same-direction vertical room cycles
  across the whole lookup domain. `engine/res/validate.c` intersects the
  position bands of each successive sector for a fixed world X/Z
  (including wrapped deltas, TR1 baseline clamping, TR2 border clamping,
  and direct vertical lookup). A repeated room at different positions is
  not a cycle. The independent Great Wall host probe measured a median
  admission validation time of 2.32 ms with the cycle check versus
  0.063 ms before it (100 runs each, `admission-v5-review2/probe.log`).

### Wall questions

1. **Q-T2L-01 to 04:** answered by the merged startup and slide/height/gaze
   specifications. Q-T2L-03 identified a first-game port rounding
   difference, not a universal II-vs-I retail rule.
2. **Q-T2L-05: answered.** Grounded advancement adds no displacement to
   the zero-speed Stand landing but moves 47 in the authored Run landing.
   `movement` tick 152 is simultaneous frontal reaction and landing:
   choose clip 31 at its first frame (460), preserve reaction state, then
   advance it to 461 as part of landing. The shared Stand terrain check
   chooses the slide at tick 153. `after-landing/movement.compare.json`
   has 224 comparable matches, up from 223 in v3.
3. **Q-T2L-06: answered.** A hypothetical body link from 258 to 254/19
   keeps that frame and samples its real storage key. The held flare
   instead resets its own arm state to 254/0 and does not take that
   body link. This slice preserves the safe body lookup and leaves the
   separate flare progression out of scope.
4. **Q-T2L-07:** v3 `stand` tick 54, `camera.pos[0]`: the experimental
   renewing fixed camera stays at 61952; retail is at 62050. What clearance
   query/movement rule changes the authored eye while retaining the view?
   The startup spec distinguishes renewal and clearance without giving all
   of the applicable camera algorithm.
5. **Q-T2L-08:** `stand` tick 2, `rng[1]`: retail changed to 2506421620
   from the fixture seed; the host hasn't drawn or played audio and stays
   at 3547461959. What presentation work consumed the intervening draws?

The independent v4 cook check also passed floor grammar for Venice and
Bartoli's Hideout but both hit the level arena limit: 1,869,504 B and
1,743,360 B against 1,507,328 B, respectively
(`admission-v4-review2/cooks.log`). This is a capacity failure, not native
gameplay verification of either level.

## Verification, size and handoff

* **Solo TR1:** all 25 M2 fixtures byte-identical for each of schemas 2, 3,
  4 and 5. Schema 2 used the immutable fixture bytes; schemas 3–5 streamed
  the passport OpenLara oracle (`a98eff3f…`) against the host executable.
  `solo-v5/{2,3,4}-{level1,gym}.log` and `solo-v5/5.log` retain the individual
  results. Schemas 2–4 used `test/game/fixtures.py`; schema 5 used
  `schema5.py` with `PYTHONPATH=test/game`.
* **Shipping TR1:** both builds used `TR64_PLAY=1 TR64_TEST=0` and the
  same `pc-campaign21-video.pack` (SHA-256 `ec7d96d2…`) at `bdff898`
  (`tools/ship/ship.sh build`) and v5.
  The candidate and baseline `.text` are byte-identical: 437,744 B, same
  address. All defined symbol names, sizes, addresses and types match
  (`cart/v5/symbol-diff.txt` is empty). `.rodata` (85,448 B) is byte-identical
  after excluding the four source-build and two configuration strings.
  `.data` (40,504 B), `.bss` (26,200 B), `.sdata`, `.sbss`, and all resident
  and paged code also match; the derived `.tlbcode_id` differs.
  `size_gate` passed at 601,880/622,592 resident bytes.
  `hot-check.py` passed: 41 ordered sections, render window 16,288 B, hot
  data 6,864 B at phase 1,024, entry SP 1,016/3,368. See `cart/compare-v5.txt`,
  `cart/v5/{hot-check.log,size.json}`, `cart-v5-build.log`, and both ELF/map pairs.
  Whole-ROM byte identity is not asserted because of build identities.
* **Directed controls:** `test/game/tr2-compare-test.py FIXTURES/stand`
  passed including rehashed tick-12 `lara.pos[2]`, `lara.ammo[0]`, and
  `camera.lastItem`, rejection of relabelling as baseline to avoid those
  comparisons, numeric domains, wrong manifest, misbound stream, rehashed
  false coverage and rejection of zero comparable leaves.
  At v4, `test/game/tr2-input-test.py HOST WALL.pack`
  passed for both variants (opposite cancellation and one-sided control,
  roll and both sidestep aliases, all raw bits, reserved input refusal).
  `test/game/tr2-packed-frames.c` linked the real sampler with section GC:
  checked selected strides, a valid retained frame 19 outside the declared
  clip, and refused both current and successor frame 20 beyond actual data
  with a named count of 1.
  `test/game/tr2-floor-admission-test.py` rebuilt valid checksums and
  reached gameplay admission for both the ordinary and disjoint-position
  positive controls. Eight negatives failed by name: span, terminator,
  camera index, item index, the reviewer's self-portal, a two-room
  portal loop, and distinct above/below loops.
* **Size:** per-object measurements in `object-size-v5.txt`, against `bdff898`.
  Host added **10,452 B `.text` / 1,225 B `.rodata`** in gameplay and
  shared admission TUs excluding the writer and CLI; **17,957 / 3,095 B**
  including both. The host validator also adds 12 B of relocation-backed
  data; other static data is unchanged. Great Wall host state used
  173,452/196,608 B. Compiling shared sources as VR4300 objects with the
  rule-bearing code enabled gives a projected **+10,464 B `.text` /
  +1,282 B `.rodata`** before link with the writer excluded
  (including writer: +24,672 / +3,093 B and +12 B `.sdata`). This is an
  object-section estimate, not an exact linked delta: object alignment,
  section GC and a per-game selection can change it. The actual shipping
  cart links none of the sequel logic.

### Reproducing and review state

Use the host executable and evidence-root copies of the runners, or replay
one fixture:

```sh
rtk "$HOST" --game-tr2 "$WALL_PACK" "$FIXTURE/input.txt" "$OUT"
rtk python3 tools/tr2-compare.py "$FIXTURE" "$OUT" --producer tr64-tr2-dry \
  --require lara --require 'rng[0]' --require camera --accept-profile-exclusions
```

Use `--game-tr2-baseline` and `--producer tr64-tr2-baseline` for the
original rules; camera look subjects are unavailable in that profile.
A strict Lara verdict is expected to fail because some implementation state
is intentionally unavailable. The hashed port capture and gate output are
separate from the retail fixture. `replay-v5.py` pins the producer for each
run mode. The final host executable is retained at `host/v5/tr64-host`
(SHA-256 `91439bcbe8eaae15a3899a7b75889cb55bf60d1d29cc323ca59c9df5c1dff983`).

Comparator commits on `m10-tr2-lara`: `ed9ffd8d70`, `093ffc10de`,
`1920b2bdc6`, `44ceb08298`, `82964861bb`. The 22 runtime and directed
control files were accepted and committed together as `cbac9c957a`.
No runtime files await review; patches, captures, runners and other
evidence remain outside Git. Independent acceptance is recorded in
`admission-v5-review2/`.
The frozen review set `admission-v5.patch` is based on `82964861bb`
and has SHA-256 `67559de94988c52cc9ba1731348f8fcd90e9936cfa2a4708db3357ca0d21dc69`.
Its supplemental files and v4 delta hash are in `admission-v5.txt`.
The only source difference from the reviewed snapshot is that the
OpenLara source and revision notice in `game/tr2.h` is on one line,
as required by `provcheck`.
There are no changes to Lara or camera motion since v4.

### Known gaps

* TR1 admission accepts a room whose horizontal portal points to itself
  (`admission-v5-review2/tr1-self.log`). The traversal in
  [`game/room.c:83–90`](../game/room.c#L83) has no cycle guard. This is
  pre-existing; the control tested admission only, not traversal, and
  this slice does not change TR1.

Not checked: emulators or hardware, a second-game cartridge, complete
first-game or Great Wall playthrough, actual successful ledge/ladders,
sequel save/restore, water, flare/weapon or creature parity, sound playback,
rendered animation joints, and presentation RNG parity.
