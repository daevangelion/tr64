# Native UI presentation

The UI consumes the existing level Core sprite sequences and TX64 textures.
There is no fifth level section, UI palette cook, extra level allocation, or
PC companion requirement for PlayStation cooks. The level remains borrowed
until `tr64_ui_unbind`, before its arena is released. Font sequence type 190
contains the 110 original game glyphs. Core sequence `first` is already a
native combined descriptor ID; no source-index lookup is performed per glyph.
TX64 sprite bounds are pixel offsets for the font (for example the first
LEVEL1 glyph has left/top/right/bottom `0,-16,16,0`). These bounds, rather
than a replacement font or image dimensions alone, define the drawn rectangle.

## Glyph layout

Layout facts: OpenLara `2333adef`, `src/fixed/draw.h:146-223`,
`src/fixed/inventory.h:1740-2018`; glyph placement corroborated by the allowed
32X renderer `src/platform/32x/render.cpp:1190-1233`. No platform renderer
implementation is copied into the engine.

Text uses the fixed game's 110-entry advance and character-remap tables:
space advances six pixels; every other character advances its glyph's width
plus one. Bytes 1-10 and 11-15 address the game's numeric/ammunition glyphs.
`$` consumes the next byte as a raw glyph index. The `p` baseline adjustment
of minus one pixel is retained. No case conversion or extra scaling occurs.
Left alignment uses the supplied x; centre adds `floor((320-text_width)/2)`; right
adds `320-text_width`. The pinned source's width-measurement quirk is retained:
`getTextWidth` remaps both bytes of an escaped glyph even though drawing
consumes the two bytes as one glyph. Invalid glyph bytes and truncated escapes
are rejected before drawing, rather than indexing outside either source table.

The host consumes CI4 high-nibble-first image bytes and big-endian RGBA5551
palette words directly. Native 2D drawing enters rdpq once after tiny3d's
last triangle synchronization and before frame detach. Glyphs use a textured
rectangle, point samples, alpha compare one, and no Z reads/writes. Bars use
half-open untextured rectangles: fill mode for solid gradient/bevel pixels,
one-cycle blending for shaded background pixels. Neither touches Z or pixels
outside the requested rectangles. The next frame resets the 3D state.

## Health and air

Game-owned visibility and values are exposed by `game_hud_view`; presentation
never reads Lara internals or changes simulation state. The source predicates
are `src/fixed/draw.h:1075-1096` and `game.h`'s inventory visibility gate.
Air appears at `(212,4)` for surface/underwater states. Health appears at the
same position, or y=14 when air is visible, while the health timer is active
or a weapon is ready. Both have a 100-pixel interior and five one-pixel colour
rows; a one-pixel bevel makes the total rectangle 102 by 7. The fill width is
`clamp(value,0,256)*100/256`. The game supplies maxima 1000 and 1800.

## One common colour table

Main's M5 decision: both source editions use the same small table of opaque
RGBA5551 colours, taken from the PC TITLE palette. This is UI data, not a
per-level source palette or a substituted PlayStation CLUT. A user with only
PlayStation data can cook without any PC files. Each six-bit RGB channel is
converted with `c >> 1` to RGB555, then alpha one is appended.

Palette index facts and bar gradients come from the allowed renderer
`src/platform/32x/render.cpp:1235-1272` at OpenLara `2333adef`. TITLE data SHA256:
`f2f8d28e2803d69ce096df0d34e9fbe70406e93cd838e045e491805bfe52ad39`.

| Index | TITLE six-bit RGB | RGBA5551 | Use |
|---:|---|---|---|
|6|22,17,0|5a01|health row 4|
|8|28,23,11|72cb|health rows 1,3|
|10|40,26,14|a34f|menu border|
|11|41,30,18|a3d3|health row 2|
|12|44,32,20|b415|menu colour|
|14|55,40,25|dd19|menu border|
|15|58,48,28|ee1d|menu selection|
|17|29,33,29|741d|bar right/bottom bevel|
|19|19,20,19|4a93|bar left/top bevel, air row 4|
|21|12,12,12|318d|air row 5|
|24|20,12,5|5185|health row 5|
|25|22,16,9|5a09|palette colour only; not the menu background shade|
|26|21,5,8|5089|dash row 4|
|27|15,3,6|3847|dash row 5 only; not the empty-bar background shade|
|28|31,8,8|7909|dash rows 1,3|
|29|40,10,7|a147|dash row 2|
|32|25,29,25|6399|air rows 1,3|
|41|23,40,39|5d27|air row 2, option row 5|
|42|22,47,49|5df1|option row 4|
|43|33,51,51|8673|option rows 1,3|
|44|47,55,55|bef7|option row 2|

The same indices were decoded from LEVEL1 (SHA256
`75306547c82d8e142f6dc80d85703d1fd5a5871071e4d04233660347865d26c7`)
and LEVEL6 (`f7fa9eac88678dd48b37651c02ef1b14d8bbea0d3bbcf9f39c515ff82b6157f7`):
all measured values agree with TITLE. This comparison establishes palette
agreement for those inputs, not a host/cart rendering or timing gate.

## Strings as language data

`engine/ui/en.json` preserves OpenLara's pinned StringID order with subtitles
disabled, through `STR_TR1_END2`: 170 entries. The source is
`src/fixed/common.h:2386-2624` and `src/fixed/lang/en.h` at `2333adef`.
The language strings retain their BSD-2-Clause attribution and licence
(`game/LICENSE-OPENLARA`); the packer and lookup code are original MIT code.
TR2/TR3 strings and subtitles are not included in this TR1 presentation table.

