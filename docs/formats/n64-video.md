# Video: native streams, player and measurements (ABI 1)

## Native pack entries

Per video NAME (ESCAPE, CORE, CAFE, MANSION, SNOW, LIFT, VISION, CANYON,
PYRAMID, PRISON, END; the PlayStation disc has no ESCAPE):

- `VIDEO_<NAME>`, kind `0x70000`: one raw H.264 elementary stream in 16 KiB
  blocks, never compressed. 320x240, 15 frames a second (VUI timing 1/30 per
  field pair), Main profile with **CAVLC** (CABAC off: the SDK decoder asserts on it), 8x8
  transform off, B-frames off, deblocking off, one slice per picture, **one
  reference frame** (the decoder sizes its picture buffer by this: 8 references
  allocated 13 pictures and overflowed the arena), weighted prediction for P
  slices only, BT.709 full range. One SPS and one PPS, repeated unchanged at each
  keyframe by the encoder. The first NAL after SPS/PPS is the SDK's `LDSZ` SEI (maximum slice
  size, big endian), without which its 64 KiB input window cannot refill safely.
  The cook (`tools/cook/n64_video.py`, checks in `n64_video_h264.py`) parses the
  produced stream (SPS, PPS and every slice header, not the encoder's flags) and rejects by name:
  a resolution or frame rate other than 320x240 at 15, cropping or interlacing, a profile
  other than Baseline/Main/Extended, `num_ref_frames` other than 1 on any SPS, CABAC, an 8x8 transform,
  slice groups, B or SP/SI slices, more than one slice per picture, deblocking on, a colour
  description other than BT.709 full range, more than one different SPS or PPS (each different set the
  decoder meets stays allocated: the arena would grow), a first picture that is not an IDR, a
  NAL type outside slice/IDR/SEI/SPS/PPS, and a missing, malformed or too small LDSZ.
  Advertised slice size: the SDK refills its window when `left <= LDSZ` and
  `h264bsdExtractNalUnit` ends a NAL at the end of the window if the next start code is not wholly
  inside it, swallowing the zeros it saw. A window that ends 1 to 3 bytes into a start code therefore
  starts the next window mid start code, which is parsed as one raw NAL: up to 64 KiB of stream is
  lost. LDSZ is the largest NAL (start code included, up to the next start code) **plus 4**, so
  the refill comes first, and at most 65,536 (the cook rejects a larger NAL). A model of the SDK's
  window and extractor (`test/media/test_video_cook.py`) shows a stream with the biggest NAL 1 to 3
  bytes short of the window end losing everything after it with the old value and parsing whole with
  the new; the cart A/B is in "Measured".
- `FILM_<NAME>`, kind `0x60000`: an MU64 stream (SPU ADPCM, mono, **16 kHz**:
  22.05 kHz would not fit the 16 MiB budget) with track id 60, which is the film
  sound and not a game request. Source stereo audio is mixed down.
- Source: PC `*.rpl` (Escape124 + 8-bit PCM) or the PlayStation disc's
  `*.FMV` (STR with XA, extracted as complete 2352-byte sectors); both cooked by
  one tool; 320x120 and 320x208 sources are stretched to 320x240. The PlayStation
  path was first run on CORELOGO: 152 pictures (10.13 s) and 12.8 s of XA sound (video
  ends first; the sound is cut with the film), and since on all ten files (see
  "PlayStation pack" below). Encoded with FFmpeg libx264
  (`slower`, CRF 32, `-maxrate 480k`); results are external data.
- `tools/cook/n64_video_pack.py` makes a video-only pack; `pack_level.py
  --video-catalog manifest.json` puts the same streams into a level pack.

## Player

`tr64_video_play(id, max_ticks, report)` (`engine/media/cinema.c`, resident):
binds the configured pack's raw range, mounts only its directory, copies the two
stream references, borrows the empty 1,472 KiB level arena (`mem_video_begin`),
loads the `video` code unit, calls it once, unloads it and returns the arena.
`engine/media/film.c` (the unit) loads both stream indexes, runs the SDK decoder
(libdragon 0822b98: `video_allocator_t`, `video_source_t`, `yuv_set_interleave_buffer`)
with every allocation taken from the arena (`engine/media/film_arena.h`: a bump allocator; a
freed block below 64 KiB is reused for the next request of the same size, because the decoder allocates and
frees a 960 B parameter-set block at every keyframe and without reuse the arena grew 960 B per keyframe, headroom
about 521; pictures keep their guard bands and are never reused; a double or foreign free and a full
block table stop by name) and every stream byte from an 8 x 16 KiB read-ahead window over
`engine/io` (32 KiB a frame budget respected). The decoder
runs ahead one picture (`buffered_pics=1`); while it works or waits the unit keeps
`engine/io` and the audio mixer serviced. Pictures are converted by the RSP/RDP
YUV blitter (`yuv_tex_blit`, no rspq block, no heap) and waited for with the RDP
interrupt before the decoder may reuse the picture. Film sound
(`engine/media/film_audio.c`) is pulled by the mixer beside music
(`tr64_film_mono_pcm`, ignores the music setting: it is the film's soundtrack).
Clock: the film clock is the audio position minus a constant 40.64 ms mixer lead
(**an estimate, unmeasured**); a picture a whole period late is dropped, never
shown late; after the sound ends the wall clock continues. START skips. Every wait stops by name after
8 s: for the index, a stream block, the first picture, a display buffer (`video stalled waiting for a
display buffer`), the RDP (`... for the RDP`) and the film clock itself (`video film clock stalled at <us>
us`: the film sound is never pulled, or underruns for good: the clock is the sound's position).
Which video plays when is `tr64_video_transition` (`engine/media/transition.c`, resident), called by
`tr64_play_run` between `eng_level_release` and the next load. The table names every level of the play
loop and is derived from the source game's flow (OpenLara `gameflow.h` `getGameVideo`: a level's video
plays when that level starts): boot ESCAPE, CORE and, before TITLE, CAFE; before GYM MANSION, LEVEL1 SNOW,
LEVEL4 LIFT, LEVEL8A VISION, LEVEL10A CANYON, LEVEL10B PYRAMID, **CUT4 PRISON** (PRISON belongs to the
fourth cutscene level, so it plays between LEVEL10B and CUT4, not before LEVEL10C); LEVEL10C to title END.
CUT1 to CUT3 and every other level show none; the cutscene levels are ordinary entries of the play loop.
An unknown level name is a named fatal. `make media-transition-test` plays the whole campaign from boot
to END through the policy on the host and checks the order, `play.c`'s level list and the negatives.
Only a campaign advance plays one, never a restart, a save load, a level select or a demo
selection. A pack without the video plays nothing. The hook returns how many videos it showed;
after any, the play loop takes the pad again: a START still held (the skip) is not an edge and
not SELECT for the level that follows.

## Scenes

Host tests (no ares): `make media-transition-test` (the policy over a whole campaign, boot to END, against
`play.c`'s level list), `make media-arena-test` (the unit's arena: 5,000 parameter-set cycles at a constant
peak, guards, misuse), `make media-cook-test` (the cook's checks on real libx264 streams and the model of the
SDK's input window).

Route `probe video`: `load level <name>` names the video (lower case), `seed` is a tick
limit (0 = to the end). `test/media/video-*.route`, built with
`TR64_SCENE_TEST_SOURCES=test/media/video-probe.c TR64_TEST=0 TR64_LEVEL_PACK=<pack>`.
The probe checks level, code, I/O and state pools are back to zero, no unit mapped,
no request pending. The host film has a test clock (`tr64_film_idle`, `sys_host_clock_skip`): when the unit has nothing to do
until the next 30 Hz service tick it skips the time instead of waiting, so a film plays as fast as FFmpeg
decodes it (CORE, 11 s of film: well under a second), and `wall_us` of a host run is film time, not elapsed time. The host decoder is FFmpeg on the stream the engine read: the
SDK's pure-C path is not usable (first IDR errors), so host runs prove delivery,
timing and accounting, not the SDK decoder.

## Measured (ares, 4 MiB, expansion pak off)

### All eleven videos whole

Scene `test/media/video-all.route` (`probe video`, `load level all`): the eleven videos in id
order, one after the other, no level resident, PC cook, `ARES_EXPANSION_PAK=false`, ROM with
`pc-campaign21-video.pack` (below). **In two runs**: the host's load average was about 30 on 4 cores,
which made ares about 12 times slower than real time (4.7 times unloaded), so the one run would have
passed its 6,000 s deadline inside CANYON. Run 1 (`video-all`) played ESCAPE to VISION, and I
stopped it in CANYON; run 2 (`test/media/video-canyon-end.route`, `load level canyon-on`:
the probe plays that video and every later one) played CANYON, PYRAMID, PRISON and END and PASSED
(ares wall 3,203 s). Together: 11,010 pictures, every one shown, **none dropped, no audio underruns**, level,
code, I/O and state pools back to zero after each video (the probe checks it and run 2's verdict
includes it), the `video` unit 22 pages, no unit mapped. "Over 66.7 ms" counts pictures whose decoder
work exceeded one picture period; the longest run of consecutive such pictures is the next column.
Decoder work is each decode step's time minus the time spent inside the decoder's stream reads
(`source_read`: waiting for a block, servicing audio and I/O, the copy), so it is the SDK decoder's own
time; the first table (m4) counted those reads too (means and maxima moved by under 2 ms, the slow-picture counts by at most 1). Sync error is
picture time minus audio time (always at or after the audio, never early).

| Video | Film | Shown / dropped | Decode mean / max (ms) | Over 66.7 ms | Longest slow run | Underruns | Sync error (ms) | Arena peak (B) | Warnings |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| ESCAPE | 4.00 s | 60 / 0 | 22.5 / 30.2 | 0 | 0 | 0 | 0..+20 | 1,007,376 | 0 |
| CORE | 11.00 s | 165 / 0 | 19.1 / 32.7 | 0 | 0 | 0 | 0..+20 | 1,008,336 | 0 |
| CAFE | 104.00 s | 1560 / 0 | 18.5 / 68.2 | 1 | 1 | 0 | 0..+20 | 1,008,336 | 0 |
| MANSION | 11.77 s | 180 / 0 | 26.3 / 62.4 | 0 | 0 | 0 | 0..+20 | 1,008,336 | 0 |
| SNOW | 102.00 s | 1530 / 0 | 18.7 / 87.2 | 9 | 1 | 0 | 0..+32 | 1,008,336 | 0 |
| LIFT | 103.00 s | 1545 / 0 | 19.4 / 72.2 | 2 | 1 | 0 | 0..+28 | 1,008,336 | 0 |
| VISION | 54.00 s | 810 / 0 | 26.5 / 68.9 | 1 | 1 | 0 | 0..+20 | 1,008,336 | 0 |
| CANYON | 180.98 s | 2715 / 0 | 21.0 / 92.3 | 16 | 1 | 0 | 0..+41 | 1,008,336 | 0 |
| PYRAMID | 8.61 s | 135 / 0 | 17.4 / 42.9 | 0 | 0 | 0 | 0..+20 | 1,008,336 | 0 |
| PRISON | 92.07 s | 1395 / 0 | 19.5 / 72.3 | 2 | 1 | 0 | 0..+25 | 1,008,336 | 0 |
| END | 60.06 s | 915 / 0 | 20.5 / 68.4 | 1 | 1 | 0 | 0..+20 | 1,008,336 | 0 |

Why nothing drops: the decoder runs one picture ahead (`buffered_pics=1`), so one slow
picture is absorbed by the next picture's slack, and no video ever had two slow pictures in a
row (longest run 1 everywhere). The worst single picture is CANYON's 92.3 ms (25 ms over the
period plus the lead), and the largest sync error +41 ms is that same catch-up. No bitrate or
keyframe setting needs to change. Arena peak includes the guard bands (below); the arena is
1,507,328 B. **The peak is now the same for every video**: 1,007,376 B (ESCAPE, one parameter-set
block) or 1,008,336 B (the others: one more 960 B block, allocated at the second keyframe, freed and
reused at every later one; CANYON's 38 keyframes made 1,042,112 B before the arena reused freed
blocks, and the old growth was 960 B a keyframe). The 784 B above the old 1,006,592 B base are
the 64-entry block table of `film_arena.h` (64 x 12 B and a 16 B header) in the unit's state. Stream index plus decoder open
cost 140 ms and 9 ms; the unit is 22 of the 24 code frames (90,112 B). The ares run takes about
4.7 times the film's length unloaded (CANYON: 843 s for 181 s in the m4 run), so these pictures take real time on
the host only because it emulates; the cart numbers are the emulated microsecond counters.
Log lines: `$TR64_WORK/evidence/m6/video3/census-run{1,2}-lines.txt` (the m4 table's:
`$TR64_WORK/evidence/m4-video/census-video-lines.txt`).

### Advertised slice size: before and after on the cart

`test/media/window_case.py` crafts one stream from CORE's pictures (unchanged): filler NALs (type 12,
ignored by the decoder) in front of its 41st picture, which is padded with trailing zero bytes to
5,000 B (the largest NAL), so the SDK's 64 KiB input window begins that picture with 2 bytes more than
its size; then the same stream twice, differing in the LDSZ SEI by one byte: 5,004 (the cook's rule)
in the ESCAPE slot and 5,000 (the old rule) in the CORE slot of an experiment pack (never shipped; the
cook rejects both streams). `video-all.route` on a ROM with it, ares 4 MiB: **LDSZ 5,004: 165 of 165
pictures shown, none dropped; LDSZ 5,000: `tr64 fatal: src/video/h264.c:369 poll ASSERT !"h264 status
error"`** while decoding CORE. The model of the SDK's window in `test/media/test_video_cook.py` predicts
exactly this: the NAL at 5,000 B is cut at the window end inside the next start code, the next window
begins `00 01` and is one raw NAL (52 of 179 NALs found, everything from byte 65,536 lost); with 5,004
the refill comes first and all 179 are found. The model also shows the case for a window end 1, 2 or 3
bytes past the biggest NAL and never otherwise. The shipped eleven streams did not meet it (all census
passes), which depends only on where their NALs fall. Lines:
`$TR64_WORK/evidence/m6/video3/window-case-lines.txt`.

### The flow on the cart

`tools/ship/boot.py --video` (ROM built with `TR64_PLAY=1 TR64_TEST=0` and the campaign pack
with videos): boot, ESCAPE (60 pictures) and CORE (165) played to the end, CAFE skipped with
START 30 s of wall time in (about 3 s of film: 41 to 69 pictures were shown first), title, B
(passport), B (new game), SNOW skipped the same way, LEVEL1, forward run, ring open and close.
Gate: no fatal line, the four results `done done skipped skipped`, at least 10 pictures shown of
each skipped video, **START held until LEVEL1 has ticked and no ring menu line in that window**
(the level must not see the held START as SELECT: `tr64_play_run` takes the pad after any video,
`previous` and `hold_select`), the LEVEL1 start hash printed (`play: start_hash=level1`). Rerun on the m6-video3 ROM (build E03DAFB0B699, pack
`ec7d96d2...f7ff98`, `--video --reload --sound`, no cutscene stage, 4,500 s): pass, no fatal, `done done skipped
skipped`, levels `title level1 level1 level2 level1`, sound pass, title hash 76b0a313 and LEVEL1 hash 6ae75006 as
before (`$TR64_WORK/evidence/m6/video3/boot-flow-summary.json`).
The shipped gate is `TR64_VIDEO=1 tools/ship/ship.sh check`. PRISON (between LEVEL10B and CUT4) needs a
campaign advance, which a level select is not, so no flow gate reaches it: the host policy test and the
all-videos scene cover it. Compared with a ROM of the same sources and the 17-level pack without the
videos (`pc-campaign-spu.pack`, before the cutscene levels were merged): LEVEL1's start hash **6ae75006 in both**, and every `pool:` /
`level-section:` line at LEVEL1's ready is identical in `used` (peaks differ: the video unit's 90,112 B
of code, the borrowed arena and the I/O window show in `peak`). After each video
`code`, `level`, `state` and `io` are `used=0`. The title's hash 76b0a313 is also the same.
Negative control: the same gate on a ROM with the pad re-read removed fails (`video_gate` false): LEVEL1's
first ticks log `input=0800` (SELECT) and the ring opens (`ui: menu ... state=1`, `state=4`); with it the first
ticks are `input=0000` and no menu line appears.
Pictures (ESCAPE, CORE, CAFE, title, SNOW, LEVEL1): `$TR64_WORK/evidence/m4-video/flow-sheet.png`.

### RSP "not cache coherent" warnings: cause and fix

ares (homebrew mode) taints every DMEM word the RSP DMAs from RDRAM while the CPU data cache holds
that line dirty and reports the later RSP read. The report names the CPU PCs that wrote and loaded
the line and the RSP PCs of the DMA and of the read; resolving them (`addr2line` on the ELF,
the RSP ucode reassembled with the SDK's rules) gave: CPU writes in the decoder's bookkeeping
(`h264bsdMarkDecRefPic`, `ShellSort`: DPB output list; `h264bsdResetStorage`, `SetMbParams`: the
macroblock table), RSP DMA in `Interpolate_Bootstrap_Phase2` and `LumaInterpolateDiag` (the
source rows of motion compensation), RSP read in `OverfillVertical` and the diagonal filter.
The RDRAM addresses were inside the arena blocks next to a picture: the 144 B DPB table just
above the first picture, the 48 B output list just below the last (CORE), the macroblock
table a few rows above it (CAFE). Mechanism:
the macroblock DMA reads whole rows of 21 (or more) lines, starting above the picture's first row and
running below its last one when a motion vector points off the frame; the ucode then overwrites those
rows with the replicated edge row (the overfill), but its vector loads still read the stale bytes
beside them. Nothing the RSP needs is missing a writeback: the lanes read are never used.
Proof: with a per-picture FNV hash of Y, U and V read after `rspq_wait()`, all 165 CORE pictures are
bit-identical to FFmpeg's decode of the same stream **with and without the fix**; the warnings
are 15 without it and 0 with it. (A first hash taken without draining the RSP differed on a dozen
pictures: that was the check reading while the RSP wrote, not the decoder. The player never reads
a picture on the CPU.) Fix, in the player (`decoder_alloc`, `engine/media/film.c`): every block of picture
size or larger gets a `VIDEO_GUARD` of 16 KiB before and after (the first fix, 1 KiB, left two warnings in
CAFE: a vector 8 rows off the top; x264 allows 24 rows plus the 6-tap margin, 27 rows of 320 B =
8,640 B). With 16 KiB: 0 warnings in all eleven videos, CAFE's skipped flow included, and the
flow run. No change in the SDK was needed. The SDK hands a finished picture on without its own
`rspq_wait()` (`h264.c`: `poll` returns at PIC_RDY); the blit and the next picture's decode are in the
same rspq queue behind the macroblock tasks, so order holds.

### Sizes

Resident image 529,560 B of the 557,056 B (544 KiB) row, 27,496 B free (video: RSP microcode and
glue). ROM with the 21-level pack and videos: 52,002,816 B of 67,108,864 B. Cooked video 9,321,932 B
plus film sound 6,688,784 B = 16,010,716 B (15.27 MiB of the 16 MiB row). The campaign pack:
`pc-campaign21-video.pack` (21 levels, SFX, 55 music tracks, 11 videos, 99 entries) 50,938,912 B,
SHA256 `ec7d96d2dcb1435b7f438d410e19888546b2a46f0cd38bbd6d55f4cc59f7ff98` (the m4 pack, `9b161f3b...b4848`, differs
in the 11 LDSZ bytes only); the 17-level pack the first measurements used is
50,029,424 B (`ccd10895...3dd3771`). Film sound lead of 40.64 ms is a constant, not separately
measured.

### Pack reproduction

`tools/cook/pack_add_video.py --base <campaign+music pack> --video-catalog <manifest.json> --cache
<dir> --out <pack>` appends the catalog to a composed pack, first rebuilding the base byte for byte
from its own decoded sections and then checking every old section decodes unchanged. Run on
`pc-campaign-spu.pack` (Core's 17-level music pack) it reproduces VideoC's pack exactly (same
SHA256); on `pc-campaign21-spu.pack` it makes the shipping pack. The video catalog (streams cooked
by `n64_video.py` on a host with FFmpeg and libx264) is external data:
`$TR64_WORK/builds/Video/catalog/<sha>/` with its `manifest.json`. The m6-video3 cook changes the
streams only in the LDSZ value, so its catalog was re-derived without the sources (they live on <remote-host>) with
`n64_video.py --from-catalog <old manifest.json> --out <dir>`: the cook's own finishing (`embed_slice_metadata`
drops every SEI and writes its own, `validate` parses the stream) applied to the old streams, the film sound
carried over, the manifest recording `derived_from`; one byte per video differs (offset 73) and a second run
reproduces the catalog byte for byte. Catalog `09c2567f9a8e66dc253d99593a4915c5db4b524021e3dcc2650283e2f7fd51d8`
(`$TR64_WORK/builds/Video3/catalog/`), pack `$TR64_WORK/builds/Video3/pack/pc-campaign21-video.pack`.


### PlayStation pack

`psx-campaign21-video.pack`: the 21 levels cooked from `PSXDATA`, effects (SPU ADPCM), the 55 music
tracks of `psx-campaign-spu.pack` and the ten disc films, made by the commands in `test/game/README.md`
("PlayStation shipping pack"). **Ten films, not eleven**: the disc has no ESCAPE; the tool maps the disc's
`CORELOGO.FMV` to CORE. `result=absent` is logged for ESCAPE at boot and the flow goes on with CORE
(`tr64_video_transition` counts only films shown). Each film's picture count equals the MDEC frame count of
its STR file, 15 pictures a second, 320x240 from 320x208 coded pictures; the disc pictures carry their letterbox
bars, so the picture fills 140 of the 240 lines (ffmpeg `cropdetect` on CAFE and SNOW: `crop=320:140:0:50`), where the PC cook fills the frame (its 320x120 sources are
the bars removed). Cooked on this host (FFmpeg 8.0.1, libx264, SPU encoder built with gcc -O2), 2 min 6 s:

| Film | Pictures | Film s | XA audio s | Video B | Film sound B | Largest slice B |
|---|---:|---:|---:|---:|---:|---:|
| CAFE | 1547 | 103.13 | 104.05 | 593,033 | 951,424 | 5,350 |
| CANYON | 2709 | 180.60 | 181.01 | 1,461,711 | 1,655,056 | 11,270 |
| CORE (CORELOGO) | 152 | 10.13 | 12.80 | 139,931 | 117,104 | 2,174 |
| END | 909 | 60.60 | 60.11 | 449,101 | 549,616 | 4,181 |
| LIFT | 1534 | 102.27 | 103.04 | 634,989 | 942,144 | 7,599 |
| MANSION | 177 | 11.80 | 11.79 | 158,273 | 107,840 | 2,445 |
| PRISON | 1381 | 92.07 | 92.11 | 561,386 | 842,192 | 6,246 |
| PYRAMID | 129 | 8.60 | 8.64 | 40,492 | 79,072 | 1,017 |
| SNOW | 1529 | 101.93 | 103.31 | 638,346 | 944,592 | 8,168 |
| VISION | 810 | 54.00 | 54.99 | 779,505 | 502,800 | 4,893 |

Totals 5,456,767 B of video and 6,691,840 B of film sound (12,148,607 B; PC: 16,010,716 B for eleven films).
Pack: 42,110,688 B, 97 directory entries, 24,998,176 B under 64 MiB; without videos 29,833,888 B (77 entries,
the 17 old levels' sections identical to `psx-campaign-spu.pack`'s). Shipping ROM (`TR64_PLAY=1 TR64_TEST=0`):
43,171,840 B of 67,108,864; resident image 529,368 of 557,056 B (identical to the PC ROM's). The PlayStation CUT1..CUT4 carry
no samples: their SN64 banks are 112 B of metadata and the six sound ids their tables name (108-112, 115) are
listed as unavailable, where the PC banks hold six PCM16 samples (69,344 B).
Gate (`TR64_VIDEO=1 TR64_VIDEO_ABSENT=ESCAPE tools/ship/ship.sh check`): pass, CORE done (152/152 shown, none dropped),
CAFE, SNOW and PRISON skipped with at least 22 pictures shown, no audio underruns, sound reports with no underruns,
LEVEL1 start hash printed. Pictures: `$TR64_WORK/evidence/m6/psx-ship/`.

## Source survey (Media, before the player)

## Original PC release

The supplied PC tree has eleven RPL files. FFprobe identifies Escape124 video
and unsigned8 PCM stereo44100Hz audio. Durations below are container header
durations; the final cook must retain frame/sample counts, not assume that
an integer header duration proves exact A/V endpoints. Most movies store
320x120 line-doubled video intended for a4:3 display; core is320x208, and the
4s escape file is320x240/25fps. A viewed source contact sheet identifies
escape.rpl as the Eidos logo, not a missing gameplay escape scene.

| File | Source video | Header seconds | H264320x240 bytes | Average PSNR dB |
|---|---|---:|---:|---:|
|cafe.rpl|320x120 @15/1|104|1029120|36.83|
|canyon.rpl|320x120 @15/1|181|2469360|34.51|
|core.rpl|320x208 @15/1|11|122604|36.09|
|end.rpl|320x120 @15/1|61|792674|35.19|
|escape.rpl|320x240 @25/1|4|65759|33.97|
|lift.rpl|320x120 @15/1|103|1022547|35.46|
|mansion.rpl|320x120 @15/1|12|255036|31.95|
|prison.rpl|320x120 @15/1|93|959117|35.03|
|pyramid.rpl|320x120 @15/1|9|105230|36.92|
|snow.rpl|320x120 @15/1|102|1067767|35.46|
|vision.rpl|320x120 @15/1|54|1143120|34.29|

Header durations total734s (12.233333min). All eleven are counted, including
logos and mansion. The source contact sheet is external game data at
`$TR64_WORK/tmp/Media/fmv-source-montage.png`, not committed.

## Original PlayStation release

The disc ISO has ten /FMV/*.FMV files. The common2048B-per-sector extraction
drops XA subheaders/audio and is **not** directly an FFmpeg STR input.
n64_video_source.py extracts the complete2352B sectors from the BIN, validates
duplicate MODE2 subheaders and complete MDEC chunk sets. Its SNOW extraction
is independently recognized by FFprobe as MDEC320x208/15fps plus adpcm_xa.
All ten have320x208 coded video. XA is file1/channel1,4-bit stereo37800Hz.
The BIN is read at CD2x for these files: dividing sector count by75 gives
twice the read duration, not a film duration. XA decoded seconds below count
2016 stereo sample frames per audio sector (18groups*4units*28samples).

| File | MDEC frames | Frame span at15fps, seconds | XA sectors | XA decoded seconds |
|---|---:|---:|---:|---:|
|CAFE.FMV|1547|103.133333|1951|104.053333|
|CANYON.FMV|2709|180.600000|3394|181.013333|
|CORELOGO.FMV|152|10.133333|240|12.800000|
|END.FMV|909|60.600000|1127|60.106667|
|LIFT.FMV|1534|102.266667|1932|103.040000|
|MANSION.FMV|177|11.800000|221|11.786667|
|PRISON.FMV|1381|92.066667|1727|92.106667|
|PYRAMID.FMV|129|8.600000|162|8.640000|
|SNOW.FMV|1529|101.933333|1937|103.306667|
|VISION.FMV|810|54.000000|1031|54.986667|

XA and video endpoints differ; no silent audio truncation or guessed frame
rate correction is part of this survey. Original music is CD-DA separately,
not these XA movie channels. The PC escape logo has no corresponding separate
file in this supplied PlayStation ISO. No codec is inferred from a suffix.

## H264 budget experiment

n64_video_measure.py decodes original RPLs on <remote-host> and encodes H264 baseline,
one reference, no B-frames/CABAC/8x8 transform/deblock/weighted prediction,
15fps, CRF32, slow preset. Source lines are doubled/scaled to4:3 first.
This is not the SDK shipping converter; its slice-size metadata and target
decoder support still need the video gate. PSNR compares against the same
scaled original, so does not score resolution/downscaling loss.

| Target | Measured video payload | Projected mono16k IMA film audio | Combined bytes |
|---|---:|---:|---:|
|320x240/15fps|9032334|5915136|14947470|
|256x192/15fps|6023602|5915136|11938738|
|160x120/15fps|3126899|5915136|9042035|

The320x240 sum14947470B leaves1829746B within16MiB **before container
overhead**. Film audio is an exact-block-rounding **projection from header
durations**, not an encoded/measured A/V multiplex. This budget does not omit
dialogue. No subjective listening or full-picture quality approval is claimed.
A source-frame count check on cafe-320.h264 decoded1560 frames; its SPS has
num_units_in_tick1/time_scale30, giving15frames/s. FFprobe raw elementary
stream r_frame_rate30/avg_frame_rate25 is not used as the presentation clock.

## Target ABI RAM audit (libdragon61d9cab77966)

n64_video_sizes.c includes the pinned SDK translation unit without copying
its implementation. The VR4300/mabi32 compiler emitted these sizeof constants:

| Type | Bytes |
|---|---:|
|h264_t (includes128 queued macroblocks)|209552|
|storage_t|209440|
|per-picture mbStorage_t|188|
|queued macroblockLayer_t|1600|
|SPS /PPS|92 /76|
|DPB picture /output records|44 /16|
|DPB storage|68|

h264_open allocates sizeof(player)+65536 input bytes =275088B. For320x240,
300 macroblocks cost56400B picture state and1200B slice map. With one reference,
no reordering and buffered_pics0, h264bsdInitDpb reserves two pictures:
2*(300*384+47)=230494B YUV420+alignment/overread. Known allocations total
about563.5KB including small lists/parameter records, before other parameter
payloads/RSP/YUV/player glue. One additional buffered picture adds115247B
plus its small descriptors. This is an allocation audit, not a measured pool
peak. The previous TLB example used94208B/23pages for its video unit; the
new engine must measure its actual linked unit instead of adopting that size.

The no-level arena is1507328B; these known costs fit in projection. The
existing SDK player nevertheless calls malloc/free/open/read internally,
and tr64 freezes library allocation after boot. It cannot simply be called.
A Skeleton-owned named no-level borrow and SDK caller-memory/I/O interface
are required; heap thawing, hidden DFS handles and implicit reserve borrowing
are not a proposed solution. Return all borrowed frames on finish or skip.

## Playback ordering evidence ceiling

Fixed common.cpp defines four in-engine CUT levels, not a table of FMV
transitions. Their synchronized CD tracks23,25,24,22 are music, not movie files.
The original-source contact sheet identifies cafe (Calcutta hotel), snow
(Peru arrival), lift (Natla Technologies), vision (Scion vision), canyon
(Natla steals the artifacts), pyramid (island pyramid reveal), prison
(Natla judgment), end (island escape), mansion (home) and the two logos.
A secondary narrative ordering reference is
[Croft Generation original cutscenes](https://www.croftgeneration.com/en/games/tr1/media/cutscenes/).
It is not an authoritative filename-to-level trigger table. Exact transition
dispatch is a PartC prerequisite to establish with the game owner; no guessed
transition table or executable-derived lookup has been committed.

## Reproduction and evidence

Remote originals are read-only. FFmpeg outputs and scripts are confined to
`~/tmp/tr64-media/Media` on <remote-host>. Only JSON/ASM and a small inspected contact
sheet were copied locally; no disc image/RPL set was downloaded. Reports:
`$TR64_WORK/tmp/Media/{source-inventory,str-inventory,video-survey}.json`,
`video-sizes.s`. Shared SDK/compiler caches were reused; no SDK rebuild.
There is no ares video decode-time result yet, by Main's approved sequencing.
