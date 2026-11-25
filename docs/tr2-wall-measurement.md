# M10: The Great Wall at 4 MiB

This is a posed-render measurement, not Tomb Raider II gameplay support. The
same level loader and renderer serve both games. The stock 4 MiB rows already
fit the complete, full-resolution Wall pack; the proposed reserve transfer is
an A/B configuration, not required by this level. No half-scale option was
added. The main draw covers room geometry and placed static meshes. There are
**no authored room sprites** in `WALL.TR2`; see the separate sprite control.

Evidence paths below are relative to `$TR64_WORK/evidence/m10/wall/`.
The original inputs were read-only: `WALL.TR2` SHA-256
`a6e0acef3c28a0ca37b1262fb1040279ba4ef8f0efc95731065f181ebc3d1d3b`,
`MAIN.SFX` SHA-256
`74cd20e38dec5db3c31f21d2a5699f087f0cc15ba98e2e5c3333d0aef46f5be0`.

## Pack

`wall.pack` is **3,160,784 B**, SHA-256
`d5e1cb90646be0bdb94545e369eca0c118a5684a68cbcc9e2c454de73a59a02e`.
Its **1,406,320 B** level allocation has **101,008 B** free on stock rows.
The census's `WALL` row was an estimate/projection, not an existing pack:

| Section | Census B | Actual B | Actual − census B | Cart payload + pad B |
|---|---:|---:|---:|---:|
| Core, sum of individually aligned tables | 535,392 | 535,056 | −336 | 361,632 |
| TX64 | 436,424 | 432,544 | −3,880 | 292,704 |
| RG64 | 226,552 | 226,552 | 0 | 105,376 |
| MG64 | 212,152 | 212,152 | 0 | 85,712 |
| **Level row, aligned** | **1,410,544** | **1,406,320** | **−4,224 (−0.30%)** | |
| SN64, streamed; metadata included | 2,260,944 | 2,260,944 | 0 | 2,260,944 |
| Pack header, directory and 849 physical records | not estimated | 54,416 | — | 54,416 |

Core is smaller because the two TR2 room fields fill the old four-byte tail
rather than adding a wider record (84 × 4 = 336 B). Core carries 84 rooms,
6,556 sectors, 236 portals, 82 lights, 52 placed statics, nine static mesh
definitions, 106 items, 68 movable models and 373 mesh-pointer slots, 389
animation records with 358,916 packed frame bytes, all ten zone arrays, and
the 370-entry sound map. Geometry matches the census sizes; that does not
make the projections byte-identical. Core uses 535,048 B of the 535,056 B
reservation; the difference is alignment, not discarded records.

TX64 contains 1,347 object and 183 sprite descriptors, 876 clusters, 793
deduplicated palettes, 21 split lines and one active four-member animation
range (12 bytes including the count words). It contains **349,998 B** of
CI4 image bytes (census 350,559, −561), 31,720 palette bytes, 14,016 cluster
bytes, 36,354 descriptor bytes, 42 split bytes, 264 flat-colour bytes, 12
animation bytes, 80 header bytes, and 58 alignment bytes. The original
estimate padded both sides and charged an undeduplicated palette per
cluster; the measured writer groups overlapping crops by horizontal-origin
parity. Cluster-local X stays even, odd widths are kept, and the I8 load
reads `ceil(width/2)` bytes while the actual CI4 tile clamps at `width−1`.
This needs neither a UV shift nor padded sprites. The synthetic `3×8`
crop test stores row `20 10`: high-nibble first, zero unused low nibble.
At `(6,7)`, the synthetic `3×5` sprite emits load words `f400c01c 0101002c`
and clamp words `f2000000 00008010`; at width 4 the load is unchanged and
the clamp end moves to `0000c010`. The real Wall has 10 odd-width sprites
and no odd-width object descriptor after origin-parity grouping.
These are cart encoder command-word assertions; the host backend counts
record bytes and samples pixels in software, but emits no RDP texture
words. Host/cart pictures therefore do not prove command-word parity.

The material source is the 16-bit ARGB1555 pages, not the 8-bit compatibility
pages. RGB555 precision feeds per-cluster CI4 quantisation; alpha classes
remain separate, RGB under zero alpha is dropped. Flat faces use the high
material byte's RGB888 palette entry reduced to RGB555 (OpenLara
`src/format.h:6454–6465,6530–6531`, `src/utils.h:1497–1505`). The cook uses
the second room vertex light word (`src/format.h:5283–5309`). The first word
and vertex attribute are retained by the source reader but not packed into
render vertices. The pack carries the two room ambient values, room light
mode, both lamp intensity/falloff pairs, and both static/item intensity words.
Static submission uses the second TR2 word, provisionally following OpenLara
`src/format.h:5527–5543`; it uses the first on TR1. Eight of Wall's 52 placed
statics differ between the two words. Neither this selection nor the existing
static shade/tint model is a claim about the retail renderer's dynamic light
modes, water or vertex attributes.

