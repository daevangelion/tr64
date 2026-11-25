# M10: Lara's arms in The Great Wall (host)

This slice adds held and loose flares and paired pistols to the Great Wall host
harness. The other weapons, water locomotion, creature controllers,
manual Look and camera motion are separate. No game data or record is
required by the implementation. Evidence: `$TR64_WORK/evidence/m10/arms/`.
All results include tick 0. The retail fixture is verification, not a rule
source. The initial implementation baseline is the M10 items merge,
`90217e8af14d964fbd944b985efa30ff03184e63`; replay uses the unchanged
`wall.pack` (SHA-256 `d5e1cb90646be0bdb94545e369eca0c118a5684a68cbcc9e2c454de73a59a02e`).

## Before

Raw equal records / first differing tick among values in each column.
A dash means all records in that column agree before this work. It does
not turn a default value from an unavailable field into evidence of behavior.
The complete field-by-field data is `before-metrics.tsv`.

| Route | Records | Weapon state | Weapon / goal | Right arm | Left arm | Flare age | Flare Left | Inventory | Pistol reserve / ammo used / gameplay RNG |
|---|---:|---|---|---|---|---|---|---|---|
| flare | 127 | 66 / 36 | 36 / 36 | — | 36 / 36 | 75 / 75 | 63 / 36 | 49 / 49 | — |
| flare-held | 166 | 105 / 36 | 36 / 36 | — | 36 / 36 | 75 / 75 | 63 / 36 | 49 / 49 | — |
| flare-throw | 1926 | 1834 / 36 | 1784 / 36 | — | 36 / 36 | 75 / 75 | 1842 / 36 | 49 / 49 | — |
| flare-drop | 421 | 86 / 36 | 310 / 36 | 147 / 147 | 36 / 36 | 75 / 75 | 337 / 36 | 49 / 49 | — |
| pistols | 116 | 46 / 46 | — | 46 / 46 | 46 / 46 | — | — | — | 76 / 76 for each field |
| look | 352 | 217 / 176 | — | 176 / 176 | 176 / 176 | — | — | — | — |
| stand | 301 | — | — | — | — | — | — | — | — |
| turn | 178 | — | — | — | — | — | — | — | — |
| movement | 224 | — | — | — | — | — | — | — | — |
| jump | 226 | — | — | — | — | — | — | — | — |
| grab-attempt | 252 | — | — | — | — | — | — | — | — |
| roll | 184 | — | — | — | — | — | — | — | — |
| travel | 320 | — | — | — | — | — | — | — | — |
| tiger-cave | 464 | — | — | — | — | — | — | — | gameplay RNG 397 / 397 |

At tick 36 the flare routes first switch the selected and requested
weapon to 8 and the hand state to drawing. At tick 49 stock goes from 2
to 1; the items baseline never spends it. Pistols first draw at tick 46;
`look` first draws at 176. The items baseline also differs in the
`pistols` body at 78 and `look` head/torso at 280. Body and camera figures
are tracked independently from the requested weapon columns.
The original port lacks loose active slots from `flare-throw` tick 167
and `flare-drop` tick 147. `before-release-metrics.tsv` includes the
throw's first body and retained-counter differences at tick 147.

## Runtime rules

The host-only [`ARMS` rule](../game/tr2.h) binds the replacement
handlers at level entry. `game/tr2_arms.c` is on the existing host
source list; when a TR2 cart exists it belongs to that cart's source
list. Core/pose ABI still uses the same loaders and authors' animation
headers. The arm handlers store the authored absolute frame number and
use the auxiliary model animation base (the body animation is independent).
`game/lara_weapon.c` supplies the shared aim-point geometry, slew and
ballistic spread. No camera source is modified.

* [Ground and weapons, Aiming and resolving a shot](specs/wall/tr2-lara-ground-and-weapons.md#aiming-and-resolving-a-shot):
  dry paired-gun origin 650 above Lara, strict range and lock cones,
  remembered target under Action, separate retention and aim per arm.
  The only eligible creature in this harness is a live awakened tiger.
  A selected tiger exposes the port's live pose (which does not simulate
  later AI); aimed shots against it are counted but sphere/hit resolution
  and damage are outside this slice, not simulated with a fake target.
  Untargeted projectile attempts use the ordinary world line trace.