`tools/cook/n64_ui_strings.py` packs one NUL-terminated byte blob, a
170-entry u16 offset table and a 170-entry u16 text-width table into generated
read-only C data. English measures 1,696 blob bytes plus 340 offset bytes plus
340 width bytes = **2,376 encoded bytes** before linker alignment. The widths
and runtime remap/advance arrays share the single `engine/ui/font.json` layout
source. Game borrows the immutable width table to position text/arrows without
owning language or glyph data; all 170 widths are checked against the runtime
C text measurer. When a string-ID consumer is linked, this table belongs to
the resident image and its ROM ELF, not a copied font, per-level section,
heap allocation, or separate runtime file. The HUD-only demo currently uses
direct strings, so linker garbage collection omits `strings.o` and its table.
Generated `ui_string_ids.h` publishes the numeric layout as `TR64_STR_*`;
Game's DTOs can use those numeric IDs without including an engine header.
`tr64_ui_string` returns a borrowed immutable string, NULL for an invalid ID.
The UI draws a string ID or direct bytes through the same glyph path.

Another language is supplied as JSON with the exact same ID set via
`TR64_UI_LANGUAGE=path/to/data.json`; JSON member order does not change IDs.
Language bytes must be representable by the level's fixed-game glyph mapping.
No UTF-8 replacement font or silent transliteration is introduced. The
reference checkout's `src/fixed/lang` currently contains only `en.h`;
the format permits other language data but does not claim unavailable
translations were ported.

`tr64_ui_message` is the standalone, wrapped boundary-message path. It borrows
the bound level font and owns one complete acquire/attach/present transaction.
Before a font is bound or if bytes cannot be represented it returns zero
without acquiring a surface, allowing the platform's pre-bind fatal
diagnostics to remain independent. This is not the menu-page layout.

Focused checks: `make ui-test BUILD_DIR=$TR64_WORK/builds/MenuUI/tests`.
Synthetic checks cover layout, malformed escapes and missing glyphs, HUD
visibility/fill/footprint, actual host adapter CI4 alpha/scale/clipping and
language-schema rejection. These are algorithm checks, not cart screenshots
or emulated performance measurements.

The live play loop binds the font after level publication, enables the last
2D pass, and disables/unbinds it after sound stop and before game/renderer
release. A missing glyph sequence is a named load failure, not a silent HUD
fallback. Boundary messages call the same sprite-font message transaction;
the independent diagnostic font remains available only to pre-bind fatal
handling. `test-play.c` checks those ownership/order rules and rejects a
missing-font start before simulation or audio begins.

Host pictures can opt in with `--game-ui`. `--game-live` selects the same
unposed, unrestricted renderer binding as the pad-driven play loop, instead
of the compiled reference-pose fixture. Both switches are off by default so
existing oracle and posed-image consumers retain their contracts. Use the
same `--game-live` choice for a HUD-on/off picture comparison.

## Title picture UI64, version 1

This is a source-independent image blob in opaque pack kind `0x50000`
(`TR64_PACK_IMAGE`). Core charges it, with sixteen-byte inter-kind alignment,
to the existing planned texture section of the level arena. It is not a new
pool. The title pack also retains Core, TX64 glyphs and ring meshes.

All header words are big-endian; `>4s7I`, exactly 32 bytes:

| Offset | Meaning |
|---:|---|
|0|magic `UI64`|
|4|version = 1|
|8|exact total bytes|
|12,16|width = 320, height = 240|
|20|format: 1 CI8, 2 RGBA5551|
|24|pixel offset = 32|
|28|palette offset = 76832 for CI8, zero for RGBA5551|

CI8 stores 76,800 row-major indices followed by 256 big-endian opaque
RGBA5551 palette entries, total **77,344 bytes**. RGBA5551 stores 76,800
big-endian opaque pixels with no palette, total **153,632 bytes**. There are
no optional layouts, row padding, embedded source codecs or runtime rescaling.

`n64_ui_image.cook(source,path)` returns `(bytes,manifest)` for a pack builder.
PC `TITLEH.PCX` is an indexed 640x480 picture. Pixel-centre nearest reduction
to 320x240 preserves its indices, and RGB8 palette channels become RGB5 by
`c >> 3`; no palette quantisation or dithering is needed. PlayStation
`DELDATA/AMERTIT.RAW` decompresses as RNC2 to 196,608 bytes; its complete
384x256 little-endian BGR555 raster was arranged and visually inspected.
Pixel-centre nearest reduction preserves the original five-bit colours,
reordered to RGBA5551 with alpha one. RGBA16 is retained instead of lossy
256-colour reduction for that source.

