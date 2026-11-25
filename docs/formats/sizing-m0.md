<!-- SPDX-License-Identifier: MIT -->
# M0 sizing: three explicit candidate models

Executable arithmetic: `tools/cook/sizing.py`, `model_b.py`, `model_c.py`.
The measurements are in the external `$BUILD_DIR/cook-m0/sizing.{json,md}`.
This document specifies the calculation, not a native file format or a chosen
renderer. **Models B/C are candidate designs for Main, not implementations of
new tiny3d/Magma features and not replacements for model A's overflow lists.**
No model is adjusted to fit a budget. All values are uncompressed bytes; KiB
means 1024 bytes. No timing or quality claim follows from a byte estimate.

## Inputs, budgets and evidence

Readers and full-byte independent accounting are specified in [ir.md](ir.md).
Every source filename is discovered, not chosen from a favorable level list.
Each level/source result names its input SHA256. The report metadata has tool,
pool-table and input hashes and a content key. No source data is committed.

The **only** source of pool numbers is `include/tr64/pools.def`, parsed at
runtime for CARVE rows `core`, `tex`, `rooms`, `meshes`. The cook does not
modify it. Every needed byte total is compared with the exact row; the report
lists every failing level/source and its byte overage, and the worst case.

SDK layout facts were read in the pinned prefix
`~/sdks/n64-sdk-tlbcode-d0ec7bdec226`:

- `mips64-elf/include/t3d/t3d.h`, tiny3d `e84172f2`, lines 17 and 41–53:
  `T3D_VERTEX_CACHE_SIZE = 70`; **one 32-byte `T3DVertPacked` contains two
  vertices**, not one. Each vertex has 3 i16 position, packed normal u16,
  RGBA8 u32, UV 2 i16. The pair has eight-byte alignment.
- `mips64-elf/include/magma_constants.h`, line 17:
  `MG_VERTEX_CACHE_COUNT = 48`. Model B's 8-byte candidate is position XYZ i16
  plus lighting u16; it is a source-sized proposed ABI, **not** a claim that
  every source entity mesh or Magma's complete submission record is 8 bytes.
- CI4 indexed texels use lower **2048 bytes** of 4096-byte TMEM when TLUT is
  enabled; upper TMEM holds palettes. A CI4 palette has 16 two-byte entries.
  The model never treats all 4096 bytes as simultaneously available for
  indexed texels plus TLUT. CI8 also has a 2048-byte texel limit.

The source layout facts are from the permitted OpenLara `2333adef` format
headers/readers and public format documentation; no earlier N64 renderer,
streaming policy or native pack layout is a design input.

## Common core model (all geometry/texture alternatives)

**Assumption:** level core is resident, uses 32-bit offsets rather than host
pointers, and consists of these contiguous tables. Every table starts at an
8-byte boundary; `align8(n) = 8 * ceil(n/8)`, with `align8(0) = 0`. Core bytes
are the sum of the individually aligned table byte products below. Source
count prefixes, disk padding, raw mesh blobs, source texture pages, VAB tables,
and encoded effect bytes are not resident core.

| Table | Unaligned byte calculation |
|---|---:|
| Level header | 128 |
| Room headers (info, flags, alternate, dimensions, counts/offsets) | 64 * rooms |
| Sectors | 8 * all room sectors |
| Portals | 32 * all room portals |
| Room lights | 20 * all room lights |
| Room static instances | 20 * all room instances |
| Room sprite references | 4 * all room sprites |
| Floor data | 2 * source floor words |
| Mesh pointer table | 4 * source mesh pointers (including aliases) |
| Animations | 32 * animations |
| State changes | 6 * states |
| Animation ranges | 8 * ranges |
| Animation commands | 2 * command words |
| Bones | 4 * bone words (four words per node) |
| **Animation frames (reported separately, included in core)** | **2 * frame words** |
| Models | 20 * models |
| Static mesh definitions | 32 * definitions |
| Object texture metadata | 24 * object textures |
| Sprite texture metadata | 24 * sprite textures |
| Sprite sequences | 8 * sequences |
| Cameras | 16 * cameras |
| Sound sources | 16 * sources |
| Boxes | 20 * boxes |
| Overlaps | 2 * overlaps |
| Zones | 2 * zone words (six arrays per box) |
| Animated texture lists | 2 * words |
| Initial item definitions | 24 * items |
| Cinematic frames | 16 * frames |
| Demo data | exact bytes, zero if source lacks this section |
| Sound map | 2 * 256 |
| Sound details | 8 * details |
| Effect directory | 8 * encoded samples |

