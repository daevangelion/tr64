# Native room and mesh geometry, version 1

This is the contract for Geo, HostRaster and `engine/render`. Integers and
payload words are **big-endian**, with section-relative offsets, no relocation
and no host-order shadow copy. Room magic is `RG64`; mesh magic is `MG64`.
The section base, table starts and vertex payload start are eight-byte aligned.
Records have their stated stride, not individually padded. Padding/reserved
bytes are zero. RG64 and MG64 occupy planned sections of the shared `level`
arena, together with Core and TX64; their aggregate must fit1472KiB.

## Decisions and cost model

D4 uses tiny3d e84172f2 with the measured command-0 per-corner-UV patch.
**Main's 2026-11-17 correction to the renderer brief:** native faces are compact,
not expanded RSP commands. LEVEL6 has 19,773 room triangles; twenty bytes each
would consume 395,460 bytes before vertices, exceeding the former393,216-byte room row.
The CPU expands compact faces directly into the queue. No coordinates are
computed per face. This replaces the original copy-only requirement explicitly.

Report each frame: submitted source polygons (including split pieces), submitted
triangles before RSP culling, vertex loads including pair padding, texture
rectangle uploads, palette loads, expanded triangle-command bytes, raw-RDP
record bytes, and CPU submission time. GPU completion and pacing are separate.
The unreliable SDK-internal queue-byte/handoff observer is removed; command
and record bytes remain exact. Neither is resident geometry size.
A triangle is always five queue words / 20 bytes; a quad is two triangles /
40 bytes. Both back ends count the same input submissions. RSP post-cull metrics
are additional observations, never a substitute for the submitted count.

**Main's draw-order decision (2026-11-17): batch-major.** The cook writes runs
in `(batch, class, palette, flat, descriptor, tile, double_sided, ST words)`
order; faces follow that stored order. Walk it directly. Load vertices only
when the batch or matrix instance changes. No second run table, per-frame
sort, or material-merge metadata is carried. The wire layout itself permits
a different stored run order without a format change; measurements, not the
M0 upload-bound A/B alone, would have to justify that cook policy change.
At level bind, the renderer builds a12-byte `(first_run,first_face,run_count)`
span per object in the state pool and checks contiguous object run ranges.
There is no geometry copy or per-frame sort. This avoids rescanning the whole
section for every static instance. A future interleaved ordering would need
its traversal changed, not a native-format revision.
On stock4MiB ares, the profiled PC LEVEL6 statics frame fell from77.890 to
15.991ms; run scans fell72,801 to406, scan CPU time61.158 to0.810ms.
The sightline fell31.192 to12.216ms. All26 posed/source cases then completed
under16ms; each available before/after picture was pixel-identical.
Evidence: `$TR64_WORK/evidence/m1-render/m1-scenes-after-spans.json`.

The resident submission loop and the cart matrix/material/batch entry points
are placed first in `.text` by `plat/n64/hot.ld` (architecture section 6.1),
together with the rest of the render window, so they cannot alias each other in
the VR4300's 16KiB direct-mapped instruction cache, without another code unit or
buffer. History: an otherwise identical main build regressed to32.617ms on the
placed bridge route after code growth; grouping the four functions reduced its
worst frame to28.462ms, with2874 triangles,396 uploads,157 palette loads and83
matrices at tick54. All181 game hashes and the final picture were unchanged.
SDK-revision and layout-matched audio-mix controls excluded those as the
measured cause.
This is not a hardware fill-rate claim or a25ms pass; remeasure after major
resident-code growth. Controls: `$TR64_WORK/evidence/m3-render/` files
`main-regression-controls.json`, `hot-all-control.log`.

Retain the current texture and palette across batch/room boundaries; do not
flush TMEM merely because a matrix/scissor changed. Within a batch, equal
image/palette keys never reload; across batches/rooms, immediate repeated
keys reuse TMEM but no global regrouping is attempted. Process reachable rooms
in logical-id order, each room's geometry then its statics in Core instance
order, each instance's mesh runs in stored order. Opaque/cut-out have Z writes;
keyed fragments write neither RGB nor depth. Clip to the owning logical
room's portal rectangle. Exact-depth ties use this stable submission order.

Attribute 2 is retained as a separate blended class. **M1's reference policy**
draws it with colour-key plus opaque depth writes (including PSX 0x8000), not
PSX STP blending. Its class is not erased or conflated with attribute 0. Within
each batch it follows cut-out. No claim of M2/gameplay blending parity is made.
`gpu.h` names this policy explicitly.

## Exact tables

Header `>4s15I`, **64 bytes**, in this order:

| Offset | Field |
|---:|---|
| 0 | RG64 or MG64 |
| 4 | version = 1 |
| 8 | total bytes, including final alignment |
| 12 | object count |
| 16 | batch count |
| 20 | run count |
| 24 | compact-face count |
| 28 | packed-vertex-pair count |
| 32 | object-table offset |
| 36 | batch-table offset |
| 40 | run-table offset |
| 44 | face-table offset |
| 48 | vertex-payload offset |
| 52 | triangle count (quads count twice) |
| 56 | command ABI = 1 (20-byte corner-UV command) |
| 60 | reserved zero |

Tables are laid out in that order; align each start to eight. Vertex payload
is already a multiple of 32. Empty tables still have the current aligned offset.
Reject overflow, overlapping/out-of-order tables, trailing unexplained bytes,
unknown flags, invalid material/vertex references, or an inconsistent sum.
Maximum objects/batches/runs in either section is 65,535. Counts are bounds,
not permission to allocate from an unbudgeted heap.

Object `>IIHH6h2I`, **32 bytes**:
source id u32, first batch u32, batch count u16, reserved u16=0,
min XYZ / max XYZ i16 (six words), two reserved u32=0.
Rooms have exactly one object per source room in original room order, including
empty and alternate rooms; source id is the room index. Meshes contain every
physical mesh exactly once, sorted by source byte offset; source id is that
byte offset. Core's mesh-pointer table, including aliases, resolves to these
source ids. Object bounds enclose the cooked vertices. Empty-object bounds are
zero and batch count is zero. Bounds use the same local coordinates as vertices.

