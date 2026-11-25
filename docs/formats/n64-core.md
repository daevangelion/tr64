# Native immutable level core, ABI 3

Normative consumer rules: OpenLara `2333adef`, `src/fixed/common.h`,
`level.h`, `fmt/phd.h:15-210,1275-1425`, `room.h`, `item.h`, `nav.h`.
Container: [n64-pack.md](n64-pack.md). Source meanings: [ir.md](ir.md).

## Layout

All integers are big-endian. Each kind is one logically contiguous table,
assembled from content-addressed physical sections. Its 32-byte header is:
`TR6C`, u32 kind, count, stride, present(0/1), payload offset(32), total table
bytes, ABI version(3). ABI1/ABI2 packs must be recooked; there is no old-ABI shim.
The payload has the exact wire stride below; table
starts are16-byte aligned and all scalar members are naturally aligned.
There are no pointer relocations or writes to loaded tables. References are
element indices unless explicitly byte offsets. Public encoded-word types
perform native cart loads and scalar host swaps. Count/presence are separate;
absent PSX demo data is different from present-empty PC demo data.

|Kind|Table|Stride|Ordered fields|
|---:|---|---:|---|
|1|rooms|80|i32 x,z,source y_bottom,y_top,computed bounds_bottom,bounds_top; u16 z_sectors,x_sectors; i16 alternate_room; u16 flags,ambient,zero; u32 start/count pairs for sectors,portals,lights,static_instances,room_sprites;4 zero bytes|
|2|sectors|8|u16 floor_index,box_index; u8 room_below; i8 floor; u8 room_above; i8 ceiling|
|3|portals|32|u16 room; i16 normal XYZ then four XYZ corners|
|4|lights|24|i32 world XYZ; u16 intensity,reserved word; u32 radius,reserved_present|
|5|static_instances|24|i32 world XYZ; i16 angle; u16 intensity,mesh,reserved word; u32 reserved_present|
|6|floor_data|2|Original u16 floor/trigger words|
|7|mesh_pointers|4|Original u32 mesh byte offsets, aliases preserved; geometry owner resolves identities|
|8|animations|32|u32 frame_offset(bytes); u8 frame_rate,frame_size(words); u16 state; i32 speed,acceleration(16.16); u16 frame_start,frame_end,next_animation,next_frame,state_count,state_offset,command_count,command_offset|
|9|states|6|u16 state,range_count,range_offset|
|10|ranges|8|u16 low,high,next_animation,next_frame (OpenLara's unsigned interpretation of source words)|
|11|commands|2|i16 command/argument words; opcodes interpreted as u16|
|12|bones|4|Original i32 groups flags,X,Y,Z|
|13|models|24|u32 type; u16 mesh_count,mesh_start; u32 bone_offset(source word index),frame_offset(bytes); u16 animation,reserved word; u32 reserved_present|
|14|frames|2|Original u16 bounds/root/joint-count/packed-rotation frame words|
|15|static_meshes|32|u32 id; u16 mesh;12 i16 visible then collision bounds, min/max X,Y,Z; u16 flags|
|16|sprite_sequences|8|u32 type; i16 count; u16 first **combined native TX64 descriptor id**, not a source sprite index|
|17|cameras|16|i32 XYZ; i16 room_or_speed; u16 flags|
|18|sound_sources|16|i32 XYZ; u16 sound,flags|
|19|boxes|20|i32 min/max Z,min/max X; i16 floor; u16 overlap flags/index|
|20|overlaps|2|u16 box indices/list-end flags|
|21|zones|2|u16 words (normal ground1/ground2/fly then alternate equivalents)|
|22|items|24|u16 type,room; i32 world XYZ; i16 angle,intensity; u16 flags;2 zero tail bytes|
|23|cinematic_frames|16|i16 target XYZ,camera XYZ,FOV,roll|
|24|demo_data|1|Exact bytes, or absent|
|25|sound_map|2|256 i16 sound-detail indices, including-1|
|26|sound_details|8|u16 sample,volume,chance,flags; metadata only|
|27|node_flags|4|u32 effective flags per four source bone words, including cooked head masks|
|28|room_sprites|12|i16 x,y,z; u16 gain(Q12),descriptor(combined native TX64 sprite id),reserved zero|
|29|mesh_spheres|8|i16 source-local centre x,y,z; u16 source radius; one record per mesh-pointer slot|

For pack game version 2 the same strides carry the room's secondary
ambient/mode at offsets 76/78 and item secondary intensity at offset 22.
Lights use their reserved words for secondary intensity/radius; static
instances use the reserved u16 for secondary intensity. Ten zone arrays
are stored in source order, and `sound_map` holds 370 entries. Frame payloads
preserve the source packed-axis encoding and `node_flags` omit TR1-only
head masks. Renderer-only admission does not run TR1 gameplay consumers
against these records.

Cooked room/sprite shade uses the second TR2 PC vertex light word (the
one read as the real light value by OpenLara `src/format.h:5283–5294`).
The earlier light and vertex attributes remain in the source IR; they
are not GPU vertex channels and are not carried into the render-only pack.
Static submission also uses the second shade word for TR2, provisionally
following OpenLara `src/format.h:5527–5543`; TR1 uses the first. The renderer
applies the existing static shade/tint; no dynamic light mode, vertex
attribute animation or water simulation is implied.

Reserved model/light/instance words retain both bytes and explicit presence.
Geometry padding, vertices/faces, raw mesh blobs, encoded sounds and source
palette/page/VAB records are not core. Animation metadata lives **once** in
TX64: its active animation ranges contain native object-descriptor ids.
Inactive/unreferenced texture groups and source padding are not runtime data.
The pack cook runs `n64_tex.cook` first; the core writer receives its source-to-
native maps and resolves every sprite-sequence member, proving the native
members remain contiguous. Source texture indices never occur in the core.
The independent roundtrip reverses the manifest mapping for comparisons with
IR sprite sequences and compares TX64's active groups with source groups.

Room sprite records are flattened in source room/list order. `sprite_start` and
`sprite_count` occupy room-record offsets68 and72. Offsets76..79 are zero
on TR1 and carry the TR2 lighting fields above, so the stride stays80.
XYZ comes from the referenced source room vertex:
X/Z are room-local, Y is source world height, exactly the RG64 convention.
The renderer adds the room's X/Z origin; no cook quantization or pointer
fixup occurs. `descriptor` comes from `sprite_descriptor_map`, never a source id.

`gain` is the canonical unsigned Q12 gain used by the geometry cook before its
room cap/tint: PC `clamp(8191 - darkness, 0, 8192)`; PSX
`clamp(vertex_light_word * 32, 0, 8192)`. This matches `n64_geo.gain` and
retains its full0..2 domain; it is not dynamic item ambient/intensity. Renderer
caps to6144 (gain1.5) and applies the same room tint/fog policy as room geometry.
Only the referenced source vertex's position/light is needed at runtime, not
the source vertex index. The independent decoder checks every such record
against the source vertex and independently reverses native descriptor ids.

The cook and load checks reject invalid source vertices/textures, out-of-range
room spans/native sprite descriptors, gain above8192 and nonzero reserved words.
This adds `align16(32 + 12 * room_sprite_count)` resident core bytes per level,
including the new table header; no existing room record or mutable state grows.

Mesh spheres are immutable gameplay collision data, not renderer bounds.
The cook looks up each original `mesh_pointers` byte offset in the IR's physical
mesh dictionary and emits its exact centre/radius, in pointer-slot order.
Repeated offsets repeat records; aliases are not compacted. `models.mesh_start`
plus the local bone/mesh index therefore indexes `mesh_spheres` directly, just
as OpenLara `src/fixed/item.h` indexes meshes for its sphere collision path.
There is no geometry lookup, cook quantization, derived AABB sphere or runtime
fixup. `tr64_level_mesh_spheres(level, slot)` returns the typed immutable record;
read x/y/z with `tr64_i16` and the radius word with `tr64_u16`. OpenLara
`src/fixed/common.h:1023` interprets `Mesh.radius` as i16: the game sign-extends
this raw word before assigning its collision sphere (not an unsigned distance).
The native validator requires exactly the `mesh_pointers` count. All signed
centre words and the complete unsigned radius domain (including0 and65535)
are valid; integrity remains protected by the section CRC. Independent tests
compare every source field and preserve nonconsecutive aliases.
This adds `align16(32 + 8 * mesh_pointer_count)` resident core bytes, no mutable
state bytes. MG64 stays renderer-only and is unchanged by Core ABI3.
Measured over all42 catalog inputs (21 per source), the largest addition is
LEVEL7B in both sources:382 slots,3088 bytes including header/alignment.
LEVEL1 adds1776 bytes (217 slots). The largest complete ABI3 core is LEVEL10C:
PC528960 and PSX528928 bytes. Core bytes now contribute to the shared1472KiB
level arena;589824 is no longer an independent Core cap. Mutable-state costs
are unchanged.
Ranges/zones reverse their unsigned wire interpretation back to signed IR
words when testing preservation of the original bit patterns.

## Cook versus game-port conversions

- Room X/Z remain full world i32 rather than OpenLara's narrowed `>>8` values;
  port uses them directly. Source Y bounds remain; the cook separately computes
  vertex extrema exactly as the fixed loader, falling back for empty geometry.
- Lights/instances retain world positions, original darkness and radius. Port
  subtracts room origins for local coordinates, uses intensity `>>5`, and radius
  `>>8` where the fixed consumer requires those domains; no lossy cook copy.
- Item positions remain full world i32. The port performs the fixed loader's
  room-relative int16 conversion at state initialization, not in immutable core:
  X/Z subtract room origin; Y narrows directly. The position reconstructed by
  game initialization follows those same conversions. No extra truncation is
  hidden in the cook.
- Item intensity remains signed source i16; port clamps negative to0 and then
  shifts nonnegative values right5, as `phd.h:1181-1184` does.
- Item and static-instance angles are **signed i16**, retaining source bits.
  Quadrant is `(angle / 16384 + 2) & 3`, with C99 signed division truncating
  toward zero; port ORs it into item flag bits14-15 or instance bits6-7.
  For0xe000 (-8192), quadrant is2, not the1 obtained with unsigned division.
  Existing source flag bits are ORed, not overwritten.
- Animation frame offsets remain bytes; divide by2 to index frame words.
  Bone offsets remain source word indices; divide by4 for nodes. The cook
  precomputes OpenLara's head masks in node_flags; original bones stay intact.
- Box bounds stay original world units; the port's grid domain is min>>10 and
  (max+1)>>10. Ranges and zones use OpenLara's u16 interpretation, not signed
  comparisons. Initial item/model/static definitions remain immutable; keyed
  model bindings and mutable item instances are state-owned.
- Static visibility spheres and geometry references are the renderer owner's
  work; original collision/visibility boxes and mesh identities are retained.

## Validation and readiness

Both game tags validate native table sizes and reference spans before
returning a ready Core+texture view. This includes room dimensions,
alternate-room topology, sector/portal indices, frame and animation
table spans, terminated box-overlap chains, object and static references,
sound references and native sprite/animation ranges. The cook checks
corresponding source and native invariants.

TR1 (tag 0) requires zero room lighting and item intensity tails, six
zone arrays, and the fixed game's byte domains. It validates camera room
references, complete floor/trigger programs and their operands, and the
fixed loader's next-frame rules, including the exact terminal-range pattern.
TR2 (tag 2) requires ten zone arrays, validates packed frame byte spans
and animation target indices, and retains target frames as data without
applying TR1's next-frame predicates. The camera/sink field is opaque
(not a guaranteed room index). The source reader validates TR2's floor
programs; the cart checks sector offsets into that table but does not
interpret the program words. Sequel gameplay entry points reject the
render-only tag before any of those records can be used by TR1 Game.

Validation work is bounded: command-word reachability is computed backwards
in a u16 scratch table; overlap-chain validity is computed backwards in bits;
TR1 floor-command and trigger-action starts are memoized separately. Scratch uses
the exclusive49,152-byte loading I/O lease after read metadata/staging have been
released. A table whose validation scratch cannot fit that row is rejected in
both cook and loader, naming the table; no heap or reserve borrowing occurs.

The source catalog is exactly the fixed game's21 names (title, gym,15 game
levels, CUT1..CUT4). M0 still reads all22 PC and21 PSX files for source accounting.
PC Snowcut is outside that catalog, has no PSX counterpart, and is not one of
`isCutsceneLevel`'s four levels. Its box0 overlap0x7fff and empty overlap table
are rejected by the same strict validator as any other file. It is reported
**not cookable**, not emitted to the cart, and has no filename exemption or
invented neighbor sentinel. `common.cpp:49-71`, `level.h:396-401`, and
`fmt/phd.h:1344-1358` are the positive consumer evidence.

