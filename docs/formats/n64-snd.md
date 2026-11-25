# SN64 sound effects, ABI 1

Effects only. Music and video remain separate M4 work. Integers on the cart
are big-endian; offsets are logical-section offsets, never pointers.

## Measured source inventory

`tools/cook/n64_snd_source.py` and `n64_snd_evidence.py --inventory` read all
43 available banks (22 PC,21 PSX), including title/cuts and PC SNOWCUT.
Measurements below are encoded source sample bytes, not whole level bytes;
seconds count decoded frames at the source rate, including SPU terminal
blocks. Source hashes, each sample's frames/rate/loop marker/sound IDs, every
ambient emitter and source flags are recorded in the external JSON reports:
`$TR64_WORK/evidence/Audio/{source-survey,final-inventory}.json`.

| Level | PC samples / bytes / seconds | PSX samples / bytes / seconds |
|---|---:|---:|
| CUT1 | 6 /69618 /3.131 | 0 /0 /0 |
| CUT2 | 6 /69618 /3.131 | 0 /0 /0 |
| CUT3 | 6 /69618 /3.131 | 0 /0 /0 |
| CUT4 | 6 /69618 /3.131 | 0 /0 /0 |
| GYM | 76 /2256438 /171.555 | 51 /194896 /30.936 |
| LEVEL1 | 84 /1202666 /54.369 | 83 /332944 /52.848 |
| LEVEL2 | 90 /1373316 /62.096 | 89 /381600 /60.571 |
| LEVEL3A | 88 /1417524 /64.105 | 87 /393184 /62.410 |
| LEVEL3B | 91 /1414486 /63.961 | 90 /391024 /62.067 |
| LEVEL4 | 99 /1577618 /71.343 | 98 /436128 /69.227 |
| LEVEL5 | 90 /1365200 /61.728 | 89 /373872 /59.345 |
| LEVEL6 | 97 /1513678 /68.447 | 96 /417552 /66.278 |
| LEVEL7A | 93 /1353918 /61.210 | 92 /371984 /59.045 |
| LEVEL7B | 108 /1771120 /80.101 | 106 /443824 /70.448 |
| LEVEL8A | 96 /1439112 /65.068 | 95 /395232 /62.735 |
| LEVEL8B | 92 /1351498 /61.102 | 91 /370176 /58.758 |
| LEVEL8C | 99 /1616168 /73.091 | 97 /400976 /63.647 |
| LEVEL10A | 91 /1606822 /72.684 | 87 /401424 /63.718 |
| LEVEL10B | 99 /1486016 /67.189 | 98 /411136 /65.260 |
| LEVEL10C | 112 /1902282 /86.041 | 110 /510640 /81.054 |
| TITLE | 8 /99410 /5.340 | 7 /26544 /4.213 |
| SNOWCUT | 84 /1202666 /54.369 | absent |

PC WAVE rates are22050 Hz, except CUT1..4(11025),GYM and TITLE(both).
PSX SPU samples are11025 Hz: the permitted OpenLara2333adef
`src/sound.h:500` VAG constructor specifies that rate; all measured VAB active
tones use centre84/fine0 with program n referring to VAG n+1. Samples0..4 in
LEVEL1 also have approximately half as many decoded frames as their PC
counterparts. The cook checks this tone mapping, rather than assuming arbitrary
VAB programs equal physical sample indices.

Loop modes are taken from the TR1 sound-detail flags, not from audio-content
analysis. The following lists are identical between PC/PSX:

| Level | Mode2 loop sound IDs | Ambient-source sound IDs |
|---|---|---|
| LEVEL3A | 79,80,81 | 79,80 |
| LEVEL3B | 79,80 | 79 |
| LEVEL4 | 81 | none |
| LEVEL7A | 81 | none |
| LEVEL7B | 80,81 | 80 |
| LEVEL8A | 118 | none |
| LEVEL8C | 81 | none |
| LEVEL10A | 79,148 | 79,148,167 |
| LEVEL10B | 79,148,154,155 | 148,154,155 |
| LEVEL10C | 148,154,155 | 148,154,155 |

Other banks have no mode2 loops or sound-source records. A non-looping ambient
source is not silently converted into a loop. The core owns the sound map,
details and emitter table; SN64 does not duplicate them.

### Missing PSX source data