Batch `>6hHHIHH2I`, **32 bytes**:
min XYZ / max XYZ i16, vertex count u16 (1..70), object index u16,
section-relative vertex offset u32, run count u16, reserved u16=0,
two reserved u32=0. Object batches are contiguous and source-ordered by the
partition below. Run count is an audit count; the byte layout does not require
a contiguous run range. The chosen batch-major cook policy makes it contiguous.
Batch bounds enclose exactly its cooked positions and are used for integer
portal/frustum rejection.

Run `>HBBHH4I`, **24 bytes**:
material u16, tile ordinal u8, flags u8, batch index u16, face count u16,
four packed signed-16 S,T words. A word is `(S & 65535)<<16 | (T & 65535)`.
Flags bits 0..1 are class (0 opaque, 1 cut-out, 2 blended, 3 invalid), bit 2
is double-sided, bit 3 is flat, bit 4 is one-cycle-eligible; bits 5..7 are zero.
Material is the TX64 native object descriptor index, or TX64 flat-colour table
index when flat. Flat runs have tile=0 and all four ST words zero.
All flags/material/tile/ST values are constant over a run. A run is nonempty.
Faces are in **stored run-table order**; the first face of run r is the prefix
sum of preceding face counts. No face-pointer field or runtime sorted draw
list is needed. A frame scan advances the face cursor even over rejected runs.

Face `>4BBBH`, **8 bytes**:
four batch-local vertex indices u8, corner map u8, source-corner map u8,
source polygon id u16. Fourth vertex 255 denotes a triangle; otherwise all four
indices are < vertex count. Corner map packs four 2-bit run-ST indices,
low-first. A triangle's fourth map field repeats field zero. Source-corner map
similarly selects TX64 descriptor corners of the **original unsplit polygon**;
its triangle-unused field repeats its first. It survives splitting unchanged.
Source polygon ids are room quads then room triangles, or IR mesh face order,
zero-based within the original object. They survive triangulation/TMEM splitting.
A run can include many source polygons; a source polygon can occupy several runs.
Flat faces have both maps zero. Degenerate repeated-position geometry is retained;
a triangle containing repeated cache indices with different ST is rejected by
the cook, since command 0 installs ST into cached vertices before drawing.

Packed vertex pair: **32 bytes for two vertices**, tiny3d `T3DVertPacked`:

| Offset | Field |
|---:|---|
| 0, 2, 4 | vertex A signed-16 X,Y,Z |
| 6 | unused normal A = 0 |
| 8, 10, 12 | vertex B signed-16 X,Y,Z |
| 14 | unused normal B = 0 |
| 16..19 | cooked RGBA8 A |
| 20..23 | cooked RGBA8 B |
| 24,26,28,30 | unused vertex UV words = 0 |

Odd vertex counts have a final all-zero padding vertex. DMA size is
`32*ceil(vertex_count/2)`, not `16*vertex_count`. Room X/Z are local, room Y is
already source world-height; model matrix adds room X/Z only. Meshes are local
to their source mesh origin. No source vertices, normals or face arrays are
retained separately. Room RGB is capped gain/1.5 times room tint, as below;
alpha is255 and the existing fog path replaces it. Mesh RGB instead retains
untinted baseline gain/2 (before the1.5 cap), so an instance darkness offset
is applied before clipping its gain. Mesh submission converts these bytes
to the room-style gain/1.5 coefficients in fenced scratch, once per vertex.
Cart staging reads baseline bytes from the immutable source, not its uncached
destination, reuses adjacent equal results, and writes each aligned RGBA word
once rather than four uncached byte stores.

## Cook algorithm: no decisions delegated to consumers

1. Obtain UVs using `ir.face_uv`. Quads have polygon order (0,1,2,3) and become
   triangles (0,1,2),(0,2,3). TX64's object descriptor map, not the source id,
   supplies native material indices. Preserve source polygon ids/maps.
2. Share **by original source vertex index within each object**, before
   partitioning; coincident positions/colours at distinct source indices are
   not merged. Use `model_b.py`'s locality partition, cache70: seed smallest
   remaining source polygon id; select an incident fitting polygon by most
   shared vertices, fewest new vertices, smallest source id. If none is
   incident, try smallest remaining id; close the batch if it does not fit.
   Keep an unsplit polygon together. Assign local indices in ascending source
   vertex-index order. UV never participates in the sharing key.
3. Use TX64 descriptor-relative texel-centre coordinates. RDP point sampling
   floors its input, so encode **S=round(32*u)+16** and
   **T=round(32*(v-tile_start_row))+16**. This is `n64-tex.md`'s
   centre-to-edge-origin conversion, not a source-PC fractional byte. TX64
   cluster-local x and exclusive right end must both be even; odd endpoints
   are cook errors, not a parity-bias/clamp workaround. The measured33538
   object descriptors contain zero such cases. Load from even x, with render
   tile SL/TL=0 and clamp to the loaded rectangle; ST already includes the
   half-texel, exactly once. Compose inverse descriptor raw-slot permutation
   with `face.texture_corners`: PSX common-order quads normally map identity;
   triangles use the TX64 triangle map in that native-corner domain.
4. TMEM splits come exclusively from TX64's split lines. A line before row y
   is the continuous centre-coordinate line v=y-1/2. Split each original
   triangle against each covered tile strip in rational arithmetic, interpolating
   positions, gain and UVs. Discard empty/zero-area pieces, preserve winding.
   A 3/4-corner clipped polygon is retained as one compact triangle/quad; a larger
   polygon is a fan of triangles. Assign its at-most-four exact UV pairs to its
   run ST table in polygon order; maps are then identity. Merge equal tables.
   Generated positions round to nearest signed integer (ties away from zero);
   ST and colour round once at final encoding, also ties away from zero. Do not
   round intersections before clipping the next edge. Reject an out-of-range
   result instead of saturating position/ST. Within a split-source polygon
   only, deduplicate generated vertices by encoded position and colour.
   Append its dedicated batches within the original object's batch range;
   retain the original partition's vertex payload/bounds, counting unused
   slots. Partition further at70 with the same rule if required.
