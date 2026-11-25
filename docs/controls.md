# Playable demo controls

Use controller port 1. The shipping demo boots to the **title screen** (the
source's title ring over the cooked title picture) and runs the fixed game at 30
simulation ticks per second. No Expansion Pak is required or used. The ROM
includes the title, the gym, the fifteen campaign levels and the four in-engine
cutscenes (21 entries of the cooked pack) from the player's cooked data, and
keeps three game saves plus settings in the cartridge's 32 KiB SRAM. The inventory, passport, options, statistics and death pages are the fixed
game's own; the engine only draws them.

| N64 input | Game action |
|---|---|
| Stick up/down or D-pad up/down | Forward/backward |
| Stick left/right or D-pad left/right | Turn left/right |
| A | Jump |
| B | Action: grab, interact, fire when weapons are drawn |
| Z | Draw/holster weapons |
| R | Walk (hold with movement) |
| C-left / C-right | Sidestep left/right (walk + direction) |
| C-up | Look (hold; use movement directions) |
| C-down | Roll (simultaneous forward + backward) |
| START | Inventory ring (source `IN_SELECT`); START again, or A, closes it |

Inside the ring the source's own navigation applies: left/right rotate the ring,
up/down change ring (main, keys, options), **B** selects or uses the item, **A**
or START leaves. The passport pages (new game / save / load / exit) take left/right
to change page. **B** on a save or load entry opens the three-slot picker: up/down
choose a slot, **B** accepts it, and **A** or START cancels the picker (the
cancelling button is not also read as leaving the passport until it is released).
No save or load happens without a slot accepted in the picker. The title's *new
game* and *Lara's home* (gym) entries are ordinary passport actions: the engine
never edits a slot itself.
Switching the *Vibration* option on pulses a Rumble Pak in port 1 for 250 ms;
switching it off stops it at once, and without a Rumble Pak nothing happens.

The stick is digital, as the fixed game's input is digital: an axis must exceed
24 raw units away from center to count. Diagonal movement combines actions;
D-pad and stick inputs are combined. Buttons are held actions, not one-shot
commands; the game owns its edge detection.

The sidestep and roll combinations follow OpenLara `src/fixed/lara.h` at
`2333adef30ae2a5f3b32c5355e6e343d6aa0531a` (`STATE_STOP`,
`STATE_STEP_LEFT/RIGHT`, `s_checkRoll`), not separate invented action bits.

## Local co-op

Connect a second controller to **port 2**. In a playable level, press **START**
on pad 2 to join as a second Lara at player 1's position with full health.
If both pads newly press START in the same poll, pad 1 wins. A START that
pad 1 was already holding does not block a fresh pad-2 join. An unplugged
pad cannot join. Pad 2 has the same movement and action controls as pad 1.

The top half is player 1's view; the bottom is player 2's. Health, air,
weapons and pickup notices are individual; ammunition, inventory and
medipacks are shared. Either player can open the one full-screen inventory
ring with **START** and drive it with the pad that opened it. The ring
pauses both players. Simultaneous START presses are handled by pad 1.
The vibration option tests the ring owner's Rumble Pak, if one is fitted.

- **L + START** on pad 2 leaves co-op. On pad 1 it still restarts the level.
- **L + A** warps that Lara to the other living Lara. Warping while alive
  is free. Reviving a dead Lara uses **one large medipack** from shared
  inventory. With no large medipack, nothing changes and the dead player's
  half briefly says **NO LARGE MEDIPACK**. A surviving player can continue;
  the death page appears only when both are dead.
- Leave and warp do nothing while that Lara is finishing a committed interaction; try the chord again after it settles.

Title, videos, cutscenes, inventory, statistics and death pages use the
full screen. A co-op session survives a level restart, change or save load:
the second Lara rejoins at the first Lara's position when gameplay resumes.
Save slots keep the first Lara and the shared world/inventory, not the
second Lara's individual state.
If player 2, a fallen Lara, or a Scion pickup still has an action to finish, the passport says **CANNOT SAVE NOW** and leaves the slots untouched. Close the ring to let the action finish.

## Game speed

Game time follows the hardware clock, not the frame rate. The loop owes 30 ticks per second of the
clock (`engine/in/pace.h`: an exact accumulator, one tick is 1,000,000 units and every elapsed microsecond
adds 30), and each rendered frame first runs the ticks that are due (none: it waits, pumping audio and
I/O; one; or several after a slow frame), then draws the latest state once. A frame that took 50 ms is
followed by a frame of one tick and then of two, alternately: the game does not slow down, the picture
simply comes at 20 a second. At most four ticks run before a frame (7.5 frames a second and below the game
slows): what is owed beyond that is dropped and counted, so a stall never ends in a burst. A level
load, a video, a save and the two-second notices start the count afresh. Sound, music and cutscenes are
driven by the same clock (the mixer by the DAC, the cutscene by its ticks) and stay together.

The pad is read afresh for every tick. Edges (the level-select chords, START, the slot picker, the game's
own) are taken against the previous tick, so a press that lies in two ticks of one frame is one press and a
press seen by any tick is seen once. L+START and the other chords leave the level at once: the ticks
of that frame that were still due are not run. The game's per-tick outputs (sound events, the music request,
pickup events, the stop) are taken after every tick. The code-unit rule is kept per frame: one
`code_frame` transition point and one I/O allowance before the first tick of a frame, none between
its ticks.

