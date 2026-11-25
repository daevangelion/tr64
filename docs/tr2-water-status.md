# M10: Lara in water in The Great Wall (host)

Evidence: `$TR64_WORK/evidence/m10/water/`. The implementation uses the
[water and flares specification](specs/wall/tr2-water-and-flares.md) and
authored level animation/floor data. The schema-20 fixtures verify the rules;
they are not a source of rules. The pack is the same `wall.pack` used by the
arms slice, SHA-256 `d5e1cb90646be0bdb94545e369eca0c118a5684a68cbcc9e2c454de73a59a02e`.
The accepted arms producer is the before baseline. All counts include tick 0.

## Before

| Route | Records | First Lara difference | First raw camera difference | First logic RNG difference |
|---|---:|---|---|---|
| `pool-wade` | 840 | 568: `goal` (Stand instead of continued sidestep), `water` (Wade instead of dry) | 569: `camera.pos[1]` | 397 |
| `pool-swim` | 659 | 464: `anim` (178 instead of 0), `water` (Wade instead of dry) | 464: `camera.target[1]` | 430 |

The arms baseline cut camera coverage at water entry, so those camera rows
were not counted by its pinned comparator. `before-fields.txt` records raw
equality counts and the first difference for every Lara and camera field.

## Grounded runtime rules

`TR64_RULE(WATER)` is bound only to the host TR2 gameplay profile. The
other masks are unchanged; the host mask is widened to leave the climbing
slice's bits 30 and 31 available. The cart keeps its TR1 layout and control.

