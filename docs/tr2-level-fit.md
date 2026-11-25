# M10: PC Tomb Raider II level fit at 4 MiB

All 24 PC inputs cook at full resolution. The second-game cart is
still render-only; its row selection does not admit second-game
gameplay. `BOAT.TR2` is Venice (`boat` in the pack); `venice.TR2` is
Bartoli's Hideout (`venice`). Both use the same native format, loader,
renderer, and posed scene as [The Great Wall](tr2-wall-measurement.md).

Evidence paths below are relative to `$TR64_WORK/evidence/m10/venice/`.
`inputs.sha256` records the two read-only PC level files and their shared
`MAIN.SFX`. No game data or native pack is in Git.
The complete catalog, the new row configuration and fresh checks
are in `fit/`. The detailed earlier pose survey below is labelled
with its experimental shared-row configuration.

## Full catalog

On `53e2fc6`, the scratch invocation replaced only the aggregate
admission argument to `n64_pack.level_layout` with 4 MiB; the normal
strict readers, validators, texture writer, geometry writer, sound
writer, I/O guard and independent pack decode were unmodified.
All 24 inputs (19 maps, four cuts, one title) cooked. `fit/catalog.json`
records the resident breakdown and source-reconciled census deltas.
Core includes per-table padding; the level total also aligns each
arena section to 16 B. The I/O column is for the single-entry pack
(directory, retained records, 16 KiB staging); it does not claim a
combined campaign pack was measured.

| Input | Core B | TX64 B | RG64 B | MG64 B | Census total B | Actual B | Δ B | I/O B |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `ASSAULT.TR2` | 417,760 | 505,184 | 306,824 | 129,248 | 1,361,760 | 1,359,024 | −2,736 | 38,896 |
| `BOAT.TR2` | 626,704 | 582,912 | 305,408 | 228,328 | 1,747,808 | 1,743,360 | −4,448 | 46,000 |
| `catacomb.TR2` | 601,808 | 498,528 | 459,256 | 206,016 | 1,769,360 | 1,765,616 | −3,744 | 43,696 |
| `CUT1.TR2` | 715,184 | 178,288 | 17,624 | 68,816 | 982,000 | 979,920 | −2,080 | 21,520 |
| `cut2.TR2` | 758,272 | 240,016 | 12,264 | 112,744 | 1,125,808 | 1,123,312 | −2,496 | 21,616 |
| `cut3.TR2` | 844,048 | 305,256 | 52,360 | 141,056 | 1,345,776 | 1,342,736 | −3,040 | 23,856 |
| `cut4.TR2` | 801,088 | 182,120 | 46,000 | 114,896 | 1,145,808 | 1,144,112 | −1,696 | 21,456 |
| `deck.TR2` | 687,040 | 542,960 | 430,336 | 226,688 | 1,891,760 | 1,887,024 | −4,736 | 42,448 |
| `Emprtomb.tr2` | 623,856 | 610,728 | 555,384 | 229,896 | 2,024,784 | 2,019,888 | −4,896 | 45,008 |
| `FLOATING.TR2` | 699,536 | 590,728 | 299,896 | 232,120 | 1,826,720 | 1,822,304 | −4,416 | 45,904 |
| `house.TR2` | 607,824 | 541,640 | 286,432 | 191,408 | 1,630,368 | 1,627,312 | −3,056 | 43,856 |
| `icecave.TR2` | 612,928 | 544,072 | 436,824 | 208,488 | 1,806,016 | 1,802,336 | −3,680 | 45,360 |
| `keel.TR2` | 632,224 | 582,456 | 349,248 | 255,728 | 1,824,352 | 1,819,664 | −4,688 | 45,392 |
| `living.TR2` | 574,880 | 523,344 | 324,456 | 227,032 | 1,654,448 | 1,649,728 | −4,720 | 43,024 |
| **`MONASTRY.TR2`** | 651,888 | 615,224 | 551,320 | 237,704 | 2,060,032 | **2,056,160** | −3,872 | 44,496 |
| `OPERA.TR2` | 637,328 | 597,688 | 326,720 | 248,704 | 1,816,048 | 1,810,448 | −5,600 | 46,512 |
| `platform.TR2` | 574,704 | 626,776 | 277,856 | 264,552 | 1,749,104 | 1,743,904 | −5,200 | 46,224 |
| `RIG.TR2` | 576,784 | 561,312 | 366,968 | 234,432 | 1,744,752 | 1,739,504 | −5,248 | 45,232 |
| `SKIDOO.TR2` | 670,144 | 465,832 | 545,896 | 221,752 | 1,907,744 | 1,903,648 | −4,096 | 45,488 |
| `TITLE.tr2` | 4,064 | 60,504 | 640 | 10,696 | 77,504 | 75,920 | −1,584 | 19,280 |
| `unwater.TR2` | 617,552 | 438,552 | 320,256 | 198,304 | 1,578,736 | 1,574,672 | −4,064 | 43,632 |
| `venice.TR2` | 646,864 | 613,568 | 375,920 | 233,152 | 1,875,088 | 1,869,504 | −5,584 | 45,648 |
| `WALL.TR2` | 535,056 | 432,544 | 226,552 | 212,152 | 1,410,544 | 1,406,320 | −4,224 | 43,600 |
| `xian.TR2` | 524,080 | 460,768 | 165,616 | 301,176 | 1,455,984 | 1,451,648 | −4,336 | 45,328 |

An ordinary `TR64_GAME=2` cook then produced **24/24 identical
pack SHA-256s** with all guards active (`fit/selected/`). `OPERA`
has the largest single-entry mount footprint, 46,512 of 49,152 B.
The same `MONASTRY` input with `TR64_GAME=1` still fails
`level resident total 2056160 exceeds arena 1507328`
(`fit/monastry-stock-refusal.log`).

