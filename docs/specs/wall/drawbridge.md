# Behaviour specification: the drawbridge (object 41)
Source project: Tomb1Main (GPL-3), commit 3b2664b
Files read (all under `src/`): `game/objects/bridge.c`, `game/objects/cog.c`, `game/objects/door.c` (for contrast only), `game/control.c` (floor-height, ceiling-height, trigger, item-animation and item-activation parts), `game/collide.c` (object push-out and probe parts), `game/box.c` (creature probe height), `game/items.c`, `game/savegame.c` (item save part), `global/const.h`, `global/types.h` (enumerations of floor data, trigger types and actions)
Reader: WallReader
Date: 2026-11-18

This file describes observable behaviour only. Numbers are facts of the game; "state 0" and "state 1" are the
animation state numbers stored in the level's animation data. Terms (active, standard solid collision, hit
reaction, touch) are in `glossary.md`. Where the original game and OpenLara's floating-point game disagree, the
original is described and the line says "differs from OpenLara".

## 1. What the object is

The drawbridge is the hinged bridge of Obelisk of Khamoon (object 41). It is a normal level item with a mesh and a
two-state animation, no health, and it never moves its position: only its meshes swing, through the level's own
animation data. Its position, height and facing never change.

Everything it does depends on its **current animation state**, not on the trigger directly: the goal state follows
the trigger (section 3), and the floor, ceiling and solidity follow the state the animation is actually in
(sections 4 and 5). This differs from OpenLara, whose bridge follows the trigger state.

## 2. Animation states

| State | Meaning | Floor and ceiling | Solid to Lara |
|---|---|---|---|
| 0 | Raised | none | yes |
| 1 | Lowered (walkable) | yes (section 4) | no |

At level start the item is on the first frame of the object's first animation with its goal state equal to its current
state. Which state number that animation carries is a fact of the level data; the level data is expected to make it
state 0 (raised) [INFERENCE: not stated by the source].

The level's animation data (lengths of the lowering and raising animations, the frame ranges in which a change of goal
state may take effect, sound commands) is used unchanged. When the goal state differs from the current state, the item
switches to the linked animation only on a frame inside one of the allowed ranges of the current animation; until it
does, its current state does not change. Therefore: the bridge becomes walkable only when the lowering animation has
handed over to the state 1 animation; it becomes solid again when the raising animation hands over to state 0, not when
the trigger is released. The sounds of lowering and raising are animation commands of the level data; the logic adds
none.

## 3. Activation

The item is active from the first time a trigger completes its mask (`triggers-and-effects.md` section 4), and stays
active for the rest of the level. Each frame while active: the goal state is 1 (lower) while the item is triggered and
0 (raise) otherwise ("triggered" is the common test of `triggers-and-effects.md` section 4, including its timer and
reverse rules); then the animation advances one frame. Its position never changes, so its room never changes.

There is no delay, hysteresis or extra sound. A drawbridge on a timed trigger lowers when the trigger fires, stays
lowered for the timer and then returns to goal 0; a drawbridge on a switch follows the switch, subject only to the
animation's change windows.

If the level's animation data contains the "deactivate" command it applies as for any item (`triggers-and-effects.md`
section 11).

An item placed with all five mask bits set in its level flags starts active with the reverse flag set, so such a bridge
starts lowered (goal 1) and rises when its mask is completed; this is the general rule for all items.

## 4. Floor and ceiling

### 4.1 Which sectors

The drawbridge influences a height query on a sector only if the floor data of that sector has a trigger entry that
lists the drawbridge item as an object action. The level designer put such an entry on each sector of the deck; the
game does not look for the item geometrically. Every query through such a sector (Lara, enemies, items, camera, line
of sight) is affected alike.

For a sector with the entry, the drawbridge applies its rule only if the queried position lies in one of the two
sectors directly behind the item's own sector, given by its facing. Sector indices are the world coordinates divided by
1024, rounded down, independent of the room's offset. With `Sx`, `Sz` the item's sector indices and `Qx`, `Qz` the query's:

