# Native CI4 texture section TX64, version 1

Layout facts: OpenLara 2333adef `src/format.h:1652,6031-6037,6093-6104,
6217-6235,6521-6541`; IR `face_uv`; M0 model C. This is a new format,
not a prior N64 port. D10 selects CI4 only, with no CI8 fallback.

All integers are big-endian. Offsets are section-relative u32, table/payload
starts aligned to eight bytes, records have the exact strides below (not
individually rounded). The **section base itself must be eight-byte aligned**.
The section is used in place without relocation.
All bytes, including metadata/alignment, contribute to the shared `level`
arena in `pools.def`. There is no independent texture cap. The complete pack
cook checks Core+TX64+RG64+MG64 with16-byte section reservation alignment.
The texture manifest's `level_budget_bytes` is an absolute arena bound only,
not a private texture allocation or a claim that the complete level fits.

## Residency and descriptor references

Only referenced textures get descriptors: room/entity/static faces, room
sprites and every sprite-sequence member. An animation group is active if
it intersects referenced objects; all its members become referenced. This
closure repeats for intersecting groups. Inactive groups allocate nothing.
Triangle-only bounds use three source slots, not a phantom fourth. Animated
members otherwise lacking a face use four slots. Flat material references
allocate colours, not texture descriptors.

Object descriptors precede sprites. Each table is sorted by original source
index. The manifest's `object_descriptor_map` and `sprite_descriptor_map`
map source indices (JSON string keys) to combined native descriptor ids;
absence means unreferenced. `descriptor_index` rejects an absent reference.
Geometry and Core store those native descriptor ids, not source texture
indices. Native source-id fields remain sorted provenance identifiers;
source-to-native resolution happens in the cook, not by a runtime source-id
remap or binary search. TX64's active animation groups contain native
object descriptor ids.

Texture animation uses a **state-owned u16 native-ID permutation**.
A baked logical material id i stays stable; its currently selected content
is descriptor `animation_map[i]`. The renderer keeps logical material and
selected content separate: geometry UVs, logical identity and provenance
remain attached to i, while image/palette/upload content is selected through
the permutation. Loaded TX64, including its descriptor records and source-id
fields, stays immutable. The permutation is charged to `state`, never
allocated silently from `tex`.

Runtime rotations compose in stored animation-group order. The resulting
permutation is decomposed into cycles; the Game's cumulative texture
rotation step selects their phases, including overlapping groups and
counter wrap, without rewriting TX64. Before enabling descriptor-content
selection, boot must reject any group whose members differ in width,
height, relative corner UVs, material flags, triangle corner map or tile
split-boundary shape. Baked geometry may retain its original relative UVs
only under this compatibility contract.

## Exact binary tables

Header `>4s19I`, **80 B**: magic TX64; version=1, total bytes, object count,
sprite count, cluster count, palette count, split-line count, flat-colour
count, animation-word count; offsets of clusters, palettes, objects,
sprites, split lines, flat colours, animation words; flags (bit0 permits
odd widths; required for game version 2 and otherwise zero); two reserved
zeroes.

Cluster `>I4HI`, **16 B**: image offset, width u16, height u16, source row
bytes u16, border u16 (=0), reserved u32 (=0). Image bytes are row*height;
row=ceil(width/2), derived bit depth is always four. Images are the bounding
rectangles of connected components of same-page referenced rectangles
sharing at least one texel; TR2 additionally groups by X-origin parity.
Edge adjacency alone does not connect them.
PSX palettes do not affect index-image sharing. Holes consume image bytes.
High nibble is the left texel: PSX low-first nibbles are repacked, indices
unchanged. An odd-width row's unused low nibble is zero, not an extra texel.
No borders or row-padding texels are added. `border` belongs to the cluster,
not descriptors, allowing a future border-bearing layout without changing
descriptor sizes/meaning (version 1 rejects nonzero borders).

Palette directory `>IHH`, **8 B**, followed by **32 B** payload: RGBA5551
payload offset u32, STP mask u16 (bit i for palette index i), reserved zero
u16. Payload contains 16 big-endian RGBA5551 words. PSX BGR555 channels
are reordered to RGB; alpha=0 only for source word 0x0000. Source bit15 is
carried independently in STP, so 0x8000 remains visible black with STP.
The conversion is reversible, including STP; it does not equate STP with
alpha. Blend state and per-texel STP must be consumed together by the future
renderer. Palettes deduplicate by complete payload AND STP mask. PC
six-bit channels become c>>1, source index zero keeps its exact RGB and
transparent alpha, and opaque pixels cannot map to its palette entry.

Object descriptor `>HH4B8B2B3H`, **24 B**:

|Field|Bytes|
|---|---:|
|cluster id, palette id|2+2|
|cluster-local x,y,width-minus-one,height-minus-one|1+1+1+1|
|four descriptor-relative inclusive UV pairs|8|
|flags, triangle slot-map|1+1|
|original source object index|2|
|first split-line index, split-line count|2+2|

Flags bit0=opaque, bit1=cut-out, bit2=blended: source attributes 0/1/2;
unknown attributes are errors. Four corners are in `face_uv`'s common quad
polygon order (PC 0,1,2,3; PSX 0,1,3,2). Quad mapping is therefore identity,
not stored. Triangle slot-map packs two bits per corner low-first: PC
0,1,2 (0x24), PSX 0,1,3 (0x34); applying this map recovers the IR's triangle
`face_uv`. A triangle's unused native corner equals its first corner.

Sprite descriptor `>HH4B3H4h`, **22 B**: cluster/palette u16; cluster-local
x,y,width-minus-one,height-minus-one u8; original source sprite index,
first split-line index, split-line count u16; left/top/right/bottom world
bounds i16. Sprite flags derive to cut-out, and four UV corners derive to
(0,0),(w-1,0),(w-1,h-1),(0,h-1), in common polygon order. Source sprite
attribute absence is not replaced in the IR. Sprite-only world bounds
are never stored in an object descriptor.

Split lines **2 B each**, u16 descriptor-local start row of each tile after
the first. Unsplit rectangles have count=0, hence **no tile record**.
Each descriptor has full-width vertical tiles: starts [0]+split-lines,
ends split-lines+[height]. No tile dimensions, pitch, byte size, source row
stride or duplicate image offset is stored: all are derived. Shared cluster
stride lives in exactly one cluster record.

Flat colours `>4H`, **8 B**: original material id, RGBA5551, STP (0/1),
reserved zero. Only used materials, sorted by source id. PC uses material&255
of the original palette and forces opaque alpha (even material zero). PSX
samples the first raw UV of object texture material&0x7fff with that CLUT,
per OpenLara `format.h:6532-6541`. Geometry maps source material id to this
table. Animation words u16: group count then (member count, descriptor ids)
per active group; native member count, not source count-minus-one.

## Sampling coordinates and TMEM upload

Native integer UV k names **texel centre k**, not an edge. In edge-origin
sampling coordinates that is k+1/2. PC raw bytes encode u8 integer plus
low-byte fraction/256 (edge-oriented: low edges 0/1, high edges 255).
The IR and the cited OpenLara `SET_PARAMS` consumer deliberately select the
integer byte: fractional bytes remain source metadata, not subtexel shader
UVs. Cook normalises PC `(integer,fraction)` -> centre `integer`, for ANY
fraction, and PSX integer -> the same centre integer. Thus PC's near-edges
become centres of the inclusive endpoint texels. This is an explicit common
sampling policy, not a claim to preserve original fractional coordinates.
Sprites use the same centres, including BOTH inclusive source endpoints.

The renderer **point-samples** at texel centres. For integer native texel
coordinate u, the RDP fixed5 sample coordinate is **S = 32*u + 16**,
not 32*u. For a tile starting at descriptor-local (0,t), tile-rebased
coordinates are `S=32*u+16`, `T=32*(v-t)+16`. With unre-based cluster
coordinates use `S=32*(descriptor.x+u)+16`,
`T=32*(descriptor.y+v)+16` and set the tile origin/clamp minimum to the
same cluster-local upload origin `(descriptor.x,descriptor.y+t)`.
Continuous interpolated UVs retain their fractional part until this
fixed5 conversion. Do not mix tile-rebased and cluster coordinate systems.
A split before row t lies at centre-coordinate v=t-0.5 (edge-coordinate t).
Geometry clips there and interpolates all attributes; it must not split at t
in centre coordinates or apply the source fractional bytes a second time.

TR1 descriptors require both the cluster-local origin and exclusive right
end to be even. For TR2, clusters group overlapping rectangles by source
page **and horizontal origin parity**, so the cluster-local origin is
still even. An odd width retains its true S clamp at the last texel. The
upload alone rounds its byte endpoint up, reading the other nibble of the
last byte without making it part of the drawable rectangle. If that nibble
is beyond the cluster extent, it is the row's existing zero tail nibble.
Neither UVs, sprite bounds, nor the texture's drawable width are padded.

TMEM follows libdragon `src/rdpq/rdpq_tex.c:124-137,190-191`: for descriptor
cluster-local x and width w, `s0=x&~1`, `s1=(x+w+1)&~1` (exclusive end);
`pitch=align8((s1-s0)/2)`. With an even local x each tile loads
`ceil(w/2)` bytes per row in both game versions. Max tile height is
floor(2048/pitch); tile TMEM bytes=pitch*height <=2048 with TLUT loaded.
Source row bytes remain the cluster's tight stride; the upload API gathers
rows without changing the descriptor's tile origin or clamp minimum.
The geometry cook gets the actual split lines from the manifest/native table.