The 20/24/64/128-byte native strides are **assumptions**, not proven engine
struct sizes. The raw animation frame representation is retained without
compression. The PC light map is replaced by vertex/texture lighting, and its
palette by native texture palettes; neither original table is charged again
to core. VAB program/tone synthesis data is not needed for replaying the
retained effect samples. Mutable item/AI/effect state belongs to the separate
`state` row and is not silently borrowed from core. This slice does not size
that row, resident code, audio runtime, or other non-level pools.

The JSON exposes both `core.tables` (unaligned products) and
`core.aligned_tables`; `core.animation_frames` is the exact frame contribution,
not an additional allocation. The header and metadata tables are deliberately
retained even if a future native format can eliminate them.

## Model A1 — stock tiny3d, border on every edge (assumption)

This retains the original all-edge-border assumption and its failure lists.
`pools`, `room_geometry`, `mesh_geometry`, `textures_ci4`, `textures_ci8`,
`cart_bytes` and `summary` in JSON refer to **A1**; `model_a2` is separate.
B/C are named candidate designs. A1/A2 remain cart-storage candidates even
when room geometry requires a cache; no streaming cache is implemented here.

### Referenced texture rectangles and CI4/CI8 storage

1. Collect object textures used by **all** room/entity/static faces, all
   members of animated texture groups, room sprites, and all sprite-sequence
   members. Every decoded physical mesh is included, even if an item does not
   currently reference it. Flat mesh faces carry resolved vertex color, not a
   texture allocation. Every reference is bounds checked.
2. For an object used by triangles only, use its first three UV corners. For
   one used by any quad use four. Animated-only entries assume four corners.
   Sprite UVs are the two stored inclusive corners. The rectangle is their
   inclusive integer bounding box, `w=max(u)-min(u)+1`, similarly for height.
   A descriptor used by both shapes has one union rectangle, not two copies.
3. Extract that source page's indexed pixels. PSX keeps native 4-bit indices
   and its exact CLUT; PC CI4 is **a size estimate before quantisation**, not
   a generated 16-color image or a claim about quantisation error. Identical
   rectangles are deduplicated by `(width,height,source-index pixels,palette)`.
   Overlapping but non-identical rectangles are **not** deduplicated in A.
4. **A1 assumption:** every tile has one filtering border texel on all sides. For bit depth
   `b`, the row stride of interior width `w` is
   `row = align8(ceil((w+2)*b/8))`. The largest interior height is
   `hmax = floor(2048/row)-2`. Use the rectangle's whole width where possible,
   then maximal vertical strips, left-to-right/top-to-bottom. No arbitrary
   per-level scale factor is used. All source rectangles fit a 256x256 page.
5. Tile image bytes are `row*(tile_height+2)`. Identical padded tile pixels at
   equal width/height share image storage (PSX even across distinct CLUTs).
   PSX CLUTs are separately deduplicated by their complete 32-byte contents.
   PC assumes a separate synthesized 32-byte palette per unique rectangle;
   it does not assume different quantisations will yield identical palettes.
6. Each distinct rectangle tile has an assumed **32-byte descriptor** in the
   texture pool (image offset/stride, dimensions, palette reference and flags).
   Texture total = distinct padded tile-image bytes + palette bytes +
   32 * rectangle-tile descriptors. Image dedup does not remove descriptors.
7. The PC **CI8** second column repeats the same rectangle/border/stride/TMEM
   calculation with 8-bit texels and the original shared 512-byte native
   256-color palette once per level. It is a separate all-CI8 scenario, not
   mixed with A's CI4 pool total. M1 must choose actual mixed CI4/CI8 based on
   quantisation error. PSX has no fabricated CI8 column.