5. Within each batch group by material, tile, class, sidedness and ST table;
   retain original polygon/piece order for ties. Set one-cycle-eligible iff
   every used source/interpolated untinted gain is <=1. Mesh eligibility is
   baseline-only; an instance offset can invalidate it. Flat palette sort key
   is65535 (after actual palette ids); flat source id breaks material ties.
   Order runs by the batch-major key above. Faces and vertex payloads remain
   indexed, never relocated by the renderer.
6. No static-instance duplication in RG64/MG64. Core owns room portals, sectors,
   flags, alternate indices, static-instance positions/angles/darkness and static
   definitions. Draw definitions with flags&2 only, resolve definition mesh-pointer
   aliases, quarter-turn quantize angle, rotate about Y and translate. All pieces
   retain their logical room id. Alternate-room selection swaps reciprocal mates
   once at flip1 without changing logical room identity. Selected room flags
   choose water tint; geometry for both alternatives stays resident. Room sprites
   are not polygons in these sections; posed M1/reference scenes exclude sprites.

## Lighting, combiner and blender

Main's revised2026-11-17 model caps gain at1.5 to avoid the RDP's9-bit wrap.
Both sources start with `gain=clamp((8191-d)/4096,0,2)`; PSX
d=8191-(raw<<5), so128 is neutral. PC d is original darkness; its32x256
light-map LUT is not native data or the comparison model. Normals-only mesh
vertices use darkness4096, not a normal shader; lit meshes use their source
darkness. No dynamic normal lighting is claimed in M1.

Room RGB coefficient is
`c=clamp(round(256*min(gain,1.5)*tint/1.5),0,255)` per channel.
Water tint is (1/2,4/5,4/5), baked before that one rounding. Dry tint is1.
Mesh baseline bytes are `clamp(round(128*gain),0,255)`, untinted and not yet
capped at1.5. Reconstruct baseline gain as code/128, subtract signed
`light_offset/4096`, clamp to[0,1.5], tint, then encode room-style RGB.
Rooms use offset0; statics use `intensity-4096`; live drawable meshes use
the game's normalized signed pre-fog offset: dynamic darkness minus4096.
The source static-lighting path already produces that delta. The DTO field
is `light_offset`; raw dynamic `gLightAmbient` is NOT a delta.
Subtract in64 bits before clamping, preserving overbright baseline information.
The limited8-bit coefficient precision is explicit, common to both back ends.

Use the existing tiny3d ADD lighting mode with ambientRGBA zero and light
count zero: this is exact unlit vertexRGBA identity. NO_LIGHT is deliberately
unset so zero ambient is loaded; exposure1 (128), vertexFX NONE, UV offset0
and alpha-to-tile off. Unlike the stock MUL32767 path, this does not decrement
codes above128 or turn constant mesh alpha255 into254. No new ucode
instructions/storage are added. Shade/fog interpolate screen-affinely;
ST alone is perspective-correct. Default **two-cycle** combiner:

```
cycle0 RGB=(TEX0-0)*SHADE+0; alpha=(0-0)*0+TEX0_ALPHA
cycle1 RGB=(COMBINED-0)*PRIM+COMBINED; alpha=(0-0)*0+SHADE_ALPHA
PRIM RGB=(128,128,128), never white
```

RDP arithmetic is `(((a-b)*c+128)>>8)+d`; the final9-bit special clamp
saturates256..383 but wraps384..511 to zero. Exhaustive65536 texel/shade
byte pairs give cycle0 in0..254 and cycle1 in0..381 with PRIM128: **zero
wrap cases**. PRIM255 was wrong and is removed. Evidence:
`contract-hardware.json`, source facts in ares's
`parallel-rdp/shaders/{combiner.h:28-46,clamping.h:40-59,shading.h:281-357}`.
Colour-key comparison in two-cycle mode uses **cycle0** TEX0_ALPHA (0/255);
cycle1 alpha independently carries current-pixel fog visibility. Alpha
compare threshold1 rejects zero in ALL texture classes, including opaque;
PSX8000 remains visible black. Flat uses ENV and cycle0 alpha255, no upload.

Blender cycle0 is identity `(IN_RGB,ZERO,IN_RGB,INV_MUX_ALPHA)`;
cycle1 is `(CYCLE1_RGB,IN_ALPHA,black,INV_MUX_ALPHA)`, forced blending,
no framebuffer-colour read. With final alpha255, the RDP's explicit opaque
bypass preserves RGB exactly. Partial fog uses its native5-bit coefficients;
alpha0 produces black. IN_ALPHA avoids the hardware next-pixel bug of
second-cycle SHADE_ALPHA. The model remains
`clamp(texel*min(gain,1.5)*tint)*(1-f)`, with clamp before fog.

The former first-cycle fog stage darkened even unfogged pixels by31/32.
Moving fog to the final stage and using identity vertex colour removes
that avoidable bias. PC start meanY improved76.961→80.335 (reference81.415);
PC statics59.263→62.193 (reference64.219). Remaining mean deficits1.080/2.025
RGB8Y are close to RGB555-only predictions1.003/1.841; this is quantization,
not a changed gain factor or earlier fog start. PRIM128 is exactly
1+128/256=1.5; odd intermediate products round upward by0.5, not downward.
Room f is clamped per vertex and visibility interpolated screen-affinely,
not perspective-correct darkness/fog. Mesh f is constant at instance-origin Z.
Use the native uniforms below; no new shader instructions or DMEM.
No texture LOD, AA, RGB/alpha dither or semitransparency in M1.

Two-cycle hardware fill throughput is half one-cycle's; ares has no emulated
RDP fill/TMEM-load cost. Gain<=1 runs could use recoded coefficient gain and
TEX0*SHADE without the1.5 multiplier; the flag preserves eligibility. This
is not enabled in M1: native quantization differs and fog itself normally
requires two-cycle mode. Do not claim a free hardware speedup from emulator
timing. Full4096B DMEM and56B main-IMEM free remain the ucode constraints.

## Matrices, depth and exact fog uniforms

