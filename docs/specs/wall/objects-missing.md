# Behaviour specification: level objects that need logic of their own
Source project: Tomb1Main (GPL-3), commit 3b2664b
Files read (all under `src/`): `game/objects/{bridge,cog,boat,cabin,door,misc,pickup,keyhole,puzzle_hole,savegame_crystal,scion,switch,trapdoor}.c`, `game/traps/{dart,dart_emitter,falling_block,falling_ceiling,flame,lava,lightning_emitter,midas_touch,movable_block,pendulum,rolling_ball,rolling_block,spikes,teeth_trap,thors_hammer,damocles_sword}.c`, `game/ai/{pod,statue}.c`, `game/effects/{body_part,blood,missile,earthquake,dino_stomp,explosion}.c`, `game/{control,collide,items,sphere,savegame,setup,camera}.c`, `global/{const,types}.h`
Reader: WallReader
Date: 2026-11-18

This file states what each object does. Terms such as "active", "touch", "standard solid collision", "hit
reaction", "camera bounce", "near test", "untargetable" and the sound numbers are defined in `glossary.md`.
Heights are in game units (1024 per sector, 256 per click); larger y is lower. "Frame" is one logic tick (30 per
second). Animation state numbers, animation numbers and frame numbers are given where the behaviour depends on
them. Lengths of animations come from the level's animation data. Where the original game and OpenLara's
floating-point game disagree, the original is described and the line says "differs from OpenLara".

## 0. Objects of the list that have no logic at all

| Object | Number | What there is |
|---|---|---|
| Camera target | 169 | invisible; only a point a camera can aim at |
| Big trapdoor | 67 | no logic of its own; it is only drawn with its animation (it never becomes active) |
| Skateboard | 29 | not an independent item: drawn together with the skate kid |
| Fish (26), dino warrior (25), dust (167), bubble emitter (157), second explosion, splash, bubble and blood sprites (152, 154, 156, 159), missiles 4 and 5 (174, 175), the TEMP objects | | nothing happens even if placed (fish only have a path-finding entry: if they ever became active creatures they would fly/swim) |
| Passport, map, photo, option rings, `*_OPTION`, closed passport and map | various | inventory-only models |

## 1. Bridges

All three bridge objects share one rule. They have no animation and no per-frame logic. When the floor data of a
sector has a trigger entry listing the bridge as an object action, the bridge may change the floor and the
ceiling that a height query on that sector returns. Unlike the drawbridge (`drawbridge.md`) the bridges do not test
which sector the query is in: the level places the entry only on the sectors they span. Let `Y` be the item's y and
`y` the height of the query.

| Object | Number | Floor | Ceiling |
|---|---|---|---|
| Flat bridge | 68 | for a query at or above `Y` (`y <= Y`) the floor is `Y` | for a query below `Y` (`y > Y`) the ceiling is `Y + 256` |
| Tilted bridge, 1 click | 69 | with `L = Y + offset/4`: for `y <= L` the floor is `L` | for `y > L` the ceiling is `L + 256` |
| Tilted bridge, 2 clicks | 70 | with `L = Y + offset/2`: for `y <= L` the floor is `L` | for `y > L` the ceiling is `L + 256` |

`offset` (0 to 1023) is the distance across the sector along the slope direction:

| Item facing | offset |
|---|---|
| 0° | 1024 minus (x mod 1024); 0 when x mod 1024 is 0 |
| 180° | x mod 1024 |
| 90° | z mod 1024 |
| any other facing (270° in practice) | 1024 minus (z mod 1024); 0 when z mod 1024 is 0 |

So the 1-click bridge's height is `Y` at the offset-0 edge and `Y + 255` at the far edge, the 2-click one `Y + 511`.
This replaces the height the sector's own data produced, even when the bridge height is lower; a bridge listed
after another object overrides it. The deck is a slab 256 thick (underside at the top height plus 256; differs
from OpenLara, which uses 64). The tilted bridges give 0 at the sector edge where the offset is 0 (differs from
OpenLara, which gives 1024 there). There is no tolerance in this rule: the probe height `y` is whatever the
caller passes (see `drawbridge.md` section 4.4).

Can it block completing a level if it does nothing? Yes whenever the bridge is the only floor across a gap
[INFERENCE: level specific].

## 2. Moving block (objects 48, 49, 50, 51)

Four object numbers share one behaviour: the block that Lara pushes or pulls.

### 2.1 What the block does to the floor

A block occupies the sector under its origin. While it is there, that sector's floor is raised by 1024 (the floor
value drops by 4 clicks), so the sector reads as a step of one block height (or as a wall when that makes the floor
meet the ceiling). The change is made and undone by one operation, "raise" or "restore", on the sector:

* **Raise by D** (D = 1024 for a moving block, 2048 for a rolling block): let `C` be the ceiling value of the sector found
  at height `y - D - 1024`, where `y` is the block's y. If the sector is currently a wall, its floor value is set to `C - D`.
  Otherwise its floor value drops by D, and if it then equals `C` the sector becomes a wall.
