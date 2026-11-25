# Behaviour specification: Lara's rules and the enemies
Source project: Tomb1Main (GPL-3), commit 3b2664b
Files read (all under `src/`): `game/ai/*.c` (wolf, bear, bat, crocodile, alligator, lion, ape, rat, vole, dino, raptor, larson, pierre, baldy, cowboy, skate_kid, mutant, natla, centaur, mummy, abortion, evil_lara, pod, statue) and their headers, `game/box.c`, `game/lot.c`, `game/people.c`, `game/larafire.c`, `game/lara1gun.c`, `game/lara2gun.c` (fire timing), `game/laramisc.c`, `game/lara.c` (landing and fall damage), `game/laraswim.c`, `game/game.c` (level statistics), `game/gameflow.c` (secret counting), `game/invfunc.c` (ammunition on pickup), `game/effects/{missile,body_part,blood}.c`, `game/traps/lava.c`, `game/items.c`, `game/savegame.c`, `global/{const,types}.h`
Reader: WallReader
Date: 2026-11-18

Units: lengths in game units (1024 per sector); angles as fractions of a
circle (65536 = 360°, 182 ≈ 1°); distances written "range 1536" are real
distances (the original compares squared distances, a detail that matters only
for rounding). All durations are in frames (30 per second). Hit points are
integers; all damage subtracts from them every time it is applied. Plain-word terms (hit flag, untargetable, creature
slot, hands occupied, weapons holstered/drawn, hit reaction, near test) are defined in `glossary.md`.

## 1. Lara's constants

| Quantity | Value |
|---|---|
| Hit points | 1000 (maximum; also the value she starts every level with in the original game) |
| Air | 1800 frames (60 s) |
| Collision radius / height | 100 / 762 |
| Death delay | 300 frames before the death menu is forced; the menu can be opened after 60 frames if a key is pressed |
| Hit points that she "has" in the dead state | clamped to -1 when she is at or below 0 |

### 1.1 Death handling

Each frame, if her hit points are 0 or below they are set to -1; on the first such
frame the music is stopped; her death counter increases each frame. She stays
dead (no actions) until the player acts on the death menu. A player-triggered
restart reloads the level or a save; there is no automatic resurrection.

### 1.2 Fall damage

On landing from a fall (landing out of the falling, jumping and fast-fall states,
but **not** when entering water) the fall speed `v` at that frame (units per frame
downward; it grows by 6 per frame until it passes 128 and then by 1 per frame) gives:

* `v <= 140`: no damage;
* let `e = v - 140`: if `e > 14` (that is, `v >= 155`) she dies: her hit points become -1 and she
  goes to her death state;
* otherwise she loses `1000 * e * e / 196` hit points (integer division) with `e` from
  1 to 14 (for instance `e = 1` costs 5, `e = 7` costs 250, `e = 10` costs 510,
  `e = 14` costs 1000); if her hit points are then below 0 she dies as above; otherwise she survives the landing
  (and stands or runs on according to the input). With `e = 14` she is left at exactly 0 hit points: the landing is not
  the death landing, but the death rule of 1.1 (0 or below is dead) kills her on the next frame.

The landing also evaluates the floor trigger under her at her landing height. The
fast-fall state (state 9) is what she is in for the long falls; fast fall begins at fall speed 131.

### 1.3 Water, air and drowning

* Entering a room flagged as water from above changes her to the underwater
  state and sets her air to 1800; she dives (entering in a swan dive: goal dive, nose angle -45° and fall speed doubled; entering in a fast dive:
  -85° and fall speed doubled; otherwise she plunges at -45° with fall speed 1.5×) and a splash is made.
* Air: each frame underwater it drops by 1. When it is below 0, it stays at -1 and she
  loses **5 hit points every frame** (drowning: 1000 hit points last 200 frames after
  the air runs out). On the surface air rises by 10 per frame up to 1800; out of water it
  is reset to 1800.
* Leaving water: see 1.3.1.

### 1.3.1 Water transitions and Lara's pitch and roll (amendment)

Lara has three water states: above water (this includes walking and standing; the game has **no wading state and no shallow-water
state**: a room is either a water room or it is not), underwater, and on the surface. Pitch is her x rotation (nose up is
negative) and roll is her z rotation. Water state changes are decided once per frame, before her movement, from whether the room she is
in is flagged as water:

| Transition | When | Pitch | Roll | Other effects |
|---|---|---|---|---|
| Above water to underwater | she is above water and her room is a water room | swan dive: -45°; fast dive: -85°; any other state: -45°; (in the first two cases her fall speed is doubled, otherwise multiplied by 1.5) | unchanged (it is whatever it was; she has no roll on land) | air set to 1800; splash; head and torso turn reset |
| Underwater to surface | she is underwater, her room is **no longer** a water room, and the water surface height at her position is defined and within 256 of her y (|surface - y| < 256) | **set to 0** | **set to 0** | y set to the surface plus 1; state and goal "surface tread", animation at its first frame; fall speed 0; dive counter set to 11 (so holding jump through the transition does not dive again until jump has been released once); head and torso turns reset; breath sound |
| Underwater to above water (the same moment, the surface is not within 256, or no water height is defined) | she is underwater and her room is no longer a water room | **set to 0** | **set to 0** | she goes to the forward-jump fall: speed becomes her fall speed divided by 4, fall speed 0, gravity on; weapons holstered |
| Surface to above water by leaving the water room | she is on the surface and her room is no longer a water room | **set to 0** | **set to 0** | same forward-jump fall as the previous row |
| Surface to underwater, by diving | with the surface-tread state current, jump is held for 10 consecutive frames (the dive counter counts up while jump is held and returns to 0 when it is released; it only dives when the count equals exactly 10) | **-45°** | unchanged (it is 0: see below) | state "dive" with the surface-dive animation, goal "swim"; fall speed 80 |
| Surface to underwater, by sinking | on the surface, her y is 100 or more below the water surface after the surface collision step | **-45°** | unchanged (0) | the same dive state, animation and fall speed 80 |
| Surface to above water by climbing out | she faces within 35° of a wall direction and her facing equals her move direction, she touches a wall in front, Action is held, the two side floors differ by less than 60, the front ceiling is not above her, the middle ceiling is at least 384 above her, and the front floor plus 700 is between -512 and +100 | **set to 0** | **set to 0** | her facing is snapped to the nearest of the four axis directions; y moves by that height difference minus 5; she is put 100 outside the sector edge in front (sector start plus 1024 plus 100 for +z and +x, minus 100 for -z and -x); state "water out" (55) with the climb animation, goal stand; hands occupied; gravity off, speed and fall speed 0 |
| Above water to surface | does not exist (she always enters a water room underwater and comes up through the first two rows) | - | - | - |
| Wading to surface, surface to wading | do not exist in this game | - | - | - |