* [Lara arms, Pistol attempts and poses](specs/wall/tr2-lara-arms.md#pistol-attempts-and-random-draws-q-t2a-01):
  draw begins at 5, swaps meshes at 13, and reaches ready at 23.
  Aim builds through 0–4. Each shot starts flash 3 and recoil at 24;
  the two arms advance independently. Holster wins over fire, waits
  for both arms at 5 and retains their clip while resetting counters to
  zero. Each bullet resets the shared reserve to 1000 and spends one,
  then draws yaw and pitch from the 32-bit gameplay stream, left hand
  before right. Signed spread truncates toward zero. Each pair gives
  one pistol sound request. The record corroborates only the post-tick
  values; refill/spend ordering, hand/axis ordering, RNG output bits
  and angle arithmetic are established statically.
* [Slide, height and gaze, Paired-pistol holster](specs/wall/tr2-lara-slide-height-and-gaze.md#manual-look-eligibility-on-dry-ground-q-t2c-06):
  after arm stepping, including on the last holster tick, replace head
  and torso pitch/yaw with the sum of both aims divided by four, toward
  zero, unless Look is held. Auto-gaze can subsequently turn them.
* [Lara arms, Held flare](specs/wall/tr2-lara-arms.md#held-flare-selection-lighting-and-overlay-q-t2a-03):
  held Flare requests a draw or stow, subject to hand-state priority.
  The left arm spends stock at 46, lights at 72 and increments age
  after testing the current light. Hands become free at 94. Clearing
  the overlay in an ineligible body state does not stop the age; an
  eligible free pose restarts at 95 and loops directly to 0 after 109.
  The right arm has no flare overlay in this controller.
* [Lara arms, Letting go](specs/wall/tr2-lara-arms.md#letting-go-and-the-loose-items-life-q-t2a-04):
  standing stow retains a separate absolute body throw frame.
  Throw releases at arm phase 21; eligible Draw drops immediately.
  `game_tr2_flare_hand()` in `game/pose.c` uses the shared packed
  body sampler: exact body keys use the left shoulder translation;
  fractional body samples blend through the torso before taking the
  right shoulder translation with left-arm rotations. `releaseFlare`
  allocates a real world item; its timer and stored coarse floor are
  distinct from age and geometry. Its handler spins, moves, resolves
  rooms and contacts, and retires an expired stationary flare.
  Loose horizontal movement uses `game_tr2_heading_step` from
  [Ground and weapons, Projecting speed along a heading](specs/wall/tr2-lara-ground-and-weapons.md#projecting-speed-along-a-heading-q-t2c-09).
  The port does not render illumination or bubbles; no loose
  retrieval, wet trajectory or loaded flare has retail replay evidence.
* [The public II animation-effects list](specs/tr2-tr3-differences.md#4-world-rules-and-their-execution)
  identifies turn (0), bubbles (3), and hands free (12). Ground Roll
  turns yaw only; the specified airborne reversals and underwater
  roll also reverse pitch ([Ground and weapons](specs/wall/tr2-lara-ground-and-weapons.md)
  and [Water and flares](specs/wall/tr2-water-and-flares.md)).
  The authored body bank has six turn commands, 26 bubbles cues, and six
  hands-free commands; the selected auxiliary banks have none. The host
  runs a distinct II Lara dispatch, counts bubbles without playing them,
  and refuses any other effect number at admission. Numeric identity
  with a TR1 effect is not a rule.
  Independent review found the same body effect roster in all three
  checked PC level files: `WALL.TR2`, `BOAT.TR2`, and `venice.TR2`.
  The latter two were admitted as reduced host inputs to check animations;
  their stock packs exceed the arena, so this does not claim full-level
  admission or play.
* Arms admission walks the authored Lara body bank and the selected
  auxiliary clips. The Great Wall's body links stay within that bank
  and the destination clips' intervals. Each addressable key must hold
  the nine-word header and all 15 rotation words or word pairs, including
  the physical far key of a fractional last interval. The far key is
  selected by the shared camera-merged sampler, not a second tail rule.
  This admission restriction does not change the sampler used for the
  world animations or the historical baseline.

**Camera handoff:** `game_tr2_firearms_camera_requested()` reports that
this tick executed a pistol drawing or ready handler. It does not choose
the view. The canonical hand state is `lara->extraL->weaponState`
(`WEAPON_STATE_DRAW`, `WEAPON_STATE_HOLSTER`, `WEAPON_STATE_READY`),
and the predicate is already available before normal floor actions.

## Historical v1 measurement

The original v1 producer inherited the items profile. It compared the live
`flareAge`, `flareFrame`, `flareLeft`, `flareRight` and `target` in
addition to the already compared arm records, weapon selection and
statistics fields. `weaponItem` belongs to a later long-gun animation
item, and `save.ammoHits` stays unavailable. Neither look angles nor the
creature-owned gameplay RNG difference are excluded.

| Route | Records | Equal weapon/goal/state, both arms and held flare fields | Equal inventory | Equal pistol reserve | Equal ammoUsed | Equal gameplay RNG |
|---|---:|---:|---:|---:|---:|---:|
| flare | 127 | 127 | 127 | 127 | 127 | 127 |
| flare-held | 166 | 166 | 166 | 166 | 166 | 166 |
| flare-throw | 1926 | 1926 | 1926 | 1926 | 1926 | 1926 |
| flare-drop | 421 | 421 | 421 | 421 | 421 | 421 |
| pistols | 116 | 116 | 116 | 116 | 116 | 116 |
| look | 352 | 352 | 352 | 352 | 352 | 352 |
| stand | 301 | 301 | 301 | 301 | 301 | 301 |
| turn | 178 | 178 | 178 | 178 | 178 | 178 |
| movement | 224 | 224 | 224 | 224 | 224 | 224 |
| jump | 226 | 226 | 226 | 226 | 226 | 226 |
| grab-attempt | 252 | 252 | 252 | 252 | 252 | 252 |
| roll | 184 | 184 | 184 | 184 | 184 | 184 |
| travel | 320 | 320 | 320 | 320 | 320 | 320 |
| tiger-cave | 464 | 464 | 464 | 464 | 464 | 397 |

`release-first-metrics.tsv` measures individual fields;
`release-first/` contains the pinned comparator results. Across the
5,257 records, all target arm fields, inventory, world digests and
profile-comparable active fields agree. No target fields regress on
the seven movement routes. `pistols` issues the four two-shot volleys
at 76, 86, 96 and 106. Both arms, display reserve (999 after each
volley), ammo-used counter and gameplay RNG match every tick.
`pistols` has no selected creature; it cannot verify live-target aim
or damage. In `tiger-cave` the tiger wakes at tick 396; its missing
first AI update at 397 is also the first gameplay RNG difference.
Lara never draws or fires there, so matching arm fields are not
evidence of creature combat. Strict whole-active equality is still
zero because the existing door-touch exclusion is inherited.

Both release traces match every recorded field of the loose flare
(1,983 matching active samples). Throw starts at arm frame 1 at
147, spawns at 167, crosses to room 36 on 238, rests at 249, and
retires at 1876. Drop enters gun draw and spawns at 147, then rests
on 187. These are measured on the pre-integration heading primitive,
not a gate on general oblique motion or on the upcoming shared helper.

`look` arm and gun fields match independently of the camera; its first
head/torso mismatch is at 280, when the existing auto-gaze path turns
Lara toward the marker while the retail firearms view suppresses that
nomination. The paired holster resets the head/torso source angles on
293–311 inclusive, and both match on those ticks following auto-gaze.
Manual Look and Combat routing are the camera slice, not modeled as a
weapon-side special case.

### Wall questions

Q-T2A-01 through 04 are closed for the established scope by the
[merged arms specification](specs/wall/tr2-lara-arms.md#implementer-question-status).
Separate coverage gaps are the combat camera, actual sphere hits and
damage to live creatures, pickup and save behavior, and underwater held
feedback. **Q-T2A-05** asks for the exact II bubbles effect, including its
random draws, and the complete II animation-effect dispatch table. The
26 authored cues are counted but inert pending the water/effects slice.

## Historical v1 first-game isolation

The PC shipping build and items baseline have byte-identical `.text`:
437,744 B, SHA-256
`0aacd23cc8607acc6f2dff60c6509a7b071aa4ec41e5fd5cebd59b074e28eeaa`.
Hot-layout passed (41 ordered sections, 16,288 B render window), and
the resident size gate passed at 601,880 / 622,592 B. The 25 solo
TR1 fixtures are byte-identical in each of schemas 2–5. See
`release-shipping-identity.log`, `release-hot-check.log`,
`release-size-gate.log` and `release-solo/`. No emulator or TR2 cart
was run. Compared to the item-slice linked host, the candidate adds
11,264 B `.text` and 336 B `.rodata`, no `.data` or `.bss`, and 16 B
of Great Wall game state (175,096 / 196,608 B).

The directed host check exercises held-light stock and ignition, the
separate standing throw clock, one-tick-deferred world motion, expiry
retirement, held Flare repeat, simultaneous Draw/Flare drop, Draw over
fire during recoil, pistol reserve/cadence, four logic RNG draws per
volley, a positive and negative target cone, and
admission refusal of a shortened arm clip. The items binary fails at
the first flare draw; the held-only checkpoint fails at throw onset
(`controls-before.log`, `release-controls-held-red.log`, and
`release-controls.log`). A synthetic target is not retail combat evidence.

The v1 checker commands are in `release-first/commands.json`. To run the
directed check on the current host:

```sh
PYTHONPATH=tools/cook TMPDIR=$TR64_WORK/tmp/T2Items \
  rtk python3 test/game/tr2-arms-test.py $TR64_WORK/builds/T2Items/host/tr64-host \
  $TR64_WORK/evidence/m10/wall/wall.pack
```

The superseded `arms-v1.patch` SHA-256 is
`c19af2148bc5ee53fee476318c40f037e0916bd23caad8ada6f4ef03b695b40d`
on `5fc84e2107c4632ae122cdb0526042afba2edd80`.

## v2: camera integration and admission review

The candidate is based on `bd06f5ee12e75a3f114a950e0777aefc419409b8`,
the camera merge. `tr64-tr2-arms` inherits the camera producer's field
and route coverage; only the five now-produced arm/target paths are
removed from the exclusions. The `pistols` and `look` camera coverage
still ends before firearm draw (ticks 46 and 176); the new `flare-drop`
record has that same boundary at 147. The added placed records otherwise
report camera differences normally, without a new exclusion. The
combat camera still belongs to its own slice.

All 21 fresh starts (7,638 records) match for both arms, held flare
fields, `weaponState`, selected/requested/last/back weapon, target,
ammunition, `save.ammoUsed`, inventory, `world`, and profile-comparable
active state. Both loose flare routes match all 1,983 active samples
after the shared projection cutover, including the release, settling,
transfer to room 36 and retirement checks above. The separate head/torso
and logic-RNG gaps remain visible: `look` first differs at 280, and creature
activity changes logic RNG first at 397 in `tiger-cave` and `pool-wade`,
and at 430 in `pool-swim`.

Nine placed starts add 3,060 records. Counts below are compared rows;
any first difference is in parentheses. Busy hands during interactions
are the only divergence among the requested arm and weapon fields:
`ladder` (380/852) belongs to climbing; `pickup` (85/151) and
`switch-door` (195/286) belong to the pickup and switch interaction
controllers, respectively. None of these differences is excluded.

| Placed route | Records | Weapon state | Active | World | Inventory | Camera |
|---|---:|---:|---:|---:|---:|---:|
| fixed-camera | 388 | 388 | 388 | 388 | 388 | 388 |
| fixed-camera-repeat | 359 | 359 | 359 | 359 | 359 | 359 |
| ladder | 852 | 380 (43) | 852 | 852 | 852 | 5 (5) |
| ledge-grab | 356 | 356 | 356 | 356 | 356 | 252 (121) |
| pickup | 151 | 85 (1) | 151 | 151 | 151 | 1 (1) |
| placed-stand | 301 | 301 | 301 | 301 | 301 | 301 |
| placed-travel | 288 | 288 | 288 | 288 | 288 | 288 |
| sidestep-blocked | 79 | 79 | 79 | 79 | 79 | 66 (28) |
| switch-door | 286 | 195 (1) | 1 (1) | 160 (48) | 286 | 2 (2) |

In `switch-door`, the inactive lever means the active-list length
differs first and the door sector digest later changes only in retail.
See `v2/replay/summary.json` for every count, exact command and compared
trace hash. No profile gap hides these differences.

The directed controls also submit nine-word-only held and body throw
keys (clips 254 and 189), later idle rotation tags, and the throw's
physical far tail key. They require named admission refusals before
tick 0. The same test injects a well-formed command for effect 2 and
requires a named refusal; a valid bubbles cue runs and is counted once
without running the TR1 effect. The controls fail against the frozen v1
producer and pass against v2 (`v2/admission-v1-red.log`,
`v2/directed-final.log`). `v2/comparator.log` and `v2/provcheck.log`
contain the comparator and provenance checks; a rehashed earlier or
later `pistols` camera cutoff is rejected (`v2/inherited-cutoff-negative.log`).

The 25 TR1 solo fixtures are byte-identical to clean main in each of
schemas 2–5 (`v2/solo.log`). The PC shipping cart's `.text` and defined
symbol list are byte-identical to that independent build
(`v2/shipping-identity.log`): 437,744 B `.text`, SHA-256
`0aacd23cc8607acc6f2dff60c6509a7b071aa4ec41e5fd5cebd59b074e28eeaa`.
Hot and resident gates passed at 16,288 / 16,384 B and
601,880 / 622,592 B (`v2/build-final.log`). The linked host adds
12,672 B `.text` and 848 B `.rodata`, with unchanged `.data` and `.bss`;
Great Wall game state adds 20 B to 175,176 / 196,608 B.
No emulator or TR2 cart was run. The tested candidate host is archived
as `v2/tr64-host`, with replay and solo drivers beside it.

The frozen runtime/profile review input was `arms-v2.patch`, SHA-256
`f599c1b7773dcf58720e2d144ae039273d87df3216678ddadef60178a2e6d884`.
`arms-v2-tests.patch` is separate. Both apply to the base given above.
