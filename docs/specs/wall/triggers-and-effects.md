# Behaviour specification: floor-data triggers, activation rules, flip effects and animation commands
Source project: Tomb1Main (GPL-3), commit 3b2664b
Files read (all under `src/`): `game/control.c` (item control loop, item animation, floor and ceiling height queries, trigger evaluation, activation test, camera refresh, flip map, music triggers, secret count), `game/items.c`, `game/camera.c` (camera calculation, fixed camera), `game/cinema.c`, `game/sound.c`, `game/lara.c` and `game/laramisc.c` (where Lara evaluates triggers; Lara animation commands), `game/laraswim.c` (water current), `game/objects/{switch,keyhole,puzzle_hole,scion}.c`, `game/effects/*.c`, `game/traps/movable_block.c`, `game/savegame.c`, `specific/sndpc.c` (music play rules), `global/{const,types,vars}.{h,c}`
Reader: WallReader
Date: 2026-11-18

Conventions: a frame is one logic tick (30 per second). "Seconds" in
trigger timers are 30 frames each. The five mask ("code") bits are bits 9 to 13 (0x3E00) and the one-shot bit is bit 8
(0x0100) in the trigger's flags word. An item keeps the same bits in its runtime flags (bit 8 one-shot, bits 9-13
mask) plus an internal reverse flag, bit 14 (0x4000), which a trigger never writes. In the **level data** an item's flags
use bit 8 with a different meaning: "starts invisible" (see section 4).

This game's floor data has exactly the action codes 0 to 10 listed in section 5.
There is **no** flyby camera, no cutscene action and no "clear bodies" action in the
floor data: those do not exist in this game. A cutscene is a separate level played
from the game flow (section 9).

## 1. Floor data entry layout (facts of the level data)

A sector's floor data is a list of entries; each entry's first 16-bit word has the
entry type in bits 0-7 and the "last entry" flag in bit 15 (0x8000). Types:

| Type | Meaning | Extra words |
|---|---|---|
| 1 | door (portal to another room) | 1: room number |
| 2 | floor tilt (x slope in the high byte, z slope in the low byte, signed) | 1 |
| 3 | ceiling tilt | 1 |
| 4 | trigger | see below |
| 5 | lava floor | 0 |

A trigger entry: first word as above with bits 8-13 = the trigger type (0 to 8).
Second word = the trigger's flags: bits 0-7 timer, bit 8 one-shot, bits 9-13 mask. For
trigger types 2 (switch), 3 (key) and 4 (pickup) a third word follows holding the
controlling item's number (low 10 bits). Then a list of one or more action words: low
10 bits = value, bits 10-13 = action code (0-10), bit 15 = last action of the list. A
camera action (code 1) is followed by one more word: bits 0-7 = camera timer, bit 8 =
one-shot, bits 9-13 = camera speed code, and **the last-action bit is read from this
extra word**, not from the action word.

Floor and ceiling height queries (the rules in `drawbridge.md` and `objects-missing.md`)
walk the same list: slopes are applied first; then each trigger entry's object
actions (code 0) give the listed items a chance to change the height. For switch, key and pickup triggers the third
word is not skipped by that walk: it is read as the first object action, so the controlling item is treated as the first
object in the list (which matters only if that item has a floor or ceiling rule, and switches, key holes and pickups have none).

The "trigger pointer" of a sector for the evaluation below is the first lava entry or
trigger entry found in its data. If the sector's first data is a lava entry that
is not the last entry, evaluation continues with the next entry (the trigger).

## 2. Where and when triggers are evaluated

Every logic frame the order is: all active items; all active effects; Lara; the camera calculation; then the current
flip effect (section 8). Triggers are evaluated:

* **For Lara**, once per frame at the end of her own update (on land, in water, on the
  water surface), for the sector she was found in by the collision probe at her
  position, in "normal" (non-heavy) mode. When Lara lands from a fall, the sector
  under her is also evaluated at the instant of landing with her y set to the floor
  height, so a pad is pressed by the landing itself even if her y was above the floor
  that frame.
* **For things that "weigh" on a sector (heavy mode)**: a moving block that has come to
  rest, a rolling ball every frame it rolls, Thor's hammer when it is finished, the
  shootable scion when it dies, the mutant "Abortion" (Adam) when it dies and explodes,
  and the evil Lara double (every frame while it falls, and again at the landing
  frame); each evaluates the trigger of the sector under it in heavy mode. In heavy
  mode only trigger entries of type 5 (heavy) fire. The rolling block (`objects-missing.md` 3) is not among these
  (differs from OpenLara).

