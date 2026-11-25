<!-- SPDX-License-Identifier: MIT -->
# TR1 source IR (M0)

Implementation: `tools/cook/ir.py`, `src_pc.py`, `src_psx.py`, `decode.py`.
This is a Python analysis/cook IR, **not a native binary pack ABI**. It describes
both retail PC PHD version 32 and US PlayStation PSX files. Layout facts were
consulted in OpenLara `2333adef`, `src/format.h` and the permitted
`tools/n64_magma/psx_level.[ch]`. No reader code was copied; the Python code is
MIT. The VAB layout is also described by [psx-spx's sound format reference](https://psx-spx.consoledev.net/soundprocessingunitspu/#sound-data-file-formats).

## Conventions and absence

- All input integers are explicitly little-endian, fixed width. Python integers
  retain the full signed or unsigned value; no float conversion or gameplay
  adjustment occurs. Coordinates are original TR world units; Y points down.
  Room vertices/portals use room-local X/Z, with Y in the source's world-height
  convention. Items, lights, static instances, cameras and sound sources use
  world coordinates. Sector heights are signed 256-unit steps; room sector
  dimensions count 1024-unit grid squares, in stored Z-major order.
- Angles are unsigned full-turn units: 65536 = one revolution. Animation speed
  and acceleration are signed 16.16 integers. Camera FOV/roll and packed frame
  words retain their original integer units, not radians.
- `None` means absent in that source. An empty list/byte string means present
  with zero elements. In particular PSX has no PC palette, light map, demo data,
  combined PC sample blob or PC sample-offset table. PC has no CLUT bank or VAB
  program/tone tables. PSX cutscene files have no VAB bank/program/tone data;
  their `samples` list is empty. Never fill a missing palette/frame/table with
  invented zeroes.
- Field names and meanings are identical across sources. A value with different
  source encoding is explicitly tagged rather than equated. `reserved` holds
  uninterpreted, bounded source fields/padding; their preservation is not a
  claim to know their gameplay meaning. Platform padding is excluded from
  gameplay comparison, not confused with semantic values.
- Byte arrays are immutable `bytes`. Texture/sample encoding is explicit.
  References remain source table indices/byte offsets, not Python pointers.
  Duplicate mesh pointers remain duplicate pointers, but physical meshes are
  decoded once. No source mesh is discarded because it looks unused.

## Level

`Level` stores `source` (`pc` or `psx`), case-folded filename stem `name`, input
`sha256`, `file_bytes`, the fields below, plus a physical `sections` ledger and
source-only `reserved` metadata.

| Field | Meaning |
|---|---|
| `rooms` | Ordered `Room` array, including unused/alternate rooms |
| `meshes` | Byte-offset -> decoded `Mesh`, one per distinct mesh pointer |
| `object_textures`, `sprite_textures` | Ordered `Texture` arrays |
| `images` | 256x256 texture pages: PC 65536 8-bit palette indices; PSX 32768 bytes, low nibble first, 4-bit CLUT indices |
| `palette` | PC 256 RGB triples (original six-bit channels), or absent |
| `cluts` | PSX 1024 palettes of 16 little-endian BGR555/STP words (32 bytes), or absent |
| `light_map` | PC 32x256 palette-index lookup bytes, unchanged, or absent |
| `samples` | Exact per-sample encoded bytes, in sample-directory order |
| `sample_encoding` | `pc_wave` or `psx_spu_adpcm`; no transcoding performed |
| `data` | Gameplay and media tables described below |

The PC sample blob is divided by the checked final offset table; the last
sample ends at the declared blob length. PSX sample sizes come from the VAB
256-word size table: word zero and unused entries must be zero, and the next
`vags` words count 8-byte units. **A u32 body-size word follows the size table**;
it must equal their byte sum. Sample bytes start at `2600 + programs*512`,
and end exactly at `VAB.fsize+8`, with zero tail slack. There is no separate
texture header after a sound bank. Bankless cutscenes instead start with an
eight-byte zero header before their pages. Full encoded sample bytes are kept.

## Rooms

- `info`: `x`, `z`, `y_bottom`, `y_top`, all signed 32-bit.
- `vertices`: `x`, `y`, `z` signed 16-bit, and tagged `lighting`.
  PC vertex lighting is `{value: u16, encoding: tr1_darkness_13bit}`. PSX
  vertex lighting is `{value: u16, encoding: psx_vertex_8bit}`; the full stored
  word is kept even though the documented intensity occupies eight bits.
  The documented relationship `PC = 8191 - (PSX << 5)` is tested diagnostically,
  never applied to either gameplay input or used to suppress a finding.
- `quads`, `triangles`: `Face` objects with common polygon vertex order. PSX
  **room** quad strip vertices decode as (0,1,3,2); PSX entity-mesh vertex
  indices are already polygon order (`src/format.h:5840-5899`) and are not
  swapped. **`texture_corners[i]` gives
  the texture UV slot for polygon vertex i**: PC quad (0,1,2,3), PSX quad
  (0,1,3,2), either triangle (0,1,2). Flat faces have no texture-corner map.
  Consumers call `ir.face_uv(level,face)` rather than pairing the raw UV array
  directly with vertices. Raw object texture slots are not blanket-swapped;
  triangle-only descriptors can contain a placeholder fourth slot.
  `flags` retains the complete u16, `texture` is its 15-bit reference, and
  room `color` is absent. Bit 15 is double-sided.
- `sprites`: vertex and sprite-texture indices, both u16.
- `portals`: destination `room` u16; signed 16-bit normal XYZ and four ordered
  XYZ corners (`x0/y0/z0` through `x3/y3/z3`). No winding or coordinate matching
  is performed during equivalence.
- `z_sectors`, `x_sectors`: u16; `sectors` contains their product, in file order.
  Each sector has `floor_index` and `box_index` u16; `room_below` u8, `floor` i8,
  `room_above` u8, `ceiling` i8. Sentinel values (including room 255) are kept.
- `ambient`: tagged source TR1 darkness word, same meaning as PC room ambient.
- `lights`: signed 32-bit XYZ, tagged source intensity u16, radius u32 in
  source world-distance units, and PSX two-byte reserved field (absent on PC).
  No consumer-specific radius doubling is applied.
- `static_instances`: signed 32-bit XYZ, angle u16, intensity u16, static-mesh
  definition id `mesh` u16; PSX reserved word is present, absent on PC.
- `alternate_room` i16, `flags` u16, including original sentinels/unknown bits.
- `reserved`: PSX geometry prefix word, optional PSX geometry alignment word,
  or empty PC geometry alignment bytes. Declared room word count must equal
  decoded records plus the format's permitted alignment; extra geometry bytes
  are not swallowed as an opaque remainder.

## Entity/static physical meshes

A `Mesh` has signed 16-bit `center` XYZ, radius u16, source flags u16,
`vertices` signed 16-bit XYZ, and exactly one of `normals` (signed XYZ) or
`lighting` (signed source words). Missing normals/lighting are `None`, not
zero vectors. `faces` contains textured and flat faces; quads have the same
polygon order as room quads. `source_offset` is a byte offset in mesh data.

`Face.color` is the source flat-material index, and `Face.texture` is absent
for flat faces. PC stores flat faces in separate quad/triangle arrays. PSX
flat material references below 256 are interpreted using the **OpenLara
`2333adef` `src/format.h:5887-5888` non-`SPLIT_BY_TILE` branch**, not claimed as
an independently established universal format fact. The retail gate records
per-pointer mesh flat/textured face counts and cross-source congruence as
data evidence; genuine differing meshes remain findings. Other references
are textured object-texture indices. The material index refers to that source's
palette/material domain; it is not an RGB value. The source level identifies
the domain. `flags` always retains the full original reference/flag word.

PSX mesh vertex/normal fourth words are retained separately in
`reserved.vertex_w` and `reserved.normal_w`; absent on PC. The source uses the
sign of the vertex count to choose PSX normals versus lighting, whereas PC
has a separate signed normal count. Physical inter-mesh padding (at most
three bytes) must be zero. Invalid/odd/out-of-range pointers, unexplained
prefixes, nonzero padding and large inter-mesh gaps are errors.

## Textures

A `Texture` contains `page` index, optional PSX `palette` CLUT index, source
UV-slot integer-texel pairs (`uv`: four object slots, two inclusive sprite
bounds). A face's explicit `texture_corners` maps these slots to its common
polygon order; the array alone does not define a polygon. It also has optional
object `attribute`, and optional sprite world-space
`bounds` (left, top, right, bottom). Sprite texture attribute is absent, not
an invented alpha mode. PC sprite size words are retained as unsigned fixed
8.8 width/height; their high bytes determine the second integer UV corner,
with the source byte wrap retained. PC object coordinate adjustment bytes,
PSX object reserved word, and the two high page-reference bits remain in
`reserved` rather than disappearing during decode.

Image bytes and the source palette/CLUT are accessible through `Level` by
reference, not duplicated per texture. No quantisation, palette synthesis,
wrap inference or texture conversion output is produced in M0.

## `data` tables

Field order and signedness are executable in `ir.SCHEMAS`.

| Key | Elements / fields |
|---|---|
| `floor_data` | u16 command words, no trigger rewriting |
| `mesh_data` | Exact source mesh-word blob; decoded meshes are also available |
| `mesh_pointers` | u32 byte offsets, aliases preserved |
| `animations` | u32 frame byte offset; u8 rate and frame size (words); u16 state; i32 speed/acceleration; u16 start/end frame, next animation/frame, state count/offset, command count/offset |
| `states` | u16 state, range count, range-array offset |
| `ranges` | i16 low/high frame, next animation, next frame |
| `commands` | Signed i16 animation command words, unchanged |
| `bones` | Signed i32 node words: repeating flags, X, Y, Z; flags retain all bits |
| `frames` | Exact u16 frame words: signed bounds/root-position words retain two's-complement bits; joint count followed by paired packed 10-bit rotations. Animation frame offset/size locates frames. No frame compression or float angles |
| `models` | u32 type, u16 mesh count/start, u32 bone-word offset and frame-byte offset, u16 animation; PSX reserved word, absent on PC |
| `static_meshes` | u32 id, u16 mesh pointer index, two sets of six i16 min/max bounds (visible/collision), u16 flags |
| `sprite_sequences` | u32 type, signed i16 count (normally negative), u16 first sprite index |
| `cameras` | i32 XYZ, i16 `room_or_speed` (camera/current union), u16 flags |
| `sound_sources` | i32 XYZ, u16 sound-map id and flags |
| `boxes` | i32 min/max Z, min/max X, i16 floor, u16 overlap reference/flags |
| `overlaps` | u16 adjoining-box references and end bits |
| `zones` | i16 words, six box-count arrays: normal ground1/ground2/fly, flipped ground1/ground2/fly |
| `animated_textures` | u16 words: group count, then count-minus-one and indices per group; padding is preserved |
| `items` | u16 type/room, i32 XYZ, u16 angle, i16 intensity, u16 flags |
| `cinematic_frames` | Eight i16 words: target XYZ, camera XYZ, FOV, roll |
| `demo_data` | Exact PC demo bytes; absent on PSX |
| `sound_map` | 256 signed i16 indices into sound-details table, including -1 |
| `sound_details` | u16 sample index, volume, chance, flags (no invented floating-point defaults) |
| `sample_data` | PC combined source sample blob; absent on PSX |
| `sample_offsets` | PC u32 source byte offsets; absent on PSX |
| `vab_programs`, `vab_tones` | PSX VAB program/tone source metadata described below; absent on PC and PSX cutscenes |

VAB metadata is not substituted for sound-map/detail gameplay values. The bank
header stores original size/version/id, program/tone/VAG counts, volume, pan,
attributes and reserved fields. All 128 16-byte program records are retained
as `(6 u8, u16, 2 u32)`. Every allocated tone record (16 per declared program)
is retained as `(16 u8, 8 i16)`; unallocated/reserved records are not skipped.
The complete 256-word sample-size table and decoded body-size word are kept
in `reserved`. Bank tail slack must be zero; no sample bytes are labelled as
a texture header. No retail executable or embedded code is inspected.

## Full-byte accounting and comparison

Each `sections` entry has a unique name, absolute offset, element count,
physical bytes, count-prefix bytes, and payload bytes. Room geometry also
records vertex/quad/triangle/sprite counts and triangle equivalents. These
entries form an ordered, non-overlapping partition of the complete file.
`Reader.finish()` rejects even one trailing byte; all reads are bounds checked.

`account.py` is a separate count-only walk. It does not invoke the reader or
use its IR/cursor/section boundaries/record schemas. Its separate arithmetic
must reproduce every count/start/prefix/payload/physical-byte total, including
room geometry element counts. **This detects coding/accounting disagreements,
not an independently wrong shared layout belief.** Round trips have the same
limitation. The following consumer/data invariants address that boundary:

- Every PSX 16-byte ADPCM block has predictor <=4, shift <=12, flags <=7;
  each sample is a whole number of blocks and its last block has end bit 1.
- Every used quad UV mapping is convex (point/line degeneracies are separately
  counted, not confused with crossed UV order).
- Every PC/PSX room/mesh triangle or quad matching by object identity and its vertex
  position multiset has the same **per-position, rectangle-relative UV layout**.
  Starting corner/winding and atlas origin do not define that layout. Exact
  source UVs remain untouched in the IR; absolute atlas-placement matches and
  differences are counted separately, never silently equated. All room/mesh
  triangle/quad geometry congruence/unmatched counts are reported per paired
  level. Genuine edition geometry differences remain findings. A textured
  polygon check with no common textured geometry fails instead of passing vacuously.

These are mandatory `cook-m0` pass/fail paths, and their counts are in
`counts.json`. The PSX geometry prefix must also equal its vertex count.

`equivalence.py` uses a shortest insert/delete edit script before comparing
sequence entries, rather than turning an insertion into a cascade of field
differences. Rooms and sector cells remain coordinate-addressed, not freely
reordered sequences. It compares all gameplay sections listed above, records
every edit hunk/touched-element count and every field difference, and keeps
five examples per class per level. A constant-offset label requires more than
one observation and a single delta among all mismatches in that class.
State/range/floor-index changes proven by another table's edit map are labelled
**derived renumbering**, separate from primary table changes, never hidden.
Dependencies are resolved in order: align ranges, map range offsets solely in
state **matching keys**, align those states, then classify animation offsets.
Raw state offset changes are still compared/reported, including records that
match under those keys. Box overlap flags are kept; only the low 15-bit
index is mapped through the overlaps edit script. A changed high flag remains
primary. Every level prints separate primary and derived totals.
Floor trigger words whose only change is a mapped item index are also derived
from the items edit script: the low 10-bit argument must map exactly and all
upper action/end bits must agree. Word-offset pairs and raw edit hunks remain
in the report; genuine added/removed triggers remain primary.

Item alignment produces an explicit index map. The trigger report parses
both-sided floor-start pairs **and every one-sided room/sector**, recording
added and removed item references with room, sector, item/type and word
offset. A reference-set change makes overall `consistent` false even if all
shared references renumber correctly; `renumbering_consistent` states that
narrower result. Zero-reference cases are explicitly `no item references`
with null consistency, not an unqualified True. Counts state their dedup
scope (paired distinct floor starts, plus individual one-sided sectors).
Rules: [TRosettaStone3 sections 4.4/4.6](https://opentomb.github.io/TRosettaStone3/)
and the independently reviewed OpenLara `2333adef` pickup consumer
`src/lara.h:2108`: switch/key/**pickup** first entries and actions 0/6
reference items; camera action 1 has an extra parameter word supplying the
action-list end bit. Raw source words remain unchanged. The readable report
opens with a per-level primary gameplay overview (items, triggers, floor
edits, room flags, boxes), followed by all raw/derived evidence and lighting
encoding residuals.

## Commands and artifacts

From the repository root:

```
make -f tools/cook/cook.mk cook-tests PYTHON=python3 BUILD_DIR=$TR64_WORK/builds/Cook
make -f tools/cook/cook.mk cook-m0 PYTHON=python3 BUILD_DIR=$TR64_WORK/builds/Cook TR64_DATA=$TR64_DATA
```

The second command reads every matching file, fails on an unreadable/malformed
level or any accounting mismatch, and writes only these external artifacts:
`counts.json`, `equivalence.json`, `equivalence.md`, `sizing.json`, `sizing.md`
under `$BUILD_DIR/cook-m0`. A pool/cart overflow is a reported finding, **not**
a parsing failure or a reason to reduce the measured model. Reports name the
Python executable/version, tool/pool/input hashes, content key, and artifact
directory. The entrypoint prints measured wall time; it is not N64 performance.
The M0 analysis emits no game pack or content cache entry; the architecture's
native-output content cache begins with native writers, outside this slice.

Synthetic fixtures in `synthetic.py` populate every section in both source
formats and encode the decoded IR back to identical source bytes. Tests cover
PSX bankless cutscenes, source absence, negative signed values, reserved words,
mesh aliases, normals and lights, texture/CLUT dedup, exact 70-vertex batching,
UV/TMEM partitioning, every equivalence class mechanism and budget overflow.
Production-reader negatives reject a truncated last section, one trailing
byte, and a room geometry word count off by one for the stated reason. A
separate mutation of the reader ledger is rejected by independent accounting.