LEVEL10A has87 VAGs,87 programs and87 tones. Its unchanged core details78..81
reference missing sample IDs87..90 through sounds173,199,200,204 respectively.
PC has91 samples. These are genuinely absent, not one-based VAG confusion.
The fixed game's secret trigger uses CD track13; sound173 is also called by
`worldEffectChainBlock`, so that does **not** prove sound173 unreachable.
The fixed `item.h:48-50` gym remap comments mention0→200,4→204, but are not
implemented. Animation commands can also name sounds: absence of literal
callers is not reachability proof.

Main's explicit policy: retain87 real samples and list the four unavailable
(sound,sample) pairs; skip/count events for precisely those pairs in scene logs.
Do not create silent PCM, substitute PC samples, or hide any other bad index.
Bankless PSX CUT1..4 likewise have no samples and list their six unavailable
inventory references108..112,115. Core validates that every unavailable pair
is actually referenced and cannot mute an existing sample.

## Encoding, mixer and fixed memory

PC retains signed8 PCM at its source rate; PSX is cooked to signed16 BE
PCM at11025Hz,without additional quantization. **PSX is not decoded whole
into RAM.** Only selected effects and streaming windows are resident.
Long ambient loops read the cart. The optional raw-SPU comparison encoding
decodes one28-frame block per voice; it is not the shipping SFX format.

Measured across43 banks:

- Metadata maximum3648B(PC LEVEL10C); maximum591 sound parts including
  metadata(PC GYM). The16-byte stream index needs9456B;1024 slots reserve16384B.
- Deduplicated sound sections total5611248B PC /3498784B PSX,not a whole cart.
- Whole frequent sets would cost271200/283616B per campaign bank; they do not
  fit. Admit whole effects deterministically in source order until the cache
  is full. The98304-byte arena contains metadata4096,index16384,six double
 4096-byte windows,mixer/emitter state,and25968B remaining frequent cache.

| Resident set per level | PC bytes | PSX PCM16 bytes |
|---|---:|---:|
| Each campaign level; PC SNOWCUT |25392|25344|
| GYM (largest) |25952|24976|
| TITLE |13168|13280|
| CUT1..4 |0|0|

Standalone SFX charges107600/131072B in4 boot allocations: callback32,
AI ring5648,arena98320,cached two-bank output3600 (headers included).
Host uses the same claimed fixed-slot accounting. The merged music transport
adds19984B to the same arena allocation: **127584/131072B**,still4 allocations,
0 after boot. Library heap is1232B with0 post-boot allocations.
Measured standalone image475560/524288B; current integrated footstep image
505832/524288B. Audio objects account for16464B in the standalone link map;
merged-string ownership/padding and other agents' changes prevent attributing
the full cross-epoch image delta to SFX.

Voices are outermost; Q16 pitch resampling walks bounded source spans with
local phase/gain/format. Centred voices accumulate one32-bit channel and
duplicate only after mastering. Panned voices add a right-minus-left bank.
Invariant stereo branching stays outside sample loops; no per-sample calls.
One aligned cached-to-uncached copy replaces individual AI sample stores.

## Wire layout and binding

Sound uses pack kind0x40000, magic`SN64`, ABI1. Header64 B is magic followed by
15 u32: version1,total bytes,metadata bytes,sample count,stride32,samples
 offset64,unavailable count,unavailable offset,maximum payload block4096,
 then six zero words. Metadata size is align16(64+32*sample_count+
 8*unavailable_count), no unexplained bytes.

Each32-byte sample descriptor has eight u32:
`data_offset,frames,rate,encoding,loop_start,data_bytes,flags,zero`.
Encoding1 is signed8 PCM;2 is signed16 BE,with `data_bytes=frames*encoding`.
Encoding3 is raw SPU16-byte blocks,11025 Hz only,with frames divisible by28
and `data_bytes=frames/28*16`. Other descriptor words keep the same meanings.
Samples occur in source-index order with16-aligned starts and zero tails;
`loop_start=0` repeats the whole effect for TR1 mode2. Flags bit0 marks a
resident candidate, not a compulsory allocation or duplicate sound semantics.

Unavailable entries are8-byte BE(sound ID,sample ID), strictly sorted/unique.
Every missing sample ID is outside the real sample count; the unchanged core
supplies the reference authority. Unlisted invalid samples fail readiness.

The pack contains one metadata physical part and sample-aligned parts up to
4096 B. Explicit cuts preserve shared sample content across levels. These
parts are uncompressed by the outer pack codec; physical parts are content-
addressed and deduplicated exactly as other pack data. The independent Python
reader validates every offset/count/size/rate/encoding/reserved word and PCM
values; Core validates sample/missing references before readiness.