### 2.1 Lava entries

If the sector's first entry is lava, then (in non-heavy evaluation) if Lara's y equals
her recorded floor height she is burnt (see `objects-missing.md` 15.3). Then, if that
was the last entry, evaluation stops; otherwise it carries on with the following
trigger entry of the same sector.

### 2.2 The camera refresh that happens first

Before the type is considered (and not at all while a "heavy" camera is running), the
entry's action list is scanned to maintain a running fixed camera and look-at target
(section 6). This happens for every trigger type, including type 8 (dummy), which does
nothing else.

## 3. Trigger types

All types except heavy are tested only in normal evaluation; in heavy evaluation
everything except type 5 is ignored.

| Type | Name | Fires when |
|---|---|---|
| 0 | trigger | every frame Lara is in the sector |
| 1 | pad | every frame Lara is in the sector **and** her y equals her floor height (not jumping/falling) |
| 2 | switch | the controlling switch item is "settled" (see 3.1) |
| 3 | key | the controlling key hole is in use and Lara's hands are free (see 3.2) |
| 4 | pickup | the controlling pickup item has been picked up, once (see 3.3) |
| 5 | heavy | only in heavy evaluation (a block, ball, hammer... is on the sector) |
| 6 | anti-pad | like pad (Lara on the ground) but its object actions clear mask bits instead of setting them |
| 7 | combat | every frame Lara is in the sector with her weapons drawn |
| 8 | dummy | never (only keeps cameras/targets alive) |

### 3.1 Switch trigger