Notes:

* **Same moment, different outcomes.** There is only one way to go from underwater to the surface state: the room she is in stops being a water
  room (her position has crossed into the room above, which is an air room) and the surface height read for her position is within 256. There is
  no separate case "still inside a water room": while her room is a water room she stays underwater whatever her height, even at the surface
  level. In every case of leaving the water by room change (surface state or fall) pitch and roll are set to exactly 0.
* **Surface states do not change pitch.** The surface-tread, surface-swim, surface-back, surface-left and surface-right states only change her facing
  (turn per frame: tread and swim 728 angle units, about 4°; back, left and right 364, about 2°) and her speed (swim and the side/back states
  speed up by 8 per frame to a maximum of 60; tread slows by 4 per frame). Her pitch stays at the 0 it was given on entry. Her roll is never
  increased on the surface; each frame it is pulled to 0: if it is within ±364 it becomes 0, otherwise it moves 364 toward 0.
* **Underwater states do change pitch and roll.** In the swim, glide and tread states, Forward lowers the pitch by 2° per frame and Back raises it by 2°
  per frame (the swim state lets both act together); Left and Right turn her by 1092 angle units (6°) per frame and change roll by 546 per frame
  toward the turn; after the state's change, each frame the roll is reduced by 364 toward 0 and then clamped to ±4004 (about ±22°), and the pitch is clamped to ±100° (±18200).
  In the dive state, while input bit 1 is held (this is the Forward control [INFERENCE]), pitch decreases by 1° per frame (nose up). In the underwater death state the pitch
  returns to 0 at 2° per frame. Colliding with the floor or ceiling while swimming also changes the pitch by 2° per frame (the swimming collision).

### 1.4 Fire, lava and other special deaths (full numbers in `objects-missing.md`)

* Lava floor (or the lava wedge): if she is on the real floor, her hit points become -1 and
  10 flames attach to her (section 15 of `objects-missing.md`).
* A burning Lara (a flame effect on her) loses 5 hit points per flame per frame until she is in water.
* Dinosaur bite, giant mutant grab, Midas touch, Thor's hammer, rolling ball,
  spikes (when she falls onto them): scripted deaths by an animation with her hit points set to -1.
* Cheat and fly states (the "flying" water status) make her immune to fire and lava
  and restore her health every frame; they are not part of the normal game.

### 1.5 Medikits and health

* Small medikit: +500 hit points (half of the maximum), never above 1000.
* Large medikit: +1000 (so full health).
* Neither can be used when her hit points are 0 or below, or already 1000. Using one
  removes it from the inventory.
* Hit points are not restored between levels by the game (each level starts with
  them at 1000 in the original; the inventory items carry over).

### 1.6 Starting inventory of a level

Each level starts with the inventory recorded at the end of the previous level
(the "start" record of the level): pistols (infinite ammunition), shotgun,
magnums, uzis with their ammunition counts, number of small and large medikits,
number of scion pieces, and weapon state (drawn or holstered, which weapon). A
weapon that she already has at the start of a level causes every **pickup of that
weapon in the level to be turned into the matching ammunition pickup** (so
picking the same gun up again gives ammo only).

## 2. Weapons

| Weapon | Damage per hit | Aim spread | Cycle (frames between shots, per arm) | Ammunition per shot | Pickup gives |
|---|---|---|---|---|---|
| Pistols | 1 | ±2° | 10 | none (infinite; her pistol ammo count is reset to 1000) | - |
| Magnums | 2 | ±2° | 10 | 1 per gun | 50 rounds per magnum clip or gun |
| Uzis | 1 | ±2° | 4 | 1 per gun | 100 rounds |
| Shotgun | 4 per pellet, 6 pellets per shot | ±5° per pellet | 26 updates between shells if not locked and Action stays held; 33 if locked. See the cadence below. | 6 (one per pellet) | 12 per box (two shells of 6) |

* A bullet picks the nearest of the target's body spheres that lies on the shot line;
  the damage applies to the creature as a whole; a miss makes a ricochet where it
  hits the world.
* Both pistol-type arms fire on the same frame (each costs its own ammunition) while
  Action is held and the arm is aimed (arm frame 4); the recoil then lasts 9 frames for
  pistols and magnums and 3 for uzis.
* Aiming: Lara auto-targets the nearest-in-angle creature among **active** items with
  hit points above 0 that is within **8192** units (a cube test and a sphere test)
  and has line of sight from her point 650 above her feet to the creature's target
  point (the point one third down its bounding box). The target must be inside the
  lock cone: horizontally ±60° of her facing for every weapon; vertically ±60° for pistols, magnums and uzis and ±55° for the
  shotgun. The arms track it at 10° per frame; they can swing horizontally to -170°..+60° (left arm) and -60°..+170° (right arm)
  and vertically ±80° for the pistol-type weapons (shotgun: ±80° horizontally and ±65° vertically, both arms). If the target
  leaves the cone but is within an arm's limits, that arm keeps its lock.
* Every shot that kills a creature (its hit points were above 0 and are now 0 or
  below) increases the level's **kills** count by 1. Kills by traps, falls, bosses
  exploding or other creatures are not counted.
* A hit also makes a blood splat, and a creature-specific "hurt" sound for wolves, bears, lions and
  lionesses, rats, the skate kid and the giant mutant (Abortion).
* Picking up a gun adds the gun plus one clip (the quantity in the table) plus all loose ammunition of that
  type she holds; picking up ammunition while holding the gun adds directly; without the gun it is kept as a loose item.
* Ammunition units: the counts in the table are the game's internal counts. The on-screen counter shows the internal count for
  magnums (50 per clip) and uzis (100 per clip), and the internal count divided by 6 for the shotgun (so 2 per box).
  OpenLara's counts are 25 (magnums), 50 (uzis) and 2 (shotgun) per pickup: this differs from OpenLara in the internal units
  of magnums and uzis, and agrees on the shotgun's displayed number.
* The shotgun's six pellets all use Lara's current target; with no target they all miss.

**Established statically — shotgun cadence.** Read from the retail first-game program this specification already uses. There is no first-game recorder. A shell is issued only when the shared arm frame is exactly 47 and Action is held. The left-arm lock chooses the recoil. Releasing Action clears the current target, but a new target is still sought, so a creature still inside the lock cone keeps the lock.

Locked, Action held: after the shell the frame steps through 48–79. The step that would reach 80 snaps back to 47, and the next update fires if Action is still held. That is 33 updates between shells. The gun does not return to aim between them. Releasing Action during this recoil does not interrupt it. The full recoil still ends at 47, and the gun waits there without firing until Action is held again.