The backend composes `P * V * M` on the CPU in signed64-bit fixed-point,
subtracting integer camera eye from integer instance origin BEFORE any
narrowing. Encode one final row-major s16.16 MVP, nearest/ties-away, into
tiny3d's matrix layout. Set tiny3d projection to identity and load this MVP
as its model matrix: never multiply large world translations on the RSP.
For view coordinates right X, down Y, forward Z, the homogeneous rows are

```
clipX = (1/32)*(8/5)*X
clipY = -(1/32)*(32/15)*Y
clipZ = (1/32)*((161/159)*Z -20480/159)
clipW = (1/32)*Z
```

The Y sign explicitly compensates tiny3d's negative screen-Y scale. This
projection places near64 at clipZ=-W and far10240 at clipZ=+W.
Integer model/view rotations are Q14; source positions stay unscaled s16.
At bind, compute world bounds of all room vertices and static origins
expanded by the maximum physical-mesh L1 vertex radius (covers any quarter
turn). At bind let D=sum(max-min) over XYZ and require
`3*D+256 <=24576*32`. Per frame let D be
`sum(max(abs(eye-min),abs(eye-max)))`, and A the maximum absolute camera
basis coefficient divided by16384. Require `3*ceil(A*D)+256 <=24576*32`.
This permits a live follow camera outside the box without wrapping. Q14
basis components must fit signed16 bits; the camera basis cannot be zero.
For a live mesh let R be its local-bounds L1 radius and B its maximum
absolute basis coefficient divided by16384. Also require
`3*ceil(A*(L1(origin-eye)+ceil(3*B*R)))+256 <=24576*32`.
These conservative bounds cover dot products, focal scale<=32/15 and rounding
below32767. Failure is explicit, never silent rescaling. All43 bind-time
inputs satisfy the original bound; worst LEVEL1 D=215016, bound20165.75.

Do not use viewport defaults for screen/W normalization. Program the existing
SCREEN_SIZE command explicitly (viewport320x240, guard band1):
screen offset `(320<<17)|480`, scale `(640<<16)|uint16(-480)`,
depth/W `(16384<<16)|128`. NORM_SCALE_W is then
`{65535,65535,65535,128}`: **only W** is normalized by1/512; XYZ remain
65535/65536. Screen/depth compensation cancels the W factor without the
default viewport's depth-scale overflow. At near64 the normalized W is
1/256, so reciprocal256 fits; far10240 gives W=0.625, reciprocal1.6.

Fog sees clipZ after its65535/65536 XYZ multiplier, NOT W normalization.
For the encoded axial projection constants
`Az=2074/65536`, `Bz=-263793/65536`, set existing fog words to
offset **-16726160 Q16** (two big-endian halfwords),
limit **32767**, multiplier **512**. This is
offset=round(-(Az*8192+Bz)*65535), multiplier=
ceil(32767/floor(Az*2048*65535/65536)).
The shader takes integer part of (clipZ+offset), saturating-multiplies by512,
subtracts from32767, doubles, and stores the high byte as visibility.
At the axial start/end this gives255/0; approximately64 depth steps span the
ramp. Host models that integer quantization and the encoded MVP, not an
invented continuous hardware fog path. For meshes disable varying fog and
encode that same origin-derived visibility in staged vertex alpha.

Near clipping is enabled. **No far clipping**: reject a triangle iff all its
vertices are beyond10240; draw straddling triangles whole. Per-vertex depth
past the far plane saturates and fog visibility clamps to zero; interpolate
native attributes as above. Host/reference use the same rule; comparison
map pixels beyond far are don't-care. This is a deliberate M1 choice, not
a claim that tiny3d's near clipper also clips the far plane.

Front faces are clockwise in the Y-down image (positive signed screen area).
The production +Z/Y-compensated path needs `T3D_FLAG_CULL_FRONT` (API mask1)
to keep these faces; the earlier mask0/CULL_BACK mapping was wrong. The
LEVEL1 start scene exposed it by rejecting the floor; mask1 restores it.
Neither flag is set for double-sided runs. No mirrored instances are used.

## Integer visibility and batch tests

Portal traversal is the reference fixed-point rectangle algorithm: start the
camera room at[0,0,320,240), reject back-facing portals by integer normal dot,
reject all-Z<=0/all-Z>far, project positive-Z endpoints, expand bounds to
viewport edges on zero-plane crossings, intersect parent clip and union
destination clip. Round projected minima down and maxima up. A bounded queue
has one slot per room and a queued bit, not a fixed visible-list cap; requeue
only on clip growth, until fixed point. Select reciprocal alternate mates
without changing logical indices before traversal.

For each batch reject iff all8 AABB corners are outside ANY inward plane:
Z-64;10240-Z;256X+(160-l)Z;(r-160)Z-256X;
256Y+(120-t)Z;(b-120)Z-256Y, with(l,t,r,b)the owning portal union.
Compute these exact corner maxima as transformed minimum corner plus each
positive transformed edge contribution. Model products retain their individual
Q14 truncation before the linear view transform; this avoids transforming all
eight corners without changing any rejection decision.
Use signed64-bit products and conservative equality (zero is inside).
This is both the frustum AND portal test, not a projected-bounds shortcut
that divides behind-camera corners. Nonrejected batches still use the
portal scissor for final fragment restriction. Static batch bounds use
their quarter-turn transform and the same owning-room rectangle.

Live entity drawables follow rooms/statics through the same object-span and
batch path, with one local-to-world matrix per visible bone; reachable item
rooms select them, but their cross-room geometry uses the viewport rectangle.
The direct game camera basis is copied, not normalized from its target.
Core mesh-pointer slots (including aliases) resolve to MG64 source ids.
Visible objects/bones/sprites receive consecutive GPU slots only when their
first batch is submitted; culled level instances reserve no matrix slots.
The256-visible-matrix limit fails explicitly before an overflowing submit.
Entity transforms and signed lighting need no geometry copy or allocation.
Scenes `m2-level1-idle` (60 idle ticks) and `m2-level1-run` (the first75 ticks
of the150-tick FORWARD fixture) use the retail start and live game camera,
without placement or camera overrides. Both run through `scene.sh` on host
and4MiB ares; `game-render:` reports actual per-frame work and completion time.

