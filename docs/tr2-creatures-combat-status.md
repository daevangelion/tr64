# Tomb Raider II: Great Wall creatures and pistol combat

Host only. Rules come from the published wall specifications, authored `WALL.TR2` data, and the permitted OpenLara comparison. No input-tape path exists in the normal host. `--game-tr2` and `--game-tr2-schema21` select serialization only. The historical producer remains in the comparison registry for old archives.

## Rules and scope

| Engine area | Source and implementation |
|---|---|
| Admission, placement and retirement | `tr2-great-wall-creatures.md`, Admission outcomes and The animals; `tr2-triggers-and-interaction.md`, Item masks: first free slot, forced admission for visible dormant actors, ordinary admission for hidden ones, and camera-distance eviction. Missing wake boxes and unbound species refuse by name; keyed creature banks reject Lara-only commands at load. `game/game.c`, `game/tr2_creatures.c`, `game/animation.c` (`CREATURES`). |
| Navigation | `tr2-great-wall-creatures.md`, Route, waypoint and random choices, Mood transitions: five live slots, four ground-zone lanes per map and one flight lane, overlap-order expansion and retained tail, ordered draws, inset corridor and temporary waypoints. `game/tr2_creature_nav.c`, `game/tr2_creatures.c`. |
| Motion and contact | `tr2-great-wall-creatures.md`, Collision after the creature turns (Q-T2K-09): old-to-new pose-top probe, asymmetric tile snap, zone-independent neighbouring radius probes, linked floor and room, nearest-key spheres with II's matrix arithmetic and local tiger joint 22, inclusive horizontal faces, zero-distance bump, and a mask for the current contact pass. `game/tr2_creature_motion.c`, `game/pose.c`, `game/collision.c`. |
| Tiger and spider | `tr2-great-wall-creatures.md`, Tiger, Tiger contact/flight/death, Small and giant spiders, Ordinary death boundary: judged contact and once-per-attack latches; tiger death keeps route and goal and retires while the corpse remains targetable; Lara's death counter. `game/tr2_creatures.c`, `game/game.c`. |
| Item links | `tr2-great-wall-creatures.md`, End links and wanted states (Q-T2K-07): ordinary same-state end links keep the goal, changed-state end links set it, and the authored state window is distinct. `game/item.c` (`ITEM_END_GOAL`). |
| Room effects | `tr2-great-wall-creatures.md`, Shared effect capacity, Shard initialization, Fragment lifetime, Splash and shot occupancy (Q-T2K-06); `tr2-items-general.md`, Emitter versus flame: 100 shared slots, free-list allocation without eviction, campfire flame ownership, blood, visible spider meshes with three draws on each successful allocation, shard motion and splash retirement. `game/tr2_effects.c`, `game/game.c`, `game/pose.c`, `game/tr2_arms.c` (`ROOM_EFFECTS`). |
| Pistols | `tr2-lara-arms.md`, Pistol attempts and Pistol lock and creature strikes (Q-T2K-02, 03, 05, 10): five-slot selection, aim and retention, nearest-key mesh spheres, right hand first, pitch then yaw draws, hit statistics, retained-corpse selection and 8192-unit live terrain sweep. A blocked miss allocates a four-update ricochet and draws once from presentation RNG only if allocation succeeds. Types 72/88 are absent from The Great Wall and refuse at admission; this does not implement their scenery-hit precedence in other levels. `game/tr2_arms.c`, `game/pose.c`, `game/camera.c`, `game/tr2_effects.c` (`COMBAT`). |
| Wire and tooling | `tr2-oracle-trace.md`, Schema 21: canonical optional brain for a live navigation slot, signed coordinates and angles, and cassette validation/projection. `game/trace.c`, `tools/tr2-compare.py`, producer profiles and host-only tests. |

Fatal Tyrannosaurus lunges refuse by name before entering the unbound special Lara pose. The giant spider type 37 is not placed here and also refuses at wake. Carried corpse loot, effect rendering, flame damage and attachment, unrelated water effects, scenery chipping, timed hazard reactions and the death-menu frontend are outside this slice. The trace harness refuses before advancing into the death menu. Save distance remains the interaction owner's work and is compared, not excluded.

