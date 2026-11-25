# MU64 music streams, ABI 1

Music is original-release content. The cook accepts a single-file BIN/CUE
from either an original PC CD or a PlayStation CD. A PC data directory alone
contains no CD audio: missing music is reported, not generated. There is no
runtime source-platform branch and no replacement-soundtrack fallback.

## Source inventory and provenance

Measured source: the supplied SLUS-00152 v1.1 BIN/CUE on <remote-host>. Its first
track is MODE2/2352; tracks02..57 are **CD-DA**, 44100Hz stereo signed16,
2352 bytes/sector,588 stereo frames/sector,75 sectors/second. Music is not
XA. The disc's ten STR movies contain XA audio separately (see n64-video.md).
Cue INDEX01 through the next track's INDEX00 excludes inter-track pregaps.
The last track ends at the BIN length. These56 spans total1696.586667s
(28.276444min). Track57 is197s but has only one nonzero signed16 value(+26)
in17375400 samples; track05 positive control has10320584 nonzero samples of
10420536. Track57 cannot be relabelled as the fixed game's cistern ambience.

The user's PC tree contains data and eleven RPLs, **no CD-DA image**. No PC
music durations, mapping equivalence or decoded quality is claimed. A user
supplies that release's CD image explicitly; no music is recovered from its
retail executable.

The directory called tr1-remaster-ogg contains02..59, tagged2024, album
`Tomb Raider I Remastered (Tomb Raider I-III Remastered) OST`. It is analysis
input only and is never accepted as cooked game content. Its02..56 durations
match original CD-DA within20.4ms, but that is not whole-waveform identity.
For57/58/59, normalized2kHz-mono correlations of three5s snippets against
all56 originals yielded maxima0.083..0.164, scattered between unrelated
tracks. Positive control05 matched disc05 at0.958..0.977 with20ms trim.
This search locates no evidenced original source; it does not prove that no
transformation or subregion could ever match.

Reproduction tools: n64_music_source.py and n64_music_fingerprint.py (analysis
only). Historical candidate survey code is retained at e505c39 and in the
external bridge-candidate source snapshot. Source/header inventories,
fingerprints and codec-error sums are under `$TR64_WORK/tmp/Media/`.
No game data or derived payload is committed.

## Game requests versus transport

Authoritative game facts: OpenLara2333adef `src/fixed/common.h:2673-2686`,
`common.cpp:51-71`, `room.h` track/secret triggers, `enemy.h` skater/Natla,
`inventory.h` end page, `game.h` ambient restart, and the port's game owner.

