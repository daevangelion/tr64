# tr64 Architecture Evidence & Milestone Record

Empirical measurements, cache-layout scans, QR photograph simulations, and
historical milestone gate records split out from [architecture.md](architecture.md).

## 1. Fatal QR Photograph Simulation & Binary Cost

patterns are computed, data modules are painted in zigzag order). Decisions, by
measurement (`tools/fatal/photo_sim.py`: the 320x240 screen upscaled by 3 or 4 with
scan-line shading, perspective jitter up to 5 % and rotation up to 4 degrees, the
frame reduced to 480-900 px, Gaussian blur, noise, gain and gamma, JPEG; same seed, 200
trials; decodes of zxing-cpp / OpenCV's Aruco detector):

| symbol | edge | chars | zxing-cpp | OpenCV |
|---|---:|---:|---:|---:|
| 6-M, 4 px (the previous format) | 196 px | 154 | 200 | 163 |
| 10-M, 3 px | 195 px | 311 | 186 | 134 |
| 12-M, 3 px | 219 px | 419 | 181 | 107 |
| 13-M, 3 px | 231 px | 483 | 188 | 124 |
| 11-L / 12-L, 3 px (80 trials, another seed) | 207 / 219 px | 468 / 535 | 73 / 75 of 80 | 40 / 39 of 80 |

- *One symbol, not two or three.* The record (about 300 bytes) needs version 12 or 13 at
  this payload. Two or three version-6 symbols at 4 px decode every time but must be
  photographed in turn (alternating seconds or side by side: 2 x 196 px do not fit 320),
  and a missed one costs the flash. A 3 px single symbol decodes in about 93 % of
  simulated photographs (OpenCV 53-67 %): a failed photograph is repeated, a failed flash is
  not. Version 13 over 12: the same decode rate, 40 % more room (316 against 273 bytes), and
  231 px still fits the screen height.
- *M rather than L:* L holds 28 % more characters but decodes in 10 points fewer cases.
- *Mask.* Fixed, mask 4; the previous measurement (600 realistic records: lowest standard
  penalty on average, equal decode rates) stands.
- *Encoding.* The record is binary; as hex it would be twice the size, as raw bytes it
  is not pasteable text. RFC 9285 base-45 puts 2 bytes in 3 alphanumeric characters.


**Cost.** Resident image, measured on the shipping exception control against `main` cb196cb:
text 92,588 to 95,468 B (+2,880), rodata 19,668 to 20,220 (+552), data 1,456 to 1,696 (+240: the
stash code in `.data`), bss 23,160 to 23,304 (+144: the stash): +3,816 B in all (the
`size.json` `resident` field: 146,040 to 146,184 B). Stack on the fatal path: 532 B for the
codewords, about 330 B for the record, 484 B for its text, 316 B for its bytes.


## 2. Hot Code Placement Measurements (I-cache)

Measurement (ares, 4 MiB, sound on, `test/snd/bridge-perf.route`: the
`m3-level1-bridges-live` placement, 180 ticks, `scene-work` total, perf-only, no
game hash): before 26,098.9 us mean / 31,174 worst, after 22,936.3 / 26,752 us.
Final picture identical (SHA-256 f8b6a0c9...). Part of that difference is data
placement, not code: the two builds' `.data` differ by 576 bytes, and
the same binary moves with where `.data` starts (below). With identical `.data`
and `.bss` addresses in both builds, five placements:

| `.data` shifted | before mean/worst us | after mean/worst us | mean change |
|---:|---:|---:|---:|
| +0 | 26,098.9 / 31,174 | 24,200.7 / 28,202 | -1,898 |
| +2,064 | 24,626.3 / 29,149 | 23,035.0 / 26,974 | -1,591 |
| +3,600 | 24,613.8 / 29,070 | 23,032.6 / 26,935 | -1,581 |
| +5,600 | 27,428.9 / 31,868 | 25,714.0 / 31,056 | -1,715 |
| +7,184 | 24,699.3 / 29,547 | 22,847.0 / 26,689 | -1,852 |

The code layout is worth 1.6 to 1.9 ms (7%) on the mean at every data
placement. **Data placement alone moves the frame by up to 2.8 ms** (`.data`
and `.bss` shifted by padding in `.rodata`, text unmoved), more than the code
layout recovers; both builds show it. It is made deliberate in section 6.2:
the numbers above are before that change, and the rule to hold the `.data` start
equal between builds no longer applies once the hot data group is in place.
Evidence logs are kept under `$TR64_WORK/evidence/Build/hot-layout/`.

## 3. Hot Data Placement & Stack-Phase Measurements (D-cache)

Choosing the phase. The cost is not smooth: the scan has narrow cliffs (1,536
and 3,072 below) where the group lands on lines that something else uses
heavily; which lines (the stack, a pool's first words, the framebuffers' tail)
was not isolated, only the placements that avoid them were measured. Scan (same
scene, one build per phase, mean / worst us, pool phase 4,096):

| group phase | 0 | 768 | 1,024 | 1,280 | 1,536 | 2,048 | 3,072 | 4,096 | 5,120 | 6,144 | 7,168 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| mean | 24,565 | 23,073 | 23,183 | 23,066 | 25,302 | 23,778 | 27,859 | 23,787 | 23,430 | 23,806 | 24,011 |
| worst | 29,387 | 27,368 | 27,482 | 27,475 | 30,014 | 28,135 | 33,352 | 28,427 | 27,804 | 28,731 | 28,562 |

Pool phase 0 instead of 4,096 at group phases 1,024 / 2,048 / 4,096 / 5,120:
23,395 / 24,200 / 24,189 / 23,851 (never better). The shipped phase is 1,024,
the middle of the 768 to 1,280 plateau.

Result, final layout (phase 1,024, pool phase 4,096), same scene and picture
(SHA-256 f8b6a0c9... in every run), eleven placements: `.rodata` padding of 0,
1,024, 2,048, 3,072, 4,096, 5,120, 6,144 and 7,168 bytes, and 2,048, 4,096 and
6,144 bytes of cold `.bss` after the group (which also moves the image end):

| | mean us | worst us |
|---|---:|---:|
| natural layout, eight shifts | 24,618.5 to 30,023.7 (mean of the eight 25,884) | 29,323 to 34,798 |
| hot data group, eleven placements | 23,181.4 to 23,322.6 (mean 23,223) | 27,307 to 27,806 |

The spread over unrelated data size falls from 5,405 us to 141 us (65 us over
the `.rodata` shifts alone); the mean is 2.66 ms (10%) below the natural
average and 1.4 ms below the best natural shift. Not placed: the few hot words
in `.data` and `.sdata` (`hit` in the matrix code, the interrupt depth, T3D's
constants) and the read-only tables (`gSinCosTable`, `divTable`), which still
follow the size of `.text`; they may account for the remaining 0.1 ms, which
was not isolated. Evidence logs: `$TR64_WORK/evidence/Gate/data-layout/`.

The stack is part of this layout. The entry stack pointer of
`tr64_render_draw_object` decides the cache index of the draw path's
locals, the transform it is handed and the packet it builds. A renderer
change that added two call levels above it (192 B lower on the room
path, 200 B on the item path) cost 0.46 ms on the same scene with
identical instruction counts in the hot functions and identical hot
data addresses: 34,024 more data-cache misses over 31 ticks, in
`tr64_render_draw_object`, `tr64_gpu_matrix` and `tr64_gpu_material`.
Restoring the depth restored the time (23.377 against 23.337 ms).
`test/run/hot-check.py` therefore checks the linked ELF's cumulative
stack depth from `tr64_scene_tick` to both draw calls (1,016 B room and
static path, 3,368 B item path, modulo 8 KiB) and fails with what to
remeasure. It checks control flow, not instruction order: one
unconditional debit in a straight-line prologue that dominates every
checked call, the whole caller scanned, matching restores only on
terminal returns (`jr ra`, or a sibling jump to a function outside the
guarded chain), anything else a failure (an earlier version accepted a
branch around the debit; found in review, 2026-11-21). It does not check
offsets of locals inside the frames, callees, interrupts or what a real
cache does.
Evidence: `$TR64_WORK/evidence/m8/split/{instructions,stack-match,review-fixes/entry-sp}/`.

The same effect reached the music decoder through the live play loop.
A co-op change grew the play loop's frame from 2,488 to 2,552 B; the
mixer's per-sample stack spill moved from data-cache phase `0x13f8` to
`0x13b8`, the line index of the decoder's unit pointer (phase `0x13b0`;
the audio arena had not moved), and the unchanged loop evicted one line
twice a sample: about 0.38 ms of cache fills per 448-sample buffer in
ares's model, against a measured peak of 0.72 to 1.07 ms. The decoder
now drains one SPU unit at a time with no stack access in the sample
loop (peak 0.61 ms; PCM identical over 52.6 million samples), so a
caller's frame cannot bring it back; no stack guard was added for the
audio path (considered and dropped: the run-time underrun gate is the
protection). Evidence: `$TR64_WORK/evidence/ShipSound/bisect/`.