## Published wall questions

- **Q-T2K-06, 07, 09:** answered on `3c32cc4`; implemented in this candidate. No constant RNG skip is used.
- **Q-T2K-08:** the crow delta implements the flight and death rules published at `d72770a`. The v3 refusal is historical.
- **Q-T2K-10:** the projectile terrain miss and ricochet sweep is implemented from `tr2-lara-arms.md`. The temporary guard is gone.
- **Q-T2K-11:** `06d3088` establishes that −32512 is compared numerically as the missing floor after the ceiling decision; the room changes only after surviving retirement tests. The v3 mismatch (13 rather than 4 slots at the second breakup) is closed by the shard control below.
- **Q-T2K-12:** `9c70455` establishes one shared blood effect per landed bite, with no RNG draw, the stated per-species joint and offset, spider allocation before damage versus tiger and crow after it, and strike latch after both outcomes. These use the ordinary blood effect: 24 updates including the bite tick, retirement on bite+23. A full pool does not suppress damage or the latch.
- **Q-T2K-13:** the published rule confirms the nearest pre-advance contact transform already used by bite blood, with earliest-key tie, enabled live rotations, and the attacker's own room. The comment and implementation now cite that rule.

Evidence lives in `$TR64_WORK/evidence/m10/combat/`. The file `first-live.log` is first integration, prior to signed serialization, margin clearance and retained death goal corrections; it is not an acceptance run. The report below distinguishes closed-loop comparisons, unsealed recorded-input checks, and unresolved behaviour. `rng[0]` gates gameplay without treating the separate presentation stream as a gameplay clock; the full `rng` comparison remains visible in the logs.

## Previous checkpoint (before the three wall amendments)

Counts are equal comparable records / published records from `candidate-live.log`. This table describes the earlier `5d7ed33` checkpoint, not the frozen review candidate.

| Route | Lara | Camera | Active and brain | Gameplay RNG | First combat difference |
|---|---:|---:|---:|---:|---|
| tiger-cave | 464/464 | 464/464 | 464/464 | 464/464 | None |
| pool-wade | 840/840 | 840/840 | 837/840 | 840/840 | 581: tiger wanted state, Q-T2K-07 |
| pool-swim | 659/659 | 659/659 | 658/659 | 659/659 | 633: tiger wanted state, Q-T2K-07 |
| tiger-bite | 238/241 | 241/241 | 235/241 | 241/241 | 121: tiger goal, then 155: Lara's reaction |
| tiger-death | 959/962 | 962/962 | 644/962 | 962/962 | 121: tiger goal, then 155: Lara's reaction |
| tiger-shoot | 286/451 | 266/451 | 391/451 | 267/451 | 121: tiger goal; missed goal at 260 changes pose at 266 |
| spider-bite | 481/481 | 481/481 | 475/481 | 481/481 | 11: spider wanted state, Q-T2K-07 |
| spider-shoot | unsealed | unsealed | unsealed | unsealed | 11: spider wanted state; explicit breakup refusal on 281 |
| crow-bite | 536/641 | 536/641 | 28/641 | 111/641 | 2: perch sample, Q-T2K-08 |
| crow-shoot | 273/561 | 276/561 | 28/561 | 123/561 | 2: perch sample, Q-T2K-08 |

Before this slice the old producer did not update the tiger and diverged on gameplay RNG at 397 on `tiger-cave`/`pool-wade`, and 430 on `pool-swim` (see `tr2-camera-status.md`).

## V3 closed-loop results

`v3-live.log` compares the pinned host and pack. Counts include tick zero. The table covers Lara, camera, active items/brains, inventory, world, ammo hits, kills and gameplay RNG, not presentation RNG or the placed-start distance. None of the numerical gaps is hidden by a new profile exclusion.