The original bounded RNC2 decoder implements the published prefix tables in
[PSX-SPX's RNC specification](https://problemkaputt.de/psxspx-cdrom-file-compression-rnc-rob-northen-compression.htm),
checking packed and unpacked CRC16/ARC, truncation, backreferences and declared
output bounds. It does not inspect an executable or import another engine.
Source and output SHA256 values, dimensions and conversion policy are in each
generated manifest. Both actual source cooks were decoded independently:
all 76,800 output pixels equal independently sampled source RGB555 values.
This is image-cook evidence, not title-screen runtime integration.

`make ui-image-test BUILD_DIR=$TR64_WORK/builds/UI` runs synthetic
literal/overlapping-match RNC2 checks, the known CRC vector, malformed RNC
controls, PCX sampling/palette checks and invalid image-size controls.

## Initial presentation evidence

The HUD/text implementation is resident, with **zero new UI code-unit pages**
and no level/state/heap allocation. In the initial sound-enabled shipping
build, the linked UI object charges are `draw.o` 4,572 B and native `ui.o`
1,356 B. The full resident image is **489,768 / 524,288 B**, before the later
SDK32/menu integrations. These are linked map charges, not a menu-size estimate.

At 4 MiB, the live shipping smoke captured 200 ticks and LEVEL1 restart,
LEVEL2 and return to LEVEL1, with no fatal or sound underrun. Compact evidence
is retained under `$TR64_WORK/evidence/m5-ui/`, with byte/pixel hashes in
`retained.json`; superseded build trees and full PPMs were removed for disk
space. The reviewed health pictures are `hud/health-cart.png` and
`hud/health-host.png`.
The same unchanged world draws underneath: paired host frames changed exactly
714 pixels, all inside the 102x7 health-bar rectangle, and complete game traces
were identical. The paired host control is `hud/health-host-control.png`;
underwater host controls are `hud/water-host-t0.png` and `hud/water-host-t0-off.png`.

The original underwater host picture captured **tick 0**, room 14, eye
`40448,3684,60828`; the cart picture captures **tick 1**, room 13, eye
`40448,3545,60730`. They must not be used as a matched image pair. The corrected
tick-1 host counterpart is `hud/water-host-t1.png` in that evidence directory.
The reported upper-frieze/cyan-strip discrepancy disappears in that matched
pair. A tick-0 HUD-off control retained all those world pixels: exactly 1,428
pixels changed, all inside the two bar rectangles, zero outside. No host
rasteriser defect was established by that mismatched comparison.

The source-placed GYM underwater route is `test/ui/hud-underwater.route` with
`hud: true` in its renderer scene JSON. The fixed game's real water state
selects air plus health; no synthetic HUD-value override is used. Cart picture:
`hud/water-cart-t1.png`. The same route with
`TR64_SCENE_TEST_SOURCES=test/ui/font-scene.c` exercises a test-only overlay
through the production font path: `hud/font-cart.png` and `hud/font-host.png`.
That host lane also linked a cart ELF against Main `aebe608`'s SDK32/Core
image seam: resident **483,224 / 524,288 B** for the font-test configuration.
This hook is compiled out of shipping builds, not a menu replacement.

Cart CPU enqueue time for the live HUD was 60-115 us (mean 72.22 us across
203 visible passes); the two-bar water scene measured 163 and 174 us. These
are the `ui: submit_us` intervals, not isolated RDP execution time. The live
smoke's worst complete 3D-plus-HUD frame was 20,954 us. The later matched
fill/one-cycle/textured comparison is recorded below. ares does not model RDP
fill time (architecture M1 evidence ceiling).

### Linked HUD cost attribution

The matched sound-only shipping baseline is
`$TR64_WORK/builds/Demo/sound-bd70/size.json`, build `D53C6A32EB05`:
470,472 B, the same old SDK16, PLAY=1, TEST=0, sound fix and immutable pack.
The 489,768 B HUD build added **19,296 B total**, not only the UI objects:

| Linked change | Bytes |
|---|---:|
|UI draw/native objects|5,928|
|libdragon `rdpq_tex.o`|8,092|
|additional `rdpq.o` functions|1,408|
|`rdpq_rect.o`|1,056|
|additional `rdpq_mode.o` functions|224|
|merged string pool (reported under `eng.o`, not engine code)|2,496|
|Game HUD accessor|192|
|play caller change|-192|
|linker fill/alignment|92|
|total|19,296|

The English ID/offset/blob table was not linked in that HUD-only build.
Future menu layout, passport/options/statistics formatting and title control
belong in the cold presentation unit. Only per-frame HUD/text work, its used
language data and the small resident unit/lifecycle bridge belong in the
resident image; unit pages and residual resident bytes are measured at link,
not inferred from this HUD attribution.

## Shaded fills: TITLE-derived RGB multiply

Correction to the first HUD implementation: the allowed 32X consumer
`render.cpp:1043-1064` emits `FACE_TYPE_FILL_S`; its
`rasterizer.h:734-765` applies `gLightmap[shade*256 + existing_pixel_index]`.
The argument 25/27 in `renderFill` is a shade row, not palette colour 25/27.
Filled gradient rows and bevels remain ordinary palette-colour lines.

Main's native RGB policy replaces this indexed mapping with one constant
framebuffer multiply per used shade, measured from the same TITLE palette
identified above. No lightmap is made resident or required for PSX cooks.
For all 256 palette indices, each source RGB6 channel is first converted to
RGB5 with `c >> 1`. Gain is the ratio of the mean mapped RGB5 intensity to
the mean original RGB5 intensity, equivalently
`sum_RGB5(palette[lightmap[row]]) / sum_RGB5(palette)`.
The denominator is 6,924 for TITLE:

| Source shade | Mapped RGB5 sum | Mean gain | Chosen retain coefficient | Black fog alpha |
|---:|---:|---:|---:|---:|
|25, options background|2,923|0.42215482380127095|14/32 = 0.4375|151|
|27, empty bar interior|2,105|0.3040150202195263|10/32 = 0.3125|183|

Coefficients are rounded to the RDP's five-bit **FORCE_BLEND** multiply,
not its normalized colour-divider path. This mode bypasses normalization;
the inverse framebuffer weight has a +1 bias. With black input, a retain
numerator `k` in 1..31 uses fog alpha `255 - 8*(k-1)`. Framebuffer RGB5 is
shifted left three bits on read (not bit-replicated), so final RGB555 is
`floor(channel5 * k / 32)`. Retain 0 fills black; retain 32 is a no-op.
The host implements this same per-channel integer multiply.
The source gains were measured before capture; no per-colour fitting/table
or picture-dependent correction is used. See the Nintendo
[blender programming manual](https://jrra.zone/n64/doc/pro-man/pro15/15-05.htm)
for the five-bit inputs and the hardware-analysis
[FORCE_BLEND commentary](https://emudev.org/2021/09/21/Angrylion_RDP_Comments)
for the inverse-weight bias and bypassed divider (facts only; no code copied).
Native drawing uses a black flat rectangle with
`RDPQ_BLENDER_MULTIPLY_CONST`, alpha compare off and no Z reads/writes.
Full bars still need no blend pass.
`test/ui/shade-scene.c` supplies a 32-level channel ramp through
the production adapter, exercises both shade rows and a partial health bar,
then draws glyphs to defend the blend-to-text state transition.

### Matched rectangle-path measurement

`test/ui/rectangle-evidence.py` checks the retained `rect-2544e26` captures:
21 source-placed GYM frames per path, identical Game hashes/camera/3D
submissions, identical complete fill/one-cycle/textured pictures, and exactly
1,428 HUD-on/off changed pixels, none outside the two 102x7 bars.

| Rectangle path | CPU enqueue min / mean / max, us | Complete frame mean, us |
|---|---:|---:|
| HUD off | — | 10,787.29 |
| Fill | 93 / 140.00 / 197 | 10,614.19 |
| One-cycle | 95 / 139.48 / 193 | 10,654.38 |
| Textured | 203 / 300.24 / 362 | 10,763.67 |

Fill and one-cycle enqueue are tied within this run's variation; textured is
about twice the enqueue cost. The negative paired whole-frame deltas are
layout/cache/CPU-RSP-overlap variance, **not** proof of a negative/free HUD
cost or a measured RDP fill-rate win. Fill is the simple untextured default.
The shade capture verifies all32 channel levels against `c*k>>5`, and rejects
the wrong-weight control at16 levels. Native fog commands in this capture
are151/183; the later host/math correction leaves those commands unchanged.

## SRAM medium, version 3

D6 uses32KiB SRAM (`N64_ROM_SAVETYPE=sram256k`, Advanced Homebrew Header
save nibble3). No detection probe writes into a live slot. Game serialization
and medium dispatch are sequential, never nested code-unit calls. The medium
unit owns no payload allocation and never calls Game. The resident caller's
per-level workspace is an ordinary6,144B state-pool allocation (one region),
including at title start; it is released by the existing whole-level reset.

All fields are big endian. A save is atomic: there are **four physical regions
for three logical slots**, and a save is written into a region no slot points to
(the spare one), verified, and only then does a new directory copy make it the
slot's save. Device layout:

| Guest byte range | Meaning |
|---|---|
|0000–17ff|region0, 64B header + at most6,080B payload|
|1800–2fff|region1, same|
|3000–47ff|region2, same|
|4800–5fff|region3, same|
|6000–607f|directory copy0 (128B)|
|6080–60ff|directory copy1 (128B)|
|6100–6fff|settings copy0: 32B header + at most3,808B payload|
|7000–7eff|settings copy1, same|
|7f00–7fff|reserved, not read as data|

The game's own save is at most6,050B (LEVEL10B, `docs/architecture.md` D6), so
a region (6,144B) holds the largest save with its 64B header (6,114B) and 30B
to spare; `TR64_SAVE_PAYLOAD_BYTES` is6,080 and a larger payload is refused
(`SPACE`) before anything is written.

Each record starts with magic, version (`u16`; directory2, settings2, game
slots3), header size (`u16`), payload length (`u32`), payload CRC32, and header
CRC32 at offsets0,4,6,8,12,16. CRC is the engine's incremental CRC32; the
header checksum covers the complete header with its own field zero and the
**valid** magic. A record that is intact but of another version is `VERSION`,
never read with this field map. Game slot magic is `SG64`; its remaining64B-header
fields are:

|Offset|Type|Meaning|
|---:|---|---|
|20|i32|source level ID|
|24|u32|statistics time in source ticks|
|28|u32|statistics distance|
|32 /34 /36|u16|medikits / ammunition / kills|
|38 /39|u8|pickups / secret bit mask|
|40 /44|u32|level title string ID / maximum secrets|
|48|u32|level identity: what the writing pack called this level (below)|
|52–63|bytes|must be zero|

The header's metadata is returned only after header and payload integrity
checks. Cache queries are resident/read-only: `tr64_save_any()` means at least
one verified slot, not that a medium is present. A failed header/payload read
invalidates that slot's cached validity; metadata from a previous successful
read is not exposed as current.

Directory magic is `SD64`, version2,128B per copy. Offset8 is slot count3,
12 the **sequence number**, 20 device bytes32768, 24 region bytes6144, 28 region
count4. Three `u32` at32,36,40 give each slot's region (0–3, or `ffffffff` for a
slot without a save; regions are distinct); 44–127 are zero. The newest valid
copy (valid by CRC and fields, then the higher sequence) is the directory; a copy
that is empty, torn or of another version is ignored while the other is valid.
Settings magic is `ST64`, version2,32B header: payload length at8, payload
CRC at12, header CRC at16, **sequence** at20, 24–31 zero. Settings follow the
same rule with their own sequence. The directory is fixed layout, not a mutable
source of unchecked device offsets.

A save of slot *s* (`slot_write`): (1) pick a region no slot points to; (2)
write it as a record (its commit word is zeroed first, payload and header follow
with the commit word zero, and the valid magic is written **last**); (3) read it
back and check both CRCs; (4) write the directory with *s* pointing to the new
region and the next sequence into the copy that is **not** current, by the same
commit ordering; (5) read the directory back. The directory write is the commit
and is the last transfer: until its commit word lands the other copy, and with
it the old save, is the directory. The slot's previous region becomes the spare.
A write that fails rescans the device, so the cache says what a rescan would.
Settings write to the copy that is not current the same way and verify it. Odd
payloads use a separate padded last halfword without reading beyond the supplied
input. All transfer pointers are16B aligned; read capacity covers16B-rounded
payload size. Other slots are never rewritten; the player keeps every save
that was complete before the write through a power loss at **any** transfer.

A device with no usable directory (empty, corrupt in both copies, or another
layout version) is made into a fresh directory (sequence1, copy0, no saves) by
the first write. **Older layouts are unusable, not fatal:** the layout of version
1 and2 (three8KiB slots at fixed offsets, one directory at6000 of version1)
reads as a directory of version1, so the scan reports `VERSION` for every slot
(the picker shows them as unusable), nothing is loaded from them, and the first
save replaces them.

Host device paths are explicit and borrowed for process lifetime. A new file
is exactly32768 erased (`ff`) bytes; an existing wrong-sized file fails closed
without truncation or reformatting. `fsync` completes every write phase.
Native transfers use libdragon SRAM and drain pending PI DMA before each
transfer; no per-frame I/O is needed for passport save availability.

### Medium evidence

`make save-test` exercises3 slots/settings,6050B worst-size and3903B odd-size
payloads, the largest payload (6,080B), CRC/header/reserved-field rejection,
insufficient capacity, cache invalidation and isolation. **Tear controls:** a
complete overwrite of a slot is8 transfers (commit word cleared, payload, odd
tail, header, commit word, then the directory's three); each of them is
interrupted both before it and halfway through it (16 tear points) and after
every one the old save reads back unchanged, the other slots are intact, the
interrupted slot can be written again, and the same is checked for every
transfer of the first save on an erased device and of a settings overwrite (8
tear points: the old settings survive). With no interruption the new save is the
slot's. At the last tear point of a slot write the new save is complete in the
spare region and only the directory's commit word is missing, which is the proof
that the flip is last. A control performs the previous in-place write and loses
the old save at its second transfer, as this harness must report. A damaged
newest directory copy reads as the device before that save; both damaged read as
corrupt (nothing loads) and the next save makes a directory; a device of layout
version1/2 reads as `VERSION` for every slot. The host process then exits and a
second process reads the same file; independent Python CRC checks verify its
wire layout (both directory copies and their sequences, region map, spare
region, settings copies). Wrong-sized devices are preserved, not repaired.

`test/ui/save-cart-test.py` exercises the real pageable medium and PI backend
in two separate4MiB emulator runs, with SRAM export at power-off and import
before the second boot. All32768 bytes equal the host backend's device image
after each run; a one-byte import fails before guest execution. Evidence is
`$TR64_WORK/evidence/m5-ui/save-final/` (`report.json`, write/read logs and
cart pictures). This is a deterministic storage fixture, **not yet** an
inventory-driven Game-state save/load claim.

The native fixture's medium unit is8,192B (two code frames); its resident image
is312,168 /524,288B. That sparse diagnostic does not render a level and is not
the full demo's resident-image measurement. Its16,384B state peak consists of
two diagnostic8KiB buffers; production uses one6,144B workspace (one region). Slot reads/writes
do not allocate or add work to ordinary3D frames.

## Optional route presentation boundary

The generic game route obtains its camera once and retains its existing
caller-owned64-drawable chunk. It first offers that view/chunk to the optional
`tr64_ui_game_frame` hook. An absent hook or an unbound UI returns to the ordinary
renderer; unbound UI performs no Game queries or drawing. A bound hook returns
one only after submitting/presenting the complete frame, so the route does not
render it twice. Linking UI alone does not enable presentation in hash lanes.

`test/run/game-route-tests.py` exercises absent, present/unbound and
present/bound providers, preserving the camera query count, chunk identity,
streamed1/71/141 drawables and actual completion-work stamps. These are
asset-free dispatch controls, not Tomb Raider menu-pixel evidence.


## Live menus in the play loop (M5)

`engine/ui/frontend.c` (resident) owns one presentation per loaded level.
`tr64_ui_game_frame` replaces the renderer call in the play loop: it queries
the game's menu view (`game_menu_view`), then draws the world with the game
camera (title: an empty frame) and lets the GPU overlay hook call the `front`
unit (`engine/ui/front.c`, 8 KiB, preloaded at level start with `inv`, `save`
and `medium`; the code pool holds 24 frames). Menus re-render the frozen
world; there is no framebuffer copy. The unit paints the title picture, one
roomless mesh pass for the ring (second camera, Z cleared once), options,
bars, labels and statistics from Game's text view, and the pickup icons.

Pickup notifications follow non-fixed `src/ui.h` at `2333adef`: 5 s, position
lerp by dt*5, one-second fade-in, slide-out during the last second, spin
`time*90 deg/s`. Declared differences: layout halved for 320x240 (pitch 48,
margin 24, 0.25 pixel per model unit), the four point lights are the existing
scalar lighting (light offset -32, same as the ring), the alpha fade-in is a
brightness ramp. State (12 icons) is resident; icons are hidden and frozen
while the ring is open. The notifications advance one 30 Hz step per game tick,
never per drawn frame: the game replaces its pickup events at every tick, and the
play loop runs several ticks before a frame when frames are slow, so it calls
`tr64_ui_game_tick(view,chunk,capacity)` after every tick that leaves the game
running (the `front` unit, `update=TR64_UI_UPDATE_TICK`: collect the events, one
step of the timers; no drawing) and the frame only draws. A caller that draws once
per tick without that hook (the hash scenes) gets the same one step ahead of the
picture (`TR64_UI_UPDATE_FRAME`), so those lanes are unchanged. The rumble pulse is
250 ms of the clock, ended from the loop's wait (`tr64_ui_game_poll`) and each tick.

The play loop boots to TITLE. Source transitions (campaign, restart, new game,
title, gym, load) carry inventory through `game_inventory_capture`; death waits
for the source's own input. START is `TR64_GAME_SELECT`; L+START stays the demo
restart. `eng_game_vibrate` pulses a Rumble Pak for 250 ms of the clock.

Logs: `ui: menu frame= state= page= passport= item=` on each change and
`ui: pickup type= tick= screen=`.

Slot picker (`engine/ui/picker.c`; `tr64_ui_picker_filter`, resident, called by
the play loop before each `game_tick`): a passport ACTION edge whose menu
`actionHint` is SAVE_SLOT or LOAD_SLOT opens a three-slot list (forward/back
choose, B (ACTION) accepts, A (JUMP) or START cancels). The game sees no input while it is
open; the cancelling button is then withheld from the game until it is released
(the passport leaves on a *held* JUMP or START, so otherwise one press would also
leave the passport on the next tick; `picker-test` covers both buttons); accepting records the confirmation (`tr64_save_game_confirm`: the slot
and which request) and passes one ACTION, so the game still saves or loads
through its one ordinary path. Load rows with no save cannot be accepted. Rows
show the level title stored with the save, "Empty", or "Unusable".
Verified on ares by the shipping gate (`tools/ship/boot.py --save-load`): save into slot
3 through the picker, power cycle (SRAM export/import), load that slot, and the loaded Lara is
the saved one (position, rotation, room, health).

A save that fails (the medium reports an error) shows the passport's three-line
text "Game not saved / Cartridge SRAM could / not be written" (`STR_GBA_SAVE_WARNING_1..3`, the source's
GBA wording replaced), with OK.

#### No save or load without the picker

Inside one game tick the game moves the option cursor on a direction edge and
the passport page on a LEFT/RIGHT edge, and only then reads an ACTION edge
against what the tick produced (`game/inventory.c`, `onOption`, `onPassport`);
the passport also acts in the very tick its open or close animation completes.
The hint of the menu view *before* the tick is therefore exact only when none of
that can happen, which gives the one rule (`tr64_ui_picker_classify`) for an
ACTION edge: no menu, or a menu not in SHOW or SELECT: passed (the game does
not read ACTION there); SELECT, SHOW not settled (`frameIndex != frameTarget`),
or a direction edge in the same tick: **withheld**: the game receives the rest
of the input and the held button arrives as a new ACTION edge on a later tick,
judged again against the then-current page and cursor; a settled SHOW with hint
SAVE_SLOT/LOAD_SLOT: the picker; any other hint: passed. Edges are taken against
what the game received on the previous tick, not against the raw pad, so a
withheld press is not lost. Inside the picker a direction and ACTION in one
tick move the cursor only; accepting needs a press of its own and re-checks that
the menu still shows the same hint.

Two guards that do not depend on that analysis: `eng_game_save` (strict mode,
turned on by the play loop; host scene runners leave it off) refuses with a failed
save unless the picker confirmed a slot for that very tick, before
`game_save` commits the game's header; and the play loop does not act on the
game's LOAD request without the picker's load confirmation, a refusal going to
the title like any unusable slot. `test/ui/picker-test.c` (in `make ui-test`)
runs a transliteration of the passport tick with the real picker, glue and
medium: all 14-symbol input sequences of length four and 4,000 random 60-tick
sequences (24,000 in all) per page/save combination (options, title, death; no saves or a
save in slot 1 only, so the remembered slot 0 is empty), asserting that every
save reaches the slot the player accepted, every LOAD stop was confirmed, and
no attempt was refused (3,735 saves, 99 loads, 48,290 pickers opened); the
previous rule, run as negative control on the same sequences, produces 3,755
unconfirmed saves and 730 unconfirmed loads. The two named cases from the review
(UP+ACTION on the load page, RIGHT+ACTION on the load page) are asserted
tick by tick. This is a model of `game/inventory.c`, not the game itself;
the cart check is `tools/ship` (picker save, power cycle, load).

#### Saves this cart cannot load

Every build is ROM title `tr64`, so emulators and flash carts hand one SRAM
file to every build and to both data sources. A slot is therefore judged
against the pack of the cart that reads it, **before** anything is released.
Slot header version 2 records the *level identity* of the saved level: at boot
(`tr64_save_identity_bind`, `engine/save/identity.c`, with no level resident)
the engine reads the pack directory once and keeps, per campaign level, the
CRC32 of the level's section table (kind, destination, ROM offset, sizes, codec
and the CRC of every block). A slot is usable (`tr64_save_slot_status`) when its
recorded identity is nonzero and equals the identity this pack gives the
level; it is **foreign** when the pack has no such level or another cook of it
(a PC save in the PlayStation build, a save from before a recook), **version**
when it is a valid slot of the older layout (version 1, no identity), and
empty or corrupt otherwise. The picker shows level titles for usable slots,
"Empty" for empty ones and "Unusable" for the rest, and cannot accept an
unusable row for loading; the load page offers "Current Position" only while a
slot is usable. The identity is deliberately stricter than the game's own check
(core tables and textures): a recook that moves the level inside the pack makes
older saves of it unusable although the game would have accepted them.

The game's check (`game_load`: core-table and texture CRC, item count, level,
format version) stays the exact and final one. A load it still rejects, after the
level of the save is loaded, does not stop the cart: the play loop shows
"SAVE NOT USABLE / RETURNING TO TITLE" for two seconds, starts the title, and
the slot stays foreign (`tr64_save_refuse`, kept across rescans) until it is
written again. A selected slot that is no longer usable when the game's LOAD
request arrives goes to the title the same way, with the current level still
loaded. `play-test --load-unusable` and `--load-refused` cover both exits;
`save-test` covers identity storage, foreign and refused slots and the
old-layout slot.

`tools/ares/build-sram.py` now also exports `ARES_SRAM_OUT` when ares receives
TERM/INT, so a shipping cart (no test exit) can be power-cycled: see
`test/ui/menu-flow-test.py`.

#### The end of the game plays END

Finishing LEVEL10C sets the pending level to the title (`game_complete_level`:
no statistics page at the end of the campaign), so the game's LEVEL_END is a
TITLE transition, and the play loop used to allow videos only on CAMPAIGN,
NEW_GAME and GYM transitions: the END branch of `tr64_video_transition` was
dead. A TITLE transition out of `level10c` now sets the advance flag, except
when the player left through the passport's exit page: the stop carries no
cause, so the loop reads whether the last presented frame had the ring up
(`tr64_ui_menu_was_shown`; a passport exit closes the menu over several drawn
ticks and the stop comes in the tick it reaches NONE, a level completion
happens in simulation with no menu). `play-test` covers both
(`END` requested exactly for ordinal 11 to 12, not for GYM to title, and not
with `--end-from-menu`). One coincidence remains: a level completion in the very
tick after the ring closed would count as a menu exit and skip END.

### Cutscene levels in the play loop (M6)

The level-select catalog (`levels[]` in `engine/in/play.c`) is the source
campaign order with the four in-engine cutscenes in place, TITLE last: gym,
level1, level2, level3a, level3b, **cut1**, level4 ... level7b, **cut2**,
level8a, 8b, 8c, level10a, **cut3**, level10b, **cut4**, level10c. L+C-left and
L+C-right step through the cutscenes like any level (a manual start has no
inventory carry); `boot.py` reads this array to plan its level-select path.

A cutscene is loaded from the pack like any level and follows the same
transition path: the stop's `next_level_id` names the next level (LEVEL3B ->
CUT1 -> LEVEL4, LEVEL7B -> CUT2 -> LEVEL8A, LEVEL8C -> LEVEL10A -> CUT3 ->
LEVEL10B -> CUT4 -> LEVEL10C), inventory and the camera timer/listener carry
across, and no statistics page is shown for an entry into or out of a cutscene
(`game_complete_level`). The level has no Lara: the game's view supplies the
cinematic camera, `game_hud_view` returns zero (no health or air bars), the
inventory ring cannot open (the SELECT test sits in the Lara update, which a
cutscene does not run), so the passport, and with it every save and load, is
not reachable while one plays. Sound effects and the music track the game
requests (`game_music_request`: tracks 23, 25, 24, 22 for CUT1..CUT4) play as
in any level. The play loop passes no input to the game while a cutscene runs.

