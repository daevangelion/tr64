# M10: Great Wall camera (host)

The behavior sources are the wall specifications and authored level data.
The schema-20 records are used only to verify the camera and locate gaps.
Working branch `m10-tr2-camera` starts at main `94a56a0`; no second-game
cart is built. Evidence root: `$TR64_WORK/evidence/m10/camera/`.

## Combat camera (Q-T2C-13)

The [Combat rule](specs/wall/tr2-camera.md#combat-view-q-t2c-13)
uses the arm controller's admitted Draw/Ready request before floor
renewal. Manual Look wins; holster stops requesting Combat on its first
tick. Suppressed fixed views can still set the last index and expire the
timer, but cannot move the eye or nominate an automatic gaze subject.

Combat raises Lara's bound-based target by 256, with signed quarter
easing or an immediate height on leaving a fixed item subject. The
unshifted target keeps Lara's live room. The nominal eye is 2560 away;
blocked sides use Follow's choices with the **last Follow horizontal
radius**, not the Combat radius. The ordinary eye divisor is 8, or 1 on
leaving a fixed item subject. Combat uses final head plus torso angles
unless an item is selected; selection takes the weapon's raw sight
instead of the arms' eased pose or lock bits. The underwater plane
rule is implemented before obstruction tests and is unit-checked.

| Route | Old first unavailable tick | Complete camera records |
|---|---:|---:|
| `pistols` | 46 | 116/116 |
| `look` | 176 | 352/352 |
| `flare-drop` | 147 | 421/421 |

`tr64-tr2-combat-camera` retires only those three camera cutoffs and
keeps all inherited field exclusions. The three routes match retail
throughout for comparable Lara and gameplay RNG. `look` no longer
nominates marker 102 at tick 280 with guns ready; manual Look and the
first holster tick return control as specified. The selected-target
path has only a native unit control; it awaits a live-target route from
the creature slice.
No firing or target-selection code is changed. The directed and
inherited replay evidence is under `$TR64_WORK/evidence/m10/camera3/`.
The other 27 accepted routes (9,809 records) have byte-identical
row streams, unchanged exclusions and unchanged comparison results.
The radius control deliberately uses the current Combat radius:
`pistols` first diverges at tick 48, and at tick 71 eye Z is 24537
instead of 24567. Using eased arm angles instead of selected-target
sight fails the linked unit control. The previous cutoff artifacts
cannot pass by changing and rehashing their producer footer.

The accepted runtime and controls are in `4181bf17f554c105517344782ed946b38e701e45`.
Independent review: `camera3/review/review-summary.txt` under the M10
evidence directory.

## Wet Follow camera (Q-T2C-12)

The [wet request rules](specs/wall/tr2-camera.md#requests-in-and-out-of-water-q-t2c-12)
are applied at two points. Wading **on entry to the tick** requests
elevation −4004 before the depth check, even in a sideways pose or
with no health. Running surface control requests the same elevation
before the stroke, including when it ends in a dive. Underwater
control adds no request. The ordinary state-65 living-Wade request
still runs at dry/wading control entry; a dry-to-wade transition is
not itself a request. Follow uses the updated pitch and its ordinary
geometry and reset.

`tr64-tr2-wet-camera` inherits the accepted water producer and extends
only `pool-wade`'s reviewed camera coverage to tick 839. No exclusion
is added. The two historical water profiles still describe their
own artifacts; the live runtime does not end camera coverage on
the wading-sidestep case. Unimplemented water movement outside
the fixtures is still the movement controller's responsibility.
Combat and its inherited camera bounds are unchanged in this patch.

The directed host gate (`test/game/tr2-wet-camera-test.py`) compares
all camera fields through the last tick: **840/840 `pool-wade`** and
**659/659 `pool-swim`**. Lara matches retail on the two routes. Raw
Lara, active items, inventory, save, world and gameplay RNG are
unchanged from the accepted water host. The existing tiger RNG
differences at ticks 397 and 430 remain visible.

The old host ends camera coverage before `pool-wade` 569; its raw eye
Y is 5416 there, versus the retail and new value 5392. A build with
only the new surface request removed differs at `pool-swim` 585
(`camera.pos[0]` 66898 rather than 66909). The evidence, including the
complete traces and the rejected reinstated cutoff, is under
`$TR64_WORK/evidence/m10/camera2/wet-v1/`.
The accepted runtime and controls are in `444e5fb4f7`; the independent
acceptance is `camera2/review-wet-v1/review-summary.txt` under the
M10 evidence directory.

## Accepted host camera (v4)

Runtime and controls: `de7056c3aa12d35207769084d68a5ba00dd0c9cd`.
The reviewed patch is based on main `5fc84e2107c4632ae122cdb0526042afba2edd80`;
it does not include the arms runtime. The independent acceptance is in
`review-v4/review-summary.txt`. `camera-v4.txt` binds the frozen patch,
its v3 delta and the evidence.

### Changes from the rejected v3

- The camera coverage boundary is **trace metadata only**: neither the
  rig nor trigger renewal reads it. The real Draw-vs-no-Draw control
  compares every underlying state field apart from the recorded input
  mask, including automatic gaze after the cut. On `look` tick 280,
  the unarmed port nominates marker 102 and slews Lara's head and torso
  to `[728,-728,0]`; retail's ready pistols suppress that nomination and
  both remain `[0,0,0]`. This is a visible Lara discrepancy, not excluded
  by the camera cut, until the arms and Combat view are connected.
- Flare and merely updating a creature are not camera events. Both flare
  routes and `tiger-cave` now require all camera ticks. Pool cuts begin at
  the water transition, not at the tiger's first update. The other
  unimplemented families still report their first real differences:
  the flare/weapon state at tick 36, tiger gameplay RNG at 397 (`pool-swim`
  at 430), and Lara's state at the pool entries. No camera cut masks those
  differences.
- Accepted Look no longer cancels the active Fast Turn state's last
  acceleration under Q-T2C-10. The linked witness at initial turn rates
  ±900 requests ±1456 and damps to ±1092, then requests Stand; v3 requests
  no increment. The dedicated standing sidestep no longer inherits the
  uncited TR1 116-unit admission probe. `sidestep-blocked` reaches the
  named obstruction counter seven times; blocked-gait parity is not
  claimed (`v4/sidestep-blocked-unreviewed.log`). The
  Q-T2C-11 answer in `docs/specs/wall/tr2-lara-ground-and-weapons.md` on
  main at `5c5777f` establishes that Stand makes no clearance test: the
  animation advances and moves before the collision test. That
  post-movement collision rule and the blocked replay are left to the
  next slice.
- After validating the placed input and manifest, the comparator checks
  XYZ, yaw with zero pitch/roll, and room in **both** tick-zero records,
  regardless of the requested families. The rehashed input+manifest
  relocation is rejected on the retail row; rehashed port position,
  pose and membership mutations are separately rejected on the port
  row. The comparator has no level data, so it does not independently
  recover the live sector's coarse floor; the native host computes it
  from the actual destination sector. Normal comparisons still compare
  the produced `lara.roomFloor` values.
- `game_tr2_heading_step(int16 yaw, int16 speed)` in `game/item.c`,
  declared in `game/internal.h`, exposes the Q-T2C-09 projection. It
  returns X/Z steps and Y zero without changing pose, speed or gravity.
  The generic item-movement branch and the loose-flare caller can
  share the single rule.

### v4 route coverage

| Routes | Camera verdict |
|---|---|
| `stand`, `turn`, `travel`, `movement`, `jump`, `grab-attempt`, `roll` | 1,685/1,685 camera, comparable Lara, gaze and gameplay RNG; no change from v3 |
| `slide-jump`, `slide-jump-backward`, `look-sidestep`, `fall-damage`, `hard-landing` | 882/882 camera; no change from v3 |
| `fixed-camera`, `fixed-camera-repeat` | 747/747 camera; both placements bound to tick zero |
| `flare`, `flare-held`, `tiger-cave` | 127/127, 166/166, 464/464; no camera gap |
| `pistols`, `look` | 46/116 and 176/352; first unavailable at firearm draw on ticks 46 and 176, respectively |
| `pool-swim`, `pool-wade` | 464/659 and 568/840; first unavailable at water entry on ticks 464 and 568, respectively |

All **5,325** covered camera records match out of 6,038 produced.
Seventeen routes cover every tick. Four carry exact named camera cuts.

On `pool-wade` tick 568 the camera numbers still coincide, but Lara
has entered unsupported wading; coverage ends at that source-state
transition. The first raw camera difference is eye Y on tick 569
(retail 5392, port 5416).

The extra arms and interaction records merged on main are not added to
this camera profile. Bounce remains unimplemented by name; the linked
control marks its boundary without stopping Follow or drawing RNG.
See `v4/routes/summary.json`, the per-route reports and the directed
control logs. Further verification commands are in `camera-v4.txt`.

The active, world and inventory comparator reports are unchanged on all
21 routes. TR1's 25 solo fixtures remain byte-identical in each schema
2–5; the TR1 shipping `.text` (437,744 B) and sized defined symbols match
the archived main baseline built for v3. Main's intervening commits are
only specifications; the v4 shipping hot-layout and size gates pass.

## Rejected review snapshot v3 (historical)

Base: `286927fd974ccea04ca11b1261b2f39b7faaeeff`, including the accepted
items slice and the wall amendments Q-T2C-08–10. `camera-v3.txt` binds the
rejected patch and supplementary replay runners in the evidence directory.

All **6,038** canonical records were written and sealed across the 19
fresh-start routes and two placed fixed-view routes. All **4,832**
covered camera records match every field. Fourteen routes cover every
tick; the other seven carry an exact, named camera coverage end.
There is no numeric camera difference inside coverage. All seven
required movement scripts also match every comparable Lara field,
head/torso, both camera gaze slots, and gameplay RNG on all **1,685**
records. Lara field exclusions inherited from the item profile remain
exclusions, not strict matches. See `v3/{final-routes,shipping-host-routes}/`.

| Route | Camera equal / total | First camera gap and owner; first related noncamera difference, if any |
|---|---:|---|
| stand | 301/301 | None |
| turn | 178/178 | None |
| travel | 320/320 | None |
| movement | 224/224 | None |
| jump | 226/226 | None |
| grab-attempt | 252/252 | None |
| roll | 184/184 | None |
| slide-jump | 144/144 | None |
| slide-jump-backward | 208/208 | None; corrected low-Z fallback at tick 93 |
| look | 176/352 | 176 `firearms` (weapons/Combat view); `lara.weaponState` 2 → 0 at 176; subsequent holster and automatic gaze are unverified camera behaviour |
| look-sidestep | 212/212 | None; denied yaw, resumed yaw and release all covered |
| fall-damage | 178/178 | None; the dry heading projection fixes Lara Z, not a target-only correction |
| hard-landing | 140/140 | None; same projection, no bounce event |
| flare | 36/127 | 36 `flare` (arms/equipment); `lara.weaponState` 2 → 0 |
| flare-held | 36/166 | 36 `flare` (arms/equipment); `lara.weaponState` 2 → 0 |
| pistols | 46/116 | 46 `firearms` (weapons/Combat view); `lara.weaponState` 2 → 0 at 46; `rng[0]` first differs at the volley on 76 |
| tiger-cave | 397/464 | 397 `creature-controller` (AI); wake and camera at 396 agree; `rng[0]` first differs on 397 |
| pool-wade | 397/840 | 397 `creature-controller` (AI), the first tiger update; `rng[0]` differs then, `lara.goal` 2 → 21 on water entry at 568 |
| pool-swim | 430/659 | 430 `creature-controller` (AI), the first tiger update; `rng[0]` differs then, `lara.anim` 178 → 0 on water entry at 464 |
| fixed-camera (placed) | 388/388 | None; view 1 expires at 166 and cannot re-arm on re-entry |
| fixed-camera-repeat (placed) | 359/359 | None; boulder subject changes at 17, 58, 222 and 312; timer stays 49 off trigger, expires at 92, then re-arms |

### What changed after the rejected v2

1. The room query returns both the resolved room and the sector actually
   selected by the vertical link. Camera heights and box take that direct
   sector instead of horizontally clamping again. The linked corner
   control changes from floor/box 3072/8 in v2 to 2048/7 in v3.
2. The eye mover keeps raw Y for simulation and trace, while the view
   uses the corrected Y and pitch. The view-publication control keeps
   raw 20, displays 256 (v2 displayed 20).
3. `tr64-tr2-camera` seals a hashed `coverageEnds` map alongside static
   exclusions and all records. It ends camera coverage on the first Draw
   or Flare request while those controllers are unbound; the item gate
   still sees all its wake, deferred-update and later records. A tiger
   merely waking does not end coverage: the next update has unknown
   physical movement, contact and gameplay RNG. The dry-to-water
   boundary uses the pre-movement depth and room conditions of the
   [water specification](specs/wall/tr2-water-and-flares.md#water-status-before-movement).
   An unavailable camera is not part of the proof of other families.
   An isolated rechecksummed pack with that tiger made inert tests the
   water cut itself: 464 on `pool-swim` and 568 on `pool-wade`
   (`v3/water-boundary.log`). The edited pack is not a retail comparison.
4. Q-T2C-08 supplies the cited pre-motion state request table, including
   the +24570 back-jump bearing. In side recovery the preferred side uses
   the old eye on the first axis and the ray-shortened endpoint on the
   fallback axis; only the preferred candidate is sight-tested. At tick
   93 the new eye X is 63661, not v2's 63508.
5. The sequel uses a widened integer bearing ratio and a full-domain
   horizontal length. The old safe-range table/octant results are equal
   on all 16,392 probes; the `(1048576,1048576)` case no longer indexes
   −2048. The first game's helper is unchanged.
6. The cook, native admission, renewal, item/trigger execution, and
   object-surface reader share the sequel's four action-kind bits.
   The `c402` bit-14 control is admitted and executes the same fixed
   action as `8402`. Renewal and execution still use distinct end bits.

Owned switch/key/pickup camera actions are counted as inert; matching
renewal still works. Centre applies in Follow or Fixed without an item
subject. An explicit full-slope request also reaches Look. The eye's
full-slope floor comparison does not skip the clearance ray for a moved
eye with a no-height boundary sample: both placed shots initially place
the smoothed eye well beyond their room. With the ordinary ray check,
all 747 placed records match instead of first differing at tick 1.

Q-T2C-07 uses the next physically stored key and the relative-key versus
absolute-end divisor test. Both roll-bearing routes pass at and after
163. Q-T2C-09 shares the quadrant-folded Q14 sine rule with the ordinary
animated item mover (Lara uses travel angle, other items use yaw); neither
airborne offset nor landing is fitted to a recorded value. At tick 37
the step from Z 23827 is +48. Q-T2C-06 and 10 separate Look yaw from
state-specific pitch, and dedicated sideways gait from raw body turn.
On `look-sidestep` tick 177 the current Turn Right state contributes its
last +45 body yaw while Look consumes Right to give +364 head yaw.

Placement accepts the bounded optional line from the [trace contract](specs/wall/tr2-oracle-trace.md#placed-starts-before-tick-1).
It changes Lara's XYZ, yaw, membership and coarse sector floor after
normal startup and leaves the camera and the world alone. The authored
spawn placement reproduces the entire *port* `stand` trace and footer
byte for byte. The two reviewed placed camera records exercise fresh
startup, one-shot expiry, and expiry of a retained timer only on a
subsequent trigger evaluation. The other four published placed records
are not part of this camera gate.

The caller pins the producer. Each scoped family must reach the last
tick and reason stated for the exact manifest-verified retail trace
digest in `tools/tr2-producer-profiles.json`. A required family with no
cut must compare on the whole script; `--accept-profile-exclusions`
allows a reviewed cut but does not allow a mismatch before it. Rehashed
missing, early, late, wrong-reason and wrong-family cuts are all rejected;
a rehashed camera difference exactly on the last covered tick still
fails. The old item, dry and inherited profiles keep their historical
coverage definitions.

Bounce remains deliberately unimplemented: the linked test requests
negative bounce, observes the named `bounce` coverage end without
stepping the rig, and leaves the gameplay RNG alone. No retail route
exercises nonzero bounce. Weapons, AI, water simulation, switch/key/pickup
controllers, cinematic playback, rendering on an emulator or hardware,
and a TR2 cart are not part of the proof.

### v3 verification

Evidence is under `v3/` unless stated otherwise. Native runtime controls:
`test/game/tr2-camera-test.py`, `tr2-floor-admission-test.py`,
`tr2-input-test.py`, `tr2-item-actions-test.py`, and `tr2-compare-test.py`.
Source-linked controls build `test/game/tr2-camera-geometry.c` with
UBSan and `tr2-packed-frames.c` with section GC. The same tests against
the frozen v2 source or executable reject the reported vertical sector,
display height, large bearing, bit 14, weapon boundary and backward
jump, plus the old packed tail, Look refusal and dry-heading differences.
`make provcheck` passes. `replay-v3.py` verifies all covered camera fields
and writes the source-bound report and exact commands per route.

The TR1 host compares 25/25 fixtures byte for byte in each of schemas
2, 3, 4 and 5 (`solo.log`). The shipping profile uses
`TR64_PLAY=1 TR64_TEST=0` and `pc-campaign21-video.pack`. Its `.text` is
437,744 B, SHA-256 `0aacd23cc8607acc6f2dff60c6509a7b071aa4ec41e5fd5cebd59b074e28eeaa`.
Both `.text` and the defined symbol list are byte-identical to a fresh
build of the stated main base (`shipping-identity.log`). The hot code
window is 16,288/16,384 B and the resident image is 601,880/622,592 B
(`candidate-hot.log`, `candidate-size.log`).

```sh
rtk "$HOST" --game-tr2 "$WALL_PACK" "$FIXTURE/input.txt" "$OUT"
rtk python3 tools/tr2-compare.py "$FIXTURE" "$OUT" --producer tr64-tr2-camera \
  --require camera --accept-profile-exclusions
```

The opening experiment and rejected v2 measurements below are retained
as history, not current v3 claims.

## Before and initial opening implementation

The accepted dry Lara executable (`lara/host/v5/tr64-host`, SHA-256
`91439bcbe8eaae15a3899a7b75889cb55bf60d1d29cc323ca59c9df5c1dff983`) is
the before baseline. The dry camera starts with the inherited seed, and
only the prior slice's look-subject fields are comparable. The opening
implementation calculates the actual tick-0 view before gameplay, executes
and renews the opening fixed view, and uses the documented target-height
easing. The camera profile is unchanged in this first piece.

`before/camera-fields.json` and `opening-v1/camera-fields.json` report the
record count, raw equality, availability and first unequal component and
values independently for all 17 camera fields, on all 13 routes. The
reports validate record and manifest hashes and pin `tr64-tr2-dry`.
The following extract lists all nonmatching raw fields. `N@T` means
`N` equal records overall, with the first difference at tick `T`. Fields
not listed have equal raw values throughout. A raw match of an excluded
field is diagnostic only, never a strict or comparable match.

| Route | Records | Before: mismatching raw fields | Opening: mismatching raw fields |
|---|---:|---|---|
| stand | 301 | pos 0@0, target 94@0, room 237@5, angle 0@0, speed 0@0, lastIndex 225@1 | pos 54@54, room 293@54, angle 57@54, speed 1@1 |
| turn | 178 | pos 0@0, target 94@0, room 114@5, angle 0@0, speed 0@0, lastIndex 102@1 | pos 54@54, room 170@54, angle 57@54, speed 1@1 |
| travel | 320 | pos 0@0, target 169@0, room 254@5, targetRoom 313@204, angle 1@0, speed 0@0, lastIndex 244@1 | pos 54@54, target 276@204, room 310@54, targetRoom 313@204, angle 59@54, speed 1@1 |
| movement | 224 | pos 0@0, target 110@0, room 160@5, angle 0@0, speed 0@0, lastIndex 148@1 | pos 54@54, room 216@54, angle 58@54, speed 1@1 |
| jump | 226 | pos 0@0, target 114@0, room 155@5, targetRoom 206@204, angle 0@0, speed 0@0, lastIndex 150@1 | pos 54@54, target 204@204, room 211@54, targetRoom 206@204, angle 57@54, speed 1@1 |
| grab-attempt | 252 | pos 0@0, target 120@0, room 188@5, angle 0@0, speed 0@0, lastIndex 176@1 | pos 54@54, target 232@163, room 244@54, angle 57@54, speed 1@1 |
| roll | 184 | pos 0@0, target 107@0, room 120@5, angle 0@0, speed 0@0, lastIndex 108@1 | pos 54@54, target 164@163, room 176@54, angle 57@54, speed 1@1 |
| flare | 127 | pos 0@0, target 94@0, room 63@5, angle 0@0, speed 0@0, lastIndex 51@1 | pos 54@54, room 119@54, angle 57@54, speed 1@1 |
| flare-held | 166 | pos 0@0, target 94@0, room 102@5, angle 0@0, speed 0@0, lastIndex 90@1 | pos 54@54, room 158@54, angle 57@54, speed 1@1 |
| pistols | 116 | pos 0@0, target 29@0, room 75@5, angle 0@0, speed 0@0, timer 46@46, lastIndex 40@1 | pos 46@46, target 46@46, room 85@46, angle 46@46, speed 1@1, timer 46@46 |
| tiger-cave | 464 | pos 0@0, target 242@0, room 399@5, targetRoom 457@223, angle 3@0, speed 0@0, lastIndex 388@1 | pos 54@54, target 421@223, room 455@54, targetRoom 457@223, angle 60@54, speed 1@1 |
| pool-wade | 840 | pos 0@0, target 346@0, room 752@5, targetRoom 573@223, angle 6@0, speed 0@0, lastIndex 764@1 | pos 54@54, target 539@223, room 808@54, targetRoom 573@223, angle 63@54, speed 1@1 |
| pool-swim | 659 | pos 0@0, target 224@0, room 591@5, targetRoom 536@223, angle 9@0, speed 0@0, lastIndex 583@1 | pos 54@54, target 421@223, room 647@54, targetRoom 536@223, angle 66@54, speed 1@1 |

Both implemented look-subject fields compare equal on every tick in all
13 routes. Strict complete-family equality is zero in both profiles.
In the opening implementation, all raw camera fields agree at tick 0 on
all 13 routes; the first fixed camera agrees on tick 1 except sampled
`speed`. For the seven movement routes (and all but `pistols`), eye
coordinates agree through tick 53, and `lastIndex` agrees throughout.

## Merged camera rules, review snapshot v2

The dry producer now compares all 17 fields strictly. Its inherited-rule
producer still excludes the camera family; no numeric mismatch is hidden
by either profile. `after-v2/camera-fields.json` gives the after count and
first unequal component and values for every field on every route.

| Route | Full equal / records | First unequal camera field (retail → port) |
|---|---:|---|
| stand | 301/301 | — |
| turn | 178/178 | — |
| travel | 320/320 | — |
| movement | 224/224 | — |
| jump | 226/226 | — |
| grab-attempt | 229/252 | 163 `target[1]`: 5099 → 5094 |
| roll | 163/184 | 163 `target[1]`: 5099 → 5094 |
| flare | 127/127 | — |
| flare-held | 166/166 | — |
| pistols | 46/116 | 46 `pos[0]`: 62047 → 61952 |
| tiger-cave | 464/464 | — |
| pool-wade | 569/840 | 569 `pos[1]`: 5392 → 5416 |
| pool-swim | 464/659 | 464 `target[1]`: 5421 → 5418 |

Both roll-bearing routes match through tick 162; their eye Y first
differs at 164. Comparable Lara and gameplay RNG still match all 1,685
required records. `pistols` stops matching when the weapon controller
would request Combat, before an eligible Combat camera state exists in
the host. The two pool first differences follow the still-inert water
rules; the camera does not infer a water mode from the fixture or inputs.

## Cited rules

- `TR64_RULE(CAMERA_VIEW)` in `game/tr2.h` is selected through the existing
  dry host mask at `game/game.c:451`; the inherited-rule harness does not
  set it. The current first-game cart folds it out.
- [`tr2-lara-start.md`, Camera seed, placement and the opening view](specs/wall/tr2-lara-start.md#camera-seed-placement-and-the-opening-view):
  seed and aim elevation, a click inside the failed adjacent rear sample,
  radius recovery and side preference, first divisor 1 and retained
  Follow speed 10, the inherited integer bearing, and quarter-step target
  height easing truncated toward zero. The rear sample, radius, eye and
  its room are derived from room geometry, not pasted from the record.
- [`tr2-triggers-and-interaction.md`, Floor evaluation and Item masks, timers and action ordering](specs/wall/tr2-triggers-and-interaction.md#item-masks-timers-and-action-ordering)
  and [I's trigger specification §6](specs/wall/triggers-and-effects.md#6-cameras-and-look-at-targets):
  camera renewal comes before the trigger qualifies; its end mark is the
  action word, not the extra word; the execution reader uses the extra
  word. Fixed view arming uses the authored one-shot, timer × 30, and
  speed code × 8 + 1; a positive countdown expires to −1. The two reads
  are checked separately by native TR2 gameplay admission. Only fixed and
  target words in the renewal-only suffix carry references; Wall's
  authored `0xfd00` ends renewal without taking an action. Switch/key/pickup
  controllers remain inert for execution, but can renew a matching view.
- [`tr2-camera.md`](specs/wall/tr2-camera.md), Aim point and modes,
  the Look rig, Two different ways to keep a point clear, and Move the eye
  and store the view: use the small-axis-first terrain line, a separate
  point/box correction, the two Follow/Look alternatives with the
  shortened endpoint's room as the room seed, raw-eye easing and the
  display-corrected room check. Auto-Look consumes the existing head and
  torso turn and eases its horizontal target after choosing an eye.
  Fixed uses the authored start, cast and inset; publication resets the
  ordinary speed to 10 after moves at divisor 1, 10 or 4. Bearing remains
  the inherited integer helper, including the asymmetric octant.
- `game_tr2_camera_heights` in `game/room.c` is the shared linked height
  interface; it asks the existing floor/ceiling implementation to
  flatten only steep slopes where the camera requires it. Lara's current
  callers still use full slopes. The pre-movement slide request already
  comes from the shared forward-slide handler.
- **Implementers' choice, not asserted as retail behavior:** without a
  point box keep X/Z and apply only the requested terrain Y correction.
  With a missing target box use the endpoint box if present, otherwise
  keep the terrain-shortened endpoint. Neither path was taken on the 13
  routes. The authored opaque renewal terminator was read 58 times per
  route and never executed.

## Wall questions

1. **Q-T2C-01 to 04:** answered in
   [`tr2-camera.md`](specs/wall/tr2-camera.md). The replay reproduces the
   fixed cast/inset (`stand` 54), return to Follow (77–78), automatic Look
   (`travel` 204), and the distinction between actual and sampled speed.
2. **Q-T2C-05 and 06:** bounce RNG/order and manual-Look state eligibility
   are outside this dry slice. A raw Look input, a nonzero requested
   bounce, or an active Combat camera is refused by name and counted
   before the first affected tick could be sealed. None of the original
   13 inputs presses Look; none of their retail samples has a nonzero
   bounce and none of the dry replays requests it. A separately published
   Look fixture is not silently substituted for the original scope.
3. **Q-T2C-07:** `roll` and `grab-attempt` tick 163, `camera.target[1]`.
   Lara agrees at y5451, clip 147 frame 3916, previous target Y5114.
   The packed sampler uses key 4 bounds Y(−519,9), then linked
   148/3917 bounds (−606,6), fraction 1/2; this gives aim 5031 and target
   5094. Clip 147 has a separate stored tail key (−566,5); what chooses
   the final endpoint and the divisor for bounds and pose? Both
   differences remain unmasked pending the wall answer.

## Directed controls

`test/game/tr2-camera-test.py` exercises the spec's opening, fixed,
Follow and Look checkpoints, the inherited-rule first-eye negative and
the named/unsealed manual-Look refusal. `test/game/tr2-floor-admission-test.py`
checks the renewal-only suffix, including an opaque terminal word, and
rejects missing termination, truncated extra words and out-of-range
camera, item and fixed-eye room references. The rechecksummed controls
reach native gameplay admission. A separate changed-authored-record
control (`fixed-speed-control-v2.log`) uses speed code 4 and timer 1:
actual first move divides by 33 and samples 10; the timer runs 29→1→−1
without re-arming while the same camera is still nominated.
Source-linked checks renew on an unqualified pad and an inert switch,
read a renewal-only subject past an execution end, and refuse a later
one-shot re-arm (`camera-trigger-v2.log`). Separately, two rechecksummed
packs with a removed box witness the no-box counters at tick 0 for
shared goal correction and tick 54 for fixed point clearance
(`no-box-controls-v2.log`).

The exact per-route commands are in `after-v2/commands.json`; for one
fixture run `rtk "$HOST" --game-tr2 "$WALL_PACK" "$FIXTURE/input.txt" "$OUT"`,
then `rtk python3 tools/tr2-compare.py "$FIXTURE" "$OUT" --producer tr64-tr2-dry --require camera`.
The full strict seven-route gate still returns a difference on the two
known clip-tail cases; this is not a profile exception.

## First-game invariant and size

All 25 TR1 solo fixtures were byte-identical in each of schemas 2, 3, 4
and 5 (`solo-v2/`). The first-game ELF's `.text` (437,744 B) and all
defined symbols, `.data` (40,504 B), `.bss` (26,200 B) and `.rodata`
(85,448 B, after stripping the fixed-size build/config identity strings)
match the shipping baseline. Hot and resident-size gates passed
(`cart/compare-v2.txt`, `cart/hot-check-v2.log`, `cart/size-gate-v2.log`).

Against `a58bafe`, the changed host objects grew by 10,622 B `.text` and
432 B `.rodata` including the trace writer, and 10,302 / 260 B without
it. The corresponding enabled VR4300 object delta is 10,208 / 404 B
including the writer, 9,952 / 283 B without; this is not a linked cart
size. No writable static data was added (`object-size-v2.txt`); the
host-only diagnostic counters add 32 B to `GameTR2`.

The rejected v2 runtime and profile were frozen as follows:
`camera-v2.patch` at `a58bafe`, SHA-256
`99f4f56478c9e840f3dd191adeafc4e89fbf013a4af3029ce6d09266c25e829a`,
with the directed test and instructions in `camera-v2.txt`.
No rendered eye check, TR2 cart, emulator, camera cinematics, manual
Look movement, bounce, weapons or water parity was attempted.