No target, Action held: the recoil steps through 48–59. The step that would reach 60 snaps to aim, frame 0. That return is 12 updates after the update that fired and left the frame at 48, not after 26 frames. If Action is still held, frames 1–12 then run, the step that would reach 13 snaps to 47, and the next update fires. The shot-to-shot interval is 26 updates.

Action released during recoil, no lock: if the frame is already past 47, the short recoil still runs to the frame-60 exit and returns to aim. That exit does not test Action. If Action is up when the frame is 47, the gun does not fire and jumps to frame 114. That sequence walks through 126; the step that would reach 127 starts the holster at frame 80. It does not return to aim from 127.

The pump sound plays on the recoil step that sets the frame to 57, whether the lock is held or not. Both recoils pass through 57 before their exit.

Retail does not return to aim after 26 frames in every case. Only the unlocked cycle returns to aim between shells, and it does so at the frame-60 exit. The locked cycle never returns to aim between shells.

**Established statically — shotgun life cycle.** Read from the same retail program. There is no first-game recorder. The two games' level files use the same clip frame ranges, so a frame number below is the same authored frame the second game names by clip and relative frame. The pose itself is not an item. Both arms share one frame counter, and every update writes that number to both. There is no temporary weapon-pose slot.

| Stage | Established outcome |
|---|---|
| Select and draw | A draw request while the hands are free and the shotgun is selected sets both frames to 0 and starts the draw. The first draw update samples frame 13, the start of the draw clip, not frame 14. Frame 23 moves the shotgun meshes onto both hands and restores Lara's torso mesh, so the gun leaves her back, and plays the draw sample. Frames 13 through 46 are the draw. The update that would reach 47 does not sample 47: it marks the gun ready, clears both arm angles, both locks, the target, and the head and torso angles, and stores aim frame 0. |
| What carries the pose | The shared arm frame counter, against the shotgun's own frame base. Both arms show that same frame. Lara's body animation is independent of it. |
| Aim | The left arm is the only arm that is aimed, at 10° per frame, within ±80° of yaw and ±65° of pitch. The right arm keeps the angles it was given at ready, which are zero, and does not copy the left. The lock cone is ±60° of yaw and ±55° of pitch, out to 8192, the same cone as the second game. While the left arm is locked, the torso yaw is half the left-arm yaw and the torso pitch is half the left-arm pitch, and the head yaw and pitch are cleared. There is no averaging of two arms. |
| Raise and lower | From aim, Action advances the frame. Frames 1 through 12 are the raise. The step that would reach 13 snaps to 47 if Action is still held, or to 114 if it is not. Entering 47 does not fire; the next update fires if Action is held. Releasing Action at aim does not by itself lower the gun. A holster request does. |
| Fire test | A shell is issued only when the shared frame is exactly 47 and Action is held. The fire update itself stores frame 48. It does not leave the frame at 47. |
| Pellets | Six pellets, in order. The aim direction is Lara's body yaw plus the left-arm yaw, and the left-arm pitch, with no body pitch or roll. For each pellet the gameplay stream supplies a yaw offset, then a pitch offset: subtract 16384 from the draw, multiply by 3640, divide by 65536 truncating toward zero. The offset is −910…909. The ray is then built from that direction and consumes two further gameplay draws, pitch then yaw, multiplied by a spread of zero, so those two draws do not change the direction. A full shell therefore takes 24 gameplay draws. The origin is 500 above her feet. Each pellet that still has a reserve unit spends one unit before the ray. A sphere hit deals 4, increments the shotgun's hit count, and can credit one kill when health crosses to 0 or below. A miss increments the shotgun's miss count, clips the line of sight, and places a ricochet. The 8192 figure is the lock distance; this program does not apply a separate 8192 pellet range. With no target every pellet misses. |
| Flash | The fire update does not store a flash count on either arm. It plays the shotgun's fire sample if any pellet returned a shot, including a miss. An empty pellet does not count as a shot. |
| Recoil | The left-arm lock chooses the path. Locked, the frame steps through 48–79. The step that would reach 80 stores 47. Action is not tested during that advance. The next update fires if Action is held. That is 33 updates between shells, and the gun does not return to aim between them. Unlocked, with Action held or released, the frame steps through 48–59. The step that would reach 60 stores aim 0. That exit does not test Action. It is not a 26-frame counter. If Action is still held, frames 1–12 then run, the step that would reach 13 stores 47, and the next update fires. The unlocked interval is 26 updates only because the re-raise is included. |
| Pump | The update that sets the frame to 57 plays the pump sample, on both the locked and unlocked paths. Both pass 57 before their exit. That is ten steps after fire frame 47, not a separate timer. |
| Empty gun | The empty test is inside the pellet loop, not before the raise. A pellet that finds no reserve plays the empty click, forces the reserve to 0, and requests the pistols. It does not consult whether the pistols are carried. The fire update still stores 48. The next gun update, seeing a different gun requested while ready, starts the holster. Once the hands are free, the requested pistols draw. Six empty pellets on a dry shell play the empty click six times. |
| Holster and stow | A Draw request, a requested weapon change, or being ready while no longer above water starts the holster from ready. From aim the first holster update stores frame 80. Frames 80 through 113 are the holster. Frame 101 restores Lara's hand meshes and puts the shotgun mesh on her torso, so the gun is back, and plays the draw sample again. The update that would reach 114 stores aim 0, clears the target and both locks, and marks the hands free. It does not keep frame 113. A holster that finds the frame already at 47 jumps to 114 without firing. A holster that finds the frame already past 47 does not replace the pose: the recoil continues to the same frame-60 exit, which stores aim 0, and the holster then proceeds from there. From 114 the sequence walks through 126; the step that would reach 127 stores 80 and the holster runs as above. |
| Ammunition | The reserve is in pellet units. A shell spends 6. A box adds 12, two shells. The on-screen counter shows the reserve divided by 6. |

**Established statically — one implementation, named differences.** The second game's rules are the recorded ones in [tr2-lara-arms.md](tr2-lara-arms.md#shotgun-draw-firing-and-stow). "Same" means an implementer can use one rule. A difference is either DATA, a frame range the two level files already share, or PROGRAM, a rule the two programs do not share. The unlocked recoil is one condition, not two mechanisms. Both programs exit to aim on the step that would reach absolute frame 60, and neither samples frame 60. The first game takes that exit whenever the lock is clear, Action held or not. The second game takes it only when Action is released; while Action stays held and the lock is clear, it runs the full recoil and fires again 33 updates later, which is the locked interval. That is the whole of the difference.