**Skip.** START (rising edge, L not held) calls `game_cutscene_skip`, which
sets the pending-level request the cinematic itself makes when its frame
table ends (`gLevelID + 1`); the very next tick latches the ordinary
`LEVEL_END` stop with that id and the play loop takes its usual level-advance
path. A skip is therefore not a second transition mechanism and not a level
selection: the level that follows is the one the cutscene's own end names.
START is also masked from the game until released, so the press that skipped
does not open the ring in the next level. L+START is still the demo restart
(it restarts the cutscene).

Logs: `play-skip: level=cutN` on a skip; the usual `play: ready=`,
`play-stop:` (with `next_level_id`) and `play: loading=` lines.
`tools/ship/ship.sh check` enters one cutscene by level select (CUT4, from
LEVEL1 via GYM and LEVEL10C), lets it play 240 ticks, skips it and requires
LEVEL10C next, with no ring frame while it plays. `boot.py --cutscene NAME
--cutscene-natural` instead lets it run out (CUT3: 398 ticks, then LEVEL10B).

Shipping pack with all 21 levels and 55 music tracks:
`test/game/campaign-pack.py` (TITLE, the sixteen levels, CUT1..CUT4, each with
its SN64 effects bank; the cutscene banks are 69,344 bytes, six PCM16 samples)
then `test/game/campaign-music.py`, which re-emits every block and appends the
56 `MUSIC_nn` entries of the earlier composed pack unchanged (the 17 old levels
and all music compare equal to it): 34,813,376 bytes, 77 directory entries,
15,518,272 bytes left under 64 MiB after the 16 MiB video reserve.