**Hand example:** a 64x64 CI4 rectangle has a 66-texel padded row: ceil(66/2)
=33, align8=40 bytes. `hmax=floor(2048/40)-2=49`; the strips are 64x49 and
64x15. Images cost `40*51 + 40*17 = 2720` bytes, plus one 32-byte palette and
two 32-byte descriptors = **2816 bytes** if no dedup applies. It is not called
a 2048-byte texture after omitting filtering borders. This exact model can
exceed the size of entire native pages when many distinct rectangles overlap;
that is a reported property, not an excuse to adjust the model.

### Geometry partitioning and exact vertex duplication

Run separately for every room and every distinct physical mesh; no vertex
shares storage across objects. PSX **room** quad strip indices are converted
to common polygon order; PSX entity-mesh indices already use polygon order.

- A quad becomes triangles `(0,1,2)` and `(0,2,3)`. UVs are obtained with
  `ir.face_uv`, applying the explicit per-face corner map (PSX quads map
  slots 0,1,3,2; triangles 0,1,2). Original vertex identity plus exact UV
  is the sharing key; source
  indices are not welded just because coordinates happen to coincide.
- Clip each source triangle into its actual A texture tiles in continuous UV
  space. Tile boundaries are halfway between neighboring integer texels
  (e.g. strips ending at y=48 and beginning at y=49 meet at 48.5). Exact
  rational barycentric weights identify generated vertices and the attributes
  that will be interpolated; no floating tolerance merges different vertices.
  A fragment is degenerate only when its source-triangle barycentric area
  is zero, **not** merely because its UVs are collinear or constant.
- Group generated triangles by texture tile/material attribute/double-sided
  state within each object. Greedily append triangles in source order until
  adding one would exceed 70 distinct vertex keys; start another batch.
  Count every new batch copy of a boundary vertex. Flat material faces share
  their solid group, but vertex keys also include their color index.
- Room sprites are charged four UV'd billboard vertices and two triangles
  **per texture tile**, instead of pretending their center vertex is a quad.
  The eventual runtime may generate them; the static model still counts them.
- For each batch with `V` vertices and `T` triangles: vertex bytes are
  `32*ceil(V/2)`; indices are three u8 cache entries per triangle, with the
  batch's index bytes rounded to 8 (`align8(3*T)`). Assumed batch header is
  **32 bytes** (vertex/index offsets and counts, material/tile and flags).
  Add **32 bytes per room/mesh render object** for its bounds/offsets/counts.
  Room render headers are distinct from the core's logical room headers.
- Geometry bytes = sum of batch vertex/index/header bytes + object headers.
  `cache_duplication` counts extra copies caused by the 70-entry boundary;
  `tile_uv_duplication` counts the increase over used source vertices before
  cache batching (including clipping and billboard corners). Unused source
  vertices are reported but not treated as negative duplication. JSON also
  contains per-object loads, counts, batch and body bytes; an object's explicit
  32-byte header is separately named there.

**Hand example:** 24 disjoint triangles (72 unique vertices) in one group:
first batch 23 triangles/69 vertices, second 1 triangle/3 vertices. Packed
vertices = `35*32+2*32=1184`; indices = `align8(69)+align8(3)=80`; headers =
64; object =32; total **1360 bytes**. A 70-vertex cache is not a 70-byte cache.

## Model A2 — internal split borders only, outer edges clamped (assumption)

**A1 versus A2 is an RDP filtering/clamp behaviour to measure in M1.** Neither
is declared the production renderer's true behaviour by this byte report.
A2 shares A1's core, rectangle selection/dedup, palettes, headers, exact
barycentric clipping and 70-entry cache batching; it changes border placement:

- Source images are at most 256 texels wide. Use full-width vertical strips,
  with no left/right padding because those are outer edges. Row bytes are
  `align8(ceil(width*bits/8))`.
- At interior offset y, top padding is 0 for the first strip, 1 thereafter.
  If all remaining rows fit with that padding, they form the last strip with
  no bottom padding. Otherwise bottom padding is 1 and interior height is
  `floor(2048/row)-top-1`. Every tile image is `row*(height+top+bottom)`.