The controlling item (the switch) must have status "deactivated" (this is the state the
item is left in at the end of the pull animation by the animation's deactivate command).
Otherwise nothing happens. If it does:

* If the switch is in state 0 (its pulled position; state 1 is its position at level
  start [INFERENCE: the source only shows that the item is created in the state of its
  first animation and that state 1 is the one a timed switch returns to]) and the
  trigger's timer is above 0: the switch item's own timer is set to
  the trigger's timer (times 30 unless the timer is 1) and its status becomes active
  again. The switch's own control then counts the timer down and, when it runs out,
  sends the switch back to state 1 by itself; when it has finished that animation
  and settles, the next evaluation of the trigger (if Lara is standing in the
  sector) passes the "settled" test again, finds the state is 1, removes the item from the
  active list and toggles the targets back. If Lara is not there then, the targets
  are only undone by their own timers.
* Otherwise the item is removed from the active list and its status becomes "not
  active".

The switch trigger's actions then run with the `switch` type rules (toggle, see 4). In
addition a flag "switch is off" is true when the switch is now in state 1; with it,
camera actions of a timed switch trigger are skipped (so the camera does not fire for
the release).

The switch item's own control sets all its code bits in its flags every frame and goes
to state 1 as soon as its timer says "not active" (resetting the item's timer to 0).

### 3.2 Key trigger

The key hole must be active (a key was inserted: `objects-missing.md` 25) and Lara's hands must not be occupied (the
use-key animation has finished). Then the key hole's status becomes "deactivated" (so it fires once) and the actions run.

### 3.3 Pickup trigger

The controlling pickup item must be invisible (already taken). Its status becomes
"deactivated" so the trigger fires once, then the actions run.

## 4. Object action (code 0): the activation rules

Taking each object action in turn, with the trigger's values `T` (timer, 0 to 255), `M` (mask bits) and `O` (one-shot bit):

1. If the target item already has its one-shot flag set, nothing happens at all.
2. The item's timer is set to `T` times 30, except that `T = 1` stays 1 (a one-frame pulse) and `T = 0` means "stay
   active". This happens on **every** evaluation, including those in which the mask turns out incomplete; because
   Lara stands on a pad evaluating it every frame, the timer is reloaded every frame she is on it and only begins to
   run down after she steps off.
3. The mask changes: for a switch trigger the item's mask bits are toggled by `M`; for an anti-pad trigger the bits `M`
   are cleared; for any other trigger, if `M` is non-zero the bits `M` are set, and if `M` is zero nothing changes.
4. If the item's mask is not now complete (all five bits set), stop.
5. If `O` is set the item's one-shot flag is set (it then ignores every later action).
6. If the item is not already active:
   * "Intelligent" objects (all enemies): if its status is "not active": its touch state is cleared, its status becomes
     active, it becomes active and its AI is switched on in always-awake mode. If its status is "invisible": its touch
     state is cleared and its AI is switched on in the ordinary mode; its status becomes active only if a creature
     slot was available, otherwise it stays invisible; it becomes active either way. Other statuses (dead,
     deactivated): nothing.
   * Everything else: touch flags cleared, status active, added to the active list (objects that have no per-frame
     logic never become active: their status returns to "not active").

An item's "triggered" test (used by every item that follows triggers) as a table. "Reverse" is the item's reverse
flag; "complete" means all five mask bits are set.

| Mask | Timer | Not reverse | Reverse |
|---|---|---|---|
| incomplete | any | not triggered | triggered |
| complete | 0 | triggered | not triggered |
| complete | -1 | not triggered | triggered |
| complete | above 0 | triggered; the timer drops by 1 each frame the item asks, and when it reaches 0 it becomes -1 | not triggered, same countdown |

So a normal item is triggered while its mask is complete and (the timer is 0 or still counting down). A reverse item
is triggered while its mask is incomplete or its timer has run out.

At level start an item whose level-data flags have all five mask bits set has them
cleared, gets the reverse flag, status active and is put in the active list at once
(it is "on" until a trigger completes its mask). An item with bit 8 set in its level
flags starts invisible (and bit 8 is cleared).

## 5. Other actions (codes 1 to 10)

| Code | Name | Value is | Effect |
|---|---|---|---|
| 1 | camera | fixed camera number | section 6 |
| 2 | sink (current) | sink number | section 7 |
| 3 | flip map | flip map slot 0-9 | section 5.1 |
| 4 | flip on | flip map slot | section 5.1 |
| 5 | flip off | flip map slot | section 5.1 |
| 6 | target | item number | section 6 |
| 7 | finish level | ignored | the level is complete (the control loop ends at the next frame) |
| 8 | CD track | track number | section 10 |
| 9 | flip effect | effect number | section 8: run after a flip |
| 10 | secret | secret number 0-15 | section 5.2 |

Codes 11 to 15 are not handled and do nothing.

### 5.1 Flip map actions

There is a table of 10 slots, each with the same one-shot and five mask bits as items.

* Code 3 (flip map, slot `s`): if the slot's one-shot is set, nothing. Otherwise: for a
  switch trigger the slot's mask is toggled by `M`; for others, if `M` is non-zero it is
  OR'd in. If the slot's mask is now complete: if the trigger's `O` is set the slot's
  one-shot is set; and if the map is currently not flipped, a flip is requested. If the
  mask is not complete and the map **is** currently flipped, a flip (back) is requested.
* Code 4 (flip on): if the slot's mask is complete and the map is not flipped,
  a flip is requested. (No bits are changed.)
* Code 5 (flip off): if the slot's mask is complete and the map is flipped, a flip
  is requested.
* When the whole action list has been processed: if a flip was requested the map is
  flipped once (section 8.1); and **only in that case**, if the list contained a flip
  effect action (code 9), that effect number becomes the current flip effect with its
  timer reset to 0. A flip effect action without a flip does nothing.

### 5.2 Secret

If the secret's bit in the level's collected-secrets word is already set, nothing.
Otherwise the bit is set and CD music track 13 is played (the secret jingle; it
replaces whatever music was playing, see 10). The game also counts, at level load,
how many different secret numbers appear in the level's floor data (this is the
"secrets in level" total shown at level end).

## 6. Cameras and look-at targets

Fixed cameras and sinks share one table of level-data "camera" records. A camera record
has x, y, z, a room number (in its data word) and flags (the one-shot bit is stored here
at run time and saved with the game).

### 6.1 Camera action (code 1) in the trigger evaluation

With `n` = the camera number, `Tc` = the camera extra word's timer (bits 0-7), `Oc` = its
one-shot bit, `Sc` = its speed code (bits 9-13):

1. If camera `n` has its one-shot flag: nothing.
2. The "current camera number" becomes `n`.
3. Nothing further if: a look-at or combat camera is in effect; the trigger is a
   combat type; the trigger is a timed switch trigger and the switch is off (3.1); or
   (for non-switch triggers) `n` is already the camera used last frame (the camera
   is not re-armed while Lara stands in the same trigger).
