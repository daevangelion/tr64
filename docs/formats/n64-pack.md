# Native pack container, version 2

All u32 fields are big-endian. File offsets are pack-relative, never pointers.
The platform binds one explicit pack range; the engine uses raw asynchronous
`engine/io` reads, not FILE, DFS, assets, malloc or a post-boot library heap.

## File table

Header32 B: `TR6P`, version2, complete file bytes, level count, directory
offset32, directory stride48, directory CRC32, reserved zero. The directory is
sorted by unique UTF-8 name. Each48 B row: NUL-terminated name[32], section-table
offset, section count, table CRC32, game version. Zero means the existing
Tomb Raider I payload (also used for media-only entries); 2 means Tomb
Raider II for posed rendering only. Other values are refused by entry name.
The Tomb Raider I directory and all payload bytes are unchanged. Tables and
payloads start16-aligned. The loader reads the header, directory and selected
level table, not all tables.

Each64 B physical section record is eight u32 followed by32 SHA256 bytes:
kind, destination byte offset within logical kind, file offset, stored size,
raw size, alignment16, compression(0 none/1 LZ4 block), CRC32(raw), SHA256(raw).
Records are sorted by kind then destination; each kind is a contiguous
partition starting at0. Stored payload has one zero DMA pad byte if odd;
that byte is not included in stored size or hash. All physical payloads are
16-aligned. Equal SHA256 content is compared byte-for-byte before deduplicating
across kinds/levels; all equal sections share a physical file offset.
The raw CRC occupies byte28, formerly reserved in version1; no entry grows.
Version1 packs must be recooked. SHA256 remains the cook's content-addressing
key and is independently verified by host decoding. Cart integrity uses the
standard reflected IEEE CRC32 (polynomial0xedb88320, initial/final xor0xffffffff)
over exact raw bytes, excluding DMA padding. This is a corruption checksum,
not authentication; all structural/reference validation remains mandatory.

Kinds1..65535 are native-core tables. Opaque kinds are texture0x10000,
rooms0x20000, meshes0x30000, sound0x40000 and title image0x50000. `Section(kind,data,alignment=16,
header_bytes=0,chunk_bytes=16384,content_defined=False,compression='auto',
boundaries=())` and
`build({name:[Section,...]},compress,deduplicate=True)` are the cook API.
Partition policy never changes reconstructed bytes. Core headers are separate;
small record-aligned animation bundles and content-defined frame runs allow
sharing despite changing counts/offsets. Frames use2..16 KiB word-aligned
chunks (4 KiB average target), other defaults are <=16 KiB fixed chunks.
Explicit `boundaries` are strictly increasing internal logical offsets (not0
or the final length); when present the first cut must end the whole metadata
prefix, and subsequent parts obey `chunk_bytes`. This lets each sample retain
its own <=4KiB pieces without cart-wasting4KiB sample padding. `compression='none'`
requires raw physical parts even if a compressor is supplied. If an identical
raw digest is shared with an automatic reference, raw storage wins globally:
there is still only one physical encoding of those bytes.

## Compression

The cook builds the pinned libdragon level1 LZ4 HC compressor from cached SDK
sources, keyed by all source bytes, host compiler/version/target and flags.
Each section uses LZ4 only if it reduces physical16-aligned storage; otherwise
none. There is no invented codec. The bounded original C decoder checks every
length, literal, match distance and output bound, then table-driven CRC32
verifies raw bytes before publication. Cart SHA256 code/constants are absent.
Output is decoded directly in its final level-arena section (or the explicitly
selected nonresident destination pool). A16 KiB io staging window holds compressed inputs, odd DMA tails and
unaligned destination pieces; even aligned uncompressed parts DMA directly.

## Production loading state and placement

There is one engine and one production loader, shared by host/cart and scenes.
`TR64_LEVEL_PACK=/external/level.pack` selects data, not an engine variant.
A game-free generated binding supplies host path, raw ROM range and input hash.
The module uses documented additive ROM file/dependency hooks. There are no
PC/PSX engine branches, scene-only loader registrations or alternate links.
`TR64_LEVEL_LOADER=0` is only the common deliberate missing-loader control:
it omits this entire module and pack binding, not a second implementation.