| Request | Role |
|---|---|
|4|Title (the source's comment about2 is not implemented)|
|5|LEVEL1,LEVEL2,LEVEL3A,LEVEL3B ambience|
|57|LEVEL7A,LEVEL7B,LEVEL10A ambience: unavailable original source|
|58|LEVEL4,LEVEL5,LEVEL6,LEVEL8A/B/C ambience: unavailable original source|
|59|LEVEL10B,LEVEL10C ambience: unavailable original source|
|23,25,24,22|CUT1,CUT2,CUT3,CUT4 respectively (one-shot synchronized audio)|
|13|Secret cue|
|56|Skater damage cue|
|54|Natla get-up cue|
|3|End inventory page|
|26..56|Gym/tutorial and ordinary floor-trigger requests; preserve source ids|
|-1|No track (GYM starts without ambient music)|

Other positive floor-trigger arguments are content requests, not a hardcoded
engine playlist. The game's masks/ONCE bits decide whether a trigger submits.
The cook carries available original tracks, not just cues observed in one
scene. Request ids2..56 map to the same numbered CD track for this measured
release. Requests57/58/59 are explicitly unavailable, not silent PCM, remaster
content or guessed mappings. A different PC CD needs its own evidenced map.

Main's transport decision: the game exports read-only
`{track,ambient,serial,reason}`. Serial changes on every explicit set, trigger, secret,
level start and restored track, including identical ids. No end-of-stream
feedback enters game state, the oracle, the trace or the hash. The engine:

- ignores an identical request while that track is still playing;
- plays a cue once, then resumes the level ambient track;
- restarts an ambient stream at its end;
- keeps music running in inventory/menus and on the death page;
- counts unavailable requests and plays nothing for them;
- drains and stops before level release, before SFX stop and cart unbinding.

This is transport policy, not a change to the fixed game's control or save
state. A restored non-ambient track resumes the level ambience instead of
replaying a possibly long-finished cue. RESTORE is an explicit request reason;
the saved/source gCurTrack remains unchanged and the engine receives no EOS feedback.

## D5 comparison and selected codec

Payload sizes below are **calculated from measured cue sample counts and
exact block rounding per track**, not measured final cartridge sizes. All56
spans, including original57's near silence, are counted. Metadata, directory,
CRC records and DMA alignment are additional. Stereo approximately doubles
these payloads and does not fit the20MiB music row at these settings.

| Mono rate | SPU16/28 | IMA WAV512/1017 | VADPCM4(9/16) | VADPCM3(7/16) |
|---|---:|---:|---:|---:|
|16000Hz|15512000|13677568|15269454|11876242|
|22050Hz|21377200|18846208|21043134|16366882|

Corresponding bytes/second before last-block padding: SPU9142.857/12600;
IMA8055.064/11101.475; VADPCM4 9000/12403.125; VADPCM3 7000/9646.875.
SPU22.05 exceeds20MiB by405680 bytes; IMA22.05 leaves2125312 bytes before
container overhead. VADPCM3 numbers are a size option, not a quality claim.

Actual encoded/independently FFmpeg-decoded snippets, SNR dB against the
**same-rate downmixed reference**, not44.1kHz stereo original:

| Track / measured seconds | SPU16k | IMA16k | SPU22.05k | IMA22.05k |
|---|---:|---:|---:|---:|
|02 /30|34.33|25.94|36.68|28.97|
|05 /30|29.68|28.60|29.12|28.51|
|13 /5.04 (whole track)|39.05|33.76|40.96|36.55|
|22 /30|26.84|19.87|28.39|22.23|

Track22 IMA22.05 peak error12539 versusSPU2370 is a transient-error warning,
not a listening result. No subjective listening judgement is claimed. SNR
excludes downmix/bandwidth loss; silence has undefined SNR, not infinite
quality. Cart decode costs and RSP contention must come from the actual
production bridge scene; existing effect-mixer timings are not music costs.

Main selected SPU ADPCM,22050Hz mono for D5: better measured quality/transients,
reuse of the existing effect decoder, and below1ms mean CPU demand on the
production bridge scene. Main explicitly accepted the1186us peak frame.
The RSP/VADPCM and Opus comparisons are not claimed or required by that decision;
the IMA cook/runtime candidate has been removed, not retained as an alias.

Shipping02..56 and the explicit unavailable57 removes2482208B of encoded
near-silence from the SPU22.05 projection:18894992B of actual audio payload,
plus55 native headers and one64B unavailable record, before pack overhead.

## Native wire contract

MUSIC is TR6P kind0x60000. Entries are MUSIC_02..MUSIC_57; absent extended
ambience ids58/59 and an unavailable57 are distinct from malformed content.
All container blocks are raw (compression='none'). Header is its own physical
part; audio parts are <=4096B and preserve complete codec frames. Data never
uses pack rebinding or filesystem reads while SFX is active.

Header64B, `>4s15I`: MU64, version1, total bytes, flags, requested track id,
codec, rate, channels, exact decoded frames, data offset, data bytes,
codec block bytes, codec block frames, then three reserved zero words.
Flags bit0 means unavailable; all other bits reject. An unavailable entry is
exactly64B and all codec/rate/channel/frame/data/block/reserved words are zero.
An available entry has flags0, channels1 and rate16000 or22050.

- Codec1 SPU:16B/28 samples, standard signed nibbles, low nibble first;
  predictors0..4, shift0..12, flags0. Exact frames trim final padded samples.

No loop flags are baked into source audio: transport knows its level ambient
id and rewinds a completed stream with reset codec history. Metadata validates
exact byte counts, offsets, codec units and zero/reserved fields before output.
Other codec ids reject. The existing SPU recurrence serves both source releases.

## I/O and memory contract

Core's resident catalog/query retains56*16=896B of track table
locations/checksums; only the current track's block index is kept (16 B/block
while queried, 8 B/block once its partition is checked).
A track change asynchronously verifies that track's64B wire records through
engine/io, without a cold-unit call, level plan, pack rebind or heap call.
The largest shipped track uses600 records (599 payload+header),38400B of wire
table: at least two idle32KiB service frames, more with effect contention.
Do not call this pure music bitrate or hide its startup latency.

At22050Hz mono SPU, steady data is12600B/s =420B per30Hz tick. This is an
average, not peak reads: physical parts are4096B except the final partial part.
A refill leaves28672B of the shared frame budget if no other music metadata
work runs that frame. All three windows, decoder state, current index, catalog and
output are boot-owned in the unchanged128KiB audio row. Level-start priming
runs while loading; cue switches later stay asynchronous within play's budget.
No SDK mixer init, heap allocation, code-unit load or filesystem access is
allowed from a pull, an interrupt or a playback refill.

