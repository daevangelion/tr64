# M10: Would streaming textures buy room for Tomb Raider II?

Base: `5d7ed33812a1c06484cabd653fc502ad309be077`. Measurements are from
cooked PC packs, plus named historical TR1/Wall timings; **no texture
streamer or new cart run**. Files named `E/...` are under
`$TR64_WORK/evidence/m10/texstream/`. B means bytes, KiB = 1,024 B.
The 4 MiB question here is a **row** question: saving bytes on Wall
only cannot move the fixed campaign row. TR2 has 2,088,960 B for
Core+TX64+RG64+MG64; Monastery already uses 2,056,160 B. The current
play-oriented link has 4,328 B left in its 608 KiB image; remaining
TR2 text is **estimated** at 93,100–207,900 B gross, not net image growth
([row policy](tr2-level-fit.md#row-policy), [link census](tr2-cart-image.md)).

**Result.** A fixed cache with enough full-resolution pixels for the
all-direction one-hop margin needs **492 KiB for pixels alone**. With
descriptors, palettes and bookkeeping, it saves **no campaign row space**.
A bounded hi-res cache over a complete lower-resolution copy would buy an
**estimated 240 KiB** of campaign level row at quarter pixels, but fitting
and refilling are separate: the worst adjacent move still has **172.6 KiB**
of newly wanted high-res pixels after the margin, about **38.5 ms or 22
frames at an 8 KiB cap** by the archived ares proxy. For a code-memory
problem, test selective offline downscaling before paying that runtime cost.

## What is resident and what draws it

- [TX64 v1](formats/n64-tex.md) is an immutable section. A 24 B object or
  22 B sprite descriptor names a CI4 image cluster and independent
  palette, a crop, the split lines and, for object textures, its corners
  and material policy. A cluster is a tight CI4 bounding image for
  overlapping referenced rects on one source page (TR2 also separates
  horizontal origin parity); edge adjacency alone does not merge, and holes
  still take space. Each cluster has a 16 B directory entry and
  `ceil(width/2) × height` image bytes, with 8 B starts. Deduplication is
  shared index images by clustering, and independent TLUT equality
  on the full 32 B RGBA5551 palette **and** the STP mask (8 B directory
  entry). Do not count a whole source page, or a palette for each run.
  The **2,048 B** limit is **per uploaded CI4 tile** with TLUT enabled,
  including TMEM row alignment; the cook pre-splits tall descriptor
  crops and splits the faces. It is **not** a limit on cluster/DMA size.
  Flat colours live in a small resident table and use no image. The
  animation map permutes object content ids; baked logical material and
  UVs stay fixed. The survey includes every member of the composed cycle.
- `engine/render/render.c` walks the draw portals from the **camera**,
  not Lara. It rejects back-facing portals, all-behind or all-beyond-far
  portals, intersects projected bounds with the parent's rectangle, and
  requeues a room's logical id when its merged clip grows. There is no
  baked PVS or hop limit. The room batch then passes near/far/frustum/clip
  bounds before its RG64 runs submit. Room statics use MG64 runs via
  the static definition, visible flag and aliased Core mesh pointer.
  Authored room sprites name TX64 descriptors directly in Core. A flip
  exchanges physical room slices under the logical ids; the edges then
  come from the exchanged physical room. TR1 split-screen draws the union
  of two independent views, not just one cache demand.
- `game/view.c` gives the renderer room-linked drawables; item mesh and
  Lara bone slots are resolved into MG64 in `engine/render/items.c`.
  Non-mesh items/effect sprites take a sprite-sequence frame. Muzzle
  flashes, Lara weapon swaps, cut actors, inventory ring and pickup-card
  models can also take MG64 paths; pickups shown in the HUD can substitute
  an inventory model. Items are eligible by logical room, then culled
  against the viewport, not clipped to the root-room portal. Text takes
  the UI sprite/TLUT path; the health/air bars themselves aren't a TX64
  stream. TR2's authored sky model (slot 254) belongs in the always
  available set when implemented, even though the poser did not draw it.
  The TR2 census includes authored assets for consumers the poser doesn't
  run; it isn't a measured playable sequel frame.
- RG64/MG64 already carry descriptor ids, **not** physical texture
  pointers. Today `plat/n64/gpu.c:tr64_gpu_material` injects
  `PhysicalAddr` of pixels/TLUT into the RDP records, while the UI path
  creates a surface from resolved pointers. These are **RDRAM→TMEM**
  uploads, not ROM reads. Invalidate image bindings on allocation/LOD
  changes; resolve once per run or sprite, not per vertex. Never overwrite,
  relocate or publish a slot while a PI request or previous RSP/RDP reader
  can still see it. Use the existing frame-end RSP/RDP fence, with a stable
  handle→address/LOD snapshot for the whole next frame. Persistent command
  blocks embedding physical addrs (including cached profiler glyph records
  when their fonts move) must pin their sources or rebuild after a change;
  a cache hit in TMEM and a resident cluster are different things.

## Cart I/O: measured proxies, not hardware guarantees

The cold TR6P mount validates header/directory/section directory, retains
32 B per wire record and uses a 16 KiB staging window in the exclusive
48 KiB I/O row. Raw aligned pieces can DMA directly into level memory;
LZ4 and misaligned parts stage first; CRC and decode are separate work.
Core, TX64, RG64 and MG64 become resident, with the mount released; that
scratch is reused for native validation. The current TX64 chunks partition
a *byte sequence*, not an independently requestable tile or cluster index.
The production async path is `engine/res/pack.c` → `engine/io/io.c` →
`plat/n64/sys.c:dma_read_async`: one in-flight request, 16-entry FIFO,
4 KiB pieces, 32 KiB charged per play frame, 16 B destination alignment,
even ROM offset/size, cache writeback/invalidation before submit. The
loader can reset the cap in its own loading state; play cannot assume that.

| Historical comparison | Measured input / guest wall-clock interval | Derived |
|---|---:|---:|
| TR1 PC `load` code unit, 14 acquisitions | 36 KiB, 7.768–7.893 ms | 216–219 µs/KiB, including code acquire/probe |
| Wall posed ares, view 15266 `level-load` | 712,044 physical B, 153.393 ms summed request time | 220.60 µs/KiB, 4.43 MiB/s |
| Same Wall pose, host | 712,044 B, 136.149 ms request time | 195.80 µs/KiB (synthetic host bus: 195 µs/KiB before rounding) |
| Archived TR1 PC shipping gameplay-map loads | per request totals in `E/timings.json` | worst 223.32 µs/KiB, **4.37 MiB/s** |

`bus_us` is submit→poll-done time summed over those pack requests, not a
PI logic-analyser sample, and includes headers/tables, Core/TX64/sound
metadata in this log line, not an isolated texture read. Wall separately
spent 158.474 ms on decode and 159.118 ms on hash; neither should be billed
as DMA. Using the last measured proxy, **estimates** for raw texture
traffic are 0.45 ms/2 KiB, 0.89 ms/4 KiB, 1.79 ms/8 KiB,
3.57 ms/16 KiB and 7.15 ms/32 KiB, before request/validation/cache
management and audio waits. A scattered few-hundred-byte workload may
achieve less. Do not use the complete cold-load time or the host model as
an in-game transfer-rate guarantee.

`E/timings.py` anchors all the numbers to exact log names, SHA-256s and
line numbers. ares' `ares/n64/pi/dma.cpp:PI::dmaDuration` at the documented
`d9117003` base models PI domain latency, pulse/release cycles, ROM page
boundaries and PI buffer/partial-byte costs; with the same ROM settings
and request pattern this is deterministic simulated PI timing. It does
not measure SummerCart64/other cart memory-controller service, possible
firmware/cache latency, or physical RDRAM contention with CPU, RSP/RDP,
VI or AI on the target. Neither the flash medium's cold-file/SD time nor
an assumed zero-seek/RDRAM guarantee belongs in this estimate. Measure
on the hardware with the launched ROM, not the flash cart menu.

Effects keep frequent samples resident, with per-voice block windows for
other samples; music queries an index then primes current/next/loop
4 KiB windows (`engine/snd/{snd,music}.c`). TR2's poser validates sound
metadata but does not play it. In the historical PC ship attempt, the
last audio counters are 271,872 B in 70 effect reads, or **3.14 kB/s**
per 86.681491 s of active audio (not steady state or all PI bytes). Music
reported 528,960 B in 138 reads per 827,456 active-track output frames at
22,047 Hz: **14.09 kB/s** of active music. The six-voice TR1 scene is a
separate sample, 13,792 effect B in four reads / 6.021890 active seconds
(**2.29 kB/s**). These averages hide simultaneous misses and track index/
prime traffic; the shipping log shows a 36.014 ms music-startup peak
(selection latency, not PI busy time). A 4 KiB audio window competes with
one PI texture piece, not with average audio throughput. Code unit fills
use the SDK's synchronous PI path outside the texture/audio FIFO, so they
also do not consume its byte counter; one code load is already allowed per
play frame. Sequence mandatory code and sound deadlines ahead of best-effort
texture work, keep in-flight texture requests short, leave queue slots and
bandwidth for audio, and don't schedule a long batch across a code-fill or
save boundary. Use 8 KiB of **texture** payload per frame (estimate for the
prototype, not a new shipping limit); 32 KiB/frame alone is only the entire
engine queue's theoretical upper bound, not texture service time.

## How the working sets were measured

The survey memory-maps the existing PC TR1 pack and only expands a level's
Core/TX64/RG64/MG64. TR2 uses the stock `TR64_GAME=2` cook from its 24 PC
level files and `MAIN.SFX`, one disposable file at a time with no pack
copies kept. It accepts the existing Wall pack only on an exact catalog
hash match. The driver independently verifies directory/table CRCs, each
selected part's CRC and SHA, the exact native TX64 extents, geometry run
ownership, and byte totals against the prior TR2 catalog. The 45 inputs
include both titles and all eight cuts; no TR1 `SNOWCUT` extra source,
PSX or expansion levels. Scene/load backdrops like the TR1 `UI64` are
untouched and excluded, even when loaded in the same level allocation.

Each RG/MG textured run maps to distinct **cluster and palette ids**; the
entire source cluster is counted, not just the rectangle or a TMEM tile.
Repeated refs, shared palettes, aliases, animation phases, static
instances and sprites only count once in each union. Pixels and referenced
palettes are counted below, not TX64 metadata or alignment. In evidence
`own_world` is scenery alone for room R. The main sets add a global pin
for every placed movable type (including doors and other things that never
move between rooms; safe overcount), Lara/arms, weapons, pickups, menus,
fonts, effects, sky and known spawned replacements. The explicit
per-game pin tables and exact asset-role ids are in `E/survey.py`. Absent
roles cost zero. The **room-local** variant instead gathers ordinary
placements from their authored logical rooms, still pinning the semantic
roles above. A real implementation would maintain room links at runtime;
this variant does not bound simultaneously moved creatures or unimplemented
TR2 spawns. Neither variant is a retail object reachability proof.

**A** is only R plus pins. **B** uses a static far-limited *draw portal*
closure from R plus pins. **C** is B's rooms plus every dest of one more
outgoing portal from any of those rooms, without retesting that last
edge's range, plus pins. This last step is a margin, not in general the
next camera's B. Iterate to a fixed point: there is no drop after N hops.
For B the camera can be anywhere in R's physical sector footprint AABB
expanded to contain the authored Y bounds, drawable Y extrema and draw
portal vertices. From this original eye box (not from each room visited),
filter portals by the distance from their centroid to the box: allow up to
`1.01 × (far_corner + sqrt(3) × radius) + 4`, with radius the farthest
corner from the centroid and `far_corner = hypot(10240, 6400, 4800)`.
These latter lateral extents come from projection 320×240/focal 256 at
far Z=10,240. A portal not wholly behind or wholly beyond far has at least
one vertex past Z=0 and one at Z≤far. For a portal with a nonempty
screen bound, each projected axis's min is at or below the viewport's
max and its max is at or above the viewport's min (including bounds
grown at an edge crossing the eye plane). Thus each centroid component
can exceed the viewport/far extents by at most the portal radius; use
the vector triangle inequality and generous integer/fixed-basis slack.
Keep every permitted edge; skip facing, nesting rectangle, frustum,
batch visibility and doors. This is deliberately looser than an actual
frame for an eye in the box, not a guarantee for a cinematic eye outside
its labelled room, a giant scripted jump, or a replay at a different far
plane. `rooms_unbounded` is the connectivity-only control. Do not use
sector vertical links or door floor data to infer *visibility*.

Measure both global flip states where alternates exist; a physical room
gets its own one-room set and the worse B/C value over the contexts in
which it is selected. The room percentiles weight each physical room
once with nearest-rank p95. A directional adjacent pair is counted once
per flip state (repeated draw portals to the same room are collapsed);
we do **not** call every draw portal traversable by Lara. Delta is
`destination minus source`, not absolute size change. We report without
prefetch (`B' − B`), how much refreshes the margin (`C' − C`), and
immediately missing after prefetch (`B' − C`). `E/rooms.tsv` and
`E/edges.tsv` also contain the room-local results, and edges contain
16-byte-aligned **pixel-only** raw transfer bytes for the designs that
pin all palettes. `E/levels.json` includes worst directed two-hop skips,
non-adjacent jumps and flips (not claimed to be plays).

## Catalog measurements

**45 levels, 3,736 physical rooms, 6,715 room/flip observations, 18,220 directed portal/flip pairs.** Values below are **p95 / max**, not simultaneous demand in a drawn frame. `TX64` is the full section in B. Work sets use KiB (rounded to one decimal); no-room-edge cases are `—`, not zero.

| Game | Pack entry | Rooms | Full TX64 B | A KiB | B KiB | C KiB | B room-local KiB | C room-local KiB | Peak C physical room |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | cut1 | 15 | 125,992 | 101.2 / 101.2 | 107.3 / 107.3 | 107.3 / 107.3 | 107.3 / 107.3 | 107.3 / 107.3 | 5 |
| 1 | cut2 | 4 | 90,920 | 59.3 / 59.3 | 78.0 / 78.0 | 78.0 / 78.0 | 78.0 / 78.0 | 78.0 / 78.0 | 1 |
| 1 | cut3 | 26 | 140,592 | 78.5 / 88.7 | 125.7 / 125.7 | 125.7 / 125.7 | 125.7 / 125.7 | 125.7 / 125.7 | 0 |
| 1 | cut4 | 18 | 173,696 | 89.8 / 89.8 | 143.1 / 143.1 | 143.1 / 143.1 | 143.1 / 143.1 | 143.1 / 143.1 | 0 |
| 1 | gym | 19 | 345,360 | 183.7 / 183.7 | 292.7 / 292.7 | 292.7 / 292.7 | 292.7 / 292.7 | 292.7 / 292.7 | 13 |
| 1 | level1 | 38 | 386,536 | 225.5 / 229.0 | 319.3 / 319.3 | 322.1 / 330.3 | 319.2 / 319.2 | 322.1 / 330.3 | 32 |
| 1 | level10a | 108 | 457,288 | 254.7 / 269.1 | 345.6 / 368.5 | 347.6 / 370.5 | 308.9 / 350.4 | 323.0 / 354.5 | 103 |
| 1 | level10b | 101 | 459,728 | 303.2 / 325.5 | 403.4 / 405.4 | 403.4 / 405.4 | 380.5 / 388.6 | 395.0 / 403.1 | 40 |
| 1 | level10c | 67 | 471,632 | 318.1 / 334.2 | 397.2 / 397.2 | 397.2 / 397.2 | 375.4 / 375.4 | 375.4 / 375.4 | 52 |
| 1 | level2 | 94 | 450,624 | 225.2 / 262.2 | 364.5 / 368.4 | 364.5 / 374.5 | 355.3 / 361.8 | 357.8 / 367.9 | 15 |
| 1 | level3a | 91 | 433,424 | 267.1 / 291.4 | 364.5 / 364.5 | 364.5 / 364.5 | 356.6 / 356.6 | 358.7 / 358.7 | 62 |
| 1 | level3b | 54 | 409,336 | 262.9 / 280.0 | 344.0 / 344.7 | 351.3 / 351.9 | 335.1 / 344.0 | 346.0 / 351.9 | 11 |
| 1 | level4 | 63 | 445,464 | 262.2 / 271.1 | 380.7 / 384.8 | 380.7 / 384.8 | 372.4 / 379.5 | 372.4 / 379.5 | 18 |
| 1 | level5 | 89 | 371,216 | 222.0 / 233.0 | 298.0 / 304.7 | 302.7 / 309.3 | 289.7 / 296.4 | 299.3 / 307.5 | 79 |
| 1 | level6 | 79 | 442,736 | 226.0 / 245.0 | 365.7 / 378.4 | 381.9 / 384.0 | 355.8 / 373.2 | 378.7 / 378.7 | 44 |
| 1 | level7a | 139 | 371,264 | 234.3 / 241.1 | 317.1 / 317.1 | 317.6 / 317.6 | 314.6 / 314.8 | 317.6 / 317.6 | 1 |
| 1 | level7b | 114 | 479,464 | 289.1 / 299.2 | 387.4 / 399.6 | 399.6 / 399.6 | 373.8 / 384.0 | 386.0 / 386.0 | 90 |
| 1 | level8a | 71 | 440,736 | 217.6 / 267.7 | 372.7 / 372.7 | 372.7 / 372.7 | 372.7 / 372.7 | 372.7 / 372.7 | 30 |
| 1 | level8b | 84 | 469,032 | 245.5 / 274.9 | 399.3 / 409.5 | 415.6 / 416.6 | 397.7 / 409.5 | 415.6 / 416.6 | 59 |
| 1 | level8c | 64 | 436,200 | 257.0 / 273.2 | 371.2 / 385.4 | 385.4 / 385.4 | 369.1 / 385.4 | 385.4 / 385.4 | 41 |
| 1 | title | 2 | 54,256 | 47.2 / 47.2 | 47.2 / 47.2 | 47.2 / 47.2 | 47.2 / 47.2 | 47.2 / 47.2 | 0 |
| 2 | assault | 99 | 505,184 | 165.8 / 175.6 | 441.0 / 441.0 | 441.0 / 441.0 | 441.0 / 441.0 | 441.0 / 441.0 | 9 |
| 2 | boat | 155 | 582,912 | 338.1 / 349.7 | 466.1 / 472.9 | 476.2 / 488.4 | 459.3 / 464.1 | 476.2 / 488.4 | 65 |
| 2 | catacomb | 129 | 498,528 | 302.0 / 310.2 | 425.3 / 425.3 | 425.3 / 425.3 | 423.1 / 423.2 | 423.1 / 423.2 | 60 |
| 2 | cut1 | 3 | 178,288 | 140.7 / 140.7 | 149.7 / 149.7 | 149.7 / 149.7 | 149.7 / 149.7 | 149.7 / 149.7 | 2 |
| 2 | cut2 | 2 | 240,016 | 201.0 / 201.0 | 201.0 / 201.0 | 201.0 / 201.0 | 201.0 / 201.0 | 201.0 / 201.0 | 1 |
| 2 | cut3 | 14 | 305,256 | 192.3 / 192.3 | 227.8 / 227.8 | 227.8 / 227.8 | 227.8 / 227.8 | 227.8 / 227.8 | 8 |
| 2 | cut4 | 8 | 182,120 | 109.1 / 109.1 | 150.9 / 150.9 | 150.9 / 150.9 | 150.9 / 150.9 | 150.9 / 150.9 | 4 |
| 2 | deck | 116 | 542,960 | 361.1 / 374.2 | 445.9 / 445.9 | 449.9 / 449.9 | 440.0 / 442.0 | 446.3 / 446.7 | 20 |
| 2 | emprtomb | 193 | 610,728 | 315.1 / 337.4 | 473.1 / 493.4 | 481.3 / 499.5 | 465.9 / 485.1 | 478.7 / 493.2 | 57 |
| 2 | floating | 172 | 590,728 | 296.7 / 315.0 | 469.1 / 471.1 | 471.1 / 471.1 | 465.0 / 469.1 | 469.1 / 471.1 | 59 |
| 2 | house | 86 | 541,640 | 206.7 / 228.3 | 463.2 / 463.2 | 463.2 / 463.2 | 463.2 / 463.2 | 463.2 / 463.2 | 9 |
| 2 | icecave | 151 | 544,072 | 335.9 / 350.3 | 437.7 / 441.8 | 439.8 / 441.8 | 394.3 / 398.1 | 396.6 / 399.0 | 60 |
| 2 | keel | 114 | 582,456 | 353.9 / 358.6 | 455.6 / 458.1 | 457.9 / 462.1 | 437.2 / 445.6 | 443.4 / 449.5 | 49 |
| 2 | living | 82 | 523,344 | 304.1 / 308.5 | 402.7 / 422.2 | 414.5 / 424.2 | 400.9 / 420.4 | 413.6 / 423.3 | 16 |
| 2 | monastry | 164 | 615,224 | 307.8 / 334.6 | 486.4 / 488.4 | 488.1 / 518.4 | 473.0 / 482.6 | 480.6 / 515.4 | 104 |
| 2 | opera | 193 | 597,688 | 322.2 / 330.3 | 474.2 / 502.6 | 492.4 / 502.6 | 474.2 / 502.6 | 492.4 / 502.6 | 122 |
| 2 | platform | 96 | 626,776 | 400.9 / 418.4 | 504.6 / 506.7 | 506.7 / 506.7 | 497.3 / 501.6 | 501.6 / 502.6 | 12 |
| 2 | rig | 115 | 561,312 | 326.6 / 339.0 | 456.6 / 464.7 | 468.7 / 468.7 | 439.0 / 462.7 | 454.6 / 468.7 | 16 |
| 2 | skidoo | 149 | 465,832 | 293.9 / 304.0 | 345.5 / 347.6 | 349.6 / 359.8 | 330.6 / 344.0 | 344.0 / 346.0 | 46 |
| 2 | title | 2 | 60,504 | 52.6 / 52.6 | 52.6 / 52.6 | 52.6 / 52.6 | 52.6 / 52.6 | 52.6 / 52.6 | 0 |
| 2 | unwater | 80 | 438,552 | 308.0 / 310.1 | 351.3 / 352.1 | 351.6 / 357.6 | 337.8 / 337.8 | 339.8 / 340.2 | 28 |
| 2 | venice | 166 | 613,568 | 335.2 / 355.1 | 519.1 / 519.1 | 519.1 / 519.1 | 517.7 / 519.1 | 519.1 / 519.1 | 0 |
| 2 | wall | 84 | 432,544 | 280.7 / 300.9 | 338.2 / 338.2 | 340.2 / 340.2 | 313.6 / 313.6 | 315.9 / 318.7 | 4 |
| 2 | xian | 23 | 460,768 | 325.9 / 334.6 | 374.1 / 374.1 | 374.1 / 374.1 | 345.2 / 345.2 | 357.4 / 357.4 | 18 |

### Directed portal transition measurements

All columns are p95 / max. Byte deltas include referenced palettes; raw C→B estimate at right pins all palettes, charges 16 B aligned pixel bytes, and uses the archive rate. Other cases, alternate item policy, exact bytes and 32 KiB/frame figures: `E/timed-edges.tsv`.

| Game | Entry | Pairs | B' − B KiB | C' − C KiB | B' − C KiB | Raw C→B ms (est.) | Raw C→B frames @8 KiB (est.) |
|---|---|---:|---:|---:|---:|---:|---:|
| 1 | cut1 | 62 | 0.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 | 0 / 0 |
| 1 | cut2 | 6 | 0.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 | 0 / 0 |
| 1 | cut3 | 108 | 12.2 / 14.2 | 8.1 / 10.2 | 4.1 / 10.2 | 0.9 / 2.2 | 1 / 2 |
| 1 | cut4 | 34 | 12.2 / 20.3 | 8.1 / 20.3 | 4.1 / 4.1 | 0.9 / 0.9 | 1 / 1 |
| 1 | gym | 36 | 52.2 / 88.7 | 11.1 / 70.4 | 11.1 / 70.4 | 2.1 / 15.2 | 2 / 9 |
| 1 | level1 | 78 | 47.6 / 57.8 | 37.9 / 64.3 | 35.2 / 51.3 | 7.7 / 11.2 | 5 / 7 |
| 1 | level10a | 610 | 26.4 / 64.2 | 27.4 / 70.9 | 16.2 / 45.2 | 3.6 / 9.9 | 2 / 6 |
| 1 | level10b | 464 | 32.3 / 87.1 | 32.5 / 87.1 | 24.3 / 87.1 | 5.4 / 19.2 | 3 / 11 |
| 1 | level10c | 294 | 28.4 / 56.8 | 28.4 / 36.5 | 26.4 / 34.5 | 5.8 / 7.6 | 4 / 5 |
| 1 | level2 | 224 | 32.7 / 57.7 | 28.6 / 59.7 | 22.5 / 57.7 | 4.9 / 12.6 | 3 / 8 |
| 1 | level3a | 526 | 14.0 / 70.3 | 15.2 / 62.2 | 0.0 / 10.2 | 0.0 / 2.2 | 0 / 2 |
| 1 | level3b | 242 | 38.6 / 52.8 | 43.5 / 53.9 | 31.3 / 47.2 | 6.7 / 10.3 | 4 / 6 |
| 1 | level4 | 254 | 56.3 / 79.4 | 48.2 / 79.8 | 44.1 / 73.9 | 9.7 / 16.2 | 6 / 10 |
| 1 | level5 | 238 | 18.4 / 60.5 | 16.9 / 60.5 | 9.2 / 60.5 | 2.0 / 13.3 | 2 / 8 |
| 1 | level6 | 408 | 66.7 / 113.4 | 77.9 / 136.2 | 62.2 / 100.3 | 13.5 / 21.9 | 8 / 13 |
| 1 | level7a | 788 | 4.1 / 51.1 | 2.6 / 49.6 | 2.0 / 49.1 | 0.4 / 10.6 | 1 / 6 |
| 1 | level7b | 488 | 40.2 / 102.5 | 33.2 / 99.7 | 30.4 / 90.4 | 6.7 / 19.9 | 4 / 12 |
| 1 | level8a | 376 | 7.2 / 55.9 | 6.1 / 45.4 | 6.1 / 41.3 | 1.3 / 8.7 | 1 / 5 |
| 1 | level8b | 408 | 31.4 / 82.8 | 37.5 / 86.9 | 25.3 / 80.8 | 5.5 / 17.6 | 4 / 10 |
| 1 | level8c | 292 | 54.4 / 102.6 | 61.5 / 106.7 | 49.3 / 92.5 | 10.7 / 20.2 | 6 / 12 |
| 1 | title | 2 | 0.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 | 0 / 0 |
| 2 | assault | 372 | 90.4 / 143.8 | 79.5 / 123.8 | 66.9 / 107.4 | 14.2 / 22.8 | 8 / 13 |
| 2 | boat | 1130 | 24.8 / 69.9 | 22.1 / 72.0 | 10.1 / 65.9 | 2.2 / 14.4 | 2 / 9 |
| 2 | catacomb | 726 | 38.6 / 77.9 | 28.4 / 81.9 | 22.3 / 77.9 | 4.9 / 17.1 | 3 / 10 |
| 2 | cut1 | 4 | 0.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 | 0 / 0 |
| 2 | cut2 | 0 | — | — | — | — | — |
| 2 | cut3 | 26 | 30.6 / 32.7 | 32.7 / 32.7 | 30.6 / 32.7 | 6.8 / 7.2 | 4 / 5 |
| 2 | cut4 | 14 | 2.0 / 2.0 | 0.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 | 0 / 0 |
| 2 | deck | 764 | 13.4 / 38.1 | 10.2 / 40.6 | 8.1 / 34.8 | 1.8 / 7.6 | 1 / 5 |
| 2 | emprtomb | 898 | 42.2 / 201.1 | 36.6 / 182.8 | 31.1 / 176.8 | 6.8 / 38.5 | 4 / 22 |
| 2 | floating | 698 | 36.5 / 92.2 | 45.8 / 91.2 | 23.6 / 90.1 | 5.2 / 19.8 | 3 / 12 |
| 2 | house | 338 | 67.8 / 113.4 | 58.1 / 104.2 | 39.5 / 75.5 | 8.5 / 16.1 | 5 / 10 |
| 2 | icecave | 890 | 16.9 / 71.0 | 8.1 / 71.0 | 2.9 / 71.0 | 0.6 / 15.6 | 1 / 9 |
| 2 | keel | 538 | 47.9 / 66.8 | 33.4 / 66.8 | 32.3 / 66.8 | 7.0 / 14.6 | 4 / 9 |
| 2 | living | 404 | 33.5 / 69.0 | 29.8 / 60.2 | 18.1 / 53.6 | 3.9 / 11.7 | 3 / 7 |
| 2 | monastry | 790 | 56.9 / 96.6 | 58.5 / 93.0 | 39.4 / 74.2 | 8.6 / 16.3 | 5 / 10 |
| 2 | opera | 1138 | 33.7 / 116.8 | 35.0 / 112.2 | 28.4 / 112.2 | 6.3 / 24.6 | 4 / 14 |
| 2 | platform | 414 | 31.6 / 82.5 | 24.8 / 80.5 | 24.5 / 80.5 | 5.2 / 17.4 | 3 / 10 |
| 2 | rig | 664 | 29.1 / 87.0 | 33.6 / 71.0 | 25.8 / 63.9 | 5.6 / 13.8 | 4 / 8 |
| 2 | skidoo | 782 | 15.6 / 51.7 | 14.2 / 55.7 | 8.0 / 47.6 | 1.8 / 10.4 | 1 / 6 |
| 2 | title | 2 | 0.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 | 0.0 / 0.0 | 0 / 0 |
| 2 | unwater | 394 | 12.6 / 32.6 | 11.6 / 28.6 | 10.6 / 22.5 | 2.3 / 4.9 | 2 / 3 |
| 2 | venice | 998 | 31.3 / 76.5 | 25.2 / 47.2 | 23.2 / 42.6 | 5.0 / 9.3 | 3 / 6 |
| 2 | wall | 236 | 8.9 / 32.3 | 4.8 / 28.3 | 4.8 / 26.2 | 1.0 / 5.7 | 1 / 4 |
| 2 | xian | 62 | 22.9 / 35.1 | 6.2 / 41.6 | 6.2 / 29.4 | 1.3 / 6.5 | 1 / 4 |

## Cache choices and accounting

Compare all choices against aligned current TX64, not pixel bytes alone.
Keep the compact descriptors, split/animation/flat tables and **all**
palettes; together their measured level-specific cost is `prefix` in
`E/levels.json`. Existing descriptor and cluster ids cost no new bytes.
For raw full-resolution streams, reuse each 16 B cluster directory
record's offset for a ROM locator; charge **estimated** extra 4 B CRC +
8 B mutable slot offset/state/age per cluster and 4 KiB for work bits,
requests and staging. No permanently retained copy of the TR6P mount;
bookkeeping is billed to the level row here, not to unallocated state/I/O.
The estimates use 16 B alignment per cluster/cache slot. A variable-sized
cache needs a bounded allocator: a packed payload total alone doesn't
prove fragmentation, residency across simultaneous views/frames, or a
refill deadline. Do not copy/compact live data per draw; if no fit, apply
fallback, or compact only at a cancelled/drained stream and GPU fence.
All choices retain Core/RG64/MG64 except changes to their cooked UV
encoding where needed; geometry size changes and linker cost are not
credited.

1. **Fixed full-resolution cache.** Reserve capacity for worst C plus
   metadata. At cold start prime B before play; then request visible
   misses before nearby rooms. Pin both current viewport sets, semantic
   assets, in-flight slots and queued GPU addresses; keep extras by
   last-needed age and evict only unpinned stale slots. Keep PAL resident.
   A cache that's large enough does not make a newly visible cluster
   arrive immediately. A no-regression release can choose the old
   resident encoding for a level cheaper that way. Size the level row
   to the largest actual plan plus the existing design cushion, not to
   the dynamic use of the current map. Don't spend host sightline results
   as 4 MiB linker headroom.
2. **All clusters have a resident smaller copy.** Keep each glyph/UI
   cluster full resolution and model both 2×2 and 4×4 size reductions
   elsewhere. On miss draw the low copy using the original coverage and
   silhouette geometry, not an absent texture or blocked gameplay; use
   a fixed **96 KiB** high-res cache in the cost comparison, bounded even
   on a wide open view. Charge **8 B extra per cluster** for low offset/
   extent in addition to the above cache metadata. The cook must make
   palette-compatible reductions (or measure extra TLUT bytes), maintain
   transparent coverage and sprite geometry, and address odd crop
   alignment, remapped UV centre/clamp/split endpoints and repeat/seams.
   A naïve nibble decimator blurs or erases narrow masked art. Budget
   here is an estimate from measured dimensions, rounded up per row and
   per 16 B payload; no cooked LOD images/quality test. Suggested pack:
   resident metadata+palettes+LOD (ordinary cold load), separate validated
   raw stream with independently addressed, aligned full clusters. Check
   image/CRC before making the handle ready. Coalesce genuinely adjacent
   DMA ranges; reorder by deadline without starving audio; compare raw
   layout's increased ROM extent to block compression before shipping.
3. **Block when the needed B is missing, no fallback.** Same metadata,
   smaller capacity for B rather than C. Resident B includes every turn
   inside R under the box model. Load before the first destination draw
   (also at cuts, flips, spawns and restores), keeping audio service;
   cannot rely on Lara physically crossing a draw portal. Letting play
   reset the 32 KiB cap to synchronously hide a large burst still stalls
   and can starve audio. Not a general play policy; cold transition/known
   scripted fade is different.
4. **Downscale the entire level once.** Keep full UI as above. No high
   cache, slot metadata, per-frame PI traffic or handles beyond existing
   TX64. Rescale crop and tile plans in the cook, regenerate RG/MG UVs,
   use the standard TX64 bind and pre-split writer, and choose the
   smaller pack per title only after inspecting image quality. Costs below
   keep existing tables and palettes fixed for comparison; remapped crop
   boundaries may need padding, a new split or a changed palette, so
   they are estimates, not actual smaller cooks. Half in X alone is a
   comparison, not a claim that it looks better than half in both axes.

The sizes below retain full-resolution UI. For comparison, scaling **every**
cluster at quarter/sixteenth pixels yields 129,984/39,280 aligned pixel B
on Monastery and 89,408/28,608 B on Wall (`low2`, `low4`, `E/levels.json`).
Wall's 432,544 B resident TX64 is 292,500 B of actual aligned read payload
in the archived pack. At Wall's measured aggregate request rate that is
an **estimated 63.0 ms** for the TX64 traffic alone, or about **93.2 ms**
if the full section were raw. The 2 KiB tile limit clearly isn't a useful
I/O block size: the largest cluster is 8,192 B in a TR2 gameplay map
(`xian`), or 23,296 B with the TR2 title included.

### Price sheet (estimates)

Fixed high-res payload C = **503,808 B (492 KiB)**, home-room items variant **499,712 B (488 KiB)**, blocking B **499,712 B (488 KiB)**. These are aligned pool capacities, not TX64 replacement sizes.

| TR2 entry | Baseline level B | Prefix B | Cache extra B | Low quarter / sixteenth pixel B (full UI) |
|---|---:|---:|---:|---:|
| monastry | 2,056,160 | 103,024 | 16,624 | 139,504 / 50,192 |
| emprtomb | 2,019,888 | 101,112 | 16,576 | 139,008 / 49,920 |
| skidoo | 1,903,648 | 90,720 | 15,568 | 105,408 / 41,808 |
| deck | 1,887,024 | 109,448 | 18,528 | 120,560 / 47,280 |
| wall | 1,406,320 | 82,504 | 14,608 | 98,928 / 39,520 |

**Net level bytes saved (est.)**, signed before falling back to the old pack on a negative result. Row reclaim uses the cheaper encoding on each TR2 level, keeps a 32 KiB cushion and rounds up to 8 KiB. It does not include extra image/state/link changes.

| Policy | Monastery | Emprtomb | SKIDOO | Deck | Wall | Max new campaign level B | TR2 row reclaimed KiB |
|---|---:|---:|---:|---:|---:|---:|---:|
| prefetch fixed | -8,224 | -10,768 | -144,256 | -88,832 | -168,384 | 2,056,160 | 0 |
| prefetch local fixed | -4,128 | -6,672 | -140,160 | -84,736 | -164,288 | 2,056,160 | 0 |
| blocking fixed | -4,128 | -6,672 | -140,160 | -84,736 | -164,288 | 2,056,160 | 0 |
| prefetch sized per level | -8,224 | 9,712 | 15,488 | -11,008 | 7,744 | 2,056,160 | 0 |
| blocking sized per level | 24,544 | 13,808 | 27,776 | -6,912 | 11,840 | 2,031,616 | 24 |
| LOD quarter + hi96 | 249,424 | 247,408 | 148,208 | 186,496 | 131,184 | 1,806,736 | 240 |
| LOD sixteenth + hi96 | 338,736 | 336,496 | 211,808 | 259,776 | 190,592 | 1,717,424 | 328 |
| downscale XY | 372,704 | 370,608 | 269,712 | 312,944 | 251,104 | 1,683,456 | 360 |
| downscale X | 249,456 | 248,160 | 180,944 | 210,144 | 168,400 | 1,806,704 | 240 |

### What sets the size and what makes it miss a deadline?

- **The absolute set peak is not the largest TX64.** Gameplay/training
  TX64 ranges from 432,544 B (Wall) to 626,776 B (Diving Area), 24.5–37.2%
  of each map's used level bytes. Monastery is 29.9%. Bartoli's Hideout
  (`venice`, not `boat`) has the largest union including palettes:
  **531,591 B**, or **86.6%** of its TX64. R0 reaches 151/152 rooms
  in B/C under the static rule, and sky-flagged R74 already ties at
  C=139 rooms; many connected canal/courtyard sides reuse the same
  textures. Its immediate neighbors aren't a useful bound on what
  becomes reachable in the unoccluded closure. The largest cache
  *pixel* peak is Monastery R104, a 14×20-sector room with seven
  portals and 74/90 B/C rooms: 530,832 B of pixels+palettes, including
  500,096 B of aligned pixels. These large hubs and their neighbors,
  not the small rooms or a single tile, set the budget. The archive's
  actual best Bartoli's sampled view saw 34 rooms, not 152; that is
  why an actual-view cache may need much less but is unproved here.
  On Wall, R4 and the sky-flagged R10 tie at 348,407 B, while the
  large outdoor R65 has 51,328 B of scenery by itself, but one
  portal and only 15 rooms in C (323,543 B including pins). The actual
  outside battlement probes draw up to 18 rooms in the archived
  cart upper-tail replay ([Wall](tr2-wall-measurement.md#stock-cart-replay-of-the-upper-tail)).
- **Pinning by room has a cost.** Wall's role pins are 181,527 B and
  adding all placed types raises them to 256,343 B. With placed types
  following rooms, Wall's peak C drops from 348,407 to 326,391 B;
  Bartoli's stays at 531,591 B, since the closure already pulls in those
  placements. On Emprtomb R54→R51, the bytes still needed after C
  increase from 180,992 to 251,648 B when assets aren't universally
  pinned. Those are placement-model results, not a test of roaming AI.
- **Crossing and turning aren't the same deadline.** Wall's worst
  modelled crossing R44→R42 is 32,480 raw B without prefetch, or 26,336
  after it (estimated 7.08/5.74 ms transfer and 4/4 capped frames).
  Emprtomb R54→R51 is 201,280/176,704 raw B (43.90/38.54 ms estimated,
  25/22 texture frames or at least 7/6 at the entire engine ceiling).
  The worst C→C refresh there, R54→R57, is 182,848 raw B (39.88 ms,
  23 frames estimated). A yaw change within R needs nothing *if the
  entire all-direction B is loaded*. A smaller cache keyed to today's
  clip rectangles cannot promise this; a 180° turn at Wall's gate or
  a canal corner can swap the visible branch in one frame without
  traversing any portal. No turn delta was measured from retail poses.
- **Vehicles and scripted jumps can outrun the margin.** Venice
  (`boat`) has a worst adjacent after-prefetch raw burst of 66,080 B
  even with all actor types pinned. Its worst two-hop skip requests
  75,616 raw B (estimated 16.49 ms or 10 texture frames); SKIDOO's
  is 47,840 B (10.43 ms or 6 frames estimated). These are graph
  stressors, not proof those edges are driveable. Venice's authored
  canal race is a real stress route: boost limit 140 world units/tick
  means about 7.3 ticks *per complete 1,024-unit sector* at that speed
  (estimate; actual room boundaries need not be one sector apart)
  [speedboat rules](specs/wall/tr2-venice-objects.md#riding-and-driving-the-speedboat-14).
  The authored lock also flips rooms; a flip with no room change is
  not covered by prefetch in only the old topology. Diving Area R72
  is the largest measured flip stress case, 133,984 referenced B
  absent from old C.
- **Hard cuts/teleports/flybys must be scheduled by the destination
  camera, not the actor's route.** For the Great Wall's fixed boulder
  shot, using its trigger room R52 as the old eye and its documented
  eye room R53 as the destination gives 0 new B after C;
  the opening fixed view stays in R35, also not a worst case. The
  [specified Wall cameras](specs/wall/tr2-great-wall-level.md) are
  cases to test, not evidence that every fixed view costs zero.
  As a nonlocal stress case, Emprtomb R141→R51 lacks 213,056 raw B
  out of old C (estimate 46.47 ms or 27 frames), not an observed
  teleport trigger. TR2 CUT2 has two mutually disconnected room
  sets, with **55,328 raw B** missing on R0→R1 (12.07 ms or 7 frames
  estimated); don't infer that playback makes that cut from this
  static result. Known cinematic camera plans can prime the next
  shot during the preceding shot/fade or stay resident. Unannounced
  off-path jumps need LOD fallback or a deliberate fade/wait. This
  survey did not find or time authored teleports or flyby room jumps.

### Recommendation and smallest cart experiment

**Recommendation:** Don't replace TX64 with the full-resolution prefetch
cache or a blocking portal loader. Keep Wall full-res for the playable
image/code-link work. If that measured link eventually needs level-row
bytes, try *only* Monastery and Emprtomb as offline scale candidates,
keeping characters/UI full as an art-quality policy to be budgeted in
the cook. First price the actual required carve, re-cook, compare key
views, and rerun the link/row gates. Even the less aggressive full-level
X-only size model reclaims as much as the quarter-pixel + 96 KiB cache.

In the X-only estimate with full UI, scaling only Monastery moves the
peak to Emprtomb and allows **32 KiB** reclaimed; scaling those two
moves it to SKIDOO (1,903,648 B) and allows **144 KiB** reclaimed,
with the same cushion and rounding. These are estimates before
preserving additional actor textures, not a reason to scale both if
the link needs only the first step.

The cache's advantage would be recovering detail, not more space. If
offline quality is unacceptable and that detail is worth the engineering,
try **quarter-pixel fallback + a capped full-res cache**, not a
full-res cache that stalls. Review the low-copy quality before trying
the much blurrier sixteenth-pixel variant.

**Smallest falsifier on the cart:** boot a disposable 4 MiB Wall poser
with ordinary audio output and an address-only read workload using
actual aligned ROM regions. Feed archived TR1 music and cold SFX
windows through the audio pump as test traffic (TR2 gameplay sound
isn't admitted); if needed isolate the audio-contention test in the
archived TR1 Level 6 scene rather than enabling unrelated TR2 rules.
Use the archived battlement poses
50333 and 15266 (`wall/heavy/poses.in`, via the scene harness) for a
turn/forced view switch. At the busy pose queue first a 26,336 B Wall
burst and then a 176,704 B Emprtomb-sized stress burst in actual 4 KiB
pieces; clock an overlap with a permitted code-acquire or music-prime
at a legal boundary. Limit textures to 8 KiB/frame and let the base
queue/cap still govern audio. Run streamer-off and streamer-on on both
stock hardware + flash cart and the user's M64 + SummerCart64. Record
frame CPU/GPU/PI wait, PI submit→done/service and queue delay (bytes and
request count), audio/AI underruns, DMA/cache and code peaks, deadline
lateness, plus per-frame blur/miss bytes and LOD-age histograms when
actually drawing the low copy. No framebuffer/geometry substitutions.
Start with passthrough DMA reads; if that meets deadlines, implement
only one cluster class and draw it via the same GPU address path,
forcing a cold slot miss with a masked sprite beside the opaque world.
The DMA-only case can **kill** the proposal if it introduces audio
underruns or takes the baseline pose over the 33.333 ms target;
it cannot **prove** quality or wall-play performance. A drawn prototype
also needs no absent pixels, no GPU/source corruption on slot reuse, and
acceptable recovery by the next shot/arrival as observed (no stalls,
and no late / continuously switching near-field LOD). Stage the 22-frame
stress recovery explicitly; it isn't an acceptable 22-frame freeze.

### Reproduce

From a disposable checkout at the base, with Python 3/NumPy/Pillow and
the existing cook toolchain:

```sh
set -eu
E=$TR64_WORK/evidence/m10/texstream
export TMPDIR=$TR64_WORK/tmp/TexStream PYTHONPATH=tools/cook PYTHONDONTWRITEBYTECODE=1
rtk mkdir -p "$TMPDIR"
rtk proxy python3 "$E/survey.py" > "$E/survey.log"
rtk proxy python3 "$E/timings.py" > "$E/timings.json"
rtk proxy python3 "$E/tables.py" > "$E/tables.md"
rtk rm -rf -- "$TMPDIR" # this job's temporary compressor and cook scratch only
```

Evidence to inspect: `survey.log` (45 input bindings), `levels.json`
(exact per-level maxima/percentiles in B and pack SHA-256s),
`rooms.tsv` (every physical/logical room, including work-set sizes
and visited room ids), `edges.tsv` (every directed edge/flip),
`timed-edges.tsv` (p95/peak raw and referenced bytes, estimated ms and
8/32 KiB frame counts for each policy), `designs.tsv` (per-level costs
and net saves for every design), and `timings.json` (archived raw
numerators, denominators, log identities). The TR1 campaign input hash
is `ec7d96d2dcb1435b7f438d410e19888546b2a46f0cd38bbd6d55f4cc59f7ff98`;
TR2 finished packs matched the prior `venice/fit/selected-packs.sha256`
for all 24 inputs. No data pack, binary, or measurement code changes
the production tree.

**Not measured:** an actual texture cache hit rate/eviction trace,
all orientations/teleport/cinematic paths in play, moving actor or
multi-view stress, a smaller cook's visual quality/real bytes (including
ROM extent, crop/atlas or palette changes), allocator/load CPU overhead
and real 4 MiB cart frame cadence or bus throughput. The model is
intentionally conservative about potential room visibility, and the
bandwidth model omits contention; those effects must not cancel one
another in a go/no-go decision.
