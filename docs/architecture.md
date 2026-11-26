# tr64 Engine Architecture

**tr64** is a Nintendo 64 engine that runs **Tomb Raider I (1996)**, **Tomb Raider II (1997)**, and **Tomb Raider III (1998)** in the console's **stock 4 MiB of RDRAM** (no Expansion Pak required), cooked from the player's own PC (Steam or CD-ROM) or PlayStation copy.

- Human build guide: [BUILDING.md](BUILDING.md)
- Player manual & controls: [MANUAL.md](MANUAL.md)
- Native binary formats: `docs/formats/`
- Empirical measurements & M0–M10 milestone records: [evidence.md](evidence.md)
- Provenance rules: [PROVENANCE.md](PROVENANCE.md)

---

## 1. Scope

- **Hardware target**: Stock 4 MiB Nintendo 64 (`Nintendo64/ExpansionPak=false`), 64 MiB cartridge ROM, 256 Kibit (32 KiB) cartridge SRAM, 320×240 16-bit color framebuffer with 16-bit Z-buffer.
- **Games supported**:
  - **Tomb Raider I** (`TR64_GAME=1`): full 15-level campaign, Lara's Home, 4 in-engine cutscenes, title screen, SFX, optional CD-DA music and H.264 FMVs, plus 2-player split-screen co-op. Cooked from PC (`*.phd`) or PlayStation (`*.PSX`) releases.
  - **Tomb Raider II** (`TR64_GAME=2`): PC (`*.TR2` + `MAIN.SFX`) levels, title backdrop (`TITLE.PCX`), 3D inventory/options ring, SFX, vehicles (Venice boat), climbing, flares, harpoon/grenade/M16/uzis/shotgun, traps, enemies, and SRAM saves.
  - **Tomb Raider III** (`TR64_GAME=3`): PC (`*.TR2` + `MAIN.SFX`) India levels (`JUNGLE`, `TEMPLE`, etc.), title backdrop (`TITLEUK.BMP`), sprint/crouch/crawl/monkeyswing, unified Lara/animation/camera controller with 30 Hz interpolation, 3D inventory/options ring, SFX, and SRAM saves.
  - **Tomb Raider IV & V** (`game/tr4/`, `game/tr5/`): directory structure reserved on top of `game/common/`.
- **Per-game carts, one engine (D17)**: One shared engine (`engine/`, `plat/`, `game/common/`) compiles per-game N64 ROMs via `TR64_GAME=1|2|3` (game-specific rules fold to compile-time constants and unused game code is omitted). On host, TR1/TR2 share one binary (`TR64_GAME=1|2`) and TR3 uses `TR64_GAME=3`.
- **Cross-game Lara Moves**: TR1 and TR2 packs can optionally graft TR2 (`WALL.TR2`) and/or TR3 (`JUNGLE.TR2`) Lara animation banks at cook time (`tools/cook/lara_graft.py`), exposing a live **Lara Moves** (`TR I` / `TR II` / `TR III`) toggle in the Options ring.
- **Determinism**: Single-threaded simulation plus hardware interrupts. All simulation state in `game/` is integer and fixed-point at a fixed 30 Hz tick rate — identical on host and cart, with no floating point in `game/`.

---

## 2. Top-level structure