## 4. Historical Milestone Record (M0–M10)

### 10.1 Milestone record

**M0, passed 2026-11-16** at 38306f5: `BUILD_DIR=$TR64_WORK/builds/Main
test/run/m0.sh --ares`, run by Main on the merged tree; log and reports
under `$TR64_WORK/evidence/m0/`.

| Check | Observed |
|---|---|
| Boot at 4 MiB (ares, Expansion Pak off) | boot card shown; host and cart state hash equal (`b84a49c7`) |
| Pool table | 10 pools carved in file order; framebuffers at `8037f800` and `803a5000`, Z at `803ca800`, stack `803f0000`-`803fffff`; nothing at or above `80400000` |
| Expansion Pak present | 1,048,576 guard words above 4 MiB unchanged after the scene; a deliberate write there fails the scene |
| Resident image | 192,664 B test build, 192,312 B shipping build, against 458,752 B; the gate runs inside `make rom` |
| Library heap | 1,328 B, 6 calls, none after boot; a later allocation is fatal |
| Code units | `init` 1 page, released after boot; `test` 4 pages: loaded, called, unloaded, reloaded into different frames; TLB entries read from the CPU: 1 unloaded, 3 loaded; the unloaded call is a real instruction-fetch exception reported as "code unit test not loaded" |
| Negative controls | pool one byte too small, unloaded unit, allocation after boot, write above 4 MiB, image row too small: each fails for its stated reason |
| Harness | deadline, SIGTERM, signal during lock wait, SIGKILL of the harness: no process or slot lock left behind |
| Cook | 22 PC and 21 PlayStation level files read with no unexplained byte; 1,466 PlayStation samples valid; PC and PlayStation geometry and texture-corner layouts agree wherever faces match |
| Equivalence | the two releases differ in a handful of places per level (room flag 0x100 cleared on PlayStation, one animation state removed); LEVEL8A has one extra item and two triggers moved; LEVEL10A has one room pair edited |
| Oracle | 25 fixtures on LEVEL1 and GYM, byte-identical across reruns, fresh builds and five compiler configurations |

What M0 changed in the design: geometry shares vertices and carries
texture coordinates per face corner (4.1, section 7); the texture row
grew to 512 KiB; the cart needs content-addressed, compressed sections
(4.3); the 3D back end is chosen by measurement (D4).

Known defects carried into M1: the boot card's `R` glyph draws as `A`.
Not covered: real hardware; the micro-TLB.

**M1, rooms under posed cameras, 2026-11-17** at 3d78ba6. Evidence:
`$TR64_WORK/evidence/m1-render/` (per-pose table
`m1-scenes-after-spans.json`, pictures), `$TR64_WORK/evidence/m1/`
(cart beside reference for every pose), `$TR64_WORK/evidence/m1-ref/`
(reference pictures).

| Check | Observed |
|---|---|
| Cook | 21 levels from each source: core, textures, room and mesh geometry; every section inside its row (worst: core 514, textures 468, rooms 361, meshes 191 KiB) |
| Level load on the cart | LEVEL1: 0.40 s (bus 0.11, decompression 0.13, CRC32 0.13, validation 0.03) |
| Poses | 13 poses on LEVEL1 and LEVEL6 from each source, 26 scenes, all drawn on ares at 4 MiB |
| Pictures | each cart picture viewed beside its reference by the renderer lead and by Main: same rooms, textures, lighting, water tint, static meshes; no holes. Host, cart and reference agree on the start room |
| Frame | 3.9 to 16.0 ms until the RSP and RDP are done; worst: LEVEL6 statics, 1,875 triangles, 221 texture changes. LEVEL6 sightline: 11 rooms, 2,407 triangles, 278 texture changes, 12.2 ms |
| Counts | 485 to 2,407 triangles, 38 to 278 texture changes, 32 to 233 palette loads per frame |
| Memory | GPU boot allocations 230,608 B of 237,568, none after boot; resident image about 295 KB of 448 KiB with loader and renderer |

Not covered, by decision or not yet: the picture comparator's limits are
provisional and were not used as a gate (the host build does not yet
write the comparison maps); ares does not model RDP fill time, so the
frame times are CPU and RSP only; the level loader is resident (not yet
in a code unit); real hardware. One defect found on the way: drawing an
object scanned the whole level's run table (73 ms on the statics pose);
fixed with per-object run spans.

**M2, Lara, 2026-11-17** at cc782c8. Evidence:
`$TR64_WORK/evidence/m2/` (cart pictures, summary, image audit),
`$TR64_WORK/evidence/m2-render/` (host beside cart).