4. Otherwise: the camera timer becomes `Tc` times 30 unless `Tc` is 1; if `Oc` is set the
   camera's one-shot flag is set at once; the camera's speed (smoothing divisor) =
   `Sc * 8 + 1` (the plus 1 differs from OpenLara); the camera mode becomes "fixed" (or "heavy" when evaluated in heavy
   mode).

### 6.2 What a fixed camera does

Each frame, in the camera calculation: the camera position moves toward the record's position (its room is in the record's
data word) by `(ideal - current) / speed` per frame on each axis. If the line of sight from the target to that ideal point is
blocked, the ideal point is moved first: on each axis where it lies within 256 of a wall edge of its box and a point 256
beyond that edge is out of bounds, it is pushed to 256 inside the box on that axis; and its height is kept 256 below the floor
and 256 above the ceiling (or at the middle if those cross). The target point
is Lara's (or the target item's, see below). If the camera timer is above 0 it
counts down by 1 per frame and at 0 becomes -1.

At the end of each camera calculation the camera mode is reset to the normal chase
mode, the "current camera number" is cleared (but remembered as "last") and the
target item is cleared, **unless the mode is heavy and its timer is not -1**.
Consequences you can test:

* A normal (non-heavy) camera trigger only lasts while the trigger is re-evaluated each
  frame: Lara standing in the trigger sector keeps it going; the first frame she is
  not in it, the camera goes back to chase.
* While she stays in the sector, the running fixed camera is kept by the camera
  refresh in 2.2: each frame it finds its own camera number equal to the last one and
  re-asserts "fixed" unless the camera timer is below 0 or a look/combat camera is in
  effect, in which case the timer becomes -1 and the camera is no longer fixed. So a
  timed camera (timer > 0 seconds) shows the fixed view for exactly that many seconds
  from the frame it was armed (its timer is counted in the camera calculation) and
  then goes back to chase even though Lara is still on the trigger; it re-arms only
  after she has left the trigger sector for at least one frame and come back. A camera
  timer of 0 means untimed (while Lara stays on the trigger). A heavy camera (armed
  from a heavy trigger) stays until its timer reaches -1 even without re-evaluation.
* A one-shot camera plays once: its flag blocks any later arming; the camera that is
  already running continues by the refresh rule above.

### 6.3 Target action (code 6)

The listed item is remembered during the list and then, if the camera ended up in
fixed or heavy mode, becomes the camera's target item. While a target item is set and
the camera is not fixed, Lara's head and torso turn toward it (half of the angle to
it, each frame by at most 4°, only if that half-angle is within ±50° yaw and the half
pitch is within ±85°) and the camera goes to the "look" mode; the item is marked as
looked at. In the refresh of 2.2, the target item is dropped if the camera is no longer
armed, or if it has been looked at before and is not the one used last (so a target
looked at once is not looked at again on later passes).

When the camera is fixed, the camera looks at the target item's centre (the middle
of its current bounds) instead of Lara.

## 7. Sink (code 2): underwater current

The sink's record gives a target position (x, y, z), a box number (stored in its flags field) and a
strength (stored in its data field). Evaluating the action while Lara is in the trigger sector:
if Lara's required box differs from the sink's box number, the target position and
box number replace hers; and her current strength becomes strength × 6 for this frame.
Each frame in which her current strength is non-zero (and she is not in the fly cheat
state) and she is swimming or at the surface, she is pulled by that many units per axis
per frame toward the next waypoint of her path to the sink's box (each of x, y and z moves
by the smaller of the strength × 6 and the remaining difference, independently), her collision is then
re-tested at the new position, and her current strength is reset to 0 (so it only
applies while the trigger is being re-evaluated every frame). The path to the box is found by the
creature path search of `lara-and-enemies.md` 3.2, with Lara's limits: step up 20480, drop 20480 and vertical
speed 256.

## 8. Flip map and flip effects

### 8.1 What flipping does

Flip map swaps every room that has an alternative with that alternative: the room's
floor data, portals, boxes, lights, geometry, water flag and static meshes become those
of the alternative, and vice versa. The **room numbers of items and effects are not
changed**: they stay in the room list of the same number. Movable blocks and rolling
blocks (`objects-missing.md` 2.1) have their floor change removed from the room
being swapped out and re-applied to the room swapped in. The ambient sound loops are
restarted. The "map flipped" state toggles.

