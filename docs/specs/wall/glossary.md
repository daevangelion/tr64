# Glossary of game terms used by the wall specifications
Source project: Tomb1Main (GPL-3), commit 3b2664b
Files read (all under `src/`): `game/collide.c`, `game/camera.c`, `game/items.c`, `game/control.c`, `game/sphere.c`, `game/lara.c`, `game/lot.c`, `game/effects/*.c`, `global/{const,types}.h`
Reader: WallReader
Date: 2026-11-18

These are plain-word definitions of the game concepts that the other files in this directory rely on. Where a
definition says "differs from OpenLara", the original game and OpenLara's floating-point game disagree and the
original is the one described.

## Items and activity

* **Item**: a placed object of the level (enemy, trap, door, pickup, Lara herself...). It has a position, a facing,
  a room, an animation (animation number, frame, current state, goal state), a status and flags.
* **Status**: not active, active, deactivated, or invisible. "Deactivated" is what an item's animation leaves it in
  when the animation ends with the level's "deactivate" command; "invisible" is an item that is hidden and
  not running until something reveals it.
* **Active item**: an item that runs its own per-frame logic. Items become active by a trigger (see
  `triggers-and-effects.md` section 4) or by their own rules, and stop being active when a rule removes them.
  An object type that has no per-frame logic never becomes active.
* **Per-frame logic**: what an active item does each game frame, in the order the item list is kept (most recently
  activated first).
* **Current state / goal state**: the animation state the item is in and the state it asks for. The item changes
  state only on a frame where its current animation allows a change to that goal (the level's animation data
  lists the allowed frame ranges); until then the current state does not change.
* **Touch**: a frame in which Lara's body spheres intersect the item's mesh spheres (the item's "touched meshes"
  set is then non-empty for that frame, and each mesh of the item has one bit in it).
* **Hit flag**: a one-frame mark meaning "this creature or Lara was hurt on the previous frame"; it is cleared at
  the start of the item's next animation step.
* **Untargetable**: the value -16384 stored as an item's hit points to mean that nothing can hurt or target it
  (used for dead or disabled creatures and for Natla's invulnerable period).
* **Creature slot**: the game keeps at most 8 awake enemies at once; each awake enemy owns one slot. See
  `lara-and-enemies.md` section 3.

## Lara's weapon and hand states

* **Weapons holstered**: no weapon is drawn and her hands are free.
* **Weapons drawn**: a weapon is out and ready to fire (drawing and putting away are the two transitions).
* **Hands occupied**: her hands are in use by a scripted action (pushing a block, picking something up, using a
  key or switch); weapons cannot be drawn then.

## Standard solid collision

Many items are solid in the same way (doors, the raised drawbridge, switches, blocks, pods, the cabin, the
moving bar, big enemy bodies...). The rule, applied when Lara is within 4096 units on every axis of an item that is
visible and collidable, is:

1. Lara's vertical extent must overlap the item's current bounding box in height.
2. Her horizontal position, expressed in the item's own rotated frame, must lie inside the item's bounding box
   (for the current animation frame) widened by Lara's radius (100) on every side.
3. Her body spheres must touch the item's spheres (this sets the item's "touch" state). Some rules (for
   example the Thor's hammer handle) skip this step.
4. If Lara's collision probe has body-push enabled (it is enabled while she is walking, running, standing and
   so on), she is moved out of the widened box along whichever of the four faces needs the smallest move.
5. After the move her new position is tested: with her height 762 and the direction she moved in, the move is
   accepted only if the floor there is not more than 384 above her feet (there is no limit downward) and the
   ceiling is not lower than the top of her head, and she has not hit a wall; otherwise she is put back at the
   position she had before the push.

Some items use a variant in which the push also triggers a **hit reaction** (see below): the raised drawbridge
and doors while they are moving, balls, the creatures.

## Hit reaction

Lara can be "bumped" by something. The bump records a direction and a frame counter. The counter rises by 1 per
frame of bump up to 34 and returns to 0 on a frame without any bump; on the first frame (counter 0) the body-slap
sound (sound number 27) is played at Lara. The direction is a whole number computed from where the source is:
let the angle `a` be Lara's facing minus the direction from her to the source, as a signed angle between
-180° and +180°; the direction is `(a + 45°) / 90°` with the division truncating toward zero. That gives 0 when
the source is between 135° to her right and 45° to her left (this includes straight ahead and her whole right
side), 1 when it is 45° to 135° to her left, 2 when it is 135° to 180° to her left, and -1 (which is also the
"no bump" value) when it is 135° to 180° to her right. The direction is consumed by the animation that shows the
bump. Lightning and some explosions use the same counter with their own direction rules.

## Camera bounce

The camera keeps one number called bounce. Positive: for one frame the camera and its target are moved down by
that many units, then bounce becomes 0. Negative: for each frame until it reaches 0 the camera and its target
are moved by a random amount of up to half the absolute value on every axis, and the bounce value then rises by 5
(so a bounce of -250 shakes for 50 frames, getting weaker linearly).

## Near test

"A point is near Lara within D" means: the point's horizontal distance from Lara is at most D on both axes and,
as a circle, at most D; its vertical distance is at most 3072; and its height lies between the top of Lara's
current bounding box and the bottom of that box plus 100.

## Random numbers

The game keeps two separate random sequences: one advanced by game logic and one advanced by drawing. Each is a
16-bit-style number in 0..32767. Rules that say "random chance" use the logic sequence unless the text says the
drawing one.

## Floor values

Floor and ceiling heights are in game units; larger numbers are lower. A sector stores its floor and ceiling in
clicks of 256. "Raising the floor by 1024" means the floor height value decreases by 1024 (4 clicks). "No floor"
(a wall) is the value -32512 as a height (-127 clicks).

## Sound numbers used in these files

| Sound | Number | Sound | Number |
|---|---|---|---|
| Lara says no | 2 | Lara ricochet | 10 |
| Body slap | 27 | Lara injury | 31 |
| Splash | 33 | Bubbles | 37 |
| Bullet hit | 50 | T-rex footstomp | 70 |
| Big waterfall | 81 | Thunder | 98 |
| Explosion | 99 | Sword landing | 103 |
| Atlantean explode | 104 | Raising block | 117 |
| Sand | 118 | Stairs to slope | 119 |
| Atlantean needle | 123 | Rolling ball | 147 |
| Lava fountain | 149 | Fire | 150 |
| Darts | 151 | Power up | 155 |
| Trapdoor open | 161 | Explosion effect | 170 |
| Atlantean death | 171 | Secret | 173 |

## Trigger terms

"Mask", "one-shot", "timer", "heavy" and the rest are defined in `triggers-and-effects.md`.
