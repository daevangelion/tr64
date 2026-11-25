# M10: Great Wall items, world and starting inventory (host only)

Measured baseline `94a56a0d0dedce67db5cecd870cade7188323309`; frozen
patch base `cd81271b1c667287f66c46935adf559a766be02e`, after merging
the reviewed wall answers and pose-tail update. Six routes were
published since the initial 13-route baseline.
All fixtures are under `$TR64_WORK/fixtures/oracle-wall/tr2/`.
The cook input is the unchanged Great Wall pack (`wall.pack`, SHA-256
`d5e1cb90646be0bdb94545e369eca0c118a5684a68cbcc9e2c454de73a59a02e`).
Evidence and exact route commands: `$TR64_WORK/evidence/m10/items/`.
The comparator validates each manifest, the record stream and its bound
availability footer, then compares with the caller-pinned producer. The
retail records locate and verify differences; they supply no rules.

## Before: dry Lara, untouched world

A fresh run at base, with `--game-tr2` and `--producer tr64-tr2-dry`.
Each cell gives equal records including tick 0. `—` means no comparable
state, not equality. The results for the excluded flip word are just the
raw serialized equality and are marked `*`.

| Script | Records | Active | World 0 | World 1 | World 2* | World 3 | Inventory |
|---|---:|---:|---:|---:|---:|---:|---:|
| stand | 301 | — | 0 | 0 | 301 | 301 | — |
| turn | 178 | — | 0 | 0 | 178 | 178 | — |
| travel | 320 | — | 0 | 0 | 320 | 320 | — |
| movement | 224 | — | 0 | 0 | 224 | 224 | — |
| jump | 226 | — | 0 | 0 | 226 | 226 | — |
| grab-attempt | 252 | — | 0 | 0 | 252 | 252 | — |
| roll | 184 | — | 0 | 0 | 184 | 184 | — |
| flare-held | 166 | — | 0 | 0 | 166 | 166 | — |
| pistols | 116 | — | 0 | 0 | 116 | 116 | — |
| tiger-cave | 464 | — | 0 | 0 | 464 | 464 | — |
| pool-wade | 840 | — | 0 | 0 | 840 | 840 | — |
| pool-swim | 659 | — | 0 | 0 | 659 | 659 | — |
| flare | 127 | — | 0 | 0 | 127 | 127 | — |
| fall-damage | 178 | — | 0 | 0 | 178 | 178 | — |
| hard-landing | 140 | — | 0 | 0 | 140 | 140 | — |
| look | 352 | — | 0 | 0 | 352 | 352 | — |
| slide-jump | 144 | — | 0 | 0 | 144 | 144 | — |
| slide-jump-backward | 208 | — | 0 | 0 | 208 | 208 | — |
| look-sidestep | 212 | — | 0 | 0 | 212 | 212 | — |

On **every script**, the first raw active mismatch was `active.length`
at tick 0 (2 retail, 0 port); for the inventory it was
`inventory.items.length` at tick 0 (6 retail, 0 port). Both whole families
were unavailable, so neither has an official comparable first difference.
The first comparable world differences on **every script** were both at
tick 0: `world[0]` 2415482401 → 669297626 and `world[1]` 3089653279 →
602422431 (retail → port). Raw `world[2]` and comparable `world[3]` have
no differences. Details: `before.tsv` and `before/*.compare.json`. The
extra rows used the archived source-equivalent M10 step-2 host;
its positive-control `stand` trace and footer are byte-identical
to the initial baseline capture (`before-extra/`, `before-extra.tsv`,
and `v3/before-look-sidestep.tsv`).

### Why the digests differed