| Check | Observed |
|---|---|
| Port | `game/`: Lara's states and collision, rooms, items framework, camera, triggers, death page: C99 from OpenLara `src/fixed` at 2333adef |
| Oracle traces, host | 25 of 25 fixtures (LEVEL1 and GYM, PC data) equal byte for byte, reproduced by a reviewer |
| Beyond the fixtures | 863 placed random 600-tick scripts over every room of both levels, oracle against port: equal, or stopped by a named not-ported error before any wrong record (pickups, enemies, switches: M3); none diverged silently after two fixes |
| Cart at 4 MiB | the 12 LEVEL1 fixtures: every tick's state hash equal to the host's (1,432 records); game tick at most 842 µs |
| Lara drawn | 15 bones from the game's view through the batch path on host and cart; idle and running pictures viewed |
| Frame with Lara | worst 13.9 ms until the RSP and RDP are done (1,618 triangles, 17 matrices, 272 texture changes) |
| Memory | state 64,932 B of 131,072; resident image 404,808 B of 458,752 (game 97,798 B; libdragon 115,171; newlib 55,112) |

Defects found on the way: the system clock wrapped every 11.45 s (32-bit
tick arithmetic) and stalled the frame pacer; the pacer compared absolute
timestamps (would have stalled at 71.6 minutes); both fixed and covered by
a scene that crosses the wrap. Not covered: the GYM fixtures on the cart;
PlayStation data under the game (the oracle reads PC levels only); real
hardware. Carried into M3: the image row needs room for the rest of the
game (loader into a code unit: 19.5 KB measured; enemy families and menus
as units; SDK diagnostics and printf diet available: up to about 65 KB
gross).

**M3, the simulation, 2026-11-17** at m3-game e6be4c6 (renderer side at
94579e4). Evidence: `$TR64_WORK/evidence/m3/` (campaign table and
report, hand-off index), `$TR64_WORK/evidence/m3-render/`,
`$TR64_WORK/evidence/demo/`.

| Check | Observed |
|---|---|
| Port | objects and traps, enemies with navigation and encounters, Lara's weapons, health and effects, inventory counts and carry between levels: all item types of the 15 levels and GYM |
| Oracle comparison, host, PC data | 1,600 seeded 600-tick placed scripts (100 per level) and 185 source-trigger scripts: 1,651 equal, 132 stopped by name at boundaries M5 owns (119 death-page input, 13 level transition), no silent difference, no unexpected error. The 25 M2 fixtures still equal. Directed ledge sweep: 10,434 scripts on LEVEL1, LEVEL2 and GYM equal |
| Reviews | weapons area (320 scripts on three levels, tables token-identical to OpenLara): accepted; level restart path (36 level changes on the cart with identical pool numbers on a second lap): accepted; level arena and cold loader: accepted |
| Cart at 4 MiB | two 600-tick scenes (wolf combat; dart trap to the death page) equal to the host on all 601 hashes each; game tick at most 1.7 ms |
| Drawing | items, enemies, sprites, animated textures from per-room pulls (64 drawables, 4,352 B); no worst-case list |
| Frame, worst room found | LEVEL1 room 14 (two bridges, two active wolves, Lara firing): 37.1 ms as first shipped, 26.6 ms now; target 25 ms not met. 2,874 triangles submitted of which 1,127 are rasterised: back-face rejection before submission is in progress |
| Memory | state worst 161,700 B of 196,608 (LEVEL3A; 63,680 B of it navigation cells); level data worst 1,440,528 B of 1,507,328; game code units loaded at most 36,864 B of 98,304; resident image 457,016 B of 524,288 in the play build |
| Play | a demo ROM boots into LEVEL1 with a pad, restarts on death, advances at level end, shows the name of a not-ported boundary and restarts (`docs/controls.md`) |

Oracle repairs, each applied identically to the oracle patch and the
port and listed in `test/oracle/README.md`: an uninitialised local in
weapon aiming leaked stack contents into the trace; the object collision
loop read `nextItem` after a bullet freed itself; an activated pickup
without a model asserted in animation processing. Defects found by the
user playing the first demo: an unsigned shift of the animation Z offset
(Lara thrown to the ceiling at the end of a climb; the random scripts had
not faced a ledge squarely) and items clipped to their root room's portal
rectangle (Lara invisible while climbing). Not covered: PlayStation data
under the game (the oracle reads PC levels); non-pistol weapons and
medikits driven from input (they need the inventory menu, M5); real
hardware.

**M4 part 1, sound and music, 2026-11-17** at b6a149a. Evidence:
`$TR64_WORK/evidence/Audio/`, `$TR64_WORK/evidence/Media/`,
`docs/formats/n64-snd.md`, `docs/formats/n64-music.md`.

| Check | Observed |
|---|---|
| Effects | cooked from PC and PlayStation data (43 banks, independently decoded); six voices, pan and attenuation by position, ambient sources from the level core, loops end when no longer refreshed; bus headroom of one half |
| Scenes, host and cart, both sources | footsteps, pistols with a wolf, a waterfall: no clipped frame, dropped effect, stream underrun or hardware-empty output; game hashes identical with sound on and off |
| Cost | six voices 1.6 ms a frame; bridge room whole frame 28.2 ms with sound against 27.8 without; output queue three buffers, 61 ms worst latency |
| Music | 55 CD audio tracks of the PlayStation disc as SPU ADPCM, 22.05 kHz mono, 18.9 MB, streamed; 0.67 ms a frame; ambience per level, cues with resume, stop on release; a host harness matched a reference decode sample for sample over 60 million samples |
| Cart | 33.3 MiB ROM with 17 levels, effects and music; 15.4 MB left after reserving 16 MiB for video |
| Reviews | effects: one defect (loops never ended), fixed; music: accepted, one latent end-of-track fatal and the loop seam's read lead fixed afterwards |

Defects found on the way: effects played at a quarter of the game's
volume; the SDK's queued-buffer mask hid real output underruns with two
buffers (the hardware busy bit is now the measure); `kirq_wait` is a no-op
without the SDK kernel, so the framebuffer wait busy-pumped audio (2.6 ms
a frame). Not covered: nobody has listened on hardware or in an emulator
with audio output; PC CD audio (no image available); video.

**M5, front end and saves, 2026-11-18** at 22b467a. Evidence:
`$TR64_WORK/evidence/m5/` (campaign table, save matrix, editions
table, cart scenes), `$TR64_WORK/evidence/m5-ui/`,
`docs/formats/n64-ui.md`, `test/game/README.md`.