* **Waterline, mode and air:** [Water status before movement](specs/wall/tr2-water-and-flares.md#water-status-before-movement).
  Resolve horizontal portals first, then use the last wet ceiling or last
  dry floor click before the vertical medium boundary. Use the live floor
  for column depth. Keep the pre-movement `waterDistance` for the whole
  update. The shallow dry-to-wade case needs feet depth above 384 and
  column depth at most 474. Wading above 730 enters the appropriate
  surface pose and its control runs that tick. Air is selected before
  motion by that update's medium; a dive decided after a surface stroke
  spends no underwater air on the dive tick itself.
* **Wade:** [Wading, swimming, ledge exits and reversal](specs/wall/tr2-water-and-flares.md#wading-swimming-ledge-exits-and-reversal)
  and [Collision envelopes, Q-T2W-02](specs/wall/tr2-water-and-flares.md#collision-envelopes-q-t2w-02).
  A grounded entry requests Stand, without discarding its current clip.
  An ongoing run can select Wade. A wading idle selects Wade on Forward
  with or without Walk, in the priority order below; the level provides
  its lead-in and continuation frames and speeds.
  The upright collision uses a 100-radius probe. Forward Wade permits an
  open drop and rises up to 384, blocks steep rises but does not turn
  descending slopes into pits, and follows at most 50 downward per tick.
  The wading back/side gaits keep their separate inherited raise limits
  and an open drop. Their frontal shifts restore the perpendicular axis.
  The sampled middle floor drives the current height adjustment; the
  stored room floor refreshes after the position correction.
* **Surface:** [Surface strokes, room membership, and Dive from the surface](specs/wall/tr2-water-and-flares.md#wading-swimming-ledge-exits-and-reversal).
  Tread reduces the counter by 4; strokes add 8 up to 60. Translate after
  animation advance using the retained travel angle and signed product
  divided by 65536, then refresh that heading during collision. The new
  pose selects the 800-high envelope, 700 below origin, with front rise
  limit 384 for forward stroke and 128 otherwise. Refresh membership after
  movement and collision at Y+100, including the tick that dives. Forward
  plus Jump requests tread; only actual tread updates count toward 10,
  and that tick spends the decayed surface stroke before entering 119.
* **Underwater:** [Underwater strokes, collision, and Roll](specs/wall/tr2-water-and-flares.md#wading-swimming-ledge-exits-and-reversal).
  Choose acceleration/deceleration and pitch from the starting pose, then
  advance animation, translate and resolve collisions. Round underwater
  X and Z first at the heading product and again with pitch cosine. Use
  three pitch-dependent probe quantities: body height `max(200,abs(sin
  pitch * 762 >> 14))`, half height as the foot reference offset, and the
  negative of that height as the front rise limit. At tick 633 in
  `pool-swim`, a 711-high body with foot reference 6401 at proposed Y 6046
  is moved up one against the floor at 6400; pitch corrects from −12558
  to −12194. The fixed TR1 400-high body cannot produce that correction.
* **Animation cues:** [Numbered animation effects and bubbles, Q-T2A-05](specs/wall/tr2-water-and-flares.md#numbered-animation-effects-and-bubbles-q-t2a-05).
  The low 14 bits choose the effect. High bits filter the authored frame
  cue using the pre-movement water distance. Keep the existing turn and
  hands-free handlers; 16 and 17 are valid inert II slots. Reject
  unsupported numbers before tick 0. Effect 3 is presentation-only; count
  accepted cues and leave visual/sound dispatch inert in the host. It draws
  nothing from either stream. The four reached cues in `pool-swim` are
  counted; neither their presentation stream totals nor their visuals are gated.

## Boundaries and unexercised rules

* **Q-T2W-01:** host bubble emission and lifetime are left to the cart
  presentation slice. One earlier presentation draw per cue tick is not
  established; `rng[1]` is ungated. The tiger's first update is the logic
  RNG boundary (397 on `pool-wade`, 430 on `pool-swim`); equality of
  `rng[0]` is gated before, not after, those ticks. No RNG field is erased.
* **Q-T2W-02:** the probe envelope table is the source of the implemented
  limits. The two records exercise the wading front/side bank and the
  dynamic submerged body; they do not isolate every equality boundary,
  backward or sideways surface translation, a surface rise from 128 to
  384, other static wet interfaces, or drowning at zero air.
* **Q-T2W-03:** an executed current sink refuses the host run by name;
  it does not apply a first-game displacement. The census checked 2,970 lists
  and found **zero** sink actions in all of The Great Wall, not just in the
  recorded routes (`floor-census.log`). A later current slice owns the
  documented separate fixed-400 probe and an exercising record; the TR1
  current handler is not run on TR2.
* **Q-T2W-05:** the conditions for a possible wade step-up (middle
  rise 129–384) and a wade splat (flat front rising more than 640)
  are known, but the left/right clip selection by Wade animation
  phase is not. Both outcomes refuse with their own diagnostic instead
  of guessing Run's frame intervals. Neither occurs in the pool records.
* **Q-T2C-12:** camera coverage for an update beginning in Wade with a
  sidestep pose (21/22) is not yet supported. This state first occurs on
  `pool-wade` 569. The camera's first raw difference is eye Y 5392 vs 5416,
  with all Lara and other camera fields equal. `pool-swim` wades only in
  forward Wade poses and keeps full camera coverage. Neither boundary is a
  script-name check; the pinned producer rejects a different cutoff.
  The [camera specification](specs/wall/tr2-camera.md#requests-in-and-out-of-water-q-t2c-12)
  now gives the missing request. Applying it and reopening coverage
  belong to the camera integration, not the wet movement handlers.
* The wade-to-dry exit, underwater yaw-steering control, and surfacing
  from below refuse the host run by name rather than sealing a trace
  whose Lara state is incomplete. A wet weapon hit, current, underwater
  death, and deliberate surface ledge exit have no parity evidence here.

| Named refusal | Evidence needed to enable the path |
|---|---|
| `exit from wade` | A recorded wade-to-dry crossing, including the below-384 threshold and the resulting gait and room membership. |
| `underwater steering` | A submerged Left/Right route showing turn-rate and bank changes, damping, and travel. |
| `underwater surfacing` | An ascent from below through the air-room/waterline check into surface tread, including the held-dive protection. |
| `current sink` | Q-T2W-03: a route with an authored sink that distinguishes current waypoint displacement and its pre-stroke collision from ordinary swimming. |
| `wade step-up pose` | Q-T2W-05: the left/right animation-phase rule and a wading step-up record. |
| `wade splat pose` | Q-T2W-05: the left/right animation-phase rule and a wading frontal-wall record that reaches the splat condition. |

* **Q-T2W-06 (resolved by the delta below):** a wading Stand with
  simultaneous Forward and Jump exercised the old Stand priority.
  The port requested Compress (73/1178) at `pool-wade` 775; retail
  requested Wade (186/5197). The amended rule distinguishes
  forward-plus-jump from Jump alone.
  No Lara exclusion hides this difference.

## Integration checkpoint before shared ground-stop recovery

| Route | Lara equal (raw fields) | Camera equal within pinned coverage | Active / world / inventory equal |
|---|---:|---:|---:|
| `pool-wade` | 706/840, prefix 620 | 569/569 | 840/840 each |
| `pool-swim` | 659/659 | 659/659 | 659/659 each |

At `pool-wade` tick 620, both origins and heights match, but the port
uses a fresh Stand (11/185); retail resumes the starting 11 and advances
to 103/1608. This is the same starting-stance recovery rule as the
climbing slice's Run and sidestep paths. The checkpoint does not copy
that rule before integration. The full per-field observations are
`camera-cut/pool-wade.json` and `camera-cut/pool-swim.json`.

The 28 non-pool routes are byte-identical to the preceding arms
producer's body records and preserve its scope (`dry.log`). The
directed movement test fails on the arms baseline at `pool-wade` 568;
the cue test fails there on high-bit cue admission. The new cue dispatch
passes, including equal entire traces for the unchanged cue variants
and named refusal before tick 0 for unsupported ids.
Section-GC controls linked against `game/lara.c` and `game/tables.c`
also pass under UBSan (`motion-control.log`): zero-air/damage ordering,
air restoration by medium, signed surface travel, and the two-stage
underwater projection.

## Frozen candidate after climbing integration

The merged wade controller uses climbing's shared starting-stance
recovery after the frontal probe and collision shift; sidestep already
uses it. No second recovery implementation is introduced. The last
matching Lara tick of `pool-wade` moved from 619 to 774. The accepted
camera gate stops before the missing wet sidestep view as agreed.

| Route | All 51 recorded Lara fields | Camera (covered ticks) | Active / world / inventory | Gameplay RNG (gated prefix) |
|---|---:|---:|---:|---:|
| `pool-wade` | 775/840, first 775 (`Q-T2W-06`) | 569/569 | 840/840 each | 397/397 |
| `pool-swim` | 659/659 | 659/659 | 659/659 each | 430/430 |

`review/` contains exact raw traces, per-field counts, first mismatches,
and pinned comparator results. `pool-wade`'s later inert wade-exit
counter is downstream of the incorrect Jump outcome, not a retail
wade-exit sample. The `pool-swim` mode changes at ticks 464 (Wade),
584 (Surface), and 609 (Under); every recorded field remains equal
through underwater tick 658. A separately rehashed camera footer with
the wrong endpoint, reason or no cutoff is refused (`coverage-control.log`).
`directed-trace.log`, `directed-cue.log` and `motion-control.log` record
the transition, dry and wet cue filter, air and movement controls.

The 28 inherited non-pool routes (9,199 records) and their coverage
endpoints are identical to the merged climbing producer
(`final-dry.log`). Provenance and the existing comparator controls passed
(`provcheck.log`, `comparator.log`). All 25 TR1 solo fixtures have
byte-identical traces in each of schemas 2–5 (`solo.log`). The shipping
profile versus the same independently built main has byte-identical
437,744 B `.text` and defined symbols (`shipping-identity.log`).
The 16,288/16,384 B hot window and 601,880/622,592 B resident image
passed (`build-final.log`). Host growth against merged main is 6,784 B
`.text`, 384 B `.rodata` and 24 B of Great Wall state
(175,212/196,608 B); `.data` and `.bss` are unchanged. No emulator or
TR2 cart was run.

Uncommitted review snapshot `water-v1.patch` (runtime, producer and
directed tests) applies to `643478afccc912db1273a556ebfdf220c31acd2f`.
SHA-256 `14f3fc938382f6a6f9352e2a6e4b4ef9f9f00aeab7b2d3e33148ba2f05e7cd49`.

## Q-T2W-06: wading Stand input priority

[Standing inputs in shallow water](specs/wall/tr2-water-and-flares.md#standing-inputs-in-shallow-water-q-t2w-06)
establishes the priority after Look consumes directions. Forward requests
Wade ahead of Jump and Back; Walk selects the slower turn cap and no added
lean, not a different gait. Back without Forward requests the backward
walk ahead of Jump. An otherwise unmodified Jump still starts compression,
and Roll does not start a roll while wading. Only Forward plus Jump is
exercised in the two published pool records; the other cases follow the
static rule.

After the delta, all 51 recorded Lara fields match for **840/840**
`pool-wade` and **659/659** `pool-swim` rows. The camera, RNG and world
comparison scopes are unchanged. Tick 775's directed control fails
against the frozen v1 host and passes after this change
(`q-t2w-06/directed-red.log`, `q-t2w-06/directed-green.log`);
`q-t2w-06/replay/` keeps the complete comparisons. The 25 TR1 solo
fixtures still match in each schema 2–5, and shipping `.text` and
defined symbols are byte-identical (`q-t2w-06/solo.log` and
`q-t2w-06/shipping-identity.log`).

Historical separate delta `q-t2w-06/water-wade-priority.patch`, SHA-256
`a37c40c38faab13a30d9244ecdd56e3e00ca7df13bd0b5c3d4f007013e7b35e3`,
applies after v1 on `a4a06a93bd73c42b8b28ab94f66ff434906b1e69`;
it is folded into the v2 review input.

## v2 review repairs

* An unsupported movement path stops the run at the first reached
  operation, before it can emit a coverage footer. This covers wade
  exits, underwater steering or surfacing, both missing wade poses,
  and executed current sinks. The existing simulation-based camera
  cutoff stays unchanged. Presentation bubbles remain inert and do
  not end a gameplay trace.
* When the WATER profile binds its native room tables, admission
  validates the entire X/Z tile of every above/below link against the
  target direct grid, independent of actor height and room wetness.
  The water lookup starts from a horizontally clamped grid selection;
  a link on a clamped border with an unbounded address band is not
  admitted. Direct-only corner cells are tested as normal finite tiles.
  The 3,510 links in The Great Wall and all links in the 21 other
  locally installed uppercase `.TR2` files pass the finite-grid and
  clamping census (`v2/link-census.log`); this does not claim full
  gameplay admission of those levels. The pre-water host baseline
  does not run this new validation.
  Independent review also exercised the native validator on the cooked
  Venice and Bartoli's Hideout tables: both passed. Those were reduced
  Core-plus-texture host inputs, not complete stock bundles
  (`review-v2/retail-admission.tsv`).
* Effect 0 reverses both yaw and pitch regardless of Lara's clip,
  as specified by the [defined effect numbers](specs/wall/tr2-water-and-flares.md#defined-effect-numbers).
  The new control injects the cue into an ordinary nonzero-pitch dive,
  compares every preceding record, and checks both angle changes.

The new red/green controls (`v2/review-red.log` and `v2/review-green.log`)
exercise the reached steering, surfacing, wade exit, and sink branches
through the production host, plus the two Wade pose branches using
the same linked collision-probe wrapper against each revision. Both
disjoint links are accepted by the historical mode and refused before
tick zero by WATER; the effect case verifies a nonzero pitch reversal.
The contact control links `test/game/tr2-water-contact.c` with the
normal host objects using the host's `-m32` target and
`-Wl,--wrap=ItemObj_collideRoom`, then passes that executable as
`--contact-host` to `test/game/tr2-water-review-test.py`.

The folded v2 matches all 51 Lara fields on all 840 `pool-wade` and
659 `pool-swim` rows. Camera coverage remains 569/569 and 659/659
respectively; both post-tiger RNG differences remain visible and
ungated. The 28 inherited dry routes (9,199 records) are byte-identical
to the climbing baseline. Independent instrumentation observed effect 0
executing on exactly two dry routes, `roll` and `grab-attempt`, both at
tick 150 (clip 147/frame 3903, pitch 0 and yaw 0 to −32768;
`review-v2/authored-effects.log`).
The 25 TR1 solo fixtures remain identical in each schema 2–5. The
shipping cart keeps identical `.text` (437,744 B), defined symbols,
and hot/resident sizes (16,288 and 601,880 B); the host has 7,616 B
more `.text`, 736 B more `.rodata`, and 4 B more Great Wall state
than the climbing baseline (175,192/196,608 B). See `v2/replay/`,
`v2/dry.log`, `v2/solo.log`, `v2/shipping-identity.log` and `v2/build.log`.

The frozen v2 review patch applies to `a4a06a93bd73c42b8b28ab94f66ff434906b1e69`.
SHA-256 `95dc04561b8c8648502e339412ac69393b318bf8763d29a2440d5ebb44961d11`.