SN64 keeps the 137 referenced external sample slots (137 distinct ordinals;
no unused slots or missing sample descriptors for Wall). Existing SN64
records hold 624 raw blocks and a 4,448-byte metadata prefix. The original
stock cart stopped at `sound section: metadata prefix capacity/offset`
(`stock-old-snd-cap/engine.log`, no frame): that exceeds the legacy 4,096 B
sub-cap by 352 B, **not** the audio row. TR2 validates its prefix in the idle
frequent-effects cache; no bank is activated and no PCM sample is preloaded.
The audio row therefore stays at the measured TR1 cost. The stock host's next
failure was `render bind code=6 section=0 offset=0` (`TR64_RENDER_BOUNDS`):
the old check charged the biggest mesh (including movable scenery) around
every static instance. Its conservative bound was 973,312 vs 786,432.
Using each placed visible static's own quadrant-transformed geometry AABB
makes the same conservative formula 560,896, without enlarging the signed
coordinate domain. Movable meshes retain their separate submit-time check.

## Admission and limits

The per-entry game word uses the old reserved directory word (`0` legacy
TR1/catalog; `2` TR2 render-only). A repaired directory CRC does not make
unsupported tag `3` admissible (`wall-version-control.log`: `pack wall:
unsupported game version 3`). TX64 bit 0 must agree with that tag; explicit
cached textures are checked at the cook boundary too. Same container, layout
sizes and Core/TX64 ABI versions; there is no alternate reader or renderer.
The common admission definition is `tools/cook/n64_validate.py:11–28`, used
by the cook and generated into C by `tools/cook/n64_core.py:schema_header`.
Sequel render-only admission uses field widths and table spans, not limits
picked to match Wall. Core command/overlap scratch and mount table size are
charged to the same I/O row. The rechecksummed short-zones test reaches the
native loader and fails `core zones: zone array count differs from game
version`; the source-projection control rejects a missing zone word too.
The overlap validator charges one bit per word, including an unreferenced
tail. A synthetic chain padded to 393,216 words still fits both budgets;
one more word needs 49,153 B of scratch and fails by name in the cook,
even though its decoded core would fit the level row. Cook and loader
derive the I/O row from the same `include/tr64/pools.def`.

| Admission field | Stock TR1 | TR2 render-only | Max TR2 / TR3 across maps |
|---|---:|---:|---:|
| Rooms (sector links have an 8-bit sentinel) | 139 | 255 | 193 / 233 |
| Sector grid dimension | 255 | 65,535 | 20 / 20 |
| Per-room portals/lights/static instances (source u16) | 255 | 65,535 | 46 / 41 |
| Per-room room sprites | 65,535 | 65,535 | 4 / 0 |
| Aggregate sectors / draw portals | arena | arena | 15,973 / 15,039; 710 / 694 |
| Items | 256 | arena | 240 / 248 |
| Models | 191 | 65,536 | 87 / 143 |
| Largest item/model ID (inclusive) | 190 | 65,535 | 264 / 373 |
| Mesh-pointer slots | 512 | arena | 468 / 551 |
| Mesh count per model | 127 | 65,535 | 31 / 29 |
| Static definitions | 50 | 65,536 | 47 / 50 |
| Largest static ID (inclusive) | 49 | 65,535 | 49 / 49 |
| Cameras/sinks | 16 | arena | 30 / 81 |
| Boxes (14-bit overlap targets) | 1,024 | 16,384 | 1,999 / 1,544 |
| Zone arrays (exact, each box-count long) | 6 | 10 | 10 / 10 |
| Animation records | 65,535 | 65,536 | 515 / 651 |
| Largest state ID / count of ranges per state | 255 / 255 | 65,535 / 65,535 | 70 / 88; 5 / 6 |
| Cinematic frames | 65,535 | 65,535 | 780 / 780 |
| Sprite sequences | 65,535 | arena | 37 / 7 |
| Object descriptors | 1,536 | 65,535 | 2,016 / 2,406 |
| Sprite descriptors | 180 | 65,535 | 202 / 161 |
| Combined descriptor IDs (excludes sentinel) | 65,535 | 65,535 | 2,218 / 2,553 |
| Clusters / palettes | 65,535 each | 65,536 each | ≤1,342 / ≤1,169 each |
| Tile split lines | 65,535 | 65,535 | 31 / 376 |
| Sound map entries (exact) | 256 | 370 | 370 / 370 |
| Sound sources / playback emitters | 65,535 / 64 | arena / no playback | 16 / 36 |
| Sample slots / missing references | 256 / 256 | 256 / 0 | 155 / 145; 0 / 0 |
| SN64 metadata bytes / raw stream blocks | 4,096 / 1,024 | 8,256 / 1,024 | 5,024 / 4,704; 672 / 913 |
| Command / overlap validation scratch bytes | 49,152 | 49,152 | 3,618 / 5,042; 1,589 / 1,301 |