| Route | Combat records equal | Boundary |
|---|---:|---|
| tiger-cave, pool-wade, pool-swim | 464/464, 840/840, 659/659 | Both schemas; brain fields in schema 21. |
| tiger-bite | 241/241 | Exact-face contact and push at 155 and 163. |
| tiger-death | 962/962 | Last gameplay tick before retry. |
| tiger-shoot | 451/451 | All 32 hits, including hits on the retained corpse. |
| spider-bite | 481/481 | All six injuries. |
| spider-shoot | 381/381 | Both breakups allocate 30 shards; pool lifetime discrepancy Q-T2K-11 above. |
| pistols (schema 20) | 116/116 | Restores the inherited sealed route. |
| crow-bite, crow-shoot | 1 sample each, unsealed | Named refusal at first activation. |

The review controls (`v2-rejection-controls.log`, `v3-rejection-controls.log`, `review-v2/wake.log`, `v3-wake.log`) distinguish v2 from v3: a no-list box does not read an overlap, bit 14 is not part of the neighbour index, and both temporary waypoint axes are 142578 rather than 11506 in the 200-tile case. A Tyrannosaurus wake and a tiger without a box cannot seal; a tiger bank with Lara command 3 cannot even write tick zero. A visible dormant entrant evicts the farthest eligible occupant; the same hidden entrant waits. The 4096 contact-scan prefilter is inherited from OpenLara `src/fixed/lara.h:369`.

## Separate checks

- `v3-tape.log`: unsealed, separately linked weapon-input controls match 300 tiger and 380 spider updates; changing the supplied origin fails on tick 247. It supplies neither damage nor RNG state.
- `v3-sparks.log`: exact linked spark positions and counts where the wall specification gives observations; a successful spark advances presentation RNG only. The linked pool saturation control (`v3-effect-control.log`) verifies zero shards/draws with zero free slots and three shards/nine draws with three free slots.
- `v3-schema21.log`: ten captures and 1,963 exact projected rows, with malformed and absent-brain controls. `v3-solo.log`: 25 first-game fixtures byte-identical in schemas 2–5. `v3-camera.log`: 16 second-game camera routes unchanged against the combat-camera host.
- `v3-cart.log` and `v3-hot.log`: 437,744 bytes of shipping `.text` byte-identical, 16,288-byte render window in a 16,384-byte I-cache and 6,864-byte hot data in an 8,192-byte D-cache. `v3-build.log` includes the 601,880 B / 622,592 B resident check. No second-game cart or emulator run.
- `v3-host-size.log`: GNU `size` reports 29,960 B more host text and unchanged data/BSS versus the accepted climb/water host. The Great Wall state pool uses 135,884 B. Navigation uses five eight-byte route vectors per box; the effect pool has 100 entries.

## Crow and shard delta

