# Tomb Raider III against Tomb Raider II: floor data, Lara movement, Jungle inventory, and India campaign delta
Reference source checked in place: TRX (`<remote-ssh>:~/src/TRX`), commit `cf5bbaaa8eff9a29c036896b6a7975c58cae635a`.
Authored retail PC data cross-checked (verified by WallCheck6): `JUNGLE.TR2` (`165` rooms, SHA-256 `e72190e3c011f5d76cc596d0806cc78f70fbce5e27468edfcfa9bbf5befd17ee`), `TEMPLE.TR2` (`224` rooms, SHA-256 `5404a0b39afebf3a61fa54949d1d63190740d81e44500a43fc98164bcb69aabe`), `QUADCHAS.TR2` (`177` rooms, SHA-256 `ebf3c22af417ee643afd2e1bca2bed9a7c3c3b6a1634eb31bce81f0deec616ca`), `TONYBOSS.TR2` (`25` rooms, SHA-256 `c6c025833652c9c0a869ea0439375164f03ed6aff8c6a628184b1e352fe26fe0`), `HOUSE.TR2` (`133` rooms, SHA-256 `51f165edfaa8b8808a5cc8919704dcae4266ee58430c7850cc2aa10953e8c508`), and `TOMBPC.DAT` (`5550` bytes, version `3`, SHA-256 `1191d732e1e3beaa8bee15c7cc16d8b5e019e2ecb15cf0cacc11a1b121f5f632`).
Files read: `data/trx/ship/games/tr3/{catalog_lara_anims.csv,catalog_lara_states.csv,catalog_music.csv,catalog_objects.csv,gameflow.json5,strings.json5,weapons.json5}`, `data/trx/ship/games/tr3/scripts/{_game,antarc,jungle,mines,quadchas,temple,tonyboss}.lua`, `tools/tr3/objects_tracker/objects_dump.json`, `tools/tr2/read_tombpc_script`, `src/trx/config/map_tr3.def`, `src/trx/game/{rules.def,rooms/{enum,types}.h,rooms/{floor_data,geometry}.c,rooms/triggers/{camera,flip,general,item,music}.c,level/format/format_tr3.c,level/sections/rooms.c,level/finalize/gameplay_objects.c,level/settings.def,collision/common.c,lara/{breath,col,common,const,control,hair,misc,poison}.c,lara/state/{crouch,land,monkey}.c,lara/col/{climb,crouch,jump,land,monkey}.c,items/{anim,enum}.h,items/anim.c,items/manager.c,gun/{control,flare,pistols,rifle}.c,creature/{behavior,common}.c,objects/creatures/{monkey,tiger}.c,objects/effects/flame.c,objects/general/{ai_node,bird_tweeter,door,flare_item,pickup,save_crystal,shoal,smoke_emitter,switch,trapdoor,waterfall_mist,zipline}.c,objects/projectiles/{dart,grenade,harpoon_bolt,rocket}.c,objects/traps/{dart_emitter,falling_block,flame_emitter,pendulum,rolling_ball,spike_ceiling,spike_wall,spikes}.c,objects/traps/common.h,objects/types.h,fx/weather.c,output/sky.c,game_flow/inventory.c,savegame/{resume,vars}.c,stats/common.c}`.
Reader: WallReaderTR3. Date: 2026-11-24.
Evidence: Static behavioural rules derived from the pinned in-place reference source (filtering out optional modern enhancements and Tomb Raider IV+ additions to isolate original retail PC Tomb Raider III behaviour) and cross-checked against the pinned retail PC India `.TR2` files and `TOMBPC.DAT`. No retail Windows executable was disassembled and no live gameplay recording was performed.

Distances are in game units (`1024` per tile, `256` per height click); Y increases downward. Angles use `65536` units per full turn (`182` per degree, `16384` per quarter-turn). Gameplay logic runs at `30` ticks per second. The impassable / absent height sentinel is `−32512`. All differences are stated against the Tomb Raider II baseline already specified in [triggers and interactions](tr2-triggers-and-interaction.md), [climbing](tr2-climbing.md), [ground movement and firearms](tr2-lara-ground-and-weapons.md), [water and flares](tr2-water-and-flares.md), [pistols, shotgun and flares](tr2-lara-arms.md), [title, death and save flow](tr2-death-save-and-finish.md), and [Opera House objects](tr2-opera-house.md). Features unchanged from Tomb Raider II are noted as unchanged rather than repeated.

---

## 1. Floor Data, Level Geometry, and Room Flags Delta

### 1.1 Level container and sector layout deltas