Sound sources in the level data carry flag bits: bit 6 (0x40): sounds only while the
map is flipped; bit 7 (0x80): sounds only while it is not flipped (a source with both
sounds always).

Flip state, the 10 slot masks (stored as the mask bits and one-shot, 8 bits each),
the current flip effect and the effect's timer are part of the save game. A game loaded
with the map flipped re-flips it first; **the music one-shot table is not saved** (only
a few track flags tied to a boss kill are rebuilt on load).

### 8.2 The effect list

A flip effect (action 9) runs once per frame, after the camera update, with no item, until it ends itself by clearing the
current effect. Its timer counts frames from 0 and rises by 1 each frame (the numbers below are the timer value
tested). The same effects are applied by animation commands (section 11), once, with the animating item.

| # | Name | Behaviour |
|---|---|---|
| 0 | turn 180 | adds 180° to the item's facing (only meaningful from an animation command) |
| 1 | dinosaur stomp | camera shake from an item: if the item is within 16384 of the camera on all axes the camera "bounce" becomes `(1048576 - d) * 100 / 1048576` with `d` = (dx²+dy²+dz²)/256 |
| 2 | Lara normal | puts Lara in the standing state (state/goal 2, animation 11, frame 185), restores the chase camera and the normal field of view (animation command use) |
| 3 | Lara bubbles | each call: a random 0-2 bubbles (random 0..32767 × 3 / 32768; none: nothing else happens); with at least one: bubble sound at Lara, and that many bubble effects at her head joint offset (0,0,50), each with speed 10 + (0..5), sprite frame -(0..2). A bubble drifts up by its speed, weaves ±11 on x and ±8 on z, and vanishes when it leaves water or reaches the ceiling |
| 4 | finish level | the level is complete |
| 5 | earthquake | timer 0: explosion sound (global) and camera bounce -250; timer 3: rolling-ball sound; 20, 50, 70: T-rex footstomp sound; 35: explosion sound; ends when the timer reaches 105 (after running that frame) |
| 6 | flood | for timer 0..120: the "big waterfall" sound at (Lara x, camera target y + 100 × |timer - 30|, Lara z) each frame; the effect ends on the first frame the timer is above 120 |
| 7 | raising block | global "raising block" sound; ends at once |
| 8 | stairs to slope | at timer 5: the sound "stairs to slope" and the effect ends; earlier frames do nothing |
| 9 | drop sand | timer 0: trapdoor-open sound; timer 0..120: the "sand" sound at (camera target x, camera target y + 100 × timer, camera target z) each frame; ends when the timer is above 120 |
| 10 | power up | timer 0..120: the "power-up" sound at (camera target x, y + 100 × timer, z) each frame; ends when above 120 |
| 11 | explosion | global explosion sound, camera bounce -75, ends at once |
| 12 | Lara hands free | Lara's weapons become holstered |
| 13 | flip map | flips the map (section 8.1) |
| 14 | draw right gun | swaps Lara's right thigh and right hand meshes with the pistol holster ones |
| 15 | chain block | timer 0: the "secret" sound (sound 173, global); on timer 55 the splash sound (33) and the effect ends. This is a sound, not the CD track 13 that the secret action plays |
| 16 | flicker | timer 90, 92, 105 and 107: the map is flipped; timer above 125: the map is flipped once more and the effect ends. The timer rises by 1 every frame |

Effects 0 to 3, 12, 13 and 14 are meant for animation commands; the flip-effect action of a trigger is used with 4 to 11, 15
and 16. Effect numbers of 17 and above are undefined: reject them.

Sound numbers for the effects above (see `glossary.md`): earthquake effect: explosion 99 at timer 0 and 35, rolling ball 147 at timer
3, T-rex footstomp 70 at 20, 50 and 70; flood: big waterfall 81; raising block 117; stairs to slope 119; drop sand: trapdoor
open 161 at timer 0 then sand 118; power up 155; explosion effect 170 (effect 11); bubbles: 37 on the frame bubbles appear.

## 9. Cutscenes

Cutscenes are not floor-data actions. The game flow starts a cutscene level (its own
level file) in which the cinematic players (objects 77, 78, 79, 80) are the actors:
player 1 follows the cinematic camera position and facing every frame; players 2 and
3 are placed with facing 0 and run their animations; player 4 only animates. A cutscene
has a table of per-frame camera keys: 8 numbers each (target x, y, z; camera x, y,
z; field of view; roll) relative to the starting position and the base facing. The
cutscene logic ticks at 30 frames per second: each tick the active items and effects
run, then the cinematic camera is computed from the key for the current frame; when
the frame counter reaches the table's length (or the Option key is pressed) the
cutscene ends, music and sounds are stopped and the game moves to the next level in
the game flow (it counts as a completed level).