## Cook before the row change

With the original 1,507,328 B level row the normal cook reported
`level resident total 1743360 exceeds arena 1507328` (Venice) and
`level resident total 1869504 exceeds arena 1507328` (Bartoli's).
A scratch invocation passed a larger budget to `n64_pack.level_layout`
only (1,900,544 B); the normal floor, assets, geometry, sound and I/O
checks and pack writer still ran. The two reports and the independent
pack ledger are `boat.json`, `venice.json` and `ledger.json`. The census
columns are from [the original PC census](tr2-tr3-census.md#native-budgets-and-streaming).

| Section | Venice census → actual (delta B) | Stored + pad B | Bartoli's census → actual (delta B) | Stored + pad B |
|---|---:|---:|---:|---:|
| Core, aligned sum of tables | 627,328 → 626,704 (−624) | 401,632 | 647,536 → 646,864 (−672) | 403,728 |
| TX64 | 586,728 → 582,912 (−3,816) | 382,800 | 618,472 → 613,568 (−4,904) | 404,480 |
| RG64 | 305,408 → 305,408 (0) | 128,240 | 375,920 → 375,920 (0) | 169,248 |
| MG64 | 228,328 → 228,328 (0) | 89,232 | 233,152 → 233,152 (0) | 93,760 |
| **Level row, aligned** | **1,747,808 → 1,743,360 (−4,448, −0.25%)** | | **1,875,088 → 1,869,504 (−5,584, −0.30%)** | |
| SN64, streamed, includes metadata | 2,416,224 → 2,416,224 (0) | 2,416,224 | 2,331,776 → 2,331,776 (0) | 2,331,776 |
| Pack header, directory and records | — | 59,216 | — | 58,512 |
| **Whole pack** | | **3,477,344** | | **3,461,504** |

The Core estimate used a wider room record. The current record puts two
extra words into its old tail (4 B per room plus a table alignment delta).
Each pack has a 370-entry sound map and ten zone arrays. The Core also has
7,446/9,319 sectors, 579/505 draw portals, 85/266 placed statics, 133/134
items, 77/77 movable models, and 493/496 animations with 433,656/443,442 B
of original frame words, in Venice/Bartoli's order throughout.

TX64 stores 479,658/506,159 B of CI4 pixels (−256/−745 B against the
estimate), 1,087/1,151 clusters, and 998/1,047 palettes after deduplication
(−3,560/−4,160 B against one palette per cluster). It carries 23/21 split
lines, and 32/12 bytes including the texture-animation count words.
Both packs are full resolution. Neither has authored room sprites; the
182/185 sprite descriptors support other consumers and stay in the pack.

| Pack | SHA-256 |
|---|---|
| `boat.pack` | `2ad67892c1369ab1ef2c6ad6d25a49785286e4dbdc5be5980b79ff40fd1abc78` |
| `venice.pack` | `f6070e82f7ecc9fc52b269fc1d2a100d3ad325f5fccf3e808e0a4cd55e876188` |

## Admission and fixed bounds

The changed budget is the total level row, not four section-specific caps.
The old row falls short by 236,032 B and 362,176 B. Bartoli's still fails
at 1,825 KiB: `level resident total 1869504 exceeds arena 1868800`
(`one-kib-short-cook.log`). The minimum integral-KiB row is 1,826 KiB,
which leaves 126,464 B and 320 B free, respectively. Passing the exact
scratch packs to the unmodified host engine reproduced independent
loader refusals (`boat/stock-loader-engine.log`,
`venice/stock-loader-engine.log`): `pool level overflow level=boat
section=rooms request=305408 reserved=1209616 budget=1507328` and
`pool level overflow level=venice section=rooms request=375920
reserved=1260432 budget=1507328`. All four resident section sizes are
checked before binding; the running total crosses the old row at RG64
in each case, not at textures, sound, or I/O.

| Other bound | Venice | Bartoli's | Existing ceiling and diagnostic if exceeded |
|---|---:|---:|---|
| Pack mount records / peak I/O bytes | 924 / 46,000 | 913 / 45,648 | 49,152 B; `pack <name> section table: section too large for pool io` (native); cook names required bytes |
| Command / overlap scratch B | 3,218 / 432 | 3,312 / 506 | 49,152 B per validation phase; `core <table>: validation scratch exceeds io pool` |
| Sound metadata B / stream blocks | 4,832 / 663 | 4,928 / 644 | 8,256 B / 1,024; `sound section: metadata prefix capacity/offset` or `stream index capacity` |
| Sound sample slots / missing | 149 / 0 | 152 / 0 | 256 / 0; cook counts the slots, native verifies the retained stream and references |
| Renderer bind coordinate test, `3 × sum of world AABB axis spans + 256` | 453,376 | 287,506 | 786,432; `render bind code=6 section=0 offset=0` |
| Room/mesh object count | 155 / 316 | 166 / 337 | 65,535 for each section in the geometry writer; `geometry table exceeds u16` |
| Room/mesh batch count | 234 / 354 | 291 / 370 | 65,535 each; same geometry diagnostic |
| Room/mesh run count | 1,832 / 2,386 | 1,946 / 2,402 | 65,535 each; same geometry diagnostic |
| Maximum encoded CI4 tile image in TMEM | 2,048 B | 2,048 B | 2,048 B; `textures: split-line order/TMEM extent` |

The I/O peak uses the directory (48 B), 32 B of retained metadata per
record, and a 16,384 B window. The native validation scratch is used
*after* releasing the mount, not added to its peak. TR2 floor programs
are checked by the strict source cook and carried opaque by the posed
loader (it checks sector references into the table, not floor programs).
TR2 sound uses the idle frequent-effects cache for metadata validation;
it does not start a sample bank or need an audio row increase.

The legacy TR1 limits are not the TR2 limits. In particular, 155/166
rooms exceed 139, 1,727/1,774 object and 182/185 sprite descriptors exceed
1,536 and 180, 370 sound-map entries and ten zone arrays differ from
TR1's exact 256 and six, sound metadata exceeds the old 4,096 B prefix,
and the models reach type 254 (TR1's last ID is 190). The placed-item
maxima are 244/253. These are not new render blockers: the per-entry TR2
tag selects the existing bounds generated from `n64_validate.py`; the
legacy numbers and checks do not change.

### Gameplay room pairs (not on the posed path)

The other real 139-room limit is `game/world.c:roomPairs`: its error is
`game room capacity`, before initializing the cache. Both counts would
hit that guard **if** admitted to the game world (the first case needs
3 authored pairs, the second 14). They are not admitted: the route has
no `game` directive and `engine/test/route.c:tr64_scene_begin` does not
call `game_level_start`. The renderer uses its own level-sized mate
array, `uint16_t[rooms]`, in the existing workspace.

When these maps are admitted for gameplay, replace the inline
`GameState.roomMate[139]` and `roomAlternate[139]` in `game/internal.h`
with pointers (8 B in the host/cart ABI). In `game/game.c:levelStart`,
include a single `align4(2 * roomsCount)` span in the existing contiguous
state reservation and bind the pointers to its two `roomsCount`-byte
slices. Resize the initial fills in `game/world.c:roomPairs` to the level
count, retaining the original derivation and the 255 no-mate sentinel.
Loader admission already ensures a room index cannot equal 255. Keep
`roomPairsReady` in the zeroed slab: a new load frees it with the state
row; a save restore reuses and zeros the same sized slab, rebuilds the
pointers and derives the pairs from the same Core. Do not allocate anew
on a flip or save restore; no room-pair bytes belong in the save file.

The arrays would need **310 B** rounded to 312, and **332 B**,
respectively. On the current 32-bit layout, the span between
`unlinkedItems` and `effect` shrinks from 280 to 12 B, including
`roomPairsReady` and alignment: +44/+64 B in the slab request before
16-byte alignment of the next state-row allocation. Re-measure the full
state row once the gameplay consumer exists.
The proposed room-pair change is not in the render-only patch. The
census's total state costs of 161,808/172,496 B are gameplay projections,
not a budget guarantee for a shipping TR2 game.

## Row policy

The largest level is **Barkhang Monastery**, at 2,056,160 B (just over
2,008 KiB), not `SKIDOO`. The 1,826 KiB experimental row would refuse
`DECK`, `SKIDOO`, `EMPRTOMB` and `MONASTRY`, by 17,200 B, 33,824 B,
150,064 B and 186,336 B respectively.

Allow at least 32 KiB (two 16 KiB native block sizes) above the measured
largest full-resolution pack, then round up to preserve the existing
8 KiB cache phase of the following rows. This gives a **2,040 KiB**
level row (2,088,960 B), with **32,800 B** headroom on `MONASTRY`.
The **296 KiB** gameplay state row is provisional: 3,984 B above the
census's 299,120 B projection for `SKIDOO`, not measured gameplay use.
The **second game's gameplay image is unbudgeted**. The 608 KiB image
row has 61,720 B free in the render-only poser (560,872 B used), not
in a playable sequel. About 10 KiB of TR2 rule text existed at the
M10 step-2 measurement, with most gameplay still to come. Under D17
a sequel cart drops the first game's rule code; how much it drops,
and what the net playable image costs, have not been measured.
The 70 KiB reserve is unassigned. None of these numbers proves
room for the remaining gameplay.

| Budget (KiB) | Default/TR1 | TR2 |
|---|---:|---:|
| Image / stack / framebuffers / Z | 608 / 64 / 300 / 150 | same |
| Code / GPU / audio / library heap / I/O | 96 / 232 / 128 / 64 / 48 | same |
| Level | 1,472 | 2,040 |
| State | 192 | 296 |
| Total / reserve | 3,354 / 742 | 4,026 / **70** |

The 70 KiB reserve is 71,680 B. Both enlarged rows are multiples of
8 KiB; they keep state and I/O at the first game's cache indices, not
necessarily at the same physical addresses. There is no level that
needs texture downscaling or the Expansion Pak for resident size, so
no half-scale variant was made.

`TR64_GAME` defaults to `1`. An explicit `2` selects
`include/tr64/pools-tr2.def` at C compile time; there is no cart runtime
branch. This is the selector D17's future rule family can fold on.
The allocator, cook's arena/I/O guards, scene verifier, size gate and
linked hot-pool placement check all consume the selected table.
Content and build identity record the selection. A host executable
carries both tables; only the selected cart build supplies the layout
calibration, not the limit for either game. After CRC and directory
validation, the mounted level's game tag selects its own level/state
sizes before any resident reservation. Only that completed directory
is moved to the relocated I/O base. Remounts retain the live layout;
level release restores the stock no-level rows. The fixed/boot-owned
rows and I/O capacity are common.

On a cart the cold loader refuses a mismatched selected entry by name
before reading the section table. Shared header/directory-only music
and video lookup does not select or allocate a level. The engine
refuses gameplay routes on the TR2 cart before game startup; the
runner and the `TR64_PLAY=1` build also refuse them.

The C build and the Python linker gates enforce the total declared
rows separately from physical placement. The difference from
4,194,304 B is explicitly reported as unassigned reserve. The
placement gate uses the actual cache-aligned image end and the
addresses of fixed rows, not the image allowance, to check overlap.

## What the posed draw covers

The existing draw submits portal-clipped world geometry, placed
statics, and any authored room sprites (neither pack has them).
Venice has 38 water-flagged rooms and Bartoli's 17; the cook bakes the
existing underwater-room vertex tint, and the renderer uses its
existing mesh tint for statics in those rooms. That is not the retail
water surface/wave/refraction, underwater fog, animated lights, or
underwater gameplay. The pose does not advance the texture animation
clock. Both packs contain one mesh in TR2 skybox model slot 254
(29/23 rooms have the `0x0008` flag); the render-only scene does not
draw the sky dome. Sky can therefore appear black. No Lara, vehicles,
moving enemies/items, HUD, triggers or sound playback run here.

## Portal-clipped workload search

The grid uses the Great Wall pose method: 1,024-unit sector centres,
excluding walls and lateral transfer cells, with signed slants used for
local floor and ceiling. Dry grounded eyes stand 762 units above the
floor and clear the ceiling by more than 64 units. High and mid-height
eyes sample long spans; a deepest-cell midpoint covers rooms with no
other sample. These are posed viewpoints, not routes Lara can play.
All physical rooms (including alternates) are considered as starting
rooms; rendering uses the unflipped state. For each position test eight
compass yaws and pitches of nominal −26.6°, 0°, and +26.6°.

| Map | Rooms | Ground | Mid | High | Fallback | Posed views |
|---|---:|---:|---:|---:|---:|---:|
| Venice | 155 | 666 | 803 | 803 | 73 | 56,280 |
| Bartoli's | 166 | 1,528 | 1,057 | 1,057 | 38 | 88,320 |

`measure.py` links the same loader, `tr64_render_camera`, portal
visibility, batch culling and GPU submission code with the optional
host rasterizer left unbound. It calls the normal `tr64_render_frame`;
it does not estimate visibility from portal connectivity. The
authored start pose is also drawn by both rasterizing backends as a
positive control. Each selected row is replayed through the regular
scene harness with the exact pack, eye and look vector. `poses.py`,
`submit.c`, `rank.py`, and each map's `poses.in`, `poses.json`,
`views.csv`, `rank.txt`, and `ranking.json` hold the inputs and results.

Percentiles weight each sampled view once (ordinary median, nearest-rank
p95). Triangles are counted before RSP clipping. Statics are visible
definition instances visited in visible rooms, before batch culling.
Record bytes are the raw RDP records, separate from the 20-byte
per-triangle command stream. The one-view caps are 65,536 record
bytes and 256 matrix slots; the extrema are of the sampled grid, not
proof of bounds over all possible poses.

| Counter | Venice median / p95 / max | Bartoli's median / p95 / max |
|---|---:|---:|
| Visible rooms | 4 / 12 / 27 | 3 / 13 / 34 |
| Statics visited | 4 / 15 / 31 | 6 / 31 / 53 |
| Triangles | 284 / 1,187 / 2,124 | 339 / 1,449 / 2,593 |
| Texture uploads | 36 / 154 / 272 | 29 / 158 / 322 |
| RDP record bytes | 3,352 / 13,504 / 24,536 | 2,704 / 14,264 / 28,312 |
| Matrix slots | 3 / 16 / 38 | 3 / 19 / 53 |

Record/matrix maxima are 37.44%/14.84% of capacity for Venice and
43.20%/20.70% for Bartoli's. No view hit either hard cap. **23 of
88,320** Bartoli poses exceed the old 2,500-triangle *estimate* in
architecture §4.2 (Venice: 0 of 56,280). There is no rejection at that
triangle count; the actual gates are for storage and frame time.
Rank ties use ascending input ID.

### Venice top ten

| Tri rank | View | Camera room | Visible / statics | Triangles / uploads | Records B / slots |
|---:|---:|---:|---:|---:|---:|
| 1 | 6340 | 19 | 22 / 31 | 2,124 / 271 | 24,536 / 27 |
| 2 | 6532 | 19 | 22 / 31 | 2,120 / 269 | 24,408 / 27 |
| 3 | 44123 | 122 | 21 / 18 | 2,068 / 256 | 21,984 / 25 |
| 4 | 6364 | 19 | 23 / 30 | 2,063 / 271 | 24,496 / 29 |
| 5 | 9749 | 25 | 20 / 31 | 2,058 / 262 | 23,704 / 29 |
| 6 | 39341 | 120 | 21 / 18 | 2,050 / 270 | 23,360 / 26 |
| 7 | 54797 | 145 | 22 / 18 | 2,049 / 272 | 23,880 / 27 |
| 8 | 44075 | 122 | 21 / 18 | 2,048 / 253 | 21,792 / 24 |
| 9 | 44099 | 122 | 21 / 18 | 2,048 / 253 | 21,792 / 24 |
| 10 | 44171 | 122 | 20 / 18 | 2,040 / 254 | 21,704 / 25 |

| Upload rank | View | Camera room | Visible / statics | Triangles / uploads | Records B / slots |
|---:|---:|---:|---:|---:|---:|
| 1 | 39893 | 120 | 19 / 18 | 1,903 / 272 | 23,376 / 25 |
| 2 | 54797 | 145 | 22 / 18 | 2,049 / 272 | 23,880 / 27 |
| 3 | 6340 | 19 | 22 / 31 | 2,124 / 271 | 24,536 / 27 |
| 4 | 6364 | 19 | 23 / 30 | 2,063 / 271 | 24,496 / 29 |
| 5 | 39341 | 120 | 21 / 18 | 2,050 / 270 | 23,360 / 26 |
| 6 | 6532 | 19 | 22 / 31 | 2,120 / 269 | 24,408 / 27 |
| 7 | 54821 | 145 | 22 / 18 | 1,981 / 265 | 23,192 / 25 |
| 8 | 6556 | 19 | 21 / 30 | 2,009 / 264 | 23,984 / 25 |
| 9 | 39317 | 120 | 20 / 18 | 1,915 / 263 | 22,904 / 23 |
| 10 | 9749 | 25 | 20 / 31 | 2,058 / 262 | 23,704 / 29 |

### Bartoli's Hideout top ten

| Tri rank | View | Camera room | Visible / statics | Triangles / uploads | Records B / slots |
|---:|---:|---:|---:|---:|---:|
| 1 | 6213 | 22 | 18 / 40 | 2,593 / 232 | 19,560 / 46 |
| 2 | 16629 | 52 | 18 / 40 | 2,593 / 232 | 19,416 / 46 |
| 3 | 7061 | 29 | 20 / 42 | 2,579 / 246 | 21,656 / 42 |
| 4 | 17357 | 56 | 20 / 42 | 2,579 / 245 | 21,552 / 42 |
| 5 | 8145 | 36 | 15 / 52 | 2,572 / 219 | 18,536 / 38 |
| 6 | 4539 | 22 | 18 / 51 | 2,571 / 234 | 20,312 / 35 |
| 7 | 14955 | 52 | 18 / 51 | 2,571 / 234 | 20,168 / 35 |
| 8 | 6189 | 22 | 17 / 40 | 2,567 / 229 | 19,312 / 45 |
| 9 | 16605 | 52 | 17 / 40 | 2,567 / 229 | 19,120 / 45 |
| 10 | 4299 | 22 | 17 / 35 | 2,566 / 230 | 19,520 / 36 |

| Upload rank | View | Camera room | Visible / statics | Triangles / uploads | Records B / slots |
|---:|---:|---:|---:|---:|---:|
| 1 | 55204 | 130 | 14 / 41 | 2,540 / 322 | 28,312 / 24 |
| 2 | 82948 | 147 | 14 / 37 | 2,533 / 322 | 28,120 / 23 |
| 3 | 55228 | 130 | 13 / 41 | 2,557 / 315 | 27,808 / 25 |
| 4 | 59525 | 135 | 11 / 35 | 2,402 / 315 | 27,600 / 24 |
| 5 | 74789 | 146 | 11 / 35 | 2,400 / 315 | 27,600 / 24 |
| 6 | 46846 | 120 | 13 / 41 | 2,423 / 311 | 28,120 / 32 |
| 7 | 60509 | 135 | 13 / 38 | 2,397 / 310 | 27,056 / 23 |
| 8 | 75749 | 146 | 13 / 38 | 2,395 / 310 | 27,056 / 23 |
| 9 | 59597 | 135 | 11 / 35 | 2,422 / 307 | 27,184 / 21 |
| 10 | 74861 | 146 | 11 / 35 | 2,420 / 307 | 27,184 / 21 |

## Original Venice and Bartoli pose survey (experimental shared row)

The replay set is the union of each map's top five triangle and top
three upload views, plus the authored Lara start as a posed control.
The start is only the authored XYZ and forward yaw, not the gameplay
camera. All poses use the regular `--pose --render-measure` scene
through ares on <remote-host>, no Expansion Pak. (`placement` requires both
hardware and SDK to report 4,194,304 B and validates the carve.) A
pose's path is `<level>/view-<id>/`, or `<level>/authored-start/` for
`start`; `ares/ares-test-0000.ppm` and `host/boot.ppm` are the pictures.
`<level>/cart-measurement.json` retains frame counts, times and image
hashes, and `<level>/contact.png` is the side-by-side contact sheet.

`Load` is Core+TX64+SN64 metadata (`level-load total_us`); `Scope`
includes geometry reads, bind, first draw and code-unit transitions
(`load-scope total_us`). `FRM` goes from back buffer to GPU completion;
`RND` is CPU render submission, and `GPU` is the final wait. Numbers
are guest microseconds, not elapsed host time. Subphases overlap;
do not add them. RGB MAE is the mean absolute channel error divided
by 255; SSIM uses the existing 8×8 luminance-block diagnostic.
These compare renderers, not a visual-parity verdict.

### Cart pool peaks

Every measured pose in a map had the same final used/high-water row
values below. `Used` is after the load unit is released. Stack/boot
and framebuffer/depth rows account for the fixed reservations, not
touched bytes. Reserve is not a pool.

| Cart row | Budget B | Venice used / peak B | Bartoli's used / peak B |
|---|---:|---:|---:|
| Image | 622,592 | 560,872 / 560,872 | 560,872 / 560,872 |
| Stack + boot | 65,536 | 65,536 / 65,536 | 65,536 / 65,536 |
| Framebuffers | 307,200 | 307,200 / 307,200 | 307,200 / 307,200 |
| Z buffer | 153,600 | 153,600 / 153,600 | 153,600 / 153,600 |
| Code frames | 98,304 | 0 / 36,864 | 0 / 36,864 |
| GPU | 237,568 | 230,608 / 230,608 | 230,608 / 230,608 |
| Audio | 131,072 | 127,584 / 127,584 | 127,584 / 127,584 |
| Library heap | 65,536 | 1,328 / 1,328 | 1,328 / 1,328 |
| Level | 1,869,824 | 1,743,360 / 1,743,360 | 1,869,504 / 1,869,504 |
| State | 196,608 | 11,456 / 11,456 | 12,032 / 12,032 |
| I/O | 49,152 | 0 / 46,000 | 0 / 45,648 |
| Unassigned reserve | 397,312 | — | — |

State counts only the renderer workspace; the sound metadata uses
existing audio memory, without enabling playback. Code peak is the
nine 4 KiB load pages. No I/O lease remained.

### Venice (eight cart/host pairs)

The two rankings overlap at view 6340, giving seven ranked replays
plus the authored start. The slowest was ground view 9749
(**15.046 ms**), neither the triangle nor the upload peak. The
position at the authored start is room 0, `(58880, −2304, 22016)`,
looking toward −Z. Both rasterizers reproduced the sweep counts.
The contact sheet shows corresponding canal walls, masonry and
water-surface textures on both; texture/light and edge details differ.

| View | Load µs | Scope µs | FRM µs | RND µs | GPU µs | Rooms / statics | Tris / uploads | Records B / slots | MAE / SSIM |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| start | 638,958 | 1,053,849 | 2,112 | 1,880 | 232 | 2 / 0 | 224 / 32 | 3,456 / 2 | 0.0214 / 0.817 |
| 6340 | 638,827 | 1,065,885 | 13,918 | 13,842 | 76 | 22 / 31 | 2,124 / 271 | 24,536 / 27 | 0.0650 / 0.535 |
| 6532 | 638,812 | 1,066,323 | 14,384 | 14,291 | 93 | 22 / 31 | 2,120 / 269 | 24,408 / 27 | 0.0665 / 0.557 |
| 44123 | 638,736 | 1,066,050 | 14,013 | 13,535 | 478 | 21 / 18 | 2,068 / 256 | 21,984 / 25 | 0.0657 / 0.537 |
| 6364 | 638,768 | 1,065,843 | 13,898 | 13,810 | 88 | 23 / 30 | 2,063 / 271 | 24,496 / 29 | 0.0660 / 0.545 |
| 9749 | 638,524 | 1,066,830 | 15,046 | 14,987 | 59 | 20 / 31 | 2,058 / 262 | 23,704 / 29 | 0.0614 / 0.539 |
| 39893 | 638,745 | 1,065,831 | 13,803 | 13,341 | 462 | 19 / 18 | 1,903 / 272 | 23,376 / 25 | 0.0462 / 0.574 |
| 54797 | 638,519 | 1,065,641 | 13,767 | 13,585 | 182 | 22 / 18 | 2,049 / 272 | 23,880 / 27 | 0.0490 / 0.627 |

| View | Visibility µs | Cull µs | Scan µs | Submit µs | Expand µs | Tex setup µs | Matrix µs | Vertices µs | Recolour µs | Vertex wait µs |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| start | 292 | 86 | 69 | 1,016 | 468 | 230 | 74 | 9 | 0 | 0 |
| 6340 | 2,084 | 1,523 | 677 | 8,129 | 3,484 | 1,493 | 987 | 224 | 150 | 0 |
| 6532 | 2,027 | 1,522 | 667 | 8,643 | 3,925 | 1,441 | 938 | 251 | 150 | 0 |
| 44123 | 2,196 | 1,825 | 763 | 7,432 | 3,439 | 1,334 | 598 | 156 | 103 | 0 |
| 6364 | 2,178 | 1,554 | 677 | 7,973 | 3,584 | 1,566 | 664 | 216 | 152 | 0 |
| 9749 | 2,310 | 1,498 | 661 | 9,076 | 4,809 | 1,434 | 677 | 221 | 157 | 0 |
| 39893 | 1,864 | 1,756 | 984 | 7,591 | 3,040 | 1,516 | 591 | 369 | 287 | 0 |
| 54797 | 1,934 | 1,845 | 816 | 7,842 | 3,585 | 1,424 | 633 | 224 | 147 | 0 |

### Bartoli's Hideout (nine cart/host pairs)

The triangle and upload sets do not overlap. The slowest was ground
view 17357 (**16.042 ms**), versus 15.820 ms at triangle rank 1 and
15.759 ms at upload rank 1. Both rasterizers reproduced the sweep counts.
The authored start at room 102, `(73216, 4864, 19968)` looking toward
+Z, is dim on both backends. The ranked interiors show corresponding
chandeliers and walls; the exteriors show the same arched windows and
roof outlines on both, with differing textures/light and edge details.
No replay spilled RDP records or exceeded the 33.333 ms target.

| View | Load µs | Scope µs | FRM µs | RND µs | GPU µs | Rooms / statics | Tris / uploads | Records B / slots | MAE / SSIM |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| start | 662,672 | 1,140,660 | 2,414 | 2,099 | 315 | 1 / 0 | 252 / 14 | 1,296 / 1 | 0.0044 / 0.912 |
| 6213 | 662,045 | 1,153,991 | 15,820 | 15,724 | 96 | 18 / 40 | 2,593 / 232 | 19,560 / 46 | 0.0522 / 0.595 |
| 16629 | 662,054 | 1,153,821 | 15,633 | 15,577 | 56 | 18 / 40 | 2,593 / 232 | 19,416 / 46 | 0.0523 / 0.594 |
| 7061 | 662,294 | 1,154,182 | 15,921 | 15,827 | 94 | 20 / 42 | 2,579 / 246 | 21,656 / 42 | 0.0190 / 0.800 |
| 17357 | 662,310 | 1,154,311 | 16,042 | 15,891 | 151 | 20 / 42 | 2,579 / 245 | 21,552 / 42 | 0.0190 / 0.799 |
| 8145 | 662,106 | 1,153,605 | 15,431 | 15,326 | 105 | 15 / 52 | 2,572 / 219 | 18,536 / 38 | 0.0287 / 0.767 |
| 55204 | 662,616 | 1,154,015 | 15,759 | 15,690 | 69 | 14 / 41 | 2,540 / 322 | 28,312 / 24 | 0.0439 / 0.730 |
| 82948 | 662,457 | 1,153,733 | 15,537 | 15,474 | 63 | 14 / 37 | 2,533 / 322 | 28,120 / 23 | 0.0439 / 0.730 |
| 55228 | 662,608 | 1,153,705 | 15,440 | 15,250 | 190 | 13 / 41 | 2,557 / 315 | 27,808 / 25 | 0.0508 / 0.695 |

| View | Visibility µs | Cull µs | Scan µs | Submit µs | Expand µs | Tex setup µs | Matrix µs | Vertices µs | Recolour µs | Vertex wait µs |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| start | 212 | 84 | 37 | 1,361 | 1,054 | 130 | 48 | 9 | 0 | 0 |
| 6213 | 1,128 | 1,873 | 669 | 9,803 | 4,681 | 1,410 | 1,226 | 462 | 335 | 0 |
| 16629 | 1,129 | 1,869 | 654 | 9,659 | 4,681 | 1,365 | 1,224 | 460 | 334 | 0 |
| 7061 | 1,240 | 2,092 | 694 | 9,659 | 4,936 | 1,343 | 1,096 | 409 | 305 | 0 |
| 17357 | 1,236 | 2,066 | 702 | 9,775 | 5,068 | 1,357 | 1,081 | 410 | 305 | 0 |
| 8145 | 966 | 2,173 | 715 | 9,131 | 4,100 | 1,604 | 1,001 | 421 | 306 | 0 |
| 55204 | 1,082 | 2,776 | 1,155 | 8,718 | 3,120 | 1,747 | 646 | 613 | 415 | 35 |
| 82948 | 1,065 | 2,650 | 1,190 | 8,766 | 3,209 | 1,765 | 618 | 591 | 420 | 34 |
| 55228 | 1,015 | 2,732 | 1,074 | 8,564 | 3,204 | 1,711 | 655 | 541 | 335 | 35 |

## Replays with the selected TR2 table

The largest pack and each canal map's previously slowest view loaded
and drew through the ordinary scene harness on <remote-host> at 4 MiB. All
three matched host room, static, triangle, upload, record and matrix
counts; `fit/rom-pack-binding.json` authenticates the loaded packs
against the remote ROMs. The Venice and Bartoli cart pictures are
byte-identical to the corresponding original 4 MiB survey pictures.

| Map / view | Level peak B | State peak B | I/O peak B | Load / scope µs | FRM / RND / GPU µs | Tris / uploads | MAE / SSIM |
|---|---:|---:|---:|---:|---:|---:|---:|
| MONASTRY / authored start | 2,056,160 | 12,112 | 44,496 | 667,811 / 1,272,351 | 2,765 / 1,917 / 848 | 197 / 37 | 0.0032 / 0.943 |
| Venice / 9749 | 1,743,360 | 11,456 | 46,000 | 639,406 / 1,067,078 | 14,897 / 14,839 / 58 | 2,058 / 262 | 0.0614 / 0.539 |
| Bartoli's / 17357 | 1,869,504 | 12,032 | 45,648 | 661,613 / 1,152,360 | 16,121 / 15,947 / 174 | 2,579 / 245 | 0.0190 / 0.799 |

Other rows' budgets and peaks match the prior posed pool table.
The larger TR2 state row still contains only renderer workspace.
The level, state, and I/O start at `0x8010b000`, `0x80309000`, and
`0x80353000`; all are 4,096 modulo 8 KiB. The physical gap before the
fixed surfaces is 133,120 B for this linked scene image (includes
unused image allowance), distinct from the 71,680 B budget reserve.
The per-pose logs, phases and images are in
`fit/{monastry,boat,venice}/`; `fit/contact.png` puts cart and host
side by side, in that order, with matching dark Monastery geometry.

The revised patch reran Monastery using the default host and the
TR2 cart. Both used the entry's 2,088,960 B row and matched the render
counts above. The cart frame was 2,808 µs; Core/TX/SN load 664,657 µs,
scope 1,268,474 µs. `fit/review2/monastry-ares/` has the fresh 4 MiB
report. Its cart picture is byte-identical to the earlier Monastery
cart capture, not to the host image.

## First-game invariant

`fit/review2/bridge.json` compares the ordinary one-view bridge on
`a4a06a9` with the first row-table patch applied (before the new
game-admission guard) and after the guard. The pack, compiled route,
picture, every pool report including bases, and all 181 game hashes
match. Across 180 `scene-work` records, mean is **22.687 → 22.681 ms**
(−0.006 ms) and worst **26.327 → 26.300 ms** (−0.027 ms). The picture
SHA-256 is
`f8b6a0c9d5e1fcde6bc754c6c879a9d76b00093f4bf3b1533815117dc91bd122`.

TR1 retains its 1,507,328 B level and 196,608 B state budgets.
The prior one-tick cart loads (`tr1-level10b/`, `tr1-level3a/`)
measured the two TR1 high-water cases at 1,440,528 B for level
and 179,136 B for state. These observed peaks have 66,800 B
and 17,472 B of stock headroom, respectively.

Three distinct PC packs retained their byte identities with the game
selector at default (`fit/tr1-selected-packs.sha256`; historical exact
comparisons: `tr1-{stock,new}-packs.sha256`).

| TR1 pack | Unchanged pack SHA-256 |
|---|---|
| LEVEL1 | `ccdd12ad2ca6957ea341cfa14719f6c6abb969f5c1a328ae7552bd7c6f0c9da2` |
| LEVEL6 | `12f7b31b9a9db2757de8a198eed2bd4190cd748c37c691e260a2d5415bd9f984` |
| LEVEL10B | `49970228e3dd7ecece9e93c3073232975e8e8dfa0e2b6eb0289c196ee38d4fd1` |

All 25 solo fixtures (12 LEVEL1, 13 GYM) matched the immutable traces;
changing input diverged at tick 31 in both lanes, and both verifiers
rejected a changed production hash and a missing field
(`fit/review2/fixtures-level1.log`, `fit/review2/fixtures-gym.log`).

The shipping build (`TR64_PLAY=1 TR64_TEST=0`) passes at **601,880 B**
in its 622,592 B image row, with byte-identical resident `.text`, ELF
section addresses/sizes, and symbols outside `.tlbcode.load`.
The loaded byte deltas, from `fit/review2/tr1-elf-delta.json`, are:

| Section | Changed ranges |
|---|---|
| `.tlbcode.load` | 3,326 disjoint half-open byte ranges, 29,455 different bytes in `[0x032f,0x8748)`; exact offsets and before/after symbols in the JSON. `load_pack_poll` adds the guard, 2,820 → 2,844 B, plus moved functions, relocated branches/literals, the new diagnostic and padding changes. The unit is still 36,864 B at `0xc1180000`, nine pages. |
| `.data` | `[0x9253,0x9254)`, the one differing byte in the load dispatch word at `0x80089e90` (`0xc1188340` → `0xc1188360`). |
| `.rodata` | ID/digest strings only (20 exact differing ranges in the JSON); comparison with those strings masked is byte-identical. |
| `.tlbcode_id` | `[0,32)`, the packaging ID. |

The hot-region and render stack gates pass (16,288 / 16,384 B I-cache
window and 1,016 / 3,368 B entry stack depths), with all carved pools still at
4,096 modulo 8 KiB. This is not a full shipping route run.

### Why the rejected shared-row bridge peaked 1.220 ms later

The earlier 1,826 KiB experiment did **not** move the level arena
base or its resident section offsets for the same pack. It moved
state and I/O by `0x58800` (2 KiB in the 8 KiB direct-mapped D-cache),
and the new borrow byte count also changed generated instructions.
At the maximum in both older records (tick 152), immediate audio
service went **1,760 → 2,939 µs** (+1,179); game tick
**1,161 → 1,086 µs** (−75); render completion interval
**23,308 → 23,427 µs** (+119). These explain +1,223 µs of the
+1,220 µs measured difference, with a −3 µs unlabelled residual.
This is mainly **audio work landing on that frame**, not an increase
from level-arena cache placement. The route had the same game state
and submissions, but its pre-render diagnostic at that tick took
1,573 µs longer. That time is subtracted from `scene-work`; it still
advances the emulated audio clock. Across the route the altered
build logged 5,824 more output samples and 12,408 µs more mixer work
without underruns. The logs do not isolate mixer cache misses from
when blocks become due; the state/I/O phase shift is a plausible
contributor to timing, not an established cause. The selected TR2
rows deliberately preserve the stock cache indices. Evidence:
`main-bridge/engine.log` and `fixed-bridge/engine.log` in the parent
evidence directory, not the fresh pair under `fit/`.

## Reproduction and review

The freeze base is `a4a06a93bd73c42b8b28ab94f66ff434906b1e69`.
The revised review artifact and checksum are `fit/review2/runtime.patch`
and `fit/review2/runtime.patch.sha256`; the production changes, controls
and report are uncommitted. No game rules or world renderer changed.

Directed controls:

- `scene-controls.log` (under `fit/review2/`) rejects unknown
  `TR64_GAME=3` before the build, and independently checks a declared
  4,098 KiB table and a 4,096 KiB table that still overlaps the fixed
  rows after alignment. `declared-size-negative.log` shows the real-ELF
  size gate rejects 4,196,352 declared bytes against 4,194,304.
- `n64-core-tests` fills each game's exact arena across TX64 and RG64
  and refuses one extra raw byte at both the cook and native mount.
  An identical-size TR1 entry is rejected at the TR1 bound even when
  configured for TR2; a single host also switches TR1 → TR2 → TR1 in
  one mixed pack. Both host build selectors passed 113 native runs;
  see `tr1-fixtures-build.log`, `tr2-configured-host.log` and the
  corresponding `large-legacy-entry` / `mixed-game-switch` logs.
  `core-observable-controls.log` shows the verifier rejects I/O and
  code-page readings outside adjusted test rows, including the number
  of pages returned after load.
- `cart-controls.sh` builds directly with Make and runs through the
  ares slot harness, not the scene runner's argument checks.
  `tr2-refuses-tr1/` and `tr1-refuses-tr2/` show both game mismatches
  by level name; `tr2-refuses-game/` shows the matching TR2 pack
  refused for gameplay. Each has a fatal picture and exit 1, with
  no game tick.

Run from the selected-table worktree, using the pinned Makefile SDK
and toolchain:

```sh
export BUILD_DIR=$TR64_WORK/builds/T2VeniceFit2
export TMPDIR=$TR64_WORK/tmp/T2VeniceFit2
export TR64_ARES_HOST=<remote-host> TR64_TIMEOUT_SCALE=8
export PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=tools/cook:test/run
export TR64_GAME=2
unset ARES_EXPANSION_PAK
rtk mkdir -p "$BUILD_DIR" "$TMPDIR"
D=$TR64_DATA/pc-tr2
E=$TR64_WORK/evidence/m10/venice/fit
for source in "$D"/*; do
    [[ ${source,,} == *.tr2 ]] || continue
    level=${source##*/}; level=${level,,}; level=${level%.tr2}
    rtk python3 tools/cook/pack_level.py --source pc \
      --input "$source" --main-sfx "$D/MAIN.SFX" \
      --out "$BUILD_DIR/$level.pack" --cache $TR64_WORK/cache
done
for level in monastry boat venice; do
    rtk python3 "$E/replay.py" "$level"
done
```

For the historical 144,600-pose search and 17 replay pairs, the
parent evidence directory retains the original scripts and raw
logs; the experimental row is frozen there for reproduction, not
proposed for adoption. Use fresh output directories rather than
overwriting the captures. `verify-rom-pack.py` there verifies the ROM
receipt and hashes the actual pack bytes at `0x101000`; the host
reports its pack hash directly. No TR2 gameplay cart, real hardware,
combined campaign pack, continuous camera search, retail visual
parity, or exhaustive render walk of the 24 inputs was tested.
