# M10 PC Tomb Raider II / III census

This is a **memory and source-data census, not sequel play support**. Inputs
are the classic Steam files in [the source inventory](data-tr2-tr3.md),
not remasters, expansion packs, or PlayStation editions. The raw data are
in `$TR64_WORK/evidence/m10/census/pc.json`: exact input hashes and
source counts, every named validation finding and its byte offset, byte
breakdowns, matrices of room/model dependencies and the portal-bound room
indices. No source data or cooked pack was written into the repository.

Reproduce from the repo root (one process, native sections discarded in memory):

```sh
rtk env PYTHONDONTWRITEBYTECODE=1 OPENBLAS_NUM_THREADS=1 python3 \
  tools/cook/census_tr2_tr3.py --data $TR64_DATA \
  --output $TR64_WORK/evidence/m10/census/pc.json
```

## Source checks and boundaries

II: **24/24 strict parses**, 80,473,884 B. III: **13/34 strict parses**,
20 more byte-complete but with named validation findings, and one
texture-only prefix. Count-only [`account_tr2_tr3.py`](../tools/cook/account_tr2_tr3.py)
reproduces the structural decoder's physical ledger (counts, offsets,
prefixes, payload lengths and geometry subcounts), independently of its
IR and references. It does not prove the physical spec itself, or that
all source references are valid. III `VICT.TR2` version `0xff180034` ends
exactly at **2,361,096**, the end of 12 pairs of texture pages after the
palettes. RS §10.3 requires the reserved level word and rooms there;
OpenLara accepts that magic but loads the ordinary PC body. Every byte of
its prefix is explained; no playable level, synthetic room or native fit
is inferred from it. See the [source-format contract](formats/src-tr2-tr3.md).

The three strict-reject classes below retain their exact bytes in the
census projection; none is interpreted as valid sequel gameplay:

- **D** `ranges.next_animation`: RS §§6.2.4–5 describes an index into the
  local animation table; these targets are out of range. Some dispatch
  windows overlap their owning animation's frame interval, so they cannot
  all be dismissed as outside-frame padding. Why they are authored this
  way or whether an original controller requests them was not established.
- **B** `boxes.overlap`: RS §7.3.1 gives bits 0–13 as the overlap-list head.
  Some `0x07ff` heads do not address a valid chain. RS §3.2.3 describes
  2047 for **sector box indices**, not as an overlap-head sentinel.
  Other heads with the same value are in bounds: no general sentinel
  substitution was made.
- **F** `floor_data` action word `0xffff`: low 10-bit argument 1023, code
  `0x1f`, set end bit. RS §4.6 does not document this action. Only in
  census mode is it counted as one opaque ending word, not as a gameplay
  action. `ANTARC` also contains two unreferenced words 8142–8143; they
  are retained separately, not decoded using the preceding trigger.

### Strict finding locations

First exact decimal byte offset and number of fields for each class. The raw
JSON records **every** field offset, and the `†` marker denotes one or more
strict rejects.

| III file | D (dispatch) | B (box head) | F (action) |
|---|---|---|---|
| `ANTARC.TR2` | – | – | 2,775,762; 1 field |
| `AREA51.TR2` | 2,986,082; 5 fields | – | – |
| `COMPOUND.TR2` | 2,998,116; 24 fields | – | 2,765,182; 1 field |
| `cuts/CUT1.TR2` | – | 1,477,464; 1 field | – |
| `cuts/CUT11.TR2` | – | 1,357,365; 2 fields | – |
| `cuts/CUT2.TR2` | – | 1,885,687; 9 fields | – |
| `cuts/CUT3.TR2` | – | 1,923,233; 1 field | – |
| `cuts/CUT6.TR2` | – | 2,348,916; 2 fields | – |
| `cuts/CUT7.TR2` | 1,065,057; 9 fields | 1,205,903; 3 fields | – |
| `cuts/CUT8.TR2` | 644,206; 23 fields | – | – |
| `cuts/CUT9.TR2` | – | 1,728,341; 1 field | – |
| `HOUSE.TR2` | 2,972,987; 3 fields | – | – |
| `JUNGLE.TR2` | 2,858,399; 2 fields | – | – |
| `NEVADA.TR2` | 2,617,431; 9 fields | – | – |
| `OFFICE.TR2` | – | 2,457,172; 4 fields | – |
| `RAPIDS.TR2` | 2,625,196; 6 fields | – | – |
| `STPAUL.TR2` | – | 3,081,040; 2 fields | – |
| `TITLE.TR2` | 916,760; 1 field | – | – |
| `TONYBOSS.TR2` | 2,209,987; 5 fields | 2,572,855; 3 fields | – |
| `TRIBOSS.TR2` | 1,822,058; 6 fields | 2,233,634; 33 fields | – |

## Census

All counts in this section are **exact source counts** on the bounded
parse, including retained records on strict rejects. A quad counts as
one polygon/face, two triangle equivalents. Room totals include alternate
rooms, although the portal calculation does not draw both at once.
Mesh V/F counts use each physical offset once; raw JSON also has the
per-static-ID and per-movable-type slot/vertex/face counts with aliases
retained. Texture rectangles (R) are distinct `(page,x,y,w,h)` unpadded
*used* crops, including room sprites, sprite sequences and reachable
animated frames. Counted per level, not deduplicated across levels.
Camera records include sinks. Sound columns are distinct *referenced*
external ordinals and complete RIFF bytes, following sound-map entries
and their variations—not files played in a route. Whole directory sizes
(including slots not selected by that walk) are in the raw JSON.

### Exact game totals and floor opcodes

| Physical/census field | II | III |
|---|---|---|
| Rooms | 2,396 | 3,178 |
| Room vertices | 230,208 | 289,622 |
| Room triangle equivalents | 369,362 | 404,166 |
| Sectors | 161,985 | 181,663 |
| Draw portals | 7,301 | 9,087 |
| Lights | 2,375 | 3,881 |
| Static instances | 2,074 | 2,531 |
| Static definitions | 382 | 465 |
| Movable models | 1,403 | 2,495 |
| Physical meshes | 5,994 | 6,605 |
| Mesh pointer slots | 8,061 | 10,017 |
| Physical mesh vertices | 129,839 | 178,368 |
| Physical mesh faces | 121,489 | 189,053 |
| Physical mesh triangle equivalents | 207,117 | 304,570 |
| Animation records | 9,198 | 12,579 |
| Frame bytes | 10,558,216 | 14,220,280 |
| Placed items | 2,576 | 3,262 |
| Page pairs | 294 | 293 |
| Object texture descriptors | 34,527 | 51,190 |
| Sprite descriptors | 4,112 | 4,653 |
| Used distinct rectangles (level sums) | 24,260 | 28,757 |
| Room sprites | 5 | 0 |
| Sprite sequences | 598 | 166 |
| Floor words | 157,939 | 234,770 |
| Boxes | 16,431 | 20,009 |
| Overlap words | 103,112 | 114,462 |
| Camera/sink records | 180 | 372 |
| Sound sources | 74 | 209 |
| Distinct placed type IDs | 157 | 188 |
| Unique referenced external samples | 408 | 414 |
| Unique referenced RIFF bytes | 7,961,208 | 11,646,918 |

Retained source payload bytes (including structured extensions; rows are
not all disjoint). III excludes `VICT`, whose positive page counts and
full prefix bytes are accounted separately.

| Source extension bytes | II | III |
|---|---|---|
| `solid_colour_palette` | 24,576 | 33,792 |
| `argb1555_pages` | 38,535,168 | 38,404,096 |
| `vertex_legacy_lighting` | 460,416 | 579,244 |
| `vertex_attributes` | 460,416 | 579,244 |
| `vertex_rgb555` | 0 | 579,244 |
| `room_lighting_mode` | 4,792 | 0 |
| `room_ambient2` | 4,792 | 6,356 |
| `tr2_light_secondary` | 14,250 | 0 |
| `tr3_light_colour_type_union` | 0 | 46,572 |
| `static_instance_secondary` | 4,148 | 5,062 |
| `tr3_static_rgb555` | 0 | 5,062 |
| `tr3_water_reverb_filter` | 0 | 9,534 |
| `tr3_sector_material_box_words` | 0 | 363,326 |
| `textured_face_flags` | 0 | 17,812 |
| `flat_two_palette_words` | 17,160 | 63,066 |
| `additive_attributes` | 0 | 4,550 |
| `tile_flag_words` | 0 | 0 |
| `floor_program` | 315,878 | 469,540 |
| `extra_zone_words` | 131,448 | 160,072 |
| `packed_frames` | 10,558,216 | 14,220,280 |
| `item_secondary` | 5,152 | 6,524 |
| `cinematic_frames` | 206,352 | 470,640 |
| `demo_bytes` | 12,212 | 0 |
| `extra_sound_map_entries` | 5,472 | 7,524 |
| `tr3_sound_range_pitch` | 0 | 6,602 |
| `external_sample_ordinals` | 10,560 | 16,540 |

Occurrence histograms count physical reachable opcode words once per level,
not sector references. Names follow RS §§4.4–6. `opaque_actions` is separate.