- Extract the actual padded pixel extent and deduplicate only identical
  images/row stride. Internal UV boundaries remain at half-texel positions.
  Count actual newly clipped vertices/batches rather than scaling A1 counts.
- For 64x64 CI4, row=32, all 64 rows fit in 2048 bytes: one tile, no split.
  A1 instead uses 49+15 interior rows and 2720 image bytes. This explicit
  assumption explains a large change without choosing the more favorable one.

`model_a2` contains separate CI4/PC-CI8, room, mesh, pool-overage and per-level
cart columns. Both A1 and A2 vertex loads are compared with every B variant.

## Model B — shared vertices, per-corner UV

JSON: `model_b.rooms` and `model_b.meshes`, each with four variants:

| Variant | Position/lighting record | Cache entries | Interpretation |
|---|---:|---:|---|
| `v16_c70` | 16 B (32 B per pair) | 70 | tiny3d-sized/cache candidate |
| `v16_c48` | 16 B (32 B per pair) | 48 | isolate cache-size effect |
| `v8_c70` | 8 B | 70 | isolate vertex-size effect |
| `v8_c48` | 8 B | 48 | source-sized/Magma-cache candidate |

**Assumptions:** a source vertex's position/lighting is shared by every face
regardless of texture. A 16-byte record leaves UV slots unused; an 8-byte
record is XYZ i16 + lighting u16. Neither record claims to encode every
possible normal/dynamic lighting operation without backend work. A flat face's
color is a per-face material, not a reason to duplicate a shared position.
This ABI requires a per-corner UV path; stock tiny3d's existing vertex UV
interface does not implement it. The count is not a benchmark or an engine
backend choice.

### Minimal aligned face record (8 bytes)

| Field | Bytes |
|---|---:|
| Four u8 cache indices (fourth=255 for triangle) | 4 |
| u16 object-texture/material id | 2 |
| u8 corner-order selector | 1 |
| u8 flags (triangle/quad, flat/textured, double-sided, sprite) | 1 |

The object texture table supplies the four corner UVs. Eight bytes is the
smallest four-byte-aligned record for these seven-to-eight bytes of required
fields; variable triangle-only records are not assumed. No texture-driven
vertex split or A-style TMEM clipping occurs in B's stored geometry: a backend
must derive upload/subtile handling from texture metadata. This is why B must
not be presented as a drop-in renderer implementation.

### Real cache partition and arithmetic

For each room/mesh, iterate its original quads then triangles (mesh arrays in
source order), adding complete faces greedily while their union of source
vertex indices fits the selected cache size. Start a new batch before the
first face that exceeds it. This is deterministic authored/source order,
not an optimal partition or a claimed spatial optimizer. Inside an existing
batch, sorting faces by texture would not duplicate vertices; no metric or
texture key is added to vertex identity. Sprites have four independent
billboard corner identities per sprite, not just their center.

Three policies are retained for every level, variant and object:

1. **File order (`greedy`)**: the complete-face greedy procedure above.
2. **Locality (`locality`)**: seed a batch with the first remaining face.
   Grow it from faces incident to its current vertices, maximizing shared
   vertices, then minimizing new vertices, then choosing earliest source
   order. A candidate must fit the cache. If no adjacent face fits, append
   the first remaining face if it fits; otherwise close the batch. This is
   real vertex-adjacency partitioning, not a factor or an optimality claim.
3. **Lower bound (`lower_bound`)**: let U be distinct used vertex identities,
   F face count, C cache size. Assume every vertex loaded exactly once,
   `ceil(U/C)` batches per object, and the most favorable pair padding:
   B16 vertex bytes `32*ceil(U/2)`, B8 `8*U`; add `8*F +
   32*ceil(U/C) + 32` per object. It is not necessarily a legal partition
   because arbitrary faces may straddle the hypothetical batches.

Sum object bounds, never pool vertices across rooms/meshes. The JSON stores
all three policies and every real batch; both vertex strides reuse the same
measured partition for a given cache size. The final table shows lower-bound,
file-order and locality bytes plus row margins. A case whose lower bound fits
but a measured partition exceeds the row is **depends on batching**, not a
categorical no-fit. When both measured partitions fit, the verdict says
exactly that, not that every possible partition or production backend fits.