Source-derived LEVEL6 sightline witness: the portal test leaves the SAME
27 room batches/2407 triangles/278 runs as frustum-only testing. This is not
a claimed culling reduction: those AABBs are too broad. Q14 corner tests
with outward-rounded reference portal unions reproduce the same batch set.
Statics are excluded from this witness; engine scene counts are acceptance
evidence. The triangle budget is set by step2, not assumed to be2500.

## GPU memory: fixed allocations, not library-heap leakage

Current rspq boot payload, measured in bakeoff and confirmed by pinned sources:
four 2,048-byte RSP queue buffers = 8,192; two 65,536-byte RDP dynamic buffers =
131,072; total **139,264 bytes**. This already exceeds the old 96 KiB row.
Do not shrink RDP buffers on speculation: their size is also compiled into
both tiny3d overlays. A shrink requires a single consistent SDK rebuild and
production-scene evidence of buffer turnover/stall cost.

Main approved168KiB for the base GPU plan, then up to **232KiB =237568B**
including the raw-RDP frame records (2026-11-17); Skeleton owns the row update.
Boot in `sys_heap_boot_pool_begin(TR64_MEM_gpu)` scope: `rdpq_init()` first,
then `t3d_init(matrixStackSize=2)` reserving overlay slot1 **before mixer or
any other dynamic overlay**, one buffered viewport and one fixed workspace.
End scope before `sys_heap_freeze`. Do not let audio initialization steal slot1.

| Allocation | Bytes |
|---|---:|
| rspq/rdpq six buffers | 139264 |
| tiny3d matrix stack, two entries | 128 |
| viewport camera/projection, one buffered set | 128 |
| 256 frame-visible instance matrices, 64 each | 16384 |
| 8 fenced mesh-colour scratch slots, 70 vertices each | 8960 |
| raw-RDP frame records (included in same workspace) | 65536 |
| **payload sum** | **230400** |

Workspace matrices stay valid through GPU completion; cache one per visible
room/instance, not one per material switch. Measured whole-level room+static
instance maximum is 187 (PC LEVEL10A; both-source sweep <=187), below 256.
Scratch slots are reused only after their vertex-load syncpoint; never overwrite
memory that an asynchronous RSP DMA still reads. Room vertices use resident
payload directly. Render visibility/work-queue scratch is charged to `state`,
not secretly to library malloc. Skeleton's align16 scoped allocator uses
`round_up(payload+16,16)` bytes per allocation. Nine allocations (six buffers,
stack, viewport, one combined workspace) add144 bytes: planned **230544B**,
leaving **7024B** at232KiB. Owner descriptors and bitmap live in IMAGE
(bitmap1856B for232KiB).
Measured first engine boot: **230608B,11 allocations**, zero allocations
after freeze, leaving6960B. The64B beyond this source-derived plan is included
in the measured GPU peak, not charged to libheap or omitted. Evidence:
`$TR64_WORK/builds/Render/picture/scenes/ares/m1-level1-start/engine.log`.
Do not mix bump allocations with the scoped GPU slot heap. Both back ends
enforce the row. Linked ucode/data live in IMAGE; RSP DMEM/IMEM are not extra
RDRAM allocations. No recorded blocks, model/skeleton loaders, or per-frame
allocation is used.

## LEVEL6 room 44 worked example and whole-level estimates

Input PC `level6.phd`, SHA256
`f7fa9eac88678dd48b37651c02ef1b14d8bbea0d3bbcf9f39c515ff82b6157f7`.
Measured through the existing source reader/locality rule: 487 source vertices,
427 quads +17 triangles =444 source polygons, 871 triangles; no TMEM split face.
The nine batches are:

| Batch | Vertices | Pair-padded vertices | Polygons | Triangles | Runs |
|---:|---:|---:|---:|---:|---:|
|0|69|70|46|91|18|
|1|69|70|52|102|18|
|2|70|70|47|92|17|
|3|70|70|52|100|19|
|4|70|70|45|88|18|
|5|70|70|50|99|16|
|6|70|70|49|95|20|
|7|70|70|51|101|18|
|8|63|64|52|103|15|
|sum|621|624|444|871|159|

Exact contribution excluding shared section header: vertices 624*16=9,984;
faces 444*8=3,552; runs 159*24=3,816; batches 9*32=288; object 32;
**17,672 bytes**. A section containing only this room is **17,736 bytes**.
Expanded triangle commands are 871*20=**17,420 bytes/frame** when all batches
survive culling. Batch-major gives **9 vertex loads /624 padded vertices /
9,984 DMA bytes,159 image uploads and144 palette loads** for this isolated
room, with a cold historical descriptor-ID cache and palette/descriptor order inside each batch.
There are22 distinct textures and18 distinct palettes, but those are not frame
upload counts. Global material-major would instead give158 vertex loads /
10,976 vertices /175,616 DMA bytes,22 uploads /18 palette loads. It saves137
image uploads but adds10,352 vertex transforms and165,632 DMA bytes. The
upload-bound bakeoff does not isolate their RSP cost; it cannot justify that
trade. The chosen default has known minimal vertex work, and step2 measures
the remaining upload cost through the engine scenes.

An all-43-input source sweep with this 24-byte run/8-byte face format gives
worst room section **370,056 bytes**, LEVEL6 PC, against 393,216: **23,160 free**.
No room polygon in either source references a TMEM-split descriptor. Sprites are
excluded, as in the posed renderer. Worst pre-split physical mesh section is
LEVEL7B PC **192,712 bytes** before splits in this pre-writer estimate.
Its ten two-tile split quads contributed at most280 extra bytes each in that
model (195,512 total). These are source-model estimates, not current writer
measurements; the former fixed geometry rows have been replaced by the shared
level arena. Native writer totals and per-section peaks supersede estimates.
The first PC LEVEL1 engine load measured rooms294160B and meshes118616B;
current complete-level cooks check the aggregate Core/TX64/RG64/MG64 layout.