* **Restore by D**: let `C` be the ceiling value of the sector found at height `y + D - 1024`. If the sector is currently a
  wall, its floor value is set to `C + D`. Otherwise its floor value rises by D, and if it then equals `C` the sector
  becomes a wall.
* In both cases, if the sector belongs to an AI box that enemies may be blocked by, the box is marked blocked while
  the block is present and unblocked when it is absent, so enemies do not path through a block.

When each is applied:

* At level start the block is raised, unless the block starts invisible.
* When a push or pull begins, the sector it is leaving is restored.
* When the move has ended, the sector it arrived on is raised.
* When a trigger sets the block's one-shot flag the sector is restored and the block is removed for ever; this is
  how a trigger makes a block vanish.
* When the map flips, every block in the room swapped out has its sector restored and every block in the room
  swapped in has its sector raised, so the cell stays solid in whichever version of the room is current.

### 2.2 Block behaviour while it is active (moving or falling)

* If its one-shot flag is set: the sector is restored and the block is removed.
* The block follows its push or pull animation, which moves it and ends by deactivating it.
* If the floor under it is below it, it falls with ordinary gravity (6 per frame up to 128). When it reaches the
  floor it stops on it, its status becomes deactivated, the camera is shaken (below) and the sound 70 (T-rex
  footstomp) plays at it.
* When the status is deactivated (the move ended or it landed): the block stops being active, the sector under it is
  raised, and the floor data of that sector is evaluated for **heavy** triggers only (`triggers-and-effects.md`
  section 2), so a heavy pad under the block's new place fires when the block arrives.
* If its room changes it moves to the new room.

Landing shake: when the block is within 16384 on every axis of the camera, the camera bounce becomes
`(1048576 - d) * 100 / 1048576`, where `d` is the sum of the squares of the differences divided by 256 (up to 100
when it lands on the camera; negative near the edge of the range).

### 2.3 Lara pushing or pulling it

The block reacts only when all of these hold: Action is held; the block is not already moving; Lara is on the
ground; Lara's y equals the block's y.

Let the quadrant be Lara's facing rounded to the nearest axis direction (+z, +x, -z, -x).

A. **Taking hold.** Lara is standing, Forward and Back are both released and her weapons are holstered. The
   block is turned to face the quadrant (0°, 90°, 180°, 270°). Lara must then be in the block's grasp zone: her
   facing within ±30° of the block's facing, her tilt within ±10° on the other two axes, and her position in the
   block's own frame within x between -300 and 300, y exactly 0, z between -692 and -512 (512 to 692 in front of
   the block centre; 692 is 512 plus Lara's radius 100 plus 80). If so, her coordinate along the facing axis is moved
   to the near edge of her sector, 100 inside it (facing +z: 1024 - 100; -z: 100; +x: 1024 - 100; -x: 100,
   measured from the sector start), she is turned to the block's facing and her goal state becomes
   "push-pull ready" (state 38); her animation advances one frame, and if she is now in state 38 her hands are
   occupied.
B. **Starting the move.** Lara is already in state 38, is exactly on animation frame 2091 and is in the grasp zone.
   * With Forward held and the push test (2.4) passing: the block's goal state becomes push (2) and Lara's goal
     state "push block" (36).
   * With Back held and the pull test (2.5) passing: the block's goal state becomes pull (3) and Lara's "pull
     block" (37).
   * Otherwise nothing happens.
   On success the block becomes active and its sector is restored, and both the block and Lara advance one frame.

Block states: 1 = still, 2 = push, 3 = pull.

### 2.4 Push test (the sector one ahead of the block, in Lara's facing direction)

All must hold:

1. The block's own sector is "movable": it is a wall or its floor is exactly 1024 above the block's y.
2. No scenery mesh overlaps a box of radius 500 and height 1000 at the destination.
3. The destination floor equals the block's y exactly.
4. The destination ceiling, at the block's top (y - 1024), is not below that top.

There is no test for slopes or trapdoors at the destination (differs from OpenLara, which rejects them).

### 2.5 Pull test (the sector toward Lara)

1. The block's own sector is movable (as 2.4 item 1).
2. The destination sector (one toward Lara): no scenery overlap (radius 500, height 1000), floor exactly the block's
   y, ceiling not below y - 1024.
3. The sector beyond it (where Lara stands) has the same floor, and its ceiling is not below y - 762.
4. Lara moved back one sector must not overlap any scenery with her radius (100) and height 762.

### 2.6 Save and load

Saved: position, room, speed, fall speed, animation state, frame and flags (see `drawbridge.md` section 6). On load
the block's sector is first restored at the block's initial position; after its saved data is read, if the block is
not moving its sector at the loaded position is raised. The rolling block (section 3) is handled the same with 2048
and "not in the moving state".

Can it block completing a level if it does nothing? Yes: pushing blocks is required in several levels
[INFERENCE: level specific].

## 3. Rolling block (object 52)