| Rule | First game | Second game | Kind |
|---|---|---|---|
| Pose carrier | Shared arm frame. Both arms show it. No pose item. | Pose item, both arms the same frame. Right angle is not copied. | PROGRAM |
| Clip ranges | Draw 13–46, recoil 47–79, holster 80–113, unaim 114–126. | The same ranges, named as clips 223, 224, 225 and the unaim band. | DATA, same |
| Draw start | First update samples 13. | First update samples relative 1, absolute 14. | PROGRAM |
| Mesh to the hands | Frame 23. | Relative 10, absolute 23. | SAME |
| Ready | The update that would reach 47 stores aim 0 and clears angles, locks and target. | Entering 222/0 does the same. | SAME moment |
| Which arm aims | Left arm, 10° per frame, ±80° yaw, ±65° pitch. | Left arm, the same speed and limits. | SAME |
| Torso and head when locked | Torso yaw and pitch are half the left-arm angles. Head cleared. | Torso copies the full left-arm angles. Head cleared. | PROGRAM, half against full |
| Lock cone | ±60° yaw, ±55° pitch, 8192. | The same. | SAME |
| Fire test | Frame 47 and Action. That update stores 48. | Start at absolute 47 and Action. That update samples absolute 48. | SAME |
| Pellets and spread | Six. Yaw then pitch, then two zero-spread draws. Gameplay stream. Origin 500. | The same order, stream, arithmetic and origin. | SAME |
| Damage | 4 per pellet. | 3 per pellet. | PROGRAM |
| Miss range | Line-of-sight clip and a ricochet. 8192 is only the lock distance. | A miss has range 8192. | PROGRAM |
| Fired and hit counts | Reserve minus one per spent pellet. Shotgun hit count, or miss count. | Level `ammoUsed` and `ammoHits`, and the reserve. | SAME counting, different fields |
| Flash | The fire update does not store an arm flash. | Right-arm flash starts at 3 and counts down. | PROGRAM |
| Locked recoil | Full clip, wrap at 80 to 47, 33 updates, no return to aim. | The same. | SAME |
| Unlocked, Action held | Frame-60 exit, then the re-raise. 26 updates to the next shell. | Full recoil. 33 updates to the next shell. | PROGRAM, the condition below |
| Unlocked, Action released during recoil | Frame-60 exit. The exit does not test Action. | Frame-60 exit, seen as relative 12 then aim on the next update. | SAME exit. PROGRAM condition: the first game also takes it while Action is held; the second game takes it only on release |
| Pump | Program plays it when the frame becomes 57. | Recorded cue at absolute 52, relative 5. | PROGRAM test against an earlier DATA cue |
| Empty gun | Tested per pellet at the fire frame. Empty click, request pistols, frame still becomes 48, holster on the next update. | Tested before the raise. No pellet draws. Stow from the current pose, then pistols. | PROGRAM |
| Holster during recoil | Continues to the frame-60 exit, then unaim and holster. From 47, jump to 114 without firing. | Stowing is set, but the full recoil finishes before the lower clip and the stow. The gun stays selected. | PROGRAM |
| Mesh back | Frame 101. | 225/21, absolute 101. | SAME |
| Hands free | The update that would reach 114 stores aim 0 and clears the target and locks. | The update after 225/33 frees the pose slot and keeps 225/33. | PROGRAM |
| Ammunition units | 6 per shell, 12 per box, display is units divided by 6. | The same. | SAME |
| Camera while the gun is out | Combat, unless the camera is already look or cinematic. | No-target play stays in ordinary follow. | PROGRAM |

**Established statically — where a 26-frame right-arm counter is wrong.** Three points in the first-game code derived from the other engine do not match this program, beyond the locked cadence already stated.

The aim is not the right arm. The left arm carries the angle. The torso uses half of that yaw and half of that pitch. The head is cleared while locked. The right arm is shown at the same frame and is not aimed.

There is no 26-frame counter after every shot. A locked shot wraps at frame 80 and can fire again 33 updates later without returning to aim. An unlocked shot returns to aim on the step that would reach frame 60, twelve stored frames after the fire update has stored 48. The 26-update figure is only the unlocked interval from one shell to the next, and only because the re-raise is included. It is not a timer that runs after a locked shot.

The pump moment is not wrong if it is ten steps after fire frame 47: that step sets frame 57, and the program plays the pump then. It is wrong if that step is counted on a 26-frame timer that replaces the shared frame. The pump is the shared frame passing 57, which the full locked recoil also does.



## 3. Creature AI primer (the rules shared by every enemy)

Every enemy item ("intelligent" object) follows the same loop each frame while it is in
the active list. It needs a creature slot: at most **8 creatures are "awake" at once**. When a
ninth is activated: if the new creature is not "always awake", the awake creature farthest from the camera takes the slot, but only if
it is farther from the camera than the new one (otherwise the new one stays invisible and waits); if the new one
is "always awake" the farthest awake creature is replaced regardless. The replaced creature is put back to the invisible, inactive state. A creature whose
status is "invisible" waits until it can get a slot, then becomes
active. A creature that goes inactive gives up its slot.

### 3.1 Information computed first

* Its **zone** and Lara's zone (zones are level data of path-finding boxes: ground
  (two flavours depending on how far the creature can step), and flying; they are
  selected per creature and separately for the flipped and unflipped map).
* **distance**: the horizontal distance to Lara measured from a point shifted
  forward from the creature's origin by its "pivot length" (table below; 0 for those not listed), capped
  at 30720 on either axis.
* **angle**: the direction to Lara minus its facing; **ahead** is true if |angle| < 90°;
  **bite** is true if ahead and Lara's y is within ±256 of the creature's y.
* **enemy facing**: how Lara is facing relative to the line between them.

### 3.2 Mood

Mood is one of bored, attack, stalk, escape. A creature "violent" flag (listed per enemy) changes the
rules. Mood changes each frame in this order:

* If Lara is dead: bored.
* Violent creatures: attack -> bored if Lara's zone is not its zone; bored or stalk -> attack if same
  zone, or escape if it was hit this frame; escape -> attack if same zone.
* Non-violent creatures: attack -> escape if hit this frame and (random 1/16 chance (2048/32768) or different
  zones), else bored if different zones; bored/stalk -> escape if hit this frame and
  (random 1/16 or different zones); else if same zone: attack if the distance is under
  3072, or if stalk and it has no pending path target; otherwise stalk; escape -> stalk
  with probability 1/128 per frame (256/32768) while in the same zone.
* While **bored** it picks random boxes in its zone to wander to (stalk is entered if the
  chosen box is a "stalk" box: its centre within 3072 of Lara on both axes and not in the quadrant Lara
  is facing). While **stalk** it keeps picking such boxes. While
  **escape** it picks boxes at least 5120 from Lara on one axis and not on the opposite side of Lara from the creature on both axes
  (or switches to
  stalk). While **attack** it targets Lara's position and box with the probability
  equal to its "smartness" (a number out of 32768 per frame: 0x7FFF means always, 0x2000
  means one frame in four); other frames it keeps the previous target.