The loader copies only metadata and compact16-byte raw-block descriptors into
boot-owned audio memory before unmounting its temporary I/O lease. Playback
uses the resident pack-stream helpers and ordinary engine/io requests: CRC
verification, <=4KiB pieces, <=32KiB/frame. No cold-unit function pointer, pack
scratch pointer, direct cart read, post-boot malloc/free, or new level-arena
reservation survives into play. Stop drains outstanding audio DMA before pack
rebinding, then invalidates every voice and cache.

## Runtime rules and shared output

Consume `game_sound_events` once after each completed tick,including terminal
ticks,before stop/presentation. OpenLara2333adef `src/fixed/item.h:36-119`
already chose chance,gain,pitch,sample and camera-target attenuation.
Do not repeat them or draw game RNG. Volume64 and pitch128 are unity;
`common.h:427,1812` and `game.h:509` define Q6 gain and a one-bit SFX enable,
not an extra quarter-volume master.

Mode0 is unique;1 replays;2 is refreshed **every simulation tick**;3 overlaps.
An unrefreshed mode2 voice ends on the next tick and schedules no new reads.
The finite flood emitter(`fixed/lara.h:4429-4446`)otherwise leaks a drone.
Stop affects the selected physical sample. SPU predictor history carries
across loop wraps; a new/replayed voice resets it.

Main explicitly selected **one-half output-bus headroom(-6.02dB)**.
SFX and optional unity music remain Q12 through addition,then share one
mastering division and final counted saturation. Event gains are unchanged.

Positional math facts come from OpenLara main `src/sound.h:36-40,973-988`
and `src/utils.h:470,653-655,776-784` at2333adef,attributed in the original
MIT integer implementation `spatial.c`. Q12 gains implement pan0.7,
facing0.3 and,for core emitters only,linear8192-unit fade. Game events
receive **no second distance fade**. Listener eye/basis,flip and the public
`game_settings_view()->audio_sfx` preference are read-only.

The12 real source/level emitter inventories have maximum38 sources(LEVEL10B),
one sample each,chance0,no gain/pitch-randomization bits,volume32767.
All are mode2 except LEVEL10A sound167 mode0. Camera bit12 on water80 does
not invent the main reference's disabled camera-bit special case.
Cook/load reject more than64 sources or a randomized-emitter contract.
Core remains the sole position/map/detail authority.

Flags64/128 select flipped/unflipped;192 allows both;0 has no flip restriction.
Sources use nominal pitch128. Free channels select the loudest eligible
emitters; existing audible voices retain continuity. Game effects may displace
a quieter ambient voice. Inaudible/disabled loops release ownership and
restart when eligible; non-loops start at most once per level. The engine
never alters game state,RNG,trace or hash.

AI is22047Hz on the measured cart,three448-frame buffers: maximum60.96ms
latency,minimum roughly40.64ms remaining after service. The requested two
buffers failed:88 physical empty observations,although SDK ownership bits
reported0,and only5.506s PCM over6.036s. Check physical AI BUSY(0xa450000c,
bit30),not only `audio_get_queued_buffers()`. Three-buffer6-voice runs have
0 physical empties and6.035s PCM matching the active clock. Pump resident
audio in route idle/framebuffer waits,never the renderer hot loop or AI IRQ.

Host emits735 stereo frames/tick at22050Hz into `TR64_AUDIO_WAV`,in chunks
of at most448. Music uses the same output,not a second device: paired
`tr64_music_memory_bytes/init` extend the single boot allocation; lifecycle
start/tick/pump/report hooks remain resident; stop drains music first.
`tr64_music_mono_pcm(frames,rate)` supplies one mono buffer before final clip,
including when there are no SFX voices. Decoder windows/sample clock and
codec state must fit the same row. No heap,code-unit transition,cold pointer
or blocking decode belongs in an interrupt. See [n64-music.md](n64-music.md).

## Exercised evidence

All reports,WAVs,packs and source hashes stay external under
`$TR64_WORK/evidence/Audio/` and `$TR64_WORK/tmp/Audio/`.
The emulator harness disables its host audio device: cart runs establish
AI operation and bounded counters; host WAVs are listenable. No claim of
human listening on physical N64 hardware is implied.