- The authored crow perch and 15 starting HP are bound in `game/game.c`. The three placement draws use the fixture pre-level seed; the ordinary initial sample resets the gameplay stream. Placement gives headings +3230, −4159, +2705 for slots 2, 12, 13 respectively. The trigger admits 13, 12, 2 into navigation slots 0, 1, 2; prepending gives active list order 2, 12, 13.
- `tr2-great-wall-creatures.md`, Crow flight, altitude, membership and death (Q-T2K-08): `game/tr2_creatures.c` binds attack and retained-waypoint death, and `game/tr2_creature_motion.c` uses the advanced interpolated pose top for terrain, signed retained vertical step for pitch, and the 512-above-origin probe for room membership. The last corpse stride runs before retirement but does not steer. An accepted pending escape request is kept after the candidate draw, as in OpenLara `src/fixed/enemy.h`, `moodEscape`.
- The merged camera already gives the first fixed eye (48127, 324, 33957); `game/camera.c` is unchanged. In `crow-shoot` at 346 both arms lose lock but slot 2 remains selected. `game/tr2_arms.c` preserves the selected target's current geometric bearing for the existing Combat rig while failing the visibility lock, per `tr2-camera.md` Q-T2C-13.
- The linked `--shards` control measures 11, 0, 0 fragments at ticks 300, 317, 350 of `spider-shoot`. At 350 the four occupants are the campfire, two blood effects and a ricochet. Both breakups still produce 30 shards and every recorded gameplay RNG state agrees. The archived combat host fails the same control with 25, 11, 9 (`crows-shard-v3-red.log`); the delta passes (`crows-review-shards.log`). Effect rendering and presentation-stream parity are not claimed.
- The normal host passes `crow-bite` 641/641 and `crow-shoot` 561/561 for Lara, camera, active actors/brains, gameplay RNG, inventory, world and all saved counters except the still-visible placed-start distance gap (`crows-review-gate.log`). The archived v3 host refuses the same gate at the first crow wake (`crows-v3-red.log`). The existing tiger, spider and pistol regressions are in `crows-final-replay.log`.
- All 25 first-game fixtures match in schemas 2–5; all 16 earlier camera routes are unchanged (`crows-final-solo.log`, `crows-final-camera.log`). Shipping `.text` bytes, size and hot layout pass (`crows-final-cart.log`, `crows-review-build.log`, `crows-final-hot.log`); provenance passes (`crows-provcheck.log`). Host text increases by 1,680 B versus combat v3 with no linked data or BSS increase (`crows-host-size.log`).
- V2 validates the fixed crow fall (+1) and perch (+14) before placing any item. Both must remain in the same checked model animation bank. The generated clipped-bank cases require a named refusal before tick zero; the same cases seal on v1. The independent review pack also refuses (`crows-v2-review-clipped.log`).
- V2 also gives a running Bored/Stalking small spider the crawl goal without taking a pose-choice draw, as specified in `tr2-great-wall-creatures.md` (Small and giant spiders, Idle and pursue). The linked unit control checks this after actual navigation and before the stride; no zip-line fixture is registered.
- `crows-v2-final-effects.log` confirms three crow, four tiger and six small-spider blood allocations on the damaging ticks, no RNG draw, retirement on bite+23 wherever sampled, and no lost HP damage or latch when a full pool refuses blood. It also confirms unchanged shard counts and, against `crows-v2-spider-red.log`, the corrected crawl goal. The archived no-blood runtime fails the same allocation test (`crows-v2-bites-red.log`).
- Relative to crow v1, the linked host `size` text category grows by 1,064 B with unchanged linked data and BSS (`crows-v2-final-size.log`). Resident shipping size remains 601,880 B of 622,592 B (`crows-v2-final-build.log`).
- All 46 pinned schema-20/21 traces, including both crows and the earlier combat routes, are byte-identical to crow v1 (`crows-v2-final-identity.log`). All 100 schema-2–5 first-game comparisons pass; cart `.text`, hot layout and provenance remain unchanged or clean (`crows-v2-final-{solo,cart,hot,provcheck}.log`).

### Fixed creature indexes and load checks

`game_tr2_validate_creature_assets` (`game/animation.c`) runs before item
placement on the host and the TR2 cart. The bank begins at the model's
authored `animIndex` and ends at the next model's first clip (or the
end of the animation table). It also checks the skeleton stack, body
command stream, every authored state link and end link, and the packed
rotation stream through each ordinary frame and the effective tail key.

| Species | Fixed model-relative clips and frame indexes | Fixed joints and admission |
|---|---|---|
| Tiger (39) | Start at the authored model base (Wall 286). Death is `TR2_TIGER_DEATH_CLIP` (+11, Wall 297). Admission requires +11 inside the checked bank. Both direct entries use the checked clip's authored first frame (Wall 0 and 250), not hardcoded frame indexes. The attacks 287–289 and other pose changes are authored links, not direct selections. | Bite blood 26 (`TR2_TIGER_BITE_JOINT`) requires 27 meshes. Shared contact and pistol spheres apply live extra yaw to joint 22 (`TR2_TIGER_HEAD_JOINT`); the same bound covers it and strike-mask meshes 14–16 and 18–26. |
| Crow (38) | `TR2_CROW_FALL_CLIP` (+1, Wall 271) and `TR2_CROW_PERCH_CLIP` (+14, Wall 284) must both be inside that model's checked bank. Their first frames (Wall 21 and 382) are authored. Grounded death (Wall 270/0) and other transitions use checked authored links. | Bite blood 14 (`TR2_CROW_BITE_JOINT`) requires 15 meshes. No fixed extra rotation or strike mesh index. |
| Small spider (36) | Start at the authored model base (Wall 261); the terrain-induced leap selects `TR2_SPIDER_LEAP_CLIP` (+2, Wall 263), checked against the same bank. Both first frames are authored. Other attack entries and frame windows are checked authored links. | Bite blood 1 (`TR2_SPIDER_BITE_JOINT`) requires two meshes. No fixed extra rotation or strike mesh index; fragments walk the validated mesh table. |