| Domain | Code | Meaning | II | III |
|---|---|---|---|---|
| functions | `0x01` | lateral portal | 24,914 | 26,841 |
| functions | `0x02` | floor slant | 23,384 | 23,514 |
| functions | `0x03` | ceiling slant | 19,927 | 20,837 |
| functions | `0x04` | trigger | 4,911 | 8,203 |
| functions | `0x05` | kill Lara | 512 | 1,768 |
| functions | `0x06` | climb wall | 220 | 247 |
| functions | `0x07` | floor diag A | 0 | 7,103 |
| functions | `0x08` | floor diag B | 0 | 8,241 |
| functions | `0x09` | ceiling diag A | 0 | 6,268 |
| functions | `0x0a` | ceiling diag B | 0 | 7,255 |
| functions | `0x0b` | floor part A1 | 0 | 228 |
| functions | `0x0c` | floor part A2 | 0 | 242 |
| functions | `0x0d` | floor part B1 | 0 | 226 |
| functions | `0x0e` | floor part B2 | 0 | 224 |
| functions | `0x0f` | ceiling part A1 | 0 | 234 |
| functions | `0x10` | ceiling part A2 | 0 | 245 |
| functions | `0x11` | ceiling part B1 | 0 | 232 |
| functions | `0x12` | ceiling part B2 | 0 | 233 |
| functions | `0x13` | monkey-swing | 0 | 396 |
| functions | `0x14` | minecart left | 0 | 18 |
| functions | `0x15` | minecart right | 0 | 21 |
| actions | `0x00` | object | 5,438 | 4,866 |
| actions | `0x01` | fixed camera | 1,055 | 675 |
| actions | `0x02` | current/sink | 1,434 | 3,532 |
| actions | `0x03` | flip map | 45 | 85 |
| actions | `0x04` | flip on | 78 | 148 |
| actions | `0x05` | flip off | 62 | 164 |
| actions | `0x06` | look at item | 831 | 166 |
| actions | `0x07` | end level | 32 | 64 |
| actions | `0x08` | soundtrack | 362 | 739 |
| actions | `0x09` | flip effect | 232 | 384 |
| actions | `0x0a` | secret | 21 | 108 |
| actions | `0x0b` | clear bodies | 11 | 95 |
| trigger_types | `0x00` | normal | 3,720 | 7,199 |
| trigger_types | `0x01` | pad | 91 | 183 |
| trigger_types | `0x02` | switch | 191 | 260 |
| trigger_types | `0x03` | key | 55 | 55 |
| trigger_types | `0x04` | pickup | 9 | 15 |
| trigger_types | `0x05` | heavy | 680 | 35 |
| trigger_types | `0x06` | antipad | 3 | 0 |
| trigger_types | `0x07` | combat | 18 | 0 |
| trigger_types | `0x08` | dummy | 109 | 108 |
| trigger_types | `0x09` | antitrigger | 35 | 348 |
| opaque_actions | `0x1f` | opaque (not admitted) | 0 | 2 |

### Per-level source counts

**TR2**

| File | Rooms | Room V | Room tris | Max room / tris | Max room / V | Portals | Static / movable | Mesh V / F | Anim | Frame B | Items | Pages / R | Sprite D / Q / room | Floor words | Boxes / ovl | Cam / snd | Ref SFX / WAVE B |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `ASSAULT` | 99 | 10,418 | 16,190 | 50 / 948 | 50 / 544 | 388 | 47 / 36 | 3,737 / 3,496 | 284 | 251,986 | 40 | 14 / 854 | 174 / 17 / 5 | 7,175 | 562 / 4,511 | 1 / 1 | 118 / 1,835,132 |
| `BOAT` | 155 | 10,588 | 15,741 | 97 / 275 | 79 / 153 | 579 | 13 / 77 | 6,116 / 5,773 | 493 | 433,656 | 133 | 15 / 1,201 | 182 / 32 / 0 | 7,997 | 625 / 3,456 | 10 / 8 | 149 / 2,417,086 |
| `CATACOMB` | 129 | 15,021 | 23,570 | 113 / 636 | 113 / 398 | 407 | 9 / 70 | 5,750 / 5,332 | 474 | 391,810 | 170 | 13 / 1,027 | 182 / 31 / 0 | 8,159 | 901 / 6,095 | 9 / 5 | 132 / 2,173,526 |
| `CUT1` | 3 | 511 | 949 | 2 / 619 | 2 / 333 | 4 | 5 / 11 | 1,759 / 1,881 | 42 | 660,572 | 8 | 5 / 374 | 127 / 6 / 0 | 530 | 82 / 408 | 0 / 0 | 4 / 18,920 |
| `CUT2` | 2 | 425 | 698 | 1 / 538 | 1 / 331 | 0 | 10 / 12 | 2,996 / 3,255 | 98 | 690,598 | 8 | 6 / 503 | 121 / 5 / 0 | 1 | 2 / 0 | 0 / 0 | 4 / 18,920 |
| `CUT3` | 14 | 1,659 | 2,921 | 0 / 782 | 0 / 417 | 28 | 1 / 13 | 3,377 / 3,552 | 274 | 742,972 | 7 | 8 / 703 | 121 / 5 / 0 | 411 | 72 / 342 | 0 / 0 | 4 / 18,920 |
| `CUT4` | 8 | 1,452 | 2,480 | 1 / 624 | 1 / 324 | 14 | 2 / 11 | 2,870 / 3,011 | 61 | 752,598 | 8 | 5 / 432 | 127 / 6 / 0 | 771 | 74 / 394 | 0 / 0 | 4 / 18,920 |
| `DECK` | 116 | 14,285 | 23,214 | 61 / 849 | 61 / 520 | 386 | 15 / 71 | 6,398 / 5,897 | 485 | 441,368 | 105 | 14 / 1,249 | 198 / 34 / 0 | 13,937 | 1,329 / 7,546 | 6 / 9 | 125 / 1,955,618 |
| `EMPRTOMB` | 193 | 18,004 | 28,933 | 14 / 1,517 | 14 / 862 | 473 | 23 / 87 | 6,541 / 5,847 | 459 | 379,948 | 240 | 16 / 1,205 | 189 / 34 / 0 | 11,049 | 1,202 / 6,763 | 30 / 16 | 142 / 2,255,418 |
| `FLOATING` | 172 | 9,968 | 14,072 | 86 / 664 | 86 / 356 | 710 | 7 / 78 | 6,358 / 5,934 | 475 | 404,570 | 141 | 15 / 1,250 | 181 / 25 / 0 | 10,735 | 859 / 12,711 | 9 / 0 | 139 / 2,405,020 |
| `HOUSE` | 86 | 9,674 | 15,112 | 48 / 948 | 48 / 544 | 354 | 41 / 49 | 5,366 / 5,093 | 407 | 431,862 | 108 | 14 / 1,092 | 155 / 17 / 0 | 6,007 | 512 / 4,165 | 1 / 1 | 137 / 2,229,550 |
| `ICECAVE` | 151 | 14,470 | 22,570 | 34 / 710 | 138 / 404 | 507 | 9 / 76 | 5,782 / 5,448 | 456 | 400,814 | 158 | 14 / 1,103 | 189 / 33 / 0 | 8,650 | 946 / 5,879 | 6 / 1 | 138 / 2,344,818 |
| `KEEL` | 114 | 11,035 | 18,903 | 55 / 829 | 56 / 463 | 271 | 30 / 81 | 7,356 / 6,674 | 515 | 453,488 | 199 | 15 / 1,353 | 184 / 33 / 0 | 5,882 | 874 / 4,400 | 15 / 1 | 136 / 2,310,320 |
| `LIVING` | 82 | 9,936 | 17,009 | 37 / 998 | 37 / 537 | 207 | 22 / 69 | 6,454 / 5,902 | 472 | 399,678 | 92 | 13 / 1,221 | 182 / 31 / 0 | 5,927 | 903 / 5,380 | 10 / 0 | 128 / 2,105,640 |
| `MONASTRY` | 164 | 17,575 | 28,463 | 108 / 876 | 149 / 491 | 415 | 24 / 85 | 6,957 / 6,226 | 496 | 426,224 | 217 | 16 / 1,297 | 192 / 37 / 0 | 6,936 | 994 / 5,406 | 18 / 4 | 144 / 2,154,522 |
| `OPERA` | 193 | 11,930 | 17,614 | 136 / 528 | 122 / 290 | 587 | 25 / 82 | 6,877 / 6,237 | 513 | 430,858 | 216 | 15 / 1,315 | 177 / 31 / 0 | 6,332 | 678 / 4,064 | 7 / 2 | 155 / 2,428,052 |
| `PLATFORM` | 96 | 8,843 | 15,217 | 48 / 794 | 48 / 419 | 215 | 12 / 79 | 7,479 / 6,802 | 498 | 445,264 | 131 | 16 / 1,380 | 202 / 34 / 0 | 3,614 | 378 / 1,808 | 14 / 5 | 144 / 2,395,392 |
| `RIG` | 115 | 12,051 | 20,284 | 85 / 762 | 85 / 414 | 357 | 27 / 73 | 6,603 / 6,007 | 469 | 418,548 | 112 | 15 / 1,227 | 183 / 32 / 0 | 3,573 | 439 / 3,168 | 14 / 7 | 141 / 2,313,932 |
| `SKIDOO` | 149 | 17,097 | 27,997 | 99 / 1,252 | 99 / 676 | 393 | 9 / 66 | 6,149 / 5,722 | 445 | 359,632 | 108 | 12 / 1,038 | 183 / 32 / 0 | 19,302 | 1,999 / 10,111 | 5 / 3 | 143 / 2,361,300 |
| `TITLE` | 2 | 18 | 22 | 1 / 12 | 1 / 10 | 2 | 0 / 6 | 300 / 249 | 6 | 1,126 | 0 | 3 / 145 | 121 / 5 / 0 | 5 | 2 / 2 | 0 / 0 | 8 / 123,826 |
| `UNWATER` | 80 | 10,062 | 17,659 | 51 / 1,017 | 51 / 567 | 201 | 8 / 59 | 5,591 / 5,215 | 437 | 402,154 | 69 | 11 / 1,076 | 180 / 29 / 0 | 11,352 | 1,192 / 6,246 | 11 / 0 | 121 / 2,220,156 |
| `VENICE` | 166 | 13,094 | 19,606 | 22 / 452 | 22 / 275 | 505 | 34 / 77 | 6,334 / 5,812 | 496 | 443,442 | 134 | 16 / 1,268 | 185 / 32 / 0 | 5,730 | 557 / 4,048 | 6 / 10 | 152 / 2,332,672 |
| `WALL` | 84 | 7,533 | 11,793 | 65 / 619 | 36 / 335 | 236 | 9 / 68 | 5,525 / 5,139 | 389 | 358,916 | 106 | 11 / 956 | 183 / 32 / 0 | 7,443 | 1,078 / 5,104 | 6 / 1 | 137 / 2,261,748 |
| `XIAN` | 23 | 4,559 | 8,345 | 18 / 1,908 | 18 / 988 | 62 | 0 / 67 | 7,164 / 6,985 | 454 | 436,132 | 66 | 12 / 991 | 194 / 25 / 0 | 6,421 | 171 / 1,105 | 2 / 0 | 135 / 2,411,412 |