Every face's 3/4 vertices must reside in a **single** batch. Count boundary
copies explicitly. Shared-before = distinct used vertex identities; after =
sum of distinct batch vertices; boundary duplication = after-before. Vertex
loads = after (not the padded record count). B16 vertex bytes sum
`32*ceil(batch_vertices/2)`; B8 sum `8*batch_vertices`. Face bytes =8*faces;
batch headers=32*batches; object headers=32*objects. Their sum is each B
geometry pool need. The same partition is repeated for each cache size, so
16/70 and 8/70 have identical loads, as do 16/48 and 8/48. Odd pair padding
can make byte changes non-monotonic even when loads are monotonic.

The report shows per level and **every room**: shared vertices before/after,
faces, bytes, boundary duplication, A1/A2 vertex loads and all four B loads
for both real partitions, plus the optimistic lower bound. The A1/B and A2/B
ratios divide their actual transformed vertices by B's actual loads.
`1-B/A` is the transform saving fraction, **not** runtime speedup. Per-mesh
detail is in JSON. No B result rewrites either A1/A2 pool/cart findings.

## Model C — native pages / union coverage

JSON `model_c` exposes three distinct representations instead of disguising a
nonrectangular union as a contiguous DMA image. Rectangles are the **same
referenced rectangles** used by A, with no filtering border. Clusters connect
rectangles sharing at least one texel **on the same source page**; mere edge
adjacency does not connect them. Disjoint clusters on the same page do not
share a PC palette. Source page IDs are not deduplicated across pages.

1. **PSX native pages:** exact byte lengths of all original 13 CI4 pages plus
   all 1024 source 32-byte CLUTs, including duplicates. `native_pages.bytes`
   is the input payload total (13*32768+1024*32), not an estimate. It excludes
   any future descriptor ABI, so an exactly-full pool has no descriptor
   headroom. PC has no native CI4 pages: the field is absent, not zero.
2. **Union payload lower bound:** form exact 256-bit per-row coverage masks
   per overlap cluster. Sum union texels, then `ceil(cluster_texels/2)` for
   each cluster's tightly packed nibbles. Add one 32-byte palette per PC
   cluster (a quantisation **assumption**, with quality unmeasured), or the
   complete source CLUT count *32 for PSX. This is the requested coverage
   arithmetic, explicitly **not** an addressable rectangular image if holes
   exist. Clustering/index sharing does not merge different PSX CLUTs.
3. **Bounding uploadable clusters:** allocate each cluster's bounding image,
   no border, `ceil(width/2)*height` bytes. Hole texels consume storage. Add
   the same palettes plus an assumed 16-byte descriptor per cluster. Texel
   rows are tightly stored; TMEM's own stride may require a gather/upload
   adjustment. Upload subrectangles of this larger image with per-face CLUTs;
   a large cluster need not fit TMEM all at once. No entire-cluster upload is
   assumed. This is a byte model, not an implemented upload scheduler.
4. **Sparse addressable rows:** for transparency, also show the actual union
   as contiguous row runs. Each run costs `ceil(run_width/2)` bytes (nibble
   padding counted per run), plus an assumed **8-byte run directory record**;
   add cluster descriptors and palettes. Its upload path requires gathering
   runs into rectangles. No claim is made that ordinary contiguous-image DMA
   can address this representation directly.

**Hand example:** two 4x4 rectangles at (0,0) and (2,2) overlap in four
texels. Union=28 texels=14 CI4 bytes; bounding image=6x6=18 bytes. For PC the
single cluster's palette is 32 bytes. Union payload is **46 bytes** (lower
bound); bounding uploadable bytes **18+32+16=66**. There are six row runs,
with widths 4,4,6,6,4,4. Sparse image bytes =2+2+3+3+2+2=14; addressable
sparse total=14+32+16+6*8=**110 bytes**, not 46.

### Rectangle-size and palette evidence

