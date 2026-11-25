# Provenance

tr64 is not a clean-room project: Tomb Raider's data formats are publicly
documented and OpenLara is open source. These rules keep the licences
straight and the retail data out of the repository.

## 1. What may be used

| Source | Where | Use |
|---|---|---|
| OpenLara fixed-point game, `src/fixed/*.h` (BSD-2-Clause, Timur Gagiev) | `$OPENLARA_SOURCE`, branch `preview` | Ported into `game/`. Built unmodified apart from a trace exporter as the oracle in `test/oracle`. Its format readers may be consulted for `tools/cook`. |
| OpenLara floating-point game, `src/*.h` outside `src/fixed` and outside `src/platform` (BSD-2-Clause, same author) | same tree | Reference and source for game behaviour the fixed-point game leaves out (objects without a controller, ignored effects). Code ported from it into `game/` or the oracle patch carries the same BSD-2 notice naming the file and commit. Not a source for `engine/` or `plat/`. |
| Behaviour specifications written behind the wall (section 2a) | `docs/specs/wall/` | Read by implementers as the statement of what the original game does. |
| OpenLara PlayStation level reader, `tools/n64_magma/psx_level.[ch]` (same tree, same author as this project) | same | Reference for `tools/cook/src_psx`. |
| hl64 and ac3n64 (MIT, same author as this project) | `~/src/ports/hl64`, `~/src/ports/ac3n64` | Skeleton, harness, pacing, memory and I/O patterns may be ported. Name the source revision in the file header. |
| OpenLara host tests of the fixed-point game, `tools/n64_magma/test_*.cpp`, `run_fixed_hazard_tests.py` and their build lines | same | `test/oracle` only: how `src/fixed` is compiled and stubbed for the host. |
| OpenLara's other platform renderers and packers for the fixed-point game (`src/platform/{gba,3do,32x,dos}`, `src/format.h`, `src/fixed/fmt`) | same | Cited in `docs/specs` and used by `test/ref` as evidence of how the game's data is meant to be drawn (lighting, tint, projection). Facts only: no code from them in `engine/` or `plat/`. |
| Public console hardware documentation (PlayStation GPU, Nintendo 64 RDP/RSP) | network | Facts about how the hardware combines colours and samples textures. |
| Source of open hardware reimplementations of the console (the MiSTer N64 core, `MiSTer-devel/N64_MiSTer`) | network; read in scratch, not kept | Facts about how that platform's CPU, TLB and caches behave, cited by file, line and commit in `docs/hw/`. No code from it in this repository. |
| libdragon fork (branch `tlbcode`) and tiny3d | `$TR64_LIBDRAGON_SRC`, SDK prefix | Linked. Not copied into this repository. |
| Public format and engine documentation (TRosettaStone 3 and similar published write-ups of the Tomb Raider I to III level formats, floor data, trigger functions, object and state identifiers, script file and audio containers) | network | Facts about file layouts and data-defined behaviour, cited by section. The basis, with OpenLara and the level data, for what differs between Tomb Raider I, II and III. Published prose and tables only: a document that reproduces decompiled source is treated as that source (section 2). |
| The player's Tomb Raider data | `$TR64_DATA/{pc,psx}` (read-only) | Input to the cook and the oracle. Never committed. |

## 2. What may not be used