**TR3**

| File | Rooms | Room V | Room tris | Max room / tris | Max room / V | Portals | Static / movable | Mesh V / F | Anim | Frame B | Items | Pages / R | Sprite D / Q / room | Floor words | Boxes / ovl | Cam / snd | Ref SFX / WAVE B |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `ANTARC` † | 204 | 14,377 | 18,394 | 7 / 495 | 7 / 419 | 694 | 29 / 114 | 7,713 / 7,818 | 610 | 400,724 | 152 | 12 / 1,202 | 147 / 6 / 0 | 14,152 | 1,449 / 7,892 | 11 / 29 | 136 / 3,256,130 |
| `AREA51` † | 156 | 13,808 | 20,169 | 0 / 776 | 0 / 484 | 434 | 31 / 143 | 9,456 / 10,033 | 651 | 425,612 | 242 | 12 / 1,274 | 147 / 6 / 0 | 8,811 | 1,058 / 5,443 | 15 / 0 | 139 / 3,330,314 |
| `CHAMBER` | 44 | 4,458 | 5,506 | 5 / 1,108 | 5 / 752 | 105 | 9 / 106 | 6,982 / 7,826 | 526 | 434,462 | 51 | 12 / 1,108 | 148 / 7 / 0 | 4,163 | 448 / 3,424 | 2 / 1 | 127 / 3,133,158 |
| `CITY` | 233 | 18,284 | 27,854 | 45 / 1,213 | 45 / 680 | 597 | 20 / 115 | 7,347 / 7,556 | 537 | 383,506 | 202 | 12 / 1,215 | 147 / 6 / 0 | 11,812 | 1,260 / 7,312 | 13 / 22 | 138 / 3,448,476 |
| `COMPOUND` † | 186 | 15,308 | 23,139 | 19 / 797 | 160 / 533 | 475 | 21 / 128 | 8,901 / 9,174 | 643 | 451,482 | 241 | 12 / 1,226 | 147 / 6 / 0 | 6,576 | 866 / 3,672 | 21 / 0 | 140 / 3,294,302 |
| `CRASH` | 98 | 13,711 | 18,189 | 5 / 1,055 | 5 / 858 | 261 | 26 / 129 | 9,046 / 9,420 | 576 | 427,358 | 148 | 11 / 1,173 | 147 / 6 / 0 | 13,994 | 1,141 / 8,510 | 9 / 8 | 138 / 3,434,602 |
| `CUTS/CUT1` † | 12 | 1,767 | 1,992 | 9 / 757 | 10 / 564 | 42 | 1 / 7 | 1,047 / 1,320 | 33 | 415,894 | 2 | 5 / 363 | 110 / 1 / 0 | 1,282 | 124 / 1,087 | 0 / 0 | 108 / 2,554,946 |
| `CUTS/CUT11` † | 7 | 351 | 562 | 4 / 308 | 4 / 174 | 16 | 9 / 17 | 1,780 / 2,404 | 21 | 313,900 | 8 | 5 / 419 | 136 / 4 / 0 | 55 | 11 / 37 | 0 / 0 | 108 / 2,554,946 |
| `CUTS/CUT12` | 17 | 2,329 | 3,065 | 3 / 1,108 | 3 / 752 | 41 | 1 / 14 | 1,794 / 2,266 | 72 | 538,948 | 6 | 8 / 582 | 136 / 4 / 0 | 2,100 | 279 / 2,608 | 0 / 0 | 108 / 2,554,946 |
| `CUTS/CUT2` † | 35 | 4,036 | 3,415 | 0 / 466 | 34 / 410 | 132 | 1 / 12 | 1,149 / 1,519 | 28 | 560,868 | 4 | 6 / 465 | 136 / 4 / 0 | 2,801 | 88 / 362 | 2 / 0 | 108 / 2,554,946 |
| `CUTS/CUT3` † | 13 | 829 | 1,019 | 2 / 273 | 2 / 205 | 40 | 6 / 17 | 2,771 / 3,397 | 32 | 843,190 | 9 | 5 / 500 | 120 / 2 / 0 | 765 | 69 / 354 | 0 / 0 | 108 / 2,554,946 |
| `CUTS/CUT4` | 6 | 453 | 656 | 4 / 190 | 4 / 123 | 12 | 1 / 8 | 913 / 1,185 | 20 | 153,216 | 3 | 4 / 335 | 120 / 2 / 0 | 121 | 16 / 82 | 0 / 0 | 108 / 2,554,946 |
| `CUTS/CUT5` | 2 | 175 | 244 | 0 / 194 | 0 / 142 | 2 | 2 / 10 | 1,471 / 1,922 | 49 | 652,312 | 7 | 4 / 500 | 136 / 2 / 0 | 121 | 21 / 78 | 0 / 0 | 108 / 2,554,946 |
| `CUTS/CUT6` † | 6 | 1,077 | 1,756 | 1 / 796 | 1 / 434 | 14 | 6 / 15 | 1,508 / 1,820 | 19 | 900,174 | 7 | 7 / 447 | 136 / 4 / 0 | 811 | 88 / 472 | 0 / 0 | 108 / 2,554,946 |
| `CUTS/CUT7` † | 7 | 1,368 | 1,261 | 3 / 443 | 4 / 592 | 14 | 2 / 9 | 1,801 / 2,443 | 15 | 137,112 | 4 | 5 / 419 | 110 / 1 / 0 | 783 | 97 / 602 | 0 / 0 | 108 / 2,554,946 |
| `CUTS/CUT8` † | 2 | 464 | 854 | 1 / 748 | 1 / 400 | 2 | 2 / 13 | 1,665 / 2,161 | 14 | 249,578 | 5 | 3 / 455 | 136 / 4 / 0 | 61 | 36 / 204 | 0 / 0 | 108 / 2,554,946 |
| `CUTS/CUT9` † | 19 | 1,765 | 2,128 | 10 / 249 | 14 / 208 | 58 | 0 / 18 | 1,382 / 1,757 | 22 | 655,114 | 10 | 5 / 491 | 136 / 4 / 0 | 1,511 | 124 / 826 | 2 / 0 | 108 / 2,554,946 |
| `HOUSE` † | 133 | 17,148 | 22,132 | 86 / 1,199 | 86 / 978 | 472 | 50 / 62 | 6,245 / 6,516 | 504 | 352,006 | 98 | 12 / 1,107 | 161 / 7 / 0 | 11,028 | 947 / 6,284 | 2 / 3 | 142 / 3,265,174 |
| `JUNGLE` † | 165 | 18,369 | 26,255 | 4 / 792 | 4 / 439 | 539 | 17 / 95 | 6,878 / 6,892 | 509 | 370,432 | 142 | 11 / 1,188 | 147 / 6 / 0 | 16,306 | 1,396 / 7,198 | 13 / 10 | 128 / 3,123,190 |
| `MINES` | 199 | 13,626 | 21,234 | 160 / 1,131 | 160 / 617 | 477 | 19 / 115 | 7,565 / 7,692 | 624 | 435,336 | 186 | 11 / 1,167 | 147 / 6 / 0 | 14,519 | 1,239 / 5,584 | 22 / 6 | 138 / 3,423,228 |
| `NEVADA` † | 181 | 16,795 | 21,191 | 65 / 848 | 118 / 671 | 580 | 15 / 103 | 6,677 / 7,090 | 582 | 405,998 | 154 | 10 / 1,041 | 147 / 6 / 0 | 17,150 | 1,250 / 6,953 | 22 / 15 | 142 / 3,442,796 |
| `OFFICE` † | 20 | 4,201 | 4,733 | 8 / 542 | 17 / 499 | 42 | 11 / 90 | 6,127 / 6,899 | 479 | 390,688 | 44 | 9 / 874 | 147 / 6 / 0 | 1,974 | 142 / 1,167 | 3 / 2 | 123 / 3,177,098 |
| `QUADCHAS` | 177 | 17,278 | 24,837 | 127 / 410 | 128 / 295 | 500 | 12 / 101 | 6,437 / 6,388 | 571 | 403,196 | 117 | 10 / 1,203 | 147 / 6 / 0 | 13,072 | 1,047 / 7,494 | 24 / 32 | 128 / 3,032,782 |
| `RAPIDS` † | 146 | 16,485 | 23,244 | 90 / 1,059 | 53 / 726 | 446 | 12 / 102 | 6,484 / 6,713 | 580 | 405,766 | 135 | 10 / 909 | 147 / 6 / 0 | 23,289 | 1,389 / 7,074 | 81 / 36 | 136 / 3,384,320 |
| `ROOFS` | 190 | 18,657 | 22,036 | 0 / 627 | 0 / 421 | 593 | 15 / 112 | 7,080 / 7,348 | 574 | 415,068 | 217 | 12 / 1,074 | 147 / 6 / 0 | 13,709 | 736 / 4,126 | 28 / 7 | 137 / 3,434,674 |
| `SEWER` | 163 | 13,333 | 21,872 | 37 / 992 | 37 / 574 | 420 | 41 / 134 | 8,717 / 8,809 | 585 | 415,616 | 248 | 13 / 1,249 | 147 / 6 / 0 | 9,303 | 778 / 3,082 | 21 / 4 | 145 / 3,430,470 |
| `SHORE` | 198 | 17,469 | 23,042 | 2 / 914 | 2 / 796 | 673 | 22 / 112 | 7,135 / 7,414 | 557 | 382,414 | 178 | 11 / 1,001 | 147 / 6 / 0 | 19,478 | 1,544 / 10,402 | 13 / 11 | 139 / 3,235,480 |
| `STPAUL` † | 68 | 4,600 | 6,391 | 5 / 822 | 5 / 434 | 180 | 48 / 104 | 7,312 / 7,497 | 544 | 385,886 | 103 | 12 / 1,013 | 147 / 6 / 0 | 2,482 | 277 / 1,952 | 24 / 2 | 140 / 3,184,930 |
| `TEMPLE` | 224 | 17,798 | 26,792 | 211 / 1,256 | 211 / 668 | 607 | 8 / 111 | 8,552 / 8,624 | 549 | 394,508 | 229 | 10 / 1,278 | 147 / 6 / 0 | 10,040 | 996 / 5,313 | 11 / 10 | 133 / 3,290,998 |
| `TITLE` † | 2 | 18 | 8 | 0 / 8 | 1 / 10 | 2 | 0 / 68 | 5,047 / 4,749 | 418 | 323,850 | 0 | 4 / 530 | 139 / 6 / 0 | 5 | 2 / 2 | 0 / 0 | 108 / 2,554,946 |
| `TONYBOSS` † | 25 | 3,576 | 6,380 | 0 / 900 | 0 / 471 | 53 | 6 / 91 | 5,767 / 5,787 | 459 | 350,428 | 33 | 10 / 865 | 147 / 6 / 0 | 826 | 160 / 1,080 | 0 / 4 | 119 / 3,220,186 |
| `TOWER` | 210 | 13,076 | 19,724 | 69 / 1,299 | 69 / 749 | 505 | 17 / 119 | 9,078 / 9,712 | 626 | 447,648 | 212 | 12 / 1,254 | 147 / 6 / 0 | 8,875 | 792 / 3,452 | 30 / 7 | 135 / 3,420,844 |
| `TRIBOSS` † | 30 | 2,633 | 4,092 | 29 / 390 | 29 / 310 | 59 | 5 / 101 | 6,541 / 6,902 | 550 | 397,990 | 65 | 8 / 830 | 147 / 6 / 0 | 2,795 | 139 / 334 | 3 / 0 | 136 / 3,337,850 |