Pistol sight consumes the selected creature's validated nearest-key
bounds; the hit ray walks all of that model's validated pose meshes.
There is no weapon-specific creature clip or frame index. Lara's
separate pistol arm models are checked by `game_tr2_validate_arms` for
four consecutive clips with inclusive frame ranges 0–4, 5–12, 13–23
and 24–32, rate 1, and the Lara joint count. These ranges cover the
named controller thresholds; they are not creature animations.

The v3 red controls shorten the three models to 26, 14 and 1 mesh
respectively: v2 seals all three (`crows-v3-joints-red.log`); v3 names
each missing bite joint before emitting tick zero.

### Cart-enabled v3 link

The crow controller, navigation and stride share the existing
`tr2_creatures` unit with tiger and spider. The resident species
predicate selects that unit even for crow-only placement before any
item can wake. The host control removes the tiger and spider
placements and observes one unit load plus three live crow samples
(`crows-v3-host-gates.log`). The shared effect pool and pose queries
remain resident; there is no per-frame load or new paging reference.

| TR2 link measure | `2231e00` before | Crow v3 |
|---|---:|---:|
| Resident image (622,592 B budget) | 461,128 B | 461,128 B |
| Linked sections inside the image | 453,436 B | 455,220 B |
| `tr2_wall` paged section | 4,096 B | 4,096 B |
| `tr2_creatures` paged section | 16,384 B | 16,384 B |

The TR2 `rom` target passes paging references (five dispatch rows,
zero allow-listed), size and hot layout (16,288 B render window;
1,008 / 3,360 B room/item entry stack), in `crows-v3-tr2-link.log`
against the baseline `cart-play/v2/build.log`. The shipping TR1 link
also passes and keeps identical `.text` (`crows-v3-tr1-identity.log`).
All 100 first-game fixture/schema pairs match on the TR1-configured
host (`crows-v3-solo.log`); provenance passes (`crows-v3-provcheck.log`).
No emulator was run in this slice.

All three short-model refusals, the crow 641/641 and 561/561 gates,
the 46 pinned TR2 byte comparisons, and the effects and spider controls
pass on the merged host (`crows-v3-host-gates.log`,
`crows-v3-identity.log`, `crows-v3-effects.log`).

## Shotgun and Tyrannosaurus delta

- `tr2/lara_weapon.c` owns the long gun beside the pistols. It advances
  five admitted authored clips on an ordinary free-list item, outside
  every room and the active list. Both wire arms take the relative pose
  frame; only the left aims and only the right flashes. The empty ready
  update keeps the previous sight for Combat. Both guns share the ray
  geometry, blood, ricochet and damage path; a shell issues up to six
  pellets, with outer yaw then pitch, and an inner pair for each issued
  pellet. The setback, frame, hand/back swaps and shell limits cite
  `tr2-lara-arms.md`, Shotgun draw, firing and stow, and
  `tr2-lara-ground-and-weapons.md`, Frames for drawing, firing and holstering.
- The normal host accepts mask 2048 only with the validated menu
  manifest and calls the resident
  `game_tr2_request_ring_weapon(WEAPON_SHOTGUN)` at tick 3. The entry
  accepts `WEAPON_PISTOLS` or `WEAPON_SHOTGUN` if carried, Lara is
  alive, `extraL` is present, she is shallow or dry enough, and free
  or ready. A new shotgun pose also needs a free item slot that is
  not already live; otherwise the entry returns false and changes
  nothing. It requests the next gameplay update to draw, stowing any
  different ready gun first. Selecting the ready gun itself does not
  stow it. No changing a ring ordinal, advancing time, consuming RNG
  or granting stock is part of the entry. The replay selects the ring
  ordinal and separately
  applies +87 saved clock ticks and 91 presentation advances.
  `weaponItem` is now compared. The cart UI has not landed.