```text
tr64/
  engine/
    core/         frame loop, clock, ring log, assertions, unwinder, fatal QR encoder
    mem/          boot pool carve, bump/fixed-slot arenas, budget enforcement
    io/           asynchronous cart DMA reads, 4 KiB piece scheduler, LZ4 decompression
    res/          content-addressed pack mount, level loader, sound/image/stream views
    code/         TLB code units: load, unload, dispatch tables (section 6)
    render/       portal visibility, skybox, rooms, meshes, sprites, textures, 2D HUD
    snd/          spatial effect mixer, SPU ADPCM music streamer
    media/        H.264 video playback & cinema transitions (lives in a code unit)
    ui/           bitmap font, 3D ring frontend, slot picker, profiler overlay
    save/         3-slot SRAM save manager and integrity checks
    in/           N64 pad normalization, frame pacer, TR1/TR2/TR3 play loops
    test/         route VM, state hashing, scene runner, stats cards
  game/
    module.mk     top-level game build dispatcher
    common/       shared Lara, animation, camera, world, UI/ring, inventory, save, math, tables
    tr1/          TR1-specific objects, hazards, enemies, and TLB code units
    tr2/          TR2-specific objects, hazards, enemies, vehicles, validation, and TLB code units
    tr3/          TR3-specific floor/world, Lara adapter, jungle entities, ring, and TLB code units
    tr4/, tr5/    reserved for Tomb Raider IV and V
  plat/
    n64/          libdragon + tiny3d backend, TLB runtime, fatal screen, hot.ld, build_sdk.py
    host/         32-bit host backend enforcing cart pool and I/O limits
  include/tr64/   public engine/game contracts and per-game pool tables (pools*.def)
  tools/
    build.py      one-command Steam/CD-ROM/ISO/GAME.GOG/CUE -> .z64 ROM builder
    cook/         PC/PSX/TR2/TR3 readers -> IR -> native TX64/RG64/MG64/SN64/MU64/UI64 packs
    fatal/        QR & fatal-screen decoder (decode.py, photo_sim.py)
    routec/       deterministic scene route compiler
    provcheck/    provenance, SPDX, and binary-artifact gate
    ship/         boot/save/pace/delivery gates
  test/           host unit tests, oracle suites, directed tests, cart scenes
  docs/           architecture, formats, manual, building guide, wall specs, evidence
```