Schema 20 already hashed the right sector stream: logical room order,
X-major/Z-minor sectors, two little-endian 16-bit words followed by
below-room, floor, above-room and ceiling bytes, without the TR1 room
ordinal. The missing **live**
mutations were the four door placements at slots 0, 3, 6, 72 and the
resting block at 30. The two doors straddling portals and the reverse
start are not equivalent to adding room flags to a file checksum.
Specifically, door 6 owns room 4 (23,35) and room 41 (24,35), but its
blockable box comes from room 41 at the *near query* (23,35), box 703.
Both actual door cells have `NO_BOX`. The resting block's own sector
(room 42, tile (13,35)) has box 705. From `WALL.TR2` overlap words,
authored FNV is 602422431, applying only the block gives 130147807, and
applying both door and block gives 3089653279. The intermediate candidate
that confused the far leaf with the same-coordinate portal query is in
`after-first.tsv`; it passed sectors and failed boxes on every tick. No
ordinal, room coordinate or digest from this paragraph is used as a
runtime exception.

## After: pinned `tr64-tr2-items`

`Active` is comparable (`knownEqual`), not a strict full-family count.
`World` is strict equality **of all four independently counted words**.
`Inventory` is strict. A count smaller than the number of records is
followed by the first differing tick and field.

| Script | Records | Active | World 0 / 1 / 2 / 3 | Inventory | First comparable difference |
|---|---:|---:|---:|---:|---|
| stand | 301 | 301 | 301 / 301 / 301 / 301 | 301 | none |
| turn | 178 | 178 | 178 / 178 / 178 / 178 | 178 | none |
| travel | 320 | 320 | 320 / 320 / 320 / 320 | 320 | none |
| movement | 224 | 224 | 224 / 224 / 224 / 224 | 224 | none |
| jump | 226 | 226 | 226 / 226 / 226 / 226 | 226 | none |
| grab-attempt | 252 | 252 | 252 / 252 / 252 / 252 | 252 | none |
| roll | 184 | 184 | 184 / 184 / 184 / 184 | 184 | none |
| flare-held | 166 | 166 | 166 / 166 / 166 / 166 | 49 | tick 49 `inventory.items[3][1]`, 1 → 2 |
| pistols | 116 | 116 | 116 / 116 / 116 / 116 | 116 | none |
| tiger-cave | 464 | 464 | 464 / 464 / 464 / 464 | 464 | none |
| pool-wade | 840 | 840 | 840 / 840 / 840 / 840 | 840 | none |
| pool-swim | 659 | 659 | 659 / 659 / 659 / 659 | 659 | none |
| flare | 127 | 127 | 127 / 127 / 127 / 127 | 49 | tick 49 `inventory.items[3][1]`, 1 → 2 |
| fall-damage | 178 | 178 | 178 / 178 / 178 / 178 | 178 | none |
| hard-landing | 140 | 140 | 140 / 140 / 140 / 140 | 140 | none |
| look | 352 | 352 | 352 / 352 / 352 / 352 | 352 | none |
| slide-jump | 144 | 144 | 144 / 144 / 144 / 144 | 144 | none |
| slide-jump-backward | 208 | 208 | 208 / 208 / 208 / 208 | 208 | none |
| look-sidestep | 212 | 212 | 212 / 212 / 212 / 212 | 212 | none in these three families |

These are 5,291 records in total, including 1,685 on the seven dry-Lara
routes. All routes start with the same carried ring and both selection
indices equal. The known flare-stock charge at arm frame 46 belongs to
Lara's unimplemented flare controller ([Water and flares](specs/wall/tr2-water-and-flares.md#flares-new-in-ii)).
It remains a **real numeric inventory mismatch**, not a profile omission
or a guessed charge on an input or script tick. `v3/after.tsv` and
`v3/after/*.compare.json` include the exit codes (17 success, two inventory
mismatches). Camera, water, weapons, RNG and other Lara fields are not
claimed by this gate.
`v3/after/*.log` reports zero campfire range candidates on all 19 routes;
no positive flame-contact case is verified.
`look-sidestep` differs independently in `lara.rot[1]` at tick 155
(45 retail, 0 port), identically before the item changes. Manual Look
motion is outside this slice.

### Active fields