- `tr2/enemy_rex.c` is in `tr2_creatures`, selected by an authored rex
  placement. It uses the shared violent navigation and terrain rules,
  blocks the blockable-box bit, turns two live neck joints, and applies
  the prior contact mask before movement. Death retains the route and
  uses authored return, death and deactivation links; the body stays
  hittable once it has retired. A fatal lunge contact refuses by name
  rather than substituting ordinary Lara state 6 for the special pose.
  `tr2-camera.md` Q-T2C-05 binds the pre-stride footfall cue and the
  common positive and negative bounce consumer; at 491 of `rex-mask`
  the +40 kick is consumed by the same camera update.
- The merged navigation already preserves an older link on a blocked
  mark (`tr2/nav.c`, `expand`) and clears a pending request on a
  current-generation blocked mark on the creature's own box
  (`tr2/enemy.c`, `chooseMood`). Neither rule was rewritten.

| Route | Verified scope on the integrated base |
|---|---|
| `shotgun-tiger` | 246/246 logic samples, including all save fields. The first shell hits four times; a second 33 ticks later hits six times. The weapon slot is released during stow. |
| `rex-mask` | 761/761 logic samples; the production trigger completes only rex 104's mask. Reversing the order of the authored sector groups, including repeating the incomplete contribution, also wakes only slot 104. |
| `rex-gold` | 1,251/1,251 logic samples from the production host. The genuine Gold pickup wakes slot 99 at tick 43; no supplied pickup or wake is linked or used. |
| `shotgun-ammo` | 81/81 logic samples on the published schema-20 fixture; no schema-21 version was published. Both co-located items commit at tick 43 through the merged pickup code: reserve 12→24 and large medikits 1→2. No combat-side grant. |

Logic covers Lara, camera, active items (including schema-21 brains),
world, inventory, save and gameplay RNG. Presentation RNG remains
visible and differs (e.g. `shotgun-ammo` from tick 2); the native menu
advance is verified separately.

### What the two games share

Peers below are in `game/lara_weapon.c` unless noted. The final column
counts TR2 executable source lines, not comments or braces, which
still restate a TR1 rule. It distinguishes an identical leaf (`=`)
from a parameterized policy shape (`~`); a line that calls shared
code is not counted. The pistol counts describe the merged code,
not a proposed rewrite.