- Source code of other Tomb Raider engines and decompilations under
  copyleft or unknown licences: `TR1X`, `TRX`, `Tomb1Main`, `TombATI`,
  `tomb1-decomp`, `TR1-PSX-decomp`, `TR2Main`, `TR2X`, `Tomb2Main`,
  `tomb3` and other Tomb Raider II and III decompilations, and forks of
  them. Implementers, Main and reviewers of code do not open, search for
  or download them. They may be read only by a wall reader under section
  2a (user's direction, 2026-11-18; extended to II and III 2026-11-21).
- The retail executables (`tomb.exe`, `Tomb2.exe`, `tomb3.exe`,
  `SLUS_001.52` and other titles' and regions'): no disassembly, no
  decompilation, no extraction of tables from them by implementers,
  Main or reviewers. They may be analysed only by a wall reader under
  section 2a (user's direction, 2026-11-21: with Ghidra and the `re`
  skill, for the specific features a specification needs).
- The earlier OpenLara N64 ports as a design or code source for `engine/`,
  `plat/` and native formats: `src/platform/n64*`, `n64_magma/`,
  `tools/n64_magma/` other than `psx_level.[ch]`, and the branches
  `n64`, `n64-heap4mb`, `n64-stream-plan`. The engine is new; reading them
  for a measured fact about the source data is allowed and is noted in the
  commit message.
- OpenLara code outside `src/fixed` in `engine/` or `plat/`.

## 2a. The wall

`Tomb1Main` and `TRX` (GPL) describe the original game's behaviour more
completely than OpenLara. They are used only through a wall:

- A **reader** is an agent started for that purpose. It reads the source
  where it lives (`<remote-ssh>:~/src/Tomb1Main`; it is never copied to
  this host or into any repository or scratch directory here) and writes
  a behaviour specification in `docs/specs/wall/<topic>.md`.
  For Tomb Raider II and III the reader also reads, in place on the same
  host, the sources the user keeps there (`~/src/TR2Main`, the Tomb
  Raider II part of `~/src/Tomb1Main` if that checkout has one, and a
  Tomb Raider III source when the user provides one). Its
  specifications for those games are **difference specifications**
  (`docs/specs/wall/tr2-<topic>.md`, `tr3-<topic>.md`): what the later
  game does differently from the one before it, subsystem by subsystem,
  each difference stated against the Tomb Raider I behaviour this
  project already implements, and answers to numbered questions put by
  the implementers' side. What does not differ is said to be unchanged
  and is not transcribed.
  Where the sources do not establish a rule, a reader may establish it
  from the game's own retail executable (the user's licensed copy) by
  reverse engineering with Ghidra, following the `re` skill's process
  (a knowledge base of findings, labels and dead ends; one feature at a
  time). That work lives in a wall directory outside this repository
  and its scratch (`~/re/tr-wall/`: executables, analysis
  databases, labels, notes, listings); nothing in it is committed here
  or opened by implementers, Main or reviewers. Only a specification in
  the reader's own prose crosses, under the same rules as below; it
  names the executable by release and hash in its header and marks each
  rule as established or inferred. Addresses, listings, decompiler
  output and recovered identifiers stay in the wall directory.
  Where the user's host has no source for a game, a reader may fetch
  one itself (user's direction, 2026-11-21): cloned on `<remote-host>` beside
  the others and read in place there, under the same rules; the
  specification's header names the project and commit.
- A reader may also **run** the retail game (user's direction,
  2026-11-21): the user's licensed executable under Wine on `<remote-host>`,
  with an instrument the reader builds and keeps in the wall directory,
  to record what the game does for scripted inputs. This is observation
  of behaviour, and two things cross the wall from it. First, a
  specification of the record in prose (`docs/specs/wall/<game>-oracle-trace.md`:
  which facts of the game's state are recorded at each game tick, in
  what units, under names this project chooses), checked like any other
  specification. Second, the records themselves: numbers observed while
  the game ran (positions, angles, state, animation and frame numbers,
  health, timers, the flags of level items, the inputs fed), written in
  that format to `$TR64_WORK/fixtures/oracle-wall/` with their
  hashes, the executable's hash and the script that produced them.
  A record contains no code, no program addresses, no recovered
  identifiers and no copy of the executable's tables beyond the state
  the running game exposed tick by tick; it is not committed. The
  instrument, the addresses it reads and how it finds them stay in the
  wall directory. A retail record verifies the port; it does not
  replace a specification (an implementer is not given a record to
  derive a rule from without the rule being written down by a reader).
- A specification states behaviour: what an object does, in which
  states, on which conditions, with which numbers (distances, timers,
  damage, animation and state ids are facts of the game data and its
  rules). It is written in the reader's own words as prose and tables.
  It contains no source code, no pseudo-code that follows the source
  line by line, no comments or identifiers copied from the source beyond
  the public names of game objects, and no description of how the source
  is organised. Its header names the project, commit, the files read,
  the reader and the date.
- A reader never writes or edits anything else in this repository: no
  `game/`, `engine/`, `plat/`, `tools/`, `test/` or oracle patch, in that
  session or a later one. A reader's transcript is not given to an
  implementer.
- An **implementer** works from the specification, OpenLara, the level
  data and the oracle. It asks Main for a specification or a
  clarification; the question goes to a reader and the answer comes back
  as an amendment to the specification, never as a message with code.
- Main and code reviewers stay on the implementers' side. A separate
  read-only check of each specification (does it contain code or
  source structure) is made by an agent that has not seen the source.
- The register at the end of this section lists each specification,
  its reader, the source commit and the implementer who used it.

| Specification | Reader | Source commit | Used by |
|---|---|---|---|
| `docs/specs/wall/drawbridge.md` | WallReader | 3b2664b | Gaps (drawbridge in the oracle patch and `game/`) |
| `docs/specs/wall/objects-missing.md` | WallReader | 3b2664b | Gaps (audit `docs/game-gaps.md`; cabin, section 18); Gaps2 (falling ceiling, 12; burning and lava, 15.2, 15.3; lava emitter and particle, 16.1, 16.2); Gaps3 (rolling block, 2.1 and 3; moving bar, 6; Thor's hammer, 12; lightning, 13); Gaps4 (falling ceiling 7.2, bar 6) |
| `docs/specs/wall/triggers-and-effects.md` | WallReader | 3b2664b | Gaps (audit); Gaps2 (section 2.1); Gaps4 (section 4, object action: items that start invisible) |
| `docs/specs/wall/lara-and-enemies.md`, `glossary.md` | WallReader | 3b2664b | Gaps (audit); Gaps2 (section 1.4) |
| `docs/specs/wall/tr2-delta-map.md`, `tr2-first-levels.md`, `tr23-sources.md` | Wall23 | as named in each header (a partial Tomb Raider II source; rules it does not establish are marked so) | none yet |
| `docs/specs/wall/tr2-triggers-and-interaction.md`, `tr2-climbing.md`, `tr2-great-wall-objects.md`, `tr2-great-wall-creatures.md`, `tr2-great-wall-level.md`, `tr2-water-and-flares.md` | WallRE2 | the retail `Tomb2.exe` named by size and SHA-256 in each header (Steam appid 225300), by static analysis; no game launch | none yet |
| `docs/specs/wall/tr2-lara-ground-and-weapons.md`, `tr2-venice-objects.md`, `tr2-venice-creatures.md`, `tr2-venice-level.md` | WallRE2 | the same retail `Tomb2.exe`, by static analysis; no game launch | none yet |
| `docs/specs/wall/tr2-oracle-trace.md` (the record format, schema 20) and the records under `$TR64_WORK/fixtures/oracle-wall/tr2/` (thirteen scripts in The Great Wall, not committed) | WallRE2 | the same retail `Tomb2.exe`, RUN under Wine 11.22 on `<remote-host>` with the reader's instrument; 45 launches, each script byte-identical over 3 or 5 | T2Lara (verification only: `tools/tr2-compare.py`, the schema-20 writer in `game/trace.c`) |
| `docs/specs/wall/tr2-lara-start.md`, `tr2-lara-slide-height-and-gaze.md` (answers to implementer questions Q-T2L-01 to 06) | WallRE2 | the same retail `Tomb2.exe`, static analysis checked against the records; each reproduced from its text and `WALL.TR2` by WallCheck23 before merging | T2Lara (`game/tr2.h` rule table and the rules it lists; also `tr2-lara-ground-and-weapons.md`, `tr2-triggers-and-interaction.md` for the cited ground, ledge, reversal and look-subject rules) |
| `docs/specs/wall/tr2-camera.md`, `tr2-items-general.md`, `tr2-lara-arms.md` (new), and revisions of `tr2-lara-ground-and-weapons.md`, `tr2-climbing.md`, `tr2-water-and-flares.md`, `tr2-lara-slide-height-and-gaze.md` (answers to Q-T2C-01 to 12, Q-T2I-01 to 03, Q-T2A-01 to 05, Q-W-01 to 08, Q-T2M-01 and 02, Q-T2W-02 and 06) | WallRE2 | the same retail `Tomb2.exe`, static analysis checked against the records; each reproduced from its text and `WALL.TR2` against the records by WallCheck23 before merging | T2Items (level items, world and inventory; arms: pistols and flare; water), T2Lara and T2Camera (camera, wet camera; ledges, the ladder, stops and stance recovery): each rule listed with its citation in `game/tr2.h` |
| The records of thirty scripts under `$TR64_WORK/fixtures/oracle-wall/tr2/` (schema 20; fresh starts and starts placed by an optional first input line; not committed) | WallRE2 | the same retail `Tomb2.exe`, RUN under Wine on `<remote-host>`; each script byte-identical over 3 or 5 launches (the reader's count) | T2Items, T2Lara, T2Camera and their reviewers (verification only: `tools/tr2-compare.py` with the pinned producer and `tools/tr2-producer-profiles.json`) |
| `docs/specs/wall/tr2-bartoli-and-weapons.md` (pushable blocks 67–70, trapdoors 114–115, detonator box 227, statue blade 82, Auto Pistols / Uzis / Grenade ammo, Bartoli's Hideout script metadata, campfire ignition & flame-on-Lara, floor trigger ops 8/9/10) | WallRE3 | the same retail `Tomb2.exe` plus `venice.TR2` and `TOMBPC.DAT` (SHA-256 in header), by static analysis; checked in two passes by WallCheck3 on 2026-11-24 (0 code/identifier leaks; 7 behavioural rewordings applied) | T2Level3Bartoli |
| `docs/specs/wall/tr2-death-save-and-finish.md` (Title/front-end & `TOMBPC.DAT` campaign script, death/retry & Passport modes, level-end statistics & inter-level carry, and behavioural save/load persistence contract) | WallRE2 | the same retail `Tomb2.exe` plus `TITLE.tr2` and `TOMBPC.DAT` (SHA-256 in header), by static analysis; checked in two passes by WallCheck4 on 2026-11-24 (0 code/identifier leaks; 7 behavioural rewordings + 2 cross-spec fixes applied) | T2FlowSave |
| `docs/specs/wall/tr2-opera-house.md` (Puzzles 174–189, Clear Bodies op 11, Drawbridge 63, Elevator 65, Swinging Sandbag/Crate 58/96, Falling Sandbag 79, Spinning Fans 76/95, Multi-Boulder 83, Opera Boss 20, Grenade Launcher 141/7/163/148/248/229, Opera House `TOMBPC.DAT` metadata) | WallRE2 | the same retail `Tomb2.exe` plus `OPERA.TR2` and `TOMBPC.DAT` (SHA-256 in header), by static analysis; checked in one pass by WallCheck5 on 2026-11-24 (0 violations) | T2OperaHouse |
| `docs/specs/wall/tr3-jungle-and-movement.md` (TR3 floor split-triangles 0x07–0x12, monkey-swing 0x13, quicksand 0x80, Lara 313-clip movement delta [sprint/crouch/crawl/monkey/swamp/poison/cold], `JUNGLE.TR2` 95 models / 142 items [Monkey 71, AI waypoints 74–82, Tiger 28, Piranhas 338, Save/Health Crystal 180, traps/weapons], India `TOMBPC.DAT` sequence) | WallReaderTR3 | in-place `TRX` (`cf5bbaaa8eff9a29c036896b6a7975c58cae635a`) cross-checked against retail `JUNGLE.TR2`, `TEMPLE.TR2`, `QUADCHAS.TR2`, `TONYBOSS.TR2`, `HOUSE.TR2`, and `TOMBPC.DAT` (SHA-256 in header); checked in two passes by WallCheck6 on 2026-11-24 (0 code/identifier leaks; 11 wording/boundary/retail-data revisions verified) | TR3Jungle |

Checked from the implementers' side by WallCheck on 2026-11-18 (no source
code, names in headers only; code-shaped passages listed and reworded by
the reader in 02ef65b, 41abe0b, 4ccf7e6, 0dcf95a).

The Tomb Raider II documents were checked from the implementers' side by
WallCheck23 on 2026-11-21 (no source code or source identifiers; sixteen
passages describing source structure or written as sequences of steps,
and six statements stronger than what was established, reworded by the
reader in 6170fdf, a050a2c, 77922ea, 691aee6, 8a56936).

The six executable-derived documents were checked from the implementers'
side by WallCheck23 on 2026-11-21 in four passes (no code, decompiler
output, program addresses or recovered identifiers at any pass; 13, 17,
12 and 1 passages reworded by the reader: code-path wording, evidence
labels claiming more than their cross-check, ambiguous conditions; where
they supersede the earlier Tomb Raider II documents they say so). Where
two documents disagree, the one whose claim is marked established from
the executable holds.

The second batch (ground movement and firearms, the three Venice
documents, and revisions to the climbing and Great Wall route documents)
was checked by WallCheck23 on 2026-11-21 in two passes (no code,
addresses or recovered identifiers; 22 then 7 passages revised by the
reader, by then about ambiguous boundaries and evidence labels, not
form); Main read the final seven changes before merging.

The trace specification and the twelve records were checked by
WallCheck23 on 2026-11-21 in two passes (no addresses, recovered
identifiers or instrument detail; address-like values occur only in the
two random-number fields; every record parses with the documented field
order and a valid hash; Lara's start and every active item's slot,
type, first position, room, animation and frame reconcile with
`WALL.TR2` read by the cook; both startup world digests rebuilt from the
specification's definitions and the level file match the record). The
checker did not launch the game: the repeat counts are the reader's.
Main read the final one-row change before merging.

## 3. Licences in this repository

- The SPDX line is required in source files: C, headers, `.def`, `.S`,
  `.ld`, Python, shell, makefiles. It is the first line, or the second
  after a `#!` line. Exempt: Markdown, licence texts, `.gitignore`, and
  data files with no comment syntax (JSON, routes, scenes, patches).
- Everything outside `game/` and `test/oracle/` is MIT:
  `SPDX-License-Identifier: MIT` in a comment.
- `game/` and code in `test/oracle/` derived from OpenLara are
  BSD-2-Clause: first line `SPDX-License-Identifier: BSD-2-Clause`, and a
  second comment line `Derived from OpenLara (c) 2018 Timur "XProger"
  Gagiev; see game/LICENSE-OPENLARA.` naming the OpenLara file and commit
  it was ported from.
- A file in `tools/cook` that takes code (not just layout facts) from
  OpenLara's readers carries the BSD-2-Clause header in the same form.

## 4. Never committed

Anything that came off a disc or was derived from it: level files, cooked
packs, textures, sounds, video, screenshots of game content, oracle
traces, ROMs, ELFs. They live under `$TR64_WORK/` only. Test inputs
in the repository are synthetic (hand-written or generated by a committed
script).

## 5. The repository check

`make provcheck` must pass before every commit. It checks the index and
the tracked working files for: a missing or wrong SPDX line; a `game/`
file without the OpenLara notice; a path or file type from section 4; a
file larger than 256 KiB; the project names in the first bullet of section
2 (other engines and decompilations) anywhere outside this file,
`AGENTS.md` and the header lines of `docs/specs/wall/*.md` (the lines from
the top of the file up to the first blank line or the first `#` heading
after the title line; the body is checked like any other file). Paths inside
the OpenLara tree are not forbidden names: a permitted reference is cited
by its full path and commit.

## 6. Incidents

An accidental breach of section 2 or 4 is reported to Main at once and
recorded in `docs/provenance-incidents.md` with what was seen and what was
done about it.