The ordered list length, slot and public type always compare. For the
campfire and helicopter all 22 schema fields are compared. For both door
types all fields except `hitMask` compare (animated-body contact not bound
here). For a tiger, **every item field compares on its wake record**, when
its room, position, orientation, health, motions, animation/frame, states,
timer, floor, touch and visible bits are still the placed initialization.
Once the same slot and type were live in both streams on the previous
record, the unsupported tiger controller leaves `pos`, `rot`, `room`,
`anim`, `frame`, `state`, `goal`, `next`, `hSpeed`, `vSpeed`, `roomFloor`,
`hitMask` and `gravity` unavailable for that item. The always-compared
tiger fields are `index`, `type`, `flags`, `health`, `timer`, `visibleMask`,
`status`, `wasHit`, and `collidable`. No AI, aiming, contact, navigation,
combat or post-wake motion follows from that subset. A missing/reordered
item or a type change is never excluded, nor is a campfire/door/helicopter
field simply because a tiger appears. Strict active equality is **zero
on every route** because the reverse door's touch field is unavailable.
Review found its raw value matches throughout; it is still not computed.
On the 739 post-wake tiger samples, four excluded fields happen to
match throughout, and nine differ at least once (`review/exclusions.json`).

The records verify the initial ordered `[100,72]`, the ordinary early
helicopter wake (`travel` and `jump` tick 204, followed by motion at 205),
and tiger 18 waking in `tiger-cave` and `pool-wade` at 396 and in
`pool-swim` at 429. The helicopter leaves the active list in `pool-wade`
at 603 and `pool-swim` at 616, while later entries continue to update.
These ticks are observations, not scheduling constants.

## Rules and provenance

The independent activity, action and starting-stock bits are registered in
[`game/tr2.h`](../game/tr2.h). All are host-only and bound at level entry;
the TR1 cart and the archived TR2 baseline run no sequel item controllers.