| TR2 routine / rule | TR1 peer | Same, parameterized or different | Restated `= / ~` |
|---|---|---|---:|
| `fireShotgun`: origin, six-ray limit, outer spread, reserve and inner draws | `Lara_weaponFire`, `weaponParams`, `inventory_economy.c:weaponRule` | Origin (height 500 for both) and six attempts shared; signed spread primitive shared. TR1 atomically consumes six pellet units and takes only pitch then yaw draws per ray with shift rounding; TR2 takes outer yaw then pitch, amplitude 3640 and truncation, then zero-amplitude pitch and yaw only for each issued pellet (24 full shell, 16 two pellets). Same reserve units, different accounting, partial shells and empty priority. [Arms 80–89][arms] | 0 / 0 |
| `firePistol`, `game_tr2_pistol_ray_angles` | `Lara_weaponFire`, `weaponParams` | One ray, 650 muzzle offset, two draws pitch then yaw, damage 1. TR1 rounds by shift and uses unlimited inventory sentinel, TR2 truncates and restores live 1000 reserve before spending. Spread primitive and origin shared. [Arms 40–42, 54–66][arms] | 0 / 2 |
| `resolveRay`: damage, terrain and counters (both) | `Lara_weaponFire`, `Lara_applyWeaponHit` | Not equivalent. TR1 rejects dead targets, approximates perpendicular length and uses its own trace; TR2 uses nearest keys with live joints, exact squared sphere test, corpse hits, positive-to-nonpositive kill, ordered terrain sweeps and effect allocation. Damage is parameterized within the TR2 routine: pistol 1, shotgun 3 (TR1 shotgun 4). [Arms 54–66, 87][arms] | 0 / 1 |
| `pistolAimAt`, `selectTarget` (both) | `Lara_weaponGetAimPoint`, `Lara_weaponFindTargets`, `Lara_weaponTrackTargets` | Similar authored bounds aim point, not identical: signed midpoint truncation, quantized bearing/projection, linked-sector sight with full slopes. Selection visits five creature slots rather than TR1's active list; tests from the muzzle, strict closest range, yaw slack, and admits retained corpses. Cannot share the whole search/sight path. [Arms 44–50, 66][arms] | 0 / 4 |
| `aimShotgun`: lock and easing | `Lara_weaponTrackTargets`, `weaponAim`, `Lara_weaponUpdateShotgun` | Shared 10° easing, different arm and gaze: left angles, both locks, torso from left, head neutral; shotgun limits 10010/10920 and 11830/14560. TR1 retains independently with mirrored asymmetric bounds, so its lock controller is not interchangeable. [Ground 141–145][ground], [Arms 79][arms] | 0 / 2 |
| `shotgunTick`: draw, fire, stow, pose | `Lara_weaponDrawShotgun`, `Lara_weaponUpdateShotgun`, `Lara_weaponHolsterShotgun`, `Lara_meshSwapShotgun` | Shared hand/back swap interface, draw/stow pair publication and lock-or-free-fire predicate. A different controller is required: TR1 drives a right-arm counter, reverses draw for stow and reloads at 26. TR2 steps authored item links: 223+1 draws, 224+0 fires before advance, 33-tick cycle, 225+21 stows, final pose preserved. Both request the hand mesh at draw +10. `TR64_RULE(ARMS)` distinguishes separate back model from TR1 torso swap. TR1's ready-path stores retain their existing `R`/`L` aliases: using the helper there adds 16 bytes of MIPS instructions. No TR1 peer for pose allocation/release. [Arms 72–85][arms] | 1 / 0 |
| `drawPistols`, `setPistolPose`, `pistolClip` | `Lara_weaponDrawPistols`, `Lara_setWeaponState` | Mesh routine shared. Same draw motif and arm angles zeroed, but TR2 samples a contiguous 5→23 counter and resets target/flash/status at its recorded transition, rather than traversing relative clip boundaries. [Arms 27–32][arms] | 1 / 4 |
| `holsterPistols`, `setPairedLook`, `clearPistolAim` | `Lara_weaponHolsterPistols`, `Lara_setWeaponState` | Same per-arm recoil abort, aim lowering and gun return; TR2 preserves the last arm clip with zero counters and keeps its gaze timing independent of ready targeting, including the finish tick except with Look. [Arms 35–36][arms], [gaze rule][gaze] | 2 / 5 |
| `updatePistolAim`, `readyPistols` | `Lara_weaponTrackTargets`, `Lara_weaponUpdateTargets`, `Lara_weaponUpdatePistols` | Easing shared. Similar lock cone and independent arms, but decoded counter, lock retention/target geometry, interrupted recoil, gaze, sound and presentation timing differ. Draw/Action arbitration occurs before ready fire. [Arms 31–38, 44–52][arms] | 2 / 9 |
| `game_tr2_request_ring_weapon`, `game_tr2_update_arms`, `gunCanDraw` | `Lara_changeWeapon`, `Lara_weaponUpdateState`, `Lara_updateWeapon` | Ready → holster → free is common; ring ownership, last-gun and flare transitions and current-gun wade depths are different. Calls the same TR2 update for UI and replay; host-only pause/RNG does not belong in the rule. [Ground 152][ground], [Arms 31, 78, 106][arms] | 0 / 4 |
| `game_tr2_validate_arms`, shot-pose bank admission | None | TR2 has packed pose links and free-list pose item; no TR1 peer. | 0 / 0 |

[arms]: specs/wall/tr2-lara-arms.md
[ground]: specs/wall/tr2-lara-ground-and-weapons.md
[gaze]: specs/wall/tr2-lara-slide-height-and-gaze.md