The maxima use the 19 TR2 and 21 TR3 maps, not titles or cutscenes, from
`../census/pc.json` and source-reader recounts (`bounds.py`, `bounds.json`).
The cluster/split calculation groups rectangle origin parities as the TR2
cook does; palette counts are bounded by clusters and may be lower after
deduplication. Offsets, spans, per-rectangle TMEM, decoded level size, I/O
mount table and GPU budgets still apply. The table is not a claim that any
other map cooks, or that TR3 is admitted. For Wall, 849 retained entries
plus directory and 16,384-byte mount staging need 43,600 B of the 49,152 B
I/O row. The unchanged one-view matrix and command-record budgets remain
256 matrices and 65,536 B; censused potential visibility is not a frame
command-record count.

## Typical close views on the cart

Used `test/run/scene.sh --ares --pose ... --render-measure`, through the
<remote-host> harness slots (`TR64_ARES_HOST=<remote-host> TR64_TIMEOUT_SCALE=8`).
Guest `hardware_rdram=4194304 sdk_rdram=4194304` and the pool-placement
gate passed. `used / peak` below is the final scene report, after releasing
the load code unit; the code row needs nine pages while loading. The stack
and framebuffer rows account for their entire fixed reservations, not a
measurement of touched stack bytes. Reserve is not a pool.

| Cart row | Stock budget B | Split budget B | Used / peak B (both) |
|---|---:|---:|---:|
| Image | 622,592 | 622,592 | 560,872 / 560,872 |
| Stack + boot | 65,536 | 65,536 | 65,536 / 65,536 |
| Framebuffers | 307,200 | 307,200 | 307,200 / 307,200 |
| Z buffer | 153,600 | 153,600 | 153,600 / 153,600 |
| Code frames | 98,304 | 98,304 | 0 / 36,864 |
| GPU | 237,568 | 237,568 | 230,608 / 230,608 |
| Audio | 131,072 | 131,072 | 127,584 / 127,584 |
| Library heap | 65,536 | 65,536 | 1,328 / 1,328 |
| Level | 1,507,328 | 2,060,288 | 1,406,320 / 1,406,320 |
| State | 196,608 | 303,104 | 8,144 / 8,144 |
| I/O | 49,152 | 49,152 | 0 / 43,600 |
| Unassigned reserve | 759,808 | 100,352 | — |

The state figure is only renderer workspace (room queue/clips, geometry
run spans, sprite mapping and texture animation data); no item instances,
nav, triggers, save buffer or other game state were installed. The census's
183,744-byte state estimate includes a gameplay projection, so subtracting
the poser figure does **not** show that a playable Wall needs only 8 KiB.
The inherited audio reservation includes the boot mixer and music buffers
even though Wall neither activates the mixer nor binds an effects bank.

The map's unclipped portal graph gives the same bound of 84 rooms and
11,793 authored room triangles from every unflipped start. The five added
poses break that tie by starting in the largest individual rooms by
authored triangles: 65 (619), 36 (598), 7 (501), 59 (491), 74 (396).
The report `cameras.json` gives absolute world XYZ eye and look targets.
Lara's eye uses the authored type-0 placement exactly
`(62976,1536,23040)` in room 35, looking along her start yaw (+Z).

Core/TX/SN64 metadata load time is `level-load total_us`; the outer
`load-scope total_us` also covers the RG/MG remount, geometry binding and
the initial draw, plus the loading-unit transition. `FRM` measures from
the acquired back buffer to GPU completion; `GPU` is the wait after the
last CPU submission. All timings are cart microseconds, not wall time.
The reserve-split run was measured before the final second-static-shade
selection and shared-cap cleanup; stock is the final-render implementation.
The two rows are evidence of fit, **not** a controlled frame-time A/B of
the reserve transfer. No count or frame-time threshold was changed.

| Row config | Camera | Core/TX/SN load | Scope incl. draw | FRM | RND | GPU | Rooms / statics | Tris / texture uploads |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| Stock | Lara, r35 | 517,360 | 866,103 | 4,518 | 4,100 | 418 | 3 / 0 | 706 / 80 |
| Stock | r65 | 517,163 | 865,144 | 3,810 | 3,730 | 80 | 2 / 4 | 590 / 65 |
| Stock | r36 | 517,277 | 866,395 | 4,841 | 4,223 | 618 | 2 / 0 | 691 / 83 |
| Stock | r07 | 517,338 | 869,325 | 7,682 | 7,531 | 151 | 7 / 2 | 1,153 / 156 |
| Stock | r59 | 517,182 | 867,782 | 6,364 | 5,978 | 386 | 4 / 16 | 881 / 68 |
| Stock | r74 | 517,214 | 867,151 | 5,758 | 5,675 | 83 | 3 / 19 | 850 / 44 |
| Split | Lara, r35 | 515,064 | 864,874 | 4,522 | 4,078 | 444 | 3 / 0 | 706 / 80 |
| Split | r65 | 514,587 | 863,709 | 3,909 | 3,851 | 58 | 2 / 4 | 590 / 65 |
| Split | r36 | 514,528 | 864,587 | 4,825 | 4,211 | 614 | 2 / 0 | 691 / 83 |
| Split | r07 | 514,574 | 867,522 | 7,669 | 7,519 | 150 | 7 / 2 | 1,153 / 156 |
| Split | r59 | 514,519 | 866,161 | 6,380 | 5,982 | 398 | 4 / 16 | 881 / 68 |
| Split | r74 | 514,523 | 865,431 | 5,661 | 5,553 | 108 | 3 / 19 | 850 / 44 |