## Native budgets and streaming

Budgets are `include/tr64/pools.def` and [architecture §4.1](architecture.md#41-memory-4-mib--4096-kib):
level **1,507,328 B**, state **196,608 B**, audio **131,072 B**. The
**742 KiB** reserve scenario assumes the **608 KiB** resident image row
on `m9-coop-plat`. Count all native row fits as **estimates**, even where
an individual section was emitted exactly. They do not imply the current
engine will load a sequel pack.

- **Core estimate:** use the current `TR6C` table widths and align each
  `32 + stride × count` to 16 B, including both states' ten zone arrays
  and the full 370-entry sound map. Increase room stride by 4 B (II) or
  8 B (III) for ambient/mode or ambient/room tail. Full sequel lamps,
  static instances and items fit their current record *widths* if repacked
  (24/24/24 B); they do not have the same ABI, and this is not a proposal
  to overload existing fields. Raw compressed-axis frame words cost 2 B
  each and stay compressed; rebaking poses to a TR1 frame format isn't
  charged. The current `n64_core.encode()` was tried unchanged; III/II
  domains, validation and expanded zone/sound-map tables prevent a
  byte-exact Core result (first rejection per level in JSON).
- **TX64 full-resolution CI4:** `W` = native writer byte count, indexed
  PC source, not a claim of colour/alpha parity with retail's 16-bit
  renderer. `E` = writer rejected an odd cluster-local descriptor
  endpoint. Use **bounding rectangles of the writer's same-page overlap
  clusters**, not a sum of crop areas or an emitted atlas. Round each
  cluster and crop X start/end outward to even texels and keep the
  original vertical extent. Charge `width/2 × height` per padded
  cluster (including holes), one 40 B palette per cluster without
  deduplication, actual descriptor and flat-colour records, split lines
  from the existing TMEM tile planner, active animation words, and 8-B
  alignment. This is an allocation model, not a sprite UV/clamp design.
- **RG64/MG64:** `W` = emitted unchanged; `P` = exact bytes from the
  existing writer **on a sizing projection**, hence an estimate for the
  sequel. These are real serializations using the padded TX plan where
  necessary, but they do not implement III's coloured lighting or
  dynamic water. Each emitted object costs 32 B, a batch 32 B, a run 24 B, a
  face 8 B, and a vertex pair 32 B; add the 64-B section header. Writer
  includes vertex duplication, pair padding and TMEM geometry splits.
- **Level total:** align Core/TX/RG/MG separately to 16 B and sum. In
  `half`, replace **each level's aggregate CI4 image bytes** `I` with
  `ceil(I/4)`, leaving TX records/palettes/alignment and geometry unchanged.
  This is not per-cluster dimension rounding, a filtered texture cook,
  a new split/clamp count, or an FPS credit. The level-only reserve adds
  759,808 B at either scale. None of these estimates is a guaranteed
  upper bound on a sequel-correct design. Expansion
  uses another 4,194,304 B at full texture scale, allocating deficits to
  their own rows—not shrinking unrelated pools. No compression assumed
  in resident sizes; no CART pack-size or asset-quality claim.
- **State estimate:** current 32-bit C layout was checked with `cc -m32`
  on `game/internal.h`: 8,392 B fixed, 16/room, 8/sector, 76/item slot,
  8/model slot, 8/nav cell. Follow `game/game.c:235–238`: II/III use
  80-B items and 10-B models (projected widened type fields),
  `max(256,placed)` item slots, 265/374 raw type slots, nine nav cells
  per box, and two mutable bytes per box and camera. Charges of 4,352
  (drawables) and 6,144 (current single-player save workspace,
  `engine/save/game.c:27–36`, `engine/save/save.h:9`) remain. The 265
  II slots exclude `ASSAULT`'s sprite-only IDs 289–291, following TR1's
  policy of not installing out-of-domain sprite sequences as gameplay
  models (`game/game.c:344–353`). Renderer workspace is
  `align16(align8(13*rooms) + 12*(rooms+physical_meshes) + 2*placed + animation)`
  where active groups add `2*nObjDesc + 4*sum(1 + group_length)`
  (`engine/render/{bind_load,texture_load}.c`). Use the C allocation
  alignments. This is a single-player estimate; no new spawned-item,
  creature, effect, save, or co-op workspace demand is inferred.
  The fixed `roomMate`/`roomAlternate` arrays support only 139 rooms
  (`game/internal.h:756`, `game/world.c:32–49`); III reaches 233 and II
  reaches 193. The model does not size their replacement.
- **Sound:** `W` = exact emitted SN64 bytes (streamed PCM/data + metadata);
  `E` = current writer rejected a sequel ambient case, so use
  `align16(64+32*sample_slots)` plus each PCM payload aligned to 16 B
  and one metadata block plus `ceil(payload_aligned/4096)` per sample.
  This models the writer's lossless 8-BIT/16-BIT PCM selection, not
  guessed recompression. Estimate the **audio RAM row**, separately,
  as the current measured 127,584 B, plus metadata above 4,096,
  16 B per stream block beyond 1,024, and aligned `5*(emitters−64)`
  only when emitters exceed 64 (`engine/snd/snd.c`, `snd.h`). This
  keeps the existing voice windows and capped frequent-effect cache.
  The **4 KiB metadata sub-cap** rejects **16/19 II** and **19/21 III**
  map/training directories even though the model fits all in the total
  audio row. `TONYBOSS` separately fails the unchanged ambient policy.
  Only three II maps and one III map pass both the unchanged writer and
  sub-caps. Playback policies for sequel IDs/volumes/ranges remain open.

### Per-level native row costs, in bytes

**TR2**

| File | Core (E) | TX | TX images | RG | MG | Level (E) | Half (E) | State (E) | SN64 | Audio RAM (E) |
|---|---|---|---|---|---|---|---|---|---|---|
| `ASSAULT` | 418,160 | 507,512 E | 438,077 | 306,824 P | 129,248 P | 1,361,760 | 1,033,200 | 158,864 | 1,834,480 W | 127,584 |
| `BOAT` | 627,328 | 586,728 E | 479,914 | 305,408 P | 228,328 P | 1,747,808 | 1,387,872 | 161,808 | 2,416,224 W | 128,320 |
| `CATACOMB` | 602,336 | 501,736 E | 410,181 | 459,256 P | 206,016 P | 1,769,360 | 1,461,728 | 195,200 | 2,172,752 W | 127,776 |
| `CUT1` | 715,200 | 180,344 E | 144,486 | 17,624 P | 68,816 P | 982,000 | 873,632 | 51,232 | 18,944 W | 127,584 |
| `CUT2` | 758,288 | 242,496 E | 191,839 | 12,264 P | 112,744 P | 1,125,808 | 981,936 | 45,968 | 18,944 W | 127,584 |
| `CUT3` | 844,112 | 308,232 E | 239,734 | 52,360 P | 141,056 P | 1,345,776 | 1,165,968 | 59,136 | 18,944 W | 127,584 |
| `CUT4` | 801,120 | 183,792 E | 142,775 | 46,000 P | 114,896 P | 1,145,808 | 1,038,736 | 56,768 | 18,944 W | 127,584 |
| `DECK` | 687,504 | 547,224 E | 433,657 | 430,336 P | 226,688 P | 1,891,760 | 1,566,512 | 232,640 | 1,954,896 W | 127,584 |
| `EMPRTOMB` | 624,640 | 614,840 E | 510,320 | 555,384 P | 229,896 P | 2,024,784 | 1,642,048 | 222,416 | 2,254,528 W | 128,096 |
| `FLOATING` | 700,224 | 594,464 E | 484,346 | 299,896 P | 232,120 P | 1,826,720 | 1,463,472 | 248,048 | 2,404,224 W | 128,000 |
| `HOUSE` | 608,176 | 544,344 E | 449,398 | 286,432 P | 191,408 P | 1,630,368 | 1,293,312 | 152,528 | 2,228,752 W | 127,936 |
| `ICECAVE` | 613,536 | 547,144 E | 448,593 | 436,824 P | 208,488 P | 1,806,016 | 1,469,568 | 195,216 | 2,343,984 W | 127,968 |
| `KEEL` | 632,688 | 586,688 E | 464,235 | 349,248 P | 255,728 P | 1,824,352 | 1,476,176 | 167,184 | 2,309,488 W | 127,904 |
| `LIVING` | 575,216 | 527,720 E | 416,336 | 324,456 P | 227,032 P | 1,654,448 | 1,342,192 | 176,400 | 2,104,880 W | 127,648 |
| `MONASTRY` | 652,544 | 618,440 E | 512,552 | 551,320 P | 237,704 P | 2,060,032 | 1,675,616 | 205,200 | 2,153,696 W | 128,160 |
| `OPERA` | 638,112 | 602,504 E | 485,355 | 326,720 P | 248,704 P | 1,816,048 | 1,452,032 | 177,840 | 2,427,120 W | 128,512 |
| `PLATFORM` | 575,088 | 631,592 E | 503,155 | 277,856 P | 264,552 P | 1,749,104 | 1,371,744 | 121,856 | 2,394,560 W | 128,160 |
| `RIG` | 577,248 | 566,096 E | 452,656 | 366,968 P | 234,432 P | 1,744,752 | 1,405,264 | 139,424 | 2,313,088 W | 128,064 |
| `SKIDOO` | 670,752 | 469,320 E | 375,596 | 545,896 P | 221,752 P | 1,907,744 | 1,626,048 | 299,120 | 2,360,448 W | 128,128 |
| `TITLE` | 4,080 | 62,072 E | 50,523 | 640 P | 10,696 P | 77,504 | 39,616 | 42,560 | 123,824 W | 127,584 |
| `UNWATER` | 617,872 | 442,296 E | 343,240 | 320,256 P | 198,304 P | 1,578,736 | 1,321,312 | 217,968 | 2,219,440 W | 127,584 |
| `VENICE` | 647,536 | 618,472 E | 506,904 | 375,920 P | 233,152 P | 1,875,088 | 1,494,912 | 172,496 | 2,331,776 W | 128,416 |
| `WALL` | 535,392 | 436,424 E | 350,559 | 226,552 P | 212,152 P | 1,410,544 | 1,147,632 | 183,744 | 2,260,944 W | 127,936 |
| `XIAN` | 524,176 | 465,000 E | 369,898 | 165,616 P | 301,176 P | 1,455,984 | 1,178,560 | 78,320 | 2,410,656 W | 127,872 |

**TR3**

| File | Core (E) | TX | TX images | RG | MG | Level (E) | Half (E) | State (E) | SN64 | Audio RAM (E) |
|---|---|---|---|---|---|---|---|---|---|---|
| `ANTARC` † | 686,048 | 474,312 E | 361,303 | 426,512 P | 271,744 P | 1,858,624 | 1,587,648 | 244,432 | 3,255,408 W | 127,904 |
| `AREA51` † | 657,712 | 484,664 E | 361,545 | 400,904 P | 340,808 P | 1,884,112 | 1,612,960 | 203,616 | 3,329,520 W | 128,000 |
| `CHAMBER` | 544,688 | 488,504 E | 376,755 | 136,480 P | 261,824 P | 1,431,504 | 1,148,944 | 107,792 | 3,132,512 W | 127,616 |
| `CITY` | 656,896 | 485,624 E | 372,947 | 602,824 P | 265,920 P | 2,011,280 | 1,731,568 | 235,168 | 3,447,712 W | 127,968 |
| `COMPOUND` † | 670,928 | 475,520 E | 358,441 | 435,488 P | 317,088 P | 1,899,024 | 1,630,208 | 188,496 | 3,293,504 W | 128,032 |
| `CRASH` | 665,936 | 468,800 E | 354,718 | 425,376 P | 335,320 P | 1,895,440 | 1,629,408 | 214,112 | 3,433,808 W | 127,968 |
| `CUTS/CUT1` † | 486,880 | 168,152 W | 132,632 | 47,056 P | 45,240 W | 747,344 | 647,872 | 62,176 | 2,554,384 W | 127,584 |
| `CUTS/CUT11` † | 345,664 | 172,816 E | 130,131 | 10,712 P | 75,696 P | 604,896 | 507,312 | 46,576 | 2,554,384 W | 127,584 |
| `CUTS/CUT12` | 630,784 | 303,960 E | 246,387 | 78,864 P | 77,648 P | 1,091,264 | 906,480 | 80,160 | 2,554,384 W | 127,584 |
| `CUTS/CUT2` † | 660,384 | 238,536 E | 191,467 | 70,152 P | 54,104 P | 1,023,200 | 879,600 | 82,784 | 2,554,384 W | 127,584 |
| `CUTS/CUT3` † | 930,160 | 196,104 E | 144,492 | 24,392 P | 109,760 P | 1,260,432 | 1,152,064 | 53,936 | 2,554,384 W | 127,584 |
| `CUTS/CUT4` | 211,488 | 139,816 E | 107,336 | 12,536 P | 42,048 P | 405,904 | 325,408 | 47,424 | 2,554,384 W | 127,584 |
| `CUTS/CUT5` | 727,712 | 170,408 E | 117,656 | 5,176 P | 70,952 P | 974,272 | 886,032 | 46,384 | 2,554,384 W | 127,584 |
| `CUTS/CUT6` † | 972,784 | 249,768 E | 203,606 | 38,176 P | 62,272 P | 1,323,008 | 1,170,304 | 55,552 | 2,554,384 W | 127,584 |
| `CUTS/CUT7` † | 179,072 | 189,736 W | 150,615 | 28,296 P | 76,208 W | 473,328 | 360,368 | 61,104 | 2,554,384 W | 127,584 |
| `CUTS/CUT8` † | 283,936 | 129,504 E | 86,725 | 16,752 P | 79,200 P | 509,392 | 444,352 | 47,984 | 2,554,384 W | 127,584 |
| `CUTS/CUT9` † | 715,008 | 194,352 E | 152,171 | 49,840 P | 58,216 P | 1,017,424 | 903,296 | 65,520 | 2,554,384 W | 127,584 |
| `HOUSE` † | 600,272 | 467,392 E | 367,617 | 428,936 P | 218,336 P | 1,714,944 | 1,439,232 | 209,776 | 3,264,448 W | 128,096 |
| `JUNGLE` † | 653,472 | 442,296 E | 329,960 | 596,208 P | 243,968 P | 1,935,952 | 1,688,480 | 256,320 | 3,122,496 W | 127,648 |
| `MINES` | 692,976 | 450,640 E | 339,275 | 475,344 P | 268,568 P | 1,887,536 | 1,633,088 | 216,624 | 3,422,400 W | 127,968 |
| `NEVADA` † | 682,144 | 402,032 E | 300,185 | 485,600 P | 244,312 P | 1,814,096 | 1,588,960 | 237,840 | 3,441,952 W | 128,096 |
| `OFFICE` † | 486,208 | 349,968 E | 261,735 | 93,424 P | 227,760 P | 1,157,360 | 961,072 | 95,680 | 3,176,432 W | 127,584 |
| `QUADCHAS` | 648,688 | 400,496 E | 292,671 | 561,512 P | 231,936 P | 1,842,640 | 1,623,152 | 212,912 | 3,032,080 W | 127,648 |
| `RAPIDS` † | 687,456 | 383,288 E | 295,726 | 540,216 P | 228,208 P | 1,839,184 | 1,617,392 | 241,616 | 3,383,552 W | 127,904 |
| `ROOFS` | 696,912 | 487,248 E | 383,418 | 444,768 P | 260,128 P | 1,889,056 | 1,601,504 | 233,376 | 3,433,920 W | 127,936 |
| `SEWER` | 626,192 | 515,992 E | 399,735 | 409,424 P | 316,920 P | 1,868,544 | 1,568,736 | 167,712 | 3,429,616 W | 128,192 |
| `SHORE` | 686,048 | 453,960 E | 357,563 | 527,128 P | 252,376 P | 1,919,536 | 1,651,360 | 260,112 | 3,234,656 W | 128,000 |
| `STPAUL` † | 496,688 | 463,448 E | 363,583 | 127,096 P | 267,184 P | 1,354,432 | 1,081,744 | 97,200 | 3,184,160 W | 128,032 |
| `TEMPLE` | 641,424 | 418,000 E | 300,463 | 571,728 P | 304,800 P | 1,935,952 | 1,710,608 | 214,320 | 3,290,240 W | 127,808 |
| `TITLE` † | 358,416 | 163,792 E | 114,047 | 344 P | 165,040 P | 687,600 | 602,080 | 45,088 | 2,554,384 W | 127,584 |
| `TONYBOSS` † | 418,320 | 388,912 E | 306,491 | 124,816 P | 202,736 P | 1,134,784 | 904,928 | 74,832 | 3,219,536 E | 127,584 |
| `TOWER` | 662,624 | 520,728 E | 394,946 | 393,216 P | 337,632 P | 1,914,208 | 1,618,000 | 183,136 | 3,420,112 W | 127,872 |
| `TRIBOSS` † | 472,272 | 336,120 E | 255,107 | 76,592 P | 231,672 P | 1,116,672 | 925,344 | 71,968 | 3,337,056 W | 127,904 |

### Streaming and triangle bounds

`Keep` is the conservative resident immutable Core: collision world
(room/sector/portal/floor, static bounds/placement and mesh spheres), boxes,
overlaps/zones, frames/animations/commands/nodes, items, and remaining
non-draw metadata. Physical draw meshes are **not** needed for the assumed
Core sphere/box collision path; preserve any additional sequel collision
requirements when implementing it. `Draw` is all remaining level bytes
(TX64, RG64, MG64, and Core light/sprite tables plus row alignment).
Mutable `state` and audio are separate. Raw JSON breaks this down further
and lists each room's RG bytes and image-cluster union, and each object
ID's MG bytes and image union. Shared clusters/mesh offsets count once in
window sums. `Window` is the largest estimated content window (per-object
RG/MG records and CI4 image union for those rooms and authored instances);
it omits common headers, TX palettes/descriptors, in-flight staging,
transition overlap, spawned/held/menu meshes and cache policy. It is not
an estimate of I/O time or the complete streaming cache size.

`Rooms` and `Tsrc` are **static upper bounds** on authored room reachability
and authored room triangle equivalents, not frame submissions. The
projected graph folds alternate destinations into base rooms and selects
one active physical room per pair. `Raw rooms` and `Raw Tsrc` instead
follow `engine/render/render.c:101–135,229–236`: queue literal destination
IDs and, when flipped, read each queued ID's physical mate. Both walk
from every eligible start in each flip state, using draw portals only.
The actual renderer rejects back-facing, behind-camera and beyond-far
portals (far = 10,240), intersects with the parent clip, and culls batches.
Both `Tsrc` columns exclude sprites, instances, and cook-introduced
triangles. The projected `Tnative` column includes the native sizing
writer's TMEM splits, but still omits sprites and instances.
The JSON separately sums all authored static and placed meshes as if
drawn once per placement: projected maxima of **70,276** at `MONASTRY`
and **56,306** at `SEWER` when combined with room geometry, before any
model culling, and excluding transient meshes/sprites. The per-game peak
content windows are **1,021,207 B** (`EMPRTOMB`) and **947,728 B** (`CITY`).
Projected/raw authored maxima are **27,583 / 27,941** (II),
**26,128 / 26,575** (III), and **18,290 / 18,978** (I). The measured
**2,598 submitted** at TR1 LEVEL1's bridge ([architecture M8](architecture.md),
one-view mean in play) is a separate on-frame anchor with different work.
Do not infer frame time or
RDP/fill rate by dividing one by the other.