A larger block moved by a trigger, two sectors high: it keeps its sector raised by 2048 instead of 1024 (same
raise and restore operation as 2.1 with 2048).

States: 0 = start, 1 = end, 2 = moving.

* At level start the sector is raised.
* While triggered and in state 0 the goal state is 1 and the sector is restored (opened).
* While not triggered and in state 1 the goal state is 0 and the sector is restored too; it is raised again at
  the end of the move.
* When the animation ends (its status becomes deactivated) the block becomes active again, its sector at the new
  position is raised, and its x and z are moved to the centre of that sector (sector start plus 512).
* The block does not evaluate heavy triggers (differs from OpenLara).
* Save, load and flip map: as for the moving block, with 2048.

## 4. Trapdoors (objects 65, 66; 67 has no logic)

States: 0 = closed (acts as floor), 1 = open. While triggered and closed the goal is open; while not triggered and
open the goal is closed (the goal is only changed when the trapdoor is in the opposite state).

Floor and ceiling (through trigger entries, as the bridges):

* The trapdoor covers its own sector and one neighbour: facing 0°, the next sector in +z; 180°, the next in -z; 90°,
  the next in +x; 270°, the next in -x (the other coordinate must equal the item's sector). Sectors are the position
  divided by 1024, rounded down.
* Floor: for a query in a covered sector with `y <= Y`, with the trapdoor closed and `Y` above the floor already
  computed, the floor becomes `Y`. Open trapdoors, or ones below the existing floor, change nothing.
* Ceiling: for a query in a covered sector with `y > Y`, closed, and `Y` below the existing ceiling, the ceiling
  becomes `Y + 256`.

So a trapdoor never lowers a floor or raises a ceiling; it only fills in.

Save: current, goal and required state, animation, frame, flags, timer.

Can it block completing a level if it does nothing? Possibly [INFERENCE: level specific].

## 5. Doors (objects 57 to 64)

* At level start a door shuts two sectors: (a) the sector next to the item in its room in the direction of its facing
  (facing 0°: z - 1024; 180°: z + 1024; 90°: x - 1024; 270°: x + 1024), following that sector's portal if it
  has one; and (b) the sector at the item's own position in the room on the other side of that portal. The same two
  sectors in the flipped version of their rooms (if any) are shut too. Shut sectors become walls (no floor, no
  ceiling, no portal) and their AI boxes, when of the blockable kind, are blocked. The original floor data of each
  sector is remembered for reopening.
* While triggered: if the door's state is closed (0) the goal is open (1); if it is already open the sectors get
  their remembered data back and their boxes are unblocked.
* While not triggered: if the door is open the goal is closed; if it is already closed the sectors are shut again,
  except that they stay open while Lara is within 2048 units (three-dimensional distance) of the door and are shut
  once she leaves.
* While closed or moving the door is a standard solid collision with the hit reaction while it moves.
* The cheat that opens the nearest doors is not part of the game.

## 6. Cogs (74, 75, 76) and the moving bar (47)

Each frame while active the goal state is 1 while triggered and 0 otherwise, and the item follows its animation.
Cogs are not solid; the moving bar is a standard solid collision. All save flags and animation; the moving bar also
saves position. The drawbridge follows exactly the same rule.

## 7. Falling block (object 35) and falling ceiling (objects 53, 54)

### 7.1 Falling block

States: 0 = set, 1 = activate (shaking), 2 = working (falling), 3 = finished. The block's origin is 512 below its
walking surface.

* Floor (through trigger entries): for `y <= Y - 512` in states 0 and 1 the floor is `Y - 512`. Ceiling: for
  `y > Y - 512` in states 0 and 1 the ceiling is `Y - 256` (differs from OpenLara, which uses `Y - 512`). In
  states 2 and 3 it gives nothing.
* It is activated by a floor trigger on its sector. In state 0, if Lara's y is exactly `Y - 512` (she stands on
  it) the goal becomes 1; otherwise it becomes inactive again at once and nothing happens.
* State 1: the goal becomes 2. State 2: unless the goal is already 3, gravity is turned on.
* When it has landed (in state 2 and `y` at or below the floor under it) the goal becomes 3 and it rests on the
  floor with gravity off.
* If its status becomes deactivated it stops being active.
* Save: position, room, speed, fall speed, animation fields, flags.

### 7.2 Falling ceiling

States: 0 = set, 1 = activate (falling), 2 = working (landed).

* In state 0 the goal becomes 1 and gravity turns on (it starts falling at once when activated).
* In state 1, each frame it touches Lara, she loses **300** hit points and the hit flag is set (differs from
  OpenLara, which applies one hit of 1000).
* It lands when `y` reaches the floor height recorded for the item at level start; then the goal becomes 2 and it
  rests on that floor.
* Collision: while active, the touch test only; when not active (and not invisible) it is a standard solid
  collision.

Can they block completing a level if they do nothing? Only the block, if a level needs the drop [INFERENCE].

## 8. Darts

### 8.1 Dart emitter (object 40)

