# M10: Tomb Raider II cart image at 4 MiB

**The rules implemented at `45ea38e` fit in the 608 KiB resident row as
linked code, with 4,328 B left. A playable Great Wall does not fit there
in the planning estimate; the whole game needs both a larger image row
and level-selected code, or measured savings equivalent to them.** This
is a link and content census, not a gameplay cart. The stock TR2 cart
still refuses play; the measurement ELF also fails a real paging-reference
check. No game rules or production build settings change here.

Base: `45ea38e232176c54fda3e67e136b6463e6c8ce82`, the first merged per-game
row/water/wet-camera tree. Evidence and reproducible drivers: **`$TR64_WORK/evidence/m10/cart-image/`**
(`E` below). Units are binary KiB and decimal bytes throughout. See
[architecture §6](architecture.md#6-code-residency), [D17](architecture.md#11-decisions), the
[row table](tr2-level-fit.md#row-policy), and the individual implementation
status documents in architecture §10.1, M10 step 3.

## Merged baseline (before the scratch link)

| Check | Result | Evidence in `E` |
|---|---|---|
| Shipping PC TR1 cart, host build and `make unit` | Passed. Resident 601,880 / 622,592 B; hot window 16,288 / 16,384 B; hot data 6,864 B at phase 1,024; room/item stack depths 1,016 / 3,368 B. ELF `.text` 437,744 B, byte-identical to the archived pre-merge cart. | `baseline-tr1-build-unit.log`, `baseline-replays.log`, `tr1/size.json` |
| `n64-core-tests` with `TR64_GAME=1` and `2` | 113 native production-harness runs each; both passed, including native pack/texture and over-budget admission controls. Host also built with selector 2, using the ordinary TR2 posed cart for layout. | `baseline-native1.log`, `baseline-native2.log` |
| TR1 solo | 25/25 byte-equal in each of schemas 2, 3, 4 and 5; frozen M2 traces for schema 2 and pinned `m9-coop-passport` oracle for 3–5. | `baseline-replays.log` |
| TR2 host replays | All four pass the reviewed `tr64-tr2-wet-camera` profile in the five gated families below; each complete port trace is byte-identical to the independently reviewed pre-merge trace. | `*.compare.json`, `baseline-replay-identity.log` |

| Route | Records | Lara / active / world / inventory, comparable rows each | Camera | Gameplay RNG equal prefix |
|---|---:|---:|---:|---:|
| `pool-swim` | 659 | 659 | 659 | 430 |
| `pool-wade` | 840 | 840 | 840 | 397 |
| `ledge-grab` | 356 | 356 | 356 | 356 |
| `pistols` | 116 | 116 | 46 | 116 |

Rows include tick 0. `pistols` camera ends after tick 45 (`firearms`). The
pool RNG differences start at ticks 430/397 (creature updates, not yet
implemented). Save and presentation RNG are reported but not gated:
`ledge-grab` still differs on `save.distance` from tick 1 and the draw RNG
from tick 2 like the other three runs. These are not new exclusions or new
merge failures. No emulator, new whole-game battery, or shipping route run.

## VR4300 measurement

All four links use the production compiler/ABI (`-march=vr4300`, o64,
`-O2`, function/data sections), GCC 16.2.0, GNU ld 2.45 and the production
libdragon/tiny3d SDK pins. The ordinary link already uses `--gc-sections`.
The scratch overlay adds the play roots and `tr2_arms.o`; it enables the
existing host-gated state/types, gameplay and validation only on the game
objects and native validator (not the host platform or host scene driver).
`TR64_GAME=2` folds `TR64_RULE` to the same rule set as the reviewed host
profile, including its safety boundaries. It does not root the schema-20
trace exporter or enable the extra co-op trace schema. TR1 paths not
selected through `TR64_RULE` can still link through the existing play,
scene, menu, cut and save surfaces. This is not a complete D17 cutover.

| Configuration | Resident text¹ | RO data | Data | BSS | Full resident extent² | Free in 608 KiB | Paged payload³ |
|---|---:|---:|---:|---:|---:|---:|---:|
| TR1 shipping (`tr1`) | 438,504 | 85,476 | 41,272 | 28,440 | 601,880 | 20,712 | 258,048 |
| TR2 stock poser (`poser`, reference) | 403,048 | 81,300 | 41,264 | 28,384 | 560,872 | 61,720 | 258,048 |
| TR2 folded, old roots retained (`tr2-full`) | 454,888 | 87,740 | 41,304 | 28,432 | 618,264 | 4,328 | 262,144 |
| TR2 folded + ten legacy overlay roots removed (`tr2-pruned`) | 451,912 | 87,132 | 40,152 | 28,432 | 618,264 | 4,328 | 221,184 |

¹ Includes .boot and vectors; the `.text` section alone is 437,744 / 402,288 /
454,128 / 451,152 B. ² `__bss_end - 0x80000000`, including alignment holes.
³ Sum of page-aligned ELF unit payloads, **not** simultaneous RDRAM use or
compressed ROM size. `size.json` in each profile is the actual size gate result.

Dropping ten overlay registrations cuts 40,960 B of paged payload, and
2,976 text + 608 RO + 1,152 data = **4,736 B** of initialized resident
sections. It buys **zero** resident extent in this link: the space before
the hot `.sbss` anchor expands from 2,520 to 7,256 B. With the post-anchor
sizes unchanged, growing pre-anchor content by more than about 7 KiB on
the pruned link would move the anchor by 8 KiB and break the 608 KiB gate.
Do not spend both the 7,256 B hole and the 4,328 B distance from the current
image end to the cap.

**Per-file and per-unit tables.** `objects.tsv` has all 81 `game/*.o`
rows per configuration (including the absent TR2 arms object in TR1) plus
`engine/res/validate.o`: raw object and actual linked resident/paged
text/RO/data/BSS bytes separately. `object-roles.tsv` partitions the linked
input sections per file and unit into named TR1-only, named TR2-specific,
and remaining shared/mixed. Uncertain reuse, changed shared functions and
inline branch text stay mixed; this conservative partition is not a count
of retail rules or a proof that all remaining code is needed. `units.tsv`
has the same four byte classes for **every** overlay (including non-game
units), fill, frame count and TLB entries. Unit payload is text/RO only;
any unit-owned writable data remains resident and is billed by object.
`bodies.tsv` gives the largest individual symbols and their input-section
sizes. Merged literals are charged to the link-map owner, not uniquely to
the source that authored them. The parser reconciles resident attribution
against `size_gate` and overlay attribution plus fill against ELF extents.

| Folded TR2 game + validator, linked input sections | Resident text / RO / data / BSS | Paged text / RO / data / BSS |
|---|---:|---:|
| Full: shared/mixed | 153,088 / 25,527 / 22 / 4 | 41,888 / 2,080 / 0 / 0 |
| Full: named TR1-only | 3,392 / 0 / 0 / 0 | 30,176 / 1,884 / 0 / 0 |
| Full: named TR2-specific | 28,704 / 336 / 11 / 0 | 2,496 / 0 / 0 / 0 |
| Pruned: shared/mixed | 150,688 / 25,527 / 22 / 4 | 41,888 / 2,080 / 0 / 0 |
| Pruned: named TR1-only still linked | 2,816 / 0 / 0 / 0 | 6,016 / 122 / 0 / 0 |
| Pruned: named TR2-specific | 28,704 / 336 / 11 / 0 | 2,496 / 0 / 0 / 0 |

The 31,200 B of named sequel text is **not** the net image delta. It includes
7,904 B linked from `tr2_arms.o` (the arms tick is 4,252 B), 8,480 B of
named Lara movement (ladder/air/water and gates), 4,640 B of camera,
2,240 B of pose, 2,080 B of animation, 1,472 B of room, and 2,496 B of
cold native validation in `load`. Startup, activation/door/trigger logic,
stock, and wet-camera changes also modify shared functions. Conversely,
legacy paths fold away and the old game/engine wrappers still retain work
that will be replaced. The scratch comparison changes whole-image resident
extent by +16,384 B versus shipping TR1 and +57,392 B versus the poser.
Neither is a source attribution or an estimate of future work.

### What legacy code still links, and why not to count it as shared

The census decodes the production TR1 PC campaign pack (21 entries) and
freshly cooks and independently decodes all 24 installed TR2 inputs (19
maps, four cuts, title; every TR2 pack hash equals the prior full cook).
`placement-types.tsv` counts 1,533 instances / 109 types for TR1 PC, and
2,576 / 157 for TR2 PC. The existing TR1 PlayStation pack has the same
placed type sets on all 21 maps, with 1,534 instances (one more in LEVEL8A,
`tr1-psx-census.log`). `placement-levels.json` lists each PC type by level,
input identity, resident level bytes and projected state. Raw
numeric IDs are in separate domains: TR1 slot 39 is a dart; TR2 slot 39
is a tiger. Use the named [TR2 crosswalk](specs/tr2-tr3-differences.md#32-tr2-complete-slot-list-rs-1512)
and the [family differences](specs/wall/tr2-delta-map.md#which-of-is-enemies-and-encounter-rules-carry-over),
not the intersection of raw IDs. Absence from
placed items is not dynamic unreachability (e.g. TR1 Adam, TR2 discs,
black snowmobile and dragon halves). Follow known spawning and references
from their placed roots before removing them.

* The largest old bodies in the full link (ELF symbol bytes) are Mutant
  2,452, Natla 2,392, Adam 1,792, Doppelganger 1,492, Bear 1,476, Gorilla
  1,444, Pierre 1,400, Centaur 1,252 and Wolf 1,216. None of these exact
  encounter controllers serve the TR2 campaign. The removed units are
  `enemy_{peru,greece,mutant,atlanteans,gunmen,henchmen,natla,adam,doppelganger,encounter}`.
  In this scratch proof, their dispatch-table roots are absent and normal GC
  discards the unreachable bodies; room/family selectors by *name* are not
  rewritten and the result is not runnable. Rat and Rex variants need
  independent TR2 rules, not automatic reuse because the role is familiar.
* Removing whole units leaves Raptor (892 B) packaged with Rex, plus legacy
  objects in the combined `objects` unit: Midas (752 B), Lightning (736 B),
  Thor (608 B), Scion target (560 B), and cabin (160 B), among others.
  Resident examples are `encounterPrecreateChildren` (1,060 B), the skater
  board helpers (e.g. `Skater_syncBoard`, 252 B), and the TR1 campaign and
  special-actor branches. Marked old object bodies are a reachability list
  for replacement, not extra observed image savings; some have a similar
  later-game role but no evidence for reusing the old body as authored.
* Hot layout and pool placement also pass on the pruned ELF. The same ELF
  correctly exceeds a 600 KiB row (614,400 B). **The paging references gate
  rejects** one edge: resident `levelStart` directly calls
  `load:tr64_level_validate_tr2_gameplay`, after the production loader has
  released that unit. A real TR2 cart must route validation through the
  load lease or retain the validator resident and remeasure. It also needs
  real input, flow/transition, audio, save and object dispatch admission.
  An ELF with enough bytes free is not a ROM that can play.

## Remaining text: estimates, not measurements or upper bounds

`estimate-text.tsv` is the per-family basis: linked TR1 symbol or controller
object bytes, exact placed trigger IDs, and explicit low/high reuse
multipliers. For example: TR1 Lion 864 B, Rat 928 B, Bat 448 B and Rex
1,216 B anchor the four Great Wall creature families (five tigers, 13
small spiders, three crows, two Rex placements); one controller per family,
not per instance. Shared navigation uses the measured 13,632 B of TR1
`enemy` + `nav` as a reuse base. The discs, boulders, spikes and blade/wall
movers use a 2,504 B base of TR1 dart, boulder, blade and slam bodies;
`lara_weapon` (7,744 B), `object_block` and the relevant hazards approximate
boat/sled work because TR1 has no equivalent vehicle. Multipliers widen
where the spec gives no implementation shape (especially vehicles and
bosses). Repeated doors, meshes, costume IDs and ring/pickup variants do
not buy separate controllers. This is gross new or replacement text relative
to the baseline, not the amount by which an ELF would grow.

| Estimated scope at `45ea38e` | Gross text B if all remaining work is resident | Text B that stays resident with the example unit split | Text B assigned to units in that split |
|---|---:|---:|---:|
| Great Wall, including optional creatures, dragons and its onward cut/carry | 25,200–61,600 | 17,100–42,200 | 8,100–19,400 |
| Rest of the base campaign, additional | 67,900–146,300 | 11,200–26,300 | 56,700–120,000 |
| All 24 inputs | 93,100–207,900 | 28,300–68,500 | 64,800–139,400 |

The Wall scope is from the [objects](specs/wall/tr2-great-wall-objects.md),
[creatures](specs/wall/tr2-great-wall-creatures.md), [route](specs/wall/tr2-great-wall-level.md),
[interaction](specs/wall/tr2-triggers-and-interaction.md) and the cited
on-foot status docs (combat camera, wet/climb exits, hit/death, weapons and
flare presentation remain). Rest-of-campaign breadth comes from the
[delta map](specs/wall/tr2-delta-map.md) and the Venice
[vehicle/object](specs/wall/tr2-venice-objects.md) and
[creature](specs/wall/tr2-venice-creatures.md) specs plus the cooked census.
It includes water/cargo, hostile projectiles, later humans, sea life,
snowmobiles, monks/guardians and dragon phases. The census cannot establish
rule completeness or bound code size for unresearched later set pieces.

## Options and costs

1. **Keep the image at 608 KiB.** Only the current link fits as measured.
   Neither Wall estimate fits as new resident text. With an illustrative
   15% for RO/data/link overhead, the observed pre-anchor hole and unchanged
   BSS, `resident-scenarios.tsv` gives **estimated** Wall image extents of
   642,840–683,800 B all-resident, or 634,648–667,416 B split. The
   equivalent full-campaign ranges are 724,760–855,832 B and
   651,032–691,992 B. These scenarios intentionally do not credit further
   pruning of mixed TR1 code or introduce new gameplay/state data. For a
   possible measured reclaim target, `trace.o` still owns 19,712 B of
   resident text in the pruned ELF: the legacy diagnostics/scene-hash
   roots would need to be reworked or kept in an explicitly leased unit,
   not just deleted to fit a row. Moving it would not save the full amount
   after alignment; changing the shipping gate is not part of this work.
2. **Page cold code and retain hot shared code.** Current `load` goes from
   9 to 10 pages (both 5 TLB entries) with the native validation included.
   The stock `load`, `front`, `medium`, `inv`, `save`, gameplay controllers
   and no-level `video` show the existing lifetimes and what can be paged:
   text + RO only; not mutable tables, not code reached by an interrupt,
   not another unit's callee. Resident-to-unit calls must be through the
   dispatch table; transitions are outside dispatch, at a load/frame
   point, with at most one load per play frame. Keep Lara, room/collision,
   common navigation, render/audio hot paths resident. Good candidates are
   start/validation, level-only actor and trap controllers, and genuinely
   cold diagnostics/presentation. Moving a shared per-item helper into a
   unit would multiply call crossings even if no DMA occurs during play.
3. **Select level units by types (including spawn closure).** Stock TR1
   already scans the item list for enemy units after releasing `load`;
   objects/inventory/save plus medium/front account for 15 live frames
   excluding enemies in the earlier shipping log. The example splits families by
   placed roles, charging `ceil((1.15 × estimated text + 256) / 4096)` per
   unit (including a conservative separate charge for expanding the common
   objects unit). It budgets 17 additional unit names, within 32 once the
   old enemy overlays are removed. It is not a cook implementation or a
   measured footprint. `overlay-plan.tsv` enumerates all levels: **21–23**
   frames for a Wall-only scope; **21–24** for Wall in the campaign split,
   **24–32** Venice (`boat`), **25–31** Monastery, **27–34** SKIDOO,
   **29–38** Diving Area (`platform`, worst). The high case there sums to
   24 TLB entries including the wired resident entry, but fails the current
   24-*frame* row. Even the low scenario fails some levels. Check capacity
   at every transition (including live menu/save, cut and loader overlap),
   not just a level's largest object count. Prefer grouping by actual
   co-occurrence and moving infrequent work out of the always-loaded 15
   frames before increasing this row. A cook gate must close over generated
   objects, child types, effects and level scripts, reject unresolved
   families, and assert frames and pairs separately.

   Historical TR1 PC ares data (not a new emulator run): the 9-page load
   unit acquired in 7.768–7.893 ms and released in 0.314–0.382 ms across 14
   acquisitions in the archived shipping log; see extraction below. Scaling by bytes gives a
   **rough** 0.86–0.88 ms per page (about 7 ms for eight extra pages at
   load); distinct units add fixed lookup, DMA/cache and probe overhead,
   and ares models a fixed PI speed. Loaded units need no per-frame DMA.
   The earlier [perf audit](perf-audit.md#ranked-list-expected-gain-on-the-console-largest-first)
   measured 0–6 explicit unit dispatches/tick at TR1 level starts, *not*
   crossing time: each dispatch is at least two crossings plus calls back
   to resident helpers. The perf audit models up to 35 clocks per M64
   translation refill, not a measured cost per crossing. Its one-entry
   instruction translation cache can add refills that ares does not model.
   There is no TR2 frame or real-console cost measurement for this split.
4. **Grow the rows explicitly.** `row-trades.tsv` spends reserve first and
   holds the level/carve phases in 8 KiB steps. Up to +64 KiB image (672 KiB)
   leaves 6 KiB reserve without touching level/state. +96 KiB (704 KiB)
   cuts 32 KiB out of level, leaving Monastery **32 B** instead of the
   32,800 B margin; +104 KiB (712 KiB) cuts 40 KiB and refuses Monastery.
   +136 KiB also refuses Emprtomb. Every other level has at least 185,312 B
   raw margin (SKIDOO), and Wall has 682,640 B; this is not permission for an
   in-place variable carve or to spend the Monastery design cushion. At the
   code high scenario of 38 frames (152 KiB), 640 KiB image takes 24 KiB
   from level (8,224 B Monastery margin), 656 KiB takes 40 KiB (Monastery
   fails by 8,160 B). By contrast 640/128 KiB image/code leaves the level
   row alone, 6 KiB reserve, but requires at most 32 live code frames.
   `joint-row-trades.tsv` covers the combined cases. State remains 296 KiB;
   its largest **projection** (SKIDOO, 299,120 B) leaves only 3,984 B.
   Do not count that or unused-looking GPU/audio/I/O capacity without a
   new high-water measurement. Physical placement as well as declared rows
   must pass; cache-alignment costs are real.

**Recommendation.** Do not take bytes from level or state now. Get a
working Wall-only D17 play link first with real validation/dispatch and
legacy role removal, keep the 96 KiB code row, and plan a **656 KiB image
from reserve if the measured Wall link needs it** (22 KiB reserve remains).
Measure resident extent, code peaks at load/menu/save, state high-water and
frame cost in the Wall scenes before committing a row change. For the
campaign, prioritize stage-specific code residency and build a cook proof
before treating any estimate as a budget. Monastery's margin makes
“just add image and code pages” a poor campaign default. These data do
not show that 4 MiB is impossible; they show why the 608 KiB image alone
cannot presently be treated as a budget for full TR2 play.

## Reproduce

Use a disposable worktree at the base above. Run from its root. Scripts
use the evidence directory for their output and `$TR64_WORK/{builds,tmp}/T2Cart`
for scratch. No pack or fixture is copied. `snapshot.sh` records compiler
versions, section and symbol dumps, map, compressed ELF, unit manifest and
real size-gate JSON. `measurement-inputs.sha256` binds the scratch overlay,
unit list, source patch and external sound bank. Source patch SHA-256:
`440c75c272966798155290ba391feae5930d4748996f0e14454e0ba1a7a8d3dd`.
The patch is a **measurement input**, not a proposed build-system change.

```sh
set -eu
export TMPDIR=$TR64_WORK/tmp/T2Cart PYTHONDONTWRITEBYTECODE=1
E=$TR64_WORK/evidence/m10/cart-image
B=$TR64_WORK/builds/T2Cart
W=$TR64_WORK/evidence/m10/wall/wall.pack
rtk mkdir -p "$TMPDIR"
rtk bash "$E/baseline.sh" tr1
rtk bash "$E/baseline.sh" replays
rtk bash "$E/baseline.sh" native1
rtk bash "$E/snapshot.sh" tr1
rtk rm -rf -- "$B"                       # only this disposable build tree
rtk bash "$E/baseline.sh" native2
rtk bash "$E/snapshot.sh" poser
rtk sha256sum -c "$E/measurement-inputs.sha256"
rtk git apply "$E/measurement-source.patch"  # disposable worktree only
rtk proxy make -f Makefile -f "$E/measure.mk" -j1 BUILD_DIR="$B" \
    TR64_GAME=2 TR64_PLAY=0 TR64_TEST=0 TR64_LEVEL_PACK="$W" elf
rtk bash "$E/snapshot.sh" tr2-full
rtk proxy make -f Makefile -f "$E/measure.mk" -j1 BUILD_DIR="$B" \
    TR64_GAME=2 TR64_PLAY=0 TR64_TEST=0 TR64_LEVEL_PACK="$W" PRUNE_TR1_UNITS=1 elf
rtk bash "$E/snapshot.sh" tr2-pruned
rtk bash "$E/controls.sh"
PYTHONPATH=tools/cook rtk proxy python3 "$E/census.py" > "$E/placement-types.tsv"
PYTHONPATH=tools/cook rtk proxy python3 "$E/tr1-psx-census.py" > "$E/tr1-psx-census.log"
rtk proxy python3 "$E/image-ledger.py" > "$E/image-summary.txt"
rtk proxy python3 "$E/estimate.py" > "$E/estimates.txt"
rtk awk '/^load-scope:/ {
  l=$2; u=$4; sub(/^unit_load_us=/,"",l); sub(/^unload_us=/,"",u)
  if (!n++ || l+0<ml) ml=l+0; if (l+0>xl) xl=l+0
  if (n==1 || u+0<mu) mu=u+0; if (u+0>xu) xu=u+0
} END {print n,ml,xl,mu,xu}' \
    "$E/../venice/fit/review2/ship-pc/play-check/ares.log"
rtk rm -rf -- "$B" "$TMPDIR"             # only this job's paths
```

**Not measured:** runnable TR2 boot or play, real TR2 controller/rodata
sizes beyond the code already written, actual future code-unit peaks and
switch costs, campaign/media ROM capacity, state on a playable cart,
new frame timings, and real hardware. The TR2 placements cover base PC,
not expansions or a PlayStation sequel edition. Historical ares
costs describe their recorded TR1 builds, not timing guarantees.