Render subphases are measured by the normal renderer. Columns overlap
(for example expansion and texture setup occur during submission); do not
sum them as a frame total. Vertex wait was zero on every listed frame.

| Config | Camera | Visibility | Cull | Scan | Submit | Expand | Tex setup | Matrix | Vertices | Recolour |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Stock | Lara | 216 | 256 | 256 | 2,979 | 1,535 | 486 | 244 | 15 | 0 |
| Stock | r65 | 128 | 351 | 371 | 2,415 | 1,314 | 435 | 71 | 13 | 0 |
| Stock | r36 | 172 | 247 | 213 | 3,216 | 1,557 | 836 | 74 | 13 | 0 |
| Stock | r07 | 679 | 547 | 425 | 5,355 | 2,621 | 979 | 238 | 107 | 65 |
| Stock | r59 | 383 | 731 | 227 | 3,923 | 2,257 | 494 | 338 | 165 | 99 |
| Stock | r74 | 264 | 824 | 193 | 3,632 | 2,011 | 407 | 396 | 184 | 140 |
| Split | Lara | 215 | 254 | 239 | 2,989 | 1,708 | 490 | 101 | 15 | 0 |
| Split | r65 | 126 | 325 | 209 | 2,732 | 1,471 | 432 | 74 | 12 | 0 |
| Split | r36 | 170 | 202 | 197 | 3,283 | 1,465 | 884 | 210 | 14 | 0 |
| Split | r07 | 685 | 537 | 421 | 5,359 | 2,633 | 1,131 | 237 | 104 | 62 |
| Split | r59 | 377 | 736 | 206 | 3,933 | 2,095 | 489 | 327 | 321 | 280 |
| Split | r74 | 259 | 801 | 187 | 3,518 | 2,007 | 399 | 398 | 179 | 134 |

## Pictures

For each run directory, the pictures are `ares/ares-test-0000.ppm` and
`host/boot.ppm`; the latter uses the same pack and renderer inputs. Float
pictures are `float/<camera>.ppm`. `measurement.json` has hashes, raw phase
metrics and comparisons for both budget configurations.

| Camera | Stock run | Split run | Split cart vs host MAE / SSIM | Split cart vs float MAE / SSIM |
|---|---|---|---:|---:|
| Lara | `stock-final/wall-lara/` | `split/wall-lara/` | 0.0135 / 0.806 | 0.0209 / 0.719 |
| r65 | `stock-final/wall-r65/` | `split/wall-r65/` | 0.0261 / 0.677 | 0.0343 / 0.619 |
| r36 | `stock-final/wall-r36/` | `split/wall-r36/` | 0.0508 / 0.451 | 0.0585 / 0.403 |
| r07 | `stock-final/wall-r07/` | `split/wall-r07/` | 0.0186 / 0.790 | 0.0279 / 0.706 |
| r59 | `stock-final/wall-r59/` | `split/wall-r59/` | 0.0371 / 0.582 | 0.0371 / 0.596 |
| r74 | `stock-final/wall-r74/` | `split/wall-r74/` | 0.0262 / 0.613 | 0.0272 / 0.595 |

MAE is mean absolute RGB difference divided by 255 over 320×240×3
channels; SSIM is the existing 8×8 luminance-block diagnostic. Neither is
a visual parity verdict. Host and cart agree on the logged submission
counts (including visible rooms, statics, triangles, texture uploads and
command bytes) but not every pixel. OpenLara `2333adef` was built with a
scratch pose/capture patch only (`float/posed-capture.patch`): same source
level, camera room, eye, target, focal 256 and near/far 64/10,240. Sky,
moving entities, HUD and startup movies were suppressed to compare the
world; OpenLara still uses its own atlas, filtering, lighting,
water/fog and portal renderer. The contact sheet `float-contact.png`
shows recognizably corresponding geometry, with visible lighting, clipping
and water differences, especially r36. This is not retail parity evidence.

A separate control (`sprite-control.py`, `wall-sprite.route`) places
one existing Wall sequence sprite, source index 63 (31×31), 304 units
ahead of the Lara-start camera. It does not alter the measured Wall pack.
Both renderers report one room sprite and one tile. `sprite-control/pair.png`
shows the stepped grey glow tapering into the background at its right edge
in both pictures, with no added vertical stripe from the spare low nibble.
This is a visual control, not a claim that Wall has an authored room sprite.