| Check | Observed |
|---|---|
| Oracle comparison, host, PC data | 3,383 of 3,383 seeded 600-tick scripts equal over the 16 levels, with the menu alphabet (ring, passport, options, death page, statistics) in the inputs and level transitions followed; the M3 named stops at the death page and at transitions are gone |
| Save and load | the fixed game's own serialisation, at most 6,050 B; 135 of 135 qualified save/load cells equal to the oracle across save, 200 ticks and load (10 cells not covered: 4 hang, 3B boulder, dead-enemy cells on 8B, 8C, 10A, 10B, 10C) |
| PlayStation data | 3,529 cases run on both packs: 3,306 equal to the PC traces; 223 differ and each is traced to a level-data difference (LEVEL8A has one more enemy, so every random draw differs; one camera record in LEVEL10A over a differently sloped ceiling sector). The oracle reads PC levels only, so equality with PC is the evidence here |
| Cart, 4 MiB, both packs | three hash scenes equal to the oracle on all 601 ticks: medikit from the ring (LEVEL4), save and load (LEVEL1), transition LEVEL1 to LEVEL2 with the carried inventory. Ordinary tick at most 1.9 ms; the save tick 127 ms, the load tick 157 ms, the level swap 895 ms |
| Presentation on the cart | title boot with the passport, inventory ring, save to SRAM and load after a power cycle, three-slot picker: pictures viewed. The ring's size equals the fixed game's computed proportion |
| Memory | resident image 477,400 B; state 178,544 B at the worst level with the 8 KiB save buffer; code pool 77,824 B of 98,304 at the worst level with the front-end units preloaded; ring-open frame 12.0 ms against 8.5 for the same world frame |

Decisions made on the way: START opens the inventory (the pad has no
SELECT); the death page waits for input; three save slots are an engine
addition over the game's single save; menus redraw the frozen world, no
framebuffer copy. Not covered: death page, statistics, pickup
notification and medikit use were viewed on the host only; non-pistol
weapons chosen through the ring are host-checked only; the ring's frame
time in a heavy room; the title level against the oracle; real hardware.

**M4 part 2, video, 2026-11-18** at 3272277, review fixes at f18c3c3.
Evidence: `$TR64_WORK/evidence/m4-video/`,
`$TR64_WORK/evidence/m6/video3/`, `docs/formats/n64-video.md`.

| Check | Observed |
|---|---|
| Player | H.264, 320x240, 15 frames a second, in a 22-page code unit with no level resident; pictures and decoder state in the borrowed level arena (peak 1,008,336 B of 1,507,328), returned after every video; 16 KiB guards round each picture |
| All 11 PC videos whole, cart, 4 MiB | 11,010 pictures shown, none dropped, no audio underrun; decode 17 to 26 ms a picture mean, worst 91.6 ms; never two slow pictures in a row, so the one-picture lead absorbs them |
| Sizes | PC: 16,010,716 B video and film sound; pack with 21 levels, effects, music and video 50,938,912 B; ROM 52,002,816 B. PlayStation (ten films, the disc has no ESCAPE; pictures letterboxed as on the disc): ROM 43,171,840 B |
| Flow | ESCAPE, CORE, CAFE at boot; MANSION before GYM, SNOW before LEVEL1, LIFT before LEVEL4, VISION before LEVEL8A, CANYON before LEVEL10A, PYRAMID before LEVEL10B, PRISON before CUT4, END after LEVEL10C; START skips |
| Review | no memory or code-unit defect; PRISON one step late, no stall stop, arena growth per keyframe, cook validation, slice-size margin: all fixed |

Cost: the decoder's resident RSP microcode and glue took the image row
from 512 to 544 KiB. Not covered: film sound by ear and its constant
40.6 ms lead; six videos only standalone, not through the play loop;
real-hardware cache behaviour.

**M6, 2026-11-18 and 19** at 8cd5072 (delivered as
`tr64-demo-8cd50727085c-{pc,psx}.z64`). Evidence:
`$TR64_WORK/evidence/m6/` (`sweep/`, `campaign-final/`, `play3/`,
`fatal-qr/`, `cuts/`, `render/`, `surface/`), `docs/game-gaps.md`,
`test/oracle/README.md` (29 hunks), `docs/specs/wall/`.

| Check | Observed |
|---|---|
| Cutscene levels | `game/cut.c`; CUT1 to CUT4 equal to the oracle for their whole length; entries from 3B, 7B, 10A and 10B equal; START skips; in the play loop and the shipping gate |
| Cart sweep, both sources, 4 MiB, sound on | 106 runs of 600 ticks (every level from its start and two placed starts): 104 with every game hash equal to the oracle (PC) or the host (PlayStation); the two title-level runs (one per source) fail in the scene harness after tick 0 with a render error and are not hash results (the title level is not supported there: see Not covered; corrected 2026-11-21 from "every game hash equal", found when the M9 merge-set batteries read the archived rows); whole-frame p99 under 26 ms on every level; one frame over 33.3 ms (the first frame of LEVEL10B, 37.6 ms); pool peaks: level 1,406 of 1,472 KiB, state 166.7 of 192, code 60 of 96 |
| Closing campaign, final oracle, schema 4 | 16 levels x 100 scripts equal; of 418 trigger scripts 417 equal and one an invalid placement that produced no comparison; menu scripts 1,600 equal with 31 placements rejected (corrected 2026-11-21 from "418 trigger scripts all equal": the raw report was summarised by attempts, not verdicts); 618 item and 6,058 movement scripts equal; blocks 4,416, switches 1,793; each repair's own script passes with a negative control |
| Ticks and frames | the play loop runs 30 ticks per second of the clock: with 0, 20 and 40 ms burned per frame on the cart Lara covers the same distance in 10 s to within 15 units (one tick is 46.5); in the shipping gate |
| Cache placement | hot code first in `.text` and hot data in one group at a fixed cache phase, pools at a fixed phase: bridge room 23.2 ms mean (25.9 before), spread 0.14 ms across 11 unrelated placements (5.4 ms before) |
| Saves | unusable slots refused; picker cannot be bypassed; atomic write with a spare region (old save survives every tear point); save tick 2.3 ms on the cart |
| Diagnostics | QR code with the crash record on the fatal screen (3,008 B), decoded on the cart for all 11 controls in both profiles; resident-to-unit references are a link gate |
| Remote runs | ares on the 12-core machine with identical results (bridge scene 13 s against 324 s); host suites there too |

The reference game is incomplete, which equality with it had hidden. An
independent review found that the port faithfully mirrored a defect
that made six levels unfinishable, and an audit against the original's
behaviour (the floating-point OpenLara game and specifications written
behind a wall from a decompilation, `docs/PROVENANCE.md` section 2a)
found the rest. Repaired identically in oracle and port, each with a
directed proof: level-ending animation effect (3B, 8C, 10B could not
end); doors and flips refused after a triggered pickup; cutscene camera
timer; drawbridge (never lowered) and its floor rule; cabin; rolling
block; moving bars; lava floor (Lara could walk on it); lava particles;
burning without a timeout; falling ceiling damage; Thor's hammer (its
strike point was inside the rock); trigger-activated invisible items
stayed invisible; collected pickups returned after a load; a load with
the doppelganger over a wall aborted; a finished puzzle slot stopped the
port; Lara surfaced early inside a water room and kept her swimming
pitch (a branch the fork had added; reported from play). One port
defect found by the save sweep: a pending collision offset was lost
across a load. For behaviour implemented from a specification the
oracle is not an independent check; the directed script is.