Pack parsing, LZ4, TX64/core validation and renderer binding are cold
work in the single `load` code unit. Its selectors include only text/rodata;
all writable state, public dispatch facades, getters and their labels remain
resident. Facades resolve `code_call("load", request)` on every work call, not
a cached entry pointer. Core operations0..8 and renderer operations>=0x100 use
the same entry; delegation within the unit is a direct private call.

`tr64_load_scope_begin/end` is a balanced resident-thread lease. Only the outer
begin loads the unit; only the outer end unloads it. The route keeps that lease
through Core/TX, renderer geometry remount/bind and its initial frame, releasing
it before returning to play. The synchronous Core/TX driver nests a lease so
standalone callers are safe; incremental pack/core callers explicitly hold one.
The unit uses only the24 budgeted code frames, never implicit pool borrowing.
No persistent view contains a pointer into the unit or temporary io storage.

Final read rule: every content-addressed physical section is one asynchronous
engine/io request, scheduled in <=4 KiB pieces. This replaces the former
architecture outline's one-read-per-entire-core assumption. **Loading is not
play:** the production driver pumps requests back-to-back, resetting service
budgets without advancing game ticks. It does not poll only once per30 Hz
frame, so175 sections do not imply175 frames. During play the existing32 KiB
per-frame budget still applies. A saturated queue produces BUSY and is retried
while other requests drain; invalid requests/binding are named as such, not
misreported as corrupt/truncated content.

## Pool lifetimes
Core, textures, room geometry and meshes share one1472KiB `level` arena,
not four independent partitions. After validating the selected directory the
loader computes each logical kind's checked decoded size. Core reserves the
sum of16-byte-aligned table sizes; RG64/MG64 reserve their exact raw sizes.
TX64 and optional UI64 image share the tex section: align the accumulated tex
size16 before each successive kind, then add its exact raw size. A TX64-only
pack keeps its former exact child budget. Image adds no fifth pool or arena.
`mem_level_plan(name, sizes)` lays out core/tex/rooms/meshes in that fixed order,
aligning each section reservation16 bytes and rejecting an excessive aggregate
before any payload read. A remount must name exactly the same level and sizes.

Resident `pack_begin_kind` callers select `TR64_MEM_level`; kind determines the
planned section. Each allocation is bounded by that section's directory size,
while block destination offsets, decompression lengths and hashes retain their
existing exact bounds. Nonresident destination pools remain generic. No section
borrows from its neighbor and there are no old pool aliases or per-section
fixed caps. Generic allocation/reset of a published level arena is forbidden.
Parent `pool: level` use is the total reserved layout; `level-section` lines
report each section's base, exact directory budget, actual use and peak.

### Optional title image

IMAGE is an opaque UI64 asset, not a Core table or UI colour-constant table.
The normal level finish first retains TX64's own span, then reads IMAGE if
present and validates its whole payload before publishing readiness. The
read-only `tr64_level_title_image(level, &bytes)` returns that arena span after
the loading unit is unmapped; absent/not-ready views return NULL/zero. Release
invalidates it with the other level pointers.

UI64 ABI1 is exactly `>4s7I`: magic, version1, total, width320, height240,
format, pixels offset32, palette offset. Format1 is CI8:76800 pixel bytes plus
512-byte RGBA5551 palette at76832, total77344. Format2 is big-endian RGBA5551:
153600 pixel bytes, palette offset0, total153632. Unknown dimensions, formats,
offsets, versions or trailing/short data are errors, not alternate layouts.
Source decoding/scaling is cook-only; the loader has no PC/PSX distinction.
`make n64-image-tests` exercises both layouts, source fixtures, shared tex
alignment, retained TX identity and correctly rehashed hostile UI64 headers.
Full-pack observable manifests include `image_bytes`, numeric `image_format`
and `image_checksum` (the usual verified-kind fingerprint). The Core checker
requires the matching image record and checks combined aligned TX64+image use,
not the obsolete assumption that tex's entire allocation is TX64 alone.

The cook's `level_layout` applies the same alignment and aggregate bound.
Complete playable packs and the42-level measurement include Core/TX64/RG64/MG64;
a Core/TX-only cook checks only that partial layout and does not claim geometry
fit. Current decoded worst is PC LEVEL10B1440528 bytes (core452368, tex459728,
rooms357480, meshes170936), leaving66800 bytes in1507328; PSX worst LEVEL6 is
1428800 bytes. These are actual current payloads, not M0 source-model estimates.


