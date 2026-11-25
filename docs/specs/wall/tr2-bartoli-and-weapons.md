# Tomb Raider II: Bartoli's Hideout objects, paired weapons, flame and floor trigger actions
Game and release: Tomb Raider II, Steam appid 225300, Windows PC `Tomb2.exe`, 912896 bytes, SHA-256 `fd9c91d14d54540b3e00c9bedafeef9e4ddb1e9542adc9f29170c7f19b1a657b`.
Authored data: `venice.TR2` (SHA-256 `46fe2ac17a8629393a00866293adfe7f79b444909c81406920f74b86be6f712f`) and `TOMBPC.DAT` (SHA-256 `d1f1a9c8f784301824ed6c59c8b34378d84e7f7fc93c90f5df7c4a5c8ed9868c`).
Reference source checked in place: TR2Main (`f38bc90438e21e8a37e915a812cade52efce1d68`), files `game/{control,effects,gameflow,invfunc,larafire,objects,pickup,sound,sphere,traps}.cpp`, `specific/sndpc.cpp`, `global/{types,vars}.h`; all rules below are independently verified against the pinned retail executable and authored level/script files.
Reader: WallRE2. Date: 2026-11-24.
Evidence: static retail PC rules cross-checked against `venice.TR2`, `TOMBPC.DAT`, and existing Tomb Raider II wall specifications. No runtime playthrough, reload test, or PlayStation parity is asserted.

Distances are in game units (1024 per tile, 256 per height click); Y increases downward. Angles use 65536 units per full turn (182 per degree, 16384 per quarter-turn). Each gameplay tick is 1/30 s. Public object type numbers, animation clip numbers, frame numbers, and sound IDs refer to the authored PC data in `venice.TR2` unless noted as global. See [shared triggers and interactions](tr2-triggers-and-interaction.md), [The Great Wall objects](tr2-great-wall-objects.md), [ground movement and firearms](tr2-lara-ground-and-weapons.md), and [placed-item defaults and campfire](tr2-items-general.md) for inherited baselines referenced below.

---

## 1. Pushable block variants (`67..70`) and trapdoor variants (`114..115`)

### Pushable blocks (`67..70`)

**Established (static) — complete behavioural equivalence.** All four pushable block object types (`67`, `68`, `69`, and `70`) share identical initialization, per-tick update, Lara interaction, drawing, room-flip floor adjustment, and save/load restoration behaviour. No gameplay rule branches on which of the four type numbers is used; the four types differ only in their authored visual meshes and per-model animation tables.