The route, hash-scene and oracle lanes still run exactly one tick per frame: the accumulator is the
interactive loop's policy, not the game's. The loop logs `play-frame: tick= input= tick_us= ticks=` per
drawn frame (`input` is every action any of its ticks received) and `play-pace:` (frames, ticks,
frames by ticks run, ticks dropped, restarts, with the clock) every 30 ticks and when a level ends.

## Restart and campaign flow

Death opens the source's death page and **waits for input**: its passport offers
*restart level*, *load game* and *exit to title*. There is no automatic restart.
Level completion shows the source's statistics page (time, distance, kills,
pickups, secrets, medikits, ammo) and advances through the gameplay campaign
when it is dismissed. START that dismisses the page (or ends a cutscene, or
restarts the level with L+START) is not the ring's START in the level that
follows, also after a video: it is withheld until it is released. The four in-engine cutscenes play between their levels
(LEVEL3B-CUT1-LEVEL4, LEVEL7B-CUT2-LEVEL8A, LEVEL10A-CUT3-LEVEL10B,
LEVEL10B-CUT4-LEVEL10C); they take no input and **START skips one** to the
level it leads to. The final level (or a completed gym) returns to the title. A named not-yet-ported game
boundary displays its name for two emulated seconds, then restarts the current
level.

Normal campaign completion preserves the game's inventory across the reload,
across a cutscene, skipped or played out. Restart from the death page, *new game*
and a gym/finale return use the fresh starting inventory. A loaded slot restores
its own level and inventory. The game's source-derived transition rules,
including LEVEL10A's weapon/ammunition reset, are applied before Lara and item
construction; the engine does not edit slots.

Pickups draw as small rotating icons that fly to the lower right corner for five
seconds of game time; the ammunition count of the drawn weapon appears at the source's HUD
position. The pickup layout is half the reference's pixel pitch for the 320x240
frame and its four point lights are the engine's scalar lighting.

Hold **L**, then press one of these buttons for demo level selection:

| Chord | Demo command |
|---|---|
| L + START | Restart current level |
| L + C-left / C-right | Previous / next level in the twenty-level catalog (the cutscenes included) |
| L + C-up | LEVEL1 |
| L + C-down | Lara's home (GYM) |