## Quantisation, decoder and evidence

Deterministic weighted median-cut on displayed RGB555 values, no dithering:
reserve exact transparent source index zero if present; allow at most 15
opaque representatives then, otherwise 16. Split the weighted widest box
on its widest channel at weighted median. Representatives are rounded
weighted means. Assign opaque pixels to nearest squared RGB555 distance,
ties lowest entry. Palettes deduplicate after quantisation. D10 accepts
this CI4 quality; no fallback API, format variant or policy table remains.

For TR2 PC textures, the material source is the ARGB1555 page, not the
indexed compatibility page. RGBA5551 retains source RGB555 precision
before quantisation, and alpha is never merged across opaque/transparent
texels; transparent RGB is discarded. Flat colours instead use the high
byte of the face material word in its RGB888 palette, reduced to RGB555.
Source: OpenLara `src/format.h:6454–6465,6530–6531`.

PSNR=10 log10(31^2/MSE) over RGB555 channels; exact images have JSON null
PSNR plus exact=true, maximum channel error 0..31. Alpha is asserted
separately. Host-only `n64_tex_view.py` imports no writer; it checks table
bounds, palettes, source ids, splits and rounded TMEM extents. Evidence
recomputes errors from source and binary-decoded pixels without calling
the writer's quantiser; scalar nearest-colour assignment independently
checks every PC cluster. Synthetic exact-colour and hand-computed distance
fixtures reject an argmin->argmax mutation. PSX checks every resident
descriptor's full RGBA and STP against the source, non-vacuously.

Cross-source pairs use M0's object identity, vertex-position multiset and
per-position rectangle-relative `face_uv` key. Distinct texture/used-bounds
pairs are checked, not an unrelated unused fourth corner. Original-source
PSNR is computed alongside decoded PSNR, with duplicate-key ambiguity
counts and matching geometry evidence retained. Three low-PSNR examples per
level show original PC, decoded PC, original PSX, decoded PSX side by side.
Raw RGB PSNR deliberately includes RGB hidden by alpha=0. Separate
common-opaque and same-black-background composite metrics distinguish
invisible palette differences from visible art or quantisation differences;
alpha disagreements remain separately counted. PC's transparent palette
entry can have nonzero RGB; PSX's transparent 0x0000 is always black.
Three inspected examples (original source PSNR, before cooking):

|Pair|Raw PSNR|Jointly transparent / pixels|Common-opaque result|
|---|---:|---:|---|
|GYM PC93 / PSX346|8.023 dB|962 / 4096|28.524 dB|
|LEVEL10A PC5 / PSX261|6.084 dB|3557 / 4096|exact|
|LEVEL8B PC6 / PSX262|5.268 dB|3557 / 4096|exact|

PC palette zero is respectively (63,0,63), (0,47,34), (26,47,34) in
source six-bit RGB. The latter two viewed panels are identical on common
opaque texels; their low raw score is entirely invisible RGB, not wrong
pairing, STP loss, or visible art changes. GYM has small visible source
palette differences as well. The generated report classifies every raw
PSNR<16 pair using composited metrics, without suppressing the raw result.

In the all-level measurement, 615 of the 616 raw-RGB pairs below 16 dB
have invisible transparent RGB differences and reach at least 16 dB after
compositing over black. The one remaining visible case is TITLE PC0/PSX256:
the original PC patch is opaque black while the original PSX patch contains
decorative pillar art (9.663 dB); both decoded images retain their own
source pictures. This case has no ambiguous geometry key. The composited
cross-source comparison is exact for 12,170 of 16,656 paired used rectangles.

## Commands and artifacts

```
make -f tools/cook/n64_tex.mk tex-tests PYTHON=python3
make -f tools/cook/n64_tex.mk tex-evidence PYTHON=python3 TR64_DATA=$TR64_DATA TEX_EVIDENCE=$TR64_WORK/evidence/m1-tex
python3 tools/cook/n64_tex.py --source pc --input LEVEL.PHD --output $TR64_WORK/tmp/Tex/level
```

Host dependencies: Python 3, NumPy, Pillow. `cook(level)->(bytes,manifest)`.
Each level produces `section.bin` plus JSON manifest; no derived data is
committed. Evidence writes compressed report.json.gz, report.md, 22 PC
contact sheets (worst up to 40 clusters per level), and cross-source panels.
Per-level breakdown sums header, images, palette directory+payloads,
cluster records, object+sprite descriptors, split lines, colours,
animations and alignment to exact section bytes. Cache key includes all cook
Python files, pool table, source hash and schema version; only matching
content-addressed outputs are reused, while assertions run on every hit.