Gates on the final tree: `m0.sh --ares` in one run, the fatal controls
in both profiles, `make unit`, and the shipping gate on both packs
(boot films, title, levels, save through the picker, power cycle and
load, death page, natural level end, cutscene, pacing at 0 and 40 ms).
Image row raised to 576 KiB (resident 548,376 B; reserve 774 KiB).
Coverage of `game/` by all scripts: 88.1% of lines, 769 of 855
functions; the uncalled ones are listed with reasons in
`campaign-final/coverage.md`. Verification scope and concurrency are
now rules in `AGENTS.md`, after broad suites were rerun per change and
28 parallel lanes loaded the remote machine.

Not covered: combat against centaur, Natla, the scion target, the
doppelganger, the T-rex and the water rat in the closing round (earlier
rounds compared Natla, the scion target and the doppelganger);
LEVEL8B's last door by one walk; the LEVEL3B boulder save cell; the
title level in the scene harness; three water deviations of the
reference left for a decision (`docs/game-gaps.md` 6b); audio by ear;
real hardware.

**M7, first hardware result, 2026-11-19** at b61661c (delivered as
`tr64-demo-b61661c22986-{pc,psx}.z64`). Evidence: `docs/hw/m64-tlb.md`,
`$TR64_WORK/evidence/m7/`, `tools/hwprobe/`.

Every ROM up to 8cd5072 died at boot on the user's ModRetro M64 (an FPGA
console) with a SummerCart64, at the first call into a code unit. The
QR record (EPC C3F0000C, BadVAddr ..FFFFC0C0, return address in
`code_call`) was explained from the open MiSTer N64 core's source, by
the same author: with Status.KX set, that core does not translate
instruction fetches from kseg2; resident text ran only because its
mapping is the identity. A probe ROM confirmed the fetch path on the
console. The SDK fork (dde6197, reviewed before commit) no longer sets
KX, SX and UX in TLB-code builds, uses kseg1 for cartridge accesses,
invalidates caches by index at unit load and unload, and saves BadVAddr
at exception entry. An ares patch that imitates the core's fetch path
reproduces the record on the old build and passes on the new one; it
is a boot scene of the M0 gate (`m0.sh --hw`). The crash record
(format 2) now carries the registers, the words at the fault and at the
unit entry, and the mapping entries.

Not covered: the fixed build on the console (awaiting the user); speed
there (the core translates resident text through a one-entry
instruction cache of translations); a real VR4300.

**M8, split-screen measurement, 2026-11-21** at 53d0e39. Evidence:
`$TR64_WORK/evidence/m8/split/` (`measurements.md`, `one-view.md`),
`docs/specs/coop.md`, `docs/perf-overlay.md`, `docs/perf-audit.md`.

The user confirmed that the b61661c pair boots on the M64 and reported
slowdowns there. An on-cart profiler (L + D-pad Down) shows frame,
simulation, render, RSP/RDP wait, audio and I/O times, the RDP's
counters and per-frame counts; no numbers from the console yet.