* It then moves toward the first waypoint along its box path (path search of up to 5
  box expansions per frame; boxes marked blocked by doors, blocks or enemies are avoided; a creature
  is not allowed to step up more than its "step" or drop more than its "drop":
  ground creatures 256/-256, wolves, lions, lionesses, pumas 256/-1024, apes 512/-1024, flyers
  (bats, alligators, fish) 20480/-20480 with a vertical speed of 16 per frame,
  flying mutants 30720/-30720 at 32 per frame, Natla in flight 20480/-20480 at 32; the dinosaur, warrior 1 and the centaur
  avoid any box that can ever be blocked by a door or movable block, even when it is currently free, everyone else only the boxes
  that are blocked right now).
* Each frame the creature then turns toward the target by at most its current turn
  rate (halved when the target is more than 90° off its facing and closer than `speed * 16384 / turn rate` units, so slow
  creatures turn sharply at close range), moves by its
  animation, is pushed out of walls by its radius, and does not enter a sector occupied
  by another moving active item within its radius (if it would end within its radius of another active, moving item in the same room,
  it stays where it was). Ground creatures follow the floor (they snap up to the floor at once and descend by at most 64
  per frame); flyers adjust height by their vertical speed within the box heights.
* **Head turn**: the head turns toward Lara by at most 5° per frame, limited to ±90°.
* **Hit status**: each frame "was hit" is cleared when the item is animated and set again
  by damage, so it is true only on the frame after a hit.

### 3.3 Table of creature statistics

| Creature (object) | Hit points | Radius | Smartness (of 32768) | Pivot | Violent flag | Notes |
|---|---|---|---|---|---|---|
| Wolf (7) | 6 | 341 | 8192 | 375 | no | starts asleep (frame 96 of its animation) |
| Bear (8) | 20 | 341 | 16384 | 500 | yes | |
| Bat (9) | 1 | 102 | 1024 | 0 | no | flies |
| Crocodile (10) / Alligator (11) | 20 | 341 | 8192 / 1024 | 600 | yes | one creature that swaps type with the water |
| Lion (12) | 30 | 341 | 32767 | 400 | yes | |
| Lioness (13) | 25 | 341 | 8192 | 400 | yes | |
| Puma (14) | 45 | 341 | 8192 | 400 | yes | |
| Ape (15) | 22 | 341 | 32767 | 250 | no | |
| Rat (16) / Vole (17) | 5 | 204 | 8192 | 200 | no / yes | swaps type with water |
| Dinosaur, T-rex (18) | 100 | 341 | 32767 | 2000 | yes | |
| Raptor (19) | 20 | 341 | 16384 | 400 | yes | |
| Flying mutant (20, "warrior 1") | 50 | 341 | 32767 | 150 | variable | |
| Warrior 2 (21) | 50 | 341 | 8192 | 150 | variable | same AI as 20 but never flies |
| Warrior 3 (22) | 50 | 341 | 32767 | 150 | variable | same AI, never flies, never shoots |
| Centaur (23) | 120 | 341 | 32767 | 400 | yes | |
| Mummy (24) | 18 | - | - | - | - | not an enemy; see 4.20 |
| Larson (27) | 50 | 102 | 32767 | 0 | no | |
| Pierre (28) | 70 | 102 | 32767 | 0 | no | |
| Skate kid (30, "mercenary 1") | 125 | 204 | 32767 | 0 | no | |
| Cowboy (31, "mercenary 2") | 150 | 102 | 32767 | 0 | no | |
| Bald man (32, "mercenary 3") | 200 | 102 | 32767 | 0 | yes | |
| Natla (33) | 400 | 204 | 32767 | 0 | variable | boss, 4.18 |
| Giant mutant / "Abortion" (34) | 500 | 341 | 32767 | 0 | yes | boss, 4.19 |
| Doppelganger / evil Lara (6) | 1000 (reset every frame) | - | - | - | - | 4.17 |

The hit points above are the object's starting value; a level's enemy starts with
that many. Radius is the wall-collision radius.

## 4. Per-enemy behaviour

States are the animation state numbers of the enemy's own animations; where it matters
the table lists them.

### 4.1 Wolf (7)

States: 1 stop, 2 walk, 3 run, 4 jump, 5 stalk, 6 attack (pounce), 7 howl, 8 sleep, 9 crouch,
10 fast turn, 11 death, 12 bite. It begins in the sleep pose (frame 96 of its first
animation). Turn rates: walk and stalk 2°, run 5° per frame.
Behaviour: asleep it wakes (to crouch) if Lara is in its zone or it is escaping, or at random
(32/32768 per frame, then to walk). Walking: if not bored it goes to stalking; if bored 32/32768 per frame to
sleep. Stalking: bites if Lara is in bite range (345) and in bite position; runs if more than 3072
away; in attack mood: runs when not ahead, or farther than 1536, or Lara is facing it (within ±90° of facing it);
otherwise (1/85 per frame, 384/32768) howls. Running: if ahead and within 1536 it pounces
(attack), unless it is farther than 768 and Lara is facing away from it (more than 90° off), when it goes back to stalk. Attack states
deal damage once per attack if the touch mask 0x774F is touched: **pounce 50** (on the attack state),
**bite 100** (on the bite state, ahead only). Death animation 20 plus a random choice of 0-2 (random / 11000).
It is killed by 6 hit points: a few pistol shots.

### 4.2 Bear (8)

States: 0 stroll (walk), 1 stop, 2 walk (rearing walk), 3 run, 4 rear (stand up), 5 roar, 6 attack 1
(bite), 7 attack 2 (swipe), 8 eat, 9 death. Turn rates: walk 2°, run 5°.
Violent. Touch mask 0x2406C. Behaviour:

* Running: **3 hit points per frame** while touching ("charge"). If bored or Lara dead, stops.
  If ahead and not already requested: when it has not been hit, within 2048 and a 2.3% chance
  per frame (768/32768) it rears; if within 1024 it bites (attack 1).
* Attack 1 (bite): once per attack when touching: **200**, with a blood splat at the head.
* Rear/walking state: if it has been hit since it started chasing it drops to strolling; if
  the target is within 600 and in bite position it swipes (attack 2): **400** once per attack when
  touching.
* When Lara is dead and the bear is touching her, it stands and eats (state 8) if within 768.
* Death: it first goes to rear (if walking) or stop, then plays death. **If it dies
  while rearing and Lara is touching it while in the death state, it deals 200
  once** (the collapse slam).

### 4.3 Bat (9)

States: 1 stop, 2 fly, 3 attack, 4 fall, 5 death. Turn rate 20° per frame. Flies; it moves in
fly mode with 102 radius. When it touches Lara it goes to attack and then **bites 2 hit points
per frame** with a blood splat as long as it touches; when not touching it goes back to fly and
its mood becomes bored. It has 1 hit point. Dying: it falls with gravity until the floor, then plays death.