Mount requires an empty exclusive io lease. Header scratch is in the caller's
loading-state object; directory, selected section table and staging are in io.
The wire table is read in <=16KiB pages through the existing staging window.
Its complete64-byte records, including SHA words, contribute to the table CRC;
only the first32 bytes (the eight runtime words) are retained in the lease.
This bounds a700-record synthetic table to22400 retained bytes instead of44800,
leaving room for staging within the unchanged48KiB row. Descriptor bounds and
the full wire CRC are validated before payload or stream-index publication.
Other users may drain already queued requests but cannot allocate from that
lease. `pack_unmount` requires no outstanding request and verifies ownership,
then releases only the exclusive lease. It does not reset any resident payload
pool. No view retains an io pointer after readiness.

**On-demand nonresident catalogs.** MUSIC0x60000 and VIDEO0x70000 remain outside
the level arena. Music entries are named MUSIC_02..MUSIC_57 by the media cook;
the container does not interpret the track codec or unavailable flags.
While a level is mounted, `tr64_pack_stream_source` looks up a named directory
entry and exports a16-byte table-offset/count/CRC/kind reference. The optional
resident `tr64_music_catalog_bind(pack)` callback runs before the mount lease
is released and may copy those references only (no I/O, allocation or unit
transition). Fifty-six references occupy896 bytes, not a whole-disc block index.

At playback, `tr64_pack_stream_query_begin/poll` reads only the selected table
through caller-owned64-byte-multiple scratch (normally4096 bytes). It validates
all wire bytes/CRC, expected kind, raw codec, logical contiguity and ROM bounds,
and publishes `query.result` only after the complete table passes. Destination
index/scratch/query state are caller-owned; no pool allocation, level plan,
pack rebind, synchronous I/O or cold-code dispatch occurs. Requests use the
same engine/io queue and32KiB/frame service budget as SFX. Startup latency
includes that contention, not a promised fixed number of frames.

Stop uses `tr64_pack_stream_query_drain`: it never submits another page, waits
only for an already queued DMA, then invalidates the partial result. BUSY
requests not yet queued are discarded without submission; no cancellation is
pretended. Catalog/index offsets require the same bound pack and all media DMA
must drain before level rebind/release. Codec choice and the music memory row
are Media/Main decisions, not allocations made by this generic transport.
`make n64-catalog-tests` exercises this resident path after load-unit unload,
including full-queue retry, drain, later-page CRC and repaired hostile records.

`tools/cook/pack_level.py --music-catalog /content-key/manifest.json` merges
Media's validated catalog entries into the same pack as the selected level and
SN64 bank. The argument is an immutable manifest file, not a directory. Music
keeps the producer's raw, codec-aligned block boundaries; it does not enter the
level arena or trigger a second binding. Name collisions, resident kinds and
automatic container compression in a music entry are rejected. The report
records `music_catalog_sha256`, `music_tracks` and raw `music_bytes`; the manifest
file's SHA-256 is also included in the pack producer's content identity. The
independent decode check covers every level and music entry in the assembled
binding. The catalog gate includes synthetic producer composition controls.

For boot movies or playback after level release,
`tr64_level_pack_bind_streams()` restores the configured campaign's raw binding
and I/O extent, returning its byte size. It neither plans nor loads a level.
The caller first stops/drains consumers; live DMA, an exclusive I/O lease or
an active load scope are named errors. Normal scene loading uses the same bind
operation, so host/cart range identity has one implementation.

After binding, enter a load scope and call `tr64_pack_mount_catalog(pack, bytes)`.
Poll normally: this validates only the TR6P header and whole directory
CRC/names, retaining exactly48 bytes per directory entry in `io`. It reads no
selected section table, allocates no staging window and never calls the level
planner. There need not be any level entry at all. Copy the desired VIDEO_* or
AUDIO_* sources with the existing name/kind lookup, unmount, and end the load
scope before entering the video code unit. The existing resident query builds
each stream's index into caller-owned video-arena storage; raw stream reads
then use ordinary engine/io. No directory pointer survives unmount, no fake
TITLE is mounted, and rebind after release always names the same configured
pack. Catalog-only mounts cannot load a selected payload kind or Core.
Native controls include a VIDEO-only pack before `mem_level_begin`, zero
level/staging allocation, cold-unit unload, CRC/name failures and bind guards.