TX64 is a real separate resident kind loaded through the same pack API into
tex. Its native descriptor counts bound core sprite sequences; its own active
animation words are validated against its object count. Geometry will join
this production load plan when its owner supplies the native kind/validator.
There is no encoded sound section yet (M4): the cook checks actual PC sample
offsets where present; it does not substitute PSX VAG counts for that absent
domain. The cart cannot validate the external sample domain. The
readiness log explicitly says `sound_refs=unvalidated`; it does not claim sound
or geometry ready. No duplicate texture/source count table is stored in core.

## Mutable copies and state budget

Observed fixed-game writes and the native state representation:

|Data|Consumer writes|Native state bytes|
|---|---|---:|
|Room bindings/item heads|install, add/remove, alternate selection|16 per room|
|Sectors|Room::modify; blocks, doors and flip rebuild|8 per sector, conservative full-copy upper bound|
|Box overlap flags/baseline|door/flip rebuild|2 per box plus ceil(boxes/8)|
|Camera flags|once-only trigger|2 per camera|
|Models by admitted type|sprite install, Adam mutation|24 per admitted type slot|
|Texture animation permutation|logical content rotation|2 per native object descriptor, state-owned; never mutates TX64 source-id columns or content|

Sprites' packed-loader copy/relocation is unnecessary for native numeric ids.
Floors, frames, animations, states, ranges, commands, cooked bones, overlaps,
zones, static definitions, sound metadata, cinematic frames and demo bytes
remain immutable. Runtime items, Lara extras, enemy navigation, effects and
trigger/flip/flow state must also fit the128KiB state row; this slice reports
exact mutable-table copy costs and remaining capacity, not a measured complete
unported game-state allocation.