### Per-level static reachability and streaming bytes

**TR2**

| File | Collision | Nav | Anims | Items | Keep | Draw | Rooms | Tsrc | Raw rooms | Raw Tsrc | Tnative | Window room images | Window meshes | Window draw content |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `ASSAULT` | 109,856 | 31,616 | 270,432 | 992 | 415,472 | 946,288 | 99 | 16,190 | 99 | 16,190 | 16,190 | 218,128 | 92,960 | 756,846 |
| `BOAT` | 113,392 | 32,032 | 467,312 | 3,232 | 624,256 | 1,123,552 | 152 | 15,627 | 153 | 15,677 | 15,627 | 165,376 | 121,696 | 773,303 |
| `CATACOMB` | 119,296 | 48,352 | 423,680 | 4,112 | 599,072 | 1,170,288 | 119 | 20,734 | 120 | 21,270 | 20,734 | 137,728 | 95,176 | 774,626 |
| `CUT1` | 4,912 | 4,208 | 664,176 | 224 | 714,960 | 267,040 | 3 | 949 | 3 | 949 | 949 | 45,824 | 43,488 | 175,480 |
| `CUT2` | 4,528 | 192 | 696,752 | 224 | 758,112 | 367,696 | 1 | 538 | 1 | 538 | 538 | 14,464 | 98,528 | 270,928 |
| `CUT3` | 11,520 | 3,664 | 761,136 | 208 | 843,632 | 502,144 | 14 | 2,921 | 14 | 2,921 | 2,921 | 56,320 | 81,712 | 327,800 |
| `CUT4` | 11,472 | 3,872 | 758,544 | 224 | 800,736 | 345,072 | 8 | 2,480 | 8 | 2,480 | 2,480 | 60,928 | 90,696 | 250,232 |
| `DECK` | 136,400 | 68,384 | 473,392 | 2,560 | 684,304 | 1,207,456 | 112 | 22,782 | 113 | 22,934 | 22,782 | 105,984 | 116,136 | 831,909 |
| `EMPRTOMB` | 136,240 | 61,728 | 411,344 | 5,792 | 619,440 | 1,405,344 | 183 | 27,583 | 184 | 27,941 | 27,583 | 201,344 | 120,408 | 1,021,207 |
| `FLOATING` | 192,368 | 59,888 | 436,736 | 3,424 | 696,080 | 1,130,640 | 172 | 14,072 | 172 | 14,072 | 14,072 | 200,704 | 80,752 | 708,884 |
| `HOUSE` | 101,312 | 28,912 | 458,592 | 2,624 | 607,072 | 1,023,296 | 86 | 15,112 | 86 | 15,112 | 15,112 | 209,408 | 133,728 | 806,582 |
| `ICECAVE` | 120,912 | 49,712 | 431,584 | 3,824 | 609,568 | 1,196,448 | 136 | 20,849 | 137 | 21,127 | 20,849 | 145,408 | 94,240 | 799,382 |
| `KEEL` | 87,120 | 43,872 | 487,136 | 4,816 | 629,680 | 1,194,672 | 106 | 17,153 | 107 | 17,477 | 17,153 | 142,848 | 149,544 | 820,757 |
| `LIVING` | 89,552 | 46,992 | 430,528 | 2,240 | 572,752 | 1,081,696 | 70 | 13,380 | 71 | 14,378 | 13,380 | 124,416 | 119,456 | 662,897 |
| `MONASTRY` | 126,624 | 50,688 | 460,016 | 5,248 | 646,544 | 1,413,488 | 156 | 26,039 | 157 | 26,727 | 26,039 | 200,192 | 124,208 | 1,015,546 |
| `OPERA` | 123,024 | 35,360 | 465,728 | 5,216 | 633,344 | 1,182,704 | 184 | 17,176 | 185 | 17,296 | 17,176 | 168,448 | 139,888 | 818,632 |
| `PLATFORM` | 67,232 | 18,848 | 479,168 | 3,184 | 572,384 | 1,176,720 | 89 | 13,535 | 90 | 14,045 | 13,535 | 119,808 | 160,200 | 746,141 |
| `RIG` | 88,768 | 24,000 | 450,448 | 2,720 | 574,160 | 1,170,592 | 108 | 18,820 | 109 | 19,472 | 18,820 | 139,264 | 130,600 | 795,928 |
| `SKIDOO` | 164,144 | 100,288 | 390,928 | 2,624 | 666,848 | 1,240,896 | 145 | 26,068 | 146 | 27,032 | 26,068 | 110,592 | 105,568 | 843,205 |
| `TITLE` | 720 | 208 | 1,888 | 32 | 3,936 | 73,568 | 2 | 22 | 2 | 22 | 22 | 4,096 | 0 | 4,672 |
| `UNWATER` | 119,328 | 60,272 | 431,168 | 1,696 | 615,760 | 962,976 | 70 | 15,601 | 71 | 16,273 | 15,601 | 72,192 | 98,488 | 609,676 |
| `VENICE` | 127,584 | 30,496 | 477,312 | 3,248 | 642,736 | 1,232,352 | 152 | 16,096 | 153 | 16,548 | 16,096 | 173,568 | 126,648 | 807,400 |
| `WALL` | 86,704 | 53,440 | 386,816 | 2,576 | 533,072 | 877,472 | 84 | 11,793 | 84 | 11,793 | 11,793 | 96,256 | 101,280 | 540,098 |
| `XIAN` | 36,064 | 9,168 | 468,224 | 1,616 | 523,072 | 932,912 | 23 | 8,345 | 23 | 8,345 | 8,345 | 97,792 | 66,312 | 407,192 |