| Aspect | Behaviour across types `67..70` |
|---|---|
| Setup and save flags | **Established (static).** Each of `67..70` raises its starting sector by 1024 units and closes its blockable navigation box on startup (unless invisible), and saves position, animation, and status flags identically. |
| Push, pull, drop, and heavy trigger | **Established (static).** Identical to type `67` in [The Great Wall push/pull block rules](tr2-great-wall-objects.md#pushpull-block): grasp bounds X `−300..300`, Y `0`, Z `−692..−512`, angles `±1820 / ±5460 / ±1820`, ready commit at 19 frames after Lara's clip `120` starts, push ceiling clearance 100 below nominal top with slope rejection, pull clearance at nominal top plus Lara retreat check, immediate departure-sector restoration, arrival-sector raise and navigation-box closure on settle, followed by heavy trigger evaluation. If a block moves onto a lower floor it enters gravity fall, lands on the underlying floor, triggers the range-weighted floor-shake kick and sound `70`, and settles with heavy evaluation. |
| Room flip and reload | **Established (static).** Room-flip and save-restoration passes identify pushable blocks by their shared behaviour class rather than by type `67` alone, so all four variants release the outgoing floor height on flip and reapply the 1024-unit raise on the incoming room or resting reload. |
| Authored placements in `venice.TR2` | **Established (static).** Item `6` (type `67`, room `4`, tile `(53, 29)`) and item `129` (type `68`, room `151`, tile `(63, 38)`) both author identical three-clip layouts: idle state `1` (1 frame), push state `2` (155 frames, end displacement `(0, 0, +1024)`, deactivate command, sound `63` at frames `56`, `115`, `140`), and pull state `3` (172 frames, end displacement `(0, 0, −1024)`, deactivate command, sound `63` at frames `204`, `247`, `279`, `308`). |

### Hinged trapdoors (`114..115`)

**Established (static) — complete behavioural equivalence.** Types `114` and `115` share identical per-tick open/close state requests, floor-support substitution, ceiling-underside substitution, two-tile facing footprint check, and animation/status save flags. Neither type has any subtle behavioural difference from the other.

| Aspect | Behaviour across types `114` and `115` |
|---|---|
| Open, close, and surface rules | **Established (static).** Identical to type `114` in [The Great Wall hinged trapdoor rules](tr2-great-wall-objects.md#hinged-trapdoor-and-collapsing-tiles): when triggered in state `0`, requests state `1`; when untriggered in state `1`, requests state `0`. Supplies floor support at the item's origin Y (for queries at or above origin) and ceiling underside at origin Y `+ 256` (for queries strictly below origin, when origin is below the existing ceiling) only while current state is `0` and the query tile is either the trapdoor's own tile or the single adjacent tile in its facing direction (`+Z` for yaw `0`, `+X` for `+16384`, `−Z` for `±32768`, `−X` for `−16384`), provided the sector's floor data names the trapdoor. |
| Authored placements in `venice.TR2` | **Established (static).** Item `36` (type `114`, room `33`, tile `(47, 40)`, yaw `0`, operated by lever `20` in room `20`/`54` and referenced by dummy trigger in room `40`) and item `48` (type `115`, room `50`, tile `(40, 35)`, yaw `32768`, operated by underwater lever `49` in room `51`) have identical four-clip animation structures: closed idle state `0` (1 frame), closing state `1` (46 frames, sound `162` on frame `1`), open idle state `1` (1 frame), and opening state `1` (26 frames, sound `161` on frame `48`). |

---

## 2. Detonator box (`type 227`)

### Interaction conditions and key consumption

| Condition | Established retail PC rule |
|---|---|
| Outer eligibility | **Established (static).** Lara must not already be in an extra animation. The detonator's status must not be spent/deactivated (status `4`). Action must be held, Lara's hands must be free, gravity must be off, and Lara's current state must be standing (`2`). |
| Reach and facing bounds | **Established (static).** Before testing reach, the detonator's temporary interaction pose sets pitch `0`, roll `0`, and **yaw equal to Lara's current yaw** (saving its original orientation). Reach uses inclusive local bounds X `−256..256`, Y `−100..100`, Z `−256..256`, pitch `−1820..1820`, yaw `0..0`, roll `0..0`. Because the temporary yaw matches Lara's own yaw, Lara can activate the box from **any** facing direction around it as long as she is within the 256-unit horizontal box, within ±100 vertically, within ±1820 pitch, and at zero roll. |
| Key requirement | **Established (static).** Hard-bound specifically to **Key 2** (key-ring type `198`, world pickup type `194`, named `Detonator Key` in `venice.TR2`), rather than matching generically by keyhole object number. If no inventory choice is pending (`−1`), pressing Action inside reach opens the key ring (mode `2`). |
| Refusal and solid fallback | **Established (static).** If reach fails, eligibility fails, or the chosen ring item is not `198`, the detonator restores its saved pitch, yaw, and roll and falls through to ordinary solid object collision (bounds, sphere contact, and push-out). Unlike a generic keyhole, choosing a wrong key or pressing Action on a spent detonator plays no refusal sound `2` from the detonator interaction and consumes nothing. |
| Acceptance, alignment, and key consumption | **Established (static).** When ring item `198` is confirmed: (1) one copy of `198` is removed from the key ring; (2) the detonator does **not** restore its prior yaw/pitch/roll for type `227`, so its yaw remains aligned to Lara's yaw and its pitch/roll remain `0`; (3) Lara snaps to local offset `(0, 0, 0)` relative to the detonator (exact X, Y, Z coincidence, preserving her yaw); (4) Lara switches to the auxiliary extra-animation model (`type 12`, base clip `278` in `venice.TR2`), frame `0`, current state `0`, goal state `1`, and immediately performs one animation advance (entering clip `279`, state `1`, frame `1` on the acceptance tick); (5) Lara enters extra-animation mode and hands-occupied status; (6) the detonator enters active status (`2`) and is added to the active update list. |

### Animation progression, explosion timing, and key-trigger satisfaction

Let tick `T` denote the gameplay tick on which `198` is accepted.

| Tick / relative frame | Established retail PC behaviour |
|---|---|
| Model clips in `venice.TR2` | **Established (static).** Detonator (`type 227`, 2 meshes) has a single clip `488`: state `0`, frames `0..157` (length `158`), terminal link `488/157` (holds on its final frame with no deactivate command and no animation commands). Lara's extra model (`type 12`) has clip `278` (state `0`, frame `0`, transitions to `279/1` on goal `1` over range `0..1`) and clip `279` (state `1`, frames `1..158`, length `158`, chains to `278/0`, plays sound `354` at frame `63`). |
| Extra-animation ticks `T + 1..T + 158` | **Established (static).** During extra state `1`, Lara's state update is a no-op, manual Look is suppressed (gaze relaxes normally), ordinary Lara collision and item collisions are skipped, and floor triggers are not evaluated (no sector trigger is recorded for evaluation). Each tick, the active detonator updates in the active-item pass before Lara, advances its animation, and compares its elapsed frame count (`current frame − clip base frame`): |
| Dynamic light (elapsed frames `76..99`) | **Established (static).** On ticks `T + 76` through `T + 99` inclusive (24 ticks, strictly `75 < elapsed < 100`), the detonator emits a dynamic point light at its origin `(X, Y, Z)` with intensity `13` and falloff `11`. |
| Explosion sound and camera shake (elapsed frame `80`) | **Established (static).** On tick `T + 80` (elapsed frame `80`), the detonator sets camera bounce to `−150` and plays non-positional explosion sound `105`. No explosion sprite effect is spawned and no room flip occurs on this tick. |
| Plunger sound (Lara frame `63`) | **Established (static).** Lara's extra clip `279` plays sound `354` on frame `63` (tick `T + 62`). |
| Completion and key-trigger (subtype `3`) firing (tick `T + 159`) | **Established (static).** On tick `T + 158`, Lara's clip `279` passes frame `158` and chains into `278/0` (extra state `0`), while the detonator holds at `488/157` in active status (`2`). On tick `T + 159`, extra state `0` runs at the start of Lara's update: it resets Lara to ordinary standing clip `103` (frame base, current and goal state `2`), clears extra-animation mode, frees Lara's hands, restores chase camera mode, and restores 80° FOV (`14560`). Because extra-animation mode is now clear on tick `T + 159`, Lara's collision pass runs immediately afterward, populates the sector trigger for the detonator's tile, and evaluates floor triggers. The key-trigger gate (trigger subtype `3`) requires its controlling item to be in active status (`2`) and Lara's hands **not** occupied; both now hold, so the gate transitions the detonator to spent/deactivated status (`4`) and is satisfied, executing the sector's trigger action list on tick `T + 159`. |
| Authored sector trigger in `venice.TR2` | **Established (static).** Detonator item `75` sits in room `73` at `(37376, 3840, 18944)`, tile `(36, 18)`. That sector's key trigger (subtype `3`, timer `0`, mask `0x1f`, controller `75`) executes in order: activate item `125` (Jade Dragon in room `131`, inert pickup), play one-shot music track `37`, activate item `124` (hidden gunman in room `131`), and toggle **FlipMap slot `1`** (swapping room pairs `73↔143`, `130↔147`, `131↔145`, `133↔144`, `134↔142`, `135↔146` to collapse the building). Thus the explosion audio/shake (`T + 80`) and light (`T + 76..99`) precede the room-swap collapse (`T + 159`). |
| Retirement and non-retriggerability (tick `T + 160` onward) | **Established (static).** On tick `T + 160`, the detonator's active update observes spent status `4` and removes item `75` from the active list. Because the interaction test requires status `!= 4`, the detonator **cannot be re-triggered** and remains an ordinary solid obstacle thereafter. |

---

## 3. Statue with knife blade (`type 82`)

| Rule | Established retail PC behaviour |
|---|---|
| Startup pose and placements | **Established (static).** Type `82` initializes at the model's base animation plus `3` (`407` in `venice.TR2`), first frame (`54`), current state `1`, saving animation and status flags. In `venice.TR2`, items `16`, `17`, and `18` sit in room `16` at tiles `(57, 33)`, `(55, 33)`, and `(59, 33)`, facing `−X` (`49152`), initially inactive. |
| Activation and deactivation | **Established (static).** Controlled by the standard trigger-active rule (complete 5-bit mask with untimed or non-expired timer, inverted for reverse-start items). On each active update: if triggered **and** current state is `1`, request goal state `2`; otherwise (untriggered, or already in state `2`), request goal state `1`. When untriggered (e.g. by an antitrigger or expired timer), the statue finishes its current strike/return and rests motionless in state `1` without removing itself from the active list. In `venice.TR2`, room `16` tile `(59, 33)` activates items `18`, `16`, `17` (ordinary untimed full mask), and room `110` tile `(53, 33)` deactivates `17`, `18`, `16` via a one-shot antitrigger (subtype `9`). |
| Swing cycle and sound cues (`venice.TR2`) | **Established (static).** Model `82` has 8 meshes (`0..7`, mesh `7` is the knife blade) and four clips starting at `404`: (a) `407` (+3), state `1` ready, frame `54` (1 tick), transitions to `406/31` on goal `2` over range `54..55`; (b) `406` (+2), state `2` strike swing, frames `31..53` (23 ticks), chains to `405/30`, plays sound `80` at frame `31`; (c) `405` (+1), state `2` strike hold, frame `30` (1 tick), transitions to `404/0` on goal `1` over range `30..31`; (d) `404` (+0), state `1` return swing, frames `0..29` (30 ticks), chains to `407/54`, plays sound `81` at frame `0`. While continuously triggered, one full cycle lasts **55 ticks**: **24 ticks** in hazardous state `2` (`406` frames `31..53` plus `405/30`) and **31 ticks** in safe state `1` (`404` frames `0..29` plus `407/54`). |
| Collision and joint-based contact check | **Established (static).** Uses trap collision: while inactive (and not invisible), behaves as an ordinary solid object (bounds + sphere contact with push-out). While active (status `2`), tests animated bounds overlap and per-mesh sphere overlap against Lara to populate the item's per-mesh touch bitmask, **without** pushing Lara out. Because Lara's collision pass runs after active items and ordinary item animation clears the touch bitmask on every update, a mesh touch recorded on tick `N` is evaluated by the statue on tick `N + 1`. |
| Damage and blood spawn | **Established (static).** Before advancing animation on an active tick, if **mesh `7` touch bit (`0x80`) is set** AND **current state is `2`**, the statue marks Lara hit, subtracts **20 HP** (with no per-hit cooldown across consecutive overlapping ticks), and spawns **1 blood splat** in Lara's room using four gameplay random draws (`r1..r4`, each uniform in `0..32767`, integer divisions truncated toward zero): X `= Lara.X + trunc((r1 − 16384) / 256)`, Z `= Lara.Z + trunc((r2 − 16384) / 256)`, Y `= Lara.Y − trunc(r3 / 44)`, speed `= Lara.speed`, direction `= Lara.yaw + trunc((r4 − 16384) / 8)`. Contact with meshes `0..6`, or any contact during state `1`, deals no damage and spawns no blood. |

---

## 4. Automatic pistols, Uzis, grenade ammunition, and Bartoli's Hideout script metadata

### Pickup and conversion quantities

| Item (world pickup / ring type) | First weapon pickup | Duplicate weapon pickup | Ammo box pickup |
|---|---|---|---|
| Automatic pistols (`137` / `159`) & ammo (`144` / `166`) | **Established (static).** Inserts `159` into main ring, converts every carried `166` bundle at **+40** rounds each (removing `166`), adds **+40** rounds to live reserve, and replaces all remaining uncollected world placements of `137` in the level with `144`. Does not alter remembered weapon or back-weapon slot. | **Established (static).** Because level-start initialization (if already owned) and first pickup both replace all uncollected world `137` placements with `144`, a second world pickup is collected as `144` (**+40** rounds). A script reward granting a gun already owned adds only its accompanying ammo packs. | **Established (static).** If `159` is owned, adds **+40** rounds directly to live reserve (no ring change); if not owned, adds **+1** bundle of `166` to main ring. |
| Uzis (`138` / `160`) & ammo (`145` / `167`) | **Established (static).** Inserts `160` into main ring, converts every carried `167` bundle at **+80** rounds each (removing `167`), adds **+80** rounds to live reserve, and replaces all remaining uncollected world placements of `138` in the level with `145`. Does not alter remembered weapon or back-weapon slot. | **Established (static).** Same replacement rule: a second world placement becomes `145` (**+80** rounds). Script reward for an already-owned gun adds only its accompanying ammo packs. | **Established (static).** If `160` is owned, adds **+80** rounds directly to live reserve (no ring change); if not owned, adds **+1** bundle of `167` to main ring. |
| Grenade ammunition (`148` / `170`) | — (launcher is `141` / `163`, adding **+2** grenades + `2` per carried `170` bundle) | — | **Established (static).** If Grenade Launcher (`163`) is owned, adds **+2** grenades directly to live reserve (no ring change); if not owned, adds **+1** bundle of `170` to main ring. |

### Weapon parameters and firing rules versus standard pistols

All three paired handguns — Pistols (weapon `1`, arm model `1`), Automatic Pistols (weapon `2`, arm model `4`), and Uzis (weapon `3`, arm model `5`) — share the same targeting, arm-tracking, draw/holster, spread, and muzzle-flash machinery established in [ground movement and early firearms](tr2-lara-ground-and-weapons.md#pistols-automatic-pistols-uzis-and-shotgun) and [pistols, shotgun and flares](tr2-lara-arms.md).

| Parameter | Pistols (`1`) | Automatic pistols (`2`) | Uzis (`3`) |
|---|---|---|---|
| Arm / mesh-swap model | `1` | `4` | `5` |
| Damage per qualifying bullet | `1` HP | **`2` HP** | `1` HP |
| Target acquisition range | `< 8192` (strict Euclidean) | `< 8192` (strict Euclidean) | `< 8192` (strict Euclidean) |
| Acquisition cone (yaw / pitch, inclusive) | `±10920` / `±10920` | `±10920` / `±10920` | `±10920` / `±10920` |
| Left-arm retention (yaw / pitch, inclusive) | `−30940..+10920` / `±14560` | `−30940..+10920` / `±14560` | `−30940..+10920` / `±14560` |
| Right-arm retention (yaw / pitch, inclusive) | `−10920..+30940` / `±14560` | `−10920..+30940` / `±14560` | `−10920..+30940` / `±14560` |
| Arm turn rate | `1820` / tick | `1820` / tick | `1820` / tick |
| Spread (angular dispersion) | `1456` (`−364..+363` / axis) | `1456` (`−364..+363` / axis) | `1456` (`−364..+363` / axis) |
| Muzzle height & max wade depth | `650` | `650` | `650` |
| Recoil ticks / steady cadence | `9` (**10 ticks** / hand) | `9` (**10 ticks** / hand) | **`3`** (**4 ticks** / hand) |
| Muzzle flash duration | `3` ticks / hand | `3` ticks / hand | `3` ticks / hand |
| Ammo consumption | Unlimited (resets `1000`, spends `1`) | **1 round per firing hand** | **1 round per firing hand** |
| Firing sound ID | `8` | **`21`** | **`43`** (loop/shot), **`44`** (stop) |
| Draw / holster sound IDs | `6` (draw frame `13`), `7` (holster frame `12`) | `6` / `7` | `6` / `7` |

**Established (static) — paired animation clips, mesh swaps, and facial expression.** All three paired guns drive both arms from model `1`'s four-clip sequence (`218..221` in `venice.TR2`): aim `0..4` (base `+0`), holster reach `5..12` (`+1`), draw/holster with gun `13..23` (`+2`), and recoil `24..32` (`+3`). Only the hand and holster meshes are pulled from the active weapon's model (`1`, `4`, or `5`) at draw frame `13` and restored to holsters at reverse frame `12` per hand. While ready with Action held (and nonzero pistol reserve), Lara's head mesh swaps to model `5`'s head mesh; on holstering or releasing Action it reverts to model `0`'s head mesh. While either hand's flash counter is nonzero, a dynamic point light (intensity `12`, falloff `11`) is emitted at `(Lara.X + sin(yaw)/16, Lara.Y − 512, Lara.Z + cos(yaw)/16)`.

**Established (static) — per-hand ammo order and empty-weapon handoff.** Before updating paired aim when ready with Action held, if the active gun's reserve is `<= 0`, the reserve clamps to `0`, dry-click sound `48` plays at Lara's origin, the requested weapon switches to Pistols (`1`, if owned) or unarmed (`0`), and no shot is fired that tick. Otherwise the right hand evaluates first and the left hand second; each firing hand spends `1` round (so a simultaneous double shot spends `2` rounds). With `1` round remaining, only the right hand discharges; the left hand's firing attempt fails and sets no flash or shot sound, though both hands still enter recoil frame `24`.

**Established (static) — Uzi continuous loop and stop-tail sound behaviour.** Two independent per-hand flags track whether the right or left Uzi is actively firing:
- **On firing (`frame == 4` with Action held, weapon `3`):** if the shot succeeds (ammo spent), plays sound `43` at Lara's origin (the left hand skips sound `43` if the right hand already played it on the same tick) and sets that hand's Uzi-active flag to `1`.
- **During recoil (`frame >= 24`, weapon `3`):** on every recoil tick (`24`, `25`, `26` before wrapping to `4`), each recoiling hand replays sound `43` at Lara's origin and sets its Uzi-active flag to `1`.
- **On stopping:** whenever a hand either reaches `frame == 4` with Action released, OR loses firing permission (`unlocked` and either Action released or a target is selected, including mid-recoil), if that hand's Uzi-active flag is `1`, it plays the stop/echo sound **`44`** (`43 + 1`) at Lara's origin and clears that hand's flag to `0`.

**Established (static) — ring slot ordering and selection.** Main-ring insertion sorts strictly by display priority: `0` Stopwatch (`121`), `1` Pistols (`157`), `2` Shotgun or ammo (`158`/`165`), `3` Automatic Pistols or ammo (`159`/`166`), `4` Uzis or ammo (`160`/`167`), `5` M16 or ammo (`162`/`169`), `6` Grenade Launcher or ammo (`163`/`170`), `7` Harpoon Gun or ammo (`161`/`168`), `8` Flares (`173`), `10` Large Medikit (`172`), `11` Small Medikit (`171`). Key-ring insertion sorts by `101..104` Keys 1–4 (`197..200`), `105..108` Puzzles 4–1 (`181..178`), `110` Pickup 2 (`208`), `111` Pickup 1 (`207`). See [shared inventory order](tr2-triggers-and-interaction.md#pickups) for index retention rules.

### Level 3 (`venice.TR2`, Bartoli's Hideout) declarative script metadata and capacity

| Field | Established `TOMBPC.DAT` / `venice.TR2` value |
|---|---|
| Campaign slot, name, and file | **Established (static).** Level `3`, `"Bartoli's Hideout"`, `data\venice.TR2`. |
| Level music track | **Established (static).** Track `0` (no looping ambient track started at level entry). |
| Special level flags | **Established (static).** **Sunset enabled** (script event `11`). No deadly-water, no-floor, weapon-strip, ammo-strip, or forced start-animation flags. |
| Starting pose and inventory | **Established (static).** Lara (item `97`) starts in room `102` at `(73216, 4864, 19968)`, yaw `0`, co-located with speedboat item `96` (type `14`). Script defines **no** start-class item grants (`>= 1000`); in campaign play Lara carries her complete inventory (owned weapons, reserves, flares, medikits, selected weapon) from Level 2 (`Venice`). Starting level `3` standalone without prior carry guarantees only Pistols (`1000` counter, selected). |
| 3-Dragon secret reward | **Established (static).** Four entries of script item operand `8` (Shotgun Ammunition): adds **48 internal pellet units** (**8 shells**, 4 boxes × 12) to the live shotgun reserve if the Shotgun is owned (queuing four `143` pickup icons), or adds 4 bundles of `165` to the main ring if the Shotgun is not owned. Dragons in `venice.TR2`: Gold item `50` (`190`, room `51`), Silver item `107` (`192`, room `107`), Jade item `125` (`191`, room `131`). |
| Key names and placements | **Established (static).** Key 1: `"Library Key"` (item `27`, type `193`, room `23`, socket `41` type `201` in room `38`). Key 2: `"Detonator Key"` (item `85`, type `194`, room `86`, used in Detonator Box `75` type `227` in room `73`). |
| Weapon and ammo placements in `venice.TR2` | **Established (static).** Loose Automatic Pistols `113` (`137`, room `108`, stacked with flare box `114` and small medikit `115` — converted to `144` on load if already carried from Venice); loose Uzis `126` (`138`, room `139`, first Uzi placement in the campaign); Auto Pistol ammo `87` (gunman `86`, room `86`) and `119` (gunman `118`, room `116`); Uzi ammo `73` (gunman `71`, room `70`) and `90`, `91` (gunman `89`, room `96`); Grenade ammo `51`, `52` (loose stack in room `51`); Shotgun ammo `72`, `108`, `121` (carried by gunmen `70`, `106`, `120`). |
| Post-level transition | **Established (static).** No cutscene; transitions directly to level statistics and Level `4` (`Opera House`, `data\OPERA.TR2`). |
| Carried ring capacity requirements | **Established (static).** Retail allocates **23** slots for the main ring and **23** slots for the key ring. In Bartoli's Hideout, a campaign player can simultaneously hold **9 distinct main-ring entries** — Stopwatch (`121`), Pistols (`157`), Shotgun (`158`), Automatic Pistols (`159`), Uzis (`160`), Grenade Launcher (`163`, from Level 1's secret reward) or Grenade Ammo (`170`, from room `51`), Flares (`173`), Large Medikit (`172`), and Small Medikit (`171`) — plus up to **2 key-ring entries** (`197`, `198`). Any implementation with an 8-slot main ring will overflow in this level; full-campaign maximum is 11 simultaneous main-ring entries. |

---

## 5. Campfire / flame emitter (`type 253`) and flame-on-Lara

This section expands [the campfire baseline](tr2-items-general.md#campfire-slot-100-object-type-253) with exact ignition edge cases, joint attachment, and extinguishing semantics.

| Aspect | Established retail PC rule |
|---|---|
| Emitter lifecycle (`type 253`) | **Established (static).** While triggered and holding no active flame handle, allocates one stationary flame effect (`type 252`) at the emitter's `(X, Y, Z)` and room with sprite frame `0` and cooldown `0`. While untriggered, retires any flame effect it spawned and clears its handle. In `venice.TR2`, items `8..11` (room `6`, fireplace corridor) and `21..22` (room `22`, co-located hearth flames) all carry initial mask `0x3e00` (reverse-start, burning from tick `0`). Room `6` pad trigger `(40, 29)` (timer `6` = `180` ticks) temporarily extinguishes `8..11`, and room `42` tiles `(46..47, 29)` permanently extinguish them. |
| Stationary flame proximity and height check (non-negative state) | **Established (static).** Each tick the flame decrements its sprite frame (wrapping to `0` once `<=` object `252`'s negative mesh count) and plays sound `150` at its origin. If its cooldown is positive, it decrements the cooldown by `1` and returns without testing Lara. At cooldown `0`, it tests the `600`-unit near-Lara rule: `|dx| <= 600`, `|dz| <= 600`, `dx² + dz² <= 360000`, `|dy| <= 3072`, and `top_bound <= dy <= bottom_bound + 100` (all inclusive), where `(dx, dy, dz) = flame position − Lara origin` and `top_bound`/`bottom_bound` are Lara's interpolated animated vertical bounds. Room membership is not required. |
| Proximity damage and ignition threshold | **Established (static).** A passing `600`-unit test subtracts **5 HP** from Lara and sets her hit-status flag. If additionally `dx² + dz² < 202500` (strictly `< 450` horizontally; exactly `450` deals `5` HP without igniting), the stationary flame sets its cooldown to `100` (unconditionally, even if Lara is already burning or the effect pool is full) and invokes single-flame ignition. Single-flame ignition does nothing if Lara's burn flag is already set or if no effect slot is free; otherwise it allocates one `type 252` effect in Lara's room with frame `0`, attachment state `−1`, and sets Lara's burn flag. |
| Igniting while dead or underwater | **Established (static).** **Neither Lara's health nor her water status is checked** by the stationary flame or by single-flame ignition. A dead (`HP <= 0`) or submerged Lara inside the `450`-unit ignition disk still loses `5` HP, sets the emitter flame's cooldown to `100`, attaches a state-`−1` flame effect, and sets the burn flag. However, if that attached flame's computed joint position is strictly below a water surface on the next tick, it extinguishes immediately before dealing burn damage or playing sound `150`. |
| Attached flame-on-Lara tick (negative state `k < 0`) | **Established (static).** Advances its sprite frame as above, then positions itself at Lara's skeleton joint index `−1 − k`: for campfire ignition (`k == −1`), this is **joint `0` (hips/pelvis)** with local offset `(0, 0, −100)`; for multi-flame deadly-floor lava ignition (10 effects with `k` in `−1..−24`), `k == −1` uses local `(0, 0, −100)` and all other negative values use `(0, 0, 0)`. If Lara changed rooms, the flame transfers to Lara's current room. Unless extinguished by the water check below, it plays sound `150` at the flame's computed world position, subtracts **7 HP** from Lara (continuing even when `HP <= 0`), and sets her hit-status flag. |
| Exact extinguishing condition | **Established (static).** Each tick, after updating its joint position and room, an attached flame queries the column's water-surface height at `(flame.X, flame.Y, flame.Z)` in the flame's room. It extinguishes **if and only if** a water surface exists (height `!= −32512`) **and** `flame.Y > water_height` (strictly: the flame's joint Y lies deeper than the water plane). Equality (`flame.Y == water_height`), absence of water, or wading in shallow water where the hip joint remains at or above the water plane does **not** extinguish the flame. On extinguishing, the effect resets its state to `0`, removes itself from the effect pool, clears Lara's burn flag, and skips sound `150` and the `7` HP damage on that tick. |

---

## 6. Floor trigger actions in Tomb Raider II (`op == 8`, `op == 9`, `op == 10`)

### Music / CD track action (`op == 8`)

| Rule | Established retail PC behaviour |
|---|---|
| Valid track range | **Established (static).** Logical track IDs `2..63` inclusive qualify (`1 < track < 64`). Values `0`, `1`, and `>= 64` are ignored. |
| Bitmask and one-shot gating | **Established (static).** Each track `0..63` has a 16-bit flag word (high bits `9..13` store remembered activation bits; low byte `0..7` stores its active countdown). For non-switch triggers (trigger subtype `!= 2`), if any of the trigger's 5 mask bits overlaps bits already remembered for that track, the action returns immediately without playing or advancing a countdown. Otherwise, if the trigger's one-shot bit (`0x100`) is set, its 5 mask bits are ORed into the track's remembered bits. Switch triggers (subtype `2`) skip both the overlap check and one-shot bit recording. A partial mask does not require all 5 bits to play. |
| Immediate versus timed playback | **Established (static).** If the requested track is not the currently pending/active track and the trigger's timer byte (`0..255`) is `0`, playback starts immediately (non-looping). If the timer byte is nonzero, the track becomes the sole pending track and its countdown byte is set to `(timer * 30) & 0xff` (8-bit truncation; e.g., `timer == 128` wraps to `0` and never triggers playback on revisit). When a trigger re-evaluates the already-pending track and its countdown byte is nonzero, the byte decrements by `1`; on the tick it reaches `0`, the pending track ID resets to `−1` and playback starts (non-looping). |
| Ambient loop versus one-shot behaviour | **Established (static).** Floor-triggered `op == 8` actions always start **non-looping** playback, leaving the level's looping ambient track setting unchanged. When a one-shot track finishes, the periodic CD loop poll (checked every `150` ticks when a looping track is configured) restarts the level's looping ambient track. Because `venice.TR2` sets script track `0`, no ambient loop plays there; its `op == 8` triggers (tracks `37`, `45`, `46`, `48`, `49`, `54`, `57`, `63`) are all one-shot cues. |
| Retail PC CD track numbering | **Established (static).** Before issuing playback (when music volume is nonzero), logical track IDs `2..63` map to physical PC CD track numbers starting at physical track `2` by skipping the four virtual indices `{2, 19, 20, 26}`: for each integer `i` from `2` up to `track − 1`, the physical track advances by `1` unless `i` is in `{2, 19, 20, 26}`. |

### FlipEffect / effect action (`op == 9`)

**Established (static) — scheduling and execution order.** An `op == 9` action records its argument as the candidate effect for the trigger list (later `op == 9` entries in the same list overwrite earlier ones). After the trigger action list completes and any room flip is resolved, the global active effect is set to that candidate and its age counter is reset to `0` **only if** either (a) the trigger list contained no flip-related action (`op` `3`, `4`, or `5`), or (b) a flip-related action in the list actually performed a room swap. The active effect runs once per tick during the post-camera sound-effects pass (with no subject item) until the effect clears itself (`−1`).

| `arg` | Behaviour in retail PC release |
|---|---|
| `0` | **Established (static).** Animation-command effect (requires a subject item; not valid as a floor trigger): negates item pitch and adds `32768` (180°) to yaw. Not self-clearing. |
| `1` | **Established (static).** Animation-command effect (requires a subject item; not valid as a floor trigger): if item is within `±16384` of the camera on every axis, sets camera bounce to `100 * (1024² − (dx² + dy² + dz²) / 256) / 1024²`. Not self-clearing. |
| `2` | **Established (static).** Animation-command effect (requires a subject item; not valid as a floor trigger): resets item to standing clip `11` (frame base, state and goal `2`), chase camera, and 80° FOV (`14560`). Not self-clearing. |
| `3` | **Established (static).** Animation-command effect (requires a subject item; not valid as a floor trigger): plays sound `37` and spawns `floor(draw * 3 / 32768)` (`0..2`) bubble effects from joint `14` offset `(0, 0, 50)`. Not self-clearing. |
| `4` | **Established (static).** Sets the level-complete flag to `1`. Does not clear the active effect. |
| `5` | **Established (static).** Water rush (used in Venice lock): while `age <= 120` (**121 ticks**, ages `0..120`), plays sound `79` at `(Lara.X, camera_target.Y + 100 * |age − 30|, Lara.Z)` and increments `age`; when `age > 120`, clears the active effect to `−1` (and increments `age`). |
| **`6`** | **Established (static).** **Chandelier / ceiling-drop cue** (used in `venice.TR2` rooms `30` and `57`, switch `34`, paired with FlipMap slot `0`): plays non-positional sound **`278`**, increments `age`, and clears the active effect to `−1` once `age > 30` (**31 ticks total**, ages `0..30`). Because switch `34`'s trigger includes `(3, 0)`, it runs only when the room swap actually occurs. |
| `7` | **Established (static).** Rubble / collapse (1 tick): plays non-positional sound **`24`**, sets camera bounce to **`−350`**, and clears the active effect to `−1`. |
| `8` | **Established (static).** Piston (1 tick): plays non-positional sound **`190`** and clears the active effect to `−1`. |
| `9` | **Established (static).** Curtain (1 tick): plays non-positional sound **`191`** and clears the active effect to `−1`. |
| `10` | **Established (static).** Set change / stage mechanism (1 tick): plays non-positional sound **`330`** and clears the active effect to `−1`. |
| `11` | **Established (static).** Explosion (1 tick): plays non-positional sound **`105`**, sets camera bounce to **`−75`**, and clears the active effect to `−1`. |
| `12` | **Established (static).** Animation effect: clears Lara's hands-occupied status to free (`0`). Does not clear the active effect if invoked as a floor trigger. |
| `13`, `25`, `27` (reference) | **Established (static).** `13` invokes an immediate room flip; `25` plays non-positional sound **`331`** (1 tick, self-clearing); `27` plays non-positional sound **`338`** (1 tick, self-clearing). |

### Unused secret opcode (`op == 10`) versus clear bodies (`op == 11`)

| Action | Established retail PC behaviour |
|---|---|
| `op == 10` | **Established (static) — inert no-op in Tomb Raider II.** In Tomb Raider I, `op == 10` credited a floor-triggered secret. In Tomb Raider II, secret collection moved entirely to the three dragon pickups (`190`, `191`, `192`), and `op == 10` performs no action in floor-trigger evaluation. Encountering `op == 10` scores no secret, plays no chime, clears no bodies, and does **not** stop evaluation of subsequent actions in the same trigger list. In `venice.TR2`, room `51` tiles `(50, 32)` and `(50, 34)` (`arg == 0`) and room `131` tiles `(35, 32)` and `(37, 32)` (`arg == 3`) are inert authored leftovers with no gameplay effect. |
| `op == 11` | **Established (static) — Clear Bodies.** Moved from Tomb Raider I's slot to `op == 11`. Removes dead creatures whose placement bit `15` marked them as cleanup-eligible (and does not remove unmarked corpses or dropped pickups). Not authored in `venice.TR2`. |

---

## Scope limits and open edge cases

- **Static PC evidence only:** All numeric bounds, frame timings, sound IDs, damage values, and level/script records above are established statically from the pinned retail PC executable, `venice.TR2`, and `TOMBPC.DAT`. No live playthrough capture of Bartoli's Hideout, save/load round-trip mid-detonator or mid-burn, or PlayStation parity check was performed.
- **Lava multi-flame joint indices above `14`:** Deadly-floor lava spawns 10 flames with attachment states drawn uniformly over `−1..−24` (joints `0..23`), whereas Lara's model has `15` meshes (`0..14`); runtime behaviour of joint indices `15..23` in the joint-transform walker was not examined beyond campfire ignition (state `−1`, joint `0`).
- **Uzi flags on mid-fire holster or weapon switch:** Clearing of the two per-hand Uzi-active flags outside the paired-gun firing update (e.g. if Lara holsters or dies during recoil) was not checked against a live audio capture.