Measured on ares at 4 MiB (pool figures are the `tr64_mem_log` line at the
cutscene's ready, `used`; the `peak` counters are cumulative over the boot):

| Cutscene | Level arena | State pool | Code pool | Render `done_us` mean / max |
|---|---|---|---|---|
| CUT1 | 414,528 | 62,944 | 57,344 (14 frames) | 17,149 / 18,001 |
| CUT2 | 191,536 | 48,048 | 57,344 (14 frames) | 5,233 / 6,949 |
| CUT3 | 362,896 | 63,840 | 65,536 (16 frames) | 11,579 / 13,777 |
| CUT4 | 685,456 | 58,352 | 65,536 (16 frames) | 15,280 / 17,450 |

Budgets are 1,507,328 (level arena), 196,608 (state), 98,304 (code). The game
tick costs 130-350 us on average. Music plays (CUT4: about 200,000 decoded
frames, no underrun). Black areas in CUT3 (dark rooms, vertex darkness 8191) and
CUT4 are drawn the same with portal culling and back-face culling disabled, so
they are not culled geometry; CUT4's have no drawn face at all.

## Ring size (computed, declared choice: unchanged)

Reported as too small, so the three proportions were computed. Heights are
fractions of the 240-pixel frame for a model extent `w` in model units.

1. Fixed game (`src/fixed`, 320x240 frame, projection focal 256 px as the
   world uses; `inventory.h` `INV_CAMERA_Y 96`, `INV_CAMERA_Z 768`, ring radius
   688): the selected item sits at the front of the ring, 768 units from the
   eye: `w*256/768/240 = w/720`.
2. OpenLara main game (`src/inventory.h`, commit `2333adef`; BSD-2):
   `INV_MAX_RADIUS 688`, camera `(0,0,-1286)` looking +z, front item at z=-688,
   so 598 units; `INV_FOV 70` degrees vertical; no per-item scale or selected
   z shift beyond the ring's own (`phaseChoose`): `w/(2*598*tan 35deg) = w/837`.
3. This port, cart picture: the same camera as 1, so `w/720`.

For the compass (82 units wide): fixed 0.114, main game 0.098, ours 0.114
(about 27 px). Pistols (about 300 units): 0.42 / 0.36 / 0.42. The port equals
the fixed game and the main game's proportions are slightly smaller, so no
field-of-view or distance change is made: the compass really is a small model
(cooked mesh bounds -41..41 x -9..9 x -50..49).
`test/ui/menu-flow-test.py` counts picture pixels that change when the ring
opens in the centre band and requires at least 100 (a compass alone is far
above that; no ring or an off-screen ring is not).

## Ring picture: what the two shapes are (measured, no matrix aliasing)

A cart picture of the opening ring showed "the compass body near Lara's head and
a grey disc at her hips". Both shapes are ring items, not parts of one item: a
two-item ring (LEVEL1: compass, pistols) puts the items 180 degrees apart, the
selected one at the front (z about +690, 765 units from the eye) and the other at
the back (z about -710, 2,160 units from the eye). The camera is eye
`(0,-256,1456)`, 11.8 degrees down, so the back item projects above the front
one: the pistols (3 parts) at y 90..97, 16 px wide, level with Lara's head; the
compass (4 parts, translations within 45 units, one orientation) at x 142..177,
y 141..154 (35 x 13 px), at her hips. The compass lies flat in the ring (its dial
quad is the plane y=-8; 97 of its coloured faces use palette colour 20, grey), so
at 18 degrees above it the disc is edge-on and grey. While the ring opens the two
items orbit and fly in (ticks 31..50), so consecutive pictures show them far
apart.

Checked, not assumed (host, LEVEL1, `--game-ui`, SELECT at tick 30): every
`tr64_gpu_matrix` call of one ring frame binds slots 0..16 in the world pass
(a room matrix, one item, Lara's 15 bones) and slots 17..23 in the ring pass, one slot per
drawn part, none shared (`$TR64_WORK/evidence/m5-ui/ring/matrix-slots-before.txt`).
`menu_counts.matrix_slots` starts at the world frame's count, so the ring pass
cannot reuse a world slot; both GPU backends fatally refuse a changed live
matrix, and the cart reached the same picture as the host. A reset at the menu
pass would be wrong on the cart: `t3d_matrix_set` reads `gpu.matrices[id]` when
the RSP runs, so overwriting a world slot before the world's commands finish
would corrupt the world frame. World frames use at most 84 of the 256 slots in
the retained evidence (`matrices=` in `game-render` logs), the ring adds 7.
Pictures: `$TR64_WORK/evidence/m5-ui/ring/`.