**TR3**

| File | Collision | Nav | Anims | Items | Keep | Draw | Rooms | Tsrc | Raw rooms | Raw Tsrc | Tnative | Window room images | Window meshes | Window draw content |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `ANTARC` † | 156,704 | 73,872 | 441,040 | 3,680 | 679,520 | 1,179,104 | 204 | 18,394 | 204 | 18,394 | 18,394 | 188,416 | 103,784 | 798,501 |
| `AREA51` † | 120,224 | 53,328 | 468,848 | 5,840 | 652,208 | 1,231,904 | 147 | 19,317 | 148 | 19,475 | 19,317 | 173,056 | 158,232 | 815,077 |
| `CHAMBER` | 42,144 | 24,864 | 470,352 | 1,264 | 541,936 | 889,568 | 39 | 5,196 | 40 | 5,266 | 5,196 | 202,496 | 82,768 | 489,320 |
| `CITY` | 151,248 | 65,120 | 420,160 | 4,880 | 645,440 | 1,365,840 | 224 | 26,128 | 225 | 26,575 | 26,128 | 205,824 | 104,816 | 947,728 |
| `COMPOUND` † | 117,536 | 42,096 | 494,336 | 5,824 | 663,744 | 1,235,280 | 171 | 21,244 | 172 | 21,514 | 21,244 | 167,424 | 148,696 | 814,009 |
| `CRASH` | 124,016 | 62,784 | 468,608 | 3,584 | 663,360 | 1,232,080 | 90 | 16,881 | 91 | 17,491 | 16,881 | 168,448 | 157,856 | 802,216 |
| `CUTS/CUT1` † | 14,784 | 7,232 | 419,008 | 80 | 486,624 | 260,720 | 12 | 1,992 | 12 | 1,992 | 1,992 | 72,192 | 17,984 | 151,728 |
| `CUTS/CUT11` † | 4,400 | 624 | 317,280 | 224 | 345,296 | 259,600 | 7 | 562 | 7 | 562 | 562 | 25,600 | 30,184 | 95,968 |
| `CUTS/CUT12` | 20,544 | 16,480 | 544,176 | 176 | 629,408 | 461,856 | 16 | 2,995 | 17 | 3,065 | 2,995 | 162,816 | 46,304 | 322,924 |
| `CUTS/CUT2` † | 45,248 | 4,352 | 564,064 | 128 | 659,504 | 363,696 | 34 | 2,971 | 35 | 3,415 | 2,971 | 96,256 | 21,408 | 201,600 |
| `CUTS/CUT3` † | 9,744 | 3,600 | 847,424 | 256 | 929,664 | 330,768 | 12 | 995 | 12 | 995 | 995 | 57,856 | 52,464 | 171,976 |
| `CUTS/CUT4` | 4,464 | 912 | 155,936 | 112 | 211,184 | 194,720 | 6 | 656 | 6 | 656 | 656 | 43,008 | 15,504 | 84,104 |
| `CUTS/CUT5` | 2,560 | 1,120 | 656,384 | 208 | 727,552 | 246,720 | 2 | 244 | 2 | 244 | 244 | 18,944 | 39,888 | 96,520 |
| `CUTS/CUT6` † | 9,088 | 4,560 | 903,184 | 208 | 972,528 | 350,480 | 6 | 1,756 | 6 | 1,756 | 1,756 | 109,056 | 33,112 | 215,035 |
| `CUTS/CUT7` † | 12,176 | 5,216 | 140,256 | 128 | 178,864 | 294,464 | 6 | 1,249 | 6 | 1,249 | 1,249 | 62,976 | 34,128 | 144,864 |
| `CUTS/CUT8` † | 2,928 | 1,952 | 253,184 | 160 | 283,568 | 225,824 | 2 | 854 | 2 | 854 | 854 | 26,624 | 46,856 | 107,212 |
| `CUTS/CUT9` † | 17,488 | 6,720 | 658,368 | 272 | 714,512 | 302,912 | 19 | 2,128 | 19 | 2,128 | 2,128 | 55,296 | 29,288 | 171,128 |
| `HOUSE` † | 143,072 | 50,560 | 384,704 | 2,384 | 596,320 | 1,118,624 | 133 | 22,132 | 133 | 22,132 | 22,132 | 166,912 | 161,296 | 879,717 |
| `JUNGLE` † | 165,952 | 70,336 | 405,728 | 3,440 | 649,152 | 1,286,800 | 160 | 25,423 | 161 | 25,868 | 25,423 | 173,056 | 63,040 | 864,268 |
| `MINES` | 137,600 | 60,832 | 476,640 | 4,496 | 683,488 | 1,204,048 | 181 | 19,766 | 183 | 20,047 | 19,766 | 179,840 | 94,168 | 787,340 |
| `NEVADA` † | 161,920 | 64,032 | 445,168 | 3,728 | 678,944 | 1,135,152 | 155 | 19,357 | 156 | 19,609 | 19,357 | 136,832 | 79,448 | 731,405 |
| `OFFICE` † | 49,728 | 8,128 | 423,136 | 1,088 | 485,200 | 672,160 | 19 | 4,361 | 20 | 4,733 | 4,361 | 86,528 | 54,432 | 276,892 |
| `QUADCHAS` | 138,752 | 56,976 | 442,208 | 2,848 | 645,184 | 1,197,456 | 177 | 24,837 | 177 | 24,837 | 24,837 | 130,048 | 64,008 | 812,447 |
| `RAPIDS` † | 161,456 | 69,840 | 444,496 | 3,280 | 684,336 | 1,154,848 | 137 | 21,430 | 138 | 22,067 | 21,430 | 154,624 | 64,504 | 761,844 |
| `ROOFS` | 190,464 | 37,792 | 453,584 | 5,248 | 691,200 | 1,197,856 | 167 | 17,844 | 168 | 18,292 | 17,844 | 216,576 | 87,704 | 727,764 |
| `SEWER` | 116,976 | 37,408 | 455,168 | 5,984 | 619,664 | 1,248,880 | 147 | 19,582 | 148 | 19,952 | 19,582 | 177,408 | 140,808 | 811,244 |
| `SHORE` | 170,992 | 82,672 | 420,512 | 4,304 | 682,400 | 1,237,136 | 195 | 22,661 | 196 | 22,854 | 22,661 | 186,368 | 90,536 | 853,532 |
| `STPAUL` † | 49,824 | 15,104 | 423,104 | 2,512 | 494,592 | 859,840 | 67 | 6,375 | 67 | 6,375 | 6,375 | 157,184 | 102,144 | 468,020 |
| `TEMPLE` | 143,792 | 50,576 | 433,376 | 5,536 | 637,312 | 1,298,640 | 211 | 24,719 | 212 | 25,135 | 24,719 | 131,072 | 126,624 | 852,728 |
| `TITLE` † | 2,688 | 208 | 352,640 | 32 | 358,272 | 329,328 | 2 | 8 | 2 | 8 | 8 | 2,048 | 0 | 2,328 |
| `TONYBOSS` † | 22,240 | 8,656 | 382,304 | 832 | 417,120 | 717,664 | 21 | 4,894 | 22 | 5,794 | 4,894 | 54,784 | 37,080 | 208,240 |
| `TOWER` | 121,296 | 38,688 | 489,504 | 5,120 | 658,928 | 1,255,280 | 200 | 18,807 | 201 | 19,019 | 18,807 | 192,512 | 156,304 | 813,468 |
| `TRIBOSS` † | 25,136 | 6,336 | 435,024 | 1,600 | 471,408 | 645,264 | 23 | 2,856 | 24 | 3,136 | 2,856 | 69,120 | 59,512 | 225,744 |