States: 0 = idle, 1 = fire. While triggered and idle the goal is fire; while not triggered and firing the goal is
idle. Each time the emitter is in the fire state and on the first frame of its current animation, it makes one dart.

The dart (object 39) appears in the emitter's room with:

* the emitter's facing, and y = emitter y - 512;
* an offset from the emitter's position on the axis it faces, 412 units opposite to the facing direction (facing
  0°: z - 412; 90°: x - 412; 180°: z + 412; 270°: x + 412) (differs from OpenLara, which uses 256);
* it is active at once, and a flash sprite (object 160) is made at that point with sound 151.

The flash advances one sprite frame every 3 frames and ends when its frames are used up (so it lasts 3 frames per
sprite frame).

### 8.2 Dart (object 39)

* While the dart touches Lara she loses **50** hit points each frame, the hit flag is set and a blood splat is made at
  the dart (heading and speed of Lara's). The dart is not removed when it hits and may hit again on later frames
  (differs from OpenLara, where a dart hits once).
* It moves with its animation's speed along its facing.
* When its y reaches the floor height under it, it is removed and a ricochet sprite is made (it lasts 6 frames; start
  frame random 0 to -2). A wall sector counts as a floor of -32512, so a dart that enters a wall sector is removed
  at once.

Can it block completing a level if it does nothing? No.

## 9. Pendulum (36), teeth trap (42), spikes (37)

### 9.1 Pendulum / swinging blade

States: 0 = set, 2 = working. While triggered and set the goal is 2; while not triggered and working the goal is 0.
While in state 2 and touching Lara she loses **100** hit points each frame, the hit flag is set and a blood splat is
made near her (x and z within ±64, up to 744 above her feet, heading near hers). Collision: while active the touch
test every frame; when not active a standard solid collision.

### 9.2 Teeth trap

States: 0 = open (harmless), 1 = biting. While triggered the goal is 1, and each frame it touches Lara in state 1 she
loses **400** hit points (hit flag set) and six blood effects appear at the six tooth points, which are offsets from
the item's meshes 0 and 1: x = -23 and 71 on the two meshes, z = -1718, and y = 0, 10 and -10 for the three pairs.
While not triggered the goal is 0. Collision as for the pendulum.

### 9.3 Spikes

Spikes have no animation and no per-frame logic. They act when Lara is alive (hit points not below 0), her bounds
overlap the item's and her body spheres touch it. Which case applies depends on Lara's movement:

| Lara's situation at contact | Hit points lost | Blood splats | Death |
|---|---|---|---|
| In the air and falling (fall speed above 0) | 15, and her hit points are set to -1 (killed) | 20 | yes |
| In the air and rising | 15 | 0 or 1 (random) | only if her hit points are now 0 or below |
| On the ground with horizontal speed below 30 | nothing happens | - | no |
| On the ground with horizontal speed 30 or more | 15 | 0 or 1 (random) | only if her hit points are now 0 or below |

A blood splat is placed around her (x and z within ±64, up to 512 above her feet, speed 20, random heading). Whenever
she ends contact with 0 or fewer hit points her state and goal state become "death" (8), her animation becomes
animation 149 at frame 3887 (the spike death), her y becomes the spikes' y and gravity is off.

Can they block completing a level if they do nothing? No.

## 10. Rolling ball (object 38)

The ball remembers its start position and room. States: 0 = set, 1 = activate (rolling).

**While active** (status active) the ball:

* hops when it is above the floor: if it is in the air with no gravity yet it gets gravity and a small upward speed
  (-10), otherwise, once on the floor in state 0, it goes to state 1 and rolls;
* follows its animation, which gives its speed along its facing; its room and the floor under it are updated;
* evaluates the floor data of its sector for **heavy** triggers every frame (the ball presses heavy pads);
* lands: when it is within 256 of the floor it rests on it with no fall speed;
* stops when the floor 512 units ahead along its facing is higher than the ball (a lower height value, which includes
  walls): its status becomes deactivated, its x and z return to where they were at the start of that frame, it rests
  on the floor, and its speeds and touch state are cleared.

**While deactivated**: as soon as it is no longer triggered it resets completely: not active, position and room
restored to those recorded at level start, animation and state reset to the first (state 0), and removed from the
active list. Releasing the trigger re-arms the ball.

**Collision:**

* Not active and not invisible: a standard solid collision.
* Active (needs bounds overlap and touch):
  * Lara in the air: she is pushed out with the hit reaction, loses **100** hit points, and one blood splat appears at
    the contact point;
  * Lara on the ground: she is killed outright (hit points -1): the hit flag is set, she is moved to the ball's room,
    her tilt is cleared, she is turned to the ball's facing, her state and goal state become "special" (46), her
    animation becomes 139 at frame 3561 (the rolling ball death), the camera is put on follow-centre at target angle 170°
    and elevation -25°, and 15 blood splats are made around her.

Save: position, room, speed, fall speed, animation, flags.

Can it block completing a level if it does nothing? No, but a ball that never presses its heavy pad might
[INFERENCE].

## 11. Damocles sword (object 43)

At level start: random facing, random spin of -1024 to +1023 angle units per frame, fall speed 50.

* **Hanging** (its y is not its floor, the floor being the one recorded at level start): it spins. When Lara is within
  1536 on both x and z (a box) and is below the sword by between 1 and 3071 (`0 < Lara y - sword y < 3072`), it is
  released: it falls with a sideways drift per frame equal to (Lara x - sword x) / 32 and the same for z, fixed at the
  moment of release.
* **Falling**: it keeps spinning; its fall speed rises by 6 per frame up to 128 and then by 1; it moves by that fall
  speed and the drift. When its y passes the floor it plays sound 103, rests 10 below the floor, stops falling, becomes
  deactivated and inactive.
* Collision: when the bounds overlap, a standard solid push; and while it is falling Lara also loses **100** hit points
  each frame with a blood splat near her (x, z within ±64, up to 372 above her feet).
* Save: position, animation, flags; the spin rate and the drift are part of its stored state and must survive a save.

Can it block completing a level if it does nothing? No.

## 12. Thor's hammer (objects 44 handle, 45 head)

The handle runs the logic. At level start a second item, the head, is created at the same position and facing; each
frame it copies the handle's animation offset, frame offset and state so the two stay in step. The head is not
drawn separately from the handle model set.

Handle states: 0 = set, 1 = tease (shakes), 2 = active (swinging down), 3 = done.

| State | Rule |
|---|---|
| 0 | while triggered the goal becomes 1; otherwise the item becomes inactive |
| 1 | while triggered the goal becomes 2, otherwise 0 |
| 2 | once the animation is more than 30 frames in, if Lara is alive and her x and z are both within 520 of the point 3072 units from the hammer's origin in its facing direction (0°: +z, 90°: +x, 180°: -z, 270°: -x), she is killed: hit points -1, y set to the hammer's, gravity off, state and goal "special" (46), animation 139 at frame 3561 |
| 3 | the sector of the hammer's own origin is evaluated for heavy triggers; if Lara is alive the sector 3072 ahead is raised by 2048 (it becomes a solid stump or wall); the hammer then becomes inactive with status deactivated |

Raising the sector uses the operation of `objects-missing.md` 2.1 with a raise of 2048 (differs from OpenLara,
which lowers the floor by 3072 within 512 of that point).

Collision: the handle is a solid that pushes Lara out of its box (touch test skipped). The head does the same except
while the handle is in state 2, when it lets her through so she can be struck.

Save: handle animation and flags.

Can it block completing a level if it does nothing? Possibly, if it closes the way when done [INFERENCE].

## 13. Lightning emitter (object 46)

The Thor's-hall lightning. With a single mesh it is a "no target" emitter that strikes the ground under it; with more
than one mesh it is a "targeting" one that strikes its own joints and Lara within range.

* While not triggered: its cycle is reset, the map is flipped back if it is flipped, and the item becomes inactive.
* While triggered it alternates off and on. The first off time is 1 frame. Each switching moment:
  * if the bolt was on: it switches off for 35 plus a random 0 to 44 frames, the zapped mark is cleared, and the map is
    flipped back if flipped;
  * if the bolt was off: it switches on for **20 frames**. The target is: Lara if she is near within 1024 (no-target
    emitter) or 2560 (targeting emitter): she loses **400** hit points once, the hit flag is set and she is marked
    zapped; else for a no-target emitter the ground point under the emitter; else a random one of its joints 1 to 5.
    Two thinner side bolts end at random points within 1024 of the target. The map is flipped if it is not (the
    flash is the room lighting of the flipped rooms).
  * Each switching moment plays sound 98 at the emitter.
* While Lara is marked zapped, each frame she gets a hit reaction with a random direction 1 to 4 and the counter
  rising by 1 (maximum 34).
* The drawn path is a jittered line (cosmetic). Save: flags only.

Can it block completing a level if it does nothing? Probably not, but the flip it drives could be needed [INFERENCE].

## 14. Midas hand (object 128)

An invisible item at the hand statue; it acts every frame Lara is near.

1. **Death**: if Lara is on the ground, standing (state 2) and her x and z are both within 512 of the item (a 1024
   square), she dies by the golden touch: state and goal "die Midas" (51); animation 1 of the Lara-extra object at its
   first frame; hit points -1; gravity off; air -1; weapons put away and her hands occupied; the camera goes to
   cinematic mode from her position with the cinematic frame counter at 0. No button is needed.
2. **Using the lead bar**: otherwise, if she is on the ground, standing, weapons holstered and either an inventory
   item is chosen or Action is pressed, the item is turned to the quadrant she faces. She must be inside this zone of the
   item's frame: x -700 to 700, y 284 to 996, z -700 to 700, with ±10° on x, ±30° on y and ±10° on z. If Action
   was pressed with nothing chosen, the key and puzzle items are shown for her to choose.
3. If she chose the lead bar: it leaves the inventory, puzzle item 1 (the gold bar) is added, her state and goal become
   "use Midas" (50), her animation becomes animation 0 of the Lara-extra object at its first frame and her hands are
   occupied.

Can it block completing a level if it does nothing? Yes: the gold bar is needed [INFERENCE].

## 15. Flame emitter (179), flame (178) and Lara on fire

### 15.1 Flame emitter

An invisible item. While triggered and with no flame yet, it makes one flame effect at its position (sprite frame 0,
counter 0). While not triggered and with a flame, it stops sound 150, removes the flame and forgets it.

### 15.2 The flame effect (object 178) each frame

Its sprite frame index runs through the object's sprite frames (the index counts down from 0 and wraps to 0 after
the last frame), one frame per game frame. Then it depends on its counter:

* **Counter 0 or positive: a scenery fire.** Sound 150 plays at it every frame. If the counter is above 0 it counts
  down (dormant). Otherwise, while Lara is near within 600 (glossary), unless she is in the fly cheat state: she
  loses **3** hit points per frame and the hit flag is set; and if she is also within 300 horizontally, the flame's
  counter becomes 100 (dormant for 100 frames) and a **new flame effect with counter -1 is created in Lara's room: Lara is now on fire.**
* **Counter negative: the flame is on Lara.** Each frame: its position is a joint of Lara's body, joint number
  `-1 - counter` (counter -1 puts it 100 behind the joint origin, others at the joint origin). If the water height at
  Lara is defined and the flame is below the water surface (`flame y > water height`), the flame goes out (sound
  stopped, effect removed). Otherwise sound 150 plays and Lara loses **5** hit points and the hit flag is set. (In the
  fly cheat state the flame is removed.)

There is no timeout: Lara burns until she reaches water or dies. Each flame on her costs its own 5 points, so several
at once multiply the damage. A burning Lara can be re-ignited by a dormant fire again whenever its counter has run out
and she is near.

### 15.3 Lava burn (the rule for lava floors and the lava wedge)

* It does nothing in the fly cheat state, or if her hit points are already below 0, or if the floor under her (read
  at y = 32000, the real room floor, ignoring bridges and other objects) is not her recorded floor height.