A two-player co-op mode was evaluated by measurement before any game
logic. The renderer is per-view (screen rectangle, projection, scissor
offset); a frame is one view or two 320x120 halves, each with its own
portal traversal; colour and Z are cleared once. One-view output is
unchanged (25 fixtures and the bridge route: pictures, traces and all
game hashes equal; bridge time within 0.04 ms). A split preview in
play (L + D-pad Up, gameplay only, second camera orbited about the
game camera's target) lets the console report the cost.

| Scene (ares, 4 MiB, means) | frame ms, one view → two | render CPU ms | triangles | record peak B |
|---|---:|---:|---:|---:|
| LEVEL1 start | 7.83 → 12.00 | 6.74 → 10.99 | 1009 → 1448 | 14,064 → 18,752 |
| LEVEL1 bridge | 24.61 → 40.73 | 22.24 → 38.31 | 2598 → 4228 | 37,064 → 65,496 |
| LEVEL1 room 23 | 14.56 → 29.01 | 13.53 → 27.70 | 1622 → 3382 | 11,568 → 24,080 |
| LEVEL6 room 45 | 28.91 → 42.89 | 27.16 → 41.09 | 2419 → 3258 | 20,120 → 30,640 |
| LEVEL6 room 69 | 17.65 → 34.90 | 16.11 → 33.11 | 1879 → 3826 | 15,864 → 31,912 |

Render CPU is nearly the whole frame and follows the triangle count;
heavy rooms leave the 33 ms frame with two views before any RDP cost,
which ares does not charge. The bridge needs 65,688 B of the 65,536 B
record arena: a two-view frame that fills it drains the RSP and RDP
and reuses it (3 of 180 frames); a one-view world that fills it is
still fatal; the overlay's glyph commands share the arena and drain
instead of failing. 82,112 B would leave 20% free. The gpu pool is at
97.1%. Co-op therefore waits on render CPU work, which the console's
numbers are also expected to ask for. The reference's two-player rules
and every single-player assumption in the port and the oracle are in
`docs/specs/coop.md`, with the decisions left open.

Harness defect found and fixed: the remote harness could run a
different ROM than the one passed (same basename, size and mtime
second as the previous run's ROM; rsync took it from the delta basis).
Each run is now bound to the ROM's SHA-256, verified on the host before
a slot is taken and recorded in `ares-host.txt`. Before the fix only
delivery (guest build id on three runs) and the QR fatal tests would
have caught an aliased run; earlier A/B results were not re-run.

Since 10bf54c every cart prints its source build id and a configuration
digest (everything else that decides the cart's bytes: profile flags,
route and pose inputs, probe sources, pack, layout switches and tool
files, by content) at boot, and the scene, sweep, cart-reference and
shipping verdicts compare both with the build they meant to run; a
cached sweep result is reused only against the same reference, ROM and
identity; `ship.sh check` derives its expectations from current inputs;
the cart build refuses undeclared side inputs and unpinned tool
commands. Stated limits: no defence against editing the build system
itself; lifecycle and preboot controls need no boot and carry no
identity; the constructor fatal is bound by its QR only.

Not covered: any of this on the console; a second Lara, pad or HUD; a
scripted pad press in a remote run is not yet proven to have been
sampled by the guest (a dwell expiry advances silently; boot-film skips
are unreliable remotely).

**M9, two-player co-op, 2026-11-21** at 93361ae. Evidence:
`$TR64_WORK/evidence/m9/coop/` (`platform-report.md`,
`accepted-full/`), `$TR64_WORK/evidence/m9/core/`,
`docs/coop.md`, `docs/specs/coop.md`, decision D14.

A second Lara on controller port 2, in the one engine at the stock
4 MiB. START on pad 2 joins; she plays with the same mapping; the ring
is shared and full-screen; L + START leaves; L + A warps to the partner
(free alive; a revive costs one large medipack). A frame with two
players is two 320x120 views from each player's own camera with her
bars and notices in her half. The session survives level starts and
loads; saves stay one-player.

The game logic is written twice, in the port and in the oracle patch,
and compared by full trace under an opt-in schema 5 (host and test
builds only): 105 directed two-player cases with negative controls.
With the second player absent nothing changes: the 25 fixtures and the
cutscene levels are identical on schemas 2 to 4 on both sides, all
fixture saves and the one-player menu traces are byte-identical, the
one-view bridge time is within 0.02 ms, and the old one-pad harness run
does not join.

Rules settled in review (five passes on the core): while a player has a
committed interaction (item spent, switch thrown, block moving, pickup
taken) her leave, warp and revive return busy and change nothing; if
she dies in it she stays the actor of record and the trigger of her own
sector fires at the ordinary tick, with actor-local cameras going to her
view; a key, puzzle or switch trigger cannot be fired by anyone before
the object reaches the state in which the one-player game fires it; a
level end owed by an interrupted interaction is applied by resident
code after object dispatch and waits for the ring to close; a
checkpoint is refused, with its own message and no write, while a
player the save does not carry has unsettled work; join and warp use
the partner's last stable pose, revalidated against the present floor
and water; a hazard that picks a target picks one Lara, a hazard that
tests contact tests each. An earlier design that completed a departing
player's interaction on her behalf was dropped after three rounds of
defects.

The final cart gate (two scripted pads through a private two-pad ares
build whose injection reports each press as sampled by a controller
read; pictures and HUD regions compared with the host's render): 1,322
host and cart state hashes equal; join, two rooms, a pickup by pad 2
with leave refused then accepted, the ring from pad 2, save and load at
a checkpoint away from the start with slot 1 beside slot 0 before the
first tick, restart rejoin, the refused-revive notice in the dead
player's half only, the death page only when both are dead, leave to a
settled one-view picture. Two-view frames reuse the 64 KiB record
arena after a complete RSP and RDP drain (65,688 B for the heaviest
posed pair, 79 of 256 matrix slots).

Merge-set batteries after the merge (2026-11-21; raw verdicts in
`$TR64_WORK/evidence/m9/merge-gate/`), one-player, port against
oracle, attempted / equal / different / no comparison: campaign 1,631 /
1,600 / 0 / 31 rejected placements; trigger scripts 418 / 417 / 0 / 1;
items 618 / 618 / 0 / 0; movement 8,386 / 6,058 / 0 / 2,328; blocks
4,512 / 4,416 / 0 / 96; switches 1,809 / 1,793 / 0 / 16; combat 3,913 /
3,913 / 0 / 0 (the M6 input masks replayed, not regenerated: 1,052 of
them bound to the 2,475 cases of the M6 record); save cells 10 / 9 / 0 /
1 (the LEVEL3B boulder cell is never reached, as before); cart sweep,
start tier only, both packs 42 / 40 / 0 / 2 (24,040 hashes; the two
title rows fail in the scene harness as at M6; the 64 placed-start rows
not run); two-player suite 106 / 106; line coverage 85.3% (88.1% at M6:
the one-player corpus does not reach the co-op code). `m0.sh` with the
cart and hardware-likeness lanes passes.

The batteries found what the directed checks had not. `m0.sh` had been
red since the profiler merge (a build profile nobody had built, and an
exit-line parser tested only against a fake): fixed. The shipping
check failed its sound gate on a one-player run: audio output underruns
on the first LEVEL10C visit (three; none on the pre-merge ROM). Two
causes, both in the live play loop and neither in co-op logic: the
music decoder's cache alias (section 6.2), and a diagnostic: every
tenth tick `log_lara()` serialised and hashed the whole game state to
print 239 bytes of it, before drawing and with no audio service
between: 16.0 ms a call on the pre-merge ROM, 24.8 to 25.0 ms after
the merge (why it grew was not isolated: the logged bytes did not),
0.41 to 0.58 ms now that it serialises only the prefix (same bytes).
Every ROM delivered before this one therefore stalled about 16 ms
three times a second in live play, on the console too (the cost was
formatting, not output). No frame-time measurement had seen it: they
all ran the scripted scene route, which does not call it. Evidence:
`$TR64_WORK/evidence/m9/sound-gap/mechanism.md`.

The sound gate was rebuilt on the way (five review passes): it now
accounts for one intact report packet per level stop the driver
commanded plus a terminal sample, bound by transition, pace row and
next load, with strictly increasing output counters and separate zeros
for output, effects and music (music had not been gated; the old
parser dropped a damaged row silently and never sampled the last
visit); a damaged or missing packet is an invalid run (exit 3), not a
pass and not a sound failure. Limits: a log rewritten to different
valid numbers; playback after the terminal sample. PC shipping check
on `20b8100`: pass, eight packets, all zero, peak output pump 2.98 ms.

Not covered: any of it on the console; `CANNOT SAVE NOW` on the cart
(host only: the busy window is 24 ticks); frame time with two players
in heavy rooms beyond the M8 measurement; the campaign played by two;
the watchdog that releases an interaction after 4,096 ticks; frame
times of the live play loop (no gate measures them: the shipping check
measures pace and sound); the logging cart's own audio underruns at
LEVEL10B's start (two on PC: its per-tick hashing leaves 42 to 55 ms
between pumps; not a shipping path, cause of the change from zero not
established); why the SDK reports a late vertical interrupt once or
twice a run.

**M10 step 1, a Tomb Raider II level drawn, 2026-11-21** at 3a41328.
Evidence: `docs/tr2-wall-measurement.md`,
`$TR64_WORK/evidence/m10/{wall,runtime-review2,runtime-recheck2}/`.
Render-only: no second-game logic exists. The same cook, pack format,
loader and renderer serve both games; a pack entry carries a game
word (0 the first game, 2 the second, anything else refused by name),
and game logic, sound playback, the play loop and the save picker each
refuse a second-game entry by name. Admission limits have one
definition (`tools/cook/n64_validate.py`), generated into C, enforced
by the cook and the loader alike; a level that does not fit is refused
at cook time (SKIDOO: 1,903,648 B against the 1,507,328 B level row).

The Great Wall (`WALL.TR2`, PC), full texture resolution, stock rows:
level row 1,406,320 of 1,507,328 B (the census had estimated
1,410,544); state 8,144 B of renderer workspace only; I/O peak 43,600
of 49,152 B while loading; load 0.52 s, 0.87 s with bind and first
draw. A search of 153,312 posed views over all 84 rooms with the
production visibility code: 473 triangles median, 1,378 at the 95th
percentile, 2,427 at most; 23,072 of 65,536 record bytes and 21 of 256
matrices at most. The six heaviest by triangles and texture uploads on
the cart: 10.6 to 12.5 ms a frame (the first game's bridge room, a
played frame, is 23.1 ms with 3,053 triangles). The memory-row split
the census proposed was tried and is not adopted: this level does not
need it.

The first game is unchanged: three packs byte-identical, all 42 entries
bind, the 25 fixtures equal, the bridge scene's picture and 181 states
identical (0.24 ms faster, cause not isolated), hot window 16,288 B.
Provisional: static meshes shade with the second of the format's two
intensity words, as OpenLara does (wall question Q-TR2-015); rooms are
baked with the second vertex light.

Not covered: any play (Lara, items, enemies, triggers, climbing, water,
sound, the sky dome this level has, HUD: a posed frame time is not a
frame time for play); any other second-game level, or any third-game
one, loaded or drawn; the cart's sampling of odd-width textures beyond
one synthetic sprite; PlayStation data for the sequels; the console.

**M10 step 2, Lara moves as the retail game does, 2026-11-22** at
cbac9c9. Evidence: `docs/tr2-lara-status.md`,
`$TR64_WORK/evidence/m10/lara/`. Host only. The Great Wall is
admitted to game logic through an explicit option of the host scene
program; the play loop, saves, sound playback and the cart still refuse
a second-game level by name. Rule differences from the first game go
through one family, `TR64_RULE(name)`, a mask set once at load, each
bit listed in `game/tr2.h` with its citation (a wall specification
section, OpenLara, or the level data) and its uses; a first-game cart
folds it away (shipping `.text` byte-identical at 437,744 B). Six
questions went to the wall side and came back as specifications
(`tr2-lara-start.md`, `tr2-lara-slide-height-and-gaze.md`), each
reproduced from its text and the level file by the clean-side checker
before use. Result against the retail records, schema 20, seven
movement scripts (`stand`, `turn`, `travel`, `movement`, `jump`,
`grab-attempt`, `roll`), 1,685 records: every Lara field the port
produces, both look-subject fields and the gameplay random-number
state equal on every tick. Not equal under a strict comparison: nine
Lara fields are not produced yet (a timer, four flare fields, climbing,
vehicle, weapon item, aim target) and are excluded by a reviewed
profile (`tools/tr2-producer-profiles.json`; the comparator refuses a
trace that claims more than its pinned producer's profile); the active
item list and the camera's motion are not implemented. Five review
passes before commit (a frame-layout error that had corrupted the
first baseline, uncited rules, coverage that could be faked, an
unvalidated trigger reader, a room graph that could loop). The second
game's rules so far: about 10 KB of VR4300 text before linking.

Not covered: everything outside those seven scripts; the camera, level
items, creatures, flare, weapons, water, climbing, triggers' effects;
any second-game cart (none is built); Venice and Bartoli's Hideout
cook only with a larger level row (1,869,504 and 1,743,360 B against
1,507,328); the first game's own admission accepts a room that portals
to itself (found here, pre-existing, not changed).

**M10 step 3, Lara's whole body in The Great Wall, 2026-11-22** at
d26512d. Host only; no second-game cart is built. Six slices, each
implemented from wall specifications and level data, frozen as a patch,
reviewed once before commit and merged when its record comparison and
the first game's checks passed:

| Slice | Commit | Status document | Result against the retail records |
|---|---|---|---|
| Level items, world, starting stock | 1292bed | `docs/tr2-items-status.md` | world digests, the active item list and the inventory equal on the 19 routes published at its merge, and on both pool routes since |
| Camera: start, fixed views, follow, automatic gaze, manual Look | de7056c | `docs/tr2-camera-status.md` | every camera field equal within each route's coverage |
| Arms: pistols and the flare, held and loose | da1e648 | `docs/tr2-arms-status.md` | arm, weapon and flare fields equal; the second game's animation-effect dispatch |
| Ledges, the ladder, frontal stops, stance recovery | 014479b | `docs/tr2-climb-status.md` | `ledge-grab` 356 of 356, `ladder` 852 of 852, `sidestep-blocked` 79 of 79 |
| Water: wading, the surface, diving, air | 74a3984 | `docs/tr2-water-status.md` | all 51 Lara fields equal on `pool-wade` (840 records) and `pool-swim` (659) |
| Wet camera | 444e5fb | `docs/tr2-camera-status.md` | every camera field equal on both pool routes to the end |

Evidence: `$TR64_WORK/evidence/m10/{items,camera,arms,climb,water,camera2}/`,
review reports under each. Thirty schema-20 routes are replayed with
one pinned producer and profile (`tools/tr2-producer-profiles.json`):
28 dry routes, 9,199 records, and the two pool routes. A family's
coverage may end before a route does only at a tick the simulation
itself names (a condition, with its reason sealed in the trace footer
and a per-route minimum in the profile); it never changes what the
port computes. Two such boundaries remain, both the camera's combat
view on a firearm draw (`pistols` tick 46, `look` tick 176). Moves
with no cited rule refuse by name and leave no sealed trace: ledge
side exits, steps from a hang on a ladder, water currents, the wade
step-up, wading out, steering under water, surfacing from below. The
gameplay random-number state is equal on every route until the first
creature update (`pool-wade` 397 of 840 records, `pool-swim` 430 of
659): no creature is simulated. The first game is unchanged at each
merge: 25 fixtures in trace schemas 2 to 5 byte-identical, shipping
`.text` byte-identical at 437,744 B.

One independent review per slice before commit; the climbing and water
slices were each rejected once (the wet camera was accepted as first
frozen). The water review's findings, each reproduced with an input
and fixed with a control that fails on the rejected candidate:
coverage sealed as complete over state computed by no rule; a level
whose vertical room link leaves the linked room, admitted and then
aborting in the water-height lookup; an animation effect implemented
for the cases a record showed and not as its table states. Wall questions answered
and reproduced by the clean-side checker before use: Q-T2C-01 to 12,
Q-T2I-01 to 03, Q-T2A-01 to 05, Q-W-01 to 08, Q-T2M-01 and 02,
Q-T2W-02 and 06.

Not covered: creatures and combat (the tiger, spiders, hits, bites,
Lara's death: specified and recorded in an extended record format,
schema 21, in clean-side check at this date); the combat view;
pickups and switches beyond the two placed records; level end; sound;
every other level; the cart. The second game's logic has not been
measured for a cart since step 2 (about 10 KB of text then).

**M10 step 4, a second-game cart, creatures and interactions,
2026-11-22** at 2ddf15b. Evidence under `$TR64_WORK/evidence/m10/`
(`combat`, `interact`, `moves2`, `camera3`, `cart-image`, `cart-play`,
`source-layout`, `texstream`; large files of merged slices are stored
compressed in place, `.zst`).

| Piece | Commit | Document | Result |
|---|---|---|---|
| Combat view of the camera | 4181bf1 | `docs/tr2-camera-status.md` | the last three camera coverage boundaries removed (`pistols`, `look`, `flare-drop`) |
| Climbing and water follow-ups | af9b9ff, 8b850fa | `docs/tr2-moves2-status.md` | hang steps, the edge exit, wade steps and walls, leaving the pool, surfacing: six routes, 1,802 records equal on Lara and camera |
| Tigers, small spiders, pistol combat, the effect pool, record format 21 | d907fad | `docs/tr2-creatures-combat-status.md` | closed loop equal on `tiger-bite` 241, `tiger-death` 962, `tiger-shoot` 451, `spider-bite` 481, `spider-shoot` 381 rows; gameplay random numbers equal to the end on the older creature routes |
| Crows, bite blood, fragment flight | 0e34b63 | same | `crow-bite` 641 and `crow-shoot` 561 rows |
| Pickups, the wall switch, doors and leaf contact, the key and lock, rolling boulders, the save-counter block | a406759, bbbf5d1 | `docs/tr2-interact-status.md` | `pickup` 151, `switch-door` 286, `rusty-key` 766, three door routes 81 each |
| Second-game cart, The Great Wall | 2231e00 | `docs/tr2-cart-play.md`, `docs/tr2-cart-image.md` | boots at 4 MiB in the emulator into play; host and cart equal on 3,398 state hashes over ten scenes (at 25db66e and again after the layout move at deb4f40; not yet rerun with interactions); resident 493,896 of 622,592 B with interactions (link gates); level-selected code units for level-only controllers |
| Frame-time overlay, build id on the cart | 53a78bd | `docs/perf-overlay.md` | R + Start on the second game |
| Source layout | 384f796 | `docs/tr2-source-layout.md` | second-game code in `game/tr2/` under the first game's file names; a mechanical move: 68 route outputs and the first game's loaded bytes identical |

Two ROMs were delivered for the console: `tr64-tr2-wall-dfb01937abf1`
(88a0931) and `tr64-tr2-wall-3f0d34b6d8fc` (25db66e, with the overlay
and crows). Neither had been run on the console at this date.

Measured and not adopted: streaming textures by room visibility
(`docs/tr2-texture-streaming.md`): a full-resolution cache needs 492
KiB for pixels alone and frees no row space. Measured and open: the
cart's slow frames are render CPU, not game logic: 24.9 ms worst frame
work in the emulator in the spider and crow scenes (290 of 380 and 598
of 640 frames over 16.7 ms), against 9 to 13 ms elsewhere.

How the wall-side work ran: every rule came back as specification text
with a retail record, and an independent clean-side checker replayed
each record from the text and the level data before the text was
merged, listing every choice its replay made that the text did not
state ("Assumed, not in the text"); those became passages for the
reader. By this date The Great Wall has checked rules and records for
all its content (the last: the push block, trapdoor and collapsing
tiles, spikes, the blade, the zip line, the level's end, the shotgun,
both T. rexes), and Venice has a record format (schema 22: boats, race,
mines, an effect-pool count) and its first records. Pre-commit review
rejected a first candidate of most code slices above (creatures, crows,
interactions, the play cart, the overlay; not the combat view, the
follow-ups or the layout move); the recurring kinds: data read without a load-time check;
unimplemented behaviour running silently instead of stopping by name;
a paged controller entering another dispatch.

Not covered: on the cart: sound, the HUD and inventory ring, saves,
level flow; in game logic: the push block, trapdoor, tiles, spikes,
blade, zip line, level end, shotgun and T. rex (rules and records
merged, code in progress); every level after The Great Wall; the
console.

**M10 step 5, The Great Wall's objects, HUD, sound, and the first play
reports, 2026-11-22** at `563782f`. Merged, each after an independent
pre-commit review: the push block, hinged trapdoor and collapsing tiles
bound to the first game's controllers (`e7a1693`); stationary spikes,
the guillotine, the boulder's hit, the item contact order, the zip line
and the level's finish (`09a503a`); the shotgun and both T. rexes
(`a712dc4`) with the unlocked recoil rule (`ac2777d`); load-time and
capacity hardening and the game as a per-cart constant
`TR64_SECOND_GAME` (`0b11e74`); the HUD and the inventory ring on the
first game's UI (`677afec`); effects and ambience through the first
game's sound path, with a per-game mode table and oldest-one-shot voice
stealing (`731208e`); the texture animation clock (`a2df2f7`); the arm
pose after a holster (`a6ddf2d`); the ledge climb out of water
(`563782f`). Records equal to the retail game on every gameplay field,
full length: block-cycle 527, block-refused 92, trapdoor-fall 131,
collapse-cross, collapse-stand, collapse-air 131 each, spike-walk 93,
spike-jump 91, blade-hit 168, blade-pass 181, boulder-hit 128,
zip-release 192, zip-stop 391, level-finish 123, shotgun-tiger 246,
rex-gold 1,251, rex-mask 761, shotgun-ammo 81, shotgun-no-target 351,
shotgun-release-repress 301, the key route 766 and medipacks 212
through the ring. Host and cart state hashes equal on 22 scenes at
`677afec`. The first game's cart stayed byte-identical through all of
it (`.text` 437,744 B, like for like).

What changed in method. The GPT 6.2 Sol limit stopped every agent
mid-slice; the work continued on Grok 4.7 from the uncommitted
worktrees (D18). No clean-side checker with a Great Wall replay exists
since; wall text is merged on a form check and its determinacy shown by
the implementer's and the reviewer's closed-loop replays, with every
unstated choice returned to the reader as a numbered question (Q-HAZ,
Q-HAZ2, Q-SG, Q-HARD). That loop caught what a record alone could not:
the reader's records overturned a reviewer's reading (the shotgun's
exit is latched), and reviewers found faults no record reaches (a room
bitmap sized for the first game's 139 rooms where four levels have
more; a disc spark with no lifetime exhausting the effect pool; an
ambient emitter that never restarted; a sound queue never drained).

What the user's console found that the gates had not (ROM
`a96803017a46`): arms left in the drawn pose after a holster (the
published pose is not in the state hash); static water (the texture
clock was never driven); Lara unable to leave the water (the surface
ledge climb was specified, looked implemented because the first game's
routine was in the tree, and silently did nothing: no record takes that
exit). The last is the cost of parallel second-game bodies beside
first-game routines; an inventory
(`$TR64_WORK/evidence/m10/convergence/lara-inventory.md`) found no
other defect of that kind and about a fifth of the second-game Lara
code foldable. The retail first game's shotgun, read for that
purpose, differs from this tree's OpenLara-derived one (33 updates
between shells when locked, left-arm aim): `docs/specs/wall/lara-and-enemies.md`.

Not covered at this date: the disc launchers, moving spike walls and
large rolling blades (six records equal in a candidate; in review);
the filter option as a Graphics menu item (candidate); music, saves,
title and level flow; every level after The Great Wall; a first-game
re-release carrying the retail shotgun, the voice policy and the
filter option; the convergence folds.