### Fit counts and worst cases

The scenario columns are **memory-row estimates** (level + state + audio
RAM), not validation gates. The map/training denominators exclude title,
cutscenes and `VICT`; “all bodies” includes title and cuts. `SN gate`
means the unchanged writer and its directory/block/emitter caps both fit,
not that retail sound semantics have been ported.

| Scope | N | Stock | Full + level reserve | Half | Half + level reserve | Split full | Split half | Expansion |
|---|---|---|---|---|---|---|---|---|
| TR2 map/training | 19 | 3 | 13 | 13 | 13 | 19 | 19 | 19 |
| TR2 all bodies | 24 | 8 | 18 | 18 | 18 | 24 | 24 | 24 |
| TR3 map/training | 21 | 5 | 8 | 5 | 8 | 21 | 21 | 21 |
| TR3 all bodies | 33 | 17 | 20 | 17 | 20 | 33 | 33 | 33 |

Individual rows and the unchanged sound sub-gate:

| Scope | Level stock | Full + level reserve | Level half | Half + level reserve | State fits | SN gate |
|---|---|---|---|---|---|---|
| TR2 map/training | 3 | 19 | 15 | 19 | 13 | 3 |
| TR2 all bodies | 8 | 24 | 20 | 24 | 18 | 8 |
| TR3 map/training | 5 | 21 | 6 | 21 | 8 | 1 |
| TR3 all bodies | 17 | 33 | 18 | 33 | 20 | 13 |

Worst maps by row (negative margin in the last column means under budget):

| Game | Row/scenario | Worst file | Cost B | Limit B | Over B |
|---|---|---|---|---|---|
| 2 | Full level | `MONASTRY` | 2,060,032 | 1,507,328 | +552,704 |
| 2 | Half-scale level | `MONASTRY` | 1,675,616 | 1,507,328 | +168,288 |
| 2 | Half-scale + reserve | `MONASTRY` | 1,675,616 | 2,267,136 | -591,520 |
| 2 | Game state | `SKIDOO` | 299,120 | 196,608 | +102,512 |
| 2 | Audio RAM | `OPERA` | 128,512 | 131,072 | -2,560 |
| 2 | SN64 metadata sub-cap | `OPERA` | 5,024 | 4,096 | +928 |
| 3 | Full level | `CITY` | 2,011,280 | 1,507,328 | +503,952 |
| 3 | Half-scale level | `CITY` | 1,731,568 | 1,507,328 | +224,240 |
| 3 | Half-scale + reserve | `CITY` | 1,731,568 | 2,267,136 | -535,568 |
| 3 | Game state | `SHORE` | 260,112 | 196,608 | +63,504 |
| 3 | Audio RAM | `SEWER` | 128,192 | 131,072 | -2,880 |
| 3 | SN64 metadata sub-cap | `SEWER` | 4,704 | 4,096 | +608 |

TR1 measured worst level: **1,440,528 B** (`LEVEL10B`; Core **452,368**,
TX **459,728**, RG **357,480**, MG **170,936**, architecture §4.1).
Survey §6 quotes a historical **178,544 B** state high-water with an
older save allocation. Applying the single-player formula above with
the 6,144-B save charge to 16 TR1 PC maps yields **176,816 B** (`LEVEL3A`,
71,640 B of nine nav arrays). TR1 audio boot was **127,584 B**;
none of the sequel audio-row estimates exceeds 128 KiB,
although many exceed the current 4 KiB metadata sub-cap.

**Recommendation to test [inference]:** split the 742 KiB reserve by
giving **540 KiB to level data** and **104 KiB to game state**, leaving
**98 KiB** unassigned. The peak full-scale deficits across the two games
are 552,704 B for level and 102,512 B for state (655,216 B combined).
Page-rounded, the proposed split gives rows of 2,060,288 and 303,104 B,
covering those modeled maxima at either scale. This is arithmetic,
not a port design or a claim that 98 KiB covers the remaining work.

## First three campaign maps: placed object IDs