For Q-T2K-13, the tiger leaves the old mesh-22 yaw in place while the
strike allocates blood, then eases and clamps the head for the following
pose. The directed bite control handles a prior yaw of 6000 and an
end yaw of 5090: the two transforms land at distinct positions and
the allocated blood must use the first one.

Rex admission validates all authored links and keys in its own model
bank, refuses a clip state that does not fit the 8-bit item state, and
the live switch stops by name on an unknown state. Mesh 13 bounds the
fixed neck/strike indexes (11–12 and 12–13).
The live controller has no hardwired absolute frame; the named fatal
refusal prevents using the separate special Lara bank. Shotgun
admission and the runtime share one request table. A request is
honoured when the current clip has a state-change range for the
requested state that the runtime transition can reach, including the
end-link goal rule; a named specification destination must be arrived
at, and an unnamed one is whatever the level's range says. The
shipping recoil clip, asked for aim's state at relative frame 0,
honours that request with the range at absolute frames 47-48, which
restarts recoil (224/47). `tr2-lara-arms.md:85` names goal 4. On a
recoil update that is not the first frame, Action released and no
lock sets that goal, and the write repeats while those conditions
hold. A re-press does not clear it, and neither does a lock gained
on that re-press. Absolute frames 60 and 61 then select aim 222/0;
absolute frames 47 and 48 select lower 226/0. Those are the only
early exits, checked by the same walk the runtime uses, from every
relative frame 1 to 12 and from the first frame. A target still
selected but no longer locked, with Action held, does not cut the
recoil; the update that starts the next 224/0 does not fire and
takes the lower range. A release that keeps a lock does not set
goal 4; the next first frame sets goal 0, which loops 224.
`shotgun-no-target` releases at 224/2 and selects 222/0 at the
relative 12 to 13 step. `shotgun-release-repress` keeps the exit
through a re-press and, after absolute 60, finishes the clip into
lower then aim.
`shotgun-tiger` exercises the released-Action request at ticks 50-63:
the pose stays 224/0 with the lock held and does not enter 222. A
request with a known start is one walk from that frame, and the first
range that fires is the one that counts. A holster
must be honoured from every frame of the current clip. A bank that fails
either rule is refused by name. The unmutated pack loads. Aim's
start is the level record; it ends on the frame before draw. The
lower clip's span is the level record. The other three clips keep
their cited endpoints. Pose keys, compatible commands and in-bank
authored links remain checked.

`shotgun-rex-v2-controls.log` records the 26 interaction controls and
the combat suite, along with the new weapon-selection, pre-turn blood
pose, mask reversal, partial shell and named fatal-lunge controls.
The partial shell issues two pellets and advances gameplay RNG 16
times. The independently linked `shotgun-rex-v2-camera-unit.log`
checks both signs of bounce, the camera operand's bit 14, and
qualification of an unsettled lever. No emulator was run.

The pre-turn blood control rejects the old order
(`shotgun-rex-v2-tiger-red.log`). `shotgun-rex-v2-union.log` compares
52 sealed bodies and 14 refusal prefixes byte-for-byte against main,
and keeps all 48 accepted v1 seals. The v1 differences are the merged
placed-start distance rule and the now-live `pickup` and
`switch-door`, both matched to the interaction producer.

The TR2 play cart at `2ddf15b` and v2 both occupy 493,896 / 622,592 B
resident. `tr2_wall` is unchanged at 1,504 B before padding (4,096 B
paged); `tr2_creatures` grows from 14,528 to 15,776 B before padding
(16,384 B paged in both). Five resident-to-unit dispatch references
and zero allow-listed references, 16,288 B hot window and 1,008 /
3,360 B room/item stack pass (`shotgun-rex-v2-build.log`).

The TR1 shipping `.text` (437,744 B) is byte-identical to `2ddf15b`.
The four load spans and all 36 allocatable sections, including every
paged unit, match except the generated source, configuration and
TLB-package identities (`shotgun-rex-v2-tr1-loaded.log`). The raw
links retain their own real identities; the comparison accounts only
for those fields. All 100 solo fixture/schema pairs are identical
(`shotgun-rex-v2-tr1-solo.log`).
