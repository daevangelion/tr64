# M10: Great Wall hazards, zip line and finish

The gameplay patch is developed above the staged [interaction source split](tr2-source-layout.md). Clean-side sources are the wall specifications, the first-game port and the authored Wall pack. Retail captures verify, but do not set, rules. Evidence: `$TR64_WORK/evidence/m10/hazards/` (called `$E` below); input and retail records read in place from `$TR64_WORK/fixtures/oracle-wall/tr2/`. Pack: `$TR64_WORK/evidence/m10/wall/wall.pack`, SHA-256 `d5e1cb90646be0bdb94545e369eca0c118a5684a68cbcc9e2c454de73a59a02e`.

## Rules and existing code reused

| Owner | Reused rule or mechanism | Tomb Raider II difference |
|---|---|---|
| Contact pass, `game/collision.c` | First game's item-chain iteration, saved successor, positive-health admission and strict per-axis distance `<4096`. Retains the accepted TR2 door/enemy bump bookkeeping. | [Great Wall objects, Q-INT-08](specs/wall/tr2-great-wall-objects.md#recorded-hazard-approaches): fix the starting room and its authored portal order at entry; never sort, deduplicate or restart after a push. Later visits use the current item membership and Lara's current position, even after a killing contact. |
| `tr2/hazard_contact.c`, spikes | I `hazard_dispatch.c` and `lara_health.c`: ordinary bounds, damage and the same spike-death clip; reuse packed sphere contact and the resident sequel effect pool. | [Discs, blades and spikes](specs/wall/tr2-great-wall-objects.md#discs-blades-and-spikes): require both overlap tests, spend the first gameplay roll on a safe Walk, hurt at grounded speed 30 or in the air, and kill at descent >6. Keep stored descent on the lethal snap. Spend all 81 draws before pool outcomes can affect anything: one contact draw plus four per attempt. Use the pre-snap Y for blood; no public record distinguishes this field. |
| `tr2/hazard_contact.c`, guillotine | I `object_trap.c:TrapSwingBlade_*` supplies the switch/pose and damage primitives, not its immediate contact callback or gameplay RNG. | [Guillotine](specs/wall/tr2-great-wall-objects.md#discs-blades-and-spikes) and [model-relative starting pose](specs/wall/tr2-first-levels.md): third bank clip, request idle on first update starting in cut, read stored mesh-1 touch before the pose update, and spend six presentation draws for two blood attempts. The resident contact callback can register touch from another authored portal room. |
| `tr2/hazard_boulder.c` | I `hazard_boulder.c` shares the grounded death clip and 100-point airborne injury; [I's grounded hit mark](specs/wall/objects-missing.md#10-rolling-ball-object-38) is retained. Reuse the existing TR2 solid body push from the door leaf; leave the port's rolling/linked floor and camera shake timing intact. | [Great Wall boulders, Q-HAZ-01](specs/wall/tr2-great-wall-objects.md#great-wall-boulders): ground kill keeps her room, aligns the body, requests centred Follow at 30940/−4550, and attempts 15 four-draw blood effects before shake spends three draws. Airborne contact attempts a fixed radial spray after any push. |
| `tr2/object_zipline.c`, Lara state 70 | I has empty zip state hooks (`Lara_s_STATE_ZIPLINE`, `Lara_c_STATE_ZIPLINE`). Reuse the existing TR2 reach helpers, authored animation and end commands, independent room membership, airborne speed delta, forward Reach/correction and the existing camera bearing request. | [Zip line](specs/wall/tr2-great-wall-objects.md#zip-line): at mount align to +371, busy hands, state 19 / clip 215 and item ride mark 256; authored lead-ins then travel at +5 to 100, quarter-speed descent. Carry prior to the clear-line test; manual release advances once before assigning 100/40 and ordinary Lara advance; endpoint instead supplies instantaneous track velocities, clears the mark and deactivates. |
| Finish action, `game/trigger.c` and tick owner | I recognizes the shared finish opcode and level-stop interface; reuse action-list qualification and the same simulation phases. | [Great Wall level, Exit](specs/wall/tr2-great-wall-level.md#exit-cut-and-venice-carry): action 7 requests a boundary at the end of the completed gameplay update. Preserve the last movement, camera and clock sample. The canonical tape seals at tick 122; any extra mask is refused by name without a seal. No cutscene, statistics or Venice transition runs. |
| `tr2/hazard_disc.c`, launcher 62 and disc 61 | I `object_trap.c` dart emitter: clip motion through `ItemObj_animProcess`, spawn through `ItemObj_add`. | [Discs](specs/wall/tr2-great-wall-objects.md#discs-blades-and-spikes): latched 30-tick firing loop, exact-east spawn exception, 50 on the disc's next update. Spawn stores the sector floor byte shifted left by 8; the first control after the step stores the slope sample. 16384 does not end the disc. Removal spends sound 258 and one sprite-238 spark. Spawn, removal and the spark stay resident; the motion update is paged. |
| `tr2/hazard_blade.c`, rolling blade 80 | I `TrapSwingBlade` supplies the damage primitive, not its pendulum states or immediate contact. Clip speeds stay in the bank. | [Large rolling blade](specs/wall/tr2-great-wall-objects.md#discs-blades-and-spikes): wake is the model's turn clip frame 83 goal 2; reverse when the floor 1536 ahead is missing; 100 only while not in state 1; height is the floor sample. |
| `tr2/hazard_wall.c`, spike wall 85 | I has no moving wall. Bounds, spheres and `Lara_hit` are shared. The nearest-face tie is the door order already used by the solid push. | [Moving spike walls](specs/wall/tr2-great-wall-objects.md#discs-blades-and-spikes): 16-unit step, status 2 stop while still collidable, 20 on the next update. Blood is three effects at Lara's X/Z and Y minus 512, her room, the wall's yaw, speed 1. |

The three transient blood sources share the existing 100-entry room-effects pool and 24-update blood lifetime, without retries or eviction ([Q-HAZ-01](specs/wall/tr2-great-wall-objects.md#discs-blades-and-spikes)). No new gameplay stream or state writer is introduced. The host comparison uses the same pinned `tr64-tr2-great-wall20` profile and keeps every new route's full range. As in the accepted interaction slice, the raw presentation RNG difference is visible, not a gameplay gate or profile exclusion.

## Narrow evidence and boundaries

Specs were brought forward from `7fb706b` into the working tree (the six
wall files that differ from `3456da2`); they match `main` at `f38dea5`.
The closed-loop replay is the determinacy check for the three answers,
which had been merged on a form check only.

| Record | Rows | Gameplay families | Named tick from the text |
|---|---:|---|---|
| `spike-walk` | 93 | equal | safe walk spends the contact draw and does not deduct |
| `spike-jump` | 91 | equal | tick 2 goal stays 15; tick 43 state 8, health and air −1 |
| `boulder-hit` | 128 | equal | tick 84 state 46, health −1, air 1800; later crush ticks keep 1800 |
| `zip-release` | 192 | equal | tick 168 state 9, health −1, air 1800; tick 169 state 8 sets air −1 |
| `zip-stop` | 391 | equal | tick 91 camera index/last 5 at (67072, 22272, 72192) |
| `level-finish` | 123 | equal | boundary after tick 122; an extra mask is refused by name |
| `blade-hit` | 168 | equal, sealed | the collapsing tiles on the route are on this tree; no dependency |
| `blade-pass` | 181 | equal, sealed | as above |
| `disc-hit` | 111 | equal | sector byte 4352 at tick 65, slope sample 4480 at 66. Hit, 50 and death equal |
| `disc-pass` | 46 | equal | sector byte 4352 at tick 3, slope sample 4480 at 4 |
| `rolling-blade-hit` | 386 | equal | wake 315/83, reversal, 100 |
| `rolling-blade-pass` | 446 | equal | as above |
| `spike-wall-hit` | 401 | equal | 16-unit step, camera timer, stop at 353 |
| `spike-wall-pass` | 401 | equal | stop at 290 holds the position |

Presentation `rng[1]` differs from tick 2 on every route and is not a
gameplay gate. The other 54 schema-20 routes match on gameplay families
against retail on this host. `rusty-key` and `medipacks` complete when the
flags follow the trace (`--key-confirmation 610 198 50`;
`--medipack-selection 169 171 87` and `191 172 81`): 767 and 213 lines.
Without those arguments they stop by name (`keyhole: key-ring confirmation
required` at tick 610; `undefined control or mask separator` at tick 168).

Assumed, not in the text: none. The disc spark is the pistol ricochet kind
with life 6 (`tr2-great-wall-objects.md` disc spark life). A full item list
creates no disc and does not stop (`tr2-great-wall-objects.md:133`).

The placed disc launchers, rolling blades and spike walls no longer stop
by name. Their six records are full length and equal on gameplay families,
including `rng[0]`. A 1500-tick firing window then a 700-tick drain kept
the effect pool at 11 of 100 and returned the disc count to its base;
blood still allocated afterwards.

Dry jump routes already sealed stay equal on gameplay families. The
fast-fall state is not numbered in the text; no recorded tick exercises it.
End-position zip reuse is not recorded; it refuses by name rather than
approximating a fresh mount. No emulator was run.

## Cart and reproduction

Disc, blade and spike-wall motion is in `tr2_wall` (`c1200000`, 12,288 B, 3 pages): `game_tr2_launcher_update`, `game_tr2_disc_update`, `game_tr2_rolling_update`, `game_tr2_spike_wall_update`. Resident (`c0001000`): `game_tr2_disc_launch`, `game_tr2_disc_harm`, `game_tr2_disc_removed`, `game_tr2_disc_spark`, `game_tr2_rolling_harm`, `game_tr2_spike_wall_harm`, `game_tr2_moving_contact`, `game_tr2_nearest_face`, `game_tr2_hazard_prepare`. A launcher returns to resident code before the spawn; a wall or blade returns before a floor trigger. Measured on this tree: resident 502,312 of 622,592 B; `refs-check` 7 dispatch rows and 0 waivers; hot window 16,288 of 16,384 B. First-game `TR64_GAME=1 TR64_PLAY=1` `.text` is 437,744 B and identical to `GWaterTR1Base`.

```sh
export TMPDIR=$TR64_WORK/tmp/T2Hazard PYTHONDONTWRITEBYTECODE=1
E=$TR64_WORK/evidence/m10/hazards
B=$TR64_WORK/builds/T2Hazard
W=$TR64_WORK/evidence/m10/wall/wall.pack
F=$TR64_WORK/fixtures/oracle-wall/tr2
rtk proxy make -j2 BUILD_DIR="$B" TR64_GAME=2 TR64_PLAY=1 TR64_TEST=0 TR64_LEVEL_PACK="$W" rom host
rtk bash "$E/controls.sh"
rtk python3 "$E/replay.py" "$B/host/tr64-host" "$E/replay" "$W" "$F"
```