Exclude optional training and cutscenes. II order: `WALL` (Great Wall),
`BOAT` (Venice), `VENICE` (Bartoli's Hideout); III India order: `JUNGLE`,
`TEMPLE` (Temple Ruins), `QUADCHAS` (River Ganges). Filenames/order follow
[the survey §6](specs/tr2-tr3-differences.md#6-budget-risk-before-a-sequel-cook).
Names cross-reference that survey's public slot table (§§3.2–3), not
controller equivalence. Numbers are exact authored placements; `–` means
none in the file, not unavailable at runtime. All other maps' raw ID
histograms are in the JSON.

**TR2**

| ID | Public slot / available OL label | WALL | BOAT | VENICE |
|---|---|---|---|---|
| 0 | Lara | 1 | 1 | 1 |
| 14 | Boat | – | 2 | 1 |
| 15 | Doberman | – | 5 | 7 |
| 16 | Masked goon 1 | – | 2 | 6 |
| 17 | Masked goon 2 | – | 3 | 4 |
| 18 | Masked goon 3 | – | 5 | 2 |
| 21 | Rat | – | 5 | 12 |
| 24 | Gondola | – | 5 | – |
| 32 | Stick-wielding goon 1 | – | 4 | 6 |
| 36 | Spider | 13 | – | – |
| 38 | Crow | 3 | – | – |
| 39 | Tiger / Snow leopard | 5 | – | – |
| 55 | Falling block | 14 | – | – |
| 59 | Teeth spikes / Glass shards | 14 | – | – |
| 60 | Rolling ball | 2 | – | – |
| 62 | Discgun | 14 | – | – |
| 67 | Pushable block 1 | 1 | – | 1 |
| 68 | Pushable block 2 | – | – | 1 |
| 72 | Breakable window 1 | – | 10 | 17 |
| 80 | Rolling spindle | 2 | – | – |
| 81 | Wall-mounted knife blade | 3 | – | 2 |
| 82 | Statue with knife blade | – | – | 3 |
| 85 | Spiky wall | 9 | – | – |
| 93 | Small wall switch | – | 3 | 1 |
| 102 | Zipline handle | 1 | – | – |
| 103 | Push-button switch | – | 5 | – |
| 104 | Wall switch | 1 | 4 | 7 |
| 105 | Underwater switch | – | 1 | 2 |
| 106 | Door 1 | 3 | 5 | 6 |
| 107 | Door 2 | 1 | 5 | 5 |
| 108 | Door 3 | – | 3 | 2 |
| 109 | Door 4 | – | 10 | 3 |
| 110 | Door 5 | – | 1 | 2 |
| 111 | Lifting door 1 | – | 1 | 2 |
| 112 | Lifting door 2 | – | 3 | – |
| 113 | Lifting door 3 | – | 2 | 3 |
| 114 | Trapdoor 1 | 1 | 2 | 1 |
| 115 | Trapdoor 2 | – | – | 1 |
| 137 | Automags | – | 1 | 1 |
| 138 | Uzi | – | – | 1 |
| 143 | Shotgun ammo | 1 | 3 | 3 |
| 144 | Auto-pistol ammo | 1 | 4 | 2 |
| 145 | Uzi ammo | – | 2 | 3 |
| 147 | M16 ammo | – | 2 | – |
| 148 | Grenadegun ammo | – | – | 2 |
| 149 | Small medipack | 1 | 4 | 4 |
| 150 | Large medipack | 1 | 2 | 2 |
| 151 | Flares | 1 | 2 | 1 |
| 190 | Secret 1 | 1 | 1 | 1 |
| 191 | Secret 2 | 1 | 1 | 1 |
| 192 | Secret 3 | 1 | 1 | 1 |
| 193 | Key 1 | 1 | 1 | 1 |
| 194 | Key 2 | 1 | 1 | 1 |
| 195 | Key 3 | – | 1 | – |
| 201 | Keyhole 1 | 1 | 1 | 1 |
| 202 | Keyhole 2 | 1 | 1 | – |
| 203 | Keyhole 3 | – | 1 | – |
| 214 | T-Rex | 2 | – | – |
| 215 | Singing birds | – | 2 | 4 |
| 216 | Bartoli’s Hideout clock | – | 1 | – |
| 222 | Aquatic Mine (Venice) | – | 6 | – |
| 227 | Detonator box | – | – | 1 |
| 243 | Camera target | 2 | 5 | – |
| 244 | Waterfall mist | – | 3 | – |
| 253 | Flame emitter | 1 | – | 6 |
| 259 | Helicopter | 1 | – | – |

**TR3**

| ID | Public slot / available OL label | JUNGLE | TEMPLE | QUADCHAS |
|---|---|---|---|---|
| 0 | Lara | 1 | 1 | 1 |
| 16 | Quadbike | – | – | 1 |
| 24 | Kill All Triggers | – | 2 | – |
| 28 | Tiger | 9 | – | – |
| 29 | Vulture | – | – | 7 |
| 69 | Cobra / Rattlesnake | – | 8 | 4 |
| 70 | Shiva | – | 4 | – |
| 71 | Monkey | 10 | 18 | 26 |
| 75 | AI Ambush | 3 | – | 1 |
| 76 | AI Patrol 1 | 4 | – | – |
| 77 | AI Modify | 1 | – | – |
| 79 | AI Patrol 2 | 2 | – | – |
| 87 | Teeth spikes / Barbed wire | 19 | 8 | 3 |
| 88 | Rolling ball / Barrel | 6 | 9 | – |
| 91 | Dart shooter | – | 5 | – |
| 97 | Pushable block 1 | 1 | 9 | – |
| 107 | Falling ceiling | – | 6 | – |
| 111 | Wall knife blade / Knife disk | – | 4 | – |
| 114 | Spiky wall | 1 | 7 | – |
| 116 | Spiky vertical wall / Tunnel borer | – | 5 | – |
| 127 | Zipline handle | 2 | – | – |
| 128 | Push-button switch | – | – | 2 |
| 129 | Wall switch | 11 | 17 | – |
| 130 | Underwater switch | – | 9 | – |
| 131 | Door 1 | 6 | 4 | 2 |
| 132 | Door 2 | – | 16 | 2 |
| 133 | Door 3 | – | 4 | 2 |
| 134 | Door 4 | – | 2 | – |
| 138 | Door 8 | 10 | – | – |
| 139 | Trapdoor 1 | 1 | 8 | 1 |
| 161 | Shotgun | 1 | 2 | 1 |
| 169 | Shotgun ammo | 7 | 5 | 4 |
| 170 | Desert Eagle ammo | 2 | 2 | 3 |
| 171 | Uzi ammo | 4 | 4 | 5 |
| 172 | Harpoons | 1 | – | 4 |
| 173 | MP5 ammo | 3 | 3 | 2 |
| 174 | Rockets | 1 | – | 1 |
| 175 | Grenades | 1 | 1 | – |
| 176 | Small medipack | 5 | 12 | 5 |
| 177 | Large medipack | 1 | 1 | 2 |
| 178 | Flares | 6 | 6 | 3 |
| 180 | Savegame crystal | 6 | 8 | 7 |
| 205 | Puzzle 1 | – | 1 | – |
| 206 | Puzzle 2 | – | 1 | – |
| 213 | Slot 1 empty | – | 1 | – |
| 214 | Slot 2 empty | – | 1 | – |
| 224 | Key 1 | – | 5 | 2 |
| 227 | Key 4 | 1 | – | – |
| 232 | Keyhole 1 | – | 5 | 2 |
| 235 | Keyhole 4 | 1 | – | – |
| 304 | Look At item | 8 | 4 | 1 |
| 305 | Waterfall mist | – | 4 | 7 |
| 331 | Alternate Fire | 6 | 2 | 7 |
| 333 | Fire 2 | – | 10 | 1 |
| 338 | Pirahnas | 1 | 3 | 8 |
| 349 | Animating 1 | – | 1 | – |
| 350 | Animating 2 | – | 1 | – |

## Limits and next measurement

No native sequel pack, emulator/cart or hardware run, PSX sequel, script,
cutscene execution, demo playback, asset filtering, music/FMV, pose
rebaking, gameplay reachability or rendering parity was checked. Potential
work outside these rows includes code, I/O, additional streamed graphics
buffers, save/game state and GPU queues. A portal-clipped camera trace
including alternate rooms and actual object visibility is the next useful
measurement for making a streaming-cache or frame-time decision.

Verification: `cook-tests`, `tex-tests`, and `geo-tests` passed (31, 18,
and 4 tests). The TR1 geometry test now probes the current combined arena
boundary, not the removed per-section pools. Recomputing TR1 independent
byte accounting for 43 source files and the paired invariants and
equivalence for 21 PC/PSX pairs reproduced the archived
`$TR64_WORK/evidence/m0/cook-m0/equivalence.json.zst` results exactly
apart from run metadata. The 58 sequel input size/hash pairs match the
inventory. The census run also checked the independent section ledger on
each input. Neither sequel sizes nor TR1 equivalence required a cart build.

## Five surprises against the survey

1. **Navigation can cost more than texture savings can fix.** TR2 `SKIDOO`
   has 1,999 boxes (143,928 B for nine nav arrays alone); III `SHORE` has
   1,544. Adding the reserve to `level` fits every map in that row, but
   does not change the game-state row.
2. **The odd-endpoint TX64 assumption breaks before colour fidelity.** It
   rejected all 24 II bodies and 31/33 III bodies. III also has fewer
   peak page pairs than II (13 vs 16) but more peak object descriptors
   (2,500 vs 2,022); atlas page count is a poor proxy for usable CI4.
   The independent review also found active groups with mismatched
   descriptor shape in **17/33 III** bodies and **0/24 II**. The sizing
   projection reports the member IDs in the JSON. Width, height, relative
   UVs, flags, corner map and TMEM split lines must match for the existing
   permutation to bind (`engine/render/texture_load.c:16–22,41–43`).
3. **II’s actual bank is 16-bit.** All 408 II WAVE records are mono PCM16
   at 11,025 Hz, not the 8-bit format described in RS §8.2; all 414 III
   records are PCM16 at 22,050 Hz. II `OPERA` needs a 5,024-B directory
   despite having a smaller streamed SN64 section than III’s largest.
4. **The RS layout is not the whole validity contract.** III retains
   93 out-of-domain dispatch targets, 61 invalid `0x07ff` heads and
   two opaque action words; `VICT` has only the texture prefix.
   II’s full set does not need any of those census exceptions.
5. **Graph visibility is much too broad to size a tight cache.** Worst
   projected sets reach 184 rooms in II and 224 in III; raw-index
   walks reach more authored triangles than either projected maximum.
   The largest single rooms are only 1,908 (`XIAN`) and 1,299 (`TOWER`)
   room triangles, respectively.
   Neither comparison predicts measured per-frame submissions.

**Answer.** Of 19 TR2 and 21 TR3 PC maps including training, the modeled
level/state/audio rows fit **3/19 and 5/21 at stock 4 MiB**; **13/19 and
8/21** with the reserve assigned only to level data at full scale;
**13/19 and 5/21** with half-scale textures alone; and **13/19 and 8/21**
with half scale plus that same level-only reserve. Splitting the reserve
between level data and state fits **19/19 and 21/21** at either texture
scale; the **Expansion Pak** with resized rows also fits **19/19 and
21/21** at full scale. These are row estimates, not proof of playable
sequels: they leave out code growth for sequel logic, new save-size
requirements, GPU and I/O growth, co-op, unresolved texture and sound
format limits, and III's data irregularities.
