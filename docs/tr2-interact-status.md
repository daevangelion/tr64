# M10: The Great Wall pickups, wall lever and ordinary doors

Original measured base: `45ea38e`; v2 includes the reviewed Q-INT
answers, Combat camera, and the merged climbing and water follow-ups. Pack:
`$TR64_WORK/evidence/m10/wall/wall.pack`,
SHA-256 `d5e1cb90646be0bdb94545e369eca0c118a5684a68cbcc9e2c454de73a59a02e`.
Evidence is under `$TR64_WORK/evidence/m10/interact/`. The fixtures
are read in place under `$TR64_WORK/fixtures/oracle-wall/tr2/`. Tick
counts include tick zero. Retail comparisons are host-only.

## Baseline on main

| Family | `pickup` (151 rows) | `switch-door` (286 rows) |
|---|---|---|
| Lara | Tick 1, `pos[1]`: 384 retail, 408 host; the pickup had no collision callback | Tick 1, `anim`: 63 retail, 103 host; the lever had no interaction callback |
| Active items | Known fields equal | Tick 1, length 3 retail, 2 host; lever 4 missing |
| Inventory | Equal | Equal |
| World | Equal | Tick 48, sector digest 3166746734 retail, 2461810071 host; unopened door 3 |
| Statistics | Tick 1, `distance`: 13301 retail, 0 host | Tick 1, `distance`: 11592 retail, 0 host |
| Camera | Tick 1, eye Y 498 retail, 504 host; follows the wrong pose and height | Tick 2, eye X 55129 retail, 55193 host; no use-state view request |
| RNG | Gameplay equal; presentation tick 2, 2506421620 retail, 3547461959 host | Same |

`before/summary.json` and `before/*.compare.json` keep the complete
observations and pinned `tr64-tr2-wet-camera` profile. The two target
routes were fully emitted at base; these were real differences, not
availability cuts. The raw presentation difference remains visible. As
in earlier host slices, only gameplay `rng[0]` gates logic; the host
logic harness does not present the draws that drive `rng[1]`.

## Rules and coverage

The interaction rule uses bit 35, after Combat camera at 34. The
independent second-game statistics rule uses bit 42. The
`tr64-tr2-interact-v2` inherits the Combat camera and movement profiles
and re-enables both recorded ordinary-door touch fields.