* Otherwise her hit points become -1, the hit flag is set and **10 flame effects** are created in her room, each with
  a random sprite frame and a counter of -1 to -25 chosen at random, so each flame attaches to a joint numbered 0 to
  24. Lara has joints 0 to 14; joint numbers 15 to 24 have no defined position (the original reads past the end of its
  joint table). Implementations should treat them as any of Lara's body parts. Her death then proceeds
  as for any hit points of 0 or below.

A lava floor entry on a sector applies this rule when Lara's origin is exactly on that sector's floor, in ordinary
(non-heavy) trigger evaluation.

## 16. Lava emitter (177), lava particles (176) and lava wedge (180)

### 16.1 Lava emitter

An invisible item that needs no trigger once active (it never tests its trigger). Each frame it makes one lava
particle at its position with: a random heading over the full circle, horizontal speed random 0 to 31, fall speed
-(0 to 163) (thrown upward), sprite frame 0 to -4 at random, and plays sound 149. Collision: standard solid.

### 16.2 Lava particle each frame

It moves horizontally by its speed along its heading; its fall speed rises by 6 per frame; y changes by the fall
speed. Then:

* If its y is at or below the floor or above the ceiling at its place, it is removed.
* Otherwise if Lara is near within 200: she loses **10** hit points, the hit flag is set and the particle is removed.
* Otherwise it keeps flying and updates its room.

### 16.3 Lava wedge (180)

A block of lava that slides.

* While its status is not deactivated it advances **25 units per frame** in its facing direction (0°: +z, 180°: -z, 90°:
  +x, otherwise -x) and then probes the floor 2048 ahead of its new position at its own y: if that floor height differs
  from the wedge's y, it stops (status deactivated).
* In the fly cheat state its touch state is cleared.
* While it touches Lara (a standard solid collision with touch): if her hit points are above 0, lava burn (15.3)
  applies, so she dies; and the camera is forced to a fixed chase view on the wedge with target angle 180° and distance
  3072.
* Save: position, animation, flags.

Can they block completing a level if they do nothing? No [INFERENCE].

## 17. Earthquake (object 183)

An invisible item that shakes the camera while triggered. Each frame while triggered: with probability 256/32768 (the
drawing random sequence below 0x100) the camera bounce becomes -150 and sound 147 plays globally; otherwise, with probability
1024/32768 (the logic random sequence below 0x400) the bounce becomes +50 and sound 70 plays globally (differs from
OpenLara, which swaps the two sounds). No damage, no floor change. Bounce is defined in `glossary.md`. The separate
flip effect "earthquake" is in `triggers-and-effects.md` section 8.2.

Save: flags only. Can it block completing a level if it does nothing? No.

## 18. Cabin (object 162, Natla's Mines)

