# Tomb Raider II and III: implementers' survey

Scope: classic retail Windows PC and PlayStation; base campaigns, not demos,
expansions, or remasters (demo differences noted where relevant). tr64 baseline
`8fffb91`; **OL** = [OpenLara `2333adef30ae2a5f3b32c5355e6e343d6aa0531a`](https://github.com/XProger/OpenLara/tree/2333adef30ae2a5f3b32c5355e6e343d6aa0531a/src),
floating-point `src/` unless explicitly qualified. **RS** =
[TRosettaStone 3, preview](https://opentomb.github.io/TRosettaStone3/trosettastone.html);
section numbers below refer to that page. **S2** =
[IceBerg, *TOMBPC.DAT File Format for Tomb Raider II Games*](https://xythobuz.github.io/tr_docs/TombPC_TR2.pdf)
(printed page numbers). **C2** / **C3** = Stella's classic
[TR2](https://tombraiders.net/stella/walks/controls/TR2controls.html) /
[TR3](https://tombraiders.net/stella/walks/controls/TR3controls.html) controls;
**M3** = [Eidos TR3 PC manual](https://tombraiders.net/stella/files/manuals/TR3/Tomb_Raider_3_PC.pdf)
(printed pages). **Wall**, **Venice**, **Jungle** = classic route prose for
[The Great Wall](https://tombraiders.net/stella/walks/TR2-classic/01greatwall.html),
[Venice](https://tombraiders.net/stella/walks/TR2-classic/02venice.html), and
[Jungle](https://tombraiders.net/stella/walks/TR3walk/I1-jungle.html).

**Read means parsed, not verified against retail and not necessarily played.**
Reader conventions and public claims are kept separate below. Inferences and
recommendations are not retail facts. No other engine/decompilation source,
retail executable, embedded code module payload, or wall-reader transcript was
consulted. Source policy: [`docs/PROVENANCE.md` §§1–2a](../PROVENANCE.md).

## 1. Data and formats

### 1.1 Levels

**PC order.** TR1 `.PHD` starts with the version and indexed pages, uses
object descriptors before sprites, and puts the palette after LightMap
(retail order), then cinematic/demo/sound with samples inline. Both sequels
use `.TR2`: TR2 version `0x2d`, TR3 `0xff080038`, `0xff180038`, `0xff180034` in OL (RS lists multiple); OL's signatures
and release size/name fallback are in `format.h:3170–3221` and
`gameflow.h:515–527,672–905`. Both put the two palettes before the textile
count, followed by indexed pages, 16-bit pages, a reserved word, rooms/floor
words, meshes/offsets, animation/state/dispatch/command/tree/frame tables,
models and statics. TR2 then has object textures, sprite descriptors and
sequences, cameras/sinks, sound sources, boxes/overlaps/zones, animated
textures, items, LightMap, cinematics, demos, sound map/details and sample
ordinals. TR3 moves object textures to between animated textures and items.
Source: RS §§9.1, 10.1–10.3, 10.6–10.7; OL `format.h:3318–3347,3456–3478,3519–3540,3714–3751`.
The PC reader in tr64 is version-32-only (`tools/cook/src_pc.py:10–19`);
a source variant is needed even for sections marked unchanged in the IR.

**PlayStation order.** RS §§10.1–10.3 are PC-style whole-file descriptions
(they describe PC palettes, LightMap, demo bytes and PC sample ordinals);
§§2.1, 3.2.3, 6.2.7 and 11 give some platform details, not byte-complete
TR2/TR3 PSX level layouts. Do not run the TR1 VAB/13-page/1024-CLUT reader on
either (`tools/cook/src_psx.py:13–68`; `ir.md:23–28,58–65`). OL supplies the
following **reader evidence**, not a substitute for a checked section ledger:

- **II PSX**: sample count, offsets with a terminal offset, and sample bytes
  before the main arrays; variable count of CI4 pages and CLUTs after those
  arrays; special Japanese CLUT-count/kanji branch. Descriptors, cameras,
  sources, boxes/zones, animations of textures, items, sound map/details
  and cinematic frames follow; it skips three four-byte spans and does not
  call the PC demo/LightMap readers (`format.h:3481–3517`). The compact room
  words encode 5-bit local X/Z in sectors, y in 256 units relative to top,
  intensity and attributes; its quad flags/indices are separate spans,
  triangle indices are shifted, and quads change strip order
  (`format.h:5263–5389`).
- **III PSX**: sample offsets/data at the front, 13 length-delimited module
  slots **skipped**, not decoded; additional outside-room and bounding-box
  spans skipped after floors. Read variable pages, twice the CLUT count
  (underwater set), object/sprite tables and common gameplay tables, then
  sky colour and a separate room texture domain; two mip descriptors per
  room texture are skipped. Neither PC demo nor LightMap is read
  (`format.h:3543–3612,3714–3751`). Room parts use a 16-entry offset table,
  packed 5-bit positions/RGB555, byte vertex/triangle counts, 7-bit local
  indices and 10-bit materials, with a `0x3ff` end marker for quads; decoder
  flattens parts into one room, drops the packed vertex flags and swaps
  strip corners. Both sequel PSX readers omit room sprites and add room
  top to portal vertex y (`format.h:5124–5261,5383–5415`). Preserve spans
  and edition-specific evidence; no code inspection is needed to skip a
  sized opaque module.

**IR/native changes below are recommendations**: U = unchanged canonical
shape (source parsing/order or game semantics can still change); E = extend
an existing field/record; N = new record/stream. IR citations refer to
[`docs/formats/ir.md`](../formats/ir.md); native abbreviations refer to
[`n64-core.md`](../formats/n64-core.md), [`n64-tex.md`](../formats/n64-tex.md),
[`n64-geo.md`](../formats/n64-geo.md), [`n64-snd.md`](../formats/n64-snd.md),
[`n64-music.md`](../formats/n64-music.md), [`n64-video.md`](../formats/n64-video.md),
[`n64-ui.md`](../formats/n64-ui.md) and [`n64-pack.md`](../formats/n64-pack.md).

| Section | Public data delta / OL reader | Cook/IR and native impact |
|---|---|---|
| Pages, palettes, LightMap | PC II/III have 256 RGB entries, 256 four-byte solid-colour entries and both 8-bit and ARGB1555 versions of each 256² page; high 16-bit pixel bit selects opaque. Palette moves; 32×256 LightMap remains. RS §§2.3.2–3, 9.2, 9.8, 10.1–10.3; OL reads both (`format.h:3456–3460,3519–3523,3858–3863`) and forcibly zeros LightMap column 0. PSX uses CI4/CLUT, not PC 16-bit pages; III reads dual palettes (§1.1 above). | **E** tag image/palette encodings and source material domain, retain both on PC; **U** LightMap bytes or explicit absence, CI4 target for quantised output; **E** if selecting underwater palette per room/view (IR:39–65,132–148; TX64:62–83,118–124). |
| Room vertices / flags | II PC adds attributes + second intensity (the one used); low 5 attribute bits interact with room light mode, high bits special wave/light. III PC retains a legacy lighting word plus flags and RGB555; colour waves/caustics are different. II/III PSX packed layouts above. RS §§3.2.5, 3.4.2–3; OL consumes II's first word then overwrites it, III PC reads colour, III PSX sets attributes to zero (`format.h:5187–5203,5263–5311`). | **E** preserve both intensity words, colour, raw bits and light-mode semantics. Geometry already carries colour; source-to-vertex bake and dynamic vertex effects are different, not another pose format (IR:67–86; GEO §§Decisions, Exact tables). |
| Ambient, lights, static instances | II adds ambient2, light mode, second intensity and falloff on point lamps, and second static-instance shade. III drops II's light mode word, adds colour and type (point/sun; last eight bytes type-dependent in RS), RGB555 static shade and room tail bytes water scheme/reverb/filter. RS §§3.2.4, 3.2.8, 3.4.2–3, 10.6–7 (description of III's second ambient word and tail filler is not uniform across sections). OL reads those room fields and RGB/type but models III lamps as 16-bit intensity + skipped halfword + radius, not a sun-vector union; skips II light secondary values and one static shade; rescales radii (`format.h:5444–5559`). | **E** tagged light payload, dual shades, water scheme/reverb and exact source room flags; validate the III light union before interpreting it. Runtime rooms/light ABI needs extension if not baked; raw values stay distinct from rendering scale (IR:94–104; CORE:22–28). |
| Portals / sectors | Common portal vertices and linked 1024-unit cells; III packs low 4 bits material and bits 4–14 box, including the public 2047 special value; RS §3.2.2–3. OL moves PSX portal Y and extracts material; substitutes box 0 for its 2047 case, marked TODO (`format.h:5406–5441`). | **U** portal shape, coordinates normalised by reader; **E** sector material and unambiguous invalid/slide box (do not collapse a sentinel to valid box 0); native collision then interprets it (IR:88–93; CORE:22–24). |
| Meshes, faces and colours | PC still has textured and coloured triangles/quads, with the common flags/material word (high double-sided bit, two colour index domains for flat faces); no extra TR4-style effects word. PSX differs in mesh index/packing/flat materials; III's room materials are a separate table from its object materials (RS §§2.4.4–5, 5.2–4, 9.2–3; OL `format.h:3595–3606,5092–5110,5800–5965,6530–6550`). | **U** decoded triangle/quad topology, mesh pointer aliasing, material/corner mapping; **E** explicit palette bank, room/object material domain and all source flags; compact native ID after mapping, not a raw retail ID (IR:76–86,106–130; TX64:27–35,118–124; GEO:77 onward). |
| Transparency | RS §9.3.2: 0 opaque, 1 alpha-test, 2 additive in III+; §9.2's later colour-key note is not a licence to use magenta for all formats. PSX STP is separate from transparent zero; OL normalises III PSX descriptor values 0/2/`0xffff` to 0/2/1 and defaults others to 0 (`format.h:6090–6104,6127–6154`). | **E** preserve raw/normalised modes and STP/palette identity, reject or report unknown rather than defaulting. Existing TX64 flags hold 0/1/2 and separate STP; real additive, subtractive blob shadows (RS §15.1.3 #294), ordering/depth and semi-transparent texels require renderer rules (TX64:74–101; GEO:68–75; `plat/n64/gpu.c:145–159,282–349`). |
| Animated textures / sprites | Same word list of groups and count-minus-one; sprite sequence is type, negative count, first frame. PC sprite block moves relative to object textures in III; TR3 pickups use models, not a new sprite record. OL reads the groups and textures/sequences (`format.h:6244–6311`), then promotes transparency for all members of a group with a nonzero attribute (`format.h:6344–6361`). RS §§2.3.4–6, 5.6–7, 9.4, 10.2–3. | **U** table shapes; **E** source domain where room vs object differs; validate animated member UV/material/tile shape (native ID permutation assumes equality); no fake PSX room-sprite count in source ledger (IR:156–178; TX64:17–53). |
| Animation records / mesh tree / frames | Record/state/dispatch/tree shapes mostly common across I–III; sequel rotations can be variable-length axis-tagged words; retain source frame layout and command/effect meanings. PSX model record includes trailer word. RS §§6.2.1–2, 6.2.4–7; OL reads data arrays and models (`format.h:3738–3799`) and decodes rotations with a version-dependent path (`animation.h:244–252`, `format.h:2666–2717`). | **U** state/dispatch/tree/motion record shapes; **E** explicit frame encoding or bake to common fixed frame layout and rebase offsets, retain per-game effect IDs, ranges and model bindings (IR:159–167; CORE:29–37; §5). |
| Floor and trigger lists | Word container retained. §4 lists the opcodes, masks and handler coverage; II adds climb; III diagonal floors/portals, monkey and rail sectors. OL reads raw words (`format.h:3724`) and only partially consumes sequel semantics (`controller.h:413–550`; `lara.h:2011–2277`). | **U** raw floor words and sector references; **E** version-aware validation/equivalence, decoded collision metadata and game dispatcher; never rewrite retail addresses as item IDs indiscriminately (IR:156,238–255). |
| Boxes / overlaps / zones | II/III use 4 byte-sized sector extents (×1024), floor & adjacency/blocked bits; ten arrays rather than six, four ground plus fly per flip state. RS §7.3.1; OL actually scales extents and loads all arrays (`format.h:3815–3856`). | **U** normalised boxes and overlap list; **E** ground3/4 in IR/core and blocked bits/selection rules (IR:170–172; CORE §§Layout; `game/nav.c:7–21`). |
| Items | PC/PSX II and III add second intensity word; not automatically the TR4 OCB field. IDs are game-local, not globally interchangeable. RS §§5.5.2–4,15.1; OL reads both words and remaps IDs (`format.h:2084–2124,6313–6333`). | **E** game/type domain plus second/tagged shade and flags; canonicalise only proven shared behaviour; keep original numeric type available for diagnostics. Preserve model-only and lookup-only entries separately from live items (IR:174; CORE §Layout; §3). |
| Cameras and sinks | One XYZ record, camera room versus sink speed with shared flag/box field; trigger extra camera word. III currents can prevent surfacing. RS §§4.6.2–3, 9.5; OL reads the same table (`format.h:3803–3805`) and uses both roles (`lara.h:2167–2189,3792–3828`). | **U** record and IDs; **E** trigger/camera state and exact current semantics, not an invented flyby table for II/III (IR:168; CORE §Layout; §4). |
| Sound sources | XYZ/id/flags layout persists; flip-state flags affect whether ambient emitters play. RS §8.3.1; OL reads the same table (`format.h:3811–3813`); parsing does not prove loop/mix parity (§4). | **U** source table/positions; **E** resolve flags and use edition sound-map namespace rather than assuming TR1 IDs match (IR:169; SN64 §§Encoding, mixer and fixed memory). |
| Sound map, details and samples | 370 signed map entries vs TR1's 256. II PC keeps volume/chance u16; III packs u8 volume, sector range, chance, pitch, then flags. PC `DATA/MAIN.SFX` is consecutive RIFF/WAVE; level indices are ordinals, not byte offsets; RS distinguishes II 8-bit/~11 kHz and III 16-bit/22 kHz PCM and warns of inline-sample demos (§§8.2–4,10.2–3,10.10; S2 TR2.1). PSX sample-bank front matter differs above; OL reads offsets, uses local SPU/VAG data and compensates for II PSX 8 kHz (`format.h:3481–3491,3543–3560,3877–3910,4812–4836,6550–6563`; `level.h:833–850`). | **E** map cardinality, details and sample encoding/rate and loop mode; **N** host source-bank index (not required as a second runtime container), complete-audio census and independent offset/sentinel checks. Keep native streamed index/ADPCM where contracts permit, don't preserve a fabricated VAB for II/III (IR:54–65,177–189; SN64 §Encoding; PACK §§Pool lifetimes, On-demand nonresident catalogs). |
| Cinematic frames | Separate cutscene levels with eight i16 words (target XYZ, camera XYZ, FOV, roll); script selects which cut and cut angle. Both PC and both PSX entry points call frame reader, III PSX after sound-map processing (`format.h:3473,3516,3536,3609–3612,3865–3867`). RS §§2.1.6, 9.7, 11.5.1. | **U** frame table; **N** script link/entry angle and media/actor association; keep edition coordinates and verify actor roles (IR:175; CORE §Layout; `game/cut.c`). |
| Demo | PC count and bytes after cinematics, title-timeout/demo IDs in script; retail vs demos may differ in media and level contents. OL retains the PC bytes, not PSX (`format.h:3473–3476,3536–3539,3873–3875`); no retail demo input playback established by that read. RS §§10.2–3,10.10,11.1,11.5; S2 TR2.1, TR2.7; Jungle “Notes on the PC/Mac Demo”. | **U** opaque PC demo array and explicit absence (unknown skipped PSX spans are not proof of absent console demo support); **N** gameflow demo record/replay decoder only when the edition input and timings are established (IR:23–28,176; CORE §Layout). |

### 1.2 Script, soundtrack, FMV and save

| Feature | Documented / OL implementation | Port implications [inference] |
|---|---|---|
| Script structure | RS §§11.1–11.6 and S2 TR2.3–4: `TOMBPC.DAT` / `TOMBPSX.DAT`, version 3, header/gameflow config, counted offset/string banks with optional XOR, front-end + per-level 16-bit command sequences, title/picture/movie/level/cut paths, demo IDs, per-level puzzle/pickup/key names, edition language/UI strings. PSX FMV descriptors contain start/end frame values; RS distinguishes beta-only arrays. OL has **no retail script reader** in its level-load path: compiled `LEVEL_INFO`, version/path heuristics, FMV chooser and hand-coded equipment exception for Rig (`gameflow.h:255–260,288–346,975–1095,1490–1685`; `level.h:948–1027`). | **N** cook campaign/gameflow and localised string records, validated sequence offsets/opcodes and path references; compile to target data rather than executing unchecked source bytecode. Existing title/menu/UI asset and video catalog remain consumers (UI and PACK docs; `game/game.c:35–59`). |
| Sequence, secrets, inventory and environment | RS §11.5.1 lists FMV 3, level 4, cine 5, complete stats 6, demo 7, jump 8, end 9, track 10, **sunset 11** (TR2 LightMode), load picture 12 (TR3), **deadly water 13** (reported ineffectual in TR2 Temple of Xian, possible precursor—not equivalence proof), remove weapons 14, game complete 15, cut angle 16, NoFloor 17, start/bonus grants 18, starting animation 19, secrets 20, kill-to-complete 21, remove ammo/medis 22. S2 TR2.7–9 separates global options from opcodes; RS §11.5.2 distinguishes grants and secret rewards. TR3's regional selection and exposure indicator are publicly observable (M3 pp. 6,12); those sources do **not** establish encodings for TR3 lightning, cold/heat opcodes, or a global rule for sun/deadly water. OL defaults secrets in `LEVEL_INFO` (all TR3 gameplay rows there use 3, contradicted for Jungle by the route's six), follows compiled order, and does not consume these retail directives (`gameflow.h:313–346`; `level.h:87–116,1004–1027`). | **N** explicit per-level start/reward/removal/completion/environment policy and branch/progression save data. Separate authored facts (e.g. sunset) from climate mechanics or unsourced bits; capture real DAT files before calling a field supported. |
| II audio and movies | RS §§2.1.3–5, 8.1.1: PC CD-DA soundtrack, Escape `.RPL` at 15 fps; PSX CD-DA and `.STR` (the STR video format is PlayStation MDEC with optional interleaved XA audio, not a generic MPEG file: [psx-spx “Video STR Streaming” and “Audio Streaming”](https://psx-spx.consoledev.net/cdromfileformats/#cdrom-file-video-str-streaming-and-bs-picture-compression-sony)). Examples: [TR2 Windows Rayovac track table](http://redump.org/disc/54168), [PSX PAL SLES-00718 track table](http://redump.org/disc/906) both 1 data + 60 audio tracks; these counts are not a logical track mapping. OL remaps II IDs to external `track_XX` files on both platforms; checks named PC films (not an original STR reader on this path) (`gameflow.h:1289–1301,1503–1509,1604–1646`). | **U** CD extraction/re-encode and native movie playback where compatible; **E** edition track map and FMV source rate/sector windows. Existing extraction policy belongs to `tools/cook/n64_music_source.py` and `n64_video_source.py`, not retail files on cart. |
| III audio and movies | RS §§2.1.3–5,8.1.2: Windows `CDAUDIO.WAD`, 130 × `0x10c` directory bytes before embedded MS-ADPCM WAVE tracks; RPL 30 fps. [PSX PAL SLES-01649 track table](http://redump.org/disc/927) has **one Mode 2 data track, no CD-DA**. RS's WAD description does not identify that disc's soundtrack file/sector-channel layout. OL opens a WAD for both versions (a host acquisition convention, not proof of a PSX WAD), selects RPL/MPEG alternatives, and has no observed disc XA soundtrack selection in that path (`gameflow.h:1302–1305,1342–1346,1511–1515,1650–1681`; OL `sound.h`, `openCDAudioWAD`). | **N** PC WAD input reader/index, PSX source survey/container extractor; **E** preserve script IDs, trim/loop and audio/video sync. Native MN64/V64V can stay transcode targets if validated; 30 fps source does not obligate the current 15 fps native profile to decode twice as fast (MUSIC §Codec decision; VIDEO §§Encoding, Runtime; architecture §10.1 M4). |
| Retail saves | [Stella save FAQ “Introduction” / “CD versions of Tomb Raiders 2–5”](https://tombraiders.net/stella/savegame/ziphelp.html): per-slot `savegame.N` on Windows, game/level state (position, health, inventory, items/enemies); [TR3 PC save notes](https://tombraiders.net/stella/savegame/TR3saves.html#pcsave) distinguish region order and patched-build compatibility. PlayStation uses card saves ([TR2 PSX saves](https://tombraiders.net/stella/savegame/TR2saves.html#ps1save)); III PSX consumes a collected crystal while PC saves freely and has health crystals (Jungle “Notes on Save/Power-up Crystals”). These establish container/behaviour shape, **not** internal retail byte layout, offsets or cross-version interchange. OL `savegame.dat` is **its own** `OLS2`: repeated byte-size/payload records, stats, inventory, flip/track masks, entity and Lara extras (`savegame.h:9–111,124–159`); `lara.h:677–724` writes stamina/poison/freeze as zero, restores HP and oxygen. | **N** sequel save schema for controller, environment, inventory, area and crystal state; no *retail* save import implied. Prefer native logical IDs and a versioned slot payload; measure slot size against existing 8 KiB (architecture §11 D6, `engine/save/game.c`; §6). |

## 2. Lara, weapons and the rings

**Numeric IDs are not interchangeable.** A state ID is requested by the
controller and resolved by frame-window dispatch in the animation data; an
animation index can be relative to that model's base; neither is an entity
ID. RS §§6.1,6.2.2,6.2.4–7; OL `animation.h:81–113,145–197,244–252`.
**NG** = [EssGee and Jesse, NGLE Manual, *WADmerger Intermediate Tutorial*,
ch. 4 “StateIDs” / “Lara's StateID List”](https://www.treditor.hu/7/gmac/nglemanual/ngle160.html#chapter4),
a public authoring table (question marks there are not retail facts). It
also cites animation 103 for the standing state and 96/150 for grab
transitions in its StateId-link examples; those examples do not constitute
a retail II/III per-model animation catalogue. The other animation indices
below are explicitly OL's, not claimed as complete public tables. The
[catalogue thread](https://www.tombraiderforums.com/showthread.php?t=148859)
and [OL-linked thread](https://www.tombraiderforums.com/showthread.php?t=211681)
returned 403; vehicle animation/state maps and unlisted exact transition
indices remain data/wall questions.

| Ability | Public evidence and IDs | Floating-point OL coverage |
|---|---|---|
| II ladder grip/climb; III climb into crawlspace | NG II: 56 ladder wait, 57 up, 58 left, 60 right, 61 down (no verified public 59 label in NG); RS §4.4.6 direction mask; C2 “Combo Moves”; M3 p. 8 jump/backflip, face/release, top crawl. | **Parsed/declared, not implemented.** `controller.h:539–545` records climb mask. `lara.h:217–222` declares 56–61 (59 named climb end there), but never consumes `.climb`; `checkClimb` is a ground obstacle-vault height test producing animations 50,42,26, not a ladder controller (`lara.h:2541–2579`). |
| II wading, retained in III | NG 55 exit/shallow-water rise, 65 forward wade; Wall “Cave with Pool”, M3 p. 7 waist-deep movement/vertical jump only. | **Implemented, not parity.** `lara.h:44–46,2302–2355,2841–2902,2998–3011`: >384 and ≤730 depth branch, swim-to-wade threshold 512, run/raise/wade transitions and limited movement; anims 176–179, 186, 190–193 at `142–150`. No numeric retail boundary inferred from the fork. |
| II air turn / dive flip and swim roll, retained in III | NG 68 air reversal, 66 underwater roll; C2 “Combo Moves”, “Swimming”; M3 p. 6 forward/backward and opposite input. | **Implemented entry paths.** `lara.h:2466–2473` checks the sequel version, opposite input or both directions for air-roll including fast dive; `2737–2753` uses model-relative anim 203 for the submerged input chord if not already rolling. Pose/timing come from the model transitions, not established as exact for every jump or blocked exit. |
| II flares (with III additions) | NG 67 flare pickup but notes ambiguity with other pickup states; RS §§15.1.2 IDs 9,151–152,173,235 and 15.1.3 IDs 10,178–179,203; C2 “Basic Moves”; M3 p. 9 light in water/climb, draw drops, flare input throws, roughly a minute. | **Partial data only.** Typed as pickups in `format.h:2450–2459`; pickup state 67 declared, but ordinary ground/swim pickup handlers request state 39, inventory rejects unknown models, no equip/light/lifetime/throw/use state (`lara.h:217–231,1802–1864,3196–3229`; `inventory.h:271–317,763–805`; `level.h:1146–1313`). Existing muzzle/other dynamic lights are not a flare implementation. |
| II zip line, retained in III | Handle II #102 / III #127, NG 70, Wall and Jungle “Zip Line”: hold Action to ride and release to fall. | **Only ID/state.** `lara.h:231` has state 70; no mount/release branch or handle-specific controller (`lara.h:2279–2490,2574–2735`; `level.h:1146–1313`). |
| II rider: speedboat, red/black snowmobiles | C2 “Vehicles”, RS §15.1.2 #10–14,51–52,89–91: separate rider models and driveables, jump/side dismount, water boarding, turbo and armed black sled path. Gondola is a breakable prop, not the speedboat; the scripted TR1 Boat is not a rideable counterpart (`docs/game-gaps.md:73–75`). | **Absent rider/physics.** `level.h:1254,1294–1313` dispatches the *TR1* Boat, a generic enemy for sled driver; rider vehicles fall through to base controller. `objects.h:1493–1525` is the scripted prop, not drive controls. |
| III sprint, stamina, dive/roll and skid | NG 73 sprint, 74 sprint roll; C3 “Combo Moves”; M3 pp. 2,5 meter drains to run and jump while sprinting rolls. | **Absent.** State enum ends at 70; ground input/state dispatch has no sprint path. Save stamina is literal zero; rendered bars are health and oxygen (`lara.h:159–233,677–689,2574–2735`; `level.h:3459–3494`). |
| III crouch/crawl & ledge entry/exit | NG 71 crouch, 72 unused crouch roll, 80 crawl hold, 81 forward, 84/85 rotate, 86 reverse, 87 pull up into crouch, 88 crawlspace exit; C3 “Combo Moves”, M3 pp. 8–9; Jungle “Misty Area” and “Tunnel with Switch” for path/pickups. C3 distinguishes a one-handed shot in crouch from no shooting while crawling. | **Absent.** Same enum and ground dispatch, no crawl volume/bounds or special crouch weapon path (`lara.h:159–233,827–837,890–920,2574–2735`). |
| III overhead traversal | NG 75 catch thin ledge or ceiling, 76 forward, 77/78 side shimmy, 79 listed dubious/unused, 82/83 rotate. RS §4.4.8 ceiling tag; M3 p. 9 Action grip, directional traversal, release drop. | **Recognises opcode only** (`controller.h:539–545`), not hang/move states; regular low-ceiling ledge swing animation is not monkeyswing (`lara.h:2455–2463`). |
| III vehicles | M3 p. 3 and C3 “Vehicles”: UPV with harpoons, kayak paddle/paddle-turn, inflatable boat, quad with brake boost, minecart brake/duck/spanner switch. RS §15.1.3 #11–18 also lists a mounted gun; rail-turn IDs in RS §§4.4.9–10. | **No rider controllers.** Boat remaps to sequel `VEHICLE_BOAT`, not `BOAT`; minecart marker opcodes are skipped. `format.h:2235–2238`, `level.h:1254–1313`, `controller.h:543–545`. |
| Health, air, poison, cold and exposure | M3 pp. 6,11–12 gives about a minute of breath, cold water exposure bar, poison flashing health and using either medikit as antidote, restores health at successful TR3 level completion. C2/C3 “Menu/Inventory”; Jungle gives PC health-crystal vs PSX save-crystal policy; these are not exact counters or damage timings. | **Health/air implemented; poison/exposure absent.** 1000 HP, 60 s air, underwater drain by dt, then 150 HP/s damage, replenish 10 air/s otherwise (`lara.h:30–32,3303–3316`); only HP/air HUD (`level.h:3459–3494`). Save fields for poison/freeze are zeros, not live state (`lara.h:677–712`). |

**Weapons.** Public rosters and displayed nominal amounts are below (C2/C3
“Weapons & Ammunition”; RS §§11.5.2,15.1.2–3 for identifiers/pickup vs ring
models). C2/C3 also give smaller *examples* of pre-weapon Uzi clip counting
than multiplying their table's “pair” values by the number of example
pickups; confirm piece/stack units against the first collected ammo before
pinning native arithmetic. Both guides distinguish counted packs before
acquisition from ammo units afterwards, with stated per-item exceptions.

| Game | Weapons and nominal pickup units in C2/C3 | OL executable paths |
|---|---|---|
| II | Unlimited dual pistols; shotgun 2 shells; automatics 20/clip pair; Uzis 80/pair in C2 table; harpoon 3; M16 20/pair; grenade launcher 2. | Firing/damage/sound paths only for pistols, shotgun, TR1 Magnums and Uzis. The new IDs appear in the pickup predicate, not functional weapon/arm/projectile dispatch; OL's Uzi stock conversion is 50, not evidence for either retail sequel (`format.h:2439–2447`; `lara.h:827–837,987–1117`; `inventory.h:747–805`). |
| III | Unlimited pistols; shotgun 2 shells; Desert Eagle 5/pair; Uzis 40/pair in C3 table; harpoon 3; MP5 30/pair; single rocket; grenade launcher 2. | Same limit. OL's hitscan and shotgun spread logic do not implement burst, underwater harpoon/reload, rocket/grenade trajectories or their blast damage (`lara.h:1021–1117`; `level.h:1235–1241,1259–1313`). |

**Inventory.** C2/C3 “Menu/Inventory System & Hotkeys”, M3 pp. 4,9–12 and
S2 TR2.4 describe title/options, inventory and items rings; passport
new/load/save/quit (save disabled on death), training photo, audio/control/
graphics icons, stopwatch/statistics, weapons/ammo, small/large medikits,
flares, key/puzzle selection at a receptacle. II rewards all three dragon
statuettes per level; III has level secret counts and the whole-game bonus,
not the same grant rule (Wall start/“Climbing Down”, Jungle introduction).
III adds region globe/progression and edition-specific crystals (M3 p. 12;
Jungle footnote). OL genuinely animates/selects options, key rings and
stat pages, and adds a stopwatch by version, not all roster items
(`inventory.h:271–317,658–695,842–875,981–1075,1335–1361,1646–1670`). Its
generic pickup path can hide a sequel pickup and call the ring even when
`Inventory::add` rejects it for missing descriptor. It is not evidence of
flare use, reward grants, consumable save crystals or regional gameflow
(`lara.h:3196–3229`; `inventory.h:763–805`; `format.h:2450–2459` notably
recognises `CRYSTAL` but not `CRYSTAL_PICKUP`; `level.h:93–117,1299–1303`).

## 3. Object catalogue

### 3.1 Method and coverage

One row per **raw numeric slot**, not per placed instance, model, animation,
or unique runtime class. Crosswalk joins the id column in RS §§15.1.2 and
15.1.3 to the sequential `E(...)` declarations in OL `format.h:216–480`
(II) and `:482–855` (III); line numbers below point to those declarations.
`TR2_TYPES_START=1000`, `TR3_TYPES_START=2000` (`format.h:17–20`): before
remapping, II is `1000+id` through 1264, III `2000+id` through 2373, excluding
`*_TYPE_MAX`. Apply `Entity::remap` (`format.h:2107–2371`) **before** counting
factory support (`level.h:1146–1313`); a similar name, a type predicate or
a parsed model proves no game-specific controller. “—” in the name column
means no same-ID public row. This joins **248/265** and **290/374** public
IDs respectively, no public ID outside the enum spans, not 639 placed items.
Where the labels disagree both are shown; neither source settles a conflict
without the actual data/behaviour (notably II #120/121/133 and #221/222;
III #119–121 and #95–98). `UNUSED` is an enum label, not evidence of absence
on every disc. RS contains catalogue rows, not per-level placements.

TR1 comparison is a **reuse classification [inference]**, grounded in
`docs/game-gaps.md:81–117`, `game/object_factory.c:4–65`,
`game/object_dispatch.c:12–95`, `game/lara_inventory.c`, `game/cut.c` and
`game/inventory_economy.c`, not a claim that retail timing or collision is
identical. **S** = same known TR1 mechanism or named role; **V** = variant
with candidate TR1 family shown, requires behaviour comparison; **N** = no
matched TR1 role in this survey, so plan as new; **?** = unclassified or
reserved. Groups classify primary role; alternates, skins, FX and models
still each get one slot. II/III “Boat” is not TR1's animated prop.

OL column: **D** = dedicated or shared working path for the role shown
(not asserted complete against retail); **P** = identified partial controller
or pickup-only path with missing use; **G** = generic `Enemy` (inherits
base navigation, animation and damage machinery, no species attack/death
logic from a new class; `enemy.h:15–88,125–216`; `character.h:138–179`);
**A** = used by a subsystem (agent, ring, effect or media), not a dedicated
controller of its own; **B** = `level.h:1312` base controller (generic
animation/room/lights `controller.h:1264–1280`, no special behaviour),
unless the row says explicitly not constructed; **—** = no meaningful
controller claim for a reserved/unknown entry. `KeyItemInv` models and
animated sprite controllers are counted only for their roles, not as
separate placement support. Totals are computed by counting the joined
rows exactly once each by group and each status, including placeholders.

| Game / group | Slots | RS rows | S | V | N | ? | OL D | P | G | A | B | — |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| II Lara and interface assets | 24 | 24 | 7 | 4 | 13 | 0 | 0 | 1 | 0 | 14 | 9 | 0 |
| II Enemies and NPCs | 38 | 38 | 0 | 20 | 18 | 0 | 2 | 2 | 33 | 0 | 1 | 0 |
| II Vehicles and rider props | 6 | 6 | 0 | 0 | 6 | 0 | 0 | 0 | 0 | 0 | 6 | 0 |
| II Traps and hazards | 28 | 27 | 2 | 17 | 9 | 0 | 8 | 0 | 0 | 0 | 20 | 0 |
| II Doors, switches and movers | 42 | 42 | 29 | 6 | 7 | 0 | 30 | 2 | 0 | 4 | 6 | 0 |
| II Pickups, keys and ring items | 58 | 58 | 32 | 7 | 19 | 0 | 4 | 13 | 0 | 28 | 13 | 0 |
| II Effects and emitters | 31 | 29 | 11 | 1 | 19 | 0 | 10 | 0 | 0 | 2 | 19 | 0 |
| II Scripted set pieces and markers | 22 | 22 | 5 | 10 | 7 | 0 | 1 | 0 | 2 | 12 | 7 | 0 |
| II Unknown or reserved | 16 | 2 | 0 | 0 | 0 | 16 | 0 | 0 | 1 | 0 | 0 | 15 |
| II Total | 265 | 248 | 86 | 65 | 98 | 16 | 55 | 18 | 36 | 60 | 81 | 15 |
| III Lara and interface assets | 26 | 26 | 9 | 6 | 11 | 0 | 0 | 1 | 0 | 14 | 11 | 0 |
| III Enemies and NPCs | 41 | 41 | 0 | 17 | 24 | 0 | 3 | 2 | 3 | 0 | 33 | 0 |
| III Vehicles and rider props | 6 | 6 | 0 | 0 | 6 | 0 | 0 | 0 | 0 | 0 | 6 | 0 |
| III Traps and hazards | 30 | 29 | 2 | 12 | 16 | 0 | 6 | 0 | 0 | 0 | 24 | 0 |
| III Doors, switches and movers | 41 | 39 | 31 | 3 | 7 | 0 | 29 | 2 | 0 | 4 | 6 | 0 |
| III Pickups, keys and ring items | 70 | 70 | 32 | 15 | 23 | 0 | 4 | 17 | 0 | 28 | 21 | 0 |
| III Effects and emitters | 41 | 39 | 4 | 12 | 25 | 0 | 5 | 0 | 0 | 1 | 35 | 0 |
| III Scripted set pieces and markers | 36 | 36 | 1 | 15 | 20 | 0 | 0 | 0 | 0 | 10 | 26 | 0 |
| III Unknown or reserved | 83 | 4 | 0 | 0 | 0 | 83 | 0 | 0 | 0 | 0 | 0 | 83 |
| III Total | 374 | 290 | 79 | 80 | 132 | 83 | 47 | 22 | 3 | 57 | 162 | 83 |

First-playable priorities inferred from classic routes: Wall needs a real
zip line and ladder, several moving blade/spike classes, real secret grants
and credible spider/crow/tiger encounters. Venice adds boat physics, gondola,
mines, timed exit and genuinely shootable glass; a generic enemy will not
supply the key drop or sniper fight. Jungle's tiger has an actual OL AI
state machine; its crocodile/fish/monkey equivalents are **not** inferred
from names, and the monkey that can steal the exit key has no such
implementation. See §7 for the route evidence and controller questions.


Shared/dedicated implementation details for the rows without an inline
behaviour note (OL, all reusable only subject to retail comparisons):

| Factory class | What the code does; scope of the row |
|---|---|
| `Rex` / `Raptor` | Mood-driven locomotion and attack/death code; Rex has body-contact and fatal-bite paths, Raptor has gated hit-mask attacks (`enemy.h:1646–1745,1755–1855`). Named AI is real, same constants across remaps not proven retail. |
| `KeyItemInv` | Locks its model matrix at construction; a visual used during Lara's key action, not a separate key solver (`objects.h:66–70`; `lara.h:3234–3247`). |
| `Dart` / `TrapDartEmitter` | Emitter starts firing on active state, spawns on animation; dart moves, collides, hurts or removes itself at floor/ceiling/room edge. No TR3 poison or confirmed TR2 flying-disc-specific timing (`objects.h:72–127`; §2). |
| `Drawbridge` / `TrapCeiling` / `TrapSlam` / `TrapSpikes` | Dedicated trigger/update/floor or contact paths: active bridge provides a floor; ceiling animates/falls/hurts; slam has a per-cycle hit latch; spikes test running/falling. No generic claim for spiky walls or different placements (`objects.h:842–890,982–1075`). |
| `Flame` / `TrapFlameEmitter` / `TrapLavaEmitter` | Active flame spawn/teardown and burn/environment interactions; lava particles simulated inside emitter, not a placed `LAVA_PARTICLE` instance (`objects.h:133–227,306–425`); roles reached, no version-specific tuning evidence. |
| `Bubble` / `Waterfall` / `Explosion` / `Sprite` | Bubble rises with water-adjusted lifetime, waterfall emits drops near the viewer, explosion/sprite run their authored sequence with the chosen frame policy. This is visual/audio FX, not destructive geometry (`objects.h:1606–1689`; `sprite.h:6–124`). |
| `MuzzleFlash` / `Earthquake` | Flash is a transient weapon attachment; quake periodically shakes/plays sound while active, not a proof of sequel environment and trigger semantics (`objects.h:229–279,1579–1604`). |

### 3.2 TR2 complete slot list (RS §15.1.2)

#### Lara and interface assets

| ID | RS name or — | OL `format.h` symbol:line | TR1 status / counterpart | OL coverage |
|---:|---|---|---|---|
| 0 | Lara | `_LARA:216` | V: Lara | P — Lara, §2 |
| 1 | Lara pistols animation | `_LARA_PISTOLS:217` | S: pistols arm animations | A — model consumer `format.h:4840–4878`; §2 |
| 2 | Lara’s ponytail | `_LARA_BRAID:218` | V: Lara pose mesh (separate hair) | A — model consumer `format.h:4840–4878`; §2 |
| 3 | Lara shotgun animation | `_LARA_SHOTGUN:219` | S: shotgun arm animations | A — model consumer `format.h:4840–4878`; §2 |
| 4 | Lara automags animation | `LARA_AUTOPISTOLS:220` | V: Magnums / ammunition | B |
| 5 | Lara uzis animation | `_LARA_UZIS:221` | S: Uzi arm animations | A — model consumer `format.h:4840–4878`; §2 |
| 6 | Lara M16 animation | `LARA_M16:222` | N | B |
| 7 | Lara grenade gun animation | `LARA_GRENADE:223` | N | B |
| 8 | Lara harpoon gun animation | `LARA_HARPOON:224` | N | B |
| 9 | Lara flare animation | `LARA_FLARE:225` | N | B |
| 10 | Lara snowmobile animation | `LARA_SNOWMOBILE:226` | N | B |
| 11 | Lara boat animation | `LARA_BOAT:227` | N | B |
| 12 | Lara misc animations | `_LARA_SPEC:228` | V: Lara | A — model consumer `format.h:4840–4878`; §2 |
| 100 | UI frame | `UI_FRAME:316` | N | B |
| 120 | Secret 1 | `_INV_PASSPORT:336` | N | A — ring `inventory.h:273` |
| 121 | Secret 2 | `INV_STOPWATCH:337` | N | A — ring `inventory.h:277` |
| 122 | Lara’s home photo | `_INV_HOME:338` | S: training photo | A — ring `inventory.h:279` |
| 133 | Secret 3 | `_INV_PASSPORT_CLOSED:349` | N | A — ring `inventory.h:274` |
| 134 | Map | `_INV_MAP:350` | N | A — ring `inventory.h:275` |
| 153 | Sunglasses | `_INV_DETAIL:369` | S: detail menu | A — ring `inventory.h:280` |
| 154 | CD player | `_INV_SOUND:370` | S: sound menu | A — ring `inventory.h:281` |
| 155 | Direction keys | `_INV_CONTROLS:371` | S: controls menu | A — ring `inventory.h:282` |
| 223 | Menu background | `INV_BACKGROUND:439` | N | B |
| 255 | Font graphics | `_GLYPHS:471` | N | A — font `format.h:4910–4916` |

#### Enemies and NPCs

| ID | RS name or — | OL `format.h` symbol:line | TR1 status / counterpart | OL coverage |
|---:|---|---|---|---|
| 15 | Doberman | `ENEMY_DOG:231` | V: Wolf | P — Dog `level.h:1259`; sleep/death only, `enemy.h:3236–3258` |
| 16 | Masked goon 1 | `ENEMY_GOON_MASK_1:232` | V: Pierre / Mr T | G — generic Enemy `level.h:1296` |
| 17 | Masked goon 2 | `ENEMY_GOON_MASK_2:233` | V: Pierre / Mr T | G — generic Enemy `level.h:1296` |
| 18 | Masked goon 3 | `ENEMY_GOON_MASK_3:234` | V: Pierre / Mr T | G — generic Enemy `level.h:1296` |
| 19 | Knifethrower | `ENEMY_GOON_KNIFE:235` | N | G — generic Enemy `level.h:1296` |
| 20 | Shotgun goon | `ENEMY_GOON_SHOTGUN:236` | V: Cowboy (ranged gunman family) | G — generic Enemy `level.h:1296` |
| 21 | Rat | `ENEMY_RAT:237` | V: Rat | G — generic Enemy `level.h:1296` |
| 22 | Dragon (front) | `ENEMY_DRAGON_FRONT:238` | N | G — generic Enemy `level.h:1296` |
| 23 | Dragon (back) | `ENEMY_DRAGON_BACK:239` | N | G — generic Enemy `level.h:1296` |
| 25 | Shark | `ENEMY_SHARK:241` | V: water Crocodile (navigation family) | G — generic Enemy `level.h:1296` |
| 26 | Yellow moray eel | `ENEMY_MORAY_1:242` | V: water Crocodile (navigation family) | G — generic Enemy `level.h:1296` |
| 27 | Black moray eel | `ENEMY_MORAY_2:243` | V: water Crocodile (navigation family) | G — generic Enemy `level.h:1296` |
| 28 | Barracuda | `ENEMY_BARACUDA:244` | V: water Crocodile (navigation family) | G — generic Enemy `level.h:1296` |
| 29 | Scuba diver | `ENEMY_DIVER:245` | V: water Crocodile (navigation family) | G — generic Enemy `level.h:1296` |
| 30 | Gunman 1 | `ENEMY_GUNMAN_1:246` | V: Pierre / Mr T | G — generic Enemy `level.h:1296` |
| 31 | Gunman 2 | `ENEMY_GUNMAN_2:247` | V: Pierre / Mr T | G — generic Enemy `level.h:1296` |
| 32 | Stick-wielding goon 1 | `ENEMY_GOON_STICK_1:248` | N | G — generic Enemy `level.h:1296` |
| 33 | Stick-wielding goon 2 | `ENEMY_GOON_STICK_2:249` | N | G — generic Enemy `level.h:1296` |
| 34 | Flamethrower goon | `ENEMY_GOON_FLAME:250` | N | G — generic Enemy `level.h:1296` |
| 36 | Spider | `ENEMY_SPIDER:252` | N | G — generic Enemy `level.h:1296` |
| 37 | Giant spider | `ENEMY_SPIDER_GIANT:253` | N | G — generic Enemy `level.h:1296` |
| 38 | Crow | `ENEMY_CROW:254` | V: Bat (flying AI) | G — generic Enemy `level.h:1296` |
| 39 | Tiger / Snow leopard | `ENEMY_TIGER:255` | V: Puma | D — Tiger `level.h:1260`; melee/moods/death, `enemy.h:3260–3382` |
| 40 | Marco Bartoli | `ENEMY_MARCO:256` | N | G — generic Enemy `level.h:1296` |
| 41 | Xian Guard w/spear | `ENEMY_GUARD_SPEAR:257` | N | G — generic Enemy `level.h:1296` |
| 43 | Xian Guard w/sword | `ENEMY_GUARD_SWORD:259` | N | G — generic Enemy `level.h:1296` |
| 45 | Yeti | `ENEMY_YETI:261` | N | G — generic Enemy `level.h:1296` |
| 46 | Bird monster (guards Talion) | `ENEMY_BIRD_MONSTER:262` | N | G — generic Enemy `level.h:1296` |
| 47 | Eagle | `ENEMY_EAGLE:263` | V: Bat (flying AI) | G — generic Enemy `level.h:1296` |
| 48 | Mercenary 1 | `ENEMY_MERCENARY_1:264` | V: Pierre / Mr T | G — generic Enemy `level.h:1296` |
| 49 | Mercenary 2 | `ENEMY_MERCENARY_2:265` | V: Pierre / Mr T | G — generic Enemy `level.h:1296` |
| 50 | Mercenary 3 | `ENEMY_MERCENARY_3:266` | V: Pierre / Mr T | G — generic Enemy `level.h:1296` |
| 52 | Mercenary snowmobile driver | `ENEMY_MERCENARY_SNOWMOBILE:268` | N | G — generic Enemy `level.h:1296` |
| 53 | Monk with long stick | `ENEMY_MONK_1:269` | N | G — generic Enemy `level.h:1296` |
| 54 | Monk with knife-end stick | `ENEMY_MONK_2:270` | N | G — generic Enemy `level.h:1296` |
| 214 | T-Rex | `_ENEMY_REX:430` | V: T-rex | D — Rex `level.h:1161` |
| 256 | Monk | `ENEMY_MONK_3:472` | N | B |
| 260 | Winston | `ENEMY_WINSTON:476` | N | P — Winston `level.h:1297`; follow/freeze, not camo, `enemy.h:3389–3517` |

#### Vehicles and rider props

| ID | RS name or — | OL `format.h` symbol:line | TR1 status / counterpart | OL coverage |
|---:|---|---|---|---|
| 13 | Red snowmobile | `VEHICLE_SNOWMOBILE_RED:229` | N | B |
| 14 | Boat | `VEHICLE_BOAT:230` | N | B |
| 24 | Gondola | `GONDOLA:240` | N | B |
| 51 | Black snowmobile | `VEHICLE_SNOWMOBILE_BLACK:267` | N | B |
| 66 | Minisub | `MINISUB:282` | N | B |
| 91 | Snowmobile belt | `SNOWMOBILE_BELT:307` | N | B |

#### Traps and hazards

| ID | RS name or — | OL `format.h` symbol:line | TR1 status / counterpart | OL coverage |
|---:|---|---|---|---|
| 55 | Falling block | `_TRAP_FLOOR:271` | V: Trap floor | D — TrapFloor `level.h:1198`; shake/fall, `objects.h:792–835` |
| 57 | Loose boards | `TRAP_BOARDS:273` | V: Trap floor | B |
| 58 | Swinging sandbag / Spiky ball | `TRAP_SWING_BAG:274` | V: Swinging blade (moving contact hazard) | B |
| 59 | Teeth spikes / Glass shards | `_TRAP_SPIKES:275` | V: Spikes | D — TrapSpikes `level.h:1201` |
| 60 | Rolling ball | `_TRAP_BOULDER:276` | V: Boulder | D — TrapBoulder `level.h:1203`; rolling/collision, `objects.h:430–510` |
| 61 | Disc | `_DART:277` | V: Dart | D — Dart `level.h:1204` |
| 62 | Discgun | `_TRAP_DART_EMITTER:278` | V: Dart emitter | D — TrapDartEmitter `level.h:1205` |
| 64 | Slamming door | `_TRAP_SLAM:280` | S: slamming door | D — TrapSlam `level.h:1215` |
| 71 | Lava bowl | `LAVA_BOWL:287` | V: Lava emitter (hazard family) | B |
| 76 | Airplane propeller | `PROPELLER_PLANE:292` | N | B |
| 77 | Power saw | `SAW:293` | N | B |
| 78 | Overhead pulley hook | `HOOK:294` | N | B |
| 79 | Falling ceiling / Sandbag | `_TRAP_CEILING_1:295` | S: falling ceiling | D — TrapCeiling `level.h:1214` |
| 80 | Rolling spindle | `TRAP_SPINDLE:296` | V: Swinging blade (moving contact hazard) | B |
| 81 | Wall-mounted knife blade | `TRAP_BLADE_WALL:297` | V: Swinging blade (moving contact hazard) | B |
| 82 | Statue with knife blade | `TRAP_BLADE_STATUE:298` | V: Swinging blade (moving contact hazard) | B |
| 83 | Multiple boulders / snowballs | `TRAP_BOULDERS:299` | V: Boulder | D — TrapBoulder `level.h:1203`; rolling/collision, `objects.h:430–510` |
| 84 | Detachable icicles | `TRAP_ICICLES:300` | N | B |
| 85 | Spiky wall | `TRAP_SPIKES_WALL:301` | V: Moving bar + spikes | B |
| 86 | Bounce pad | `JUMPPAD:302` | N | B |
| 87 | Spiky ceiling | `TRAP_SPIKES_CEILING:303` | V: Moving bar + spikes | B |
| 94 | Underwater propeller | `PROPELLER_WATER:310` | N | B |
| 95 | Air fan | `PROPELLER_AIR:311` | N | B |
| 96 | Swinging box / spiky ball | `TRAP_SWING_BOX:312` | V: Swinging blade (moving contact hazard) | B |
| 101 | Rolling storage drums | `ROLLING_DRUMS:317` | V: Boulder | B |
| 220 | Extra fire | `TRAP_EXTRA_FIRE:436` | V: Flame emitter | B |
| 221 | — | `TRAP_MINE:437` | N | B |
| 222 | Aquatic Mine (Venice) | `UNUSED_30:438` | N | B |

#### Doors, switches and movers

| ID | RS name or — | OL `format.h` symbol:line | TR1 status / counterpart | OL coverage |
|---:|---|---|---|---|
| 63 | Drawbridge | `_DRAWBRIDGE:279` | S: drawbridge | D — Drawbridge `level.h:1206` |
| 65 | Elevator | `ELEVATOR:281` | N | B |
| 67 | Pushable block 1 | `_BLOCK_1:283` | S: block 1 | D — Block `level.h:1211`; push/pull/heavy, `objects.h:511–612` |
| 68 | Pushable block 2 | `_BLOCK_2:284` | S: block 2 | D — Block `level.h:1211`; push/pull/heavy, `objects.h:511–612` |
| 69 | Pushable block 3 | `_BLOCK_3:285` | S: block 3 | D — Block `level.h:1211`; push/pull/heavy, `objects.h:511–612` |
| 70 | Pushable block 4 | `_BLOCK_4:286` | S: block 4 | D — Block `level.h:1211`; push/pull/heavy, `objects.h:511–612` |
| 72 | Breakable window 1 | `WINDOW_1:288` | N | P — BreakableWindow `level.h:1306`; mesh mask only, `objects.h:1691–1697` |
| 73 | Breakable window 2 | `WINDOW_2:289` | N | P — BreakableWindow `level.h:1306`; mesh mask only, `objects.h:1691–1697` |
| 92 | Wheel knob | `WHEEL_KNOB:308` | N | B |
| 93 | Small wall switch | `SWITCH_BIG:309` | V: Land switch | D — Switch `level.h:1224`; timer/state, `objects.h:9–55` |
| 102 | Zipline handle | `ZIPLINE_HANDLE:318` | N | B |
| 103 | Push-button switch | `SWITCH_BUTTON:319` | V: Land switch | D — Switch `level.h:1224`; timer/state, `objects.h:9–55` |
| 104 | Wall switch | `_SWITCH:320` | S: switch | D — Switch `level.h:1224`; timer/state, `objects.h:9–55` |
| 105 | Underwater switch | `_SWITCH_WATER:321` | S: switch water | D — Switch `level.h:1224`; timer/state, `objects.h:9–55` |
| 106 | Door 1 | `_DOOR_1:322` | S: door 1 | D — Door `level.h:1185`; portal/solid updates, `objects.h:672–768` |
| 107 | Door 2 | `_DOOR_2:323` | S: door 2 | D — Door `level.h:1185`; portal/solid updates, `objects.h:672–768` |
| 108 | Door 3 | `_DOOR_3:324` | S: door 3 | D — Door `level.h:1185`; portal/solid updates, `objects.h:672–768` |
| 109 | Door 4 | `_DOOR_4:325` | S: door 4 | D — Door `level.h:1185`; portal/solid updates, `objects.h:672–768` |
| 110 | Door 5 | `_DOOR_5:326` | S: door 5 | D — Door `level.h:1185`; portal/solid updates, `objects.h:672–768` |
| 111 | Lifting door 1 | `_DOOR_6:327` | V: Door (lift motion) | D — Door `level.h:1185`; portal/solid updates, `objects.h:672–768` |
| 112 | Lifting door 2 | `_DOOR_7:328` | V: Door (lift motion) | D — Door `level.h:1185`; portal/solid updates, `objects.h:672–768` |
| 113 | Lifting door 3 | `_DOOR_8:329` | V: Door (lift motion) | D — Door `level.h:1185`; portal/solid updates, `objects.h:672–768` |
| 114 | Trapdoor 1 | `_TRAP_DOOR_1:330` | S: trap door 1 | D — TrapDoor `level.h:1187`; state/floor, `objects.h:769–791` |
| 115 | Trapdoor 2 | `_TRAP_DOOR_2:331` | S: trap door 2 | D — TrapDoor `level.h:1187`; state/floor, `objects.h:769–791` |
| 116 | Trapdoor 3 | `TRAP_DOOR_3:332` | V: Trapdoor | B |
| 117 | Bridge flat | `_BRIDGE_1:333` | S: flat bridge | D — Bridge `level.h:1190`; floor based, `objects.h:838–841` |
| 118 | Bridge tilt 1 | `_BRIDGE_2:334` | S: tilted bridge 1 | D — Bridge `level.h:1190`; floor based, `objects.h:838–841` |
| 119 | Bridge tilt 2 | `_BRIDGE_3:335` | S: tilted bridge 2 | D — Bridge `level.h:1190`; floor based, `objects.h:838–841` |
| 182 | Puzzle hole 1 | `_PUZZLE_HOLE_1:398` | S: puzzle hole 1 | D — KeyHole `level.h:1232`; key/puzzle, `objects.h:1557–1612` |
| 183 | Puzzle hole 2 | `_PUZZLE_HOLE_2:399` | S: puzzle hole 2 | D — KeyHole `level.h:1232`; key/puzzle, `objects.h:1557–1612` |
| 184 | Puzzle hole 3 | `_PUZZLE_HOLE_3:400` | S: puzzle hole 3 | D — KeyHole `level.h:1232`; key/puzzle, `objects.h:1557–1612` |
| 185 | Puzzle hole 4 | `_PUZZLE_HOLE_4:401` | S: puzzle hole 4 | D — KeyHole `level.h:1232`; key/puzzle, `objects.h:1557–1612` |
| 186 | Puzzle done 1 | `_PUZZLE_DONE_1:402` | S: puzzle done 1 | A — filled mesh swap `objects.h:1557–1612`; `format.h:4848–4852` |
| 187 | Puzzle done 2 | `_PUZZLE_DONE_2:403` | S: puzzle done 2 | A — filled mesh swap `objects.h:1557–1612`; `format.h:4848–4852` |
| 188 | Puzzle done 3 | `_PUZZLE_DONE_3:404` | S: puzzle done 3 | A — filled mesh swap `objects.h:1557–1612`; `format.h:4848–4852` |
| 189 | Puzzle done 4 | `_PUZZLE_DONE_4:405` | S: puzzle done 4 | A — filled mesh swap `objects.h:1557–1612`; `format.h:4848–4852` |
| 201 | Keyhole 1 | `_KEY_HOLE_1:417` | S: keyhole | D — KeyHole `level.h:1232`; key/puzzle, `objects.h:1557–1612` |
| 202 | Keyhole 2 | `_KEY_HOLE_2:418` | S: key hole 2 | D — KeyHole `level.h:1232`; key/puzzle, `objects.h:1557–1612` |
| 203 | Keyhole 3 | `_KEY_HOLE_3:419` | S: key hole 3 | D — KeyHole `level.h:1232`; key/puzzle, `objects.h:1557–1612` |
| 204 | Keyhole 4 | `_KEY_HOLE_4:420` | S: key hole 4 | D — KeyHole `level.h:1232`; key/puzzle, `objects.h:1557–1612` |
| 226 | Gong | `GONG:442` | N | B |
| 227 | Detonator box | `DETONATOR:443` | N | B |

#### Pickups, keys and ring items

| ID | RS name or — | OL `format.h` symbol:line | TR1 status / counterpart | OL coverage |
|---:|---|---|---|---|
| 135 | Pistols | `_PISTOLS:351` | S: pistols | A — Lara pickup/ring, §2 |
| 136 | Shotgun | `_SHOTGUN:352` | S: shotgun | A — Lara pickup/ring, §2 |
| 137 | Automags | `AUTOPISTOLS:353` | V: Magnums / ammunition | P — Lara pickup predicate; ring/use missing, §2 |
| 138 | Uzi | `_UZIS:354` | S: uzis | A — Lara pickup/ring, §2 |
| 139 | Harpoon gun | `HARPOON:355` | N | P — Lara pickup predicate; ring/use missing, §2 |
| 140 | M16 | `M16:356` | N | P — Lara pickup predicate; ring/use missing, §2 |
| 141 | Grenade gun | `GRENADE:357` | N | P — Lara pickup predicate; ring/use missing, §2 |
| 142 | Pistol ammo | `_AMMO_PISTOLS:358` | S: ammo pistols | A — Lara pickup/ring, §2 |
| 143 | Shotgun ammo | `_AMMO_SHOTGUN:359` | S: ammo shotgun | A — Lara pickup/ring, §2 |
| 144 | Auto-pistol ammo | `AMMO_AUTOPISTOLS:360` | V: Magnums / ammunition | P — Lara pickup predicate; ring/use missing, §2 |
| 145 | Uzi ammo | `_AMMO_UZIS:361` | S: ammo uzis | A — Lara pickup/ring, §2 |
| 146 | Harpoon gun ammo | `AMMO_HARPOON:362` | N | P — Lara pickup predicate; ring/use missing, §2 |
| 147 | M16 ammo | `AMMO_M16:363` | N | P — Lara pickup predicate; ring/use missing, §2 |
| 148 | Grenadegun ammo | `AMMO_GRENADE:364` | N | P — Lara pickup predicate; ring/use missing, §2 |
| 149 | Small medipack | `_MEDIKIT_SMALL:365` | S: small medikit | A — Lara pickup/ring, §2 |
| 150 | Large medipack | `_MEDIKIT_BIG:366` | S: large medikit | A — Lara pickup/ring, §2 |
| 151 | Flares | `FLARES:367` | N | P — Lara pickup predicate; ring/use missing, §2 |
| 152 | Flare | `FLARE:368` | N | P — Lara pickup predicate; ring/use missing, §2 |
| 157 | Pistols | `_INV_PISTOLS:373` | S: inv pistols | A — ring `inventory.h:285` |
| 158 | Shotgun | `_INV_SHOTGUN:374` | S: inv shotgun | A — ring `inventory.h:286` |
| 159 | Automags | `INV_AUTOPISTOLS:375` | V: Magnums / ammunition | B |
| 160 | Uzi | `_INV_UZIS:376` | S: inv uzis | A — ring `inventory.h:288` |
| 161 | Harpoon gun | `INV_HARPOON:377` | N | B |
| 162 | M16 | `INV_M16:378` | N | B |
| 163 | Grenade gun | `INV_GRENADE:379` | N | B |
| 164 | Pistol ammo | `_INV_AMMO_PISTOLS:380` | S: inv ammo pistols | A — ring `inventory.h:290` |
| 165 | Shotgun ammo | `_INV_AMMO_SHOTGUN:381` | S: inv ammo shotgun | A — ring `inventory.h:291` |
| 166 | Automag ammo | `INV_AMMO_AUTOPISTOLS:382` | V: Magnums / ammunition | B |
| 167 | Uzi ammo | `_INV_AMMO_UZIS:383` | S: inv ammo uzis | A — ring `inventory.h:293` |
| 168 | Harpoon gun ammo | `INV_AMMO_HARPOON:384` | N | B |
| 169 | M16 ammo | `INV_AMMO_M16:385` | N | B |
| 170 | Grenadegun ammo | `INV_AMMO_GRENADE:386` | N | B |
| 171 | Small medipack | `_INV_MEDIKIT_SMALL:387` | S: inv medikit small | A — ring `inventory.h:295` |
| 172 | Large medipack | `_INV_MEDIKIT_BIG:388` | S: inv medikit big | A — ring `inventory.h:296` |
| 173 | Flares | `INV_FLARES:389` | N | B |
| 174 | Puzzle 1 | `_PUZZLE_1:390` | S: puzzle 1 | A — Lara pickup/ring, §2 |
| 175 | Puzzle 2 | `_PUZZLE_2:391` | S: puzzle 2 | A — Lara pickup/ring, §2 |
| 176 | Puzzle 3 | `_PUZZLE_3:392` | S: puzzle 3 | A — Lara pickup/ring, §2 |
| 177 | Puzzle 4 | `_PUZZLE_4:393` | S: puzzle 4 | A — Lara pickup/ring, §2 |
| 178 | Puzzle 1 | `_INV_PUZZLE_1:394` | S: inv puzzle 1 | A — ring `inventory.h:298` |
| 179 | Puzzle 2 | `_INV_PUZZLE_2:395` | S: inv puzzle 2 | A — ring `inventory.h:299` |
| 180 | Puzzle 3 | `_INV_PUZZLE_3:396` | S: inv puzzle 3 | A — ring `inventory.h:300` |
| 181 | Puzzle 4 | `_INV_PUZZLE_4:397` | S: inv puzzle 4 | A — ring `inventory.h:301` |
| 190 | Secret 1 | `SECRET_1:406` | V: Secret trigger (collected statuette) | P — Lara pickup predicate; ring/use missing, §2 |
| 191 | Secret 2 | `SECRET_2:407` | V: Secret trigger (collected statuette) | P — Lara pickup predicate; ring/use missing, §2 |
| 192 | Secret 3 | `SECRET_3:408` | V: Secret trigger (collected statuette) | P — Lara pickup predicate; ring/use missing, §2 |
| 193 | Key 1 | `_KEY_ITEM_1:409` | S: key item 1 | A — Lara pickup/ring, §2 |
| 194 | Key 2 | `_KEY_ITEM_2:410` | S: key item 2 | A — Lara pickup/ring, §2 |
| 195 | Key 3 | `_KEY_ITEM_3:411` | S: key item 3 | A — Lara pickup/ring, §2 |
| 196 | Key 4 | `_KEY_ITEM_4:412` | S: key item 4 | A — Lara pickup/ring, §2 |
| 197 | Key 1 | `_INV_KEY_ITEM_1:413` | S: inv key item 1 | D — KeyItemInv `level.h:1197` |
| 198 | Key 2 | `_INV_KEY_ITEM_2:414` | S: inv key item 2 | D — KeyItemInv `level.h:1197` |
| 199 | Key 3 | `_INV_KEY_ITEM_3:415` | S: inv key item 3 | D — KeyItemInv `level.h:1197` |
| 200 | Key 4 | `_INV_KEY_ITEM_4:416` | S: inv key item 4 | D — KeyItemInv `level.h:1197` |
| 205 | Quest item 1 | `QUEST_ITEM_1:421` | N | B |
| 206 | Quest item 2 | `QUEST_ITEM_2:422` | N | B |
| 207 | Quest item 1 | `INV_QUEST_ITEM_1:423` | N | B |
| 208 | Quest item 2 | `INV_QUEST_ITEM_2:424` | N | B |

#### Effects and emitters

| ID | RS name or — | OL `format.h` symbol:line | TR1 status / counterpart | OL coverage |
|---:|---|---|---|---|
| 89 | — | `WAKE_BOAT:305` | N | B |
| 90 | — | `WAKE_SNOWMOBILE:306` | N | B |
| 209 | Dragon explosion effect 1 | `DRAGON_EXPLOSION_1:425` | N | B |
| 210 | Dragon explosion effect 2 | `DRAGON_EXPLOSION_2:426` | N | B |
| 211 | Dragon explosion effect 3 | `DRAGON_EXPLOSION_3:427` | N | B |
| 212 | Alarm | `SOUND_ALARM:428` | N | B |
| 213 | Dripping water | `SOUND_WATER:429` | N | B |
| 215 | Singing birds | `SOUND_BIRD:431` | N | B |
| 216 | Bartoli’s Hideout clock | `SOUND_CLOCK:432` | N | B |
| 217 | Placeholder | `SOUND_PLACEHOLDER:433` | N | B |
| 229 | Explosion | `_EXPLOSION:445` | S: explosion | D — Explosion `level.h:1241` |
| 230 | Water ripples | `_WATER_SPLASH:446` | S: water splash | D — Sprite `level.h:1245` |
| 231 | Bubbles | `_BUBBLE:447` | S: bubble | D — Bubble `level.h:1240` |
| 233 | Blood | `_BLOOD:449` | S: blood | D — Sprite `level.h:1245` |
| 235 | Flare sparkles | `FLARE_SPARKLES:451` | N | B |
| 236 | Glow | `MUZZLE_GLOW:452` | N | B |
| 238 | Ricochet | `_RICOCHET:454` | S: ricochet | D — Sprite `level.h:1246` |
| 240 | Gunflare | `_MUZZLE_FLASH:456` | S: muzzle flash | D — MuzzleFlash `level.h:1249` |
| 241 | M16 gunflare | `MUZZLE_FLASH_M16:457` | V: Muzzle flash | B |
| 244 | Waterfall mist | `_WATERFALL:460` | S: waterfall | D — Waterfall `level.h:1235` |
| 245 | Harpoon | `HARPOON_WTF:461` | N | B |
| 248 | Grenade (single) | `GRENADE_BULLET:464` | N | B |
| 249 | Harpoon (flying) | `HARPOON_BULLET:465` | N | B |
| 250 | Lava particles | `_LAVA_PARTICLE:466` | S: lava particle | A — emitter-owned particle `level.h:1250–1251`; `objects.h:306–387` |
| 251 | Lava / air particle emitter | `_TRAP_LAVA_EMITTER:467` | S: lava emitter | D — TrapLavaEmitter `level.h:1251` |
| 252 | Flame | `_FLAME:468` | S: flame | D — Flame `level.h:1252` |
| 253 | Flame emitter | `_TRAP_FLAME_EMITTER:469` | S: flame emitter | D — TrapFlameEmitter `level.h:1253` |
| 254 | Skybox | `SKY:470` | N | A — sky `level.h:1871–1937` |
| 257 | Door bell | `SOUND_DOOR_BELL:473` | N | B |
| 258 | Alarm bell | `SOUND_ALARM_BELL:474` | N | B |
| 264 | Dragon explosion emitter | `DRAGON_EXPLSION_EMITTER:480` | N | B |

#### Scripted set pieces and markers

| ID | RS name or — | OL `format.h` symbol:line | TR1 status / counterpart | OL coverage |
|---:|---|---|---|---|
| 42 | Xian Guard w/spear statue | `ENEMY_GUARD_SPEAR_STATUE:258` | V: Centaur statue | G — generic Enemy `level.h:1296` |
| 44 | Xian Guard w/sword statue | `ENEMY_GUARD_SWORD_STATUE:260` | V: Centaur statue | G — generic Enemy `level.h:1296` |
| 88 | Tibetan bell | `BELL:304` | N | B |
| 97 | Cutscene actor 1 | `_CUT_1:313` | S: cut 1 | A — cut actors `format.h:2504–2506`; `level.h:2181–2229` |
| 98 | Cutscene actor 2 | `_CUT_2:314` | S: cut 2 | A — cut actors `format.h:2504–2506`; `level.h:2181–2229` |
| 99 | Cutscene actor 3 | `_CUT_3:315` | S: cut 3 | A — cut actors `format.h:2504–2506`; `level.h:2181–2229` |
| 123 | Cutscene actor 4 | `_CUT_4:339` | S: cut 4 | A — cut actors `format.h:2504–2506`; `level.h:2181–2229` |
| 124 | Cutscene actor 5 | `CUT_5:340` | V: Cutscene actor | A — cut actors `format.h:2504–2506`; `level.h:2181–2229` |
| 125 | Cutscene actor 6 | `CUT_6:341` | V: Cutscene actor | A — cut actors `format.h:2504–2506`; `level.h:2181–2229` |
| 126 | Cutscene actor 7 | `CUT_7:342` | V: Cutscene actor | A — cut actors `format.h:2504–2506`; `level.h:2181–2229` |
| 127 | Cutscene actor 8 | `CUT_8:343` | V: Cutscene actor | A — cut actors `format.h:2504–2506`; `level.h:2181–2229` |
| 128 | Cutscene actor 9 | `CUT_9:344` | V: Cutscene actor | A — cut actors `format.h:2504–2506`; `level.h:2181–2229` |
| 129 | Cutscene actor 10 | `CUT_10:345` | V: Cutscene actor | A — cut actors `format.h:2504–2506`; `level.h:2181–2229` |
| 130 | Cutscene actor 11 | `CUT_11:346` | V: Cutscene actor | A — cut actors `format.h:2504–2506`; `level.h:2181–2229` |
| 218 | Dragon bones (front) | `DRAGON_BONES_FRONT:434` | N | B |
| 219 | Dragon bones (back) | `DRAGON_BONES_BACK:435` | N | B |
| 225 | Gong stick | `GONG_STICK:441` | N | B |
| 228 | Helicopter (Diving Area) | `HELICOPTER:444` | N | B |
| 243 | Camera target | `_VIEW_TARGET:459` | S: camera view target | A — camera target `lara.h:1970–1989` |
| 259 | Helicopter | `HELICOPTER_FLYING:475` | N | D — HelicopterFlying `level.h:1308`; moves/fades/removes, `objects.h:1703–1727` |
| 262 | Lara cutscene placement | `LARA_END_PLACE:478` | V: Lara | B |
| 263 | Shotgun animation | `LARA_END_SHOTGUN:479` | N | B |

#### Unknown or reserved

| ID | RS name or — | OL `format.h` symbol:line | TR1 status / counterpart | OL coverage |
|---:|---|---|---|---|
| 35 | — | `UNUSED_23:251` | ? | G — generic Enemy `level.h:1296` |
| 56 | — | `UNUSED_24:272` | ? | — |
| 74 | — | `UNUSED_25:290` | ? | — |
| 75 | — | `UNUSED_26:291` | ? | — |
| 131 | — | `UNUSED_27:347` | ? | — |
| 132 | — | `UNUSED_28:348` | ? | — |
| 156 | — | `UNUSED_29:372` | ? | — |
| 224 | Gray disk | `GRAY_DISK:440` | ? | — |
| 232 | — | `UNUSED_31:448` | ? | — |
| 234 | — | `UNUSED_32:450` | ? | — |
| 237 | — | `UNUSED_33:453` | ? | — |
| 239 | — | `UNUSED_34:455` | ? | — |
| 242 | — | `UNUSED_35:458` | ? | — |
| 246 | — | `UNUSED_36:462` | ? | — |
| 247 | Placeholder | `UNUSED_37:463` | ? | — |
| 261 | — | `UNUSED_38:477` | ? | — |

### 3.3 TR3 complete slot list (RS §15.1.3)

#### Lara and interface assets

| ID | RS name or — | OL `format.h` symbol:line | TR1 status / counterpart | OL coverage |
|---:|---|---|---|---|
| 0 | Lara | `__LARA:482` | V: Lara | P — Lara, §2 |
| 1 | Lara pistol animation | `__LARA_PISTOLS:483` | S: pistols arm animations | A — model consumer `format.h:4840–4878`; §2 |
| 2 | Lara’s ponytail | `__LARA_BRAID:484` | V: Lara pose mesh (separate hair) | A — model consumer `format.h:4840–4878`; §2 |
| 3 | Lara shotgun animation | `__LARA_SHOTGUN:485` | S: shotgun arm animations | A — model consumer `format.h:4840–4878`; §2 |
| 4 | Lara Desert Eagle animation | `LARA_DESERT_EAGLE:486` | V: Magnums / ammunition | B |
| 5 | Lara Uzi animation | `__LARA_UZIS:487` | S: Uzi arm animations | A — model consumer `format.h:4840–4878`; §2 |
| 6 | Lara MP5 animation | `LARA_MP5:488` | N | B |
| 7 | Lara rocket launcher animation | `LARA_ROCKET:489` | N | B |
| 8 | Lara grenade gun animation | `__LARA_GRENADE:490` | N | B |
| 9 | Lara harpoon gun animation | `__LARA_HARPOON:491` | N | B |
| 10 | Lara flare animation | `__LARA_FLARE:492` | N | B |
| 11 | Lara UPV animation | `LARA_UPV:493` | N | B |
| 125 | UI frame | `__UI_FRAME:607` | N | B |
| 145 | Passport (opening up) | `__INV_PASSPORT:627` | S: passport | A — ring `inventory.h:273` |
| 146 | Stopwatch | `__INV_STOPWATCH:628` | V: Compass/statistics menu | A — ring `inventory.h:277` |
| 147 | Lara’s Home photo | `__INV_HOME:629` | S: training photo | A — ring `inventory.h:279` |
| 158 | Passport (closed) | `__INV_PASSPORT_CLOSED:640` | S: closed passport | A — ring `inventory.h:274` |
| 159 | Map | `__INV_MAP:641` | N | A — ring `inventory.h:275` |
| 181 | Sunglasses | `__INV_DETAIL:663` | S: detail menu | A — ring `inventory.h:280` |
| 182 | Portable CD Player | `__INV_SOUND:664` | S: sound menu | A — ring `inventory.h:281` |
| 183 | Direction keys | `__INV_CONTROLS:665` | S: controls menu | A — ring `inventory.h:282` |
| 184 | Globe | `INV_GLOBE:666` | N | B |
| 204 | Savegame crystal | `INV_CRYSTAL:686` | V: Save crystal (different use) | B |
| 315 | Lara skin | `LARA_SKIN:797` | V: Lara body mesh | A — model consumer `format.h:4840–4878`; §2 |
| 356 | Font graphics | `__GLYPHS:838` | N | A — font `format.h:4910–4916` |
| 362 | Timer font graphics | `GLYPHS_TIMER:844` | N | B |

#### Enemies and NPCs

| ID | RS name or — | OL `format.h` symbol:line | TR1 status / counterpart | OL coverage |
|---:|---|---|---|---|
| 20 | Tribesman with spiked axe | `ENEMY_TRIBESMAN_AXE:502` | N | B |
| 21 | Tribesman with poison-dart gun | `ENEMY_TRIBESMAN_DART:503` | N | B |
| 22 | Dog | `__ENEMY_DOG:504` | V: Wolf | P — Dog `level.h:1259`; sleep/death only, `enemy.h:3236–3258` |
| 23 | Rat | `__ENEMY_RAT:505` | V: Rat | G — generic Enemy `level.h:1296` |
| 25 | Killer whale | `ENEMY_WHALE:507` | N | B |
| 26 | Scuba diver | `__ENEMY_DIVER:508` | N | G — generic Enemy `level.h:1296` |
| 27 | Crow | `__ENEMY_CROW:509` | V: Bat (flying AI) | G — generic Enemy `level.h:1296` |
| 28 | Tiger | `__ENEMY_TIGER:510` | V: Puma | D — Tiger `level.h:1260`; melee/moods/death, `enemy.h:3260–3382` |
| 29 | Vulture | `ENEMY_VULTURE:511` | V: Bat (flying AI) | B |
| 31 | Crawler mutant in closet | `ENEMY_CRAWLER_MUTANT_1:513` | N | B |
| 32 | Crocodile (in water) | `ENEMY_ALLIGATOR:514` | V: water Crocodile | B |
| 34 | Compsognathus | `ENEMY_COMPSOGNATHUS:516` | V: Raptor | B |
| 35 | Lizard man | `ENEMY_LIZARD_MAN:517` | N | B |
| 36 | Puna | `ENEMY_PUNA:518` | N | B |
| 37 | Mercenary | `ENEMY_MERCENARY:519` | V: Pierre / Mr T | B |
| 39 | RX-Tech guy in red jacket | `ENEMY_RX_TECH_GUY_1:521` | N | B |
| 40 | RX-Tech guy with gun | `ENEMY_RX_TECH_GUY_2:522` | V: Pierre / Mr T | B |
| 41 | Dog (Antarctica) | `ENEMY_ANTARC_DOG:523` | V: Wolf | B |
| 42 | Crawler mutant | `ENEMY_CRAWLER_MUTANT_2:524` | N | B |
| 44 | Tinnos wasp | `ENEMY_TINNOS_WASP:526` | V: Bat (flying AI) | B |
| 45 | Tinnos monster | `ENEMY_TINNOS_MONSTER:527` | N | B |
| 46 | Brute mutant (with claw) | `ENEMY_BRUTE_MUTANT:528` | N | B |
| 49 | Willard spider | `ENEMY_WILLARD_SPIDER:531` | N | B |
| 50 | RX-Tech flamethrower guy | `ENEMY_RX_TECH_FLAME_GUY:532` | N | B |
| 51 | London mercenary | `ENEMY_LONDON_MERCENARY:533` | V: Pierre / Mr T | B |
| 53 | Punk | `ENEMY_PUNK:535` | N | B |
| 56 | London guard | `ENEMY_LONDON_GUARD:538` | V: Pierre / Mr T | B |
| 57 | Sophia Lee | `ENEMY_SOPHIA:539` | N | B |
| 60 | MP with stick | `ENEMY_MILITARY_1:542` | N | B |
| 61 | MP with gun | `ENEMY_MILITARY_2:543` | V: Pierre / Mr T | B |
| 62 | Prisoner | `PRISONER:544` | N | B |
| 63 | MP with sighted gun and night sight | `ENEMY_MILITARY_3:545` | N | B |
| 65 | Dam guard | `ENEMY_DAM_GUARD:547` | V: Pierre / Mr T | B |
| 69 | Cobra / Rattlesnake | `ENEMY_COBRA:551` | N | B |
| 70 | Shiva | `ENEMY_SHIVA:552` | N | B |
| 71 | Monkey | `ENEMY_MONKEY:553` | N | B |
| 73 | Tony Firehands | `ENEMY_TONY:555` | N | B |
| 287 | Tyrannosaur | `__ENEMY_REX:769` | V: T-rex | D — Rex `level.h:1161` |
| 288 | Raptor | `__ENEMY_RAPTOR:770` | V: Raptor | D — Raptor `level.h:1162` |
| 360 | Winston | `__ENEMY_WINSTON:842` | N | P — Winston `level.h:1297`; follow/freeze, not camo, `enemy.h:3389–3517` |
| 361 | Winston in camo suit | `ENEMY_WINSTON_CAMO:843` | N | B |

#### Vehicles and rider props

| ID | RS name or — | OL `format.h` symbol:line | TR1 status / counterpart | OL coverage |
|---:|---|---|---|---|
| 12 | UPV | `VEHICLE_UPV:494` | N | B |
| 14 | Kayak | `VEHICLE_KAYAK:496` | N | B |
| 15 | Inflatable boat | `__VEHICLE_BOAT:497` | N | B |
| 16 | Quadbike | `VEHICLE_QUADBIKE:498` | N | B |
| 17 | Mine cart | `VEHICLE_MINECART:499` | N | B |
| 18 | Big gun | `BIG_GUN:500` | N | B |

#### Traps and hazards

| ID | RS name or — | OL `format.h` symbol:line | TR1 status / counterpart | OL coverage |
|---:|---|---|---|---|
| 19 | Hydro propeller (?) | `HYDRO_PROPELLER:501` | N | B |
| 58 | Cleaner robot | `CLEANER_ROBOT:540` | N | B |
| 64 | Gun turret | `GUN_TURRET:546` | N | B |
| 66 | Tripwire | `TRIPWIRE:548` | N | B |
| 67 | Electrified wire | `ELECTRIC_WIRE:549` | N | B |
| 68 | Killer tripwire | `KILLER_TRIPWIRE:550` | N | B |
| 83 | Falling block | `__TRAP_FLOOR:565` | S: falling floor | D — TrapFloor `level.h:1198`; shake/fall, `objects.h:792–835` |
| 86 | Swinging thing | `TRAP_SWING_THING:568` | V: Swinging blade (moving contact hazard) | B |
| 87 | Teeth spikes / Barbed wire | `__TRAP_SPIKES:569` | V: Spikes | D — TrapSpikes `level.h:1201` |
| 88 | Rolling ball / Barrel | `__TRAP_BOULDER:570` | V: Boulder | D — TrapBoulder `level.h:1203`; rolling/collision, `objects.h:430–510` |
| 89 | Giant boulder | `TRAP_BOULDER_GIANT:571` | V: Boulder | B |
| 90 | Disc | `__DART:572` | V: Dart / dart emitter | D — Dart `level.h:1204` |
| 91 | Dart shooter | `__TRAP_DART_EMITTER:573` | V: Dart / dart emitter | D — TrapDartEmitter `level.h:1205` |
| 94 | Skeleton trap / Slamming door | `TRAP_SKELETON:576` | V: Slamming door | B |
| 106 | Area 51 swinger | `AREA51_SWINGER:588` | N | B |
| 107 | Falling ceiling | `__TRAP_CEILING_1:589` | S: falling ceiling | D — TrapCeiling `level.h:1214` |
| 108 | Rolling spindle | `__TRAP_SPINDLE:590` | V: Swinging blade (moving contact hazard) | B |
| 110 | Subway train | `SUBWAY_TRAIN:592` | N | B |
| 111 | Wall knife blade / Knife disk | `TRAP_WALL_KNIFE_BLADE:593` | V: Swinging blade (moving contact hazard) | B |
| 113 | Detachable stalactites | `__TRAP_ICICLES:595` | N | B |
| 114 | Spiky wall | `__TRAP_SPIKES_WALL:596` | V: Moving bar + spikes | B |
| 115 | — | `UNUSED_TR3_115:597` | N | B |
| 116 | Spiky vertical wall / Tunnel borer | `TRAP_SPIKES_VERT:598` | V: Moving bar + spikes | B |
| 119 | Damaging animating 1 | `__CUT_1:601` | N | B |
| 120 | Damaging animating 2 | `__CUT_2:602` | N | B |
| 121 | Damaging animating 3 | `__CUT_3:603` | N | B |
| 282 | Fire-breathing dragon statue | `DRAGON_STATUE:764` | V: Flame emitter (hazard family) | B |
| 291 | Laser sweeper | `LASER_SWEEPER:773` | N | B |
| 292 | Electrified Field | `ELECTRIC_FILER:774` | N | B |
| 338 | Pirahnas | `ENEMY_PIRANHAS:820` | N | B |

#### Doors, switches and movers

| ID | RS name or — | OL `format.h` symbol:line | TR1 status / counterpart | OL coverage |
|---:|---|---|---|---|
| 95 | — | `__BLOCK_1:577` | S: block 1 | D — Block `level.h:1211`; push/pull/heavy, `objects.h:511–612` |
| 96 | — | `__BLOCK_2:578` | S: block 2 | D — Block `level.h:1211`; push/pull/heavy, `objects.h:511–612` |
| 97 | Pushable block 1 | `__BLOCK_3:579` | S: block 3 | D — Block `level.h:1211`; push/pull/heavy, `objects.h:511–612` |
| 98 | Pushable block 2 | `__BLOCK_4:580` | S: block 4 | D — Block `level.h:1211`; push/pull/heavy, `objects.h:511–612` |
| 101 | Destroyable boarded-up window | `__WINDOW_1:583` | N | P — BreakableWindow `level.h:1306`; mesh mask only, `objects.h:1691–1697` |
| 102 | Destroyable boarded-up window / wall | `__WINDOW_2:584` | N | P — BreakableWindow `level.h:1306`; mesh mask only, `objects.h:1691–1697` |
| 117 | Valve wheel / Pulley | `__WHEEL_KNOB:599` | N | B |
| 118 | Small wall switch | `__SWITCH_BIG:600` | V: Land switch | D — Switch `level.h:1224`; timer/state, `objects.h:9–55` |
| 127 | Zipline handle | `__ZIPLINE_HANDLE:609` | N | B |
| 128 | Push-button switch | `__SWITCH_BUTTON:610` | V: Land switch | D — Switch `level.h:1224`; timer/state, `objects.h:9–55` |
| 129 | Wall switch | `__SWITCH:611` | S: switch | D — Switch `level.h:1224`; timer/state, `objects.h:9–55` |
| 130 | Underwater switch | `__SWITCH_WATER:612` | S: switch water | D — Switch `level.h:1224`; timer/state, `objects.h:9–55` |
| 131 | Door 1 | `__DOOR_1:613` | S: door 1 | D — Door `level.h:1185`; portal/solid updates, `objects.h:672–768` |
| 132 | Door 2 | `__DOOR_2:614` | S: door 2 | D — Door `level.h:1185`; portal/solid updates, `objects.h:672–768` |
| 133 | Door 3 | `__DOOR_3:615` | S: door 3 | D — Door `level.h:1185`; portal/solid updates, `objects.h:672–768` |
| 134 | Door 4 | `__DOOR_4:616` | S: door 4 | D — Door `level.h:1185`; portal/solid updates, `objects.h:672–768` |
| 135 | Door 5 | `__DOOR_5:617` | S: door 5 | D — Door `level.h:1185`; portal/solid updates, `objects.h:672–768` |
| 136 | Door 6 | `__DOOR_6:618` | S: door 6 | D — Door `level.h:1185`; portal/solid updates, `objects.h:672–768` |
| 137 | Door 7 | `__DOOR_7:619` | S: door 7 | D — Door `level.h:1185`; portal/solid updates, `objects.h:672–768` |
| 138 | Door 8 | `__DOOR_8:620` | S: door 8 | D — Door `level.h:1185`; portal/solid updates, `objects.h:672–768` |
| 139 | Trapdoor 1 | `__TRAP_DOOR_1:621` | S: trap door 1 | D — TrapDoor `level.h:1187`; state/floor, `objects.h:769–791` |
| 140 | Trapdoor 2 | `__TRAP_DOOR_2:622` | S: trap door 2 | D — TrapDoor `level.h:1187`; state/floor, `objects.h:769–791` |
| 141 | Trapdoor 3 | `__TRAP_DOOR_3:623` | V: Trapdoor | B |
| 142 | Bridge flat | `__BRIDGE_1:624` | S: flat bridge | D — Bridge `level.h:1190`; floor based, `objects.h:838–841` |
| 143 | Bridge tilt 1 | `__BRIDGE_2:625` | S: tilted bridge 1 | D — Bridge `level.h:1190`; floor based, `objects.h:838–841` |
| 144 | Bridge tilt 2 | `__BRIDGE_3:626` | S: tilted bridge 2 | D — Bridge `level.h:1190`; floor based, `objects.h:838–841` |
| 213 | Slot 1 empty | `__PUZZLE_HOLE_1:695` | S: puzzle hole 1 | D — KeyHole `level.h:1232`; key/puzzle, `objects.h:1557–1612` |
| 214 | Slot 2 empty | `__PUZZLE_HOLE_2:696` | S: puzzle hole 2 | D — KeyHole `level.h:1232`; key/puzzle, `objects.h:1557–1612` |
| 215 | Slot 3 empty | `__PUZZLE_HOLE_3:697` | S: puzzle hole 3 | D — KeyHole `level.h:1232`; key/puzzle, `objects.h:1557–1612` |
| 216 | Slot 4 empty | `__PUZZLE_HOLE_4:698` | S: puzzle hole 4 | D — KeyHole `level.h:1232`; key/puzzle, `objects.h:1557–1612` |
| 217 | Slot 1 full | `__PUZZLE_DONE_1:699` | S: puzzle done 1 | A — filled mesh swap `objects.h:1557–1612`; `format.h:4848–4852` |
| 218 | Slot 2 full | `__PUZZLE_DONE_2:700` | S: puzzle done 2 | A — filled mesh swap `objects.h:1557–1612`; `format.h:4848–4852` |
| 219 | Slot 3 full | `__PUZZLE_DONE_3:701` | S: puzzle done 3 | A — filled mesh swap `objects.h:1557–1612`; `format.h:4848–4852` |
| 220 | Slot 4 full | `__PUZZLE_DONE_4:702` | S: puzzle done 4 | A — filled mesh swap `objects.h:1557–1612`; `format.h:4848–4852` |
| 232 | Keyhole 1 | `__KEY_HOLE_1:714` | S: keyhole | D — KeyHole `level.h:1232`; key/puzzle, `objects.h:1557–1612` |
| 233 | Keyhole 2 | `__KEY_HOLE_2:715` | S: key hole 2 | D — KeyHole `level.h:1232`; key/puzzle, `objects.h:1557–1612` |
| 234 | Keyhole 3 | `__KEY_HOLE_3:716` | S: key hole 3 | D — KeyHole `level.h:1232`; key/puzzle, `objects.h:1557–1612` |
| 235 | Keyhole 4 | `__KEY_HOLE_4:717` | S: key hole 4 | D — KeyHole `level.h:1232`; key/puzzle, `objects.h:1557–1612` |
| 295 | Detonator switch box | `__DETONATOR:777` | N | B |
| 314 | Movable Boom | `MOVABLE_BOOM:796` | N | B |
| 373 | Electrical switch box | `ELECTRIC_SWITCH:855` | N | B |

#### Pickups, keys and ring items

| ID | RS name or — | OL `format.h` symbol:line | TR1 status / counterpart | OL coverage |
|---:|---|---|---|---|
| 160 | Pistols | `__PISTOLS:642` | S: pistols | A — Lara pickup/ring, §2 |
| 161 | Shotgun | `__SHOTGUN:643` | S: shotgun | A — Lara pickup/ring, §2 |
| 162 | Desert Eagle | `DESERT_EAGLE:644` | V: Magnums / ammunition | P — Lara pickup predicate; ring/use missing, §2 |
| 163 | Uzis | `__UZIS:645` | S: uzis | A — Lara pickup/ring, §2 |
| 164 | Harpoon gun | `__HARPOON:646` | N | P — Lara pickup predicate; ring/use missing, §2 |
| 165 | MP5 | `MP5:647` | N | P — Lara pickup predicate; ring/use missing, §2 |
| 166 | Rocket launcher | `ROCKET:648` | N | P — Lara pickup predicate; ring/use missing, §2 |
| 167 | Grenade launcher | `__GRENADE:649` | N | P — Lara pickup predicate; ring/use missing, §2 |
| 168 | Pistol ammo | `__AMMO_PISTOLS:650` | S: ammo pistols | A — Lara pickup/ring, §2 |
| 169 | Shotgun ammo | `__AMMO_SHOTGUN:651` | S: ammo shotgun | A — Lara pickup/ring, §2 |
| 170 | Desert Eagle ammo | `AMMO_DESERT_EAGLE:652` | V: Magnums / ammunition | P — Lara pickup predicate; ring/use missing, §2 |
| 171 | Uzi ammo | `__AMMO_UZIS:653` | S: ammo uzis | A — Lara pickup/ring, §2 |
| 172 | Harpoons | `__AMMO_HARPOON:654` | N | P — Lara pickup predicate; ring/use missing, §2 |
| 173 | MP5 ammo | `AMMO_MP5:655` | N | P — Lara pickup predicate; ring/use missing, §2 |
| 174 | Rockets | `AMMO_ROCKET:656` | N | P — Lara pickup predicate; ring/use missing, §2 |
| 175 | Grenades | `__AMMO_GRENADE:657` | N | P — Lara pickup predicate; ring/use missing, §2 |
| 176 | Small medipack | `__MEDIKIT_SMALL:658` | S: small medikit | A — Lara pickup/ring, §2 |
| 177 | Large medipack | `__MEDIKIT_BIG:659` | S: large medikit | A — Lara pickup/ring, §2 |
| 178 | Flares | `__FLARES:660` | N | P — Lara pickup predicate; ring/use missing, §2 |
| 179 | Flare | `__FLARE:661` | N | P — Lara pickup predicate; ring/use missing, §2 |
| 180 | Savegame crystal | `CRYSTAL_PICKUP:662` | V: Save crystal (different use) | P — CrystalPickup `level.h:1299`; animation/light only, `objects.h:923–939` |
| 185 | Pistols | `__INV_PISTOLS:667` | S: inv pistols | A — ring `inventory.h:285` |
| 186 | Shotgun | `__INV_SHOTGUN:668` | S: inv shotgun | A — ring `inventory.h:286` |
| 187 | Desert Eagle | `INV_DESERT_EAGLE:669` | V: Magnums / ammunition | B |
| 188 | Uzis | `__INV_UZIS:670` | S: inv uzis | A — ring `inventory.h:288` |
| 189 | Harpoon gun | `__INV_HARPOON:671` | N | B |
| 190 | MP5 | `INV_MP5:672` | N | B |
| 191 | Rocket launcher | `INV_ROCKET:673` | N | B |
| 192 | Grenade gun | `__INV_GRENADE:674` | N | B |
| 193 | Pistol ammo | `__INV_AMMO_PISTOLS:675` | S: inv ammo pistols | A — ring `inventory.h:290` |
| 194 | Shotgun ammo | `__INV_AMMO_SHOTGUN:676` | S: inv ammo shotgun | A — ring `inventory.h:291` |
| 195 | Desert Eagle ammo | `INV_AMMO_DESERT_EAGLE:677` | V: Magnums / ammunition | B |
| 196 | Uzi ammo | `__INV_AMMO_UZIS:678` | S: inv ammo uzis | A — ring `inventory.h:293` |
| 197 | Harpoons | `__INV_AMMO_HARPOON:679` | N | B |
| 198 | MP5 ammo | `INV_AMMO_MP5:680` | N | B |
| 199 | Rockets | `INV_AMMO_ROCKET:681` | N | B |
| 200 | Grenades | `__INV_AMMO_GRENADE:682` | N | B |
| 201 | Small medipack | `__INV_MEDIKIT_SMALL:683` | S: inv medikit small | A — ring `inventory.h:295` |
| 202 | Large medipack | `__INV_MEDIKIT_BIG:684` | S: inv medikit big | A — ring `inventory.h:296` |
| 203 | Flares | `__INV_FLARES:685` | N | B |
| 205 | Puzzle 1 | `__PUZZLE_1:687` | S: puzzle 1 | A — Lara pickup/ring, §2 |
| 206 | Puzzle 2 | `__PUZZLE_2:688` | S: puzzle 2 | A — Lara pickup/ring, §2 |
| 207 | Puzzle 3 | `__PUZZLE_3:689` | S: puzzle 3 | A — Lara pickup/ring, §2 |
| 208 | Puzzle 4 | `__PUZZLE_4:690` | S: puzzle 4 | A — Lara pickup/ring, §2 |
| 209 | Puzzle 1 | `__INV_PUZZLE_1:691` | S: inv puzzle 1 | A — ring `inventory.h:298` |
| 210 | Puzzle 2 | `__INV_PUZZLE_2:692` | S: inv puzzle 2 | A — ring `inventory.h:299` |
| 211 | Puzzle 3 | `__INV_PUZZLE_3:693` | S: inv puzzle 3 | A — ring `inventory.h:300` |
| 212 | Puzzle 4 | `__INV_PUZZLE_4:694` | S: inv puzzle 4 | A — ring `inventory.h:301` |
| 224 | Key 1 | `__KEY_ITEM_1:706` | S: key item 1 | A — Lara pickup/ring, §2 |
| 225 | Key 2 | `__KEY_ITEM_2:707` | S: key item 2 | A — Lara pickup/ring, §2 |
| 226 | Key 3 | `__KEY_ITEM_3:708` | S: key item 3 | A — Lara pickup/ring, §2 |
| 227 | Key 4 | `__KEY_ITEM_4:709` | S: key item 4 | A — Lara pickup/ring, §2 |
| 228 | Key 1 | `__INV_KEY_ITEM_1:710` | S: inv key item 1 | D — KeyItemInv `level.h:1197` |
| 229 | Key 2 | `__INV_KEY_ITEM_2:711` | S: inv key item 2 | D — KeyItemInv `level.h:1197` |
| 230 | Key 3 | `__INV_KEY_ITEM_3:712` | S: inv key item 3 | D — KeyItemInv `level.h:1197` |
| 231 | Key 4 | `__INV_KEY_ITEM_4:713` | S: inv key item 4 | D — KeyItemInv `level.h:1197` |
| 236 | Quest item 1 | `__QUEST_ITEM_1:718` | N | B |
| 237 | Quest item 2 | `__QUEST_ITEM_2:719` | N | B |
| 238 | Quest item 1 | `__INV_QUEST_ITEM_1:720` | N | B |
| 239 | Quest item 2 | `__INV_QUEST_ITEM_2:721` | N | B |
| 240 | Infada stone | `STONE_ITEM_1:722` | V: Scion/artifact or receptor | P — StoneItem `level.h:1303`; rotates/lights, missing ring/place, `objects.h:1732–1756` |
| 241 | Element 115 | `STONE_ITEM_2:723` | V: Scion/artifact or receptor | P — StoneItem `level.h:1303`; rotates/lights, missing ring/place, `objects.h:1732–1756` |
| 242 | Eye of Isis | `STONE_ITEM_3:724` | V: Scion/artifact or receptor | P — StoneItem `level.h:1303`; rotates/lights, missing ring/place, `objects.h:1732–1756` |
| 243 | Ora dagger | `STONE_ITEM_4:725` | V: Scion/artifact or receptor | P — StoneItem `level.h:1303`; rotates/lights, missing ring/place, `objects.h:1732–1756` |
| 244 | Infada stone | `INV_STONE_ITEM_1:726` | V: Scion/artifact or receptor | B |
| 245 | Element 115 | `INV_STONE_ITEM_2:727` | V: Scion/artifact or receptor | B |
| 246 | Eye of Isis | `INV_STONE_ITEM_3:728` | V: Scion/artifact or receptor | B |
| 247 | Ora dagger | `INV_STONE_ITEM_4:729` | V: Scion/artifact or receptor | B |
| 272 | Keys (sprite) | `KEYS_SPRITE_1:754` | V: Key pickup presentation | B |
| 273 | Keys (sprite) | `KEYS_SPRITE_2:755` | V: Key pickup presentation | B |

#### Effects and emitters

| ID | RS name or — | OL `format.h` symbol:line | TR1 status / counterpart | OL coverage |
|---:|---|---|---|---|
| 294 | Shadow sprite | `SHADOW_SPRITE:776` | V: Actor shadow | B |
| 296 | Misc sprites | `MISC_SPRITES:778` | N | B |
| 297 | Bubble | `__BUBBLE:779` | S: bubble | D — Bubble `level.h:1240` |
| 299 | Glow | `__MUZZLE_GLOW:781` | N | B |
| 300 | Gunflare | `__MUZZLE_FLASH:782` | S: muzzle flash | D — MuzzleFlash `level.h:1249` |
| 301 | Gunflare (MP5) | `MUZZLE_FLASH_MP5:783` | V: Muzzle flash | B |
| 305 | Waterfall mist | `__WATERFALL:787` | S: waterfall | D — Waterfall `level.h:1235` |
| 306 | Harpoon (single) | `__HARPOON_WTF:788` | N | B |
| 309 | Rocket (single) | `BULLET_ROCKET:791` | N | B |
| 310 | Harpoon (single) | `BULLET_HARPOON:792` | N | B |
| 311 | Grenade (single) | `BULLET_GRENADE:793` | N | B |
| 312 | Big missile | `BIG_MISSILE:794` | N | B |
| 313 | Smoke | `__SMOKE:795` | S: smoke | D — Sprite `level.h:1245` |
| 316 | Glow 2 | `GLOW_2:798` | N | B |
| 318 | Alarm light | `LIGHT_1:800` | N | B |
| 319 | Light | `LIGHT_2:801` | N | B |
| 320 | — | `LIGHT_3:802` | N | B |
| 321 | Light 2 | `LIGHT_4:803` | N | B |
| 322 | Pulsating Light | `LIGHT_5:804` | N | B |
| 323 | — | `LIGHT_6:805` | N | B |
| 324 | Red Light | `LIGHT_7:806` | N | B |
| 325 | Green Light | `LIGHT_8:807` | N | B |
| 326 | Blue Light | `LIGHT_9:808` | N | B |
| 327 | Light 3 | `LIGHT_10:809` | N | B |
| 328 | Light 4 | `LIGHT_11:810` | N | B |
| 330 | Fire | `PARTICLE_FIRE_1:812` | V: Flame / Flame emitter | B |
| 331 | Alternate Fire | `PARTICLE_FIRE_2:813` | V: Flame / Flame emitter | B |
| 332 | Alternate Fire 2 | `PARTICLE_FIRE_3:814` | V: Flame / Flame emitter | B |
| 333 | Fire 2 | `PARTICLE_FIRE_4:815` | V: Flame / Flame emitter | B |
| 334 | Smoke 2 | `PARTICLE_SMOKE_1:816` | V: Smoke | B |
| 335 | Smoke 3 | `PARTICLE_SMOKE_2:817` | V: Smoke | B |
| 336 | Smoke 4 | `PARTICLE_SMOKE_3:818` | V: Smoke | B |
| 337 | Greenish Smoke | `PARTICLE_SMOKE_4:819` | V: Smoke | B |
| 339 | Fish | `ENEMY_FISH:821` | N | B |
| 347 | Bat swarm | `ENEMY_BAT_SWARM:829` | V: Bat (individual vs swarm) | B |
| 355 | Skybox | `__SKY:837` | N | A — sky `level.h:1871–1937` |
| 357 | Doorbell | `__SOUND_DOOR_BELL:839` | N | B |
| 365 | Earthquake | `__EARTHQUAKE:847` | V: Earthquake (presentation) | D — Earthquake `level.h:1255` |
| 366 | Yellow shell casing | `GUN_SHELL_1:848` | N | B |
| 367 | Red shell casing | `GUN_SHELL_2:849` | N | B |
| 370 | Tinnos light shaft | `TINNOS_LIGHT_SHAFT:852` | N | B |

#### Scripted set pieces and markers

| ID | RS name or — | OL `format.h` symbol:line | TR1 status / counterpart | OL coverage |
|---:|---|---|---|---|
| 24 | Kill All Triggers | `KILL_ALL_TRIGGERS:506` | N | B |
| 30 | Assault-course target | `ASSAULT_TARGET:512` | N | B |
| 38 | Raptor hung by rope | `ENEMY_RAPTOR_HUNG:520` | V: Raptor | B |
| 47 | Tinnos wasp respawn point | `RESPAWN_TINNOS_WASP:529` | N | B |
| 48 | Raptor respawn point | `RESPAWN_RAPTOR:530` | N | B |
| 74 | AI Guard | `AI_GUARD:556` | N | B |
| 75 | AI Ambush | `AI_AMBUSH:557` | N | B |
| 76 | AI Patrol 1 | `AI_PATROL_1:558` | N | B |
| 77 | AI Modify | `AI_MODIFY:559` | N | B |
| 78 | AI Follow | `AI_FOLLOW:560` | N | B |
| 79 | AI Patrol 2 | `AI_PATROL_2:561` | N | B |
| 80 | AI Path | `AI_PATH:562` | N | B |
| 81 | AI Check | `AI_CHECK:563` | N | B |
| 122 | Shiva statue | `SHIVA_STATUE:604` | V: Centaur statue | B |
| 123 | Monkey medipack meshswap | `MONKEY_MEDIPACK:605` | N | B |
| 124 | Monkey key meshswap | `MONKEY_KEY:606` | N | B |
| 148 | Cutscene actor 1 | `__CUT_4:630` | V: Cutscene actor | A — cut actors `format.h:2504–2506`; `level.h:2181–2229` |
| 149 | Cutscene actor 2 | `__CUT_5:631` | V: Cutscene actor | A — cut actors `format.h:2504–2506`; `level.h:2181–2229` |
| 150 | Cutscene actor 3 | `__CUT_6:632` | V: Cutscene actor | A — cut actors `format.h:2504–2506`; `level.h:2181–2229` |
| 151 | Cutscene actor 4 | `__CUT_7:633` | V: Cutscene actor | A — cut actors `format.h:2504–2506`; `level.h:2181–2229` |
| 152 | Cutscene actor 5 | `__CUT_8:634` | V: Cutscene actor | A — cut actors `format.h:2504–2506`; `level.h:2181–2229` |
| 153 | Cutscene actor 6 | `__CUT_9:635` | V: Cutscene actor | A — cut actors `format.h:2504–2506`; `level.h:2181–2229` |
| 154 | Cutscene actor 7 | `__CUT_10:636` | V: Cutscene actor | A — cut actors `format.h:2504–2506`; `level.h:2181–2229` |
| 155 | Cutscene actor 8 | `__CUT_11:637` | V: Cutscene actor | A — cut actors `format.h:2504–2506`; `level.h:2181–2229` |
| 156 | Cutscene actor 9 | `CUT_12:638` | V: Cutscene actor | A — cut actors `format.h:2504–2506`; `level.h:2181–2229` |
| 276 | Infada stone | `STONE_ITEM_PLACE_1:758` | V: Scion/artifact or receptor | B |
| 277 | Element 115 | `STONE_ITEM_PLACE_2:759` | V: Scion/artifact or receptor | B |
| 278 | Eye of Isis | `STONE_ITEM_PLACE_3:760` | V: Scion/artifact or receptor | B |
| 279 | Ora dagger | `STONE_ITEM_PLACE_4:761` | V: Scion/artifact or receptor | B |
| 304 | Look At item | `__VIEW_TARGET:786` | S: camera view target | A — camera target `lara.h:1970–1989` |
| 349 | Animating 1 | `ANIMATING_1:831` | N | B |
| 350 | Animating 2 | `ANIMATING_2:832` | N | B |
| 351 | Animating 3 | `ANIMATING_3:833` | N | B |
| 352 | Animating 4 | `ANIMATING_4:834` | N | B |
| 353 | Animating 5 | `ANIMATING_5:835` | N | B |
| 354 | Animating 6 | `ANIMATING_6:836` | N | B |

#### Unknown or reserved

| ID | RS name or — | OL `format.h` symbol:line | TR1 status / counterpart | OL coverage |
|---:|---|---|---|---|
| 13 | — | `UNUSED_TR3_13:495` | ? | — |
| 33 | — | `UNUSED_TR3_33:515` | ? | — |
| 43 | — | `UNUSED_TR3_43:525` | ? | — |
| 52 | — | `UNUSED_TR3_52:534` | ? | — |
| 54 | — | `UNUSED_TR3_54:536` | ? | — |
| 55 | — | `UNUSED_TR3_55:537` | ? | — |
| 59 | — | `UNUSED_TR3_59:541` | ? | — |
| 72 | — | `UNUSED_TR3_72:554` | ? | — |
| 82 | Unknown | `UNUSED_TR3_82:564` | ? | — |
| 84 | — | `UNUSED_TR3_84:566` | ? | — |
| 85 | — | `UNUSED_TR3_85:567` | ? | — |
| 92 | — | `UNUSED_TR3_92:574` | ? | — |
| 93 | — | `UNUSED_TR3_93:575` | ? | — |
| 99 | — | `UNUSED_TR3_99:581` | ? | — |
| 100 | — | `UNUSED_TR3_100:582` | ? | — |
| 103 | — | `UNUSED_TR3_103:585` | ? | — |
| 104 | — | `UNUSED_TR3_104:586` | ? | — |
| 105 | — | `UNUSED_TR3_105:587` | ? | — |
| 109 | — | `UNUSED_TR3_109:591` | ? | — |
| 112 | — | `UNUSED_TR3_112:594` | ? | — |
| 126 | — | `UNUSED_TR3_126:608` | ? | — |
| 157 | — | `UNUSED_TR3_157:639` | ? | — |
| 221 | — | `UNUSER_TR3_121:703` | ? | — |
| 222 | — | `UNUSER_TR3_122:704` | ? | — |
| 223 | — | `UNUSER_TR3_123:705` | ? | — |
| 248 | — | `UNUSED_TR3_248:730` | ? | — |
| 249 | — | `UNUSED_TR3_249:731` | ? | — |
| 250 | — | `UNUSED_TR3_250:732` | ? | — |
| 251 | — | `UNUSED_TR3_251:733` | ? | — |
| 252 | — | `UNUSED_TR3_252:734` | ? | — |
| 253 | — | `UNUSED_TR3_253:735` | ? | — |
| 254 | — | `UNUSED_TR3_254:736` | ? | — |
| 255 | — | `UNUSED_TR3_255:737` | ? | — |
| 256 | — | `UNUSED_TR3_256:738` | ? | — |
| 257 | — | `UNUSED_TR3_257:739` | ? | — |
| 258 | — | `UNUSED_TR3_258:740` | ? | — |
| 259 | — | `UNUSED_TR3_259:741` | ? | — |
| 260 | — | `UNUSED_TR3_260:742` | ? | — |
| 261 | — | `UNUSED_TR3_261:743` | ? | — |
| 262 | — | `UNUSED_TR3_262:744` | ? | — |
| 263 | — | `UNUSED_TR3_263:745` | ? | — |
| 264 | — | `UNUSED_TR3_264:746` | ? | — |
| 265 | — | `UNUSED_TR3_265:747` | ? | — |
| 266 | — | `UNUSED_TR3_266:748` | ? | — |
| 267 | — | `UNUSED_TR3_267:749` | ? | — |
| 268 | — | `UNUSED_TR3_268:750` | ? | — |
| 269 | — | `UNUSED_TR3_269:751` | ? | — |
| 270 | — | `UNUSED_TR3_270:752` | ? | — |
| 271 | — | `UNUSED_TR3_271:753` | ? | — |
| 274 | — | `UNUSED_TR3_274:756` | ? | — |
| 275 | — | `UNUSED_TR3_275:757` | ? | — |
| 280 | — | `UNUSED_TR3_280:762` | ? | — |
| 281 | — | `UNUSED_TR3_281:763` | ? | — |
| 283 | — | `UNUSED_TR3_283:765` | ? | — |
| 284 | — | `UNUSED_TR3_284:766` | ? | — |
| 285 | Unknown visible 285 | `UNKNOWN_VISIBLE:767` | ? | — |
| 286 | — | `UNUSED_TR3_286:768` | ? | — |
| 289 | — | `UNUSED_TR3_289:771` | ? | — |
| 290 | — | `UNUSED_TR3_290:772` | ? | — |
| 293 | — | `UNUSED_TR3_293:775` | ? | — |
| 298 | — | `UNUSED_TR3_298:780` | ? | — |
| 302 | — | `UNUSED_TR3_302:784` | ? | — |
| 303 | — | `UNUSED_TR3_303:785` | ? | — |
| 307 | — | `UNUSED_TR3_307:789` | ? | — |
| 308 | — | `UNUSED_TR3_308:790` | ? | — |
| 317 | Unknown visible 317 | `UNKNONW_TR3_VISIBLE:799` | ? | — |
| 329 | — | `UNUSED_TR3_329:811` | ? | — |
| 340 | — | `UNUSED_TR3_340:822` | ? | — |
| 341 | — | `UNUSED_TR3_341:823` | ? | — |
| 342 | — | `UNUSED_TR3_342:824` | ? | — |
| 343 | — | `UNUSED_TR3_343:825` | ? | — |
| 344 | — | `UNUSED_TR3_344:826` | ? | — |
| 345 | — | `UNUSED_TR3_345:827` | ? | — |
| 346 | — | `UNUSED_TR3_346:828` | ? | — |
| 348 | — | `UNUSED_TR3_348:830` | ? | — |
| 358 | Unknown id 358 | `UNUSED_TR3_358:840` | ? | — |
| 359 | — | `UNUSED_TR3_359:841` | ? | — |
| 363 | — | `UNUSED_TR3_363:845` | ? | — |
| 364 | — | `UNUSED_TR3_364:846` | ? | — |
| 368 | — | `UNUSED_TR3_368:850` | ? | — |
| 369 | — | `UNUSED_TR3_369:851` | ? | — |
| 371 | — | `UNUSED_TR3_371:853` | ? | — |
| 372 | — | `UNUSED_TR3_372:854` | ? | — |

## 4. World rules and their execution

| Rule | Retail evidence versus TR1; OL comparison |
|---|---|
| Floor functions and height queries | RS §§4.4.1–10: 1 horizontal portal, 2 floor slant, 3 ceiling slant, 4 trigger, 5 lethal remain the baseline. II adds 6 climbable directions (+Z,+X,−Z,−X mask 1/2/4/8). III adds `0x07–0x12` diagonal floor/ceiling planes including half portals, `0x13` climbable ceiling, `0x14`/`0x15` turn minecart left/right; both mark stop. These are geometry/collision and traversal rules, not face materials. OL computes piecewise heights but its floor/ceiling walks follow vertical room links before selecting the triangle and the controller has explicit portal-side work to check (`format.h:6760–6845,6848–6932`; `controller.h:413–514`); climb mask read, monkey/rail skipped (`controller.h:535–545`). Confirm diagonal-crossing and split-portal boundary cases rather than treating room topology alone as enough. |
| Triggerers, quad sectors, deadly surfaces | RS §§4.4.5,4.4.9–10 explicitly puts deferred Trigger Triggerer at `0x14` in **IV/V**, not III; III uses it for rails. C3 “Vehicles” documents the quad controls but gives no quad-specific sector opcode; do not reuse a rail tag as quad logic without evidence. TR3 #24 is separately named Kill All Triggers in RS §15.1.3; OL has only a type entry (`format.h:506`), not a dispatch. The public lethal-sector exception is drowning rather than flame in Madubu Gorge; spikes/glass contact, slope response and quicksand are distinct (RS §4.4.5; C2/C3 “Basic Moves”; Jungle). OL only applies its generic on-floor lava hit (`lara.h:2011–2018`). |
| Trigger owners, anti-triggers and used actions | RS §§4.4.4,4.5.1–10,4.6: existing activator/pad/switch/key/pickup/heavy/antipad/combat/dummy plus `09` Antitrigger where used; do **not** import `0A` heavy switch, `0B` heavy anti or `0C` monkey trigger merely because III has a monkey-swing ceiling (RS lists those trigger subtypes for IV/V). Switch XOR, OR activation, anti clear and III early OneShot-lock have distinct policies. Actions `00` object, `01` camera (+extra word), `02` sink, `03/04/05` flip/toggle/on/off, `06` look-at, `07` end, `08` audio, `09` effect, `0A` secret, `0B` clear eligible bodies are listed; no II/III `0C` flyby/`0D` cutscene activation established (IV/V in RS; cinematics via script here). `0B` is not a new wire number so much as work for the runtime; old TR1 fixtures don't use it (`docs/game-gaps.md:41–42`). OL handles switches and common actions, but its antipad clears selected bits and OneShot latch happens after full mask; clear bodies does nothing (`lara.h:2032–2133,2139–2164,2251–2268`). Check final data footprint and activation order, particularly when the trigger owner is a vehicle or falling object. |
| Depth, currents, quicksand | Wading/rolls per §2; RS §§4.6.3,9.5 sink shared with camera and can prevent III surfacing. OL gets liquid depth through vertical portals (`format.h:6935–6978`), paths toward sink via box graph, clamps velocity and submerges surface swimmers (`lara.h:3792–3828`); not proof of III signed acceleration/drag/friction, ceiling interaction or impulse lifetime. Jungle “Overgrown Courtyard”, “First Zip Line” and “On the Roof” distinguish an unrecoverable current while in river, recoverable shallow edge, and a swamp that can be slogged through at its edge. No dedicated quicksand state/room-semantic branch found in OL water/stand paths (`format.h:1820–1830,6935–6978`; `lara.h:2302–2367,2737–2902`). N64 `game/flow.c:9–58` and `game/lara.c:2516–2720` are candidate substrate, not proof of III. |
| Lighting, weather, dynamic lights | RS §§2.3.7,3.2.4–5,3.4.2–3,11.5.1 distinguish II scalar room modes (including sunset) from III RGB vertex/point/sun data, water/wave/caustic metadata. Real light sources include flares and weapon flashes (M3 p. 9; RS §§15.1.2–3), and III light/fire/smoke slots add emission roles. OL stores coloured vertex/static data, uses shader/render lighting and room dynamic light storage (`format.h:1872–1886,1918–1943`; `mesh.h:69–77,350–382`; `controller.h:1281–1399`); witnessed example is crystal light (`objects.h:923–938`), not retail weather, sunset timing or authored dynamic emitters. Exact lightning/rain/material behaviour and whether script fields select it remain unverified (§1.2). |
| Fog, distance, sky and camera | RS §§2.2.1,3.4.3 and §15.1.2 #254 describe version/platform draw/fog and outside/sky data (not exact clip planes for retail). OL uses level overrides and underwater fog, sky pass, and camera far plane 45 sectors (`gameflow.h:1692 onward`; `level.h:573–575,948–959,1871–1937`; `camera.h:714–722`); transport, weather and per-edition fog parity not shown. M3 pp. 7,10 and Venice “Race to the Exit” document target-follow during fixed weapon lock, Look returning perspective, fixed camera on an event and separate timed trigger on crossing. OL implements ordinary/look/combat/static/heavy/target camera paths (`camera.h:123–173,415–550`; `lara.h:1970–2008,2167–2185,3318–3323`), not rider-specific camera behaviour. Current tr64's fog starts at 8,192 and goes to black; its assumptions are not retail III presets (`docs/perf-audit.md:26–30`; architecture §7, D11–12). |
| Navigation | RS §§7.3.1,7.4–7.4.1: two extra ground zone arrays, normal/flipped and fly for swimming/flying, plus authored AI helper points in III (not the IV/V separate AI data block). Paths need species-dependent bounds, blocking, water acceptance, and target choice, not just common graph adjacency. OL loads all zones but selects only ground1/2 or fly; base `flying` initialises for Bat; many aquatic/flying sequel types receive generic or base controller (`format.h:3815–3855`; `character.h:72–75,100–116`; `level.h:1259–1312`). It does not establish III monkey task ownership, piranha swarms or scripted NPC path semantics. |
| Sound | RS §§4.6.9,8.1–4: music trigger IDs/masks and one-shot/looped policies vs positional samples, flip-state ambient emitters, random variation and III per-effect range/pitch plus different loop modes. III sector material drives footsteps, on PSX some leave footprints (RS §3.2.3). OL consumes sample count, chance, optional pitch/gain and version-specific playback modes; note it uses pitch field as range of variation around 1.0, not universally as RS's “absolute” pitch, and mode 2 treatment does not prove retail semantics (`format.h:3877–3902,5423–5441`; `level.h:825–851,896–931,998–1003`). Keep resident audio mixer/voice limits separate from source map/selection policy. |
| Flip geometry and effect programs | Flip masks/one-shot and alternate rooms are reusable concepts; effect ID is a versioned namespace independent of geometry flip. RS §§4.6.4–6,4.6.10,9.9.1–2; OL has a global room swap plus subset of effect handling (`level.h:480–505,705–743,2369–2401`; `lara.h:2190–2216,2272–2277`; `controller.h:1150–1175`). Pack/renderer/game must agree which active room set an ID selects; do not simply run the I effect associated with the same numeric slot (`game/world_state.c:15–57,61–127`). |

**Flipeffect cross-check (RS §9.9.2, including animation-command context in
§6.2.6):** 0 turn, 1 stomp/floor shake, 2 Lara normal, 3 bubbles, 4 finish;
II/III 5 flood, 6 chandelier, 7 rubble, 8 piston, 9 curtain, 10 set change,
11 explosion, 12 hands free, 13 map toggle, 14/15 draw right/left; 16/17
shoot right/left in III only; 18–20 three mesh swaps; 21/22 visible/
invisible, 23/24 dynamic on/off, 25 statue, 26 hair reset, 27 boiler;
28–31 assault reset/stop/start/finish; III additionally 32 footprints, 33
8 s penalty, 34–36 racetrack start/reset/finish, 37 30 s penalty, 38–56
home hints 1–19 and 57 hint reset. This is the public *index* list, not
controller contracts; source sometimes describes context done in one place
only (animation command vs floor trigger). For example, Venice boat-lock
flood has observable effects, but the route does not prove which authored
opcode produced them (Venice “Dark, Flooded Tunnel & Boat Lock”).

## 5. Consequences for tr64

These are implementation seams, **not changes made by this survey**.

| Boundary | Reuse unchanged after proper conversion | Extend / new |
|---|---|---|
| Acquisition and cook | Bounds/ledger, IR room topology and gameplay-difference reporting, asset hashing/packing, shared tables and conversion targets (§1; IR §§Conventions, Full-byte accounting and comparison; architecture §5). | Four declared reader modes and versioned accounting; gameflow and media extraction; source-tagged frame, sector, light and material conversions. Existing `tools/cook/src_pc.py:10–54` and `src_psx.py:13–75` are not sequel readers; extend reader **and** independent byte-accounting path, not just first four bytes. |
| Native level/loader and platform | Container indirection, CRC/size checks and bounded I/O don't care which edition produced the payload (`engine/res/pack.c`, `engine/res/stream.c:7–35`, `engine/io/io.c:16–57`; PACK §§Layout, Pool lifetimes). Big-endian pointer-free core and stable IDs are useful seams. | Existing core ABI/strides and named section lifetimes are enforced in `engine/res/level.c:10–40`; extend the ABI where fields differ. Need game/edition identity in the package and at game start / save load. Keep one engine and resident footprint accounting (CORE §§Layout, Binder; architecture §§2,5,8,11). |
| World and animation | Generic sector grid, portal links, room/box links, clip rectangles, origin/angle math, explicit state changes and positional sounds can consume semantically equivalent cooked data (`game/world.c:15–92`, `game/animation.c:9–84`, `engine/render/render.c:55–135`, `engine/io/io.c:29–57`). | TR1 floor command allowlist, item/sound/zone domains, assumed paired frame words and animation effects need extensions; diagonal split portals, monkey surfaces, dynamic flags, later game triggers/camera mods are new (`game/room.c:164–310`; `game/trigger.c:102–283`; `game/nav.c:7–21`; `game/animation.c:86–170`; IR:159–177). A cook can canonicalise frame encodings, but must not translate away controller-specific effect meanings. |
| Game and progression | Existing loop, collision geometric primitives, inventory container, pickup/door/block/bridge families, scripted route VM and native cutscene clock are starting points (`game/object_factory.c:4–65`; `game/object_dispatch.c:12–95`; `game/game.c:435–439`; `game/cut.c:45–126`; `docs/game-gaps.md:81–117`). | New movement/ride/controller timing and weapons; later pickup/economy, drops, UI stamina/exposure and region/save-crystal rules (§§2–4); campaign source of truth rather than TR1 names/`ITEM_MAX`/saves (`game/game.c:35–59,152–194,235–278`; `game/view.c:345–398`; architecture §11 D6). Stable data IDs and scoped game rules prevent changing TR1 traces by accident. |
| Renderer | Portal visibility, batching, corner indices/transforms and view/drawable boundary take cooked geometry; flat/textured faces, colour-bearing vertices, animated logical texture IDs, sprites and entity mesh submission are reusable (architecture §§7–8; `engine/render/render.c:101–135,171–251`; TX64:17–53; GEO §§Decisions, Exact tables). | Current bake starts from TR1 grey vertex/light words and water tint (`tools/cook/n64_geo.py:39–62,126–135`); runtime mesh relighting currently recolours `shade` (`plat/n64/gpu.c:361–388`). Add colour/static light directions, dynamic lighting, blend/order/Z and alternate CLUT/room texture support as warranted by §1. Translucent edges, sky/fog/weather, vehicle view, footprints/new particle bounds and UI rules are not fixed by successful parsing. |
| Sound/video/save | Native media transcodes, music/effects streaming, slot integrity and engine play loop don't need retail byte formats on cart (sections above; MUSIC §§Runtime, Evidence; VIDEO §§Native pack entries, Player; `engine/save/game.c:9–39,57–102`). | Source track mapping/cut timelines and mix/range/flip rules; new game-side save payload with measured capacity, stable world/item identities, and no implicit inventory carry rules (`game/save.c:179–200,295–375,647–685`; `game/game.c:152–194`). |

### 5.1 Verification choices (no selection)

- **Host floating-point OL:** build a traceable reader/runner at the named
  commit, use released data and the same inputs and canonical identifiers.
  Useful independent cross-check of parsing, existing transitions, wading,
  rolls, Tiger, portions of object/camera/render behaviour. Compare
  normalised states/events plus tolerances; floating real-time animation,
  RNG and controller gaps rule out promising fixed byte/tick identity or
  completion proof on their own (§§1–4; `OL animation.h:70–145`,
  `OL character.h:203–212`). An accepted OL result is not a retail verdict.
- **Extend the fixed oracle patch and the C implementation separately, as
  prescribed for co-op** (architecture §11 D14; `docs/specs/coop.md` §3
  “Oracle changes and byte identity”; `test/oracle/README.md:21–45,59–64`).
  The existing oracle is a PC TR1 production logic exporter, not an
  executable sequel reference. Extending it needs sequel level/rules inputs
  as well as the wall specs; equality proves the two implementations agree
  on the admitted inputs and reduces regressions, not that the shared spec
  or shared omission faithfully models retail (architecture §10.1 M6,
  `:900–918`). Retain frozen TR1 identities for old behaviour.
- **Directed scripts only:** assertions at spec-defined events (entry/exit,
  boundary faces, damage/timers, save/reload, world hash), expected retail
  observations when available, plus negative controls and composed routes.
  Proves the checked cases, not equivalence across seeds/world states; label
  placements vs play-reached and do not pass inert substitute controllers
  (`docs/game-gaps.md:12–17`, architecture §9 items 2,5,6). In all choices
  separately compare PC/PSX input differences, measure RAM/cart media, and
  run 4 MiB cart/frame gates; no host-only evidence for RDP/PI deadlines
  (architecture §9; `docs/perf-audit.md:3–7`).

### 5.2 Lower-resolution filtered textures (requested option)

**Proposal [inference], one engine:** cook texture scale and filtering into
pack policy/compatibility metadata, leave original source coordinates and
images in the IR, and retain the existing full-resolution point policy for
legacy packs. Pack/cache identity includes policy; explicit native policy
validated on load beats a global build switch. One half-scale experiment
means making a new low-pass image (not merely changing UVs or RDP filter).
Current cluster construction groups overlapping same-page rectangles; uses
CI4 tight rows, zero border; object corners are integer centre indices;
geometry precomputes fixed5 draw UVs and clips at the actual split rows.
The world pass explicitly sets `SOM_SAMPLE_POINT`; UI and independent host
rendering also have sampling contracts (TX64:55–101,126–176;
`tools/cook/n64_tex.py:145–205,216–299`; GEO §§Decisions, Exact tables;
`plat/n64/gpu.c:128–159,282–349`). This implies:

- **Cook and coordinates.** Low-pass decoded colour/alpha, not palette
  numbers, then re-quantise at the intended extent; PSX rects sharing an
  index image but using different CLUTs may no longer share the same index
  image after filtering/shrinking. Rebuild cluster/descriptor placement,
  palette identities and split lines. Transform source texel footprint to
  destination in *edge-origin* coordinates (source centre `u+½` maps to
  destination `s(u+½)`, then convert to the backend's declared sample
  convention and tile clamp/offset); for odd extents use the actual target
  rectangle scale, not an integer shift of a prebiased UV. Update geometry
  and sprite UVs together, preserving world sprite dimensions. Validate
  every animated member's relative sample footprint and shape before using
  the existing logical-to-content ID permutation (IR:76–84,132–148;
  TX64:37–53,103–116,126–166). A new descriptor or version is required if
  exact fractional corners or a distinct drawn vs loaded rect can't be
  expressed in the existing integer fields.
- **Palette/filter correctness.** CI4 is viable: lookup expands each
  neighbouring index to colour *before* interpolation; the RDP's “bilinear”
  is three-point triangular interpolation. Palette quality is therefore
  judged on interpolated colour/alpha too; enable correct TLUT/texture
  sample state, match the host sampler and test on hardware. No averaging
  nibbles; no claim of identical filtered PC/PSX art, and no reinterpretation
  of STP as an alpha bit. [Nintendo Programming Manual §§13.8 “Texture
  Memory”](https://ultra64.ca/files/documentation/online-manuals/man/pro-man/pro13/13-08.html)
  and [14.1 “Bilinear Filtering”](https://ultra64.ca/files/documentation/online-manuals/man/pro-man/pro14/14-01.html);
  TX64:74–101,168–198.
- **Cutouts, sharp art, and edges.** Downsample coverage separately; select
  an explicit alpha-test threshold or keep small/masked art point-filtered
  if a one-bit palette cannot represent its silhouette. Avoid coloured
  fringes from transparent palette entries (preserve coverage and handle
  transparent RGB at cook time, not by treating an opaque texel as
  transparent). Compare small foliage/grilles, keys, water and UI separately;
  do not filter HUD glyphs by assumption. Clamp or duplicate correct edge
  texels at true material borders; internal split edges need neighbouring
  rows/UV continuity rather than clamping each half as if it were the
  outside of the image. A gutter may force copying/repacking or splitting
  overlapping rectangles; it is storage and upload overhead, not a free
  sampler switch (`gpu.c:147–159,298–339`; TX64:62–83,139–166;
  [Nintendo Programming Manual §14.3 “Antialiasing Texture Rectangles”](https://ultra64.ca/files/documentation/online-manuals/man/pro-man/pro14/14-03.html)).
- **Tile limits.** Even CI4 packed x start/end, 8-byte TMEM pitch and 2,048 B
  tile image budget with the TLUT in the upper half still apply. Recompute
  actual load rectangle (edge/halo, even CI4 width), pitch and max split
  height; version 1 refuses a nonzero border and even-rounding a load
  silently changes its clamp range. If switching material filtering during
  the frame, account for additional RDP state records/syncs and cache
  identity. Fewer bytes alone does not remove per-load or palette-sync
  costs (TX64:62–72,139–166; `gpu.c:282–349`; `docs/perf-audit.md:26–35,109–117`).

**Arithmetic scenario, not a cooked or measured speedup.** Using
`$TR64_WORK/evidence/m1-tex2/report.json.gz` fields
`levels[].{source,level,bytes,breakdown.images}` (human mirror
`report.md`, “Exact byte breakdown”), halve each image dimension; hold
all non-image bytes fixed. For a perfectly divisible tight CI4 rectangle
this gives one quarter of image bytes, so the level-pool saving is
`3 × breakdown.images / 4` and is **not 75% of TX64**. Below is that
arithmetic per archival section (43 rows, grouped as 22 logical names;
PSX Snowcut is absent in that report). Borders, odd/even rounding, split
shape, re-clustering, coverage exceptions, new palettes, alignment and
RDP/PI wire rounding are not in this what-if. It is neither a promise of
actual savings nor a bound on a re-quantised/deduplicated layout; keep
pack section reservations and measure the real cook once implemented.

| TR1 level | PC current TX64 → scenario (B) | PC level-pool saving (B) | PSX current TX64 → scenario (B) | PSX level-pool saving (B) |
|---|---:|---:|---:|---:|
| CUT1 | 125,992 → 48,832 | 77,160 | 121,640 → 44,480 | 77,160 |
| CUT2 | 90,920 → 35,096 | 55,824 | 87,704 → 31,880 | 55,824 |
| CUT3 | 140,592 → 48,096 | 92,496 | 137,416 → 44,920 | 92,496 |
| CUT4 | 173,696 → 63,752 | 109,944 | 168,856 → 58,912 | 109,944 |
| GYM | 345,360 → 132,096 | 213,264 | 338,488 → 122,968 | 215,520 |
| LEVEL1 | 386,536 → 136,840 | 249,696 | 379,352 → 127,400 | 251,952 |
| LEVEL10A | 457,288 → 160,072 | 297,216 | 450,360 → 150,888 | 299,472 |
| LEVEL10B | 459,728 → 162,224 | 297,504 | 449,088 → 149,328 | 299,760 |
| LEVEL10C | 471,632 → 166,808 | 304,824 | 461,336 → 154,256 | 307,080 |
| LEVEL2 | 450,624 → 157,512 | 293,112 | 443,064 → 147,696 | 295,368 |
| LEVEL3A | 433,424 → 154,256 | 279,168 | 424,280 → 142,856 | 281,424 |
| LEVEL3B | 409,336 → 152,248 | 257,088 | 399,544 → 140,200 | 259,344 |
| LEVEL4 | 445,464 → 163,224 | 282,240 | 437,296 → 152,800 | 284,496 |
| LEVEL5 | 371,216 → 136,952 | 234,264 | 364,048 → 127,528 | 236,520 |
| LEVEL6 | 442,736 → 153,536 | 289,200 | 435,728 → 144,272 | 291,456 |
| LEVEL7A | 371,264 → 140,192 | 231,072 | 363,304 → 129,976 | 233,328 |
| LEVEL7B | 479,464 → 175,840 | 303,624 | 468,216 → 162,336 | 305,880 |
| LEVEL8A | 440,736 → 154,344 | 286,392 | 432,728 → 144,080 | 288,648 |
| LEVEL8B | 469,032 → 160,728 | 308,304 | 461,504 → 150,944 | 310,560 |
| LEVEL8C | 436,200 → 153,912 | 282,288 | 427,352 → 142,808 | 284,544 |
| SNOWCUT | 252,912 → 96,912 | 156,000 | — | — |
| TITLE | 54,256 → 20,536 | 33,720 | 55,280 → 19,304 | 35,976 |

**Per-frame image traffic [arithmetic]:**
`$TR64_WORK/evidence/m8/split/measurements.md:3–13` is the archived
mean `tex_bytes` counter (excludes palettes, counts `(width/2)*rows` per
actual TMEM image upload in `gpu.c:339`). Multiply each archived mean by
0.25 for the same ideal half-scale, unchanged sequence/count scenario;
subtract for savings (rounded from the displayed means). The two-view
columns are rendering evidence, not a two-player playthrough.

| TR1 scene | One view KiB/frame: current → estimate (saved) | Two views KiB/frame: current → estimate (saved) |
|---|---:|---:|
| LEVEL1 start | 162.9 → 40.7 (122.2) | 281.4 → 70.3 (211.0) |
| LEVEL1 bridge, room 14 | 309.5 → 77.4 (232.1) | 584.2 → 146.1 (438.2) |
| LEVEL1 room 23 | 238.3 → 59.6 (178.7) | 506.0 → 126.5 (379.5) |
| LEVEL6 room 45 | 507.4 → 126.8 (380.5) | 706.2 → 176.6 (529.7) |
| LEVEL6 room 69 | 238.0 → 59.5 (178.5) | 444.7 → 111.2 (333.5) |

For an independent unit/scope sanity check,
`$TR64_WORK/evidence/m7/perf/perf-lines.log:1–5` reports 165.5 KiB
image uploads/frame at the LEVEL1 start with overlay (ideal 41.4,
124.1 saved); `$TR64_WORK/evidence/m7/perf/tex_analysis.json` reports
isolated-room runs, **not** multi-room frames, so its `rooms[].bytes` is
not a substitute denominator. Palette payload (32 B/palette load), sync
and command traffic, TMEM padding/DRAM bursts and RDP time are excluded
(`gpu.c:307–339`; `docs/perf-overlay.md` §Counters;
`docs/perf-audit.md:29,109–117`). These bytes go **RDRAM→TMEM**, not
cart→RDRAM; do not debit them against the 32 KiB PI cap below.

### 5.3 Room / region streaming (requested option)

**Today.** Whole CORE/TX64 and RG64/MG64 selected from the campaign pack
on the cart, into the same bump-carved `level` arena, committed only after
bounded mount/check/decode/bind. `engine/res/load-unit.c:8–29` is paused
loading; `engine/render/scene.c:60–77` shows the geometry acquire/bind seam;
`engine/res/load.c:12–42` scopes the cold loader, not a streaming tick API;
PACK lines 125–156 and 245–254 describe the mounts and lifetimes. SFX already
has a per-level resident metadata/index, pinned frequent samples and
streamed blocks; music/video use independent lazy catalog/block windows
(PACK:158–183; SN64:87–114; `engine/res/stream.c:7–35`).

| Candidate [inference] | Keep resident / required change | Miss/prefetch pressure |
|---|---|---|
| Room geometry first | Keep portal/room metadata resident; add checked room→chunk offsets/length/hash and room/material dependencies; RG data needs bounded relocatable per-room/region spans instead of assuming one `rooms` base (`engine/render/render.c:101–135,171–251`; GEO §Exact tables). | Portal lookahead admits visible neighbours before crossing; prefetch both sides of commonly flipped rooms, water paths, camera views. Larger region chunks gather traffic and reduce requests but waste residency on occluded rooms; per-room granularity increases overhead and seam misses. |
| Texture payload by use | Retain descriptor/animation and palette/cluster ID directory; cache only selected cluster image/palettes or region tiles; deduplicate shared assets across dependencies and reserve next animated member/flip equivalents. Separate logical ID from residency offset; adjust tex backend bind and GPU ownership (`gpu.c:282–349`; TX64:17–53). | A room is not visually ready until its geometry **and** usable materials for that draw are ready. An always-resident low-res fallback or no-draw boundary can keep movement correct but can hide traps, doors and cues. Higher-res optional refinement is different from mandatory texture availability. |
| Meshes and cold animation | Keep collision/simple bounds, item instances/links, trigger/flip graph, boxes/zones, state changes, commands and frame/collision data for active/soon-active objects including Lara, vehicles, pickups, camera and gameplay effects. Mesh geometry (MG64) can be streamed independently if no simulation refs; only proven presentation-only or currently unreachable animation ranges may be cold. `game/animation.c:9–87`, `game/game.c:235–278`, `game/world.c:15–92`, CORE §Layout. | Activation, enemy shot from a distant room, cut actors and dynamic effects need data on the correct tick even if not visible. Use prewarm or a verified synchronous *transition*, not delayed simulation of an offscreen enemy. Jobs can pin assets while GPU/DMA or logic holds references. |
| Sound | Reuse SN64/musical streams; adjust priorities/pinning of imminent triggers, short essential cues and resident bank limits instead of loading all MAIN.SFX. Source assets stay in host cook (§1). | Protect voice/music deadlines against competing region reads; no arbitrary dropped timers or RNG draws to hide a miss (`docs/perf-audit.md:80–103`; PACK:158–181; SN64 §§Encoding, streaming evidence). |

**Bandwidth model and evidence.** Scheduler is one DMA in flight,
4 KiB pieces, max 32 KiB per play frame across regular users
(`engine/io/io.c:2–3,29–57`; PACK:168–174; architecture §4.2).
`plat/host/sys.c:73–80`/§4.2 model **195 µs/KiB** (~5.0 MiB/s wire model,
0.780 ms/4 KiB or 6.240 ms/32 KiB); at 30 Hz the admission cap alone
is **960 KiB/s**, without audio/code competition. On the measured ares
cart path in `$TR64_WORK/evidence/m8/split/measure/{start-1,level6-r45-1}/engine.log`,
`level-load` reports LEVEL1 545,116 wire bytes / 116,676 `bus_us`
(~4.46 MiB/s), LEVEL6 640,836 / 138,018 (~4.43 MiB/s): divide logged
bytes by queued-completion elapsed, not pure PI time. The same records
show decode/hash at 131,959/129,003 and 154,985/152,449 µs,
respectively, for their whole waits. Do not extrapolate those costs as
hardware or every chunk's latency (PACK:270–278; `docs/perf-audit.md:3–7`).
For example 96 KiB wire needs **at least three** budget windows even with
no competing work; reserve for contention, decompression/CRC, alignment,
and main-thread/queue servicing. Track clocks, inflight/decode/pinned
bytes, stalls/underruns, worst readiness, not just average throughput.

**Cache and missing work [inference].** A resident, checked level index
with validated scatter targets plus bounded cache/staging carved once;
location IDs/generations stable across asynchronous completion, shared for
multiple users, no stale GPU pointer read. Protect in-flight data and
space for prefetch plus live chunks, acknowledge eviction only after
commands complete; room/camera/kill/flip/save state must outlive cache
entries. Separate I/O admission from render callbacks and budget upper
bounds for indices, working set, compressed and decoded staging plus
transition overlap. Policy alternatives: hold the last safe view at a
miss with observable cue and preserve tick determinism, a visible loading
boundary/pause before a committed traversal, or proven resident fallback
art for presentation-only misses. Never permit a cache miss to change
collision or trigger result. Probe fast boat/zip/teleport, flip, reload,
full-room turn and protected audio; test and measure two disjoint
camera/Lara/AI working sets and simultaneous region crossing—not twice
same-room size by assumption. `architecture.md:279–297,631–659,972–999`
and M8 evidence above bound the current 4 MiB/viewport problem, not this
future cache. Fit the measured single-player and split sections in stock
4 MiB where possible; Expansion Pak is a possibility for measured
larger cases, not evidence of fit (scope §1, budgets §4, D2/D14 and user
steering). Choose cache granularity/miss policy after sequel counts and
actual working-set traces exist.

## 6. Budget risk before a sequel cook

**This is not a sequel size measurement.** Raw file sizes, record widths,
reader limits and catalogue counts cannot establish native RDRAM use or
content complexity. To anchor the comparison, the archived **TR1** source
census `$TR64_WORK/evidence/m0/cook-m0/counts.json.zst` has 16 gameplay
entries per platform (15 levels + GYM; excludes title, cinematic files and
Snowcut). For rooms, count `rooms/<N>/geometry`; for vertices sum their
`vertices`; otherwise use `sections[].count` of the named table, not the
byte size (PC `sample_offsets` counts samples and PSX `samples` counts
VAGs, not the fixed 256-entry length table).

| Source census field, TR1 only | PC min..max | PSX min..max |
|---|---:|---:|
| Rooms (incl. alternate entries) | 19..139 | 19..139 |
| Room vertices per level | 3,967..12,088 | 3,967..12,088 |
| Room triangle equivalents | 6,767..19,773 | 6,767..19,773 |
| Texture pages / object descriptors | 9..13 / 845..1,277 | 13..13 / 1,059..1,486 |
| Placed items / models | 1..192 / 24..63 | 1..192 / 24..63 |
| Animation records / samples | 190..351 / 76..112 | 190..351 / 51..110 |

**What the sources constrain.** RS §§3.2.5,6.2.2,7.3.1,8.2–4,10.2–3
provide field/count widths and new payloads, not distributions: more bytes
per PC vertex and raw texture page (each PC sequel page has both 65,536 B
of indices and 131,072 B of 16-bit pixels), compact PSX room records with
changed palette/texture residency, larger zone/sound maps, external PC
sound pool, variable-size packed poses. OL's reader allocates from declared
counts (`format.h:3456–3612,3719–3758,3784–3855,6313–6333`) but has practical
implementation bounds: 128 spare entities, 32 trigger command slots,
1,024 mesh slots, four weapon slots, 32 joints (`format.h:8–12`), 16 room
part offsets (`:5124–5137`) and 512 SFX offset scratch entries
(`:4812–4827`). None is a documented retail maximum. Source-record sizes,
model slots (§3), and added moves are signals to measure frames, assets and
working sets, **not** that every room/level is larger. Known edition file
size recognisers in OL are spot comparisons only:

| Source filename | PC bytes | PSX bytes (one recognised non-JP entry) | Evidence |
|---|---:|---:|---|
| TR1 `LEVEL1` | 2,533,634 | 1,448,896 | OL `gameflow.h:536–542`; TR1 census |
| TR2 `WALL` | 2,986,356 | 1,661,622 | OL `gameflow.h:681–684` |
| TR2 `BOAT` | 3,945,738 | 2,014,880 | OL `gameflow.h:685–689`; PSX label is inference from size-case order, not an identified serial |
| TR3 `JUNGLE` | 3,360,001 | 1,930,923 | OL `gameflow.h:779–782`; PSX label is inference from size-case order, not an identified serial |

**Pools to check (risk is inference, not a budget change).**

| Pool | Current evidence and sequel failure to look for |
|---|---|
| `level` 1,472 KiB | PC LEVEL10B 1,440,528 B, leaving 66,800 B; PSX LEVEL6 worst 1,428,800 B (PACK:137–142). Count and cost CORE tables/frames, RG triangles/colours/light extensions, MG, used CI4 clusters and both CLUT domains. Raw file size has no fixed ratio to these four resident sums. |
| `state` 192 KiB | TR1 documented high-water with menu/save workspace 178,544 B (architecture §10.1 M5, §4.1); game currently reserves 256 item slots, `ITEM_MAX` models, nine navigation-cell arrays multiplied by boxes, plus room/sectors (`game/game.c:224–278`). Check authored plus spawned items/AI/FX, additional zone/owner state, render cache indices, animation mapping, and 8 KiB save/pick limits (D6). Catalogue slot count is not active item count, and blindly indexing a sparse 2000-offset namespace would inflate storage. |
| `code` 96 KiB; `image` 576 KiB | Controller families, loaders and codecs pressure both; measure maximum simultaneously resident units and linked image separately. No “one controller per ID = one code unit” estimate (architecture §§4.1,6,8,11; `include/tr64/pools.def:11–20`). |
| `gpu` 232 KiB | TR1 archived split run 230,608/237,568 B boot; bridge already drains its record arena at peak. More visible triangles/material blends, light/mesh-recolour work and room-cache bookkeeping can hit both records and frame time (`$TR64_WORK/evidence/m8/split/measurements.md:5–29`; `docs/perf-audit.md:26–35`). Texture shrink helps image traffic, not an automatic triangle/record saving. |
| `audio` 128 KiB | TR1 127,584/131,072 B; more detail/sample entries, working resident sounds/voices and required music windows need peak measurements; raw SFX bank bytes are cart/cook pressure, not all decoded in RAM (`docs/architecture.md:263–275`; SN64:87–114). |
| `io` 48 KiB | TR1 M8 peak 41,264/49,152 B. Cache/index/live DMA/staging overlap and wider section tables can exhaust it even with smaller compressed ROM files (PACK:145–181; M8 evidence line 27); track service and bytes separately (§5.3). |
| Cart 64 MiB and fixed rows | Sum post-dedup compressed levels, media and code vs 64 MiB, not raw source CD sizes. Stack/library heap still need bounds with new decoder/caches; FB/Z sizes stay unchanged if resolution/format stay the same. Video borrows unloaded level memory, so measure it as its own phase (architecture §§4.1,4.3,10.1 M4; `include/tr64/pools.def:11–21`). |

### 6.1 Input handoff: files needed

Preserve each release's directory names and language/region, identify
retail version and distinguish Steam/CD, patches, separate demos and
expansions. The following is the **input contract**, not a claim to have
mounted every regional disc. For geometry/core exact sizing, give every
listed level/cutscene file; for a first cost pass give the minimal pair
sets below; for ROM/media/save sizing give the remaining named inputs.
Case and alternatives are resolved with the script's actual paths. OL's
reader paths are evidence of expected inputs, not a replacement for a
release listing (`gameflow.h:1048–1095,1153–1164,1289–1305,1406–1488,1490–1681`;
S2 TR2.2–3; RS §§2.1,10.2–3,11.1–2).

| Edition | Exact level sets and paths (use the stems below) | Media, script and metadata |
|---|---|---|
| II PC | `DATA/<II stems>.TR2` (including cut files), minimum `DATA/{TITLE,ASSAULT,WALL,CUT1,BOAT}.TR2`. `BOAT` is Venice; `VENICE` is Bartoli's Hideout (S2 TR2.2; OL `gameflow.h:289–312,1084–1091`). | `TOMBPC.DAT` at its supplied script location and `DATA/MAIN.SFX`; `FMV/{LOGO,ANCIENT,MODERN,LANDING,MS,CRASH,JEEP,END}.RPL` as named by script/OL, title/credit PCX named by script (`DATA/TITLE.PCX` or `PIX/TITLE.PCX` checked by OL). Full mixed-mode CD CUE + its referenced audio track files (or lossless numbered extracts with CUE, including gap data); Steam media see actual acquisition, don't assume CD tracks are present. RS §§8.1.1,11.1,11.5.3; S2 TR2.3–4; OL `gameflow.h:1406–1412,1503–1509,1604–1646`. |
| II PlayStation | `DATA/<II stems>.PSX` including cuts; minimum `DATA/{TITLE,ASSAULT,WALL,CUT1,BOAT}.PSX` expected by OL `gameflow.h:1084–1094`. | `TOMBPSX.DAT`, title/loading RAW assets (`PIXUS/TITLEUS.RAW` or regional equivalent, `PIX/{MANSION,CHINA,VENICE,RIG,TITAN,TIBET}.RAW` as available); all script-referenced STR streams with raw sectors and complete disc CUE + referenced tracks for CD-DA. Script rather than PC names supplies exact console extent/file names; no `MAIN.SFX` needed for the embedded samples (§1.1; RS §§2.1.3,11.1–2; OL `gameflow.h:1406–1443`). |
| III PC | `data/<III level stems>.TR2`, `cuts/<III cut stems>.TR2`; minimum `data/{TITLE,HOUSE,JUNGLE,TEMPLE}.TR2`, `cuts/CUT6.TR2` (OL `gameflow.h:313–346,1093`; Jungle ending). | `TOMBPC.DAT`, `data/MAIN.SFX`, `audio/CDAUDIO.WAD`; referenced `fmv/*.RPL` including English names `logo.rpl`, `Intr_Eng.rpl`, `Sail_Eng.rpl`, `Crsh_Eng.rpl`, `Endgame.rpl` (OL's probes, script authoritative); referenced `pix/` images, e.g. `TITLEUK.BMP` and `INDIA.BMP` (OL `gameflow.h:1159–1163,1302–1305,1444–1488,1511–1515,1650–1681`; RS §§2.1.3,8.1.2,11.1). |
| III PlayStation | `DATA/<III level stems>.PSX`, `CUTS/<III cut stems>.PSX`; minimum `DATA/{TITLE,HOUSE,JUNGLE,TEMPLE}.PSX`, `CUTS/CUT6.PSX` expected by OL `gameflow.h:1094`. | `TOMBPSX.DAT`, `PIXUS/TITLEUS.RAW` or regional equivalent, script-referenced loading assets/STR; **raw Mode 2 data track (`.bin` and `.cue` referencing it)** to census the console music/movie files or sector streams (not assumed to be the PC WAD), with directory listing. File-only ISO loses XA information the music/video audit may need (RS §§2.1.3,11.1–2; Redump SLES-01649, §1; `docs/formats/n64-video.md:316–319`; OL `gameflow.h:1444–1488`). |

- **II stems** (OL `gameflow.h:288–312`; S2 TR2.2): `TITLE`, `ASSAULT`,
  `WALL`, `BOAT`, `VENICE`, `OPERA`, `RIG`, `PLATFORM`, `UNWATER`, `KEEL`,
  `LIVING`, `DECK`, `SKIDOO`, `MONASTRY`, `CATACOMB`, `ICECAVE`, `EMPRTOMB`,
  `FLOATING`, `XIAN`, `HOUSE`, and `CUT1`, `CUT2`, `CUT3`, `CUT4`.
- **III level stems** (OL `gameflow.h:313–346`): `TITLE`, `HOUSE`, `JUNGLE`,
  `TEMPLE`, `QUADCHAS`, `TONYBOSS`, `SHORE`, `CRASH`, `RAPIDS`, `TRIBOSS`,
  `ROOFS`, `SEWER`, `TOWER`, `OFFICE`, `NEVADA`, `COMPOUND`, `AREA51`,
  `ANTARC`, `MINES`, `CITY`, `CHAMBER`, `STPAUL`. **Cut stems in that table**:
  `CUT1`, `CUT2`, `CUT3`, `CUT4`, `CUT5`, `CUT6`, `CUT7`, `CUT8`, `CUT9`,
  `CUT11`, `CUT12`. Include any additional edition entries referenced by
  the supplied DAT, not a presumed `CUT10`.
- **Save compatibility only if desired:** two player-generated PC
  `savegame.N` files per first level (initial and after changing keys,
  level items, flip/vehicles) and matching PSX memory card exports; III
  after crystal use and following a regional branch. Save inputs are not
  required for geometry census. Exact retail serialised layouts were not
  verified (§1.2).

A real census must produce per-edition sections/rooms/sectors, geometry
vertices/tris, texture pages and used clusters/blend/CLUT/animation closure,
resident-vs-wire CORE/TX64/RG64/MG64, objects by type and concurrent AI/FX,
boxes/zone counts, animation frames/words, sound-map details/index, unique
and pinned/sample-stream bytes, media totals and in-play room/region overlap.
Use the paired independent section ledger level by level before measuring
allocation and transfer peaks (IR §§Full-byte accounting and comparison,
Commands and artifacts; §5.3). The existing tr64 readers do not parse a
sequel unchanged; OL source contains readers but no existing headless
census entrypoint was found at the pin. No parser or host build was made.

### 6.2 Delivered PC inputs: file metadata only

Read-only `find` and `stat` on `$TR64_DATA/pc-tr2/` found all 24
II stems above (case-insensitive `.TR2`; includes `TITLE` and four cuts):
80,473,884 B total. Excluding title/cuts leaves 19 files including
`ASSAULT` and `HOUSE`, 71,598,880 B, with `WALL.TR2` the smallest
(2,986,356 B) and `MONASTRY.TR2` largest (4,325,562 B). First-slice
cross-checks: `CUT1.TR2` 1,767,052 B, `BOAT.TR2` 3,945,738 B; alongside
are `TOMBPC.DAT` 5,340 B and `MAIN.SFX` 7,961,208 B. The copy is flat at
this location; no media tracks were present in that inventory. No
`pc-tr3/` or sequel PlayStation data was present at that check. These
are file counts and byte lengths, **not** level topology, a parse,
an installed-game total, or evidence for a native pool.

## 7. Questions for the wall reader

Order within each game is progression blockers first, then survivability,
optional content and presentation. Answer with conditions, units and frame
order, distinguishing PC/PSX data-driven routes from controller constants;
no source or source-shaped pseudocode. Without retail placements, a route
identifies a feature, not its authored item/sector indices. Keep the public
and OL baseline separate from the answer (`docs/PROVENANCE.md:63–82`).

### Tomb Raider II — The Great Wall → Venice

1. **Q-TR2-001 — Ladder mount, climb, transfer and exit.** Wall “Second
   Guardhouse” requires the Rusty Key above a ladder; the cavern descent
   also needs side/down movement and jumping back onto a ladder. RS §4.4.6
   gives the four direction bits; OL only records them (§2). Give the Lara
   alignment/clearance tests for floor and airborne mounts, offsets per
   climb step including side/corner transfers, release/back-jump and top
   pull-up checks, and the state/animation/frame that commits each snap.
2. **Q-TR2-002 — Zip line (ID 102).** Wall “Zip Line to the Exit” shows
   mount, ride, early release and landing; NG #70 and §3.2 only identify
   state and handle. Give mount reach/angle/hands, handle animation and
   Lara ownership, motion increments/clamp/room transitions, stop at
   obstacle/end, release velocity, and reset/reuse/save rule. Which of
   those are authored animation commands?
3. **Q-TR2-003 — WALL trap corridor.** Wall “Flooded Room With Flying
   Blades” and “Gauntlet” give route-level safe lanes; RS §15.1.2 #55,
   57–62,80–85,87 lists candidate families; OL shares the TR1 floor/spikes/
   dart/boulder but has no flying-disc/spindle/spiky-wall units (§3.2).
   For each placed blade/disc, floor, boulder, spike wall and rolling blade
   give the matching type, armed→active→spent conditions, movement/collision
   bounds and damage cadence, and whether a door/camera/trigger changes
   those rules. Which floor tiles yield on Lara standing vs another item
   or heavy activation?
4. **Q-TR2-004 — Venice speedboat rider.** C2 “Vehicles” and Venice
   “First Canal,” “Dark, Flooded Tunnel,” “Race to the Exit”; OL `Boat`
   is a TR1 prop (§2). Give bank/water/drop boarding and side clearance,
   mount/dismount frames, rider input/animation ownership, acceleration/
   reverse/boost/brake/steer/max ranges, bow/side/float probes, drift or
   jump after leaving water, impact damage, samples and camera priorities.
   What is updated first on a tick with impact and dismount together?
5. **Q-TR2-005 — Venice mine and gondola.** Venice “Clearing the Way”
   and both “Canals” passages; RS §15.1.2 public Aquatic Mine #222 vs OL
   `TRAP_MINE` #221 and `UNUSED_30` #222; §3.2 has no working behaviour.
   Resolve placed and spawned IDs. Give mine attach/arming/radius, boat
   vs swimmer contacts, local versus linked-chain delay, damage and boat
   removal, persistence after bailout/save; for gondola, collision and
   speed threshold, shatter effects and when the path becomes passable.
6. **Q-TR2-006 — Venice lock, timer and gates.** Venice “Dark, Flooded
   Tunnel” and “Race to the Exit” show raising water, a post-button threshold
   and about 23 seconds; swimming under can bypass starting clock; OL
   implements generic flip and timed switch/camera (`lara.h:2035–2058,
   2167–2216`), not the under-threshold race semantics. Which placed roles
   and trigger owners/actions make the lock/lever and end gate work? Give
   timer start conditions/units, expiry/close phase and bell count/rate,
   camera/Look precedence, repeat/use/save rule, and how a parked boat is
   moved when rooms flip. Can either boat/swimmer finish, and on which cell?
7. **Q-TR2-007 — Windows/button/key/drop chain.** Venice “First Canal”
   and “Stone Bridge” require breakable glass, button/trapdoor/ladder and
   keys from enemies. OL `BreakableWindow` only initialises a mesh mask
   (`objects.h:1691–1697`); buttons have OL interaction code (§4). Give
   collision and hitscan mask/material/damage rules before/after shatter,
   initial door/trigger/timer masks, reuse policy and the actual enemy-held
   item binding/drop tick including whether a sniper killed before opening
   his route or displaced over the canal can lose the Boathouse Key.
8. **Q-TR2-008 — Guardhouse key and shared activation.** Wall “First
   Guardhouse”, “Pool Below the Wall”, “Second Guardhouse” show trapdoor,
   underwater key and Tiger after collection; OL consumes ordinary key/pickup
   actions (`lara.h:1802–1870,2062–2131,3196–3249`). Specify owner/timing for
   contact trapdoor, carried key vs sector pickup activation, duplicate-use
   handling, lock animation frame and action ordering when pickups move,
   switches expire, or two actors are on one sector (retail single Lara
   precedence only; tr64 co-op policy is separate).
9. **Q-TR2-009 — Early ammo/loadout and secrets.** Wall start/“Climbing
   Down”, Venice secret sections and RS §11.5.1–2 establish 3 dragon slots
   and reward grants; OL has neither the inventory conversion nor the
   rewards (§2). Give starting inventory quantities in engine units, each
   dragon's collection/chime/bit timing, last-secret rewards for any pickup
   order and already-owned weapon, save persistence, ammunition pack-to-round
   conversions (Uzi ambiguity §2) and whether enough resources are granted
   without collecting optional caverns or triggers out of order.
10. **Q-TR2-010 — Wading and reversals.** C2 “Combo Moves”/“Swimming,”
    Wall “Cave with Pool,” Venice canals; OL provides proposed thresholds
    and entry conditions (§2). Give actual raised-floor/feet/water-depth
    thresholds with equality, movement velocity and plane transitions,
    accepted air/swim roll frame windows, heading changes, and input/weapon
    interruption on entry/exit. Is standing underwater on a slope treated
    differently or as ordinary wade?
11. **Q-TR2-011 — Early opponents.** Wall “Pool Below the Wall,” “Second
    Guardhouse,” “Huge, Dark Cavern”; Venice “First Canal,” “Second Main
    Area.” OL Tiger and reused Rex actually attack, Dog only has a sleep/
    death override, and spiders/crow/rat/guards are generic (§3.2). For
    tiger/snow-leopard variant, crow, small spider, Rex, Doberman, rat,
    stick thug, gunman/sniper: activation and sleep/wake, AI zone/step
    and flight rules, attack selection/ranges/turn/stance, damage/HP/
    contact joint and one-hit latch, drops and corpse/save rule. Prioritise
    enemies that drop progression items; identify shared families and
    concrete differences rather than transcribing full routines.
12. **Q-TR2-012 — Flare lifecycle.** C2 “Basic Moves” and Wall/Venice dark
    sections; OL has IDs, no held/throw state (§2). What frames credit a
    pickup, decrement stock, ignite/update light, drop/throw/expire, what
    underwater or hand/action states allow each, and what flame emitter,
    lifespan, bounce, relight/pickup and save fields apply? What light is
    gameplay necessary indoors?
13. **Q-TR2-013 — First cut, level end and audiovisual cues.** Wall
    “Cave” and “Zip Line to the Exit” give departure shot, exit and talk
    cut; Venice names its bell and water rush. RS §§9.7,11.5 and OL
    `gameflow.h:289–296` identify cut files but not exact activation policy.
    Which end/camera condition (and Look/skip override), cut actor origin/
    angle, post-cut destination, track/start offset and level health/
    inventory carry should be asserted on either edition?
14. **Q-TR2-014 — Script restrictions outside the initial slice.** RS
    §11.5.1 has Sunset/DeadlyWater, NoFloor, kill-to-complete and start
    inventory opcodes. For Bartoli, Rig, Temple and House (where such
    directives are authored), give per-edition operands, reset scope, actual
    condition/order and whether stale/unused opcodes are no-ops; define
    race if a last kill and another end condition happen together. No retail
    opcode reader is executed by OL (§1.2).
15. **Q-TR2-015 — Static-mesh shade word.** A room's static-mesh
    placement carries two intensity words in the second game's level
    format (RS §3.2.8 calls the second "seemingly unused"); OL reads the
    second (`format.h:5527–5543`). In The Great Wall 8 of 52 placements
    differ (for example 6144 / 4096). tr64 follows OL provisionally.
    Which word does the retail game shade a static mesh with, is the
    other read at all (alternate-room or flip state, dynamic lighting,
    another edition), and how is the value mapped to vertex brightness?
    The same question for an item's two intensity words where the
    answer differs. (Raised by the first cook of the level, 2026-11-21.)

### Tomb Raider III — Jungle

1. **Q-TR3-001 — Crouch/crawl volumes and actions.** Jungle “Misty Area,”
   “Fallen Tree,” “Tunnel With Switch” and hollow-tree secret, NG states
   71/80–88, M3 pp. 8–9; no OL implementation (§2). Need standing/crouch/
   crawl bounds and step/slope limits, transition and clearance tests at
   under-tree opening, edge/back-out/pullup animation and offsets, pickup
   frame/standing state for med/harpoon/rocket, weapon safety and what
   happens when Crouch is released too early.
2. **Q-TR3-002 — Both zip lines and ladder grips.** Jungle “First Zip Line,”
   “Calm Pool” and flooded courtyard; IDs 127, state 70 and climb mask
   documented in RS/NG, not played by OL (§2). Give differences from
   Q-TR2-001/002, including release into the current, line-to-ledge snap,
   room switching and reset/retry rule. Can the lines be re-used after
   the first river sweep, or are alternate handles/flip sectors involved?
3. **Q-TR3-003 — Indra Key and monkey theft.** Jungle “On the Roof,”
   “Overgrown Courtyard,” “Dense Jungle,” RS §15.1.3 #71,123–124; OL
   has no monkey controller (§3.3). Give neutral/retaliate/follow/carry
   state graph, how a monkey picks an item and destination, medipack drop
   vs key retention, races with Lara, exact drop-on-death frame and
   representation, meshes and persistence after load. Can the final key
   ever be inaccessible (pit/water/clear-body) in retail?
4. **Q-TR3-004 — Current, piranhas, quicksand and final trigger.** Jungle
   river/current, swamp, piranha pool and exit; RS §§3.2.3,4.6.3,9.5;
   OL has box-following current but no independent quicksand/swarm rule
   (§4). Specify current force/damping, vertical clamp/surface suppression
   and release at last sector, piranha emitter activation and damage
   schedule, which room/sector property selects quicksand, sink height per
   tick, breath/damage/death threshold, jump/escape/transition rules and
   exact eligibility of the gated exit after the Indra Key.
5. **Q-TR3-005 — Triangle floors, half-portals and slope progression.**
   Jungle opening and crystal/triangular ledge route; RS §4.4.7 documents
   plane arithmetic, OL offers pieces but no proof of boundary parity (§4).
   Give edge equality and tie rules, high/low collision sample selection,
   slide/jump/ledge response, stand-on-triangle reach, half-portal room
   traversal and the blocked/sliding box sentinel across foot/camera/AI.
6. **Q-TR3-006 — Moving wall, spike pits and boulder sequence.** Jungle
   “Small Room With Moving Wall,” “Long Slope,” “More Dense Jungle and
   Triple Boulder Trap”; RS §15.1.3 #87–89,114 and §3.3. Give placements'
   type families, damage/geometry/timer and trigger/one-shot/reset rules,
   still boulder vs moving touch, run/walk/fall spikes and safe vertical
   range, when a timed window or reopening grate is actually repeatable.
7. **Q-TR3-007 — Block/water flip/lock chain.** Jungle “Room With Pool”
   through “Mossy Courtyard ... Flooded”; old block/door/camera logic is
   reusable substrate (§§3–4). What movement, heavy ownership, switch/door
   sequence, alternate-room changes and entry heights are authoritative in
   retail vs demo? Give flip/antitrigger/one-shot mask order and what
   static collision and pickups are preserved after reload/rewind.
8. **Q-TR3-008 — Tiger and floor-assisted threat.** Jungle “Dense Jungle,”
   “Fallen Tree” and “Mossy Courtyard”; OL uses shared Tiger with attack/
   roar/mood and no III-proof tuning (`enemy.h:3260–3382`). Specify TR3
   changes to HP, attack/contact window and bite, tile/zone/height checks,
   activation/return behaviour when Lara remains on elevated pillars,
   mud/water navigation, and how to count kills for optional monkeys.
9. **Q-TR3-009 — Jungle secrets, crystals and item stock.** Jungle start,
   six secrets and crystal footnote; NG and §2, OL visual crystal only.
   Give what actually fires secrets, skull/bit/count/stat guards when
   revisiting, whole-campaign bonus threshold/edition handling, PC instant
   medicrystal use vs PSX inventory reservation and when expenditure is
   committed on failed/interrupted save, initial weapons/health/air and
   ammo unit conversions. Clarify the manual's starting shotgun claim vs
   the retail Jungle route and demo distinctions.
10. **Q-TR3-010 — Flares, weapon damage and poison.** M3 pp. 7,9,11 and
    C3 controls give symptoms, OL hitscan subset (§2). Give TR3 differences
    from Q-TR2-012, look/flare while crouched, underwater pickup/fire rules,
    shot vs projectile/blast states and per-weapon ammo accounting; specify
    poison sources, damage/flash cadence, medikit cure order and whether
    poison survives a cut/level transition.
11. **Q-TR3-011 — Sprint and monkeyswing.** NG #73–79,82–83 and M3 pp. 5,9
    document player action, neither simulated by OL (§2). Need stamina
    maximum, decrement/recovery/trigger gating, skid and jump-dive ingress/
    exit, camera effects, and ceiling grab/turn/movement/split-sector reach
    and drop offsets; identify if Jungle uses either as required passage,
    optional use, or neither.
12. **Q-TR3-012 — Environment, rail and quad semantics (post-Jungle).**
    RS §4.4.9–10 and C3 Vehicles give rail direction/stop and quad controls;
    M3 p. 6 gives exposure in cold water. Identify real III room/script flags
    for cold/heat/weather/lightning and deadly water, exposure/drowning
    counters and recovery, whether triggerer/quad sector rules exist apart
    from the documented rail markers, and minecart switch-hit/turn/stop
    tolerances. No IV/V opcode should be used by inference (§4).
13. **Q-TR3-013 — Exit cut, branching and per-edition save payload.**
    Jungle ends with the Tony cut; M3 p. 12 shows region selection and
    healing; OL simply follows `id+1` (§1.2). Give cut origin/trigger and
    next level, health/stat resets, region unlock/selection and resumption,
    checkpoint/carry semantics (including crystal stock and current actor
    or vehicle state), and any distinct native-state requirements versus
    on-disc retail layout (requested byte shape not proven by public sources).

**Evidence limits.** The state tables, level format/service reads and
published routes above establish a work queue, not sequel playability.
No retail save payload, disc/executable code, wall source or other engine
source was read. No host build, emulator or host suite was used; TR1 memory/
frame arithmetic uses retained reports and all sequel controller coverage is
static. Unverified numeric rules stay in the questions rather than being
backfilled from similarly named OL symbols.