The level driver loads core and real TX64 into their planned arena sections, releases directory,
section table and staging, then reuses io for bounded validation scratch. It
resets that scratch before returning. Loading io usage during play is **zero**.
`tr64_pack_stream_index` copies nonresident raw-block descriptors into
caller-owned storage during a loading lease; each retained16-byte record is
logical offset, bound-pack ROM offset, raw bytes and raw CRC32. Its block count
and total describe the container span, not a second native sample directory.
Compressed blocks are rejected. The caller's audio budget owns that index
until sound stops; unmount retains no I/O pointer.

Resident `tr64_pack_stream_read/poll` submit caller-owned requests through
engine/io and verify raw CRC32 after completion, with no loading-unit dispatch,
heap or decoder. Buffers are16-byte aligned and include one byte of capacity
for an odd DMA tail; CRC excludes that padding. The shared incremental
table-driven `tr64_crc32` and its single table are resident for both loader and
playback use. Invalid capacity/active requests reject before enqueue; failures
name the stream block. Partial Core/TX callers without a sound bank retain
their explicit `sound_refs=unvalidated` readiness.

The PC source validator checks sample offsets where present; PSX VAG indexing
is not substituted for that source domain. A selected SN64 bank is attached
before readiness: Core exports its raw-block index into boot-owned audio
storage, reads/verifies its first metadata part through engine/io, then releases
the temporary pack lease. Audio's pure structural validator checks the SN64
header and spans; Core checks every sound detail's sample variant range against
that loaded header's real sample count. `SI_COUNT=0` still references the base
sample, matching fixed `soundPlay`.

Out-of-count references require exact sorted native unavailable pairs for
every mapped sound id/variant; unrelated pairs and unlisted missing references
are rejected. No Core sound map/detail row is changed and no sample descriptor
is invented. Audio skips/counts unavailable events. After validated bind,
readiness is `ready=core+textures+sound sound_refs=validated`; without a bank,
partial callers retain the explicit unvalidated sound domain. Metadata/index
pointers are borrowed from Audio until its level-stop; no I/O pointer escapes.
The Core load counters include the metadata request/CRC; Audio's optional
frequent-sample preloads are separately Audio-owned work within total loading.

For game version 2, the loader borrows the idle frequent-effects cache for
up to 8,256 B of SN64 metadata (4,448 B for WALL), enough for a 64-byte header
and 256 sample descriptors. It validates the directory and cross-references
but does not bind the bank to the mixer. The source cook retains only
sound-map-referenced sample slots from the external MAIN.SFX bank, in
source variant order. The TR1 metadata cap, cache and allocations do not
change. Playback refuses the render-only version.

The outer scene subsequently fills/binds RG64/MG64 before ending its scope.

## Evidence

`make n64-core-tests` runs synthetic roundtrips using a separate decoder with
literal layouts from these documents, plus correctly hashed hostile tables
through the real host platform, carve, engine/io and production dispatch.
`make n64-core-measure` reads all43 source files, emits21 catalog levels per
source and reports Snowcut not cookable under strict validation; M0 still
accounts for its source bytes. Reports separate core-only cart storage from
core+texture storage and keep no-dedup/dedup/none/LZ4 columns.

Per-kind observable `hash` is CRC32 of the ordered four-byte big-endian raw
section CRC32 values. Host/cart compare the same verified-content fingerprint,
without a second full-byte pass. The `hash_us` metric is raw CRC32 verification
time in this version, not runtime SHA256.
The load report brackets the production operation and separates total,
per-request accepted-to-observed-completion elapsed time (host clock includes
the modeled bus; cart uses emulated time, including queue/poll delay, not pure
PI-wire time), decompression, hashing and validation. It reports occupied
staging peak separately from the fixed16384-byte reservation. `level-load`
brackets just the Core/TX call; its `unit_load_us` is the acquisition cost caused
by that call (zero when the enclosing scene already holds the lease).
`load-scope` separately reports the actual outer acquisition, full lifetime
through geometry binding/initial frame, and unload time. The engine checks that
the unit is unmapped, and the following code-frame log demonstrates the return
of its pages. Scene checks require one acquisition/release and zero pages held
before play, in addition to the content/pool checks. Resident image cost, unit
pages and load time come from the one linked build and actual scene logs, not
an alternate link or hypothetical placement estimate.
All derived packs, reports and logs remain outside the repository.