**L + D-pad Down** (D-pad Down's press, L held) switches the on-cart profiler overlay on and off: frame
times, the RDP's counters and per-frame counts over the game, for photographing on hardware
(`docs/perf-overlay.md`). It is not a game action or a level command; while L is held the game receives no input.
**L + D-pad Up** toggles the split-screen renderer preview in solo gameplay
only. Leaving gameplay turns it off; after a menu or cutscene, press it again
to resume. Both profiler chords are pad 1 only and require a neutral stick
and no other buttons. The preview uses the same player and pad with an orbited
second camera; it is not a multiplayer mode.

The catalog is the campaign order with the gym before LEVEL1 and each cutscene after the level that leads to it; it wraps at either end. Keep L held
and release/re-press the other button for another command; holding a chord
does not repeatedly load levels. While L is held, gameplay actions are
suppressed, not the simulation. The same commands can choose a different level
during the two-second boundary message.

Reloads do not reboot the console or reinitialize the GPU/library heap. The
game releases its unit references, the renderer unbinds the completed frame's
level, and the engine clears published resources, code dispatch/planner state,
the level arena and game state only after checking that I/O and leases are idle.
Malformed data, memory exhaustion and other engine invariants remain fatal.

The live loop starts level sound after game initialization, consumes sound
events exactly once after each game tick (including a terminal tick), and pumps
audio during ordinary frame waits and the two-second boundary message. Audio
is stopped and outstanding sample reads drained before game or level teardown.
The modal wait renews its I/O allowance at 30 Hz so streamed voices continue
after the first frame's transfer budget is consumed.

The earlier v0 and interim ROMs stop at death-page input, level completion and
not-ported boundaries; reset the emulator to restart those older artifacts.

## Building and checking

`make input-test play-test BUILD_DIR=$TR64_WORK/builds/Demo` runs focused
host checks independently of game data and the cart toolchain. `make pace-test` runs the accumulator
against a scripted clock (20, 33, 50, 100, 133 and 400 ms frames and mixtures; exact 30 ticks a second
across the clock wrap; a stall with and without a restart; the legacy one-tick-per-frame loop as the negative
control). `play-test --pace` drives the real loop through the same script (frames of 20, 33, 50, 100 and 400 ms,
a mixture, one 900 ms frame, then a 900 ms level swap) and checks the ticks of every frame, the dropped
count, no lost or doubled press and the first tick after the swap; `play-test --slow N` runs the whole
interactive script below with N ms added to every drawn frame, so frames carry up to four ticks. `play-test`
drives the real production play loop with synthetic game/platform services to
check death-ready/death, campaign advance, the two-second message, manual
selection, audio/release ordering and inventory carry. The slot picker, the save
glue and the medium are the real ones over a RAM device: a load takes two B
presses (open the picker, accept) and the unusable, unconfirmed and
game-rejected loads go to the title. A film between the title and LEVEL7B polls
the pad and ends on a START edge with START still held; START held at a level end,
through that film, and after an L+START restart must not reach the next level as
SELECT (ring) until released. Its negative controls require invalid capture to
fail rather than silently start fresh, and each START case fails against the
previous loop. These prove
engine policy, not game parity; `test/game/stops.py` and the real inventory
transition scene check the game's typed boundaries and initialization rules.

The two-pad cart gate uses the PC campaign pack and the private sampled
ares build described in `tools/ares/README.md`. Generate the fall pose and
build the explicit test-only probe (no probe is linked in shipping):

```sh
rtk python3 tools/ship/gate_place.py --out "$PROBE_BUILD/generated/gate_place.h"
rtk make BUILD_DIR="$PROBE_BUILD" TR64_PLAY=1 TR64_TEST=1 \
  TR64_RENDER_MEASURE=1 TR64_SCENE_TEST_SOURCES=test/play/coop.c rom
TR64_ARES_HOST=<remote-host> rtk python3 tools/ship/coop.py --rom "$PROBE_BUILD/tr64.z64" \
  --host "$HOST_BUILD/host/tr64-host" --out "$EVIDENCE"
```

`TR64_LEVEL_PACK` must already be set for both builds. The gate replays the
actual cart inputs on the host and compares schema-5 hashes across the
walking, pickup, ring, death and restart/leave sequences. It separately
saves with both players present, moves away, loads through the real
passport and checks both restored/rejoined poses. `--wolf-only` uses the
same test ROM to check that a placed fight retargets when the nearer
player changes. Use a fresh evidence directory for each run.

Set `TR64_LEVEL_PACK` explicitly to a compatible full campaign pack containing
SN64 sound sections; the older silent M3 pack is not a shipping sound input.
Reuse a pack with matching committed cook/tool identities rather than
recooking immutable content. `python3 tools/ship/test_boot.py` checks the sound
verdict against synthetic played/missing/silent/underrun logs, and drives the
base run's whole loop (`tools/ship/boot.py`) against a synthetic cart
(`tools/ship/synthetic_cart.py`) with a lost, a doubled and an unrelated lost
pad press.

Every press the base run injects is released only after the cart's own log
acknowledges it (the menu state it causes, the level, 60 ticks of forward input
in the `play-frame` lines), and the next press waits until the cart has ticked
with nothing held. The slot picker logs its cursor (`ui: picker index=N`); the
gate's cursor step presses Up again (at most six presses) until the last logged
cursor is slot 3, so a lost or doubled press cannot select another slot. A step
whose acknowledgement does not come within 300 cart ticks (1200 s of wall time
when the game does not tick) ends the run with the step named in
`summary.json` `stalled`. On `TR64_ARES_HOST` the pad reaches the emulator
through the journal `pad.in.seq` (`tools/ship/drive.py` `pad_start`/`pad_write`),
one line per value in order, each held until the cart has ticked twice
(`test/run/ares-remote.sh`).

`tools/ship/ship.sh build` builds a shipping `TR64_PLAY=1 TR64_TEST=0` ROM with
the full campaign pack. `tools/ship/ship.sh check` runs four emulator sessions
through `test/run/ares.sh` (the host-wide ares slots, Expansion Pak disabled,
injected live pad input, periodic VI pictures) on the **private ares** with SRAM
import/export (`tools/ares/README.md`; `--ares` or `ARES_BINARY`):

1. **Base run, the delivered ROM.** Boot (a pack with videos always gets the
   video verdict: ESCAPE and CORE play through, CAFE and SNOW are skipped
   with START and show at least ten pictures; `TR64_VIDEO_ABSENT=ESCAPE` names
   the film a PlayStation pack lacks and must equal what the pack's directory
   lacks), the title passport (B), new game (B), LEVEL1 with forward input
   (**Lara's** logged position must move at least 512 units, not the camera), the
   ring opened and closed with START, then the **save**: ring, options ring,
   passport (Save Game), the slot picker (it must open with three empty slots),
   cursor up to slot 3, B; the log must show the picker accepting slot 3 and the
   completed save, and Lara's state at the save is logged. Then the completed
   loads title -> LEVEL1 -> LEVEL1 (L+START restart) -> LEVEL2 -> LEVEL1, the
   level select to CUT4 (L+C-right/left), CUT4 skipped with START into LEVEL10C,
   sound reports (events played, zero stream and output underruns). The emulator
   is then terminated and exports its SRAM.
2. **Power-cycle run, the delivered ROM**, with that SRAM imported: the title
   passport opens on the load page, the picker must offer exactly slot 3 (the
   other two empty), B accepts it, and the loaded Lara (position, rotation,
   room, health) must equal the logged state at the save; she then walks at
   least 256 units, so the loaded state is live.
3. **Gate run, the gate ROM**: the same sources plus `tools/ship/gate-place.c`
   (L+Z places Lara in a tall fall, L+R on the level's end trigger;
   `tools/ship/gate_place.py` derives both from the player's LEVEL1 data of the
   pack's source, into the build's generated directory, never committed). Lara
   dies; the death page must be shown and **wait** 45 ticks with no stop, load or
   change; B restarts the level (a level end, LEVEL1 again, Lara alive, no ring).
   Then she is placed on the end trigger: the statistics page must be shown and
   wait 30 ticks; START dismisses it (held for 8 ticks into the next level): the
   next level (LEVEL2) must start with no ring and no SELECT in the game's
   input. Placement chords exist only in the gate ROM; the delivered ROM carries
   none.
4. **Pace run, the gate ROM** (`--pace-burn 0,40`, always in `ship.sh check`;
   `TR64_PACE_BURN=0,20,40` for more): L+A steps a load knob (0, 20, 40, 60 ms of
   the hardware clock burned after every drawn frame, audio and I/O still pumped);
   L+B places Lara on a flat patch of an empty room (`GATE_RUN`, derived like the
   others) and the stick runs her in a circle. For each burn, over ten seconds of
   the clock after she is up to speed: the ticks run equal what 30 Hz of the clock
   owes (to one tick, nothing dropped); her path (a chord per tick) scaled to the
   same ten seconds is within one tick's travel of the 0 ms run's; the mixer's
   output frames follow the clock (and the music stream's, when it plays) to within its
   queue, and the game's ticks agree with them. `gate-run:`/`gate-audio:` lines carry
   the evidence; `play-pace:` gives frames, ticks, frames by ticks run and drops.

The base run requires its level sequence exactly and no fatal error; every
secondary check is a named boolean in `summary.json` (`save_load`, `death_end`, `pace`).
Each session checks both `build_id` and `config_id` in its own `play:` boot
line against an independently generated `cart_identity.h`. `ship.sh check`
and `ship.sh deliver` derive both expectations from current inputs in fresh
scratch directories, so a stale ROM cannot vouch for itself with its sidecar.
Direct invocations use `generated/cart_identity.h` beside the ROM by default.
Every `level-pack: sha256=` is checked against the independently hashed `--pack` file.
For relocated ROMs use `--identity-header` and `--gate-identity-header`.
The base and power-cycle sessions use the delivered build; death/end
and pace use the gate build. The observed identities and any mismatch are retained
in `summary.json`. Secondary sessions use START to skip boot films; only the
base run owns the video verdict. For a base-only smoke run on a pack with films,
invoke `boot.py --rom ROM --pack PACK --out OUT --skip-videos`; this is not a
delivery verdict. Inspect the pictures before delivery: these records alone
do not prove that Lara moves correctly. `python3 tools/ship/test_boot.py` runs every predicate against
synthetic logs, positive and negative.

Set `TR64_VISUAL_REVIEWED=1` only after viewing the pictures, then run
`tools/ship/ship.sh deliver`. It requires a clean committed source tree and
`tools/ship/deliver_check.py` to accept the summary: the base, power-cycle and
gate runs passed; the ROM md5, both source and configuration identities
(delivered and gate ROM, from the current sources and inputs), and the pack's
SHA-256 are the ones checked; and, when the pack holds
videos, the video verdict passed. It then copies the ROM to `<remote-ssh>:roms/`
and checks local and remote MD5 values. The gate ROM is never delivered.
Generated content and evidence stay outside Git. Frame reports distinguish the
renderer interval (back buffer acquired to GPU completion) from game tick cost;
they do not claim hardware timing or full-campaign verification.
