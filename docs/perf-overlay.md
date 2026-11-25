# On-cart profiler overlay

ares models no RDP fill or texture-load cost and none of the M64 core's peculiarities. The cart
therefore measures itself and draws the numbers over the game, to be read from a photograph of the
screen.

## Switching it on

### Tomb Raider II (The Great Wall)

During play on pad 1, hold **R** and press **Start** to toggle the
overlay. Only Start is intercepted: R still modifies movement to a
walk, all other controls pass through, and L stays the flare button.
Start without R still pauses; the profiler starts a fresh window when
play resumes. The diagnostic uses the cooked TR2 font and does not
enable the gameplay HUD, inventory, sound or split preview. The
boot and pause cards show the full 12-digit build ID.

### Tomb Raider I

Hold **L** and press **D-pad Down** (on the press, not while held). The same chord switches it off.
Nothing else in the game uses that chord: the level-select chords are L + START and L + C-left, C-right,
C-up and C-down; the gate ROM's placement and load chords are L + Z, R, A and B (`tools/ship/gate-place.c`);
D-pad Down alone is "backward", and while L is held the game receives no input, so the chord does not move
Lara. It works anywhere the play loop runs (a level, the title, the passport and the ring). A toggle takes
effect with the next drawn frame, so no frame is measured half with and half without the overlay.

On pad 1, hold **L** and press **D-pad Up** to toggle **split preview** in
solo gameplay, independently of the overlay. Either chord requires only L and
its direction, with the stick neutral. Pad 2 never toggles an instrument.
Extra buttons or a digital stick action never toggle the instruments.
Split preview is a renderer measurement tool, not two-player co-op: it does not add a player, input,
simulation or another HUD. The top 320x120 view is the current game camera. The bottom camera turns 180°
about the game's camera target near Lara, at the same distance and height; fixed and look-at cameras pivot around
their own target. With no horizontal orbit radius it uses the original pose. The renderer searches
room bounds under the orbited eye, preferring the original camera's room; if none match it starts
visibility in the original room. The orbit does not check collision. Title, menus (including the death
and statistics pages), cutscenes and videos stay single-view. Leaving gameplay turns split off; it
does not arm while those screens are active and does not resume when they close.

## What it shows

Two columns at the top of the screen in the HUD font, refreshed twice a second. Each figure is the **mean**
(AVG) and the **worst** (WST) over the last second (two half-second windows; the first picture, for half a
second after switching on or after a level change, says COLLECTING). Times are milliseconds with one
decimal; a window never spans two levels.
In co-op the profiler still covers the full screen, including portions of both players' HUDs.

Left column, milliseconds per drawn frame. A drawn frame is what the loop does from the moment ticks are
owed to the end of its picture (the GPU has completed it and it has been handed to the display). The idle
wait for the next tick is not part of it (the loop spins on I/O and audio there, which is free).

| Row | What it measures |
|---|---|
| `FRM` | The whole frame: ticks due to the end of `draw()` (RSP/RDP complete, picture handed to the display). |
| `SIM` | `game_tick` for all ticks of the frame (summed, not the slowest). |
| `RND` | Render CPU time: back buffer acquired to the last command queued (scene build, culling, command submission and the HUD pass), less the overlay's own CPU time (`OVL`). |
| `GPU` | Time from the last submission to the frame's sync: the CPU waiting for the RSP and RDP to finish (`rspq_wait`). |
| `VSY` | Waiting for a free framebuffer (the VI still showing both), less the audio service done in that wait. |
| `SND` | Audio service inside the frame: the mixer pumps (voice mixing, music decode and the copy to the DAC buffer) and the sound tick (events, ambient sources). |
| `MUS` | Of `SND`, the music stream's decode and resampling (SPU ADPCM). |
| `IO` | TR1: I/O pump time inside the frame. TR2: the single pump immediately before the pacing check that found this frame due, outside `FRM`. Idle-loop pumps are not accumulated. |
| `OVL` | The overlay's own CPU time (drawing it; see Cost). |
| `CLK`, `BSY`, `PIP`, `TMM` | The RDP's own counters, read after the frame's sync and cleared at its start: `DPC_CLOCK`, `DPC_BUFBUSY`, `DPC_PIPEBUSY`, `DPC_TMEM`, as milliseconds of RDP clock (62.5 MHz). A counter that reads zero in the whole window shows `-` (the emulator or the core does not count it). `CLK` is the RDP's busy time; if it is close to `FRM` the frame is RDP-bound. |

