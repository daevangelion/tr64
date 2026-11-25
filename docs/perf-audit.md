# Performance audit for the first hardware result (2026-11-19)

Status: read-only audit by `Perf`, no renderer behaviour changed. The user's M64 shows slowdowns; the
suspects were animated textures and audio. Tags: **[measured]** numbers come from a command in the last
section; **[inference]** is reasoning about hardware that no emulator run here supports. Emulator (ares)
numbers are instruction counts: valid for COUNTS, never for RDP, uncached-store or cache-miss cost. The
overlay of `docs/perf-overlay.md` is what decides each item below; the row to read is named in each.

## Verdict

* **Animated textures are not a cost.** A step rotates at most 4 (LEVEL3A: 12) descriptor ids in a state
  array six times a second and invalidates nothing, because no texture or palette cache outlives a frame.
  Rooms with animated textures upload exactly as often per draw run as rooms without (0.95 uploads per run
  in all three groups, LEVEL2). They look expensive only because they are the larger rooms.
* **Audio is a steady 3 to 5 % of the frame and bursts to 2 to 3 ms after a long frame**; it cannot by
  itself take a frame from 30 to 20 fps. It is worth confirming with `SND`, `MUS`, `ABF`.
* **What an emulator hides and the renderer is built around**: two-cycle RDP mode for all world
  geometry (fog) over 1.1 to 2.7 times overdraw, 150 to 350 TMEM loads and 70 to 250 palette loads a frame
  each fenced by RDP syncs, and about 9,000 uncached word stores a frame to build the commands.
  The first two show in `CLK`, `TMM`, `PIP`; the third in `RND`.

## Ranked list (expected gain on the console, largest first)

Gains are [inference]; the measured counts behind them are in the sections below.

