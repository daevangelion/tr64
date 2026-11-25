# Classic PC sequel levels: source reader and IR projection

Implementation: [`src_tr2_tr3.py`](../../tools/cook/src_tr2_tr3.py), synthetic
[`test_src_tr2_tr3.py`](../../tools/cook/test_src_tr2_tr3.py). Scope: classic PC
II/III `.TR2` level bodies, and the corresponding external `MAIN.SFX`.
Not a native pack, a sequel gameplay loader, a script reader, or a PSX reader.
No executable or other engine was consulted. **RS** is
[TRosettaStone 3 preview](https://opentomb.github.io/TRosettaStone3/trosettastone.html);
**OL** is [OpenLara `2333adef`, `src/format.h`](https://github.com/XProger/OpenLara/blob/2333adef30ae2a5f3b32c5355e6e343d6aa0531a/src/format.h).
References give layout facts, not ported code or behavioural authority.

## Physical ledger

The reader's exact ledger is checked against a second byte walk,
[`account_tr2_tr3.py`](../../tools/cook/account_tr2_tr3.py), that does not use
its IR or cursor. Agreement detects offset, stride, and count drift;
it does not establish gameplay meanings or correct a shared spec error.

Fields are little-endian. Repeat counts are u32 unless noted; all count
prefixes have separate ledger charges. Room geometry and mesh-data counts
are **words**, not byte lengths. Rooms are u16-counted. The whole-file
order below is RS §§10.2–3 / OL `3456–3478,3519–3540,3714–3751`:

| Order | Structure | Public / OL detail |
|---:|---|---|
| 1 | `version` u32; 768-byte indexed palette; 1,024-byte solid-colour palette | RS §§2.3.2–3,9.1–2; OL `3170–3203,3456–3460,3519–3523` admits II `0x2d` and III `0xff080038`, `0xff180038`, `0xff180034`. |
| 2 | One count, then its 65,536-B indexed pages and same number of 131,072-B ARGB1555 pages; u32 reserved level word | RS §§2.3.3,10.2–3; OL `3456–3460,3519–3523,3714–3719`. |
| 3 | Rooms: 16-B header; word-sized geometry; portals; sector grid; ambient; lamps; static instances; alternate/flags; III three-byte tail | RS §§3.2.1–8,3.4.2–3,10.6–7; OL `5112–5122,5263–5311,5318–5380,5390–5559`. See room breakdown below. |
| 4 | Floor words; mesh-word blob, then u32 mesh byte pointers | RS §§4.2–6,5.2–4; OL `3724,3738–3739,5092–5110,5602–5614,5767–5826`. Distinct pointers decode once using the TR1 PC record layout; duplicates are not consolidated in the pointer table. |
| 5 | 32-B animations, 6-B state changes, 8-B dispatches, i16 command words, i32 bone words, u16 frame words, 18-B models, 32-B static definitions | RS §§5.2–4,6.2.1–2,6.2.4–7; OL `2666–2717,3738–3799`. ANIM command byte spans use RS §6.2.6 and OL `1390–1399`; sound/effect arguments remain game-local words. |
| 6 | II-only location of 20-B object descriptors; 16-B sprite descriptors, 8-B sprite sequences | RS §§5.6–7,9.3.1–2,10.2–3; OL `6090–6104,6162–6167,6205–6220,6244–6259`. Integer sprite corners follow OL's high-byte inclusive endpoint, not a new interpretation of the width/height comments in RS §5.6. Raw adjustment/size words are retained. |
| 7 | 16-B cameras/sinks and sound sources; 8-B compact boxes, u16 overlaps, ten box-count i16 zone arrays; animated texture words | RS §§7.3.1,8.3.1,9.4–5; OL `3803–3856,6282–6305`. Camera type is implicit in the referring action. Box extents are multiplied by 1024; both overlap flags remain raw. |
| 8 | III location of object descriptors; 24-B items; 8,192-B LightMap | RS §§5.5.2,9.8,10.2–3; OL `3532–3535,3858–3863,6313–6333`. LightMap is kept unchanged, including column zero. |
| 9 | u16 cinematics count, 16-B frames; u16 demo byte count, then exact bytes | RS §§9.7,10.2–3; OL `3865–3875`. These are the IR's existing cinematic and opaque demo sections; this reader makes no timing or replay claims. |
| 10 | 370 i16 sound-map entries; 8-B sound details; u32 sample ordinals | RS §§8.2–4,10.2–3; OL `3877–3910,4812–4836`. III detail is `u16,u8×4,u16` rather than four u16s. No inline sample blob or offset table. |

### Rooms

- RS §§3.2.1,3.2.5–7: geometry includes a u16 vertex count with 12-B XYZ,
  two lighting words/attributes records; u16-counted 10-B quads, 8-B
  triangles, and 4-B sprites. It is parsed in a separate length-bounded
  cursor with its own exact EOF check. OL `5263–5311,5318–5380,5390–5404`.
- RS §§3.2.2–3 / OL `5406–5441`: u16-counted 32-B draw portals; u16 Z/X
  dimensions followed by their product of 8-B sectors. III preserves the
  raw sector box/material word; keeps material (low nibble) separately and
  projects the documented 2047 special box and `0xffff` to IR's no-normal-
  box marker, never to box zero. The actual slide/collision rule is not
  implemented.
- RS §§3.2.4,3.4.2–3 / OL `5444–5517`: both have two ambient words; II has
  an additional light mode. Lamps are u16-counted and 24 B each. II stores
  both intensity/falloff pairs; III stores RGB/type with an opaque 8-B
  type-specific union (sun or point). This does not follow OL's provisional
  intensity/radius interpretation of that union. Unlisted III lamp types
  are errors.
- RS §§3.2.8,3.4.2–3,10.7 / OL `5518–5559`: u16-counted 20-B instances
  carry two original shade words (III first tagged as RGB555); alternate
  room i16, flags u16, then III water scheme, reverb and a third byte.
  RS calls the latter a filler; OL carries it as filter. No zero or filter
  behaviour is assumed.

### Floors and the external bank

`floor_data` retains all words and original offsets. Decoding for the
census follows RS §§4.2–6 and OL `src/lara.h:2108` for the controller
entry on pickup, not just switch/key. II reads the low byte, III reads
bits 0–4 (triangulation uses 5–7). Lateral room/slant and III triangle
functions carry one word; climb, death, monkey-swing and rail markers are
inline. Trigger setup and the separate action/end stream are checked; the
camera parameter has the action end bit. Counts are physical opcode
positions reachable from at least one nonzero sector start, not sectors or
runtime events. Zero is the no-floor index; raw unreferenced words are kept
and counted, never interpreted as opcodes. Unknown layout-level floor,
trigger, action, command, and blend codes are errors by name. Trigger
arguments that refer to level objects, cameras/sinks or rooms are checked;
script/opcode effects and raw per-game sound/item IDs are not remapped or
assigned a TR1 meaning.

RS §§2.1.5,8.2–4 and OL `4812–4827` specify the bank: concatenated RIFF
records, length `8 + riff_size`, addressed by ordinal. The reader checks
WAVE, chunk bounds/alignment, one 16-byte PCM `fmt `, one `data`, mono
8/16-bit encoding, and rate/stride consistency; unknown chunks are rejected
by name. The `.TR2` table preserves ordinals (including duplicates), not
byte offsets. With the bank provided, `Level.samples` holds exact WAVE
records in the local directory's order; without it, the encoding says
`pc_external_wave_unresolved` and no bytes are invented. The census counts
all slots, sound-map reachable variations, and unique external ordinals
separately. It does not assign playback frequency to any sequel sound ID.

## Named retained data and work still needed by a real port

The existing TR1 IR dataclasses, `Level.source='pc'`, and the existing
[IR's](ir.md) basic fields still carry shared shapes. In particular the
PC TR1 entry point does **not** accept sequel levels and no existing writer
was changed to implicitly treat a sequel as TR1. `Level.reserved['game']`
and `['version']` identify the domains. These extensions either preserve
raw payload directly or preserve every constituent word as tagged records;
the census accounts for the source byte sizes (overlapping categories are
not additive to the whole-file ledger).

| Named field / retained section | Physical contribution | A real IR/native extension needs |
|---|---:|---|
| `solid_colour_palette`, `argb1555_pages` | 1,024 + 131,072/page | Tagged image variants/alpha and selection, palette bank for flat colour's two byte indices. Indexed pages alone suffice only for the texture size proxy. |
| vertex `legacy_lighting`, `attributes`, `lighting` tagged `tr2_darkness_13bit`/`tr3_rgb555` | 2+2+2 per vertex after XYZ | Select the II active word or III RGB, don't treat III RGB as darkness; attribute mask, water/movement/light mode evaluation and renderer vertex path. |
| `ambient2`, `light_mode`, II `intensity2`/`radius2`; III `rgb`/`kind`/`sun_or_point` | 2/room each where present, 6/II lamp, 12/III lamp after XYZ | Tagged ambient semantics, local light evaluation and sun/point union after its interpretation is established. |
| static `intensity2` / `intensity_encoding`; item `intensity2` | 2 extra/record each, III first static word is 2 B RGB | Dual shade or packed RGB, not an OCB; instance/editor flags and game-local identity. |
| `water_scheme`, `reverb`, `filter`; `box_word`/`material` | 3/III room; the original 2-B III sector word | Authored water/reverb/filter rules, surface type, packed box sentinel and slide semantics. |
| `Face.flags`, flat `color`, `Texture.attribute`, `tile_flags` | original u16 words (flags/second colour domain not an extra geometry record) | Material domain + selected solid palette, double-sided rendering, actual additive blend and alpha/depth rules; optional other internal modes are not accepted as object texture modes. |
| `floor_data` with `floor_codes` annotation | 2/word | Versioned parser plus II+ ladder/controller semantics, III delta triangles, minecart, traversal and all trigger/flip effect meanings. |
| `frames`, `commands`, `animations`, `states`, `ranges`, `models` | existing words/record widths (frames 2/word), semantics differ | Sequel variable-length axis-tagged pose decode or rebake with rebased offsets; validate gameplay-reachable links, resolve game-local effects/types. Raw frames stay packed. |
| `extra_ground_zones` | 8/box | Two extra ground classes per state; decide invalid/blocked overlap sentinel and use actual creature selection policy. Ordinary `zones` contains ground 1/2/fly in each state. |
| `cinematic_frames`, `demo_data` | 16/frame, 1/demo byte | Source tables already fit the IR, but gameflow link, cut angle, actor roles and demo commands/timing need edition support. |
| `sound_map`, III detail range/pitch, `sample_ordinals`, external WAVE | 228 extra map bytes; 2 extra fields within each III detail; 4/ordinal and RIFF bytes external | Versioned mixer modes/variation/range/pitch, size and cache/rate policy, linked external sample provenance. |

Strict `read()` rejects unresolved data with a structure and byte offset.
For measurement only, `census=True` can retain three precisely bounded III
findings (out-of-domain dispatch target field; invalid `0x07ff` overlap
head; `0xffff` trigger action word) without inventing a decoded target or
meaning. None is admitted by the strict reader. An otherwise recognized
`0xff180034` prefix ending exactly after its pages raises `TextureOnly`:
the prefix is fully accounted, but RS §10.3 then requires a level body.
The census reports these separately from clean parses. It never invokes a
writer on an unbounded/truncated input. See [the census](../tr2-tr3-census.md)
for exact file locations and the cost/verification limits.