For each level, the report lists the referenced **object** rectangle size
histogram (width x height -> entries), every object rectangle exceeding
2048 CI4 bytes and its texture id/dimensions/bytes, and their count. Here
bytes=`ceil(width/2)*height`, with no border or row alignment. A rectangle
below this limit may exceed A's bordered/aligned limit. JSON also includes
a histogram of *all source object descriptors with four stored corners*,
explicitly separate from the referenced/used-corner distribution. Sprites
participate in coverage but are not smuggled into the object-only histogram.

Palette evidence includes the allocated palette count, number of distinct
**source palette contents**, and number of referenced distinct source palettes.
PC has one source 256-color palette but the C candidate allocates one future
16-color palette per cluster. PSX distinct contents are counted by exact
32-byte CLUT identity, while the C byte model deliberately retains the whole
source CLUT bank. A, unlike C, deduplicates CLUT contents.

## Model A cart accounting (still valid if geometry is cached)

For each level/source, add `align16(core) + align16(tex_A) + align16(rooms_A)
+ align16(meshes_A) + align16(encoded source effects) + 128` bytes. The last
128 is an assumed pack directory/header, separate from resident core's
128-byte level header. Encoded effects include their original PC WAVE or PSX
SPU bytes; **no unimplemented codec/compression saving** is credited. Original
PC RGB palette/light map, full source pages, VAB programs/tones and source mesh
blob are not added again. The original per-level/aggregate column does not
deduplicate anything between levels. A separate aggregate sound-dedup column
hashes complete encoded sample bytes **within each source**, counts each
distinct sample once at 16-byte alignment, removes the old per-level
`align16(sum(source samples))` contributions and adds the aligned unique
sample sum. Resident geometry/core/texture/header totals and per-level effect
references remain charged. Both A1/A2 aggregate carts and full 64-MiB
allocation rows expose undeduplicated and sound-deduplicated totals; no codec
or cross-source reuse is assumed. Unique sample count/bytes and original
sample byte totals are in `summary.cart`.

All available per-level carts are summed independently for PC and PSX. The
PC-only extra level is clearly unpaired, not silently excluded. The level
pack row is **24 MiB** (architecture 4.3), not the whole 64 MiB. For the full
allocation comparison add the other budget rows: 1 MiB image/units/table +
20 MiB music +16 MiB video +3 MiB reserve =40 MiB. These four allocations are
budget reservations, not measured media encodes. Both the 24 MiB level and
64 MiB allocation comparisons conclude the generated table. Resident pool
failure does not remove geometry from cart bytes or pretend it was streamed
for free. No all-B/all-C cart is fabricated before Main chooses a combined
geometry/texture design.

## Source byte cross-check, tests and limits

The count report retains payload and count-prefix bytes separately, so PC
LEVEL2 can be checked without confusing a 4-byte count with texture texels.
The entrypoint prints exact texture pages, room geometry (word-count prefixes
excluded), triangle equivalents, room count, floor data, mesh data+pointers,
animations/states/ranges/commands/bones/frames, frames separately, and sample
payload. These are source bytes, not predicted pool bytes. Divide by 1024
for the independent approximate KiB checks.

`cook-tests` exercises every populated source section with a generated-byte
round trip, production-path malformed/trailing/count negatives, independent
ledger mutation, exact cache batching and byte arithmetic, per-corner UV/TMEM
splits including degenerate UVs, palette/image dedup, all four B variants,
exact C overlap versus bounding-box area, and deliberate pool overflow.
Consumer invariants (ADPCM headers/end flags, convex mapped quad UVs and
cross-source geometry/UV congruence counts) are mandatory retail pass/fail
checks in `counts.json`, independent of byte-ledger agreement. Synthetic raw
offset/UV assertions and field-order shuffling ensure fixture encoding does
not merely reuse IR dictionary insertion order. A round trip is explicitly
not sufficient evidence that a shared source-layout belief is correct.
These tests need no retail input or SDK compilation. `cook-m0` reads every
retail level and emits the five reports; its wall time is analysis runtime,
not emulator performance. No ROM, rendered scene, quantisation quality,
media transcoding, native ABI loader, streaming system, or new RSP backend
is verified by this slice. A successful parser is not an engine-fit claim.