- [Reach and commitment](specs/wall/tr2-triggers-and-interaction.md#reach-and-commitment):
  dry/wading pickup bounds include +256 in Z; underwater uses the pitched
  frame and incremental alignment. Only a reachable pickup commits at +42
  in clip 135 or +18 in clip 130, independently of continued Action. Remove
  its room-list entry without losing its placed identity. Pickup owners
  are acknowledged once, after actual removal. No TR1 pickup tally.
- [The plain lever and settlement](specs/wall/tr2-triggers-and-interaction.md#levers-and-button-switches):
  closed reach bounds and no translation; use clip 63 or 64 according to
  the lever's current state. Advance the lever once immediately; supply
  its own mask on its first ordinary active tick. The authored deactivate
  command lets the floor owner acknowledge settlement, not the sound cue.
  The timer and automatic return are independent of the camera timer.
- [Floor evaluation and mask ordering](specs/wall/tr2-triggers-and-interaction.md#floor-evaluation):
  renew the camera before checking the owner. Check the owner once and
  execute the actions in list order. A switch toggles the requested mask
  and is exempt from the same-camera re-arm bar, except timed release.
  The preset camera's own flags/timer are separate from the item action.
- [Sole carried key](specs/wall/tr2-triggers-and-interaction.md#keys-and-puzzle-sockets):
  the host receives a native confirmation and its pause duration as
  fixture input. It does not grant the key, assume an in-reach socket,
  advance simulation while paused, or add a record. The matching key
  is consumed when clip 131 starts; the floor action consumes the
  eligible socket only after hands-free. Wrong, spent and multi-key
  selection use refuse by name; puzzle sockets remain unsupported.
- [Ordinary doors](specs/wall/tr2-lara-ground-and-weapons.md#ordinary-doors-and-timed-closing):
  existing topology runs before the animation step. On `switch-door`, wake
  at 46, enter open at 47, release the sector at 48. The second press at
  126 settles at 172, enters closed at 173 and shuts the sector at 174.
  A synthetic timed switch exercises last-active timer tick, closing and
  automatic return; the authored lever is untimed and is not evidence of
  the timing rule on its own.
- [Animated leaf contact and Walk](specs/wall/tr2-lara-ground-and-weapons.md#contact-with-an-ordinary-door-leaf-q-int-03):
  use nearest packed bounds and spheres, not the closed sector, for touch.
  Expand the rectangle by Lara's radius and choose the nearest face
  with the specified tie order. Terrain can reject the displacement;
  a moving leaf with opposed current/desired states can still bump.
  Old touch clears on active animation advance, and Lara's directional
  bump frame caps at 34 without a timer or health loss. A frontal Walk
  stop uses its advanced absolute frame and preserves Stand during the
  authored stop pose.
- [Great Wall boulders](specs/wall/tr2-great-wall-objects.md#great-wall-boulders):
  `door-reverse-contact` wakes both, so v2 binds rolling motion, authored
  animation, floor following and heavy evaluation. Contact and terminal
  stop/reset refuse. The [shake emitter and listener](specs/wall/tr2-great-wall-objects.md#great-wall-boulders)
  use the previous camera's projected sound origin. The last settling
  boulder within range supplies the bounce; the [eye update](specs/wall/tr2-camera.md#bounce-and-shake-q-t2c-05)
  applies it after clearance and consumes three gameplay draws if negative.
- [Intact tile support](specs/wall/tr2-great-wall-objects.md#hinged-trapdoor-and-collapsing-tiles):
  a linked floor entry names the covered cell for both planes. An intact
  tile provides its top at or above the slab and its underside below it.
  This does not run the collapse controller; activation still refuses.

### Authored pickup census

`authored-placements.txt` was generated using the licensed `WALL.TR2` and
`tools/cook/src_tr2_tr3.py`, not the retail trace.

| Slots | Types | Benefit |
|---|---|---|
| 29 | 143, shotgun ammunition | 12 pellet units with the starting shotgun |
| 63 | 144, automatic-pistol ammunition | One bundle if the weapon is not owned |
| 92, 28 | 149, 150, small and large medipacks | One corresponding medikit |
| 103 | 151, flare box | Six carried flares |
| 105, 77, 21 | 190, 191, 192, gold/jade/silver dragons | Secret bits 1, 2, 4, respectively; no dragon ring entry |
| 17, 15 | 193, 194, Guardhouse and Rusty keys | Key ring entries 197 and 198; insertion into a socket is separate |

A dragon requests track 47. The third distinct dragon grants the level's
launcher, two grenade bundles and small medipack according to
[Loadout, dragons and reward](specs/wall/tr2-great-wall-level.md#loadout-dragons-and-reward).
The [ring insertion rule](specs/wall/tr2-triggers-and-interaction.md#pickups)
uses display priorities, not public types; insertion is stable at equal
priority and both remembered selection ordinals are left alone.
The published record picks up only Silver: tick 43 sets bit 4 without
changing the ring, Stand resumes at 67 and Action at 111 does not repeat.
The Gold dragon's distinct pickup-owned floor action can request a wake;
Tyrannosaurus AI is not part of this slice.

### Named deferred paths and needed records

| Refused interaction | Exercising retail record needed |
|---|---|
| Puzzle socket / key selection outside the sole matching key | A routed puzzle commit and a wrong, spent or several-key menu sequence with selection/refusal cadence |
| Hinged trapdoor | Enter the overhead support tile, activate it from the linked floor and see the surface withdraw in the authored open pose |
| Collapsing tile | Cross before contact, stand exactly on the supported tile, then observe shake, drop, rest and revisit |
| Push block | Valid hold, rejected push and pull, move one full tile, then see the origin restored, destination raised and heavy trigger applied |
| Flying disc/launcher | Cross both flooded bank trigger rows, observe launch and a disc's flight or impact |
| Rolling blade / guillotine | A full rolling/turn cycle and a separate swept-blade contact with the registered mesh |
| Stationary spikes / spiked wall | Safe low speed, running and descending contact; wall touch, terminal stop and post-stop contact |
| Rolling boulder contact, terminal stop or reset | A collision from floor and air, a stop at a higher or missing floor, and a heavy action or trigger release at the stop |
| Zip line | Mount, travel, release and run into an obstruction; capture both release and line-stop branches |
| Loose-flare retrieval | Land/submerged reacquisition after release with carried age and no change in stock |
| Other switch variants | A Venice lever/button/submerged switch and a one-use Offshore Rig button; none is authored in `WALL.TR2` |

The inert placement census is informational. Reaching a listed use,
body contact, activation or changing support query refuses by name,
item, tick and count, without a footer. Admission also refuses an
orphan wall switch or an incompatible pickup owner. The resting block's
existing floor and box topology remains live; no grasp is simulated.

## Numbered questions

1. **Q-INT-01, distance: answered.** The
   [statistics rule](specs/wall/tr2-oracle-trace.md#live-statistics-q-int-01)
   seeds the previous sample at the authored spawn before an optional
   placement. Each completed Lara update adds net displacement from
   that sample after contact, arms and floor actions, using a 32-bit
   square sum and unsigned integer square root. This intentionally
   corrects prior distances outside the two target routes.
2. **Q-INT-02, ring ordering: answered.** Static display priorities,
   existing-entry increments and unchanged selections are in the
   [pickup rule](specs/wall/tr2-triggers-and-interaction.md#pickups).
   The order and different quantities are exercised by synthetic packs;
   the published Silver Dragon is not a positive insertion record.
3. **Q-INT-03, physical door contact: answered.**
   [Leaf bounds, touch, push and bump](specs/wall/tr2-lara-ground-and-weapons.md#contact-with-an-ordinary-door-leaf-q-int-03)
   are bound to the ordinary leaves. `switch-door` still proves only
   topology; the three door fixtures exercise physical contact.
4. **Q-INT-04, presentation RNG:** outside host logic by Main's direction;
   still visible, not excluded by profile and not used as a gate.
5. **Q-INT-05, boulders:** live motion is established and exercised in
   the untouched `door-reverse-contact` route. Lara contact and stop/reset
   are separate unverified branches and refuse by name.
6. **Q-INT-06, boulder shake emission: answered.** At `door-reverse-contact`
   tick 2 item 74 requests −5, then item 73 overwrites with −4. The
   sampled eye Y is 1190 and bounce is 1 after decay.
7. **Q-INT-07, intact tile reference: answered.** `door-opening` tick 27
   and `fixed-camera-repeat` tick 70 reach the same linked slab via different
   probe points. The slab raises the queried floor from 4096 to 3328
   without blocking the fixed-camera ray or arming the collapse.

## Great Wall play-cart integration

The host and TR2 play cart use the same rules. The placement scan selects
`tr2_wall` for an ordinary door, wall switch, boulder, campfire, or
helicopter. Their active ticks go through `code_call` and the shared
world-item entry. Boulder listener projection still happens once per
active pass, at the first boulder's turn. Contacts stay in one resident
room-item scan so pickups, locks, door pushes and paged creature contact
keep their relative order. Reach, the carried ring, trigger settlement,
geometry, admission and named interaction stops remain resident; none
goes through the TR1 object-ID dispatcher. Q-INT-08's room-set timing
remains labelled in the contact scan.

### Unit dispatch boundary

The boulder controller returns its Core floor-word index after motion,
support and shake. Resident code evaluates that word before the same
boulder's ahead-floor check and before the next active item. A heavy
action can therefore wake a creature without nesting unit dispatches.
`run_boulder_heavy_wake` replaces only sector 4349's program: the old
link refuses at the tick-2 dispatch; the corrected link puts the
dormant tiger at the head of the active list on tick 2.

The other paged paths do not run a floor program:

- **Door:** `game_door_action` only updates sector and box topology;
  it does not run the restored floor programs or activate items. The
  door clip bank admits only no-op and sound commands.
- **Switch:** its active tick changes mask, timer and goal, then
  advances the clip. The only extra admitted command sets inactive
  status. Lara's initial activation and settled trigger processing
  are both resident, not part of the unit tick.
- **Helicopter:** its tick changes pose, membership and retirement;
  admitted clips use no-op and sound, and room resolution is a query.
- **Campfire:** its effect allocates or returns a slot in the resident
  effect pool. It neither activates an item nor dispatches a controller.

`soundPlay` only counts cues under the TR2 rules. The boulder's supporting
floor query (like creature stride/contact) reads terrain and may refuse
an unsupported surface, but it does not execute action lists. Creature
wake and update call the same unit's functions directly; strike and
spider-breakup effects only allocate resident slots and update or remove
items without calling another unit.

### Link and platform limits

The cart does not have an inventory ring or a key-confirmation event.
At an in-reach unused lock it stops with `key-ring confirmation
required`. The host's explicit confirmation still drives the successful
Rusty Key path; route v4 and the interactive START limitations card are
unchanged. No cart gameplay was run for this integration.

The TR2 resident image is 477,512 B, up from 461,128 B on `88a0931`, out
of 622,592 B. Before final page padding, `tr2_wall` grows from 704 to
1,312 B and `tr2_creatures` shrinks from 12,640 to 12,480 B. They still
take one and four pages respectively; no other unit gains a page.
The 16,288 B hot window and the five dispatch references (zero
allow-listed) are unchanged. See `cart-r2-build.log`,
`cart-r2-layout.tsv` and `cart-r2-unit-spans.log`.

## Source layout and crow integration

The merge of `deb4f40` keeps `game/tr2_interact.c` resident and unsplit.
The item selector, activation and boulder's resident continuation now
live in `game/tr2/object_dispatch.c`; placement and its inert census
live in `game/tr2/object_factory.c`. `game.c` still owns active order and
the first-boulder listener sample. Per-item creature contact stays
paged in `game/tr2/enemy_motion.c`, including crows. Q-INT-08 now cites
the pass-start room set and authored portal order. The already-reviewed
room deduplication is unchanged in this merge; duplicate authored
portals are outside its contact parity claim.

All 48 accepted traces from `88b7b00` remain byte-identical. The two crow
routes seal; their data rows match `deb4f40` except for the newly live
`save.distance` from tick 1. A changed producer and the resulting
hashes are expected. The following `deb4f40` seals are not parity:
it has inert controllers where interactions refuses. The merged
build preserves the earlier named refusals and their exact stored
prefixes for these routes:

| Routes | Boundary |
|---|---|
| `blade-hit`, `blade-pass` | Guillotine activation at 1 |
| `block-cycle`, `block-refused` | Push block grasp at 1 |
| `boulder-hit` | Rolling boulder body contact at 84 |
| `collapse-air`, `collapse-stand`, `collapse-cross` | Tile activation at 1, 1, 14 |
| `spike-jump`, `spike-walk` | Spike contact at 1, 13 |
| `trapdoor-fall` | Closed trapdoor surface at 1 |
| `zip-release`, `zip-stop` | Zip line mount at 1 |
| `schema21/rex-gold` | Tyrannosaurus wake after the now-live dragon pickup |

The merged cart contains the profiler and crow changes and links without
an emulator run. The resident image is 493,896 / 622,592 B; `tr2_wall`
and `tr2_creatures` are 1,504 and 14,528 B before padding, still one
and four pages. The 16,288 B hot window and five dispatch references
(no allow-listed relocations) pass. See `layout-build.log`,
`layout-unit-spans.log`, `layout-replay.json` and `layout-controls.log`.

## Frozen v4 for independent review

Patch: `$TR64_WORK/evidence/m10/interact/interact-v4.patch`.
The exact base (`1798ddc`) and digest are in `interact-v4.base` and
`interact-v4.sha256`. This status is included.

`walkPackedSpheres` is the same traversal in admission and contact.
Admission passes no matrix or sphere outputs: it decodes packed widths
using the same decoder, follows the bone `pop` then `push` operations,
and reserves the last stack slot for the sphere transform. The contact
pass uses its own fixed-size stack so depth does not depend on another
caller's matrix state. Unused trailing words in a key are padding; the
walk requires exactly one decodable rotation for each traversed mesh.
Both passes restore the caller's matrix state or leave it untouched.

| Remaining runtime error or dependency | Why admitted TR2 contact cannot reach it; control |
|---|---|
| `game sphere count ...` and `game sphere bone stack underflow` in `game/hit.c`, and legacy `matrixPush_c` overflow | The packed path returns before the legacy code; it neither reads legacy rotations nor calls `matrixPush_c`. The packed checking mode rejects count outside 1–32, pop at the root, and a push that would consume the sphere temporary. The underflow and overflow controls fail on v3; a five-push skeleton with pop-then-push reaches a real door contact on both hosts. |
| `admitted sphere pose changed` in `game/hit.c` | Immutable model and key data have already completed the same traversal. At load, both authored transition kinds are followed for every ticking door and boulder. All physical keys of each reachable clip, including a fractional tail's far key, are checked; non-advancing hazards only evaluate the validated placed key. Lara's directly selectable body clips and their links are also checked. Root, late linked key, insufficient rotation for mesh count, out-of-clip transition and missing far-key controls all refuse before tick zero. |
| Frame, mesh/sphere or node reference outside its table | Core admission checks the meshes, node span and one-to-one sphere table; the shared walk's checking mode also checks the bound model spans. The animation admission walk validates the contact links and key storage before taking their pointers. Separate controls cover the mesh and node indices, missing sphere entry, malformed link and overrun of the final physical key. |

All 25 directed controls pass. The 46 v3 route bodies and comparison
outcomes are byte-identical, including all six interaction targets.
Five newly published hazard routes remain unsealed by name: `blade-hit`
and `blade-pass` refuse guillotine activation at tick 1, `boulder-hit`
refuses Lara body contact at 84, and `spike-jump` and `spike-walk`
refuse spike contact at 1 and 13. All 25 solo TR1 fixtures remain
byte-identical in schemas 2–5, as does shipping `.text` (437,744 B).

See `v4-red-final.log`, `v4-green-final.log`, `v4-route-identity.log`,
`v4-solo.log`, `v4-shipping.log` and `v4-provcheck.log`. Creature contact
with rolling boulders and alternate listener modes remain unverified;
no push block, collapse or trapdoor controller was added in v4.

## Historical v3 (rejected before commit)

Patch: `$TR64_WORK/evidence/m10/interact/interact-v3.patch`.
Base: `fb34bba`; its full ID and the patch digest are in
`interact-v3.base` and `interact-v3.sha256`. This document is included.

V3 fixes both v2 review findings. Door pose admission walks each placed
door type's reachable clip graph, checks transition destinations, and
checks one packed rotation per mesh in every stored key the contact
sampler can read, including a fractional tail's far key. Boulder contact
uses the same graph check; a deferred, non-advancing sphere collider's
placed key is checked once. The root-stride, linked late-key, and
extra-mesh controls refuse before tick zero. Ordinary Lara's own pose
admission still checks her whole clip bank.

Only an object action contributes a dynamic slab. The controller word
and camera extension never supply geometry, even when their low bits
name a tile. The controls also accept a real slab action after a camera
and its extension and reject support from an action naming another item.
The two tests fail on the frozen v2 executable; see `v3-red.log` and
`v3-green.log`.

All 22 directed controls pass. The 44 v2 route bodies, comparison
outcomes and refusal points are unchanged (`v3-route-identity.log`).
Two newer block fixtures refuse the unsupported grasp at tick 1 and
remain unsealed. All 25 TR1 solo fixtures match in schemas 2–5, and the
shipping `.text` matches byte for byte (437,744 B). See `v3-solo.log`,
`v3-shipping.log` and `v3-provcheck.log`.

Unverified: creature contact with a boulder and shake in listener modes
other than the ordinary view of the published door route (including
sound-at-Lara or a different field of view). No emulator or TR2 cart.

## Historical v2 (rejected before commit)

Patch: `$TR64_WORK/evidence/m10/interact/interact-v2.patch`, based on
`b6f8bc5f1a6ecc0146818b368366482a0686a74b`. Its SHA-256 is recorded in `interact-v2.sha256`.
This status document is included in the patch.

| Fixture | Matching records in every gated family |
|---|---:|
| `pickup` | 151/151 |
| `switch-door` | 286/286 |
| `door-closed` | 81/81 |
| `door-opening` | 81/81 |
| `door-reverse-contact` | 81/81 |
| `rusty-key` | 766/766 |

The three door records restore active-door hit-mask comparison; there
are no new exclusions or coverage cutoffs. Both boulders match all
sampled active fields; the route does not exercise player damage, the
terminal stop or reset. In `rusty-key`, the optional confirmation
adds 50 saved clock ticks without an extra gameplay row: key consumption
at 610, door wake at 745, open pose at 746 and sector release at 747.
No menu presentation or draw RNG parity is claimed.

The three v1 review findings are fixed: a native touching or attempted
unsupported operation cannot seal; an orphan switch and unsupported pickup
owner fail admission. Positive and negative control logs, current route
replay and before/after ledger are `v2-freeze-controls.log`,
`v2-red-final.log`, `v2-red-q06-q07.log`, `v2-final/`, and
`v2-route-delta.tsv`. The 34 non-target routes preserve their merged-main
outputs except for the now-correct `save.distance`. Five still have the
inherited post-creature gameplay RNG differences. `fixed-camera-repeat`
is fully sealed again: 359/359, including the live tile probe.
The four newly published collapse/trapdoor routes remain unsealed by
name: `collapse-air` and `collapse-stand` at tick 1, `collapse-cross`
at 14, and `trapdoor-fall` at 1. They are not part of the 40-route
same-input baseline regression ledger.

TR1 shipping `.text` is identical (437,744 B); see
`v2-final-shipping.log`. All 25 TR1 solo traces are byte-identical in
schemas 2–5 (`v2-freeze-solo.log`). No emulator or TR2 cartridge run.

## Historical v1 (rejected before commit)

`$TR64_WORK/evidence/m10/interact/interact-v1.patch` applies at
`03399d9448ebf64c69538bb42730a606c0c28c56`.
SHA-256 `47031bec65308de01f4dd6d0f0d426f2e4a58ace26e6eb787277a61cc1e9e7d3`.
It contains the runtime, reviewed profile inheritance and directed
controls. This status document is outside the patch hash.

| Route | Lara, active, inventory, world, save, camera, `rng[0]` | Coverage |
|---|---:|---|
| `pickup` | 151/151 per family | Full; inherited field exclusions only |
| `switch-door` | 286/286 per family | Full; inherited field exclusions only |
| `door-closed` | No sealed trace | Refuses leaf 3 at attempted tick 14 |
| `door-opening` | No sealed trace | Refuses leaf 72 at attempted tick 1 |
| `door-reverse-contact` | No sealed trace | Refuses leaf 72 at attempted tick 5 |

`after-q01-q02/*.compare.json` and `*.log` contain the full reports.
The three door records were published after the 36-route baseline. They
are not proof of contact. The remaining 34 original routes preserve
every emitted field except the intentional `save.distance` correction
and resulting row hash; exit status and emitted row counts are identical.
Thirteen of the original 36 routes change distance on 3,191 emitted
rows total. All 30 sealed original routes match the entire raw save
block. Among the six inherited partial routes, five save blocks match
on every emitted row; `ledge-edge` matches its first 53 of 100 emitted
rows and then retains downstream distance divergence from the unmerged
Lara movement rule (Lara first differs at tick 51). This slice does
not claim beyond the existing six movement refusals. Details:
`before-combat/summary.json`, `route-delta-q01-q02.tsv`.

Ten production-host controls pass (`controls-q01-q02-green.log`) and the
same ten fail on the pinned Combat host (`controls-q01-q02-red.log`).
They include reach limits, occupied hands, losing reach before commit,
once-only pickup owner execution, dry and submerged pickup, stock and
ordering, secret reward, saved distance, manual/timed lever, one-shot
mask, sector and navigation-box timing, and the named unsealed contact
refusal. Synthetic packs are controls, not retail evidence.

TR1 `.text` is byte-identical to the pinned shipping baseline (437,744 B,
SHA-256 `0aacd23cc8607acc6f2dff60c6509a7b071aa4ec41e5fd5cebd59b074e28eeaa`);
the hot window is 16,288/16,384 B and resident image 601,880/622,592 B.
See `shipping-q01-q02-identity.log` and `build-q01-q02.log`.
All 25 TR1 solo fixtures are byte-identical in each of schemas 2–5
(`solo-q01-q02.log`). No TR1 oracle change.

Reproduce the host and shipping checks from the interaction worktree:

```sh
export TMPDIR=$TR64_WORK/tmp/T2Interact PYTHONDONTWRITEBYTECODE=1
B=$TR64_WORK/builds/T2Interact
E=$TR64_WORK/evidence/m10/interact
W=$TR64_WORK/evidence/m10/wall/wall.pack
F=$TR64_WORK/fixtures/oracle-wall/tr2
P=$TR64_WORK/builds/Video3/pack/pc-campaign21-video.pack
rtk proxy make -j2 BUILD_DIR="$B" TR64_PLAY=1 TR64_TEST=0 TR64_LEVEL_PACK="$P" rom host
PYTHONPATH=tools/cook rtk python3 test/game/tr2-interact-test.py "$B/host/tr64-host" "$W" "$F"
rtk python3 "$E/replay.py" "$E/recheck" "$B/host/tr64-host" tr64-tr2-interact-v2
rtk python3 "$E/route-delta.py" "$E/before-moves2" "$E/recheck"
```