The old M0 estimates (312.2 KiB rooms,146.3 KiB meshes) omitted these run ST
headers and are not the totals for this contract. The host auxiliary provenance
uses each eight-byte face's source polygon id and source-corner map; there is
no omitted cart-resident per-triangle metadata.

Source-derived measurements and all43 input hashes:
`$TR64_WORK/evidence/m1-render/contract-sizing.json`. They were computed
with Python Eval importing existing `src_pc`, `src_psx` and the locality
selection rule of `model_b`; cached TX64 manifests supplied descriptor splits
and palettes. No native writer, engine timing or picture gate was run.

## Queue expansion and HostRaster observables

Command ABI 1 is overlay slot1, command0, cached vertex base0x408, stride36:
`[0x10000000|p(a), p(b)<<16|p(c), ST(a), ST(b), ST(c)]` with p(i)=0x408+36*i.
Tiny3d fork registers that slot statically. Precompute the 70 p(i) values once;
resolve a run's four ST words once. Expand triangles without floating point,
coordinate arithmetic, material lookup, source UV lookup or allocation. Quad
expansion uses the same map with corners0,1,2 then0,2,3. RSP autosync resources
are marked once per run; reserve/copy no more than available queue space, never
split one command across queue handoff. Reject the obsolete eight-byte stock
triangle API at the fork cutover. A compact quad command in RSP is not assumed:
only56 IMEM bytes remain, and a new command table would overflow full DMEM.

**[EST] expansion:**40-60 MIPS instructions/triangle PLUS five uncached32-bit
queue stores. Do not equate store issue with memory completion: allow
100-150 CPU cycles/triangle including those stores, excluding queue waits.
At2407 triangles this is2.57-3.85ms at93.75MHz, not a measured frame.
Outside expansion, work scales as portal tests/clip growth,6 plane extrema
per batch, one matrix per visible instance, and staged vertices for
meshes only. Budgetary operation estimates:200-400 scalar operations per
portal test,400-800 per AABB test,150-30064-bit multiply/add operations per
matrix,20-40 scalar operations per staged vertex. These are not cycle claims;
step2 reports each phase's actual CPU time and the observed work counts.
`encode_us` isolates cart MVP arithmetic before matrix copy/queueing; cull
reports include tested/rejected batch and scanned-run counts.
Hot phase timers accumulate unsigned32-bit raw platform ticks and convert each
aggregate once per frame. On cart this reads CP0 COUNT directly, avoiding a
64-bit clock extension/division at every run boundary. Published fields remain
microseconds; frame/deadline clocks retain their rebase/wrap contract.
Backbuffer acquisition uses `display_try_get` and the SDK's VI wait plus its
200ms RSP watchdog. The VI wait is a no-op without the SDK thread kernel,
so retry count is not a service clock. Resident `tr64_snd_pump` is limited by
an unsigned raw-tick8ms deadline while blocked, reset after each service;
successful acquisition also refreshes audio without duplicating a service
already performed on that retry. No kernel initialization, IRQ handler,
allocation, code-unit transition or mid-render audio call is added.
`acquire_us` is the gross interval; `acquire_audio_us` sums disjoint audio
service spans on the same microsecond clock. `pacing_us` is their difference,
so subtracting pacing from the caller's completion span retains audio work.

An explicit performance-only probe defining the existing weak
`tr64_scene_test_no_hash=1` also suppresses verbose per-frame renderer UART
diagnostics after frame completion. One `render-acquire` line retains gross
acquisition, its audio service and GPU-frame completion duration from the
existing counts; it executes after the captured completion stamp, not in the
renderer hot path. All drawing, completion stamps and count getters remain
live; ordinary evidence scenes retain their diagnostics.

Never switch to the rdpq overlay inside the3D pass for texture, palette,
scissor, flat colour or combiner changes. A normal round trip saves/loads
about14KiB of overlay state/code (~28us in ares), defeating a cheap CPU
passthrough writer. Instead first call **t3d_tri_sync** to drain triangles
and restore the main tiny3d code if near clipping swapped it; emit the
raw8-byte RDP words into an aligned, uncached65536B frame record arena;
submit its immutable span through **rdpq_exec/RSPQ_CMD_RDP_SET_BUFFER**.
This is a core-queue command and does not switch overlays. It is never
called in an rspq recorded block (rdpq.c:539-550). No generic per-frame
texture loader or per-level descriptor-command cache is used.

Write8 RDP commands/64B for an image: SyncPipe, SyncTile, SetTextureImage,
SetTile load, SyncLoad, LoadTile, SetTile render, SetTileSize. CI4 data is
loaded as **I8 at half horizontal texel coordinates**, using the cluster's
tight source row-byte count as I8 image width, TMEM pitch align8(width/2);
render tile is CI4, SL/TL0, clamp width/height. All source horizontal endpoints
are even. Use all16CI4 TLUT slots: palette id maps to slot(id&15), with a
full-id tag per slot (collisions reload, never alias). Slot s starts at TMEM
word256+16s; the render tile selects s in its palette field. Reset tags each
frame. This uses the existing upper2048B TMEM and64B of static tags, no pool
allocation. A miss writes6/48B: SyncPipe, SyncTile, SetTextureImage RGBA16,
SetTile TLUT, SyncLoad, LoadTLUT16 entries. A palette-slot-only change with
an unchanged image writes3/24B: SyncPipe, SyncTile, SetTile render.
When palette and image both change, reserve their112B together and enqueue
one core-buffer span; retain all14 RDP commands/syncs, not two core-buffer commands.
Retain palette/image caches independently. Image cache identity is its native
payload offset, source row bytes, loaded origin and extent, not logical
descriptor id: UV/corner-map variants often upload identical bytes. Palette
selection and logical texture provenance remain independent of image reuse.
A changed scissor is one8B record;
flat ENV and textured/flat combiner switch are at most16B per run. Other
RDP modes are fixed at pass boundaries; all classes share M1 key/Z policy.
Sync before matrix/vertex-cache replacement too, not just material changes.