| Rule | Source and scope |
|---|---|
| Authored reset and wake order | [Item initialization](specs/wall/tr2-items-general.md#ordinary-placed-item-initialization-q-t2i-01), [Lara start, Which things move first](specs/wall/tr2-lara-start.md#which-things-move-first), and the placed `WALL.TR2` model/animation tables. State and goal come from the starting clip, queued state starts at zero, stored floor starts at the coarse authored height and default health is −16384. Tiger 20 HP and starting clip 286/state 3 are from [Great Wall creatures](specs/wall/tr2-great-wall-creatures.md#tiger), not the general defaults. Full startup mask becomes reverse. Room membership and active order are separate. The active list prepends in authored initialization/action order. A Lara-triggered wake appears before its first update; removals do not skip the next item. |
| Door and resting block topology | [Ground and weapons, Ordinary doors](specs/wall/tr2-lara-ground-and-weapons.md#ordinary-doors-and-timed-closing) and [Great Wall objects, Push/pull block](specs/wall/tr2-great-wall-objects.md#pushpull-block). Doors derive owned cells and the across-portal navigation box independently. The reverse door requests open before advancing on tick 1; the tick which begins open restores the raw cells. The resting block raises a floor four clicks and blocks its eligible box. No interaction, moving block or door contact was added. |
| Helicopter | [Item rules, Helicopter](specs/wall/tr2-items-general.md#helicopter-slot-101-object-type-259-q-t2i-02), [Great Wall level, Landing and main route](specs/wall/tr2-great-wall-level.md#landing-and-main-route), and [record membership](specs/wall/tr2-oracle-trace.md#record-fields). Move +100 Z, request sound 297 at the component-wise quarter point, retire for absolute moved Z separation strictly above 30720, then finish authored animation and linked-sector room check even when retiring. Stored floor is not refreshed. Logical sound only. |
| Campfire | [Item rules, Campfire](specs/wall/tr2-items-general.md#campfire-slot-100-object-type-253). The reverse-start emitter stays at its placed pose. `tr2-campfire` counts the triggered range candidate (inclusive 600-unit horizontal disk, ±3072 vertical extent intersected with interpolated pose bounds plus the documented 100 at the bottom). No recorded route comes within that range. Separate flame creation, lifecycle and cooldown; scenery damage; attached flame, extinguishing and presentation are **not implemented**. |
| Trigger actions | [Floor evaluation and item masks](specs/wall/tr2-triggers-and-interaction.md#item-masks-timers-and-action-ordering). Native admission checks programs and references. Ordinary runs in the air; a pad needs floor contact; antitrigger can clear in the air. Timer refresh precedes mask completion; time 1 remains one tick and larger values are seconds. Positive incomplete masks cannot lock, anti can; locked items ignore later actions. Item list insertion occurs in authored action order. Switch/key/pickup interactions, dynamic floor callbacks, effects, finish and soundtrack remain inert in this harness. The camera owns camera actions. |
| World trace | [Record fields](specs/wall/tr2-oracle-trace.md#record-fields): separate FNV streams over live sectors, each overlap word, flipped flag and ten compact runtime slots repacked into source bit positions, and live room partner words. `WALL.TR2` has no alternate rooms or flip requests (`authored-flips.txt`); the partial flip-slot word is also checked by a directed native control. These traces do not establish post-swap topology on a level with alternates. |
| Inventory | [Item rules, Starting inventory](specs/wall/tr2-items-general.md#where-the-great-walls-starting-inventory-comes-from-q-t2i-03) and [Lara start, Equipment and reserves](specs/wall/tr2-lara-start.md#lara-before-the-first-gameplay-update). The fresh stock is built in, and ring quantities are owned state, not a writer literal. The cook doesn't import `TOMBPC.DAT` progression or grant data. The four Great Wall script grants are later secret rewards, not additions to this stock. No bonus, acquisition, flare use, key selection, saved start or carry semantics are asserted. |

### Authored placements

`authored.txt` contains the placement census from the allowed level
reader. Names follow [the public object catalogue](specs/tr2-tr3-differences.md#32-tr2-complete-slot-list-rs-1512)
and the Great Wall object/level specifications. Inert items have authored
pose/identity only and no placed-item controller; they are absent from the
port's active list. Should a route wake an unimplemented placement, a
list mismatch stays visible rather than masking that item.

| Type | Placed | Name | State |
|---:|---:|---|---|
| 0 | 1 | Lara | existing dry controller; separate from active items |
| 36 | 13 | Small spider | inert |
| 38 | 3 | Crow | inert |
| 39 | 5 | Tiger | placement and trigger wake only |
| 55 | 14 | Falling block / collapsing tile | inert |
| 59 | 14 | Teeth spikes | inert |
| 60 | 2 | Rolling boulder | inert |
| 62 | 14 | Discgun launcher | inert |
| 67 | 1 | Pushable block 1 | resting surface and box only |
| 80 | 2 | Rolling spindle / blade | inert |
| 81 | 3 | Wall-mounted knife blade / guillotine | inert |
| 85 | 9 | Spiked wall | inert |
| 102 | 1 | Zip line handle | inert |
| 104 | 1 | Wall switch | inert |
| 106 | 3 | Door 1 | topology and animation; body contact inert |
| 107 | 1 | Door 2 / reverse door | topology and animation; body contact inert |
| 114 | 1 | Hinged trapdoor | inert |
| 143 | 1 | Shotgun ammo | inert |
| 144 | 1 | Automatic-pistol ammo | inert |
| 149 | 1 | Small medipack | inert |
| 150 | 1 | Large medipack | inert |
| 151 | 1 | Flare box | inert |
| 190 | 1 | Gold Dragon | inert |
| 191 | 1 | Jade Dragon | inert |
| 192 | 1 | Silver Dragon | inert |
| 193 | 1 | Guardhouse Key | inert |
| 194 | 1 | Rusty Key | inert |
| 201 | 1 | Keyhole 1 | inert |
| 202 | 1 | Keyhole 2 | inert |
| 214 | 2 | Tyrannosaurus | inert |
| 243 | 2 | Camera target | no ticking controller; existing look subject pose |
| 253 | 1 | Campfire flame emitter | active stationary emitter and counted range; separate flame not implemented |
| 259 | 1 | Flying helicopter | awake animation, flight, room links and retirement |

### Numbered questions

1. **Q-T2I-01 — closed:** [placed-item and campfire rules](specs/wall/tr2-items-general.md#ordinary-placed-item-initialization-q-t2i-01)
   establish the `stand` tick-0 defaults, the pose versus effect split
   and the flame's range. This slice does not bind the flame lifecycle.
2. **Q-T2I-02 — closed:** [helicopter update](specs/wall/tr2-items-general.md#helicopter-slot-101-object-type-259-q-t2i-02)
   defines `travel` tick 205's pose advance, stored floor and speeds,
   and the actual order and sign of the retirement check.
3. **Q-T2I-03 — closed:** [stock provenance](specs/wall/tr2-items-general.md#where-the-great-walls-starting-inventory-comes-from-q-t2i-03)
   confirms fresh starting stock is not a script grant. The separate
   flare controller will own the arm-frame-46 charge.
4. **Q-T2I-04 — open, outside this slice:** no published route walks
   into campfire 100. To implement burning and effects, specify the
   attached flame's exact joint/local offset, the room used for its
   water-plane lookup, and its first update when created during the
   effect pass. OpenLara's attach point is not asserted as retail.
5. **Q-T2I-05 — inert, no authored case found:** a valid generic object
   activation naming Lara is counted and does not mutate her. The item
   timer has its own zero-initialized storage, separate from oxygen.
   It has no update behavior and remains unavailable in the profile.
   Census: none of the 118 Great Wall activation words
   targets Lara, nor any of 5,438 unique activation words in the 24
   available retail PC level/cut/title files (`v3/lara-target-census.txt`).
   All 5,291 raw `lara.timer` values agree, but that observes only reset
   state and is not a reason to remove the profile exclusion.

## v3 review fixes

The v2 review found five defects on directed native tests, not on the
published routes. Each has a control in `tr2-item-actions-test.py`;
the same tests fail on the v2 executable (`v3/controls-v2-red.log`).

| Defect | v3 correction |
|---|---|
| Open doors skipped setup geometry checks | Derive and validate every placed door's references before the first record, independent of its pose; verify the cardinal heading and sector/box lookups for both room variants. Only initially closed doors actually close the cells at startup. |
| A sequel world item could execute a TR1 animation effect | Follow authored state ranges and end links from every ticking door and helicopter's model base at admission. For the supported fixed leaves and directly drifting aircraft, refuse clips with generic motion or commands other than sound or no-op. The authored Great Wall animations use those supported commands only. Both a direct and a linked unsupported command fail by name before tick 0. |
| A generic action could overwrite Lara's oxygen | Keep a separate item countdown; count a Lara-target action as inert without rejecting its valid target index. The directed control checks the inert count and the untouched timer and air. |
| Flip actions silently accepted slot 10 | Check operands for all three flip actions against the ten-slot domain in native gameplay admission and the PC source reader. Both use the generated floor grammar. Slot 9 is the positive control. |
| Placement bit 15 leaked into source flags | Store cleanup eligibility separately from ordinary sampled flags, per [placed-item initialization](specs/wall/tr2-items-general.md#ordinary-placed-item-initialization-q-t2i-01). It neither retires the placed item nor arms an unsupported body-clear action. |

## Verification and hand-off

The runtime, profile, comparator and controls landed as
`1292bedab2b58b322931d47d6dd4bbab8522c993`. The frozen review set is `items-v3.patch`
in the evidence directory, SHA-256
`42c5157e1f9f6c14b1c5ab2825b3c614ac877430238b6fdda64d03ea869d9128`,
on `cd81271b1c667287f66c46935adf559a766be02e`. The status document is not
part of that hash. No level content, records, ROM or ELF are in the patch.

The shipping build's `.text` matches the archived M10 step-2 shipping
ELF byte for byte (both 437,744 B, SHA-256
`0aacd23cc8607acc6f2dff60c6509a7b071aa4ec41e5fd5cebd59b074e28eeaa`).
The archived build and this patch's `main` base differ only in docs and
wall specifications outside the implementation. `v3/shipping-identity.log`
records the comparison, hot-layout verdict (41 sections, 16,288 B
window), and the resident size gate (601,880/622,592 B). The same
shipping pack and `TR64_PLAY=1 TR64_TEST=0` were used. No emulator or
second-game cartridge was run. The v2 slice added **3,485 B `.text` /
620 B `.rodata`** across its five gameplay/trace host objects before
link (`host-section-delta.txt`). The v3 fixes add **1,600 B `.text` /
288 B `.rodata`** against the v2 linked host; `.data` and `.bss` remain
unchanged (`v3/linked-host-size.txt`).
Great Wall host state rose from 173,452 to **175,080/196,608 B**
(+1,628 B overall, +40 B over v2; `before/stand.log` and `v3/after/stand.log`).

**Later-level limits:** On their cooked tables, Venice (`venice.TR2`)
and Bartoli's Hideout (`BOAT.TR2`) passed direct floor, item, animation
and door-geometry checks. Neither level is supported: Venice requires
1,869,504 B and Bartoli's Hideout 1,743,360 B against the 1,507,328 B
level row, and each exceeds the inherited 139-room pair storage in
[`game/world.c`](../game/world.c) (166 and 155 rooms, respectively).
The direct geometry probe bypassed that room-pair limit; gameplay
admission did not. See `review-v3/retail-admission.tsv` and
`review-v3/retail-admission.direct.tsv`.

**TR1 solo:** all 25 fixtures were byte-equal in each of schemas 2–5.
Schema 2 used the immutable M2 records; schemas 3–5 streamed the pinned
`m9-coop-passport` oracle (`a98eff3f367758691d684e3d3f71b57ade7e14e5c290dd037ed3145dfd266b09`).
The seven logs under `v3/solo/` share the v3 host executable
`153c197af0604eceff97c1ed2aaf1cb048df71d0eb60233f04d6d42380eef9ef`,
archived as `v3/tr64-host`. Directed control and provenance logs are in
`v3/` (`input-test.log`, `controls-v3-green.log`, `comparator.log`,
`floor-admission.log`, `provcheck.log`).

Reproduction (run from the assigned worktree; commands for all route
captures and the pinned comparator are in `v3/after/commands.json`):

```sh
export TMPDIR=$TR64_WORK/tmp/T2Items PYTHONDONTWRITEBYTECODE=1
B=$TR64_WORK/builds/T2Items
E=$TR64_WORK/evidence/m10/items
W=$TR64_WORK/evidence/m10/wall/wall.pack
F=$TR64_WORK/fixtures/oracle-wall/tr2
P=$TR64_WORK/builds/Video3/pack/pc-campaign21-video.pack
rtk mkdir -p "$TMPDIR"
rtk proxy make -j2 BUILD_DIR="$B" TR64_PLAY=1 TR64_TEST=0 TR64_LEVEL_PACK="$P" rom host
rtk python3 "$E/replay.py" v3/after tr64-tr2-items
rtk python3 test/game/tr2-input-test.py "$B/host/tr64-host" "$W"
PYTHONPATH=tools/cook rtk python3 test/game/tr2-item-actions-test.py "$B/host/tr64-host" "$W"
rtk python3 test/game/tr2-compare-test.py "$F/stand" --tiger "$F/tiger-cave"
PYTHONPATH=tools/cook rtk python3 test/game/tr2-floor-admission-test.py "$B/host/tr64-host" "$W"
rtk make provcheck

O=$TR64_WORK/fixtures/oracle
R="$O/m9-coop-passport-5094f1a470676724cc521b8409d9a1cbf13adb301419db825c351bab7a8c785f/tr1-oracle"
for schema in 2 3 4; do
  for level in level1 gym; do
    rtk python3 test/game/fixtures.py --level "$level" --executable "$B/host/tr64-host" \
      --fixtures "$O/m2-start" --schema "$schema" --oracle "$R" --out "$TMPDIR/solo/$schema-$level"
  done
done
PYTHONPATH=test/game rtk python3 "$E/schema5.py" "$B/host/tr64-host"
```

The runtime action test checks airborne pad veto, ordered wakes, no
same-tick movement after wake, next-item update after helicopter
self-removal, incomplete vs complete masks, one-shot and timer rules,
and the live partial flip-slot digest. The comparator controls rehash
changed records: they detect a wrong wake pose, item order/length or
other-type pose under the projected profile, but not a deliberately
unavailable tiger pose on a later tick. No new verification input or
result is used by the engine.