## Complete measured CD-DA duration table

Durations are cue sample spans; references are static source requests, not a
claim that a scripted route executed every trigger. Of49 referenced available
original ids,239.666667s are the title/caves loop sources and918.12s are
one-shots:1157.786667s total (19.296444min). The six other original tracks
are retained by the cook, but not referenced by the current fixed controls.

| CD track | Seconds | Fixed-game use |
|---|---:|---|
|02|194.573333|not referenced by inventoried controls|
|03|61.973333|tutorial/floor/enemy one-shot|
|04|121.520000|title loop|
|05|118.146667|caves loop and floor requests|
|06|74.986667|tutorial/floor/enemy one-shot|
|07|53.880000|tutorial/floor/enemy one-shot|
|08|66.253333|tutorial/floor/enemy one-shot|
|09|38.080000|tutorial/floor/enemy one-shot|
|10|77.280000|tutorial/floor/enemy one-shot|
|11|6.493333|tutorial/floor/enemy one-shot|
|12|12.986667|tutorial/floor/enemy one-shot|
|13|5.040000|secret cue|
|14|10.066667|not referenced by inventoried controls|
|15|9.040000|tutorial/floor/enemy one-shot|
|16|36.840000|tutorial/floor/enemy one-shot|
|17|40.853333|tutorial/floor/enemy one-shot|
|18|23.320000|tutorial/floor/enemy one-shot|
|19|86.600000|not referenced by inventoried controls|
|20|52.226667|tutorial/floor/enemy one-shot|
|21|39.226667|not referenced by inventoried controls|
|22|61.506667|CUT4|
|23|55.586667|CUT1|
|24|13.373333|CUT3|
|25|34.840000|CUT2|
|26|5.013333|tutorial/floor/enemy one-shot|
|27|5.013333|tutorial/floor/enemy one-shot|
|28|5.000000|tutorial/floor/enemy one-shot|
|29|5.013333|tutorial/floor/enemy one-shot|
|30|5.973333|tutorial/floor/enemy one-shot|
|31|5.013333|tutorial/floor/enemy one-shot|
|32|7.506667|tutorial/floor/enemy one-shot|
|33|7.560000|tutorial/floor/enemy one-shot|
|34|5.626667|tutorial/floor/enemy one-shot|
|35|5.186667|tutorial/floor/enemy one-shot|
|36|14.946667|tutorial/floor/enemy one-shot|
|37|5.013333|tutorial/floor/enemy one-shot|
|38|5.013333|tutorial/floor/enemy one-shot|
|39|14.080000|tutorial/floor/enemy one-shot|
|40|8.533333|tutorial/floor/enemy one-shot|
|41|5.013333|tutorial/floor/enemy one-shot|
|42|5.000000|tutorial/floor/enemy one-shot|
|43|7.173333|tutorial/floor/enemy one-shot|
|44|6.320000|not referenced by inventoried controls|
|45|7.880000|tutorial/floor/enemy one-shot|
|46|5.013333|not referenced by inventoried controls|
|47|5.013333|tutorial/floor/enemy one-shot|
|48|5.013333|tutorial/floor/enemy one-shot|
|49|13.946667|tutorial/floor/enemy one-shot|
|50|5.000000|tutorial/floor/enemy one-shot|
|51|5.000000|tutorial/floor/enemy one-shot|
|52|5.013333|tutorial/floor/enemy one-shot|
|53|7.266667|tutorial/floor/enemy one-shot|
|54|5.013333|tutorial/floor/enemy one-shot|
|55|7.720000|tutorial/floor/enemy one-shot|
|56|5.013333|tutorial/floor/enemy one-shot|
|57|197.000000|near-silent padding; NOT ambience57|

Floor references in both original level sets: n64_music_inventory.py walked
all43 banks, including cut/title/gym and PC Snowcut. Its JSON records source
SHA256 and every referenced word offset. Literal floor-track union is
`3,4,5,6,7,8,9,10,11,12,13,15,16,17,18,20,26,27,28,30,31,32,33,34,35,36,37,38,39,40,41,42,45,47,48,49,50,51,52,53,55`.
Add tutorial remaps29/43, direct enemy requests54/56, cutscene ids22..25,
and unavailable ambient57..59. Track02,14,19,21,44,46 are not in these
controls; lack of a reference is not a universal runtime reachability proof.


## Production playback measurements