## Production loading and evidence

One engine/build handles every pack. The route requests a named level; the
ordinary loader uses the selected external pack, not a scene-only C path or a
source-specific compile flag. Cold parsing, decompression and validation run in
the `load` code unit; resident facades dispatch each operation, while typed
gameplay views and labels remain resident. Published views reference only
persistent pools, never unit text/rodata or temporary io storage.

A balanced, nestable loading lease holds the unit through Core/TX, renderer
geometry remount/bind and the first rendered frame. Its outer end unloads the
unit before returning to play. The synchronous Core/TX driver acquires its own
nested lease; incremental APIs require an explicit caller lease. `level-load`
timings bracket the Core/TX operation (unit acquisition0 when nested), while
`load-scope` reports the complete outer lifetime, actual acquisition and unload.
Both unit page use and post-release frames are observed from the real build.
The I/O lease is released before the level-ready return. Loading pumps requests
back to back; it is not limited to one physical section per game frame.

Resource invalidation is the separate resident `tr64_level_release()` seam.
The caller first stops the game and drains/invalidates renderer references,
then calls it within the shared non-dispatch loading release transaction.
It rejects an active resource loading lease, pending I/O or retained exclusive
I/O scratch before clearing the published current view. The view remains
addressable but not ready, with no retained table/texture pointers. This call
does not reset pools, release code units or recycle the level planner: those
belong to the coordinated lifecycle transaction after all users are stopped.

`make n64-core-tests` exercises synthetic sources, an independent decoder with
literal layouts from this document, and correctly rehashed hostile packs through
the production host platform/carve/I/O/loader. `make n64-core-measure` reports
all source inputs, catalog selection/not-cookable reasons, native field
roundtrips, row margins, per-level/source-wide four-way cart bytes and measured
dedup granularity. Reports/data-derived packs live outside the repository.
The standalone Core harness generates a private dispatch registry from the
base init/test selectors plus `engine/res/load.def`, using the production
unit generator. Its private include directory precedes global generated
headers: production Game/enemy unit fragments cannot add undefined entries
to a harness that deliberately does not link those modules. Cart-layout
calibration still comes from the real linked image.
Host and ares level-load routes check all table and texture fingerprints, pool
peaks, total load time and separately bus/decode/hash/validation costs. ares runs
are stock4MiB through `test/run/scene.sh --ares`; no manual emulator lane.