Reproduction: `tools/cook/pack_level.py --source pc --input
$TR64_DATA/pc-tr2/WALL.TR2 --main-sfx
$TR64_DATA/pc-tr2/MAIN.SFX --out "$BUILD_DIR/wall.pack"`.
The evidence holds `wall-start.route`, the six `pose.json` inputs, and
`capture.py` (serial host + ares via `test/run/scene.sh`). Use
`TR64_LEVEL_PACK="$BUILD_DIR/wall.pack"` and a scratch `TMPDIR`.
For the historical stock 1472/192 KiB replay, use commit `3a4132880d`.
For the historical split experiment only, set those rows on that
revision to 2012/296 KiB (transfer 540/104 KiB from reserve, leaving
98 KiB). On current checkouts, export `TR64_GAME=2` before cooking,
building, and running the posed scene; the default TR1 cart rejects
TR2 levels. See [the per-game row budgets](tr2-level-fit.md#row-policy).

Directed checks: `make n64-core-tests tr2-material-test`, and
`python3 tools/cook/test_n64_tex.py` plus `test_n64_pack.py --cache ...`.
`native-controls-final.log` contains the rehashed short-zone failure,
explicit TX64/game mismatch, second-room-light and static-word controls,
and odd-width RDP command checks. `pack-controls-final.log` includes the
overlap scratch edge. `wall-game-control.log` and
`wall-version-control.log` show actual loader/game rejection through the
host route path, not only parser tests.

## Portal-clipped workload search (stock rows)

The six close views above are not the upper tail. This search used **all
84 physical rooms**, with the unflipped Wall pack. `heavy/poses.py` takes
sector centres on the authored 1024-unit grid, rejects solid/wall and
lateral-transfer cells, and computes local floor and ceiling including
their signed slopes. The grounded eye is 762 units above its local
floor, using OpenLara's standing height as a conservative head-height
proxy, not a camera taken from the TR2 controller. It requires a dry
floor with no room below and 64 units more clearance than that height.
Additional eyes are at half and a quarter of the local height below the
ceiling for spans at least 3,072 units tall, or sky-flagged spans at
least 1,536 units tall. For five rooms with no such sample, use the
midpoint of their deepest positive cell interval; these are air-only
samples, not a claim of reachable Lara positions. No trigger, movable
collision, or room transitions are simulated in candidate selection.

The sample contains 1,639 grounded, 2,372 mid-height, 2,372 high and five
air-only positions (6,388 total). Each has eight evenly spaced compass
yaws and three pitches (nominal −26.6°, 0°, +26.6°, Y down):
**153,312** measured views. It is a finite grid search, not a bound
over continuous camera positions or a guarantee that each eye is
reachable by play. `heavy/poses.in` records all exact look vectors.

`heavy/submit.c` is a host link-only iterator around the already-bound
`tr64_render_frame`. It calls the ordinary `tr64_render_camera` and
`tr64_render_frame` for each input. The production loader, portal
visibility, batch culling, material/mesh submission, and host GPU
command counters are unchanged. The link leaves the optional rasterizer
callbacks unbound, so the search scores submission, not image fill.
The original Lara pose is a positive control: 3 visible rooms, 706
triangles, 80 uploads and 6,016 record bytes match the full host and
cart scene. A named renderer error stops the sweep with the offending
eye/room/look. The sweep completed without one.

Source and output: `heavy/{poses.py,submit.c,poses.json,poses.in,sweep.log,
views.csv,rank.py,ranking.json}`. Per-view statics count visible-definition
instances visited in visible rooms, before batch culling. Triangles are
submitted triangles, before RSP rejection. RDP record bytes are not the
separate 20-byte-per-triangle command stream. Percentiles weight every
measured view equally (ordinary median, nearest-rank p95).

| Counter | Median | p95 | Max |
|---|---:|---:|---:|
| Visible rooms | 5 | 13 | 21 |
| Placed statics visited | 0 | 17 | 36 |
| Triangles | 473 | 1,378 | 2,427 |
| Texture uploads | 44 | 162 | 281 |
| RDP record bytes (limit 65,536) | 3,816 | 13,496 | 23,072 |
| Live matrix slots (limit 256) | 4 | 12 | 21 |

Top ten in each ranking; ties use ascending input ID. For all 153,312
views the largest record allocation was **35.2%** of capacity and the
largest matrix allocation was **8.2%**. The maxima need not occur in the
same view. `ranking.json` gives all world coordinates and the replay set.

| Tri rank | View | Camera room | Visible rooms | Statics | Triangles | Uploads | Records B / 65,536 | Slots / 256 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 50333 | 13 | 16 | 2 | 2,427 | 281 | 23,072 | 15 |
| 2 | 50357 | 13 | 16 | 2 | 2,393 | 275 | 22,352 | 14 |
| 3 | 50621 | 13 | 17 | 2 | 2,385 | 269 | 22,168 | 16 |
| 4 | 50645 | 13 | 18 | 2 | 2,385 | 269 | 22,176 | 16 |
| 5 | 27567 | 7 | 17 | 2 | 2,295 | 267 | 21,976 | 17 |
| 6 | 27543 | 7 | 16 | 2 | 2,291 | 267 | 21,968 | 16 |
| 7 | 37835 | 8 | 20 | 2 | 2,286 | 247 | 20,584 | 20 |
| 8 | 29481 | 7 | 18 | 2 | 2,270 | 250 | 20,992 | 18 |
| 9 | 27639 | 7 | 18 | 2 | 2,267 | 270 | 22,368 | 18 |
| 10 | 47261 | 12 | 16 | 2 | 2,264 | 254 | 20,928 | 15 |

| Upload rank | View | Camera room | Visible rooms | Statics | Triangles | Uploads | Records B / 65,536 | Slots / 256 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 50333 | 13 | 16 | 2 | 2,427 | 281 | 23,072 | 15 |
| 2 | 15266 | 5 | 18 | 2 | 2,162 | 279 | 22,952 | 18 |
| 3 | 50357 | 13 | 16 | 2 | 2,393 | 275 | 22,352 | 14 |
| 4 | 27639 | 7 | 18 | 2 | 2,267 | 270 | 22,368 | 18 |
| 5 | 50621 | 13 | 17 | 2 | 2,385 | 269 | 22,168 | 16 |
| 6 | 50645 | 13 | 18 | 2 | 2,385 | 269 | 22,176 | 16 |
| 7 | 27543 | 7 | 16 | 2 | 2,291 | 267 | 21,968 | 16 |
| 8 | 27567 | 7 | 17 | 2 | 2,295 | 267 | 21,976 | 17 |
| 9 | 26007 | 7 | 18 | 2 | 2,255 | 264 | 21,744 | 18 |
| 10 | 26079 | 7 | 18 | 2 | 2,255 | 264 | 21,744 | 18 |

The grounded subset alone (39,336 views) reached 2,244 triangles, 258
uploads, 21,440 record bytes, and 20 slots (separate maxima). Every top-ten
triangle and upload entry above came from the elevated vista samples.

`heavy/sweep.sh` records the host build/relink and invocation; `heavy/cart.py`
replays the ranked poses through the usual stock harness. Both use the
same `wall.pack` as the close views.

### Stock cart replay of the upper tail

The top five triangle views and the top three upload views overlap at
50333 and 50357, giving six distinct replays. All passed the stock
4 MiB scene gate. The rasterizing host and the cart each reproduced
the search's rooms, statics, triangles, uploads, record bytes and matrix
loads exactly. The slowest replay was **12.488 ms** (view 15266, second
by uploads), not the triangle peak; this is the maximum of these six
replays, not a timing search over all 153,312 views.

| View | Core/TX/SN load µs | Scope incl. draw µs | FRM µs | RND µs | GPU wait µs | Rooms / statics | Triangles / uploads | Records B / slots | Cart vs host MAE / SSIM |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 50333 | 517,149 | 873,253 | 11,682 | 11,312 | 370 | 16 / 2 | 2,427 / 281 | 23,072 / 15 | 0.0302 / 0.771 |
| 50357 | 517,201 | 872,867 | 11,311 | 11,043 | 268 | 16 / 2 | 2,393 / 275 | 22,352 / 14 | 0.0234 / 0.805 |
| 50621 | 517,212 | 873,317 | 11,621 | 11,517 | 104 | 17 / 2 | 2,385 / 269 | 22,168 / 16 | 0.0332 / 0.733 |
| 50645 | 517,212 | 873,397 | 11,695 | 11,631 | 64 | 18 / 2 | 2,385 / 269 | 22,176 / 16 | 0.0273 / 0.770 |
| 27567 | 517,180 | 872,201 | 10,643 | 10,576 | 67 | 17 / 2 | 2,295 / 267 | 21,976 / 17 | 0.0258 / 0.752 |
| 15266 | 517,315 | 874,056 | 12,488 | 11,900 | 588 | 18 / 2 | 2,162 / 279 | 22,952 / 18 | 0.0223 / 0.816 |

| View | Visibility µs | Cull µs | Scan µs | Submit µs | Expand µs | Tex setup µs | Matrix µs | Vertices µs | Recolour µs | Vertex wait µs |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 50333 | 1,341 | 1,153 | 693 | 7,471 | 3,230 | 1,628 | 359 | 86 | 46 | 0 |
| 50357 | 1,341 | 1,150 | 682 | 7,239 | 2,985 | 1,808 | 335 | 34 | 0 | 0 |
| 50621 | 1,558 | 1,032 | 697 | 7,530 | 3,141 | 1,864 | 403 | 86 | 46 | 0 |
| 50645 | 1,653 | 1,052 | 704 | 7,531 | 3,375 | 1,629 | 405 | 86 | 46 | 0 |
| 27567 | 1,373 | 987 | 825 | 6,726 | 2,601 | 1,575 | 411 | 34 | 0 | 0 |
| 15266 | 1,470 | 1,055 | 693 | 7,751 | 3,479 | 1,656 | 421 | 34 | 0 | 0 |

Pictures: `heavy/wall-heavy-<view>/ares/ares-test-0000.ppm` and matching
`host/boot.ppm`; `heavy/contact.png` puts all six pairs together. All
six show the outside masonry and battlements, rather than nearby cave
walls. Black sky is expected with the absent sky draw. `heavy/cart.log`
records the replay gate; `heavy/cart-measurement.json` contains the
picture hashes, comparisons and raw phases. These six were compared
with the same-pack host renderer, not with additional float captures.

### First-game reference workloads

These are existing stock 4 MiB **game** traces, not separate Wall
measurements or comparable test inputs. `FRM` is the same back-buffer-to-
completion interval; `Scene work` also covers simulation and audio,
with pacing and pre-render diagnostics excluded.

| First-game sample | FRM µs | RND µs | GPU wait µs | Triangles | Uploads | Records B | Matrices loaded | Scene work µs |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| LEVEL1 bridge route, triangle peak (r13, tick 68) | 23,112 | 23,023 | 89 | 3,053 | 408 | 37,064 | 82 | 24,305 |
| LEVEL1 bridge room (r14, tick 152, route work peak) | 23,204 | 22,866 | 338 | 2,870 | 390 | 36,248 | 83 | 26,997 |
| LEVEL10B start (r12, tick 1) | 21,500 | 21,230 | 270 | 1,488 | 138 | 13,080 | 20 | 38,193 |

Sources: `bridge-final/engine.log` and
`$TR64_WORK/evidence/ShipSound/bisect/level10b-fixed/hash.engine.log`
(`game-render`, `render-phases`, `scene-work`). LEVEL10B's first tick
alone cost 16,262 µs, not part of its 21,500 µs render interval.

### What a playable Wall frame adds (not measured here)

- **Lara's mesh and animated pose**: the first game draws her 15 bones
  through the batch path ([M2](architecture.md#101-milestone-record)).
  Wall already stores mesh and packed animation data, but no TR2 pose
  decode, interpolation or per-bone submission ran here.
- **Enemies and movable items**: actor and object submissions, sprites,
  visibility queries, poses and drawables. The first game's view pulls
  64 drawables at a time into a **4,352 B** buffer; bridge room r14
  tick 152 logged 75 entity submissions and 10 items drawn. That case
  used **120,960 B** of game/renderer state after start, versus **3,136 B**
  during its posed preload. These are first-game costs, not Wall estimates.
- **Sky dome**: Wall has one mesh in TR2 skybox model slot **254**
  ([source slot table](specs/tr2-tr3-differences.md)); neither Wall's
  posed renderer nor the first-game measurements supply that draw.
  Play adds a sky draw and its material workload, without a measured
  Wall time or command count.
- **HUD**: health/air and pickups add draw work; the existing LEVEL10B
  tick-1 sample records **91 µs** of UI submission, not its total GPU cost.
- **Game tick and audio**: fixed-rate simulation, triggers, effects,
  loading/streaming and mixer service are absent from the posed number.
  Bridge r14 tick 152 logged **1,140 µs** simulation and **2,569 µs**
  immediate audio service. Both games reserve **127,584 B** at audio boot;
  the bridge run used **25,392 B** of its effects cache and Wall did not
  bind samples for playback. These rows are not additive predictions
  for TR2.

## First-game invariants

PC LEVEL1, LEVEL6 and LEVEL10B are byte-identical before and after (`cmp`):

| Pack | Before and after SHA-256 |
|---|---|
| LEVEL1 | `ccdd12ad2ca6957ea341cfa14719f6c6abb969f5c1a328ae7552bd7c6f0c9da2` |
| LEVEL6 | `12f7b31b9a9db2757de8a198eed2bd4190cd748c37c691e260a2d5415bd9f984` |
| LEVEL10B | `49970228e3dd7ecece9e93c3073232975e8e8dfa0e2b6eb0289c196ee38d4fd1` |

Stock one-view bridge, 180 matched `scene-work total_us` records: mean
**22.781 → 22.629 ms** (−0.152 ms, inside 0.200); worst 27.009 → 26.997 ms.
`bridge-before/` and `bridge-final/` carry the logs and identical picture
SHA-256 `f8b6a0c9d5e1fcde6bc754c6c879a9d76b00093f4bf3b1533815117dc91bd122`.
The original baseline harness invocation missed `room=14` because this
ordinary scene doesn't enable the perf-only log format; the guest completed
180 ticks, and `bridge-full.route` checks the final ordinary format using the
**identical compiled route bytes** (SHA-256
`e0305459b874a159e9341c974bf1ed4ffd8fdd3fa4756a0a48bd4b613edded49`).
The image gate passes at 560,872 of 622,592 bytes, unchanged in the bridge A/B.
All 25 solo fixtures (12 LEVEL1, 13 GYM) matched immutable oracle traces.
Both changed-input controls diverged at tick 31 as expected, and both
verifiers rejected a changed production hash and a missing field.
See `tr1-final.log` and `fixtures-final/`.

The final runtime-review rerun against `main` `b1af9ff` used the same
one-view bridge setup and matched all **181** game states, the pack, the
route and the picture. Mean work was **22.765 → 22.523 ms** (−0.242 ms);
worst was **26.913 → 25.823 ms**. The timing difference was not isolated
from compiled layout. The timing limit is a regression guard, not a
requirement to slow a faster build. The hot-window gate passed at
16,288 of 16,384 B (main: 16,320 B), with the unchanged room/item stack
depths and image use. See `review-fixes/{bridge.json,main-bridge,fixed-bridge,
native.log}`.

The runtime controls in `make n64-core-tests` cover a lying mesh object
box, shade selection held across a game-tag mutation until the next
bind, one oversized SN64 payload part (with the correctly partitioned
bank as a positive control), a valid save identity against a render-only
entry named `level1`, and nonzero first-game reserved room and item fields.
The first-game sample cache is checked byte-for-byte across a TR1 → TR2 →
TR1 sequence. The independent review's hostile packs also now pass
**60/60** (`review-fixes/reviewer-boundaries.log`).

After the fixes, view 15266 (the slowest earlier replay and second by
texture uploads) still loaded and drew at stock 4 MiB, with 2,162
triangles, 279 uploads, 22,952 record bytes,
18 matrices and a **12.215 ms** frame. Its screenshot is byte-identical
to the earlier cart replay. See `review-fixes/heaviest-wall/`.

## Implementation map

- `tools/cook/pack_level.py:56–105` selects the strict PC sequel reader
  with the explicit `--main-sfx` input; `n64_pack.py:173–235` writes the
  game word, retaining byte-identical legacy entries. `n64_core.py:79–153`
  encodes the table fields; `n64_core_view.py:72–129` reads both layouts.
  No geometry ABI changes are needed.
- `tools/cook/n64_tex.py:91–101,230–305` converts source ARGB1555, preserves
  width and centres and packs CI4. `engine/res/tex.c:19–55` enforces the
  flags, counts and tile bounds. `plat/n64/gpu.c:336–366` and `gpu-ci4.h:7–13`
  express upload byte extents and exact texel clamps separately. The host
  also accounts for the extra byte of an odd-width load.
- `engine/res/pack.c:155–171`, `level.c:15–19`, `validate.c:134–259` apply
  the version and native bounds, including zero reserved TR1 room/item
  tails. `engine/res/sound.c:12–36,82–86` validates the stream part cap
  even when the sequel bank is not bound for playback. The cache lifetime
  test loads TR1, overwrites its idle resident sample cache with TR2 metadata,
  then verifies that the next TR1 preload restores the sample bytes.
- `engine/render/geo_load.c:26–45` validates each object AABB against its
  batches, and `engine/render/bind_load.c:99–127` uses those boxes for placed
  statics. Bind resolves the shade field once (`bind_load.c:71–72`); the
  shared hot loop uses only that offset. `engine/save/identity.c:37–44`
  excludes named render-only entries from save identity admission.
  `engine/render/scene.c:59–67` rejects the sequel's live/item pose
  requests; `game/game.c:338–347` refuses its gameplay outright.

## Playable Wall still needs

- A versioned game consumer for raw type IDs, packed axis animation words,
  state and dispatch targets (four Wall targets are outside the TR1 rule),
  Lara movement/collision, authored moving instances, enemies and traps.
- TR2 floor/trigger execution, climbing, box/zone selection, room water and
  vertex light/attribute behavior; the current pack does not implement these.
- Versioned sound playback/activation, effect semantics and ambience, plus
  gameflow/UI/save validation. Gameplay and sound-start reject this version
  instead of interpreting raw sequel fields as TR1 behavior.

Not checked: real hardware, retail engine visual or behavioral parity,
complete game simulation, any other sequel level's load/draw, or PSX sequel
inputs. No campaign/gameplay sweeps were run. The posed samples are single
cold drawn frames, not a play-loop performance or hardware RDP-cost claim.

## Review handoff

Cook-only commit: `5e4a027330a67c4f4fb91cf140ca46aa80b1873e`
on `m10-tr2-wall` (merged to main as `b1af9ff`). Reviewed runtime and
format changes, including native regression controls: `3a4132880d`.
Both commits leave the stock memory rows unchanged.