Records are append-only within the frame, never reused at an RSP syncpoint:
only **frame_end's full RSP+RDP wait** proves the RDP has consumed them.
Reset cursor only after that completion and before the next frame. Reserve
the whole change's bytes before writing/publishing any of it. Overflow
increments `rdp_record_overflows`, reports used/requested bytes and fails
closed through sys_error; it never wraps, overwrites in-flight words,
silently drops geometry, or allocates. Row usage includes all65536B even
if a particular frame emits less. RDPQ CPU mode/autosync trackers do not
observe these raw words: no other-overlay mode/scissor/upload API may
interleave; establish complete raw state at the3D boundary and reset
standard RDPQ state after full completion before other platform drawing.

Source-derived all26 M1 scene/source bounds, including statics: record
bytes <=128*run_count +8*visible_rooms +160 (initial/final state records).
Worst is LEVEL6 sightline:278 runs,11 rooms, no static batch contribution,
**35832B**, within65536. Isolated room44 image/palette words are
159*64+144*48=**17088B**, plus scissor/boundary records. These are conservative
native-data bounds, not engine-frame peaks; the runtime counts actual bytes
and fails any overflow. Evidence `contract-hardware.json` records all poses.

**[EST]** image construction60-100 CPU instructions, palette40-70, plus
16/12 uncached32-bit record stores and the core-buffer enqueue. Step2 names
generated assembly and measures CPU setup, RSP command/DMA work and total
frame time in these same scenes. Ares cannot measure hardware RDP fill or
TMEM-load latency; report that ceiling explicitly, not invented per-upload
RDP time. Revisit order/buffer size only with production-scene evidence.

HostRaster consumes exactly the same view/projection, transforms, rectangle,
material state, packed vertex pairs, run ST table and compact faces. It must
model point sampling/clamp, stated winding, near clipping/far rejection,
perspective ST but screen-affine shade/fog, RGB555 output, encoded matrix
and fog quantization above. It must not reload source IR or use a second
geometry path. Logical room is the submission id; source texture id comes
from TX64 (-2 flat). Source-relative continuous texel is ST/32 minus0.5,
plus tile row, minus minima of the original source-corner-map selections.
Track contributing source polygons by `(logical room, instance id, source
polygon id)`, not emitted triangles; record any non-key depth-winning write,
even if later overwritten. Coverage/room/texture/depth/texel maps and capture
metadata are exactly the RefRender comparison ABI. Maps are host-test output,
not an uncharged resident game resource. RGB comparison on cart has no maps.

The host submission backend is `plat/host/gpu.c`: it validates indices and
expands each triangle to the same five command words, preserving face ids
and source-corner maps. `gpu-submit.h` exposes synchronous begin/triangle/end
hooks for the rasterizer; without it, submission counts are real but no image
is presented and capture returns ENOTSUP. Both backends report exact triangle
command and RDP-record bytes; host timings are CPU time, not emulated RSP time.
Host `raster-fill` reports post-cull triangles, covered sample centres before
Z, Z-pass samples, alpha-surviving writes and final covered pixels. These are
engine-raster work counts, not hardware RDP cycles; logging is outside timing.
The GPU pool includes the source-derived cart boot model, including SP/DP
callback-node allocations, rather than free host RAM.

Geo must return section bytes plus manifest with exact breakdown, per-object
and per-batch counts, per-run upload keys, source ids, split expansion, source
hash, TX64 section hash, cook hash and shared level budget; all bytes are recomputable.
HostRaster must use native data and the backend calls in `gpu.h`; capture
export is specified there, not left as a new scene-entry path. Validation,
portal visibility and batch culling are common `engine/render` work, not tasks
for either writer or rasterizer.

## Runtime sprites

Core ABI2 supplies room-sprite positions, Q12 gain and combined TX64 ids.
Posed scenes may request initial pickup sprites with `rest_items` (maximum
visible placements;0 disables). Room sprites always follow room visibility.
Pickup types resolve once at bind through Core sprite sequences; frame0 is
their first native descriptor. No source sprite index is used as a native id.

The billboard uses view-space source bounds (OpenLara2333adef
`src/platform/32x/render.cpp:1113-1158`) and the native256-focal projection.
One matrix per sprite composes P*M; local coordinates use twice the texel
centre coordinate, preserving split boundaries at `row-0.5` exactly. UVs,
cut-out/Z policy, water tint, capped gain and RSP fog use the ordinary path.
Transient64-byte vertex packets copy into the existing fenced GPU scratch;
compact faces/ST are consumed synchronously. No GPU allocation is added.
The state index costs2 bytes per Core item. Native descriptors can be objects
or sprites; sprite ids start at the TX64 object count.

`m3-level1-sprites` shows real room sprites and a pickup. Both retail LEVEL6
sources have zero room sprites; `m3-level6-sprites` shows actual pickups.
Pose eyes must occupy an open source sector, not merely the room AABB;
`render-visible:` records the actual logical room list for connectivity checks.
The posed reference excludes sprites, so projection is compared with the
above OpenLara corner rule (and MODEHW integer rounding separately), not
claimed as a sprite reference-image gate.

`tools/cook/pack_level.py` assembles the existing Core/TX64/RG64/MG64 writers.
Use `--source pc|psx --input LEVEL --out PACK`; optional `--textures DIR`
accepts an explicit immutable `section.bin`/`manifest.json` pair after source,
content-hash and independent descriptor-map validation. It does not claim
an old broad tool-cache key matches new code. `--cache DIR` reuses the pinned
host LZ4 compressor. Source/Core ABI changes require recooking the full pack.

## Loading-code lifetime

Geometry validation, bind-time span/material checks, and workspace sizing are
text/rodata in the shared `load` unit (render operations0x100..0x102).
Resident facades dispatch each call through `code_call`; Core's scene loader
holds the outer load scope through remount, validation, binding and the initial
frame, then unloads. Bind uses internal cold helpers, never nested dispatch.
Runtime descriptors retain only native-pool spans, Core and state-workspace
pointers. Projection, visibility, batch drawing, sprites and shared hot layout
accessors stay resident; no rendering call after the loading scope needs cold
code or cold read-only storage.

## Item snapshots and animated content