Both codec candidates ran the actual bridge renderer, game, SFX and shared AI
bus at stock4194304B in ares:180ticks,101 room14 frames,133056 music output
frames at22047Hz DAC. The final screenshots are byte-identical between them.

| Candidate | Pull total us | CPU demand us per30Hz | Peak buffer us | Peak actual frame us |
|---|---:|---:|---:|---:|
|SPU selected|122162|674.73|598|1186|
|IMA removed|180415|996.48|804|1785|

CPU demand is(total_us/output_frames)*actual_DAC/30, not total/tick or an
unproved worst-case bound. It includes decoder/resampler/output production;
the common bus addition/final saturation belongs to the SFX mixer's telemetry.
No standalone codec kernel or alternate ROM link supplied these measurements.
Logs, summaries and the compared source snapshot are retained under
`$TR64_WORK/evidence/Media/bridge-candidates/`.

The compared build charged20000B Music storage inside the one existing boot
audio allocation:125808/131072B total,4 boot allocations,0 after boot.
This includes600 compact block records,56 directory references,two4096B
windows,448 mono output frames and decoder/query/transport state. The linked
resident-image gate was487096/524288B. Music and SFX stream underruns and
the physical AI-empty audit were zero in both bridge runs.115 pre-headroom
clipped samples were reported, not hidden. Main subsequently chose common
one-half bus headroom; Music remains unity relative to source-normalized SFX
before that common6.02dB reduction. No unsupported music-only attenuation or
loudness normalization is baked into the source cook.

The real LEVEL1 start host scene produced132300 stereo WAV frames at22050Hz,
one Music start and zero underruns. The real secret at room36 sector6
(Z-major indexing: x89600,y1024,z37376) triggers13 and resumes5 after its
end: three starts, zero underruns in the210-tick host scene. No game request,
trace/hash or RNG was changed on completion. The music option mutes the bus
without stopping its sample clock/decoder history.

Canonical17-level input plus55 real tracks and unavailable57 is composed by
Core, not a second runtime binding. The published pack is33903888B at
`$TR64_WORK/builds/Core/music-campaign/pc-campaign-spu.pack`, SHA256
`e7bb7eb673c5673a5c6ce2266d5847d2fe6fe67841d55988d75302426f2cf1b8`.
It leaves16427760B after reserving16MiB video in a64MiB cart, before the
linked image. Final ROM bytes and post-headroom scene gates are separate
measurements; the pack-size arithmetic is not a substitute for them.


### Shipped music slice gates (before review follow-up)

After removing IMA and merging Main's M5 game API plus Audio's shared half bus,
both `test/media/level1-start.route` and `level1-secret.route` pass on host
and stock4MiB ares. The secret scene retains210 ticks; its camera faces back
into the secret room (yaw-32768) rather than through the adjacent hall, keeping
the functional software-render scene within the existing8s host deadline.
No harness deadline, cart limit or observable was relaxed. Cold builds were
prewarmed with ordinary `make -j2 ... host rom` after the harness's120s serial
build deadline was reached; tests still used the unchanged scene runner.

Final selected-codec storage is19984B; total Audio with spatial effects is
127584/131072B,4 boot allocations,0 after boot,leaving3488B. Start and secret
cart scenes report zero Music/SFX/physical-AI underruns and zero clipped samples.
The secret cart reports three starts and current5 after13 finishes. The final
host WAVs are nonzero,centred stereo:132300 frames for start (peak1966) and
154350 frames for secret (peak10317),both22050Hz. PCM hashes/RMS are in
`$TR64_WORK/evidence/Media/final-live/host-wav.json`.

The production live-pad gate also passes200 ticks, including restart and
LEVEL1->LEVEL2->LEVEL1 through the real teardown/reload path. Music stop drains
before every cart rebind and resumes after each load; all recorded Music,SFX
and physical-AI underrun counters and clip counts are zero. This is not an
injected transport restart or a Core-only reload. Evidence:
`$TR64_WORK/evidence/Media/final-live/{summary,music}.json` and `ares.log`.

Exact gates exercised:

```
PYTHONPATH=tools/cook python3 test/media/test_source.py
PYTHONPATH=tools/cook python3 test/media/test_native.py --out $TR64_WORK/builds/Media/native
test/run/scene.sh --host test/media/level1-start.route
test/run/scene.sh --host test/media/level1-secret.route
test/run/scene.sh --ares test/media/level1-start.route
test/run/scene.sh --ares test/media/level1-secret.route
make -j2 BUILD_DIR=$TR64_WORK/builds/Media/final-live TR64_PLAY=1 TR64_TEST=0 TR64_LEVEL_PACK=$TR64_WORK/builds/Core/music-campaign/pc-campaign-spu.pack rom
python3 tools/ship/boot.py --rom $TR64_WORK/builds/Media/final-live/tr64.z64 --out $TR64_WORK/evidence/Media/final-live --reload --sound
```