### 4.4 Crocodile (10) and alligator (11)

One creature (20 hit points) that is the crocodile on land (object 10) and the alligator in
water (object 11) and swaps at once when its room becomes water or dry (it keeps hit
points).

Crocodile states: 1 stop, 2 run, 3 walk, 4 fast turn, 5 attack 1, 6 attack 2, 7 death;
turn 3° per frame (6° while in the fast turn state). Violent. Bite: **100**, once
per attack, in bite position within 435. In stop: attacks if in bite range; escape: run;
attack: fast turn if Lara is more than 90° from its facing and farther than 3072, else run;
stalk: walk. Death animation 11.

Alligator states: 1 swim, 2 attack, 3 death; turn 3°. Bite **100** once per attack when touching in bite
position; death animation 4; hit points set to untargetable when it dies; the corpse floats up to the surface
at 32 per frame and then stops. If its water height is "no water" (it is out of the water) it becomes the
crocodile.

### 4.5 Lion (12), lioness (13), puma (14)

Same AI, different hit points (30, 25, 45) and smartness. States: 1 stop, 2 walk, 3 run, 4 attack 1
(pounce), 5 death, 6 warning (roar), 7 attack 2 (bite). Turn: walk 2°, run 5°. Touch mask
0x380066. Violent. Stop: if bored walk; if ahead and touching: bite; if ahead and within 1024: pounce;
otherwise run. Walking: roar chance 1/256 per frame while bored; any other mood stops. Run: stops if bored, if ahead
and within 1024, or if touching and ahead; a roar chance 1/256 per frame unless escaping. Damage once per attack when
touching: **pounce 150, bite 250 (with blood)**. Death animation 7 (lion, lioness) or 4 (puma) plus random 0 or 1
(random / 16384).

### 4.6 Ape (15)

States: 1 stop, 2 walk, 3 run, 4 attack, 5 death, 6 warning, 7 warning 2, 8 run left, 9 run right,
10 jump, 11 vault. Run turn 5°. Non-violent. Touch mask 0xFF00. It sets its "attack" flag (becomes
aggressive) when hit or when Lara is within 2048. In stop: attacks if in bite position within 430;
if not yet aggressive, same zone and ahead it picks a random number 0..1023: below 160 jump,
160-319 warning, 320-479 warning 2, 480-751 run left, otherwise run right (these last two swerve it); else runs. Running: if not aggressive and
Lara within ±45° in front, stops; if ahead and touching: stop and attack; otherwise, when not
escaping, small chance per frame of jump (160/32768), warning (160) or warning 2 (160). The side runs
swerve the ape by 90° each. Attack: **200** once per attack when touching with blood. Vault: when it
climbs a step of one and a half clicks (384) or more in one frame it turns to face the
wall and plays the vault animation (19) shifted 75 into the new sector. Death animation 7 + 0..3.

### 4.7 Rat (16) and vole (17)

One creature (5 hit points) that is the rat on land and the vole in water, swapping the moment the water
height at its position becomes defined/undefined.
Rat states: 1 stop, 2 attack 2 (charge), 3 run, 4 attack 1 (bite), 5 death, 6 pose. Run turn 6°. Touch mask
0x300018F. Bite range 341: **20**; charge when within 1536 in bite position: **20** (once per attack when
touching, ahead). Pose animation: when ahead, 1/128 per frame (256/32768) it stops to pose; it leaves the pose
when no longer bored or at random 1/128. Death animation 8.
Vole states: 1 swim, 2 attack, 3 death; turn 3°; violent; bite **20** once when touching, ahead. Stays near the
water surface (±32 of the water height). Death animation 2.

### 4.8 Tyrannosaurus (18)

100 hit points; violent; states: 1 stop, 2 walk, 3 run, 4 attack 1, 5 death, 6 roar, 7 attack 2 (bite),
8 kill. Turn: walk 2°, run 4°. Touch mask 0x3000 (the mouth). Any touch with its body each
frame costs Lara **1 hit point** (**10 per frame while the T-rex is running**). It sets an internal "hold back" flag
when Lara is not escaping, is behind it (not ahead) and facing it, or when Lara is in bite position between 1500 and 4096 away.
Stop: if Lara is in bite position within **1500** it goes to attack 2 (the bite); else if bored or hold-back
it walks; else it runs. Run: it stops when within 5120 in bite position or when the hold-back flag is set; roars (1/64 per
frame, 512/32768) when not escaping and ahead; stops when bored. Walk: stops unless bored and hold-back; roars with 1/64
per frame when ahead. **The bite: if during attack 2 the mouth touches Lara she dies instantly** (the bite does 10000
hit points): the T-rex goes to the kill state and Lara is moved onto its position with the special "T-rex kill"
animation (animation 1 of the Lara-extra object, her special state 46), unarmed, hands occupied, air -1, the camera on
follow-centre at angle 170° elevation -25°. Death: when its hit points are 0 or below it stops and plays death.
It stays collidable (it keeps pushing Lara) even when dead.

### 4.9 Raptor (19)

20 hit points; violent; states: 1 stop, 2 walk, 3 run, 4 attack 1 (lunge), 5 death, 6 warning,
7 attack 2 (charge), 8 attack 3 (bite). Turn: walk 1°, run 4°. Touch mask 0xFF7C00. Stop: touching or within
680 in bite position: bite; within 1536 in bite position: lunge; bored: walk; else run. Run: stop on
touching; within 1536 in bite position: 1/4 stop (0x2000/32768) else charge; roar chance 1/128. Each attack: **100** once per
attack when touching. Death animation 9 + random 0..2 (random / 16200).

### 4.10 Larson (27)

50 hit points. Non-violent. Gun: one pistol-type shot of **50**. States: 1 stop, 2 walk, 3 run, 4 aim,
5 death, 6 pose, 7 shoot. Walk turn 3°, run turn 6°. Aims when it can target Lara
(ahead, within 7168 and line of sight 768 above both of their positions); runs when Lara is
not ahead or farther than 3072. In shoot state, once per shoot a hit roll (section 5) decides whether
Lara takes **50**; a miss makes a ricochet near her. Pose chance 96/32768 per frame in bored mood.
**He has no health-based fleeing in this source**: he only flees when the mood system sets escape
(hit while at a different zone or the 1/16 chance when hit). Death animation 15.

### 4.11 Pierre (28)