The caller allocates64drawables (4352bytes) once after level start/place,
not a world-sized list. `game_view_camera` snapshots only camera/flip/clock.
After portal traversal, the renderer pulls `game_view_room` chunks for visible
logical rooms: cursor0 starts, UINT32_MAX ends, and item groups never straddle
chunks. A group larger than capacity fails explicitly without writes or cursor
advance. Each chunk is consumed synchronously before reuse; invisible rooms
do no bone work. Pose generation is included in frame and `view_us` timing.
Packets retain stable item id plus u8 bone, mesh/sprite variants. One bounded
256-bone work array sorts each item's parts by first material, then bone;
native mesh runs remain batch-major. Counts distinguish pulls, drawables,
submitted items, mesh parts, sprite tiles and matrix slots.

Portal rectangles select reachable item rooms, not the final scissor of an
articulated body: frame offsets and bones can cross a room boundary before
the item's root changes rooms. Draw such items against the full viewport,
with normal frustum/batch culling and opaque room Z occlusion. Cropping bones
to their root room's portal rectangle hides Lara during an upward climb.

Explicit fixtures may select `rest_models` (Core item indices), `live_item`
and `posed_game_camera`. Prepare once after Game start/chunk allocation:
rest-pose scratch is the actual largest selected model, not items-times-max-bones.
The rest query is Game's fixed bone path; no animation logic is copied here.
These settings are disabled by shipping `tr64_render_scene_set_live(1)`, which
binds any campaign without a compiled pose. `tr64_render_scene_release` clears
all borrowed level/state pointers and unbinds GPU textures at a completed-frame
boundary before memory reset.

Texture animation consumes cumulative simulation rotations, never render count.
Cold binding composes source-order groups and encodes permutation cycles in
state; hot updates advance their phases using unsigned step deltas (including
wrap/skip). TX64 is immutable. Logical material and selected content are separate
GPU fields, preserving logical capture identity while loading the current frame.
Groups must have equal dimensions, relative UVs, flags, triangle map and split
boundaries; incompatible metadata fails before drawing. The current74 retail
groups satisfy this measured invariant. Posed water snapshots exercise steps
0,1,4 independently of camera motion; live views supply Game's simulation step.

## Cutscene views and pictures that look wrong but are not (measured)

Checked on the host with the 21-level pack, `--game-live`, no input, against
`test/ref` rendering the same eye and basis from the original `.phd`
(`$TR64_WORK/evidence/m6/render/`).

- **Olive disc in LEVEL10C, CUT3, CUT4.** It is the intact shell of a mutant
  egg: item type 181 (LEVEL10C item 103) or 163 (CUT3 item 0, CUT4 item 0),
  joint 24 (small egg) or joints 9..23 (big egg after hatching starts:
  `visibleMask` `0x00FFFE00`), all faces the flat palette colour 49, vertices
  carrying normals only. The fixed game lights an object with one scalar
  (`gLightAmbient`, `src/fixed/draw.h:81-140`), no normal shading, and the item
  is fogged by its distance (fog byte 139 at the probed pixel), so a sphere
  draws as a uniform dark olive disc: the same as the reference model, a
  declared consequence of the scalar lighting above, not a missing texture. A
  LEVEL10C big egg starts hatching at tick 1 (`MutantEggBig::update` does no
  trigger or distance test); the shell is gone by tick 90.
- **Black areas in CUT3/CUT4.** The cutscene sets contain no geometry there:
  the reference renders the same black with portal traversal and also with
  every room drawn without portal culling (CUT4). In CUT3 the all-rooms picture
  adds dim geometry only from the alternate rooms 6 and 9; the cutscene never
  flips (`flipped` stays 0 for 300 ticks and the fixed game's `initCinematic`
  has `level.flip()` commented out).
- **Root room.** A cutscene's root room is the cinematic camera's `view.room`
  (set once from the cut item, `CinematicObject`, `src/fixed/object.h:819`); it
  does not follow the eye. Traversal tests portals against the eye
  (`np.dot(normal, v0-eye) < 0`), the item clip rectangles come from the same
  traversal, and cinematic items stay in their own room, so items selected by
  room clip are the same as the reference's. Checked on CUT4 ticks 20, 100,
  200, 250 and CUT3 ticks 40, 80, 250: no room is missing; the pixels exactly
  black in ours and lit in the reference are at most 1,535 of 76,800 (2%), near
  black in the reference and on item or fog boundaries.

## Sweep frames without Lara or with smeared walls (measured, faithful)

Host replays of the sweep scripts (`test/run/sweep.py` `draw_script`, mask hashes
checked equal to the sweep's JSON) against `test/ref` at the game's eye and
basis; pictures in `$TR64_WORK/evidence/m6/render/`.

- **LEVEL8C run 3, tick 600.** Lara is rolling (oracle state 45, anim 146, pos
  `28161,2040,48112`), the camera `27903,1067,47934` in room 43 aims at the
  follow target `28161,1208,48112`, computed from the standing bounding box
  (feet minus 576) not from the tumbling pose. Projecting every one of her 348
  mesh vertices through the game's bone matrices and the 256-pixel focal
  camera puts all of them below the frame (smallest row 255 of 240); the
  reference shows the same wall, whose depth is about 1,020, behind her. The
  renderer draws her 15 parts and rejects each batch as outside the view
  (`entities=0`). Her pose is oracle-equal, so the oracle's picture has her
  just below the frame too.
- **LEVEL8C run 1, tick 600.** The camera `9818,-1220,50937` is 86 units
  horizontally from Lara (`9803,-365,51022`) and 855 above: it looks down onto
  her head. Of her vertices 31 fall in the frame (head 20, torso 11) and are
  drawn, matching the hair and the gun in the picture.
- **LEVEL6 run 3, LEVEL10A run 3, LEVEL10B run 3.** Smeared walls and the
  swirled black and white rock are the level's own textures stretched
  point-sampled over a large polygon at a close oblique angle; the reference at
  the same camera shows the same picture (97.6%, 95.2% and 94.6% of the pixels
  within 24 of 255 per channel; the rest is Lara, fog and shading rounding).