Commands were run with the RTK prefix, TMPDIR=$TR64_WORK/tmp/Media;
scene BUILD_DIR=$TR64_WORK/builds/Media/final and the same campaign pack.
To repeat the live boot command with the current harness, pass the same campaign pack as `--pack`.
The five source controls and three selected-codec native controls pass. Final
scene artifacts are under that build's `scenes/{host,ares}/music-level1-*`.

The live shipping ROM is34963456B (33.34375MiB),resident image448712B of512KiB.
There are32145408 cart bytes free,or15368192B after reserving16MiB for video.
This is a measured linked ROM size,not only the pack-size projection.
No controlled net image-growth baseline,subjective listening judgement,
full118s retail loop,PC-CD extraction on an unavailable PC image,or real N64
hardware is claimed. Video playback remains gated on Main's video decision.


### Music review follow-up

The request snapshot is 16 B: DIRECT 0 and RESTORE 1. GameLead stopped publishing
an implicit-1 stop every GYM tick. On RESTORE, transport selects the level's
ambient id (or silence for GYM), retaining the exact source/save/trace/hash.
It cannot infer whether a saved cue had already finished, so it never replays
a non-ambient saved cue; a fresh direct cue request still plays normally.

Playback handles position >= frames before prefetch: an exact 4096 B data
partition ending on a 448-frame output boundary no longer asks for block_count.
Regressions cover 14336 frames (512 SPU units, two complete 4096 B data parts)
as both a one-shot and a looping ambient. On the one-shot the previous code stops
with `music position outside stream`; the ambient case fails the exact-EOF check.

At each track bind, the full wire CRC and every native contiguous/regular
partition are checked before its 600-slot index is compacted from 16 B records
to 8 B ROM/CRC pairs. Logical offsets and lengths are reconstructed exactly
from the checked 4096 B partition and total extent. The released upper 4800 B
of the 9600 B record array carry a third 4096 B window (`music_index_t`); it is
inaccessible during QUERY/HEADER and all three requests are drained before
another query may overwrite that storage. Loop block 1 is requested on
entering the second-to-last data block, not just the short final block; the
window that receives it is only replaced when a read needs a victim, so block 1
was observed to stay resident across later loops of the 4- and 5-block tracks.
Duplicate targets on one/two-part tracks are deduplicated.

The music allocation is unchanged at **19,984 B** (host `-m32` and cart
`music-boot: bytes=19984`; before this change it would have grown to 20,048 B
with the third window): window records shrank to 24 B (16-bit block, byte flags,
window data derived from the index) and the byte flags moved into the padding
before the 16-byte-aligned output buffer. The cart `level1-start` run reports
`audioheap: used=127584 ... calls=4 post_boot_allocations=0`, unchanged.

22050 Hz music plays 1:1 when the actual DAC is within 8 Hz of the 22050 Hz
request, avoiding the old one-sample drop every 8192 samples at 22047 Hz. Other
DAC rates and 16000 Hz streams retain their Q16 resampling step. The pitch/time
change is the tiny actual DAC-divider error, not a periodic sample discontinuity.

`media-transport-test` uses the real Music, query, stream, IO and filetable code
with poisoned asynchronous input and competing 4 KiB reads. Besides the cases
above it asserts that no loop-block-1 read is issued in the last block
(`loop_late=0`; the previous code reports 2 on the same pack), and plays a
synthetic 599-data-block track (every unit distinct) through two loops so
the full 600-slot compacted index and the overlaid window are exercised. The
optional `--catalog /external/manifest.json` matrix checks original tracks as
cues, both ambience loops, 22047 Hz seams, 40 level cycles, unavailable tracks,
restore policy and 16k resampling. It compares decoded samples, not only
counters; the modeled clock is explicitly not cart CPU or physical-DAC evidence.

The startup_peak_us field is request-to-PLAY **elapsed latency**,including
asynchronous read/frame waits. At level start the synchronous prime runs
while loading,before gameplay begins. Cue changes use nonblocking query/read/
poll during play; an11.8ms startup_peak is not an11.8ms decoder call in a
gameplay frame. It can delay a cue or briefly leave the transport priming.
Query/CRC CPU work belongs to the whole audio/frame measurements,not the
pull-only frame_peak_us field.