In-game cinematics (the scion and Midas pickups: `objects-missing.md` 14 and 23) use
the same table: the camera goes to "cinematic" mode, the table is played from frame 0
relative to Lara's position and facing at the start, one frame per tick, clamped at
the last frame.

## 10. Music triggers (code 8)

The value is a track number. Music one-shot state exists per track in a table of 64
(each with the same one-shot and five mask bits). Tracks 0 and 1 and numbers of 64 and
above are ignored by this action.

Before the normal rule, some tracks have special conditions:

| Track | Special rule |
|---|---|
| 28 | if its one-shot is already set and Lara is in the standing-jump-up state (28), play track 29 instead |
| 37 | only if Lara is hanging (state 10); otherwise ignored |
| 41 | only if Lara is hanging; otherwise ignored |
| 42 | if its one-shot is set and Lara is hanging, play track 43 instead |
| 49 | only if Lara is treading water on the surface (state 33) |
| 50 | if its one-shot is set: a counter increases each frame the trigger is evaluated and when it has reached 120 the level is complete (this is the gym ending); otherwise (one-shot not yet set) only if Lara is leaving the water (state 55) |

Normal rule: if the track's one-shot is set, nothing. Mask update (switch: toggle; anti-pad:
clear; others: OR if `M` non-zero). If the mask is complete: if the trigger's one-shot is set
the track's one-shot is set; and if the track is not the one playing now it is
played. **If the mask is not complete the music is stopped.**

Playing a track: a track number of 5 is ignored (differs from OpenLara, which uses track 5 as the Caves ambient); track 0
stops the music (game flow only: the trigger action never passes 0 or 1); tracks below 2 do nothing; tracks of 57 and above
are remembered as the "looped" track and restart when they end; any other track plays once. Music is the CD audio of the
original: track `n` is the disc's track `n`, ending at the start of track `n + 1`. A song already playing is replaced.

## 11. Animation commands (executed by the animation of any item)

An animation (from the level data) has a list of commands. The executed ones in this
game, with their numbers:

| # | Command | Words | Effect |
|---|---|---|---|
| 1 | move origin | x, y, z | **at the end of the animation**, before it jumps to its next animation: translate the item by (x, y, z), rotated by the item's facing (x/z rotated, y added) |
| 2 | jump velocity | fall speed, horizontal speed | at the end: set the item's fall speed and speed; gravity on (for Lara: if she has a stored "calculated fall speed" it replaces the fall speed and is cleared) |
| 3 | attack ready | - | at the end (Lara only): her weapons become holstered |
| 4 | deactivate | - | at the end: the item's status becomes "deactivated" (this is what ends the activity of switches, blocks, etc.) |
| 5 | sound effect | frame, sound number | on the frame of the animation with that absolute frame number: play the sound at the item's position (for Lara always audible; for other items in underwater mode when their room is under water) |
| 6 | effect | frame, effect number | on that absolute frame number: apply flip effect `n` (section 8.2) once, with the animating item |

The animation advances one frame per tick; the state/goal machinery (the "changes"
table: at a frame inside a range, a goal state different from the current state sends
the item to a linked animation and frame) is as for all items. A goal that nothing
accepts is ignored, which is why triggers can only change a state at specific frames.
When an animation passes its last frame the end commands run and the item continues with
the animation and frame named by the animation (its "jump" target). The item's
horizontal speed is the animation's velocity plus acceleration times frames elapsed
(16.16 fixed point, taken as the integer part), unless gravity is on (then speed is
integrated and the item falls with +6 per frame up to 128, then +1).

## 12. Save game of trigger state

Saved: whether the map is flipped, the 10 flip slot values (mask and one-shot), the one-shot mark of each fixed camera, and
for each item that saves its flags: its flag bits (including one-shot, mask and reverse), whether it is active, its status,
its gravity and collidable marks, and its timer; also the current flip effect and its timer, the collected-secrets bits, and
the kill and pickup statistics. Not saved: the music one-shot marks (except the few rebuilt from dead bosses).