| Item facing | Sectors covered |
|---|---|
| 0° | `Qx = Sx` and `Qz = Sz - 1` or `Sz - 2` |
| 90° (16384) | `Qz = Sz` and `Qx = Sx - 1` or `Sx - 2` |
| 180° (32768, which is -32768 as a signed 16-bit angle) | `Qx = Sx` and `Qz = Sz + 1` or `Sz + 2` |
| 270° (49152, -16384 as a signed angle) | `Qz = Sz` and `Qx = Sx + 1` or `Sx + 2` |
| any other angle | no sector: the object contributes nothing |

So the deck lies on the two sectors from the item's own sector toward decreasing z (0°), decreasing x (90°),
increasing z (180°) or increasing x (270°). The item's own sector is not covered. The bridge is 2048 units long.

### 4.2 Floor

Only while the item's current state is 1 and only for a covered sector: if the query height `y` (larger is lower) is at
or above the bridge height (`y <= Y`, with `Y` the item's y), the floor returned for that sector is exactly `Y`. A query
below that (`y > Y`) is unchanged. The replacement does not look at the underlying floor: it replaces the computed
height whether that was higher or lower. In state 0 nothing changes. The sector's slope entry (if any) has already been
applied when this happens; within a trigger list objects apply in list order and the last one wins. The reported height
type (wall, small slope, big slope) is not altered.

### 4.3 Ceiling

Under the same conditions: if `y > Y` (the query is below the bridge top) the ceiling returned is `Y + 256`, so the
deck is a slab 256 thick (differs from OpenLara, whose deck underside is `Y + 64`). A query at or above the top does
not change the ceiling.

### 4.4 Probe heights and tolerance

The floor and ceiling rule has **no tolerance of its own** (differs from OpenLara, which adds a 128-unit tolerance for
floor objects). What matters is the height `y` the caller asks with:

* Lara's collision probes (ground movement: middle, front, left and right) ask at 922 above her feet (her height 762 plus
  160); her floor-and-room refresh each frame asks at 381 above her feet. So for the collision probes the deck is read
  as floor whenever its top is no more than 922 above her feet (`y <= Y` with `y` = her feet minus 922), and for the
  refresh when it is no more than 381 above them.
* Creatures ask at the top of their bounding box.

Observable result: with the bridge lowered, Lara walking on it reads floor = `Y`; a probe from below reads a ceiling
256 below `Y`; with the bridge raised both sectors use their own geometry (in the level, a pit, so Lara falls in).

## 5. Collision

While the current state is 0 the drawbridge is a standard solid collision (`glossary.md`) and, while it is moving (its
current and goal states differ), the push also gives Lara the hit reaction. No damage is done in either case. While the
current state is 1 it does nothing: Lara walks over it because it provides a floor.

Because the current state is what counts, a bridge that is lowering is still solid until the state 1 animation takes over,
and one that is rising is walkable (floor and ceiling present) until the state 0 animation takes over.

## 6. Save and load

Saved for the item: current state, goal state, required state, animation number, frame number, its flag bits
(including one-shot, mask and reverse), whether it is active, its status, its gravity flag, whether it is collidable,
and its timer. The position is not saved because it never changes. On load these are restored, and an item that was
active is put back in the active list. A drawbridge saved lowered reloads lowered (state 1, same frame) and provides
floor and ceiling at once; saved raised, it reloads raised and solid.

## 7. Can it block completing a level

Probably yes [INFERENCE: not stated by the source]: a level that uses it as the only way across a pit cannot be finished if
it never provides a floor.

## 8. Related objects

The flat bridge and the two tilted bridge pieces (objects 68, 69, 70) use the same mechanism (floor and ceiling rules
consulted through trigger entries) and are in `objects-missing.md`.