A solid item (standard solid collision) with five states: 0 start, 1 drop 1, 2 drop 2, 3 drop 3, 4 finish. Each time
its mask is complete (a trigger completed it): in state 0 the goal becomes 1, in state 1 it becomes 2, in state 2 it
becomes 3, and its flags are then cleared to 0 (whether or not a state matched), so each completion of the mask advances the
cabin by exactly one stage. When it reaches state 4: flip-map slot 3 is set to a complete mask, the map is flipped (see
`triggers-and-effects.md` 5.1) and the cabin is removed. It is drawn without view clipping. Save: animation and flags.

Observable: three trigger firings lower the cabin in three steps; after the last animation the map swaps to the
alternative rooms and the cabin disappears. Can it block completing a level if it does nothing? Probably, if the
flipped rooms contain the way on [INFERENCE].

## 19. Boat (object 182)

A non-interactive object that plays its animation once. Each frame: in state 1 (set) the goal becomes 2 (move); in
state 2 the goal becomes 3 (stop); in state 3 the item is removed; state 0 (empty) does nothing. Its movement comes from
its animation. Save: position, animation, flags. It is not ridable and Lara cannot collide with it. Can it block
completing a level if it does nothing? No [INFERENCE].

## 20. Skateboard (object 29)

Not an item of its own: it is drawn with the skate kid (object 30, "mercenary 1"). When the kid is drawn, the game draws
the kid's model and then the skateboard model at the same position, with the kid's animation re-based onto the skateboard's
animations. See `lara-and-enemies.md` 4.12.

## 21. Mutant eggs / pods (objects 163 and 181) and the centaur statue (161)

### 21.1 Pod (163) and big pod (181)

At level start the pod creates a hidden creature at its own position and facing, flagged invisible, and remembers it. The
creature type is selected by the pod's item flag mask bits (bits 9 to 13 of the item flags, counted as a number from 0):

| Mask bits value | Creature |
|---|---|
| 1 | Warrior 2 (object 21) |
| 2 | Centaur (23) |
| 4 | Abortion, the giant mutant (34) |
| 8 | Warrior 3 (22) |
| anything else (0, 16...) | Warrior 1, the flying mutant (20) |

The pod's own flags are then cleared (the mask bits no longer count as a mask) and its visible meshes are set to meshes 0
to 8 and 24 to 31.

The pod runs only while active (a trigger starts it). Each frame until it has exploded, it explodes when: its one-shot flag
is set; or it is a big pod (at once); or Lara is within 4096 on all three axes. Exploding: goal state 1, all meshes 0 to 23 made
visible, not collidable, meshes 9 to 23 thrown out as body parts (section 22, no damage), meshes 0 to 8 stay; the hidden
creature's touch state is cleared, it becomes active and its AI starts: active if a creature slot is free, otherwise it stays invisible
until one is.

The pod is a standard solid collision. After loading a save in which the pod's status is deactivated, its visible meshes are 0 to 8
and it is not collidable.

Can it block completing a level if it does nothing? Only if killing the creature is required [INFERENCE].

### 21.2 Centaur statue (161)

At level start the statue creates a hidden centaur (object 23) at its position and facing (flagged invisible), set to the centaur's
animation 7, frame 36 (the statue pose), and remembers it. Each frame: if Lara's y is strictly within ±1024 of the statue's y and
her horizontal distance (a circle) is under **3584**, the statue explodes: all its meshes are thrown out as body parts with no
damage, the statue is removed, the sound 104 plays at the centaur, and the hidden centaur is activated (always-awake, status
active). Before that the statue is a standard solid collision. Save: animation and flags.

Can it block completing a level if it does nothing? No, unless the centaur must be killed [INFERENCE].

## 22. Body parts and exploding death (used above)

Exploding death (mesh mask, damage): for every mesh that is both selected by the mask and still visible on the item, one body
part effect is made at that mesh's world position and the mesh is hidden. A body part has: a random heading over the full
circle, a horizontal speed random 0 to 127 (0 to 255 for the giant mutant), a negative random fall speed of the same
magnitude, and its stored damage; it shows that mesh. Each frame it spins (5° on x and 10° on z), moves, gravity adds 6 to its
fall speed, and it bounces off a ceiling (fall speed negated). When it reaches the floor: if its damage is not 0 it turns into an
explosion sprite with sound 104, otherwise it is removed. While flying, if Lara is near within twice its damage (glossary) she loses
that many hit points and the hit flag is set; if the damage is not 0 it becomes an explosion, the hit-reaction mark on her is set
for 5 frames with the particle as its source. With damage 0 (statue, pods) the parts are harmless.

## 23. Scion objects (143 to 147)