| # | Finding | Where | Per frame [measured] | Why it can be slow on the console | Smallest change | Gain |
|---|---|---|---|---|---|---|
| 1 | All world geometry is drawn in **two-cycle mode** (the second cycle is the fog blend), with Z compare and write, perspective textures | `plat/n64/gpu.c` `begin_3d_pass` (`SOM_CYCLE_2`) | 77 k to 209 k covered pixels (overdraw 1.14 to 2.72, mean 1.3 to 1.4 of 76,800) | One pixel per two RDP clocks: 100 k to 210 k pixels cost 3.2 to 6.7 ms of RDP time at 62.5 MHz before Z traffic and per-scanline overhead; ares has no fill cost, so no run here has ever seen it | Batches whose every vertex is nearer than the fog start (`fog_near` 8,192) have fog 255, so the second cycle changes nothing: draw them in one-cycle mode (same picture); the cook's `TR64_GPU_ONE_CYCLE_ELIGIBLE` flag is already carried and unused | 1.5 to 3 ms if `CLK` is near `FRM` |
| 2 | **Texture loads**: one-entry image cache, a load per change of texture (batch-major order alternates textures); each is `t3d_tri_sync` + `rdpq_exec` of a record with pipe, tile and load syncs, then `LOAD_TILE` of the CI4 rows read as I8 | `tr64_gpu_material` | 64 to 354 uploads (mean 118 to 178), 44 to 255 palette loads (mean 71 to 100), 14 KB of records | Every record drains the RDP pipeline; `LOAD_TILE` issues one short burst per row (8 to 32 bytes) where `LOAD_BLOCK` is one burst; a stream switch RSP/RDP per record | 95 to 100 % of descriptors are the whole width of their cluster (LEVEL1/2: all 888/943), so their rows are contiguous: use `LOAD_BLOCK` (needs `w/2` a multiple of 8; 8-wide glyphs excepted) | 0.5 to 2 ms of RDP time (`TMM`, `PIP`) |
| 3 | **Command words are written uncached**: the 64 KiB record buffer, the vertex scratch and libdragon's own rspq buffers are `malloc_uncached` | `gpu.c` `record`, `vertices`; libdragon `rspq.c:458,730` | 14,064 B records + 21,180 B triangle commands = about 8,800 words (LEVEL1 start) | An uncached store stalls the CPU for tens of cycles on the console; ares counts it as one | Build records and scratch vertices in cached memory and `data_cache_hit_writeback` the lines at `execute()` (the triangle path is libdragon's) | 0.5 to 1.5 ms of `RND` |
| 4 | **The frame is serialized**: `frame_end` waits for the RDP (`rspq_wait`) before the next tick's simulation, audio and I/O can run | `tr64_gpu_frame_end` | `GPU` row | While the RDP drains its tail the CPU does nothing useful; with 30 Hz pacing this only costs when the frame already exceeds 33 ms | `rdpq_detach_show` and move the wait to the next `frame_begin`, where the 64 KiB record buffer is reclaimed anyway | the smaller of `GPU` and the next frame's `SIM`+`SND`+`IO`: 1 to 4 ms, only on overrunning frames |
| 5 | **Mesh batches go through an uncached vertex copy, a per-vertex recolour and a sync point each** | `vertices()`, `tr64_gpu_batch` | `recolour_us` 0.4 ms on ares for 15 parts; up to 70 vertices x 16 bytes per batch | Uncached copies (item 3) plus `rspq_syncpoint_new`/check per batch | Cache the recoloured copy per item per frame; fence once per item | 0.3 to 1 ms in rooms with enemies |
| 6 | **HUD text is one palette load, one image load and a rectangle per glyph** | `plat/n64/ui.c` `tr64_ui_backend_sprite` | 28 µs of ares CPU a glyph (a 300-glyph screen: 8.3 ms and 7.6 ms of RDP clock) | Every font glyph has its own CI4 image and palette: nothing repeats | Use the glyph batch (`tr64_ui_backend_glyphs`, in the tree for the overlay: 1.1 ms of ares CPU for 234 glyphs) for `tr64_ui_text` | Menus, passport and ring pages; not play |
| 7 | **Clears**: colour (150 KiB) and Z (150 KiB) every frame | `tr64_gpu_frame_begin` | 2 fills of 76,800 pixels | 0.3 ms each at 4 pixels a clock, more with RDRAM contention | Skip the colour clear when the picture is enclosed; no cheap Z change | 0.3 to 0.6 ms |
| 8 | **Audio**: mixer and music decode on the main thread | `engine/snd`, `plat/n64/audio.c` | 49.2 buffers/s of 448 frames; 0.66 ms of ares CPU per buffer with music alone, 0.9 with one voice | see Audio | none needed unless `SND` is large; if so lower the music rate | at most 1 to 2 ms |
| 9 | **Unit crossings** between resident code and code-unit pages (the M64's instruction translation cache holds one 4 KiB entry; a lookup scans up to 32 TLB entries) | `engine/code`, `game/dispatch.c` | 0 to 6 explicit dispatches per tick at level starts (LEVEL2 6, LEVEL3A 1, LEVEL8A 1, others 0) | each dispatch is at least two crossings and the unit's calls back to resident helpers are more; 2 + 2n crossings of up to 35 clocks | none yet; read `UNT` in a fight | under 0.5 ms [inference] |
| 10 | Animated-texture step | `texture_apply` | 4 to 12 stores, six times a second | none | none | none |

## (a) Animated textures

What the renderer does. The game's counter `textureTicks` gives `texture_animation_step = (ticks-1)/5`
(`game/view.c`): a new step every five ticks (six a second). `tr64_render_game_frame` hands it to
`tr64_render_texture_apply` at the start of the frame (`engine/render/texture.c`): for each cycle
(a group of members, `cycle[0]` of them) it adds the steps owed to the phase and rewrites `n` entries of the
u16 `texture_map` (logical descriptor id to content id). `TX64` itself is immutable. Each run's content is
`texture_map[logical]` when the run is not flat.

* **Is a cached texture or palette invalidated?** No, and there is nothing to invalidate: `begin_3d_pass`
  resets `texture_valid` and the 16 palette ids at the start of every frame and menu pass, so each frame
  reloads from empty, and within a frame the cache is the last image plus the 16-slot palette table keyed
  by `palette & 15`. The cache key is the content's image offset, row, origin and extent, so a rotation
  changes which image a run asks for, never whether it asks.
* **Cost of a step** [measured structure]: LEVEL2, LEVEL6, GYM have one group of 4 members (6 animation
  words); LEVEL3A three groups (16 words); LEVEL1 none (the cook keeps only groups that touch referenced
  objects, so LEVEL1's water rooms are not animated at all). A step is 4 map stores per group.
* **Uploads in rooms with animation** [measured: host renderer with the cart's counting code, Lara placed
  in every room, 3 ticks, `$TR64_WORK/tmp/Perf/place_*.json`]:

  | Level, rooms | n | uploads mean (max) | palette loads mean | triangles mean | overdraw mean (max) |
  |---|---:|---:|---:|---:|---:|
  | LEVEL2 water | 9 | 91 (114) | 59 | 871 | 1.20 (1.52) |
  | LEVEL2 dry, animated | 8 | 153 (271) | 83 | 1,166 | 1.54 (2.14) |
  | LEVEL2 plain | 70 | 118 (305) | 71 | 920 | 1.38 (2.68) |
  | GYM pool room | 1 | 141 | 50 | 854 | 1.15 |
  | GYM other rooms | 18 | 177 (354) | 97 | 943 | 1.29 (2.03) |
  | LEVEL1 (no animation) | 38 | 150 (353) | 80 | 1,206 | 1.30 (2.72) |

  Per run (static analysis of the pack, every room alone): uploads/runs = 16.8/17.7 (LEVEL2 water),
  35.8/37.4 (dry, animated), 13.8/14.6 (plain): 0.95, 0.96, 0.95. Animated runs are 1.7 of 17.7 in water
  rooms and 5.4 of 37.4 in dry animated rooms. With the camera settled, the counts at animation steps 2,
  5 and 8 are identical (LEVEL2 room 29: 116 uploads/79 palettes at all three; rooms 43 and 63 likewise).
* **Is every range advanced and re-uploaded when not visible?** Every cycle is advanced every time the
  step moves (visibility is not consulted: 6 to 16 words of work); nothing is uploaded for a range that
  is not drawn, because uploads happen only for runs that are submitted.
* **Uploads batched or per draw?** Per material change: one record per change of image and/or palette
  (`tr64_gpu_material`), each `t3d_tri_sync` + `rdpq_exec`; no batching across runs.

Smallest change: none needed for animation. The uploads are item 2.

## (b) Audio

* **Sizes** [measured from source]: AI output 22,050 Hz (the DAC divider gives 22,047), three buffers of
  448 frames (20.3 ms each, 61 ms queued), a 98,304 B arena (6 voices with two 4 KiB stream blocks each,
  25 KB of resident samples) and 19,984 B for music (three 4 KiB windows). Music is mono SPU ADPCM decoded
  in `tr64_music_mono_pcm` from inside the mixer.
* **Scheduling.** `tr64_snd_pump` (I/O pump 24 times, stream prefetch, music pump, then `tr64_audio_pump`
  which mixes every free buffer) runs: on every spin of the wait loop; after each game tick; every 8 ms
  while `frame_begin` waits for a framebuffer; and in the loader and message waits. It does **not** run
  during scene submission (about 7 ms on ares, more on the console). `tr64_snd_tick` (events, the ambient
  sources, the music request) runs after each tick.
* **A 50 ms frame.** The queue holds 61 ms, so the mixer catches up by mixing up to three buffers in one
  pump (ares: 1.5 ms for two); the work is bounded by the queue, never a burst beyond it, and lands in
  whichever of tick, VI wait or idle comes first. Output underruns need about 55 to 60 ms without a
  pump. `ABF` is the number of buffers mixed in a frame; `SND` the time.
* **Cost** [measured on ares, instruction time]: music alone 662 µs per buffer (decode 496), 32 µs of
  copy to the uncached DAC buffer; peak 997 µs; one extra voice about 250 µs; 49.2 buffers a second means
  3.2 % of the CPU with music. Each streamed 4 KiB block is CRC-checked on completion (`tr64_crc32`, a byte
  table loop: about 0.35 ms by estimate [inference]), once per 325 ms of music and per sample block.
* **Hot layout.** Code: yes. `tr64_snd_mix`, `prefetch`, `tr64_snd_pump`, `tr64_audio_pump`, the three
  `audio_*` SDK functions, `tr64_io_pump`, `tr64_music_pump` and `tr64_music_mono_pcm` are the idle prefix
  of `.text` (`plat/n64/hot.ld`); `tr64_music_decoder_sample`, `poll_cache`, `request`, `sample_data`,
  `find_block`, `span_frames`, `tr64_pack_stream_poll` and `tr64_crc32` are not. Data: the `s` and `m`
  pointers are in `.sbss` (hot group) and `profile` is listed; the structures live in the audio arena and
  the DAC buffers are uncached, touched twenty times a second.
* Not a cause of 30 to 20 fps by itself; `SND` and `ABF` will say.

## (c) Everything else an emulator hides

* **Syncs.** Mid-frame there are only RDP pipe, tile and load syncs (no `SYNC_FULL`: one per frame, from
  `rdpq_detach`): every raw record starts with `SYNC_PIPE` and `SYNC_TILE` and each load has a `SYNC_LOAD`,
  and each `execute()` is a `t3d_tri_sync` (the RSP flushes its triangle buffer and the RDP stream is
  switched to the record and back). With 155 uploads and 68 palette loads that is about 220 pipeline drains
  and 400 stream switches a frame at LEVEL1's start [measured counts, cost inferred].
* **`load_block` against `load_tile`.** `LOAD_TILE` throughout; 95 to 100 % of descriptors would allow
  `LOAD_BLOCK` (item 2).
* **TLUT loads.** Sixteen TLUT slots keyed by `id & 15`; a load only on a change, merged into the image's
  record when both change. 71 to 100 a frame.
* **Fill rate.** Item 1. Menu ring: a second pass (`tr64_gpu_menu_begin`) with its own Z clear, and
  `tr64_ui_shade` rectangles (blend, 1 cycle) over the picture: each full-screen shade is about 1 ms of RDP time
  [inference]. Water tint is baked into vertex colours: no pass. Sprites go through the matrix, material and
  batch path one at a time: 0 in LEVEL1's start, more near flames.
* **Clears.** Item 7.
* **Overdraw** [measured, host rasterizer's covered-pixel count over the 76,800 visible]: LEVEL1 1.30 mean,
  2.72 worst of 38 rooms; LEVEL2 1.38, 2.68 of 87; GYM 1.29, 2.03 of 19. Pixels that fail the Z test are
  counted: the RDP shades them too.
* **Instruction translation.** The M64 core's mini TLB is one 4 KiB entry and a miss scans the TLB from the
  previous hit: within the single resident mapping a new page is a quick repeat hit, between resident code
  and a unit it is up to 32 clocks. Explicit dispatches are rare in play (0 to 6 a tick at the starts
  measured); the crossings are not counted by `UNT` (calls into a loaded unit's other functions and its
  calls back are plain jumps) and the estimate for a fight, `2 + 2n` crossings per dispatch at
  `n` helper calls, is under half a millisecond unless dozens of items are active.

## What was run

* Counts, overdraw and animation steps: host build (`make host TR64_LEVEL_PACK=.../pc-campaign21-video.pack`),
  `tr64-host --game-input X --game-level L --game-ticks 3 --game-ui --game-live` with a `start ROOM X Y Z 0`
  line in the input file for every room; the `game-render` and `raster-fill` lines. Static pack analysis
  with `tools/cook/n64_pack.py` (rooms' runs against the texture cache rules of `tr64_gpu_material`).
* Cart phases: the ares log of `tools/ship/perf_overlay.py` (`render-phases:` lines, LEVEL1 start:
  submit 4.4 ms, of which triangle expansion 1.3, texture uploads 1.1, matrices 0.4, vertices 0.5,
  recolour 0.4; culling 0.6; the game's view pull 0.5; render CPU 6.7 ms in all).
* Audio: the `audio-buffer:`, `music:` and `audio-output:` lines of the same log.

## M10 Measured Addendum (2026-11-23..24, `7a5d689`)

### Factual correction to item 1 (1-cycle RDP mode)

* **[measured override]** Item 1 above stated that Cycle 2 is a no-op when `fog == 255` and that
  `TR64_GPU_ONE_CYCLE_ELIGIBLE` marks batches nearer than `fog_near` (8,192). Direct inspection and
  measurement of `plat/n64/gpu.c` and the cooker establish the opposite:
  * Cycle 2 computes `1.5 * COMBINED` (`PRIM = 128`, `COMBINED + COMBINED * PRIM`) because cooked
    `RG64` room vertex RGB is pre-divided by 1.5. Switching to 1-cycle mode without re-cooking darkens
    rooms by 33% and alters 262/5,504 RGB5 pairs by ±1 LSB.
  * `TR64_GPU_ONE_CYCLE_ELIGIBLE` indicates `gain <= 1.0`, not distance `< 8192`.
  * Do **not** enable 1-cycle world mode without a cooker format change.

### Emulator D-cache modeling note

* **[measured]** `ares` charges 80 VR4300 cycles per 16 B `cache 0x15` writeback and 0 cycles per
  uncached store (the inverse of real VR4300 hardware). Cached RDP record / vertex scratch writeback
  (item 3, Wave 1) therefore appears slower inside `texture_us` on `ares` while saving an estimated
  1.5–3 ms on real N64/M64 hardware.

### Landed optimizations (Waves 1–3, pixel-exact)

* **Wave 1 (`1b2df21`)**: Items 2, 3, 5 — cached `memalign(16)` GPU workspace with `cache 0x0d`/`0x15`,
  RDP `LOAD_BLOCK` with exact `dxt[16]` LUT (covering 59.3–68.9% of CI4 uploads), branchless
  `matrix_encode`, fused cached `vertices()` + 2-half `fences[2]` scratch ring, scalar Z->X->Y
  `batch_visible()`, combined `floor_ceil_div()`, 8 B packed `draw_work`.
* **Wave 2 (`0716e93`)**: Eliminated 16 KiB direct-mapped I-cache thrash between `game/matrix.c` and
  `game/view.c` (`game/module.mk` adjacency), fast TR2 packed pose/lerp/rotate, cached TX64 pointers
  + `!flat`-guarded `last_mat_key`, load-time O(1) `mesh_prep`/`mesh_slot_map`/`static_mesh_map`,
  insertion sort for `used <= 24`, lockless register-held RSPQ triangle expansion (`tr64_gpu_batch`).
* **Wave 3 (`7a5d689`)**: Item 4 — play-mode (`TR64_PLAY && !TR64_TEST`, perf overlay off) async RDP
  tail overlap via `rdpq_detach_show()` with `tr64_gpu_sync()` before Z-buffer/pool reuse; `gpu.tri_dirty`
  eliminating redundant `t3d_tri_sync()` commands; direct lockless RSPQ `execute()` + shared material
  `SYNC_PIPE`/`SYNC_TILE` header; bump-allocated 2-half `gpu.scratch` ring (`vertex_wait_us: 181.9 -> 0.0 us`);
  split 32-bit `batch_visible_room` / UB-free `batch_visible_mesh` / fast `portal_rect` / `submit_view_split`.
* **Skipped item 6 (HUD glyph batching)**: deferred due to outside-frame `tr64_ui_message` calls,
  TR2 odd-width/sub-rect sprites, `x < 0` clip vs drop semantics, and `plat/n64/ui.c` state invalidation.

### Cumulative ares 4 MiB measurements (`TR64_ARES_HOST=<remote-host> ARES_EXPANSION_PAK=false`)

| Scene | Metric (`work_us` mean / max) | Pre-Wave-1 | Wave 3 (`7a5d689`) | Cumulative Delta | Pixel / Trace SHA-256 |
|---|---|---:|---:|---:|---|
| TR2 `tr2-spider-shoot` (381 ticks) | `work_us` mean / max | `20,644` / `26,100+` | **`15,321.5` / `19,140`** | **`-5.32 ms` (-25.8%)** | Exact (`ced03054...` / `af099b6f...`) |
| TR2 `tr2-crow-shoot` (561 ticks) | `work_us` mean / max | `20,974` / `25,600+` | **`16,386.0` / `19,599`** | **`-4.59 ms` (-21.9%)** | Exact (`c223c4af...` / `7d46b13d...`) |
| TR1 `snd-bridge-perf` (180 ticks) | `work_us` mean / max | `23,325` / `28,308` | **`20,860.8` / `25,341`** | **`-2.46 ms` (-10.6%)** | Exact (`f8b6a0c9...`) |

All three waves preserve **0 B** net resident image bloat on shipping play carts (`TR1: 551,384 / 622,592 B`,
`TR2: 469,064 / 622,592 B`, hot render window `16,128 / 16,384 B`).