70 hit points; as Larson except: death animation 12 (Larson's is 15); damage **50** total (two guns, **25** each, each with its own hit roll);
walk/run ranges as Larson (3072). Rules special to Pierre:

* **Wounded flight** (when his item does not have its one-shot flag): when his hit points fall to 40 or
  below they are put back to 40 and a "wounded" counter that increases every frame starts; while it
  is non-zero his enemy zone is treated as different and he counts as hit each frame, so he runs
  away (escape mood). He can still be shot, but his hit points are reset to 40 each time. While in the shoot
  state in escape mood he abandons the shot with probability 3/4 per frame (random above 0x2000).
* He **disappears** (hit points set to untargetable, creature slot released, item removed) when the camera can no
  longer see him: each frame the line of sight from the camera to a point 1024 above his origin is
  tested; if it is clear the wounded counter is reset to 1; if it is blocked and the wounded counter has exceeded 10 he
  vanishes. He also vanishes at once if he enters water.
* With his one-shot flag set (the final encounter) none of the above applies; he can be killed.
  When he dies the game spawns three pickups at his position: **magnums, a scion piece (object 144) and
  key item 1**. If a saved game with a dead one-shot Pierre is reloaded and the player holds exactly one scion,
  the same three items are recreated; the music track 55 one-shot is set.
* Two Pierre items can exist in a level (an early one and a one-shot final one). The first
  one that runs becomes "the" Pierre; if a second one runs: if that item has its one-shot flag the first
  is removed, otherwise the second one is removed.

### 4.12 Skate kid (30) and his skateboard (29)

125 hit points; non-violent; two guns. States: 0 stop, 1 shoot (standing), 2 skate, 3 push, 4 shoot 2
(skating), 5 death. Starts in the skate state. Turn 4°. Skate: 1/64 chance per frame (512/32768) to push; if he can
target Lara: stops when she is between 2560 and 4096 away and he is not escaping, else shoots
while skating. Stop: shoots if targetable, else skates. Push: goes back to skate with
1/32 per frame (1024/32768). Shoot and shoot 2: once per shot he fires **both** guns, each with its own hit roll; each hit costs
**50** when standing (shoot) and **40** when skating (shoot 2). He goes back to skating if escaping or Lara is
closer than 1024. When his hit points are below 120 the music track 56 is started (once).
Death: animation 13; he drops an **uzi** pickup at his position. Hit sound: the skateboard-hit sound. The skateboard is
drawn as a second model on the same position and animation (section 20 of `objects-missing.md`).

### 4.13 Cowboy (31)

150 hit points. Non-violent. Guns: two; shoot state: the first shot on the first frame (**70**), the second
shot on frame 6 of the state (**70**, or a harmless muzzle flash if he can no longer target her). Walk range 3072.
Death animation 7; drops a **magnum** pickup. States as Larson without pose: 1 stop, 2 walk, 3 run, 4 aim,
5 death, 6 shoot.

### 4.14 Bald man (32)

200 hit points; violent; starts in the run state. One shot per shoot: **150**, with the hit roll made using **half
the squared distance** (so he hits more often than the others at the same range). Walk range 4096. Death animation 14; drops a **shotgun** pickup.

### 4.15 Flying mutants: warrior 1 (20), warrior 2 (21), warrior 3 (22)

All three share one AI and 50 hit points. Warrior 2 has smartness 8192 and never flies; warrior 3 never
shoots and also never flies; warrior 1 flies. Mesh mask for 2 and 3: 0xFFE07FFF (some meshes hidden).
States: 1 stop, 2 walk, 3 run, 4 attack 1 (lunge), 5 death, 6 pose, 7 attack 2 (charge), 8 attack 3
(punch), 9 aim 1, 10 aim 2, 11 shoot, 12 mummy (appearing from the shell), 13 fly. Turn: walk 2°, run 6°.
Touch mask 0x678.

* Ranged attack (warriors 1 and 2 only): when it can target Lara and (Lara is in a different zone or farther
  than 3840), and Lara is within 45° to one side of its facing (positive angle): aim 1, then a **shard** shot (shot damage
  **30**, speed 250); on the other side (negative angle): aim 2, then a **rocket** (**100** on direct hit, splash see 6).
* Melee: touch or within 300 in bite position: punch **200**; within 600: lunge **150**; running within 2560 ahead:
  charge **100**. Each once per attack when touching.
* Poses and wanders when bored or stalking within 4608; run otherwise.
* Flight (warrior 1 only): it takes off when Lara is in a different zone and it cannot shoot and
  (is facing away or bored), or when escaping. In flight it uses the flying path graph (30720 step, 32 per frame
  vertical) and lands (back to stop) when it is on the floor and out of flight mode (same zone and
  not escaping).
* Death: it **explodes at once**: all its meshes are thrown out as body parts, each of which does **100** if it
  reaches Lara within 200 (and then explodes, giving her a hit reaction), the atlantean death sound is played and
  the item is removed.

### 4.16 Centaur (23)

120 hit points; violent. States: 1 stop, 2 shoot, 3 run, 4 aim, 5 death, 6 warning (rear). Turn 4°. In stop: if in bite position within
1536 it runs; if targetable it aims; else runs. Running: within 1536 in bite position it rears (warning);
if targetable it aims; about 0.3% chance per frame (96/32768) to rear. Aim: shoots when targetable;
shoot: one **rocket** (see 6). Rear attack: **200** once when touching (touch mask 0x30199).
Death: animation 8 then it **explodes** (body parts of **100**) and is removed.

### 4.17 The doppelganger / evil Lara (6)

A reflected copy of Lara that exists in the level where the player meets her double. In the original it uses
fixed constants of that level (the mirror point at sector (36, 60), i.e. its position is
`(73728 - Lara x, Lara y, 122880 - Lara z)`).

* Until the "fall" starts, each frame: the double takes Lara's animation number and frame
  number, copies her x and z rotation, and its facing is Lara's facing plus 180°. It is moved into Lara's current room
  in the room lists and its floor height is that at the mirrored position.
* **Damage return**: the double has 1000 hit points; whenever its hit points have fallen below 1000
  (it was hurt by Lara's weapons) Lara loses `(1000 - hit points) * 10` and the double's hit points are
  reset to 1000. So every point of damage dealt to the double costs Lara ten points. The double itself never
  dies by being shot.
* **Fall**: when the floor at the mirrored position is at least 1024 below Lara's floor and she is not
  airborne, the double goes to the fast-fall state (state 9, animation 32 frame 481), gravity on, its y 50 lower,
  and from then on it only falls (the mirroring stops): every frame it advances its animation, reads the floor
  under it and evaluates the trigger of that sector in **heavy** mode. When it reaches the floor it is set on it, the
  trigger is evaluated again in heavy mode, gravity off, and its goal state is death (state 8), and it stays there.
* It is drawn with its own meshes (the dark Lara) and collides like a creature.

### 4.18 Natla (33)

400 hit points; stats: radius 204, turn: fly 5°, run 6°. States: 1 stop, 2 fly, 3 run, 4 aim, 5 semi-death, 6 shoot, 7 fall,
8 stand, 9 death. Two phases decided by hit points.

**Phase 1 (hit points above 200)**, airborne most of the time. Each frame she checks whether she can target Lara within
±30° of her facing. She flies (state 2) when she cannot shoot; while flying and able to shoot she lands with chance
1/128 per frame (256/32768); while flying she fires a **rocket** (100 on a direct hit, splash radius 1024) every **30 frames**.
On the ground she aims (state 4) and shoots (state 6): three rockets, one aimed and two scattered ±22.5°; then stops.
Her flying path uses the flying graph (20480 step, 32 per frame vertical). When flying she keeps turning toward Lara at 5° per frame.
Her damage is only by rockets (100 direct, up to 100 splash scaled by distance within 1024).

**Transition**: as soon as her hit points are 200 or below: if she is flying she falls (state 7) to the floor; if in
stop/aim/shoot she goes to semi-death (state 5) directly. When she lands she is in semi-death: her hit
points are held at the "untargetable" value (she cannot be hurt) for **480 frames (16 seconds)**, counted from the frame she
lands. At the end of that: she stands up (state 8), her hit points become 200 and the music track 54 is started.

**Phase 2 (hit points 200 or below, up to the end)**: violent mood, walks on the ground (no flying), runs at 6° turn; she
fires a **shard** (**30**, speed 250) every **20 frames** while standing or running, when Lara is within ±30° of her facing and
targetable, standing still (state 8) while firing and running (state 3) otherwise.

**Death**: when her hit points are 0 or below (and not the untargetable value) she goes to the death state (9). Her
death animation ends the fight; what happens next is level data (the final trigger or the animation's finish-level
effect: see effect 4 in `triggers-and-effects.md`) [INFERENCE: the source has no further code for it].

### 4.19 Giant mutant / "Abortion" (34, Adam)

500 hit points; violent; touch masks: left hand 0x7FF0, right hand 0x3FF8000, either 0x3FFFFF0. It turns in place if Lara is more than 45° off its
facing (turn rate 3° per frame while walking forward). In the turn-left state, from 14 to 22 frames after the state began its
facing turns 9° left per frame; in the turn-right state, from 17 to 22 frames after the state began, 14° right per frame.
States: 1 stop, 2 turn left, 3 turn right, 4 attack 1, 5 attack 2, 6 attack 3 (grab), 7 forward, 8 set,
9 fall, 10 death, 11 kill.

* It emerges by falling: state set -> fall (gravity on); on landing it goes to stop and **shakes the camera
  (bounce 500)**.
* Touching it with any body part costs Lara **5 hit points per frame**.
* In stop: if Lara is alive: turn if the angle is over 45°; walk (forward) if farther than **2600**; else if
  Lara's hit points are above 500: attack 1 or attack 2 with equal chance; else (Lara 500 or less) if closer than **2250**:
  attack 3 (the grab), else forward.
* Attack 1: once per attack if the right hand touches: **500**. Attack 2: once if any hand touches: **500**.
* Attack 3: when the right hand touches Lara or she is dead: the kill state; **Lara is killed outright** (moved to its position with
  the special animation 0 of the Lara-extra object, hit points -1, air -1, weapons put away, camera at follow-centre distance 2048).
* Death: after hit points reach 0 it plays animation 13; at the end the item is "deactivated": the atlantean death
  sound, it **explodes into body parts of 250 each** (they move faster: speed up to 255), the trigger of the sector under it is
  evaluated in heavy mode, and it is removed.
* Hit sound when hit by Lara's weapons.

### 4.20 Mummy (24)

Not a creature: 18 hit points but no AI. It stands (state 1) turning its head to Lara within ±90° and, when its hit points are 0 or below
or when its item is touched, goes to the death state (2) and is then deactivated and removed from the active list.
Its mesh mask is 0xFFFF87FF. No damage to Lara.

## 5. Shooting people: hit chance and range

Human enemies (Larson, Pierre, the skate kid, cowboy, bald man) test whether they can shoot: Lara is
**ahead**, within **7168** and the line of sight from 768 above the enemy's feet to 768 above Lara's feet is clear.
A shot is then rolled: it hits if a random number in 0..32767 is below
`(R - d) / (R / 32767) - 8192` where `R = 7168²` and `d` is the squared distance (all integer). In
percentage terms: 75% at point-blank, about 57% at 3072, 0% at about 6200 and beyond. A hit makes a blood spurt on
a random part of Lara's body and the "bullet hit" sound, and a muzzle flash effect; a miss makes a ricochet within ±256
of her feet (random on both axes) and a muzzle flash. Damage is as listed for each enemy.

## 6. Enemy projectiles

| Projectile | Source | Speed | Effect |
|---|---|---|---|
| Shard (object 172) | flying mutant (right side), Natla phase 2 | 250 | on getting within 200 of Lara: **30** damage and a blood effect; on hitting the world: a ricochet |
| Rocket (object 173) | flying mutant (left side), centaur, Natla phase 1 | 220 | within 200 of Lara: **100**, an explosion and a hit reaction; on hitting the world (floor or ceiling reached): explodes: Lara within 1024 takes `100 × (1048576 - d²)/1048576` where d² is the squared distance (so up to 100 at the point of impact, 0 at 1024) |

Projectiles are aimed at Lara's chest point (three quarters up her bounds) with a random error of up to about
±256 angle units (±1.4°) in both directions.

## 7. Level statistics

At the end of a level a screen shows:

* **Time taken**: logic frames in the level (they count while playing, and half a frame per drawn inventory
  frame while the inventory is open) divided by 30, as hours:minutes:seconds (`m:ss` when under an hour).
* **Secrets**: `found` of `total`. `found` is the number of the 16 secret bits that are set in the
  collected-secrets word. `total` is the number of distinct secret numbers appearing in the level's floor data
  (counted when the level loads). In retail data one level lists secret number 0 twice; counted as distinct numbers it
  gives one secret too few unless the second is renumbered [INFERENCE: the retail data may not have this issue]. A secret
  is collected only through a secret action in a trigger (`triggers-and-effects.md` 5.2).
* **Pickups**: the number of items the player picked up in the level (increased each time a pickup or scion is taken).
* **Kills**: the number of creatures Lara's weapons killed (section 2).

The numbers are per level and reset at level start; a saved game stores them with the rest of the state.

## 8. Items and drops that enemies make

| Enemy | Drops on death |
|---|---|
| Skate kid | uzi |
| Cowboy | magnum |
| Bald man | shotgun |
| Pierre (one-shot version) | magnum, scion piece (object 144), key item 1 |
| Everyone else | nothing |

The drop appears in the enemy's room at its position as an ordinary pickup.