**Module boundary rules**:
- `engine/` and `plat/` interact with `game/` only through `include/tr64/game.h` (except the TR3 play loop's `game/tr3/tr3.h` hooks and host test drivers).
- `game/` sees the engine only through `include/tr64/eng.h` and never includes libdragon headers.
- `plat/n64/` is the only directory that includes libdragon or tiny3d.

---

## 3. Platform

- **Toolchain & SDK** (pinned in `Makefile`, verified by `make sdk-check` against `$N64_INST/MANIFEST.json`):
  - `mips64-elf` GCC toolchain (`N64_GCCPREFIX`, GCC 16.2).
  - **libdragon**: `daevangelion/libdragon`, branch `tlbcode` (`dde6197a28aaa26b721685bfd5f2eabd32e89aa5`).
  - **tiny3d**: `daevangelion/tiny3d`, branch `tr64` (`485fe3ad0dfc5458d84aeac8b41b9f2427e50c71`), with per-corner triangle UV commands (D4).
  - Built cleanly from pinned git archives via `plat/n64/build_sdk.py`.
- **Compilation**: `-std=c99 -O2 -Wall -Wextra -Werror` (`-std=gnu99` for `plat/n64/`), `N64_TLBCODE=1`, no C++, no exceptions, and all `malloc`/`free`/`printf` calls wrapped so no heap allocation occurs after boot and no libc stdio/float formatting enters the cart image (`test/run/fmt-link-gate.py`).
- **Display & Pacing**: 320×240, RGBA5551, 16-bit Z-buffer, double-buffered. The play loop paces simulation at 30 Hz against the hardware clock (`engine/in/pace.h`), advancing at most 4 ticks per drawn frame and never drawing without a new tick. Test scenes run strictly 1 tick per frame.
- **Storage**: Up to 64 MiB ROM (checked at link time); 256 Kibit (32 KiB) SRAM (`N64_ROM_SAVETYPE=sram256k`), `N64_ROM_EXPANSIONPAK=unused`.
- **Emulator**: Patched `ares` with `Nintendo64/ExpansionPak=false` via `test/run/ares.sh` (local or remote via `TR64_ARES_HOST`).

### 3.1 Diagnostics: the fatal screen and its QR code

Every CPU exception, assertion, `sys_error`, or RSP crash lands in `plat/n64/fatal.c`: resident, allocation-free, independent of SDK stdio/inspector, and identical across shipping and test builds. A nested fault preserves the first report (`NESTED FATAL - FIRST REPORT RETAINED`).

- **Layout (320×240)**:
  - **Right**: QR version 13-M (69×69 modules at 3 px + 4-module quiet zone = 231×231 px at `(89, 9)`), fixed mask 4, encoded in `engine/core/qr.c` (ISO/IEC 18004:2015, 532 B stack, zero heap).
  - **Left strip (`x < 85 px`)**: `TR64 FATAL`, `BUILD <id>`, message, `PC`, `RA`, `SP`, `SR`, `CAUSE`, `EPC`, 64-bit `BVA`/`EHI`/`CTX`, vector/TLB/unit/flags summary, 4 instruction words around `EPC` (`E`) and unit entry via `kseg1` (`K`) and mapped VA (`M`), `PM`/`UM` TLB entries, `SPSTAT`/`DPSTAT`, and stack backtrace.
- **Exception-entry stash**: `sys_fatal_install` places 16-byte stubs at `0x80000000`, `0x80000080`, `0x80000100`, and `0x80000180` jumping to `sys_vec_common` (`plat/n64/fatal-capture.S`, in `.data` so reachable without the TLB). Non-interrupt/syscall/CpU exceptions snapshot entry-time `Cause`, `Status`, `EPC`, 64-bit `BadVAddr`/`Context`/`XContext`/`EntryHi`, `sp`, and `ra` before jumping to `inthandler`.
- **Payload format (`TR64.2.<base-45>.`)**: RFC 9285 base-45 encoding of a big-endian binary record (at most 316 bytes) carrying kind, ExcCode, 12-hex build ID, PC/RA/EPC, 64-bit CP0 registers, level ID, tick, mapped unit bitmask, flags, low 32 bits of `$1–$25/$28–$31` plus non-sign-extended upper-half mask, instruction words at EPC and unit entry (read safely via `kseg1` / `tlbp`+`tlbr` without faulting), PC/unit TLB entries, up to 7 unwinder call sites (`engine/core/unwind.c`), and error text.
- **Decoder & Tests**: `tools/fatal/decode.py [--elf tr64.elf] [--image photo.png] [TEXT]` resolves build IDs, symbolicates PC/RA/EPC/backtrace via `mips64-elf-addr2line`, compares RDRAM instruction words against the ELF, and decodes TLB entries. Verified via `make qr-tests` and `make fatal-tests`. Empirical QR photo-simulation trials and binary size costs are archived in [evidence.md §1](evidence.md#1-fatal-qr-photograph-simulation--binary-cost).

---

## 4. Budgets

### 4.1 Memory (4 MiB = 4096 KiB)

Memory is carved once at boot by `mem_boot_carve` from a per-game compile-time table (`include/tr64/pools.def` for TR1, `include/tr64/pools-tr2.def` for TR2, `include/tr64/pools-tr3.def` for TR3):

| Pool | Kind | TR1 (KiB) | TR2 / TR3 (KiB) | Holds |
| --- | --- | ---: | ---: | --- |
| `image` | IMAGE | 608 | 608 | Resident `.text`, `.rodata`, `.data`, `.bss` (checked on cart ELF by `size_gate`) |
| `stack` | FIXED | 64 | 64 | Main stack at top of RDRAM |
| `fb` | FIXED | 300 | 300 | Double-buffered 320×240 RGBA5551 framebuffers |
| `zbuf` | FIXED | 150 | 150 | 320×240 16-bit Z-buffer |
| `code` | CARVE | 96 | 96 | 24 × 4 KiB frames for paged TLB code units during play |
| `gpu` | CARVE | 232 | 232 | `rspq`/`rdpq` buffers, matrices, vertex scratch, per-frame GPU state |
| `audio` | CARVE | 128 | 128 | Effect voices, resident SFX, stream windows, music transport |
| `libheap` | CARVE | 64 | 64 | Frozen libdragon boot-time `malloc` region |
| `level` | CARVE | 1472 | 2040 | Single per-level arena carved in order: core, textures, rooms, meshes |
| `state` | CARVE | 192 | 296 | Game state, item/AI/nav pools, effects, drawables, menu view, save buffer |
| `io` | CARVE | 48 | 48 | Cart DMA staging pieces and LZ4 decompression window |
| **Total** | | **3354** | **4026** | |
| **Reserve** | | **742** | **70** | Unassigned headroom |

**Invariants**:
- Pools are fixed-order bump or fixed-slot arenas; none grows or borrows implicitly from another. Overflow calls `sys_error` naming the pool in every build.
- `test/run/size_gate` fails any cart build whose linked resident ELF exceeds `image`.
- The `-m32` host build carves identical pool sizes and enforces identical failures.
- Each level loads whole into `level` (core -> textures -> rooms -> meshes). Full-screen media (H.264 video, title backdrops) runs when no gameplay level is resident or via explicitly named idle-pool borrows in `engine/mem`.
- Only frequent SFX are resident in `audio`; other SFX and music stream from cart ROM through `engine/io`.

### 4.2 Frame

- **Target**: 33,333 µs (30 Hz); gate ceiling 50,000 µs (emulated production-path time from backbuffer acquire to GPU completion).
- **Cart I/O cap during play**: At most 32 KiB per frame in 4 KiB pieces through `engine/io` (modeled at 195 µs/KiB on host).
- **Code-unit cap**: At most one TLB code unit loaded per frame during play.
- **On-hardware profiler**: Built-in overlay (`L + D-Pad Down` in TR1, `R + Start` in TR2/TR3; `docs/perf-overlay.md`) showing frame phases, RDP counters, and triangle/upload counts.

### 4.3 Cart (64 MiB)

| Content | Budget | Notes |
| --- | --- | --- |
| Resident image, code units, file table | 1 MiB | Pack payload begins at ROM offset `1048576` (`0x100000`) |
| Levels, cutscenes, title, shared SFX | Deduplicated LZ4 blocks | Content-addressed across levels (`pack_merge.py` deduplicates identical SFX/animation/texture chunks) |
| TR1 Music (optional) | ~19–20 MiB | 22.05 kHz mono PlayStation SPU ADPCM (`MU64`, D5) |
| TR1 Video (optional) | ~15–16 MiB | 320×240 15 fps baseline-constrained H.264 + SPU audio (`VIDEO_`/`FILM_`) |

`Makefile` asserts the final `.z64` never exceeds 64 MiB.

---

## 5. Data flow

```text
TR1 PC   (*.phd, TITLEH.PCX, GAME.GOG, FMV/*.RPL, CD-DA) --+
TR1 PSX  (PSXDATA/*.PSX, DELDATA/*TIT*.RAW, BIN/CUE)    ---+--> tools/cook/ --> IR --> native sections --> .pack --> ROM
TR2 PC   (data/*.TR2, MAIN.SFX, TITLE.PCX)              ---+       (optional lara_graft.py for cross-game Lara Moves)
TR3 PC   (data/*.TR2, MAIN.SFX, pix/TITLE*.BMP)         ---+
```

- **Human entry point**: `tools/build.py` auto-detects Steam installs (including reading raw Mode-1/2352 `GAME.GOG` and Mode-2/2352 PSX `.bin`/`.cue` ISO9660 images in pure Python), PC CD-ROMs, and PSX CD-ROMs, cooks levels in parallel via `tools/cook/pack_level.py`, merges them via `tools/cook/pack_merge.py`, and invokes `make rom`.
- **Unified IR (`tools/cook/ir.py`)**: All readers (`src_pc.py`, `src_psx.py`, `src_tr2_tr3.py`) emit one canonical level representation (rooms, sectors, floor data, portals, lights, static instances, meshes, texture rectangles/palettes, animations, items, boxes/zones, cameras, sound banks, cutscenes).
- **Optional Lara Moves grafting (`tools/cook/lara_graft.py`)**: When `--lara-tr2-donor` (`WALL.TR2`) and/or `--lara-tr3-donor` (`JUNGLE.TR2`) are provided while cooking TR1/TR2 levels, the cook appends donor Lara animation clips, frames, and state-change tables into the level's native core so the runtime can switch movesets live.
- **Native cooked sections (`docs/formats/`)**: All big-endian, 16-byte aligned, mapped and used directly in place with zero parse pass on the cart:
  - **Core (`0x0001..`)**: Rooms, sectors, floor data, items, animations, meshes metadata, boxes/zones, sound map (`n64_core.py`).
  - **Textures (`TX64`, `0x10000`)**: CI4 tiles with per-cluster 16-color palettes (PSX lossless; PC quantized per cluster, D10), pre-split to fit 4 KiB TMEM (`n64_tex.py`).
  - **Geometry (`RG64` `0x20000`, `MG64` `0x30000`)**: Shared-vertex batches (<= 70 vertices, 16-byte `T3DVertPacked` pairs) with per-batch AABBs, faces grouped into texture runs with pre-encoded corner UV codes; split-texture strips are merged into contiguous deferred batches to minimize vertex loads and TMEM thrashing (`n64_geo.py`).
  - **Sound (`SN64`, `0x40000`)**: Deduplicated SFX metadata + ADPCM/PCM stream blocks (`n64_snd.py`).
  - **Title backdrop (`UI64`, `0x50000`)**: 320×240 CI8 (from indexed `.PCX`) or RGBA5551 (from RGB `.BMP` or PSX RNC2 `.RAW`) (`n64_ui_image.py`).
  - **Music (`MU64`, `0x60000`) & Video (`VIDEO_`/`FILM_`)**: Streamed SPU ADPCM music tracks and H.264 cutscenes.
- **Content-addressed `.pack` (`n64_pack.py`)**: 16 KiB LZ4-HC compressed blocks deduplicated across levels by SHA-256. Each directory entry carries a game tag (`0` = TR1, `2` = TR2, `3` = TR3) so cart play loops and save identities verify pack compatibility.

---

## 6. Code residency

All program text outside boot and exception vectors runs from mapped `kseg2` (`0xC0001000` for resident text, wired TLB entry). Cold subsystems are linked into fixed-VA **TLB code units** (`0xC1000000 + slot * 0x80000`), stored in ROM, and demand-loaded into 4 KiB RDRAM frames through `engine/code/` (libdragon `tlbcode`).

The cart executes in **32-bit kernel mode** (`KX=SX=UX=0`) so both VR4300 hardware and FPGA cores translate `kseg2` instruction fetches uniformly (`docs/hw/m64-tlb.md`). All PI CPU accesses use `kseg1`, the TLB refill vector is `0x80000000`, and `plat/n64/code.c` guards against skipped `tlbr` instructions.

| Profile | Code Units |
| --- | --- |
| **Engine common** (`engine/`) | `init` (boot-only), `load` (level/pack loader), `medium` (SRAM I/O), `front` (UI/HUD/ring renderer), `video` (TR1 H.264/YUV player, borrows idle level pool), `test` (non-shipping test builds only) |
| **TR1** (`game/tr1/units.def`, `game/common/units-trace.def`) | `objects`, `enemy_peru`, `enemy_greece`, `enemy_rat`, `enemy_dinosaurs`, `enemy_mutant`, `enemy_atlanteans`, `enemy_gunmen`, `enemy_henchmen`, `enemy_natla`, `enemy_adam`, `enemy_doppelganger`, `enemy_encounter`, `inv`, `save`, `trace` (play builds) |
| **TR2** (`game/tr2/units.def`) | `tr2_wall`, `tr2_opera`, `tr2_ai`, `tr2_creatures`, `tr2_boat`, `tr2_urban`, `tr2_validate`, `inv`, `save` |
| **TR3** (`game/tr3/units.def`) | `tr3_entities` |

**Rules**:
- Units contain `.text` and `.rodata` only; writable state lives in resident `.data`/`.bss` or carved pools.
- Resident code calls units exclusively through `engine/code`'s dispatch table (`tr64_unit_<name>`), never caching unit pointers across frames. Units call resident code directly and never call each other.
- `test/run/refs-check.py` gates every ROM build against `<elf>.tlbcode.refs`, allowing only dispatch rows and verified `test/run/tlbcode-refs.allow` entries.

### 6.1 Hot code placement

The VR4300 I-cache is 16 KiB, direct-mapped by VA (32-byte lines). `plat/n64/hot.ld` (passed via `--section-ordering-file` with `-ffunction-sections`) fixes the exact layout at `0xC0001000`:
1. **Idle / once-per-frame prefix** (audio mixer, music decode, I/O pump, VI/RSPQ waits) placed first so the tail of the hot loop never collides with itself.
2. **`@window` render window** (room/mesh/item submit loop, `plat/n64/gpu.c`, `rspq`/`t3d` writers, and `inthandler` + MI/SP/DP interrupt handlers) packed contiguously within 16 KiB so it is self-alias-free.

`test/run/hot-check.py` fails any ROM build whose `.map` diverges from `plat/n64/hot.ld`. Measured I-cache gains (~1.6–1.9 ms/frame) are archived in [evidence.md §2](evidence.md#2-hot-code-placement-measurements-i-cache).

### 6.2 Hot data placement

The VR4300 D-cache is 8 KiB, direct-mapped (16-byte lines). To prevent unrelated `.rodata`/`.bss` growth from causing up to 5.4 ms of cache-aliasing jitter:
- `plat/n64/hotdata.c` + `plat/n64/hot.ld` anchor `.sbss` on an 8 KiB boundary shifted by `TR64_HOT_DATA_PHASE` (1,024 B) and place hot renderer/GPU/audio/RSPQ/RDPQ/display statics immediately after `.sbss` as one contiguous ~6.7 KiB hot data group.
- `sys_memory_boot` aligns the carved pools to `TR64_POOL_PHASE` (4,096 B) past an 8 KiB boundary.
- `test/run/hot-check.py` enforces both the hot data group ordering/alignment and the entry stack pointer depth modulo 8 KiB on the one-view render bridge call chain (`tr64_scene_tick` -> `tr64_render_draw_object` / `tr64_render_draw_items`).

D-cache phase scans, stack-depth measurements, and the audio-decoder spill fix are archived in [evidence.md §3](evidence.md#3-hot-data-placement--stack-phase-measurements-d-cache).

---

## 7. Renderer

`engine/render/` + `plat/n64/gpu.c` drive tiny3d (`T3D`) and `rdpq`:

- **Visibility & Portals**: Integer portal traversal starting from the camera room (`docs/specs/camera-and-projection.md`). Portals whose vertices cross behind the camera clip properly against the near plane to prevent inverted screen-space bounding boxes in outdoor scenes. Each room batch and entity mesh is tested against the accumulated portal screen scissor before submission.
- **Skybox (TR2 / TR3)**: Drawn before rooms in a dedicated 1-cycle dry RDP mode (no fog/combiner overhead) with a horizon scissor derived from the camera pitch and room portal bounds, avoiding skybox/geometry fighting and overdraw.
- **Geometry & Textures**:
  - Rooms and static/entity meshes submit pre-cooked 70-vertex shared batches (`RG64`/`MG64`) via tiny3d's per-corner UV triangle command (D4), expanding compact face indices directly into the RSPQ buffer with no per-vertex UV math on the CPU.
  - CI4 texture tiles and palettes upload to TMEM on texture-run boundaries, clamped to each tile rectangle (even-texel origin alignment eliminates bleed under both point and bilinear filtering). Users can toggle **Filtering** (bilinear vs point) and **Draw Distance** / **Auto Distance** live in *Options -> Graphics*.
- **Lighting & Effects**: Cooked vertex lighting (including overbright >1.0x lighting and TR2/TR3 colored/dynamic lighting), underwater room tinting (D12), distance depth-cue fog, rigid-bone fixed-point entity transforms, sprites, weapon flashes, flares, and particles.
- **Split-Screen Co-op (TR1)**: When Player 2 joins, the renderer draws two stacked 320×120 viewports (`tr64_render_game_scene_coop_frame`) with independent portal walks and shared framebuffers.
- **2D UI & 3D Ring (`engine/ui/`)**: Native `rdpq` 2D pass after tiny3d for HUD bars, ammo/pickup notifications, title backdrops (`UI64`), bitmap font strings, SRAM slot picker, and profiler overlay; the 3D Inventory/Options ring renders through the same mesh pipeline.
- **Host Rasterizer**: `plat/host/` consumes the identical submission stream to rasterize software reference frames and verify triangle/upload counts on host.

---

## 8. Game

`game/` separates shared Tomb Raider mechanics from per-game entities and rules:

- **`game/common/`**: Shared fixed-point simulation core (BSD-2-Clause, derived from OpenLara `src/fixed` for TR1 and extended cleanly for TR2/TR3):
  - Unified Lara state machine, animation/pose evaluation, camera & look/combat camera (`lara.c`, `lara_tr3.c`, `animation.c`, `pose.c`, `camera.c`, `camera_tick.c`, `item.c`, `static.c`, `matrix.c`, `math.c`, `tables.c`), shared by **TR1, TR2, and TR3** (including 30 Hz animation interpolation in TR3).
  - Cross-game **Lara Moves** (`TR I` / `TR II` / `TR III`): runtime capability selector gated by grafted donor animation banks, enabling TR2 climbing/mid-air roll and TR3 sprint/crouch/crawl/monkeyswing inside TR1 and TR2 levels.
  - Shared world/room/trigger/collision/inventory/save/UI plumbing (`game.c`, `world.c`, `room.c`, `trigger.c`, `collision.c`, `inventory.c`, `save.c`, `save_world.c`, `frontend.c`, `menu_text.c`, `stats_view.c`, `object_common.c`, `object_block.c`, `object_trap.c`).
- **`game/tr1/`**: TR1-specific object dispatch/progression, hazards (boulders, flames, Thor lightning), all TR1 enemy families, and 13 paged enemy/object TLB units.
- **`game/tr2/`**: TR2-specific weapons/flares/grenades, Venice boat, urban/opera/wall traps and hazards (disc throwers, rolling blades, spike walls, zipline, helicopter), TR2 enemies, schema validation, and 7 paged TR2 TLB units.
- **`game/tr3/`**: TR3 floor/sector/triangle-slope queries (`floor.c`), world/static/entity lifecycle (`world.c`, `jungle.c`), TR3 Lara adapter (`lara.c`) driving the shared `game/common/` controller, view/camera bridge (`view.c`), and 3D ring (`ring.c`).
- **`game/tr4/`, `game/tr5/`**: Reserved for Tomb Raider IV and V modules on top of `game/common/`.

**Engine–Game Contract (`include/tr64/game.h`)**:
- Lifecycle: `game_init`, `game_level_start(core, name)`, `game_tick(input)`, `game_stop_info`, `game_level_stop`.
- Rendering is pull-based: `game_view_camera` supplies the camera and `game_view_room` yields drawables in fixed 64-entry chunks (4,352 B) only for visible rooms, avoiding worst-case bone buffers.
- All game state lives in one allocation in `state`, hashed deterministically for verification, and serialized explicitly into compact 8 KiB SRAM save slots (3 slots + 8 KiB settings/directory in 32 KiB SRAM, D6).

---

## 9. Verification

1. **Host first**: Fast `-m32` host builds enforce identical pool sizes and cart I/O limits (`make unit`, directed host suites).
2. **Deterministic scenes (`test/scenes/*.route`)**: Boot straight into a level, placement, or save and check state hashes, oracle traces, rendered frames, pool peaks, and frame times.
3. **Cart gates on 4 MiB `ares`**: All cart verification runs through `test/run/ares.sh` with `ExpansionPak=false` (local or `TR64_ARES_HOST=<remote-host>`).
4. **Behaviour oracles**:
   - **TR1**: OpenLara's fixed-point game built for host (`test/oracle/`), compared tick-by-tick.
   - **TR2 / TR3**: Headless retail Windows executables recorded tick-by-tick under Wine behind the clean-room wall (`docs/specs/wall/tr2-oracle-trace.md`, D16).
5. **Negative controls**: Every gate ships with a control that must fail for the expected reason (e.g. pool 1 byte too small, wrong pack, unloaded code unit, tampered QR screenshot).
6. **Build-time gates in `Makefile`**: Every `make rom` enforces `sdk-check`, `size_gate`, `hot-check.py`, `fmt-link-gate.py`, `refs-check.py`, and the 64 MiB ROM ceiling. `make provcheck` enforces SPDX headers, clean-room provenance rules, and zero retail/binary artifacts in git.

---

## 10. Milestones and gates

| Milestone | Scope | Gate |
| --- | --- | --- |
| **M0** | Engine skeleton, TLB code units, PC/PSX cook & sizing | `test/run/m0.sh [--ares]`, `make provcheck` |
| **M1** | Room renderer, TX64/RG64/MG64, both TR1 sources | Reference picture diff, triangle/upload counts, 4 MiB frame time |
| **M2** | Lara movement, animation, camera, collision | Per-tick oracle trace parity; host == cart state hash |
| **M3** | Items, enemies, traps, triggers, weapons, game code units | Level completion on host & cart; cook-proved unit residency |
| **M4** | Spatial SFX mixer, SPU ADPCM music, H.264 video unit | Zero underruns; 4 MiB video playback + clean frame return |
| **M5** | Title, 3D ring, Passport, Options, 3-slot SRAM saves | Menu routes and power-cycle SRAM save/load round-trip |
| **M6** | Full TR1 21-entry campaign, cutscenes, shipping perf | Whole-campaign gate & `tools/ship/ship.sh check` |
| **M7–M9** | Real-hardware 32-bit `kseg2` TLB fix, QR fatal v2, 2P split-screen co-op | `tools/hwprobe/`, `make fatal-tests`, `tools/ship/test_coop.py` |
| **M10–M11** | TR2 & TR3 per-game carts (`TR64_GAME=2|3`), unified Lara core, cross-game Lara Moves, outdoor skybox/portal perf | Retail Wine trace parity, TR2/TR3 cart boots, `lara-abilities-test.py` |

Detailed historical gate reports, commit SHAs, and measurement tables for M0–M10 are archived in **[evidence.md](evidence.md)**.

---

## 11. Decisions

| # | Decision | Status |
| --- | --- | --- |
| **D1** | TR1 game logic is a C99 port of OpenLara's fixed-point game (BSD-2-Clause), verified against an OpenLara host oracle | Adopted |
| **D2** | Stock 4 MiB RDRAM is a hard requirement enforced by every gate | Adopted |
| **D3** | Keep 16-bit Z-buffer (150 KiB) for exact depth sorting | Adopted |
| **D4** | Cart 3D backend is `tiny3d` (`tr64` branch) with per-corner UV triangle commands | Adopted |
| **D5** | TR1 music uses 22.05 kHz mono PlayStation SPU ADPCM (`MU64`); TR1 FMVs use H.264 in a code unit with no level resident | Adopted |
| **D6** | Save medium is 32 KiB (256 Kibit) SRAM: 3 × 8 KiB save slots + 8 KiB settings/directory | Adopted |
| **D7** | PC and PlayStation sources both cook through one canonical IR | Adopted |
| **D8** | Cold engine, SDK, and game code lives in demand-loaded 4 KiB TLB code units | Adopted |
| **D9** | Working-code review policy; pre-commit review required for allocator, loader, paging, and verdict logic | Adopted |
| **D10** | Textures are CI4 with per-cluster 16-color palettes (lossless from PSX, quantized from PC) | Adopted |
| **D11** | Depth-cue far plane with runtime Draw Distance & Auto Distance options in Graphics | Adopted |
| **D12** | Underwater rooms use fixed tint `(0.5, 0.8, 0.8)` | Adopted |
| **D13** | Single engine and build path (only `TR64_TEST` adds test harness hooks) | Adopted |
| **D14** | 2-player split-screen co-op in TR1 (320×120 stacked views, P2 drop-in/out, L+A warp) without regressing 1P determinism or frame time | Adopted |
| **D15** | Multi-agent implementation workflow recorded for early milestones | Historical |
| **D16** | Behaviour reference for TR2 and TR3 is the retail Windows executable recorded tick-by-tick under Wine behind the clean-room wall | Adopted |
| **D17** | Per-game carts (`TR64_GAME=1|2|3`) from one shared engine (`game/common/` + `game/tr1..tr5/`) with per-game 4 MiB pool tables | Adopted |
| **D18** | Independent-reviewer workflow extended across agent models during TR2 milestones | Historical |
| **D19** | Unified TR1–TR3 Lara/animation/camera controller in `game/common/` with optional cook-time donor animation grafting for live cross-game **Lara Moves** in TR1/TR2 | Adopted |