| Real scene,host and stock4MiB cart for both sources | Ticks | PC /PSX host peak dBFS | Bad audio counters |
|---|---:|---:|---|
| LEVEL1 forward footsteps |75|-19.90 /-20.20|0|
| LEVEL1 bridge,pistols,wolf |180|-4.05 /-4.96|0|
| LEVEL3A waterfall |120|-6.93 /-10.71|0|

Footsteps produce6 events/6 starts; host WAVs have55125 frames,
peaks3313/3201,RMS164.34/159.61. The bridge produces20 pistol starts(sound8),
5 wolf hits(sound20),sound22 once and9 footsteps(sound0); its132300-frame
WAVs have peaks20558/18518,RMS3868.91/3214.61 and71018/70020 differing stereo
frames. Waterfall placement is measured room1,37376,-256,76288:
10 core sources,3 ambient starts/1 stop,real streaming293248B/83 reads PC,
296288B/79 PSX. Its88200-frame WAVs have peaks14753/9551 and final-second
RMS1432.01/1056.20. All named runs have0 drops,unavailable SFX,stream
underruns,physical output empties,clips and saturated host samples.
Directories:`final-footsteps/`,`mastered-pistol-wolf/`,`waterfall/`.
Isolated SFX packs intentionally contain no music catalog; their separate
music-unavailable counter is not presented as music acceptance.

Controlled bridge probes keep the actual game input/RNG and substitute1 or6
real sample voices through normal event/I/O/output. These are CPU workloads,
not a claim that gameplay naturally sustains six effects.

| Source /voices | Mean mix/copy us per448 | CPU ms/30Hz |
|---|---:|---:|
| PC /1 |340.09 /20.04|0.591|
| PC /6 |1070.59 /19.98|1.789|
| PSX PCM16 /1 |343.34 /20.94|0.598|
| PSX PCM16 /6 |1098.78 /19.88|1.835|

`mono-fastpath/` retains the four180-tick clean cart probes. These are mean
CPU-demand figures,not hard pump maxima. Before hoisting invariant branching,
spatial-compatible centred mixing cost2.10ms/30Hz. The optional raw-SPU
decoder previously cost2.778ms/30Hz; cooked PCM16 was selected on measured
CPU cost,not convenience. Main's conditional RSP/CRC comparison is dropped
because both shipping six-voice means are below2ms; RSP was not benchmarked.

Final integrated acquisition gate(`bounded-acquisition/`): room14 sound-on/off
GPU completion26.320/26.315ms,whole-work28.209/27.758ms(+0.451ms).
Global mixer+copy totals169172/33735us over180 ticks add0.752ms/tick;
this excludes other audio bookkeeping and is not a full-engine CPU bound.
Integrated PC6 measures1.610ms/30Hz,peak pump2.188ms,with0 bad audio counters.

SDK `kirq_wait` is a no-op without its thread kernel: retry-driven servicing
previously wasted2.1–2.6ms per acquisition. Resident service is now limited by
an unsigned raw-tick8ms deadline plus a final-success refresh; room14 service
is0.306/0.082ms on/off. No kernel initialization or renderer hot-loop hook.
Work retains measured acquisition service but excludes idle wait; idle-loop
mixing outside that interval remains in global totals. Different code/layout
epochs are not an audio speedup claim. The perf opt-in writes
`hash=omitted/perf-only`,never a fake numeric hash.

Focused gates:

- `snd-spatial-test`:980 independent floating-reference/extreme-position
  cases,max error1.41 Q12 units;`snd-stereo-test`:24 exact channel PCM cases.
- `snd-ambient-test`:18 exact resident/streamed PCM/SPU cases covering flip,
  range,one-shot lifetime and public SFX-preference stop/restart.
- `snd-mixer-test`:18 exact format/pitch cases;`snd-mode-test`:25 cases,
  including three refresh ticks then87 empty ticks and predictive SPU wraps.
- `snd-off-test`:6 silence cases;`snd-evidence-test`:positive+8 rejection
  controls; cook tests reject38 malformed native/emitter cases.
- `snd-music-hooks-test`:compound boot/lifecycle/music-only stereo bus with
  explicitly synthetic PCM,not a real-codec acceptance claim.
- Ordinary host hashes match sound-on/off for76 LEVEL1 snapshots per source
  and121 real waterfall snapshots. No no-hash mode is used for that proof.
- Final inventory independently decodes all43 real cooked banks; default
  and perf route controls,provenance gate and hostile controls pass.