| Aspect | Tomb Raider III delta vs Tomb Raider II |
|---|---|
| Level version word | **Established (static).** Retail Tomb Raider III `.TR2` level files begin with 32-bit version word `0xFF080038` or `0xFF180038` (replacing Tomb Raider II's version `45`). The sound sample lookup table expands to `370` entries. |
| Sector packed box word | **Established (static).** Each sector's 16-bit box word is bit-packed in Tomb Raider III: bits `0..3` hold a 4-bit floor material / footstep effect index; bits `4..14` hold an 11-bit pathing box index (`0x7FF` means no box); bit `15` (`0x8000`) holds the creature stopper flag. |
| Room vertices | **Established (static).** Room vertices carry a 16-bit vertex flags word (`0x2000` wind-sway vertex motion, `0x4000` caustic/glow shimmer, `0x8000` disable underwater wibble) and a 16-bit ARGB1555 vertex colour (replacing Tomb Raider II's monochrome light-table value). |
| Room lights | **Established (static).** Room lights are 24 bytes each: position `(X, Y, Z)` (three 32-bit signed integers), RGB colour (three unsigned bytes), and a 1-byte light type (`0` = point light with 32-bit intensity and 32-bit falloff; nonzero = directional sun light with three 16-bit direction components plus 2 padding bytes). |
| Room trailing fields | **Established (static).** Following the 16-bit room flags word, each room ends with a 1-byte water lighting/ripple scheme, a 1-byte acoustic reverb preset, and `1` unused padding byte. |

### 1.2 Split-triangle / diagonal floor and ceiling slopes (`0x07..0x12`)

Tomb Raider III adds 12 two-word floor-data opcodes (`0x07..0x12`) that split a sector's floor or ceiling along a diagonal into two independent planar triangles, optionally leaving one triangular half open as a vertical portal into the room below or above while the other half remains solid.

#### Opcode table

| Floor-data opcode | Surface | Diagonal axis | Solid / portal partition |
|---|---|---|---|
| `0x07` (`7`) | Floor | NW–SE | Both halves solid |
| `0x08` (`8`) | Floor | NE–SW | Both halves solid |
| `0x09` (`9`) | Ceiling | NW–SE | Both halves solid |
| `0x0A` (`10`) | Ceiling | NE–SW | Both halves solid |
| `0x0B` (`11`) | Floor | NW–SE | SW half (`dx <= 1024 − dz`) is pit portal; NE half (`dx > 1024 − dz`) is solid |
| `0x0C` (`12`) | Floor | NW–SE | NE half (`dx > 1024 − dz`) is pit portal; SW half (`dx <= 1024 − dz`) is solid |
| `0x0D` (`13`) | Floor | NE–SW | NW half (`dx <= dz`) is pit portal; SE half (`dx > dz`) is solid |
| `0x0E` (`14`) | Floor | NE–SW | SE half (`dx > dz`) is pit portal; NW half (`dx <= dz`) is solid |
| `0x0F` (`15`) | Ceiling | NW–SE | SW half (`dx <= 1024 − dz`) is sky portal; NE half (`dx > 1024 − dz`) is solid |
| `0x10` (`16`) | Ceiling | NW–SE | NE half (`dx > 1024 − dz`) is sky portal; SW half (`dx <= 1024 − dz`) is solid |
| `0x11` (`17`) | Ceiling | NE–SW | NW half (`dx <= dz`) is sky portal; SE half (`dx > dz`) is solid |
| `0x12` (`18`) | Ceiling | NE–SW | SE half (`dx > dz`) is sky portal; NW half (`dx <= dz`) is solid |

#### Two-word encoding and 4-corner height formulas

**Established (static).** Every split-triangle floor-data entry consists of two 16-bit words:

1. **First word (header):**
   - Bits `0..4`: opcode (`0x07..0x12`).
   - Bits `5..9`: 5-bit signed click offset `split1` (bit `4` / `0x10` sign-extended to range `−16..+15`), scaled to units as `h1 = split1 × 256`.
   - Bits `10..14`: 5-bit signed click offset `split2` (same 5-bit sign extension, `−16..+15`), scaled to units as `h2 = split2 × 256`.
   - Bit `15` (`0x8000`): standard end-of-floordata flag.
2. **Second word (corner heights):**
   - Packed as four 4-bit unsigned nibbles `t0` (bits `0..3`), `t1` (bits `4..7`), `t2` (bits `8..11`), and `t3` (bits `12..15`).
   - For **ceiling** splits (`0x09, 0x0A, 0x0F..0x12`), all four nibbles are negated (`−t0, −t1, −t2, −t3`).

Let `dx = x mod 1024` and `dz = z mod 1024` (`0..1023` within the sector). The active triangular half picks a base vertical shift (`h1` or `h2`) and an effective planar tilt `(tx, tz)`:

| Diagonal family | Active half condition | Base shift | Floor `(tx, tz)` | Ceiling `(tx, tz)` (using negated `t0..t3`) |
|---|---|---|---|---|
| NW–SE (`0x07, 0x09, 0x0B, 0x0C, 0x0F, 0x10`) | `dx > 1024 − dz` | `h1` | `(t3 − t2, t3 − t0)` | `(t0 − t1, t3 − t0)` |
| NW–SE (`0x07, 0x09, 0x0B, 0x0C, 0x0F, 0x10`) | `dx <= 1024 − dz` | `h2` | `(t0 − t1, t2 − t1)` | `(t3 − t2, t2 − t1)` |
| NE–SW (`0x08, 0x0A, 0x0D, 0x0E, 0x11, 0x12`) | `dx > dz` | `h1` | `(t0 − t1, t3 − t0)` | `(t3 − t2, t3 − t0)` |
| NE–SW (`0x08, 0x0A, 0x0D, 0x0E, 0x11, 0x12`) | `dx <= dz` | `h2` | `(t3 − t2, t2 − t1)` | `(t0 − t1, t2 − t1)` |

From `(tx, tz)`, the surface height at `(x, z)` is `sector_base_height + base_shift + tilt_shift`, using the standard planar tilt contribution:
- **Floor:** add `−((tz × dz) >> 2)` when `tz < 0`, else `+((tz × (1023 − dz)) >> 2)`; then add `−((tx × dx) >> 2)` when `tx < 0`, else `+((tx × (1023 − dx)) >> 2)`.
- **Ceiling:** add `+((tz × dz) >> 2)` when `tz < 0`, else `−((tz × (1023 − dz)) >> 2)`; then add `+((tx × (1023 − dx)) >> 2)` when `tx < 0`, else `−((tx × dx) >> 2)`.

#### Solid-half vs portal-half traversal rule (`0x0B..0x12`)

**Established (static).** Compass quadrants follow `+Z` = North and `+X` = East within each `1024 × 1024` sector: along NW–SE, SW is `dx <= 1024 − dz` and NE is `dx > 1024 − dz`; along NE–SW, NW is `dx <= dz` and SE is `dx > dz`. When resolving pit or sky vertical portals (including room containment and water-surface height lookups), a half-portal sector blocks traversal into the vertical neighbour on its solid half and traverses into the neighbour on its open portal half:

| Opcode | Surface | Condition where vertical portal is **blocked (solid)** | Condition where vertical portal is **open** |
|---|---|---|---|
| `0x0B` / `0x0F` | Floor / Ceiling (NW–SE, SW open) | `dx > 1024 − dz` (NE half) | `dx <= 1024 − dz` (SW half) |
| `0x0C` / `0x10` | Floor / Ceiling (NW–SE, NE open) | `dx <= 1024 − dz` (SW half) | `dx > 1024 − dz` (NE half) |
| `0x0D` | Floor (NE–SW, NW open) | `dx > dz` (SE half) | `dx <= dz` (NW half) |
| `0x0E` | Floor (NE–SW, SE open) | `dx <= dz` (NW half) | `dx > dz` (SE half) |
| `0x11` | Ceiling (NE–SW, NW open) | `dx > dz` (SE half) | `dx <= dz` (NW half) |
| `0x12` | Ceiling (NE–SW, SE open) | `dx <= dz` (NW half) | `dx > dz` (SE half) |

#### Slope classification, sliding, and collision rules on split triangles

| Aspect | Established retail PC behaviour |
|---|---|
| Surface height classification | **Established (static).** Evaluating a split floor marks the surface as gentle split triangle (class `4`) when `|tx| <= 2` and `|tz| <= 2`, or steep diagonal slope (class `3`) when `|tx| > 2` or `|tz| > 2`. Walkable objects (bridges, trapdoors, blocks) stacked above a split floor override the surface class back to flat/wall (class `0`). |
| Sliding direction and threshold | **Established (static).** Slope sliding reads the active triangle's `(tx, tz)` with the same `> 2`-click steepness threshold as unsplit slopes (selecting cardinal slide yaw from the dominant axis, with Z overriding X whenever `|tz| > 2` and `|tz| > |tx|`). Sliding is suppressed inside swamp rooms. |
| Steep-slope wall/pit rejection and doubled front probe | **Established (static).** Steep diagonal triangles (class `3`) obey the same wall-blocking and pit-blocking rules as steep unsplit slopes (class `2`). In addition, Tomb Raider III re-samples the front collision probe at double the front offset: a steep slope ahead is rejected as an impassable wall only when the front sample is higher than the centre floor **and** the doubled-distance sample is higher still than the front sample. |
| Diagonal push-out rollback | **Established (static).** When a front collision fails on a surface of class `3` (steep diagonal) or class `4` (split triangle), or a lateral collision fails on class `4`, collision rollback restores Lara's full previous `(X, Z)` position rather than snapping to a cardinal tile boundary. |
| Chunky camera height | **Established (static).** During chunky-camera checks, a split floor evaluates to `min(base + h1, base + h2)` and a split ceiling evaluates to `max(base + h1, base + h2)`. |

### 1.3 Monkey-swing, climbable walls, minecart tracks, and trigger deltas

| Floor-data element | Encoding / ID | Tomb Raider III delta vs Tomb Raider II |
|---|---|---|
| Climbable walls | Opcode `0x06` (`6`), bits `8..11` | **Established (static).** Unchanged from Tomb Raider II (`bit 0` North, `bit 1` East, `bit 2` South, `bit 3` West). |
| Monkey-swing ceiling | Opcode `0x13` (`19`), 1 word | **Established (static).** New in Tomb Raider III. Sets `bit 4` (`0x10`) in the sector's climbable bitmask, marking the overhead ceiling as monkey-swingable. Checked on the bottommost pit sector at `(X, Z)`. |
| Minecart left / right / stop | Opcodes `0x14` (`20`) and `0x15` (`21`), 1 word each | **Established (static).** New in Tomb Raider III. `0x14` marks a minecart left-turn track; `0x15` marks a right-turn track; authoring both on one sector marks a minecart stop. **Important delta:** `0x14` is *not* a trigger-triggerer and `0x15` is *not* a clockwork-beetle marker in Tomb Raider III (those meanings were introduced in Tomb Raider IV). |
| Trigger subtype `10` (Heavy Switch) | Subtype `10` | **Established (static).** New in Tomb Raider III. Evaluated only for non-Lara ("heavy") items; passes only when the heavy item's nonzero trigger mask equals the trigger's 5-bit mask, toggling target items with that mask. |
| Trigger subtype `11` (Heavy Antitrigger) | Subtype `11` | **Established (static).** New in Tomb Raider III. Evaluated only for heavy items; antitriggers listed targets. |
| Trigger subtype `12` (Monkey) | Subtype `12` | **Established (static).** New in Tomb Raider III. Passes for Lara only while her current state is a monkey-swing state (`75, 76, 77, 78, 79, 82, 83`). |
| One-shot switch lockout | Subtype `2` + one-shot bit (`0x100`) | **Established (static).** Changed in Tomb Raider III. When a non-creature switch sits on a trigger carrying the one-shot bit, pulling it permanently locks out any further player interaction with that switch. |
| Trigger action `2` (Sink / Water Current) | Action `2` | **Established (static).** Changed in Tomb Raider III. Records `1 + sink_index` directly (replacing Tomb Raider I/II's box-pathing drag), feeding the smooth 2D velocity current described in Section 2.6. |
| Trigger action `10` (Secret) | Action `10` | **Established (static).** Changed vs Tomb Raider II (restored from Tomb Raider I). Operand `0..N−1` selects secret bit `1 << operand`; when newly discovered, marks that bit in the level's secret mask, increments the secret total, and plays music track `122` once. Replaces Tomb Raider II's three dragon pickups. |
| Trigger action `11` (Clear Bodies) | Action `11` | **Established (static).** Unchanged from Tomb Raider II (`tr2-opera-house.md`): permanently removes dead corpses whose placed flags carried bit `15` (`0x8000`). |
| Trigger action `7` (Finish Level) | Action `7` | **Established (static).** Marks the current level complete and advances per the campaign script. |

### 1.4 Room flags delta

| Bit mask | Flag meaning | Tomb Raider III behaviour |
|---|---|---|
| `0x0001` | Underwater | **Established (static).** Unchanged from Tomb Raider II (submerged swimming, surface lookup, underwater caustic/wibble). |
| `0x0008` | Outside / Sky | **Established (static).** Enables skybox (`355`) drawing through sky openings and enables precipitation (rain/snow) spawning and rendering in levels with active weather. |
| `0x0010` | Unused in retail PC levels | **Established (static, cross-checked against all 22 retail PC `.TR2` files).** Reserved room flag bit; does not appear on any room across the 22 retail PC levels. |
| `0x0020` | Wind / Foliage | **Established (static).** New in Tomb Raider III. Drives wind sway on room vertices carrying move flag `0x2000`, blows Lara's hair/braid (`2`), and applies wind drift to sparks, water droplets, and debris. |
| `0x0040` | Inside | **Established (static).** Paired with outside portal visibility culling. |
| `0x0080` | Quicksand / Swamp | **Established (static).** New in Tomb Raider III. Converts the room into sinking quicksand/swamp (Section 2.5): treated as a liquid surface for height queries, forbids underwater swimming, halves wading speed, sinks Lara `+2` units/tick, and suffocates her past `775` units depth. |
| `0x0100` / Cold & freezing water | Level-wide Antarctic property (not room flag `0x0100`) | **Established (static, cross-checked against retail PC `.TR2` files).** Freezing/damaging water exposure and visible cold breath are level-wide properties of Antarctica (`ANTARC.TR2`) and RX-Tech Mines (`MINES.TR2`) rather than driven by room bit `0x0100` (in retail PC data, bit `0x0100` appears on 4 dry rooms in `JUNGLE.TR2` — Rooms `30, 48, 54, 87` — and on zero underwater rooms in `ANTARC.TR2` or `MINES.TR2`). Both cold breath and exposure drain are inactive in `JUNGLE.TR2`. |

---

## 2. Lara Movement, Combat, and Status Delta (vs Tomb Raider II)

### 2.1 New retail Tomb Raider III Lara states and animation clips

**Established (static).** Original retail Tomb Raider III extends Tomb Raider II's Lara state table (`0..70`) with states `71..88` and animation table (`0..216`) with clips `217..312`. Higher IDs (`89+` / `313+`) belong to Tomb Raider IV or optional modern additions and are **not** part of retail Tomb Raider III.

| State ID | Name | Retail TR3 clips | Retail TR3 availability / notes |
|---:|---|---|---|
| `71` | Crouch Idle | `217, 221, 222, 245, 291, 293..296, 304..307, 310..312` | Active in retail TR3. Turning in place while crouched is **not** available in retail TR3 (Lara must enter crawl `80` to turn). |
| `72` | Crouch Roll | `218..220` | Clips exist in data, but crouch-rolling is **disabled** in original retail TR3. |
| `73` | Sprint / Dash | `223..229, 243..244` | Active in retail TR3. |
| `74` | Sprint Roll | `230, 232, 308..309` | Active in retail TR3 (triggered by Jump during Sprint). |
| `75` | Monkey Idle | `233..235, 237..238` | Active in retail TR3. Also serves as the thin-ledge slow-swing settle state. |
| `76` | Monkey Forward | `236, 239, 252` | Active in retail TR3. |
| `77` | Monkey Shimmy Left | `253..254` | Active in retail TR3. |
| `78` | Monkey Shimmy Right | `255..256` | Active in retail TR3. |
| `79` | Monkey Turn Around | `257` | Disabled in original retail TR3 (optional modern alternative-turn feature). |
| `80` | Crawl Idle | `258..259, 263..264, 273..274, 301` | Active in retail TR3 (crawl pickup clip `292` is disabled in original retail TR3). |
| `81` | Crawl Forward | `260..262, 266..268` | Active in retail TR3. |
| `82` | Monkey Turn Left | `271, 283..284` | Active in retail TR3. |
| `83` | Monkey Turn Right | `272, 285..286` | Active in retail TR3. |
| `84` | Crawl Turn Left | `269, 281` | Active in retail TR3. |
| `85` | Crawl Turn Right | `270, 282` | Active in retail TR3. |
| `86` | Crawl Backward | `275..280` | Active in retail TR3. |
| `87` | Climb to Crawl | `287..288` | Active in retail TR3 (pulling up from hang or ladder into a 2-click crawlspace). |
| `88` | Crawl to Climb | `289..290, 302` | Active in retail TR3 (backing out of a crawlspace to hang from the ledge). |

### 2.2 Sprint / Dash (`states 73, 74`)

| Aspect | Established retail PC behaviour |
|---|---|
| Stamina capacity, drain, and recovery | **Established (static).** Maximum sprint stamina is **`120` ticks** (`4.0` s at 30 Hz). While Lara is in state `73` (Sprint), stamina decreases by **`1` per tick**. Whenever she is in any state other than `73`, stamina increases by **`1` per tick** up to `120`. |
| Retail entry gate | **Established (static).** From state `1` (Run) while holding Sprint, Lara requests state `73` **only when stamina is completely full (`120`)** and she is not wading. Once stamina drops below `120` and sprinting stops, she cannot start sprinting again until stamina refills to `120`. |
| Steering and lean | **Established (static).** Turn rate accelerates by `±409` (`±2.25°`) per tick up to **`±728` (`±4°`)** per tick (half of Run's `±1456` / `±8°` cap). Lateral roll lean changes by `±273` (`±1.5°`) per tick up to `±16°` (`2912` angle units). |
| Transitions out of Sprint | **Established (static).** (a) Health `<= 0`, stamina `<= 0`, entering wade status, or releasing Sprint requests Run (`1`). (b) Pressing Jump while grounded requests Sprint Roll (`74`, clips `230/232` left or `308/309` right). (c) Pressing Roll immediately enters ground Roll (`45`). (d) Pressing Crouch (hands free or non-rifle weapon) in original retail TR3 first requests Stop (`2`) rather than jumping straight to Crouch Idle (`71`). (e) Holding Walk with Forward requests Walk (`0`); releasing directional inputs requests Stop (`2`). |
| Sprint collision and step-up interruption | **Established (static).** Collision matches Run (max step-up `384`, ceiling limit `0`, steep slopes act as walls). Deflecting off a wall `256` ahead at height `−640` switches directly into wall-smash-left. Stepping up onto a ledge between `129` and `384` units above her feet interrupts Sprint into run-up-step-left (relative frames `4..13`) or run-up-step-right (other frames). Small downward steps clamp to `+50` units/tick; drops `> 384` initiate a fall. |
| Sprint Roll (`74`) collision & landing | **Established (static).** Uses max step-up `256`, ceiling limit `192`, steep slopes as walls. If downward vertical speed exceeds `131` while not targeting Death, Stop, or Run, transitions to Fast Fall (`9`). On landing (floor `<= 0` with downward speed `> 0`), runs the standard landing-damage check: lethal impact enters Death (`8`); safe landing enters Run (`1`) if Forward is held (without Walk or Wade), else Stop (`2`), clearing gravity and velocity. |

### 2.3 Crouch, Duck, and Crawl (`states 71, 80, 81, 84..88`)

| Aspect | Established retail PC behaviour |
|---|---|
| Collision dimensions & overhead lock | **Established (static).** Crouched and crawling collision height is **`400`** (vs standing `762`). Horizontal radius is **`200`** in crouch, crawl-idle, and crawl-forward, and **`250`** in crawl-backward. Overhead clearance probes relative to Lara's crouched head (`ceiling_Y − (Lara.Y − 400)`) and engages the forced-crouch latch whenever that value is `>= −362` (equivalently `ceiling_Y − Lara.Y >= −762`, meaning total floor-to-ceiling headroom is `< 762`, too low to stand). |
| Entering Crouch (`71`) | **Established (static).** From Stop (`2`), holding Crouch while not wading, not on a vehicle, and either hands free or holding a **non-rifle** weapon (Pistols, Desert Eagle, Uzis, or Flare) requests Crouch Idle (`71`). Two-handed rifle-class weapons (Shotgun, MP5, Rocket Launcher, Grenade Launcher, Harpoon Gun) cannot be drawn while crouched and prevent entering crouch. |
| Standing up from Crouch | **Established (static).** In Crouch Idle (`71`, clip `222`), releasing Crouch (or entering wade depth) while the forced-crouch latch is clear requests Stop (`2`, clip `221` crouch-to-stand). If health drops to `<= 0` while crouched, transitions to Crawl Idle (`80`) and plays Crawl Death (`clip 301`). |
| Crouch pickups & flare pickup | **Established (static).** From Crouch Idle (`71`) with hands free, pressing Action on a floor pickup plays crouch pickup (`clip 291`), acquiring the item on relative frame `20` or `22`; picking up a lit floor flare plays crouch flare pickup (`clip 312`), acquiring on relative frame `22`. Original retail TR3 does **not** permit picking up items from Crawl Idle (`80`). |
| Entering and leaving Crawl (`80`) | **Established (static).** From Crouch Idle (`71`, relative frame `> 1`) with hands free, pressing Forward or Back while crouching (or under low ceiling) zeroes torso rotation and requests Crawl Idle (`80`, clips `258 -> 273 -> 264 -> 263`). Starting clip `258` marks hands occupied so weapons cannot be drawn while crawling. From Crawl Idle (`80`), releasing Crouch when not under a low ceiling, or pressing Draw Weapon, requests Crouch Idle (`71`, clips `259 -> 274`). Camera elevation during crawl is **`−23°` (`−4186`)**. |
| Crawl Forward (`81`), Backward (`86`), and Turns (`84, 85`) | **Established (static).** Crawl vertical step tolerance is `±255` units with ceiling clearance `400`, and steep slopes act as **both** walls and pits. From Crawl Idle (`clip 263` or `264`): (a) Forward enters `81` if floor `256` ahead is within `±254` and not a steep slope; (b) Back enters `86` if the rear ceiling probe `300` units behind at height `128` (`ceiling_Y − (Lara.Y − 128)`) is not `−32512` and is `<= 256` (i.e., `ceiling_Y <= Lara.Y + 128`, confirming no wall behind) and the floor `300` units behind is within `±254` and not a steep slope; (c) Left or Right enters stationary Crawl Turn `84`/`85` (`clips 269/270`), rotating **`±273` (`±1.5°`)** per tick. While crawling forward or backward, steering accelerates turn rate by `±409` (`±2.25°`) capped at **`±546` (`±3°`)** (left/right steering inverted when crawling backward). Front obstacles stop Lara back into Crawl Idle (`80`); grazing left/right edges deflects yaw by `±364` (`±2°`). |
| Pull-up into crawlspace (`87`, Climb to Crawl) | **Established (static).** From Ledge Hang (`10`), thin-ledge Monkey Idle (`75`), or Ladder Climb (`56/57`), holding Forward or Crouch when the ledge ahead lies between `−850` and `−650` vertically and has headroom between `512` (2 clicks) and `761` across centre, left, and right probes (with no blocking static object) enters state `87` (`clips 287 -> 288`), arriving in Crouch Idle (`71`). |
| Drop out of crawlspace (`88`, Crawl to Climb) | **Established (static).** From Crawl Idle (`80`, clips `263` or `264`), holding **Back + Action** when the rear ceiling probe `300` units behind at height `128` (`ceiling_Y − (Lara.Y − 128)`) is valid and `<= 256` (`ceiling_Y <= Lara.Y + 128`), floor `300` behind drops **`> 768`** (`3` clicks), and no static object blocks a `50`-radius probe `512` behind: snaps Lara's yaw to the nearest cardinal direction (`0°, 90°, 180°, −90°`) and aligns her `225` units inside the sector edge (aborting if the snapped floor violates `±255` or steep slope), then requests state `88` (`clips 289 -> 290 -> 302`). Upon reaching clip `302`, sets downward speed `512`, aligns Y to the bottom of the current click (`Y | 255`), probes at height `870` (step-up `384`, ceiling `192`), and grips the ledge (`100`-unit XZ inset) into hang or swing-in — or falls into Jump Up (`28`, relative frame `9`, horizontal speed `2`, fall speed `1`, hands free) if the edge cannot be caught. |

### 2.4 Monkey-Swing (`states 75..78, 82, 83`)

| Aspect | Established retail PC behaviour |
|---|---|
| Grabbing the ceiling | **Established (static).** While airborne in Jump Up (`28`) or Reach (`11`), holding Action with hands free and no static hit, if collision hits a ceiling (top or front-top) and the bottom pit sector at `(X, Z)` carries the monkey-swing ceiling flag (`0x13`), Lara grabs the overhead surface: plays clip `233` (from Jump Up) or slow swing-in (from Reach), enters state `75` (Monkey Idle), clears gravity and velocity, marks hands occupied, and snaps **`Y = ceiling + 704`**. |
| Stamina | **Established (static).** Monkey-swinging consumes **no stamina** in retail Tomb Raider III; Lara can hang and traverse indefinitely while Action stays held. |
| Movement, steering, and camera | **Established (static).** Monkey collision uses radius `100`, height `600`, and ceiling step tolerance **`50` units** (`|centre_ceiling − front_ceiling| < 50`). From Monkey Idle (`75`): (a) Forward enters `76` (Monkey Forward) if front is clear and ceiling step `< 50`; steering while moving forward accelerates turn rate by `±409` (`±2.25°`) capped at **`±546` (`±3°`)**. (b) Step Left / Step Right enters lateral shimmy `77` / `78` (`move heading = yaw ± 90°`) when side probes pass ceiling-step and octant wall checks. (c) Left / Right enters stationary Monkey Turn `82` / `83`, rotating **`±273` (`±1.5°`)** per tick. Every tick re-snaps `Y = ceiling + 704`. Moving monkey states set camera elevation **`+10°` (`+1820`)**; idle hanging sets **`−60°` (`−10920`)**. |
| Dropping off | **Established (static).** Releasing Action, dying (`HP <= 0`), or leaving a monkey-swing sector drops Lara into Jump Up (`28`, relative frame `9`) with gravity enabled, horizontal speed `2`, fall speed `1`, `Y += 256`, and hands free. |

### 2.5 Quicksand / Swamp (`room flag 0x80`)

| Aspect | Established retail PC behaviour |
|---|---|
| Entry & water-status lock | **Established (static).** Liquid-surface queries include swamp rooms alongside underwater rooms. However, Lara **never** enters Underwater or Surface swim in a swamp room: once feet pass `256` units below the surface, she enters **Wade** status regardless of total depth. Jumping or falling into a swamp forces the wade clip (state `65`); entering via Swan Dive (`52`) or Fast Dive (`53`) immediately plunges her to **`Y = surface_Y + 1000`**. |
| Grounded wading speed & airborne drag | **Established (static).** While wading grounded in a swamp room, animation horizontal velocity is halved (`base_velocity / 2 + acceleration × relative_frame / 4`, scaled by `>> 16`). While airborne with gravity inside a swamp room, horizontal speed decays by `1/8` per tick (stopping and clearing gravity below `8`), and downward speed halves if `> 128`, then decays by `1/4` per tick, clamped to at least **`4`** units/tick. |
| Sink rate & climbing out | **Established (static).** While wading in a swamp room with the solid floor still at or below her feet (`floor_offset >= 0`), Lara sinks vertically at **`+2` units per tick**. Wading onto a shallower floor or rising edge (`floor_offset < 0`) lifts her upward normally so she can walk out onto dry land or vault an adjacent low ledge (`<= 384` units) from Stop or Wade. |
| Restricted actions in swamp | **Established (static).** Inside swamp rooms: wading turn speed caps at slow turn (`±728` / `±4°`) and lean halves (`±1001`); fast turn is disabled; jumping from wade is disabled; slope sliding is disabled; sidestepping is disabled in original retail PC Tomb Raider III; environment-filtered footstep/wade animation sounds are muted. |
| Suffocation / drowning | **Established (static).** Once Lara sinks deeper than **`775` units** below the swamp surface, her air drains at **`−6` per tick** (6× faster than underwater). When air becomes negative, it clamps to `−1` and she loses **`10` HP per tick** (2× underwater drowning) until death. Shallower than `775` units, air recovers at Tomb Raider III's normal `+10`/tick. |

### 2.6 Poison, Cold Water / Exposure, and Other Movement / Status Deltas

| Subsystem | Tomb Raider III delta vs Tomb Raider II |
|---|---|
| Poison status | **Established (static).** New in Tomb Raider III. Poison attacks add to Lara's integer poison level (e.g., a Poison Dart `90` adds **`+160`**). Whenever `poison >= 16` and `(game_ticks × 4) mod 256 == 0` (every **`64` logic ticks**, ~`2.13` s), poison clamps to at most **`256`** and Lara takes **`poison >> 4`** health damage (up to **`16` HP** per pulse; `10` HP/pulse from one poison dart) without stumble. Using a Small or Large Medipack, or touching a retail PC healing Save Crystal (`180`), resets poison to `0`. Retail Tomb Raider III has **no** poison viewport wobble (that is Tomb Raider IV). |
| Cold water / exposure bar | **Established (static).** New in Tomb Raider III (active in Antarctica and RX-Tech Mines; inactive in Jungle). Warmth capacity is **`600` ticks** (`20.0` s). In Antarctica (`ANTARC.TR2`) and RX-Tech Mines (`MINES.TR2`), **all underwater rooms (`0x0001`)** are freezing/damaging and all rooms enable visible cold breath; **dry land rooms do not drain warmth**. Inside a freezing underwater room, warmth drains **`−2`/tick** while swimming underwater or treading at the surface, and **`−1`/tick** while wading (or grounded inside a freezing room); outside freezing rooms (including dry land), warmth recovers **`+1`/tick** up to `600`. While warmth `< 0` (clamped at `−1`), Lara loses **`10` HP/tick**. In cold levels, Lara emits visible breath puffs from her head joint while above water (after relative frame `>= 30` in Stop or Crouch Idle, or during `game_ticks mod 64` in `32..48` otherwise). |
| Wade depth & land air recovery | **Established (static).** Dry-to-wade depth threshold lowers from **`384`** (1.5 clicks in Tomb Raider II) to **`256`** (1 click in Tomb Raider III). On land or wading (outside deep swamp), air recovers at **`+10` per tick** up to `1800` instead of resetting instantaneously to `1800`. Wading and shallow water landings emit splash ripples. |
| Water current physics (Sink action `2`) | **Established (static).** Replaces Tomb Raider I/II's box-routed displacement with smooth velocity blending. On a sink trigger (`active = 1 + sink_index`), target horizontal velocity toward the sink has magnitude `sink_strength << 10` (10 fractional bits); horizontal velocity blends each tick by `vel += (target_vel − vel) >> 4` and vertical position blends by `Y += (sink_Y − Y) >> 4`. Off a sink trigger, each horizontal velocity axis decays by `vel -= vel >> shift` (`shift = 4` if `|vel| > 16`, `3` if `> 8`, `2` otherwise), zeroing below `4`. Each tick applies `X += vel_X >> 8` and `Z += vel_Z >> 8` prior to underwater collision. |
| Sidestep & walk-back pre-checks | **Established (static).** Entering Sidestep (`21`/`22`) pre-tests floor and ceiling `148` units laterally (requiring ceiling `<= 0`, `|floor| < 128`, and not a steep slope), removing Tomb Raider I/II's edge-nudge behaviour. Walk Back (`16`) and Hop Back (`5`) from Stop (`2`) pre-test floor `140` units behind (`floor > −383`, and `< +383` non-steep for Walk Back). |
| Ladder hang shift | **Established (static).** Vertical hang/ladder shift step doubles from `2` (Tomb Raider II) to **`4`** units in Tomb Raider III, and hang/ladder top pull-ups support entering 2-click crawlspaces (`state 87`). |
| Death floor / Lava (`0x05`) | **Established (static).** Touching a death floor attaches standard single-flame ignition (`7` HP/tick burning) rather than Tomb Raider I/II's 10-flame instant burst. |
| Flares (`10, 178, 179`) | **Established (static).** Burn duration halves from `60` s (`1800` ticks in Tomb Raider II) to **`30` s (`900` ticks)** in Tomb Raider III, flickering during the final `3.0` s (`90` ticks) and dying over the last `0.8` s (`24` ticks). Casts jittered reddish-orange dynamic RGB light, emits ignition sparks on lighting, and can be picked up while crouched (`clip 312`, frame `22`). |
| Braid (`type 2`) vs Articulated Skin Joints (`type 315`) | **Established (static).** Object **`2`** remains Lara's 6-segment physics-driven braid (now swayed by room wind flag `0x20`). Object **`315`** is **not** her hair — it is Tomb Raider III's new articulated skin-joint mesh object bridging gaps between limb segments. |

---

## 3. Level 1 (`JUNGLE.TR2`): Complete Object Inventory and Behavioural Rules

### 3.1 Complete `JUNGLE.TR2` object & placement inventory

**Established (static, cross-checked against retail `JUNGLE.TR2`).** `JUNGLE.TR2` (`165` rooms) loads **`95`** moveable object model slots and places **`142`** item instances across **`33`** unique placed object types. *(Exact per-room static mesh IDs and floor-data trigger tile coordinates are authored directly in `JUNGLE.TR2`.)*

#### All 33 placed item types in `JUNGLE.TR2` (`142` instances total)

| Type ID | Public object name | Count | Rooms containing placements | Category / TR3 delta summary |
|---:|---|---:|---|---|
| `0` | Lara | `1` | `4` (start at `(28160, −256, 28160)`) | Player start |
| `28` | Tiger | `9` | `28, 43, 46, 68, 70, 113 (×2), 122, 127` | Creature — **changed** vs TR2 (Section 3.4) |
| `71` | Monkey | `10` | `14, 20, 45, 54, 62, 72, 77, 132, 147, 164` | Creature — **new** in TR3 (Section 3.2) |
| `75` | AI Ambush | `3` | `1, 50, 164` | AI marker — **new** in TR3 (Section 3.3) |
| `76` | AI Patrol 1 | `4` | `51, 54, 72, 90` | AI marker — **new** in TR3 (Section 3.3) |
| `77` | AI Modify | `1` | `132` (co-located with Monkey for Indra Key) | AI marker — **new** in TR3 (Section 3.3) |
| `79` | AI Patrol 2 | `2` | `51, 72` | AI marker — **new** in TR3 (Section 3.3) |
| `87` | Stationary Spikes | `19` | `16 (×4), 23 (×6), 70, 87, 88 (×2), 99, 161, 162 (×3)` | Trap — unchanged vs TR2 |
| `88` | Rolling Ball 1 (Boulder) | `6` | `17, 47, 50 (×3), 77` | Trap — **changed** stop probe vs TR2 (Section 3.7) |
| `97` | Pushable Block 1 | `1` | `106` | Mover — unchanged vs TR2 |
| `114` | Spike Wall | `1` | `149` | Trap — unchanged vs TR2 (`16` units/tick, `20` HP) |
| `127` | Zip Line Handle | `2` | `149, 163` | Mover — TR2/TR3 baseline (Section 3.7) |
| `129` | Wall Switch (Normal) | `11` | `1, 41, 45, 77, 86, 101, 103, 109 (×2), 117, 149` | Interactive — **changed** one-shot lockout (Section 1.3) |
| `131` | Door 1 | `6` | `0, 7, 45, 106 (×2), 149` | Mover — unchanged vs TR2 |
| `138` | Door 8 | `10` | `47, 62 (×4), 82, 102 (×2), 116 (×2)` | Mover — unchanged vs TR2 (standard door) |
| `139` | Trapdoor 1 | `1` | `106` | Mover — unchanged vs TR2 |
| `161` | Shotgun (world pickup) | `1` | `21` | Weapon pickup (Section 3.8) |
| `169` | Shotgun Ammo | `7` | `17, 24, 29, 39, 48, 122, 141` | Ammo pickup (Section 3.8) |
| `170` | Desert Eagle Ammo | `2` | `70 (×2)` | Ammo pickup — **new** in TR3 (Section 3.8) |
| `171` | Uzi Ammo | `4` | `6, 21, 48, 151` | Ammo pickup — **changed** quantity (Section 3.8) |
| `172` | Harpoon Ammo | `1` | `143` | Ammo pickup — **changed** stats (Section 3.8) |
| `173` | MP5 Ammo | `3` | `67, 72, 122` | Ammo pickup — **new** in TR3 (Section 3.8) |
| `174` | Rocket Ammo | `1` | `143` | Ammo pickup — **new** in TR3 (Section 3.8) |
| `175` | Grenade Ammo | `1` | `141` | Ammo pickup — **changed** stats (Section 3.8) |
| `176` | Small Medipack | `5` | `14, 42, 80, 147, 157` | Consumable (also targeted by Monkeys) |
| `177` | Large Medipack | `1` | `17` | Consumable |
| `178` | Flare Box | `6` | `62, 72, 99, 106, 141, 164` | Consumable (`6` flares, `30` s burn) |
| `180` | Save / Health Crystal | `6` | `14, 29, 61, 98, 99, 134` | Pickup/Heal — **new** TR3 behaviour (Section 3.6) |
| `227` | Key 4 (`Indra Key`) | `1` | `132` (targeted by `AI Modify` Monkey) | Key pickup |
| `235` | Keyhole 4 | `1` | `82` | Key receptacle — unchanged vs TR2 |
| `304` | Camera Target | `8` | `14, 27, 106 (×4), 107, 153` | Camera focus point |
| `331` | Small Flame Emitter | `6` | `106 (×2), 109 (×4)` | Decorative flame — **new** in TR3 (Section 3.7) |
| `338` | Piranhas (Shoal) | `1` | `158` (item index `133`) | Water swarm — **new** in TR3 (Section 3.5) |

#### Remaining 62 loaded (non-placed) models in `JUNGLE.TR2`

**Established (static).** Loaded for player equipment, projectiles, inventory rings, swaps, and sky:
- Lara weapon and gear animation models: `1` (Pistols), `2` (Hair/Braid), `3` (Shotgun), `4` (Desert Eagle), `5` (Uzis), `6` (MP5), `7` (Rocket Launcher), `8` (Grenade Launcher), `9` (Harpoon Gun), `10` (Flare), `315` (Articulated Skin Joints).
- Support and effect models: `24` (Kill-All Triggered), `74` (AI Guard), `78` (AI Follow), `123` (Mesh Swap 2 — monkey holding medipack), `124` (Mesh Swap 3 — monkey holding Indra Key), `130` (Underwater Lever), `300` (Gun Flash), `301` (MP5/M16 Flash), `309` (Rocket projectile), `310` (Harpoon Bolt projectile), `311` (Grenade projectile), `355` (Skybox), `366` (Brass Gun Shell), `367` (Shotgun Shell).
- World pickup models loaded for carry/spawn: `160, 162..168, 179` (weapons, ammo, and loose flare).
- Inventory ring models: `145` (Passport), `146` (Stopwatch), `158` (Closed Passport), `159` (PDA/Statistics), `181` (Graphics), `182` (Sound), `183` (Controls), `185..204` (Weapons, Ammo, Medipacks, Flares, Save Crystal options), `231` (`Indra Key` option).

### 3.2 Monkey (`type 71`)

| Aspect | Established retail PC behaviour |
|---|---|
| Base statistics & pathing | **Established (static).** Starting health **`8` HP**, collision radius **`102`**, pivot length `0`, shadow size `128`, climber pathing (climbs up and drops down ledges of `2`, `3`, or `4` clicks via states `15..20` and model clips `17..22`). Initializes sitting (state `6`, clip `2`). |
| States (`0..20`) | **Established (static).** `1` Stop, `2` Walk (max turn `7°`), `3` Stand, `4` Run (max turn `11°`, bank tilt `turn / 2`), `5` Pickup, `6` Sit, `7` Eat, `8` Scratch, `9` Roll, `10` Angry, `11` Death (clip `14`), `12` Attack Low, `13` Attack High, `14` Attack Jump, `15..17` Climb Up 4/3/2 clicks, `18..20` Climb Down 4/3/2 clicks. |
| Friendly / Ally aggro rule in Jungle | **Established (static).** In `JUNGLE.TR2`, Monkeys start as **allies** and do not attack Lara unprovoked. Hurting **any** Monkey permanently flips the level-wide ally-hostility flag to hostile (persisted in saves), immediately alerting all Monkeys against Lara and overriding their patrol markers. |
| Melee attacks & retail double-check quirk | **Established (static).** Contact mask `0x2400` (paws/jaws); blood offset `(10, 10, 11)` on mesh `13`. From Stand (`3`): Low Attack (`12`, when Lara's Y `>=` Monkey's Y) or High Attack (`13`, when Lara's Y `<` Monkey's Y) within **`341` units** (`1/3` tile) dealing **`40` HP**; Jump Attack (`14`) within **`682` units** (`2/3` tile) dealing **`50` HP**; Roll (`9`) within **`1024` units**. Each swing connects at most once (hit latch cleared in Stand/Sit). *Retail quirk:* During High Attack (`13`), if the `40` HP hit check misses on a tick, the `50` HP Jump Attack hit check is also evaluated on that same tick. Against non-Lara creatures within `±256` per axis, deals half damage (`20` or `25` HP). |
| Vehicle fear | **Established (static).** Whenever Lara is mounted on any vehicle, Monkeys force escape mood. |
| Pickup theft (`Small Medipack 176` & `Indra Key 227`) | **Established (static).** While carrying nothing and not actively held by Guard, Patrol, Ambush, or Follow: (a) a Monkey whose AI bitmask equals `Modify` (`0x08`, e.g., Room `132`) seeks an unguarded, visible **`Indra Key` (`227`)** in its pathing zone and renders via Mesh Swap 3 (`124`, mesh mask `0xFFFF6F6F` empty / `0xFFFF6E6F` carrying); (b) any other Monkey seeks an unguarded, visible **`Small Medipack` (`176`)** in its pathing zone and renders via Mesh Swap 2 (`123`, mesh mask `0xFFFFFFFF` empty / `0xFFFFFEFF` carrying). *Retail quirk:* even after being shot by Lara, a retail Monkey continues prioritizing an eligible pickup in its zone over chasing Lara. |
| Carrying, dropping at Ambush, and death drop | **Established (static).** Within `682` units horizontally and `< 256` vertically of its target pickup (or an Ambush marker), the Monkey enters Pickup (`5`). On **frame `12`** of Pickup: (1) if targeting a pickup, attaches it as a carried item, removes it from its room, clears any other creature targeting it, and adds `Ambush | Modify` to its own AI bitmask (if not already `Modify`) so it flees to an `AI Ambush` (`75`) marker in the same zone (ignoring yaw tag); (2) on frame `12` at an `AI Ambush` marker, drops the carried item at the Monkey's position and room, clears the Monkey's AI bitmask to `0`, and stamps `Guard` on the dropped pickup so no Monkey steals it again. Killing a Monkey while carrying releases the stolen item at its corpse via standard carried-item retirement. |

### 3.3 TR3 AI Waypoint System (`types 74..82`)

**Established (static).** Tomb Raider III uses invisible AI marker objects placed in rooms to direct creature guard posts, patrols, ambushes, and special roles:

| Type ID | Marker name | AI bitmask | Role |
|---:|---|---:|---|
| `74` | AI Guard | `0x01` | Creature holds position, scans its head randomly (`−90° / 0° / +90°`, disabled if `Modify` is also set), and breaks guard once alerted (adding `Modify` if `Ambush` is also set). |
| `75` | AI Ambush | `0x02` | Once `Modify` is set or Lara hurts the creature, it runs to a distant `75` marker in the same zone whose yaw matches the creature's AI yaw tag (Monkeys ignore the yaw tag). Non-Monkeys arriving within **`768` units** (`3` clicks) per axis fire heavy triggers under the marker, set goal-reached, clear `Ambush | Modify`, switch to `Guard`, and clear alerted status. |
| `76` | AI Patrol 1 | `0x04` | Creature patrols toward a distant `76` marker (same zone, matching yaw tag); arriving within **`768` units** per axis fires heavy triggers under the marker and alternates to Patrol 2 (`79`). Alerting or hurting the creature clears `Patrol 1`. |
| `77` | AI Modify | `0x08` | Alters creature rules (for Monkeys, targets `Key 4` with Mesh Swap 3; suppresses Guard head-scanning; enables Ambush pursuit). |
| `78` | AI Follow | `0x10` | Creature moves to a distant `78` marker (same zone, matching yaw tag); arriving within `768` units sets goal-reached and clears `Follow`. |
| `79` | AI Patrol 2 | — | Alternate patrol endpoint paired with `76` by matching yaw tag in the same zone. |
| `80..82` | AI X1 / X2 / X3 | — | Auxiliary AI markers (not placed in `JUNGLE.TR2`). |

**Level-load binding rule (`Established (static)`):** At level load, for every intelligent creature, any AI marker (`74..78`) in the **same room** at the **exact same `(X, Z)`** adds its bit to the creature's AI bitmask, copies the marker's yaw into the creature's AI yaw tag, and removes that co-located marker from its room so only distant markers remain active waypoints.

### 3.4 Tiger (`type 28`) — Delta vs Tomb Raider II

| Aspect | Tomb Raider II baseline | Tomb Raider III delta |
|---|---|---|
| Starting health | `20` HP | **Established (static).** **`24` HP**. |
| Bite damage (Attacks 1, 2, 3) | `100` HP | **Established (static).** **`90` HP** (contact mask `0x07FD C000`, bite joint `26` offset `(19, −13, 3)`). |
| Attack cone admission | Broad ahead cone (`±90°`) | **Established (static).** Tight bite cone (narrow heading and height check; general creature bite vertical tolerance also widened from `256` to `512` in TR3). |
| Unreachable zone escape | Continues normal mood | **Established (static).** When alerted and Lara is in a different pathing zone, switches to escape mood. In Stop or Run under escape mood, if Lara is **not** targeting the Tiger and Lara is ahead, the Tiger pauses in Stop (`1`) instead of running away. |
| Walk → Run transition | Stays in Walk unless roaring | **Established (static).** Immediately requests Run (`3`) from Walk (`2`) whenever in attack or escape mood. |
| Unchanged parameters | — | **Established (static).** Radius `341`, pivot `200`, shadow `128`, walk turn `3°`, run turn `6°`, Attack 1 range `341`, Attack 3 range `1024`, running pounce Attack 2 range `1536` (only when Lara is moving), roar chance `96 / 32768`. |

### 3.5 Piranhas (`type 338`) and Tropical Fish (`type 339`)

| Aspect | Established retail PC behaviour |
|---|---|
| Swarm composition, rendering, & targeting | **Established (static, cross-checked against retail `JUNGLE.TR2`).** Untargetable by Lara's weapons. Each placed shoal simulates `1` invisible leader plus **`24` swarm fish** (`25` total). In retail PC `.TR2` files, `338` (Piranhas) and `339` (Tropical Fish) are 1-mesh moveable models (`5` vertices, `5` faces in `JUNGLE.TR2`; there are no separate fish sprite sequences in retail `.TR2` data). Each swimming fish renders procedurally as a single textured triangle oriented along its swimming-wibble heading (nose at `(x, y, z)` and two tail corners behind at `y ± size`, where `size = ((128 × sin(index << 10)) >> 14) + 192`), textured from the object's model textures with alternating UV orientation by fish index (`338`) or shoal index (`339`). Activates on trigger; antitrigger deactivates. |
| Swimming bounds (`JUNGLE.TR2` placement) | **Established (static).** Each shoal's 3D swim box is engine-defined per level/shoal (not stored in the `.TR2` item placement fields) and specified in **quarter-tiles (`256`-unit clicks)**: horizontal half-extents `±(range_X × 256)` and `±(range_Z × 256)` around the anchor, and vertical depth `0..(range_Y × 256)` below the anchor. In `JUNGLE.TR2`, the single Piranha shoal (item `133`, Room `158`) has range **`{X: 10, Y: 3, Z: 22}` quarter-tiles** (`±2560` units X, `0..768` units Y, `±5632` units Z). Tropical Fish (`339`) are decorative and never attack. |
| Attack trigger & carcass distraction | **Established (static).** Piranhas (`338`) attack whenever Lara enters the shoal's anchor room. If a **`Carcass` (`type 38`)** item is present in the anchor room (used in later levels, not in Jungle), the swarm prioritizes and attacks the Carcass instead of Lara. |
| Damage & blood cadence | **Established (static).** While attacking Lara, every individual fish (of `24`) within horizontal circular distance **`256`** and vertical distance **`<= 256`** of Lara deals **`4` HP per tick** (rapidly lethal once the swarm converges). A blood burst spawns at the biting fish on an `8`-tick cooldown. |

### 3.6 Save / Health Crystal (`type 180` world, `204` inventory)

| Aspect | Retail PC behaviour | Retail PlayStation behaviour |
|---|---|---|
| Visual hover, spin, and light | **Established (static).** Placed active in TR3 levels (`6` crystals in `JUNGLE.TR2`: Rooms `14, 29, 61, 98, 99, 134`). Spins `+1024` yaw units (`5.625°`) per tick. Hovers and bobs vertically around its placed Y: `Y = Y0 − |cos((game_ticks & 63) << 10) >> 6| − 64`. Casts a pulsing **green** (`RGB(0, 255, 0)` scaled by cosine amplitude) dynamic light with falloff `8`. | **Established (static).** Same spin and bob; crystal mesh is tinted **blue** and casts a pulsing blue (`RGB(0, 64, 255)`) dynamic light with falloff `8`. |
| Acquisition bounds | **Established (static).** Auto-collected on proximity without pressing Action whenever `|dx| < 256`, `|dy| < 1024`, and `|dz| < 256` relative to Lara. | **Established (static).** Same proximity bounds (`|dx| < 256, |dy| < 1024, |dz| < 256`). |
| Gameplay effect | **Established (static).** Restores **`+500` HP** (capped at `1000`), **immediately cures Poison**, plays the Medipack sound, increments the crystals statistic, and destroys the crystal. Saving on PC is unrestricted via the Passport at any time. | **Established (static).** Adds **`+1` Save Crystal** to inventory (Lara also starts Jungle with `1` crystal on PSX), plays the Save Crystal chime, and destroys the world crystal; saving consumes one crystal (plus free save offers between levels). |

### 3.7 Jungle Traps, Movers, Emitters, Weather, and Skybox

| Object / System | Type ID(s) | Placed in `JUNGLE.TR2`? | Tomb Raider III delta vs Tomb Raider II |
|---|---|---|---|
| Rolling Ball / Boulder | `88` (Ball 1), `89` (Ball 4), `112` (Ball 2), `126` (Ball 3) | Yes (`6×` type `88`: Rooms `17, 47, 50 ×3, 77`) | **Established (static).** Forward stop-probe distance for type `88` changes from `384` (TR2) to **`320` units** (`5/4` clicks) in TR3; type `89` uses `1088` (`17/16` tiles); types `112, 126` use `1024`. Camera shakes within `10240` units; crushes grounded Lara instantly (boulder death clip) or deals `100` HP when clipping airborne Lara. |
| Stationary Spikes | `87` | Yes (`19×`) | **Established (static).** Unchanged from TR2 (`15` HP/tick at speed `>= 30`; instant impalement if falling with downward speed `> 6`). |
| Spike Wall | `114` | Yes (`1×`, Room `149`) | **Established (static).** Unchanged from TR2: advances `16` units/tick along yaw until floor height changes; `20` HP/tick + 3 blood particles on touch; resets if untriggered. |
| Ceiling Spikes | `116` | No (later levels) | **Established (static).** Descends `5` units/tick, stops on a pushable block or floor within `1024`, deals `20` HP/tick on touch. |
| Falling Block (Collapsing Floor) | `83, 84, 85` | No (later India/TR3 levels) | **Established (static).** Animated blocks match TR2 (state `0` idle → state `1` triggered/shaking → state `2` falling → state `3` settled). Blocks without animation data use TR3 simulated shattering: stepping on the tile starts a **`60`-tick** (`2.0` s) countdown (`2` ticks if heavy-triggered), jitters pitch/roll randomly `(rand & 0x3FF) − 0x200`, then shatters into debris and destroys itself. |
| Poison Dart Emitter & Poison Dart | `91` (Emitter), `90` (Dart) | No (present in `TEMPLE.TR2`+) | **Established (static).** New in TR3. Emitter `91` is invisible; while triggered it fires every **`24` ticks** (`0.8` s), spawning Dart `90` at `Y − 512`, offset `±512` flush to the wall along its facing axis with yaw flipped `+180°`, speed **`256` units/tick**, plus 5 smoke puffs and blowpipe sound. Dart renders as a 96-unit brown line segment; hitting Lara deals **`25` HP** and adds **`+160` poison** before destroying itself; hitting solid geometry spawns 4 smoke puffs and destroys on the next tick. |
| Pushable Blocks | `97..100` | Yes (`1×` type `97`, Room `106`) | **Established (static).** Unchanged from TR2 pushable-block baseline. |
| Switches & Underwater Lever | `129` (Normal), `130` (UW), `117` (Wheel), `118` (Small), `128` (Button) | Yes (`11×` type `129`; `130` loaded only) | **Established (static).** Same interaction bounds and animations as TR2, plus TR3 one-shot lockout (Section 1.3). |
| Doors & Trapdoors | `131..138` (Doors), `139..141` (Trapdoors) | Yes (`6×` `131`, `10×` `138`, `1×` `139`) | **Established (static).** Unchanged from TR2 baselines (type `138` uses standard door behaviour). |
| Key & Puzzle Slots | `224..227` / `232..235` (Keys), `205..208` / `213..220` (Puzzles) | Yes (`1×` `227` Indra Key, `1×` `235` Keyhole 4) | **Established (static).** Unchanged from TR2 baselines. |
| Zip Line Handle | `127` | Yes (`2×`: Rooms `149, 163`) | **Established (static).** Grab from Stop (`2`) with hands free and Action held inside `X ±256, Y ±100, Z +256..+512, yaw ±25°`; aligns to `(0, 0, 371)`, plays clips `214 -> 215` (state `70`), accelerates **`+5`/tick** up to max speed **`100`** (descending `speed >> 2` vertically per tick), stops when a probe `1024` ahead hits floor or ceiling within `±256` or when Action is released (launching Lara into Jump Forward `3` with horizontal speed `= handle speed`, downward speed `= handle speed >> 2`), and resets to its initial position when untriggered. |
| Swinging Pendulums | `86, 121` | No (later levels) | **Established (static).** `50` HP contact damage; flaming pendulums pair with Big Flame Emitter (`330`) and emit fire sparks plus dynamic RGB light (falloff `11`). |
| Small Flame Emitter | `331` (plus `330` Big, `332` Jet, `333` Side) | Yes (`6×` `331`: Rooms `106 ×2, 109 ×4`) | **Established (static).** New in TR3. Spawns flame effect (`329`) with sparks, flickering RGB light (falloff `12` for small, `16` for big), and fire loop sound. **Crucial rule:** Small Flame (`331`) is purely decorative and **never damages or ignites Lara** (unlike Big `330`, Jet `332`, and Side `333`, which deal `5` HP within `600` and ignite within `450` horizontally). |
| Rain Weather | Level weather setting (`rain`) | Yes (`JUNGLE.TR2` & `QUADCHAS.TR2`) | **Established (static).** New in TR3. Spawns falling raindrops in rooms carrying the Outside flag (`0x08`) within `4096..6000` units around Lara/camera (base vertical speed `16 + (rand & 7)`, lifetime `88` ticks), creating splash ripples upon hitting water or floor. |
| Waterfall Mist | `305` | No (later levels) | **Established (static).** Changed in TR3: when triggered and within `10240` units of Lara, emits mist sparks every `4` ticks offset `544` units along yaw and plays waterfall loop sound. |
| Smoke / Steam Emitters | `334` (White), `335` (Black), `336` (Steam) | No (later levels) | **Established (static).** New in TR3. Invisible emitters active within `16` tiles; spawn rising scaled sprite puffs every `4` ticks (`8` ticks for Steam `336`, with halved vertical velocity/gravity and lower brightness). |
| Bird Tweeters | `286` (Drips), `289` (Birds) | No (later levels) | **Established (static).** New in TR3. Invisible ambient sound emitters: `289` plays bird chirp at probability `1024 / 32768` per tick; `286` plays reverb water drips at `256 / 32768`. |
| Skybox | `355` | Loaded (drawn in `0x08` rooms) | **Established (static).** Unfogged sky dome drawn through sky openings in outside rooms with flat neutral ambient lighting (`0.5` intensity, no directional lights). |

### 3.8 TR3 Weapons and Pickups Delta (Complete Roster + `JUNGLE.TR2` Placements)

**Established (static).** Tomb Raider III replaces Tomb Raider II's Automatic Pistols with the single-handed **Desert Eagle**, replaces the M16 with the **MP5**, adds the **Rocket Launcher**, and rebalances Uzis, Harpoon Gun, and Grenade Launcher.

| Weapon | Anim / World / Inv / Ammo / InvAmmo / Proj IDs | Damage | Rounds per pickup | Aim lock / track / range | Cadence / Projectile delta vs Tomb Raider II | Placed in `JUNGLE.TR2` |
|---|---|---:|---:|---|---|---|
| **Pistols** | `1 / 160 / 185 / 168 / 193 / —` | `1` | Infinite (`32`) | Lock `±60°`, L `−170..+60°`, R `−60..+170°`, pitch `±80°`, range `8192` | Unchanged (recoil `9`, `±8°` spread, usable crouched). | Starting weapon |
| **Shotgun** | `3 / 161 / 186 / 169 / 194 / —` | `3 × 6` pellets | `2` shells (`12` pellet units) | Lock `±60°/±55°`, track `±80°/±65°`, range `8192` | Unchanged (`20°` pellet scatter, recoil `9`, ejects shell on frame `12`, cannot use crouched). | `1×` gun (Room `21`), `7×` ammo |
| **Desert Eagle** | `4 / 162 / 187 / 170 / 195 / —` | **`21`** | **`10`** | Lock `±60°`, single-hand `±10°` yaw / `±80°` pitch, range `8192` | **New (replaces Auto Pistols).** Single-handed (right hand only), high accuracy (`±4°`), slower cadence (recoil `16`), usable crouched. | `2×` ammo (Room `70`) |
| **Uzis** | `5 / 163 / 188 / 171 / 196 / —` | `1` | **`40`** (was `80`) | Same as Pistols, range `8192` | Pickup halved to `40` rounds (recoil `3`, alternating fire sound, usable crouched). | `4×` ammo |
| **MP5** | `6 / 165 / 190 / 173 / 198 / —` | **`3`** | **`60`** (was `40`) | Lock `±60°/±55°`, track `±80°/±65°`, range **`12288`** | **New (replaces M16).** Rapid fire (`3` HP per shot, `±4°` spread, draw frame `16`, undraw `21`); when unlocked, adds Lara's torso rotation to aim angles. Cannot use crouched. | `3×` ammo |
| **Harpoon Gun** | `9 / 164 / 189 / 172 / 197 / 310` | **`6`** (was `4`) | `3` | Lock `±60°/±65°`, track `±80°/±75°`, range `8192` | **Changed.** Usable underwater. Bolt speed **`256`** in air / **`128`** underwater (was `150`); rolls `+35°`/tick; pitches down `−1°`/tick in air (to `−90°`); on hitting floor/wall/ceiling, sticks in place for `256` ticks, wobbling sinusoidally during the first `64` ticks before disappearing. Reloads every 4 shots. | `1×` ammo (Room `143`) |
| **Grenade Launcher** | `8 / 167 / 192 / 175 / 200 / 311` | **`20`** (was `30`) | `2` | Same as Shotgun, range `8192` | **Changed.** Launches at speed **`128`** (was `200`) with a **`120`-tick** (`4.0` s) timed fuse and bouncing/rolling collision off walls and slopes (`+3` gravity/tick in air; damped underwater). Blast radius doubled to **`1024`** (`1` tile, was `512`). Explodes on direct creature hit or fuse expiry, shattering eligible enemies. | `1×` ammo (Room `141`) |
| **Rocket Launcher** | `7 / 166 / 191 / 174 / 199 / 309` | **`30`** | **`1`** | Same as Shotgun, range `8192` | **New in TR3.** Launches at speed `16` and accelerates each tick by `+(speed >> 2) + 4` up to **`512`** in air or **`128`** underwater, spinning with flame/smoke trail and dynamic RGB light (falloff `14`). Explodes on geometry or target impact with **`1024`** blast radius, shattering eligible enemies and heavy smashables. | `1×` ammo (Room `143`) |

---

## 4. Campaign Script (`TOMBPC.DAT`) and India Progression Delta

### 4.1 `TOMBPC.DAT` format and campaign architecture deltas vs Tomb Raider II

| Aspect | Tomb Raider III delta vs Tomb Raider II |
|---|---|
| Script container format | **Established (static, verified in retail PC `TOMBPC.DAT`).** Uses the same compiled script container format as Tomb Raider II: format version **`3`**, `5550` bytes, global flags **`0x0300`** (string obfuscation enabled, Lara's House enabled), XOR obfuscation key **`0xa6`**, title music track **`5`**, script secret sound **`0`** (so the secret chime track `122` is triggered engine-side rather than from the script header), **`21`** levels (`0..20`), **`7`** FMVs (`0..6`), and **`11`** cutscenes (`0..10`, mapping in order to `cut6, cut9, cut1, cut4, cut2, cut5, cut11, cut7, cut8, cut3, cut12`; there is no `cut10`). The front-end startup sequence plays `LOGO.RPL` (FMV `2`) and `INTR_ENG.RPL` (FMV `3`). |
| Globe Select hub | **Established (static).** New in Tomb Raider III. Completing India (`Caves Of Kaliya`, Level `4`) opens an interactive 3D Globe Select screen offering free choice of order among the next three regions: **South Pacific Islands** (Levels `5..8`, starting at `SHORE.TR2`), **London** (Levels `9..12`, starting at `ROOFS.TR2`), and **Nevada** (Levels `13..15`, starting at `NEVADA.TR2`). Completing all four regions unlocks **Antarctica** (Levels `16..19`, starting at `ANTARC.TR2`). |
| Variable secret counts & All Hallows bonus | **Established (static).** Unlike Tomb Raider II's fixed 3 secrets per level, Tomb Raider III levels contain variable counts of floor-triggered secrets (`action 10`, engine-side chime track `122`). Collecting all campaign secrets unlocks bonus Level `20`, **All Hallows** (`STPAUL.TR2`), after completing Level `19` (`CHAMBER.TR2`). |
| Inventory ring additions | **Established (static).** Adds the 4 Artifact/Quest slots (`240..243` world, `244..247` inventory: `Infada Stone`, `Element 115`, `Eye of Isis`, `Ora Dagger`), replaces TR2's Auto Pistols/M16 slots with Desert Eagle/MP5, adds Rocket Launcher, and (on PSX) Save Crystal inventory slot (`204`). *Exclusion:* Binoculars (`431`) are a modern/Tomb Raider IV addition and are **not** present in original retail Tomb Raider III. |

### 4.2 Exact India level sequence, starting stock, and secret counts

| Level ID | Displayed title | Level filename | Ambient track | Weather | Pre/Post media & progression | Authored keys / puzzles / artifacts | Verified retail secrets |
|---:|---|---|---:|---|---|---|---:|
| `0` | Lara's House | `HOUSE.TR2` | `2` | None | Training course → Title | `Racetrack Key` (`224`) | **`0`** *(Established, retail `HOUSE.TR2`)* |
| `1` | Jungle | `JUNGLE.TR2` | `34` | **Rain** | Pre: none (intro FMVs `2, 3` run in front-end); Post: cutscene track `64`, cut angle `16384`, Cutscene `0` (`cuts\cut6.TR2`) → Level Complete (Stats track `14`) → Level `2` | `Indra Key` (`227`, Key 4) | **`6`** *(Established, retail `JUNGLE.TR2`)* |
| `2` | Temple Ruins | `TEMPLE.TR2` | `34` | None | Post: cutscene track `69`, cut angle `16384`, Cutscene `1` (`cuts\cut9.TR2`) → Level Complete (Stats track `14`) → Level `3` | `Key of Ganesha` (`224`), `Scimitar` (`205, 206`) | **`4`** *(Established, retail `TEMPLE.TR2`)* |
| `3` | The River Ganges | `QUADCHAS.TR2` | `34` | **Rain** | Post: Level Complete (Stats track `14`) → Level `4` | `Gate Key` (`224`) | **`5`** *(Established, retail `QUADCHAS.TR2`)* |
| `4` | Caves Of Kaliya | `TONYBOSS.TR2` | `30` | None | Completes immediately upon picking up `Infada Stone` (`240`, Quest 1) → Post: FMV `4` (`FMV\SAIL_ENG.RPL`) → Level Complete (Stats track `14`) → **Globe Select** | `Infada Stone` (`240`, Quest 1) | **`0`** *(Established, retail `TONYBOSS.TR2`)* |

### 4.3 Starting stock and inter-level carry rules

| Rule | Established retail PC behaviour |
|---|---|
| New Game starting stock (`JUNGLE.TR2`) | **Established (static).** Lara starts Level `1` (`Jungle`) with full health (`1000`), **Pistols** (infinite ammo), **`1×` Small Medipack**, **`1×` Large Medipack**, and **`2×` Flares** (on retail PSX, she also starts with **`1×` Save Crystal**). No Shotgun is granted at start (unlike Tomb Raider II's Great Wall; the Shotgun is placed in Room `21`). |
| Carried across levels | **Established (static).** All owned weapons, all ammunition counts, Small and Large Medipacks, Flares, Save Crystals (PSX mode), and collected Artifacts/Quest Items (`240..243`, until removed upon entering RX-Tech Mines Level `17`) carry forward from level `N` to the next level. |
| Reset between levels | **Established (static).** Health resets to `1000`, poison resets to `0`, and all level-local Keys (`224..227`), Puzzles (`205..208`), and Pickups (`236..237`) are cleared at level completion (not carried into the next level). |