Right column, per drawn frame:

| Row | What it measures |
|---|---|
| `FPS` | Frames a second (AVG), and the rate of the slowest frame (WST). |
| `TKS` | Ticks run per frame: mean to one decimal, and the most in one frame (at most four). |
| `DRP` | Ticks dropped (owed beyond four): in this window, and in this level so far. |
| `TRI` | Triangles submitted (a quad is two). |
| `TEX` | Texture uploads (a load into TMEM; one-entry cache, so each change of texture is one). |
| `KB` | Kilobytes those uploads read from RDRAM (image rows; palettes are 32 bytes each, counted in `PAL`). |
| `PAL` | Palette (TLUT) loads. |
| `ANI` | Animated-texture ranges rotated at this frame's start (non-zero only in frames where the animation advanced, six times a second). |
| `MAT` | Matrices loaded. |
| `RMS` | Visible rooms. |
| `ITM` | Items drawn (visible items with at least one part drawn). |
| `UNT` | Explicit unit dispatches (`code_call`) per frame. This is **not** the number of resident/unit crossings: calls between resident code and a loaded unit's other functions, and a unit's calls back into resident helpers, are plain jumps and are not counted; each dispatch implies at least two crossings and usually many more (each crossing refills the M64's one-entry instruction translation cache). |
| `LDS` | Code-unit loads in this window, and since boot. |
| `ABF` | Audio buffers (448 frames, 20.3 ms) the mixer produced in the frame. Two or three in one frame is the catch-up after a long frame. |

The footer is the level and the first camera's room. `DUAL` at the top means both views are being
drawn. The extra `VTRI` and `VTEX` rows show two means, **top / bottom**, not AVG / WST. `TRI` and
`TEX` stay totals, and `RMS` counts the two view lists separately (the same room can count twice).
In split mode, `RKB` is mean/peak total command-record KiB, including records from before any drain,
`SPL` is mean/most drains per frame, `SWT` is mean/worst milliseconds per frame spent draining, and
`MSL` is mean/peak simultaneous matrix slots in any one pass. `RKB`, `SPL`, `SWT`, and
`MSL` replace `ANI`, `ITM`, `UNT`, and `MAT` in split mode. Turning split on or off starts
a new sampling window.

## Cost

* **Off:** nothing is drawn, no register is touched, no allocation exists. The instrumented sites each
  test one byte (`tr64_perf_state`): the play loop (a few per frame, and one per spin of its wait loop),
  the sound pump's I/O loop, the UI hook and the frame begin and end in `plat/n64/gpu.c`. The always-on additions are four
  counters (`code_stats`: one increment per dispatch and per unit load; the texture upload byte count and
  `anim_ranges`/`anim_steps` in the frame counters).
  In TR2 the pump still precedes pacing, and the frame allowance still
  follows pacing and the code transition. Disabled instrumentation does
  flag tests only; it does not read the profiler clock (the normal pacing
  clock is unchanged).
* **On:** per frame a few dozen `CP0 Count` reads, the statistics (sums and maxima), and the overlay:
  one shaded backdrop rectangle and one batch of glyph rectangles. The text is laid out and sorted by
  glyph when a window closes (twice a second, a few thousand instructions); each frame then builds
  one RDP command list (a TLUT and an image load per distinct glyph, about 40, and a 16-byte rectangle for
  each of the 230 to 330 glyph occurrences), writes it back and queues it in passes of up to 16
  glyphs (TMEM holds 16 images and 16 palettes). The list uses cached, aligned space in the existing
  64 KiB frame record arena, live through the frame fence, instead of a separate 10.5 KiB resident
  buffer. `OVL` is the overlay's CPU time, measured with `CP0 Count`. On ares
  (LEVEL1's start, 234 glyphs) the frames' own lines give render CPU 6,670 us without the overlay and
  7,825 us with it (+1.15 ms), and time to GPU completion 6,819 and 7,956 us (+1.14 ms); `OVL` reads 1.1 ms.
  The RDP time of the backdrop and the rectangles (about 74,000 pixels shaded and 234 glyph rectangles of
  256 pixels, one cycle a pixel: roughly two milliseconds [inference]) is **not** separable from the
  scene's; it lengthens `GPU` and `CLK` while the overlay is up. Switch the overlay off to see the frame
  without it.
* Resident image: see `docs/architecture.md` section 4.2 for the measured bytes added.

The HUD's own text path (`tr64_ui_text`) was not used for the glyphs: every font glyph has its own CI4
image and its own palette, so that path costs about 2,800 CPU cycles a glyph on ares (the first version of
the overlay, 300 glyphs: 8.3 ms of CPU and about 7.6 ms of RDP clock more, measured), which would have
distorted what it measures.

## Reading the numbers

* They are the cart's own `CP0 Count` (46.875 MHz, wraps in 91 s; intervals are taken as unsigned
  differences) and the RDP's counters. On ares the RDP rows are the emulator's model: valid as counts, not
  as hardware cost.
* `FRM` is not the sum of the rows: it also holds the code-unit transition point, the HUD/pickup tick
  updates and the log. `RND`, `SWT` (split only), and `GPU` cover the render CPU and GPU waits. `RND`
  overlaps the RSP and RDP working through the queue as it is written, so a large `GPU` means the
  RDP was behind the CPU.
* A frame at 30 fps is the pacing, not a measurement of capacity: the loop waits for the clock. Read
  `FRM` against 33.3 ms, and `TKS` and `DRP` for the frames that overran.
* Split preview and co-op render two cropped views with the same focal length,
  near/far planes and fog as the single view. Texture animation advances once
  per frame; portal visibility, culling and camera matrices are per view.
* Split frames can reuse their 64 KiB record arena starting in the first half.
  Between halves (and before each nonempty co-op pickup mesh pass), they flush
  Tiny3D's triangles and fence both RSP and RDP with `rspq_wait()` before sharing
  the record and 256-slot matrix buffers again. Queued RSP matrix loads and
  RDP command execution read those buffers by address; the fence retires all
  readers before either buffer is overwritten. This boundary and additional
  drains when the record arena fills are all counted by `SPL` and `SWT`.
* A single-view world still keeps its records and matrix slots through the frame
  fence and fails if it runs out. Profiler glyph batches may drain after the
  world in any layout; their wait stays in `OVL`, not `SWT`.

## Where it lives

`engine/ui/perf.c` (accounting, layout), `engine/ui/draw.c` (the hook, `tr64_ui_text_glyphs`),
`engine/in/play.c` and `engine/in/play-tr2.c` (chords and instrumentation),
`plat/n64/gpu.c` (RDP counters; `tr64_ui_backend_glyphs`),
`engine/code/code.c` (`code_stats`). Checks: `make perf-test` (the arithmetic and the layout against a
scripted clock, with a negative control), `make input-test play-test pace-test`,
`tools/ship/perf_overlay.py --rom ROM --pack PACK --out OUT` (a shipping ROM on ares:
the chord on and off through the injected pad, the `perf:` log lines, pictures, the overlay's cost
from the cart's own frame lines). It checks the guest's source and configuration
IDs against `generated/cart_identity.h` beside ROM and the guest pack SHA-256
against PACK. For a relocated ROM, provide `--identity-header` from its build.
For TR2, run
`PYTHONPATH=test/run python3 tools/ship/tr2_perf_overlay.py --rom ROM --identity-header HEADER --out OUT`.
The live pad test checks that Walk remains latched through the Start
edge and release, that an accompanying Action also passes through,
and that Start without R still pauses.

## Posed scene measurements

`test/run/scene.sh --ares --pose FIRST.scene.json --split-camera SECOND.scene.json --render-measure SCENE.route`
checks the cart's boot source ID and configuration digest against the scene build's generated header and
uses the normal renderer with independent 320x120 scissor rectangles. Both poses must name the route's
level and the same flip map. A non-game route renders the supplied poses; a game route keeps the normal
game camera for the first view unless `FIRST` uses `"posed_game_camera": true`, and takes the flip map
from that view. Omit `--split-camera` for the 320x240 baseline. The `render-measure:` records give GPU-side
timer intervals and raw per-frame counts; divide `tex_bytes` by 1024 for `KB`. On game routes, join them
with `scene-work:` for simulation and frame work time; the initial frames before tick 1 have no matching
work record. The `pool: gpu` peak includes the whole reserved arena. `record_peak` is physical
high-water usage in a frame, `record_total` sums bytes across `spills`, and `spill_us` is their total
wait in microseconds. These runs do not draw the overlay. On ares, `RND` and counts are useful for
comparison, but ares charges nothing for RDP fill or texture loads: it cannot establish hardware frame
rate. `test/run/scene-split.py` uses the host path and `--half-view` to check the two halves against their
standalone projections and rejects a swapped reference pair.

The harness requires a fresh canonical screenshot and complete counter records from every split or
measurement run, even if the route's own observables do not ask for those artifacts.