| Object | Number | Behaviour |
|---|---|---|
| Scion pickup (cinematic) | 143 | drawn as a sprite. The item is turned to Lara's facing. She must be in this zone of the item's frame: x -256 to 256, y 540 to 740, z -350 to -200, x rotation within ±10°, y and z rotation exactly equal. If she is already in the pickup state (39) and on the frame 44 frames after the animation's base frame, the item goes into the inventory with the pickup notice, becomes invisible and is removed from its room list, and the pickups count rises by 1. Otherwise, if Action is held, she is standing on the ground with weapons holstered: she is aligned to (0, 640, -310) relative to the item, plays the special pickup animation (animation 0 of the Lara-extra object; state and goal 39), her hands are occupied, and the camera is in cinematic mode with the cinematic frame counter at 0 and the cinematic position at her current position |
| Scion item 2 | 144 | an ordinary sprite pickup (like a key); saves flags |
| Scion item 3 | 145 | a shootable scion with **5** hit points; saves flags. It runs its animation while its hit points are above 0. When shot to 0 or below: at once it becomes invisible, its hit points become untargetable, the floor data of its sector is evaluated for **heavy** triggers (a level can hang its follow-up on this) and it is removed from drawing; then for **90 frames** (3 seconds), every 10 frames an explosion sprite appears near it (random offset ±256 on x and z, about 500 above the item) with sound 104, and the camera bounce becomes -200; after the 90 frames it is no longer active |
| Scion item 4 | 146 | the scion on an altar: it only animates; Lara standing on the ground with weapons holstered, Action held and in this zone of the item's frame: x -256 to 256, y 206 to 306, z -862 to -200, x rotation ±10°, y and z rotation exactly equal, is aligned to (0, 280, -407) relative to the item (differs from OpenLara, which uses -612), plays the same special pickup animation as item 143 with the camera in cinematic mode (the cinematic position's facing is Lara's turned 90° left). No inventory change happens |
| Scion holder | 147 | it animates; a standard solid collision; saves animation and flags |

Can scion 3 or 4 block completing a level if they do nothing? Yes [INFERENCE].

## 24. Save crystal (object 83)

This object's logic exists in the game but runs only when the game flow's "save crystals" option is on. What the PlayStation
retail crystal does is not known to this specification [INFERENCE: the PC levels do not appear to place it]; the behaviour
below is all that can be stated. With the option off, the crystal is invisible and does nothing. With it on: the crystal is
always active and animating; Lara standing on the ground with weapons holstered, Action held and passing the ordinary
pickup zone of section 25 opens the inventory in "save crystal" mode; if the player confirms, the crystal becomes invisible,
the game is saved to the chosen slot, the user settings are written and sound is played; if the player cancels, the
crystal stays.

## 25. Pickups, keys, puzzles

Pick-ups (items 84-94, 110-113, 126, 129-132, 141, 142, 144) are drawn as sprites and use the standard pick-up sequence. On the
ground a pickup needs Lara standing (state 2), weapons holstered, on the ground, Action held, and inside the pickup zone of
the item's frame: x -256 to 256, y -100 to 100, z -256 to 100, y rotation within ±10° (the item is turned to Lara's facing
first). She is aligned at (0, 0, -100) from the item and plays the pick-up animation (state 39, hands occupied). When her
animation reaches absolute frame 3443 the item goes into the inventory, the on-screen pick-up icon appears, the item becomes
invisible and is removed from its room, and the pickups counter rises by 1 (for the shotgun item her torso mesh is also replaced
with the shotgun torso). Underwater: the zone is ±512 on each axis and ±45° on each angle with the item pitched -25°; she is moved
to (0, -200, -350) from the item and the item is taken at absolute frame 2970. A pick-up trigger (`triggers-and-effects.md`) fires
once when a pickup item that is invisible (already taken) is found. Key holes and puzzle holes accept only the matching item (key
N for key hole N, puzzle N for puzzle hole N), with the zone x -200 to 200, y 0, z 312 to 512 and angles ±10°, ±30°, ±10°; a wrong choice
(or the right one while the hole is already used) makes Lara say "no" (sound 2) once per distinct position; a correct one removes
the item from the inventory, aligns her at (0, 0, 512 - 100 - 50) for a key or (0, 0, 512 - 100 - 85) for a puzzle, plays state 42
(use key) or 43 (use puzzle), and the hole's status becomes active; the puzzle hole turns into its "done" object (122 to 125) when
her animation reaches absolute frame 3372. A key hole is released as a trigger source when its status is active and her hands are
free (`triggers-and-effects.md` 3.2).

## 26. Summary: can it block level completion if it does nothing

| Object | Blocks completion if inert? | Basis |
|---|---|---|
| Drawbridge | yes | `drawbridge.md` (inferred) |
| Flat and tilted bridges | yes when they are the only way across | inferred |
| Moving block | yes in levels that need it | inferred |
| Rolling block | possible | inferred |
| Trapdoors | possible | inferred |
| Cabin | possible (map flip) | inferred |
| Midas hand | yes (gold bar) | inferred |
| Scions 1, 3, 4, holder | yes | inferred |
| Thor's hammer, lightning, lava wedge, darts, spikes, teeth, pendulum, balls, falling ceiling, Damocles sword, flames | not for completion, but they are hazards a faithful port must keep | inferred |
| Pods, centaur statue | no, unless the enemies must be killed | inferred |
| Earthquake, boat | no | inferred |
| Save crystal | no | inferred |
