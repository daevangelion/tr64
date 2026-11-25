# Split-screen co-op: reference rules and implementation inventory

The floating-point references below are to OpenLara commit
`2333adef30ae2a5f3b32c5355e6e343d6aa0531a`; port references are to tr64
`50c32c333ce323f84aa8e26336cb949517dff495`.

## 1. Floating-point rules

Slot numbers are zero-based. These are source observations, not a mandate
to copy defects or to replace the repaired fixed-point single-player rules.

**Join, reset and leave.** When not in TITLE or the active/closing ring,
Start edges are processed in slot order (0 before 1); `addPlayer` rejects
cutscene levels. It first removes active flames whose owner pointer equals
the slot's current Lara. A missing slot gets a dynamic Lara and camera
index, with two listeners; existing slot 1 is deleted, with one listener
afterwards. Otherwise, if the opposite slot exists, reset the selected
Lara to its room, position, yaw and stand (including moving player 0 to
player 1); reset restores health and oxygen. No opposite player means no pose
reset. The spawn path assumes a free dynamic entity slot. Removal clears
enemy `target` pointers to that Lara, not every kind of retained pointer
(`src/game.h:206-222`; `src/level.h:748-789,1105-1143`;
`src/lara.h:750-795`; `src/inventory.h:735-737`). In particular the reference
flash keeps an owner pointer, but the removal path does not explicitly retire
these flashes (`src/level.h:792-803,1108-1143`; `src/objects.h:229-275`).
Clear transient references before freeing/reusing the port slot, including
effects, aim/camera targets, and queued owner pointers.

**Enemy target and death.** Indexed `getLara` defaults to 0; the position
overload returns 0 if 1 is absent, otherwise prefers a living Lara over a
dead one, then the nearest by squared 3D distance. Equal distances and
two dead Laras select 1. Ordinary `Enemy::think` reselects on its next
30 Hz think step, clears the navigation target box for a dead/out-of-zone
target and enters the dead-target sleep path; a target can be selected
even if out of zone. On leave, matching cached enemy pointers are cleared
immediately. Bat has a null-target fallback; Doppelganger's own update
reselects **only** when null, so the mirrored Lara's death is not a retarget
event; her removal is. Mummy's proximity collision asks for the nearest
Lara directly (`src/level.h:452-463,1132-1143`;
`src/enemy.h:332-365,1577-1604,1611-1621,2416-2434,2450-2495`).

**Death does not itself end the level.** Health and death animation are
actor-local, but any Lara's lethal hit stops the shared music, clears
both global player light slots and clears her own aim/look targets
(`src/lara.h:1606-1639`). Grounded death input is local; airborne Lara
can clear the death-input bit before the regular floor-trigger check
(`src/character.h:146`; `src/lara.h:3022-3048,3387-3391`). The survivor
can keep playing, and the active-list/update/menu paths do not check
for an all-dead condition or automatically transition. Inventory press
offers the passport if the current `Level::player` (normally player 0,
restored after rendering) is dead, **not** if the pressing slot is dead.
Page flipping separately checks the ring owner's health. This is an
asymmetry, not two isolated death menus. The passport can load, restart
or leave for TITLE; Start can revive via the manual reset/rejoin above
(`src/level.h:2222-2235,2263-2313,3361-3364`;
`src/inventory.h:1014-1056,1072-1083,1381-1386`).

**Ring, pickups and contention.** One ring stores its owner in `toggle`.
Normal Inventory presses choose player 0 first even if both press on
the same update, and an absent owner cannot open it. Once active,
including the closing phase, it pauses **both** Laras and world
controllers (the pause decision snapshots ring activity before the
inventory update). Item use/compass/medikit health use the owner; menu
navigation is not strictly owner-only: raw keyboard keys and raw
joystick 0 left/right are also read. Video/title skip accepts either
slot's action or inventory edge. Settings' player selector is a different
field. Split rendering can present the same ring twice; otherwise one
common ring is drawn (`src/level.h:2222-2243,2263-2308,3346-3353`;
`src/inventory.h:175-190,426-442,735-737,871-897,981-1002,1129-1138,1181-1234,1335-1355,1624-1642,2077-2089`).

Inventory additions and ammunition are common; health, oxygen, weapon,
arms and pending `usedItem` belong to a Lara. The ring offers use to
that Lara first and falls back to removing from shared stock when the
actor returns false. A medikit records shared use when chosen, then
heals and removes a pack only if health is below maximum in her update
(`src/level.h:806-821`; `src/lara.h:504-550,675-697,1719-1759,3271-3283,3306-3318`).
Crystals open the save page for the Lara who presses Action in range
(`src/lara.h:1802-1836`; `src/level.h:120-124`). Quick-save eligibility
instead tests player 0 (`src/game.h:237-242,303-311`; `src/lara.h:659-664`).

Each Lara builds a same-room list of visible pickups. On the pickup
frame she hides and adds them, increments shared statistics, and
projects a notice using her camera. The notice is tagged and drawn only
in her view, if a notice model exists. The holder special is excluded
from ordinary collection. There is **no recheck** of invisibility on the
animation frame: two Lara lists made before either hides the item can
both award it. Do not infer atomic collection from the update order
(`src/lara.h:1802-1839,3196-3230`; `src/ui.h:646-671,766-778,813-834`).

**Switches, blocks, Lara collision.** Each Lara checks her own floor
trigger in normal velocity update except for death input/cutscene levels.
The switch reads her Action, aligns and animates her, and activates the
one shared switch; the active-state guard bars the second Lara from
starting another use while it is active. Timer handling consumes the
shared inactive state; it is not a per-player timer. Key/puzzle holes
use the acting Lara's pending item and one shared hole; activation is
deferred to her animation frame, not a reservation at selection. Pickup
triggers require the item invisible; pads/antipads check floor contact;
heavy triggers require `heavy` and the global heavy entry invokes slot 0's
handler with the triggering object's location
(`src/lara.h:2011-2129,3234-3249,3387-3391`;
`src/objects.h:9-49,1557-1572`; `src/level.h:733-735`).

A block interaction scans shared blocks, aligns the acting Lara (and
can write block yaw even in the ready step); moving checks destination
geometry, trapdoors, pull clearance and animation transition, then
changes the shared floor and activates the block. Returning to stand
after normal movement fires a heavy trigger; the separate falling/landing
branch shakes cameras and does not fire that trigger. No distinct
player owner or partner-occupancy guard is shown, so do not assume it
serializes all overlap merely because updates are sequential
(`src/lara.h:1907-1961,2508-2526,2662-2671`;
`src/objects.h:541-605`; `src/level.h:2301-2308`).
Local Laras do **not** push each other: Lara's collision loop filters on
`isCollider`, whose inherited default does not admit Lara. Nor does the
other Lara enter normal weapon target acquisition, which admits enemies
only (`src/lara.h:1476-1491,3543-3606`; `src/controller.h:627-635`;
`src/format.h:2413-2428`).

**Cameras, input and audio.** Each Lara constructs and updates her camera.
Fixed-camera refresh and look-at commands affect the handler's camera,
not both; `once` is a shared flag on the authored camera entry, so one
actor can use up a one-shot for the other. Refresh precedes the trigger
type checks (unless in heavy mode); look-at can be refreshed when not in
look/combat, and the accepted action assigns it in static/heavy mode.
Heavy commands run through player 0, not the nearest Lara. Floor shake
is computed against each camera eye, and `shakeCamera` applies to both
(`src/lara.h:504-505,1964-2024,2167-2188,2217-2220,3293`;
`src/camera.h:466-507`; `src/level.h:705-743`).
In-level Scion/Midas cinematics set the actor's camera mode but write a
**shared** cutscene transform; completion switches a living ordinary-level
owner back to follow, or advances a cutscene level. `isCutscene()` queries
the current level camera, normally 0 outside rendering, so item-use
suppression is not purely actor-local. Floor FLYBY and CUTSCENE actions
are unfinished (skip an extra command word); they are not the in-level
Scion/Midas camera path (`src/lara.h:1682-1688,1719-1721,1863-1876,2261-2268`;
`src/camera.h:170-177,372-417`; `src/level.h:444-469,3361-3364`).

Gameplay views are side by side in the normal flat case, one world pass
and HUD per occupied slot, using that player's health, oxygen and weapon
with shared ammo. Controls and camera look/spectator input select the
slot's mapped pad; vibration respects that pad's setting; touch drives
slot 0's mapping. Positional sound selects the closest camera listener
(ties go to 1) without a health test; reverb and underwater filtering
use 0. A Lara's muzzle flash retains its owner and uses light index
`1 + cameraIndex` (`src/level.h:797-803,2271,2320-2332,3181-3199,3319-3342,3449-3489`;
`src/input.h:138-147,205-208,231-240,265-273`; `src/camera.h:75-77,113-120,356-359,423-455,559-574`;
`src/lara.h:1085,1390-1422,1595-1596,3022-3119,3255-3257,3295-3300`;
`src/sound.h:836-850,1210-1221`).

**Level end and load.** One accepted END (from either actor, or the
primary heavy handler) or a FINISH_LEVEL animation effect requests a
shared transition, with no proximity/consent test; `loadNextLevel` ignores
another request while one is pending. End of campaign or HOME chooses
TITLE; ordinary completion updates the common stats record (excluding
TITLE, HOME and cutscene as applicable). For a non-title, non-cutscene
destination without a previously pending load slot, it creates the
next-level inventory save and queues a common stats page for player 0;
only that page's owner edge dismisses it. Do not require a page before
every transition. Common time advances during the non-cutscene,
non-ring branch; display uses the common accumulator
(`src/lara.h:2222-2224`; `src/controller.h:1163-1175`;
`src/level.h:87-117,2245-2261,2269-2281`;
`src/inventory.h:1214-1219,2007-2034`).

Checkpoint writes include shared pickup inventory, level state and the
controllers that accept serialization. Lara 1 refuses, as do some
dynamic effects including flames and muzzle flashes. A normal load
(also a same-level load) queues a level stream, destroys the old level
including its dynamic Lara and effects, then constructs only authored
Lara 0 (the TR1 cutscene actor is also slot 0) and sets one listener.
The constructor applies the matching slot; a checkpoint clears/rebuilds
controllers, restores world state and primary Lara and resets her
camera, while a level-entry save only restores inventory. No second
Lara (including pose, health or listener) is restored across loads or
ordinary progression; she must join again after the new playable level.
Do not confuse this with parsing a save into the live two-Lara world
(`src/level.h:126-258,326-356,954-987,1042-1059,1083-1091,2173-2179,2245-2261`;
`src/game.h:60-86,244-264,280-283`; `src/lara.h:675-697`;
`src/objects.h:163-165,240-242`).

### Reference identity-use coverage

The verification commands below count **lines**, including declarations
and comments. The pinned `grep` result is 228 lines in 14 headers (224
without `listenersCount`). `level.h` has 74 and `sound.h` has 6 with it.
Ranges include context, not just matching lines.

| Reference file and lines | Rule or reason outside local gameplay |
|---|---|
| `src/core.h:448`; `src/input.h:7,16-17,138-141,205-208,232-266` | Settings selector, mapped input, edge state, rumble and touch as above. |
| `src/camera.h:76-77,115-116,359,426,432-435,449,559` | Indexed listeners, camera controls and vibration as above; spectator mode is a debug/view feature. |
| `src/controller.h:28,36,68-69,96-100`; `src/debug.h:286` | Interface/default declarations and debug floor drawing, not extra rules. |
| `src/controller.h:1244-1249,1351-1376` | Explosion part damage is slot 0 only, unlike nearest-Lara bias of debris direction. See §4. |
| `src/game.h:17-57,210-222,303-311` | Join/leave, primary quick-save check; cheat histories are indexed (`dozy` acts on owner, weapons and skip are shared), not normal gameplay input semantics. |
| `src/enemy.h:341,1615,2417,2456` | Enemy retarget and cache cases above. |
| `src/objects.h:88,174,377,500,810,970,991,1038,1064,1092,1174,1209,1356,1380,1390,1451,1540,1798,1814,1829` | Victim, proximity, Midas input and projectile rules in §4. |
| `src/objects.h:1321-1339,1613-1633,1707-1725` | Lightning rendering resamples nearest position when unarmed, not cached struck slot; waterfall proximity controls cosmetic splashes; flying helicopter uses nearest Lara for sound/despawn, not a TR1 gameplay hazard. |
| `src/inventory.h:179,237,431-432,641,842-849,871-877,992-997,1094,1130-1131,1183-1187,1211-1233,1268,1299,1354,1382,1626-1635,2082` | Ring, key choice, save/stats and pickup economy as above. Settings selection/rebinding and button help are UI only. |
| `src/lara.h:676,1085,1401-1421,1595-1596,1821-1825,2040,2079-2091,3029-3059,3118,3221-3227,3256,3298-3300` | Save/crystal, flash, per-slot aiming, switch/key, controls and notice rules above. Commented sidestep bindings do not run; Look+Action changes the local camera view. |
| `src/level.h:43,120-123,447-463,610,713-714,734,739-743,806-821` | Player/active-view storage, lookup, indexed shadow binding, shake, heavy trigger and shared inventory. |
| `src/level.h:986-996,1088-1091,1111-1143,2191,2222-2234,2271,2284,2332` | Level/join/remove/listeners, ring/underwater rules. Primary ambient preload, touch weapon button enable and spectator Start pause are auxiliary paths. |
| `src/level.h:2427,2782,2875-2890,2944,3063-3072,3119` | Sprite batch uses primary room; active shadow matrix indexed per view. The nearest-object shadow atlas and debug/ambient room filters in these ranges are commented out, not runtime. |
| `src/level.h:3184,3256-3265,3320-3322,3347,3361` | Gameplay/shadow rendering per camera; shared ring presentation and primary restore. |
| `src/sound.h:842-849,1216`; `src/ui.h:41,656,766-772,833` | Nearest listener, primary low-pass and pickup notice owner/layout. |
| `src/network.h:158-208` | Network peer lookup, input broadcast and spawn coordinate source; not the local player array. |

## 2. Port inventory

| Location | Observation |
|---|---|
| `game/internal.h:729-734` | One player pointer, one player extra and one hazard record; inventory is common at line 742. |
| `game/game.c:235-238,274-278` | A fixed pool of 256 items and nine navigation-cell arrays, one currently for Lara. A second navigation array has a level-dependent cost. |
| `game/game.c:515-553` | The simulation accepts one input and updates player 0; death and inventory decisions are coupled to that player. |
| `game/enemy.c:103-105` | The fixed-game nearest-player helper currently returns player 0. |
| `game/trigger.c:107` | A trigger without a Lara selects player 0's camera, relevant to heavy triggers. |
| `game/sound.c:36`, `engine/snd/snd.c:512-523` | The game attenuation uses player 0's camera target; the sound engine fetches one view. |
| `game/view.c:350,386-392`, `engine/in/play.c:105-108` | HUD and renderer receive the current single view. |
| `game/trace.c:495-507,545-559` | Trace and hash serialize player 0 and schemas 2–4. |
| `game/save.c:306-318,363` | Save handling reads or writes player 0's hazard state. |
| `game/cut.c:85-108` | The cutscene updates the first camera; cutscene levels follow their own tick path (`game/game.c:535-538`). |

A narrow literal search, not a complete assumption inventory, was:

```sh
rg -c 'players\[0\]|playersExtra\[0\]|gLaraHazardState' game engine plat include
```

It reports 61 matching **lines**, including comments and all indexed uses of
`gLaraHazardState`, across 20 files. It does not detect single-player
assumptions expressed without those spellings and must not be used as their
count.
### Search-derived port inventory

Command, run at the port root:
`rg -c 'players\[|playersExtra|gLaraHazardState|JOYPAD_PORT|->inventory|\.inventory|gSaveGame' game engine plat include`.
It matched 223 lines in 36 files. Counts are lines, including comments and
shared-world uses, not the number of necessary changes:

| File | Lines | File | Lines |
|---|---:|---|---:|
| `include/tr64/game.h` | 1 | `plat/n64/sys.c` | 1 |
| `plat/n64/ui-rumble.c` | 2 | `game/inventory.c` | 4 |
| `game/lara_inventory.c` | 3 | `game/effect.c` | 2 |
| `game/object_interact.c` | 6 | `game/view.c` | 10 |
| `game/item.c` | 4 | `game/hazard_thor_lightning.c` | 5 |
| `game/internal.h` | 4 | `game/trace.c` | 12 |
| `game/inventory_economy.c` | 25 | `game/hazard_flame.c` | 2 |
| `game/world_state.c` | 3 | `game/enemy.c` | 4 |
| `game/frontend.c` | 18 | `game/nav.c` | 1 |
| `game/trigger.c` | 7 | `game/cut.c` | 5 |
| `game/flow.c` | 2 | `game/enemy_pierre.c` | 1 |
| `game/game.c` | 47 | `game/menu_text.c` | 3 |
| `game/world.c` | 5 | `game/enemy_encounter.c` | 4 |
| `game/object_trap.c` | 2 | `game/object_progression.c` | 7 |
| `game/lara.c` | 1 | `game/hazard_contact.c` | 2 |
| `game/stats_view.c` | 3 | `game/sound.c` | 1 |
| `game/save.c` | 11 | `game/save_world.c` | 3 |
| `game/lara_health.c` | 8 | `game/lara_weapon.c` | 4 |

In the table, “shared” is not player-0-only: it is a single world or
inventory as in the reference. “Primary” means retain player 0 specifically
where the reference does so. “Both” means select or iterate both, not
necessarily damage both: nearest-target paths must apply the reference's
nearest-living rule. Line groups are individual functions or closely related
uses; all the search matches are represented.

| Port location | Assumption; reference rule; required change; primary? |
|---|---|
| `game/internal.h:729-734,741-742` | One player, extra and hazard; expand per-player storage, retain shared save and inventory. Primary: no for actors, shared for globals. |
| `game/game.c:107-114,163-172,182-194` | Constructor clears the only extra/hazard state; carry stores the primary camera target and one ring actor index. A new actor must not clear the other slot; retain the existing primary carry and shared inventory unless a party carry is chosen in §6. |
| `game/game.c:249-276,366-384,402-423,451-453` | Restore shared state and allocate one Lara navigation array plus eight enemy arrays; retain the authored primary and reserve a second actor's nav workspace. |
| `game/game.c:505,540-553,610,630-648` | Placement, input, death/menu, statistics and transition use one actor/input. Index placement/tick and menu owner; keep global clock and transition. Primary: no for actor; shared for globals. |
| `game/inventory.c:622-625`; `game/inventory_economy.c:31-71,96-106,141-142,162-176` | One inventory, counts, selected-use slot and ring; shared inventory is correct, but retain actor owning a selection and deterministic interactions. Primary: no; shared. |
| `game/lara_inventory.c:34-37,71`; `game/lara_weapon.c:100,114-115,848`; `game/lara.c:1500` | Shared ammo, medipacks, kill/ammo/medical/distance statistics; continue shared counts while applying actions to owner. Primary: no; shared. |
| `game/object_interact.c:19,36,89-105`; `game/object_progression.c:33-40,57` | Shared pickup counts, selected key/lead and statistics; pass the acting Lara through use and prevent the other player consuming a pending selection. Primary: no; shared resources. |
| `game/view.c:105,350-353,367-392`; `include/tr64/game.h:58-61,258,262` | One HUD owner, menu view, camera and render-view result, plus shared flip; request a camera/HUD per active Lara and split viewports. Keep the primary sound-target carry separate from the cinematic eye fallback. |
| `game/item.c:570-573`; `game/enemy.c:220-222`; `game/enemy_pierre.c:29-43` | Arm cleanup and AI eviction use slot loops with one-player bounds; Pierre tests line of sight from the camera. For eviction, don't let the last iterated camera alone decide which enemy is expendable. For Pierre, either seeing camera keeps him visible (§4). |
| `game/enemy.c:102-115,254-293,503`; `game/enemy_encounter.c:49-50,75-76` | Target helper ignores its position, and encounter proximity uses primary. Route each reference-specific choice through the appropriate lookup, not an indiscriminate replacement; keep zone flip shared. |
| `game/effect.c:253,296`; `game/hazard_flame.c:30,45`; `game/lara_health.c:21-26,40,80-101,127-138` | Damage/effects and burning use one Lara, while state is partly indexed; bind flame and hazard record to correct owner, and apply nearest/actor rules. Primary: no. |
| `game/hazard_contact.c:12,31`; `game/hazard_thor_lightning.c:10,67-83`; `game/object_trap.c:8,148` | Slam, sword, hammer, lightning and traps refer to primary; apply each corresponding nearest/acting-Lara rule; lightning flip remains global. Primary: no for victim; shared flip. |
| `game/object_progression.c:127` | Scion destruction shakes only the primary camera today; reference applies its shake to both (§4). |
| `game/trigger.c:26-43,107,223,243-245`; `game/world_state.c:39-57,92`; `game/world.c:76-92,263-302`; `game/nav.c:63`; `game/flow.c:21-32` | Tracks, secrets, flip, rooms and zone selection are shared. The camera is actor-local or primary for heavy. Flow is an actor request, not shared like the flip flag (see below). |
| `game/frontend.c:48-50,72-74,97-113,157-168`; `game/menu_text.c:89-92`; `game/stats_view.c:44-46` | Menu requests retain an actor pointer and reject duplicate queued opens. The current level-end path shows stats only for non-null `lara` and selected source/destination levels and opens for that Lara, unlike the reference's primary stats ownership. Inventory HUD text and stats remain distinct outputs. |
| `game/trace.c:328-333,454,495-533` | Serializes one actor/camera and common inventory/statistics; add an explicitly selected co-op trace schema, leave schema 2–4 bytes unchanged. Primary: existing schemas yes. |
| `game/save.c:182-199,306-318,363,629,674-680`; `game/save_world.c:256-257,316` | Save shared world/inventory and one hazard/actor; if adopting reference exclusion, validate/save only player 0 and reconstruct without player 1 on load. Primary: yes for Lara; shared for world. |
| `game/cut.c:7,85-109` | Cutscene uses primary camera and one timeline; retain single cinematic path and define co-op hand-over. Primary: yes for cutscene. |
| `game/sound.c:36`; `engine/snd/snd.c:238-252,285-336,512-523`; `engine/snd/spatial.c:23-46` | Fixed attenuation and mixer consume one listener; implement closest listener and per-view listener pose, retain primary-only reverb/underwater semantics where supported. Primary: only those effects. |
| `plat/n64/sys.c:126-128`; `plat/n64/ui-rumble.c:6-7`; `engine/in/in.c:7-10`; `engine/in/play.c:258-276` | Port 1 only, one normalized pad and edge history, port-1 rumble; poll/map both separately and maintain per-port edges; keep system chords/gate policy explicitly assigned. Primary: system commands currently primary. |
| `engine/in/play.c:99-106,116-117,261,302`; `engine/ui/picker.c:88`; `engine/ui/frontend.c:51-98` | One gate input stream, one view, menu and picker; adapt journal/gate injection and acknowledgement to player identity; render both views and shared menu. Primary: retain old gate stream for compatibility. |

### Ownership not captured by the literal searches

| Port location | State/call path to account for |
|---|---|
| `include/tr64/game.h:68`; `game/game.c:515-553,614-643`; `engine/in/play.c:144,162,183,258-280`; `plat/host/game-scene.c:191,298` | One normalized input word fans out to menu or simulation. Poll two pads and retain per-slot `input`/`lastInput`, including closing-ring and recursive transition ticks; preserve legacy input/transition handling when solo. |
| `game/lara_weapon.c:731-753`; `game/item.c:509,570-573`; `game/internal.h:729-740,767-768` | Weapon targets/aim and edge/health timers belong to each Lara's extra. `tinfo`, `cinfo`, floor-query outputs, matrix stack and temporary geometry are scratch with call-order lifetimes, not automatically two persistent copies. In particular `cinfo` survives save preparation (`game/game.c:248-259,446-460`); preserve that legacy boundary and don't feed one actor another's collision offset/trigger. |
| `game/trigger.c:61-107,188-215,252-260,282-283`; `game/flow.c:9-58`; `game/lara.c:2530,2563` | Keep mutable camera position/target/timer per view and authored `once` flags shared. A shared flow latch is currently consumed by whichever swimmer runs the successful flow path; associate the latch with its recipient, corresponding to per-Lara flow in `src/lara.h:3387-3391,3792-3828`. Preserve fixed tick timing in solo play. |
| `game/view.c:95-119,345-398`; `include/tr64/game.h:127-134,255-262`; `engine/in/play.c:103-118,302`; `game/cut.c:45-77`; `game/trace.c:327-349` | `tr64_game_view_t` is one camera eye, basis, target, room and shared fields; title/inv cameras and the cinematic matrix eye are separate from live Lara's view. Health and air values/visibility come from `game_hud_view`, not `tr64_game_view_t`. Index both APIs; reuse room/draw scratch between passes. |
| `game/hit.c:6-16,36-56`; `game/lara_weapon.c:121-148`; `game/collision.c:12-38`; `game/dispatch.c:192-207` | `gCameraViewPos` is matrix scratch as well as the cinematic export: a shot writes it before a view-space sphere test. Never use it as an implicit gameplay camera or listener. The port room collision dispatcher already returns without pushing for `ITEM_LARA`; preserve that for both slots. |
| `game/pickup_events.c:5-25`; `game/internal.h:771-773`; `include/tr64/game.h:132-144`; `engine/ui/frontend.c:14,51-62`; `engine/ui/front.c:91-135` | A tick-lifetime world-position/type event and one persistent presentation queue have no owner field. Tag the collecting slot, project with its view, and keep pickup animation aging once per game tick, not once per draw. Prevent double awards without duplicating the shared statistics. |
| `game/world_state.c:61-127`; `game/internal.h:744-758` | Timed flip effect state is shared but stores a Lara pointer for flood; sand uses primary camera target. The single `effect.cameraBounce` value is not the live Scion camera bounce. Clear or rebind retained actor pointers on leave. |
| `engine/save/game.c:9-13,27-39,57-76,91-102`; `engine/save/medium.c:147-166,199-252`; `game/frontend.c:126-154`; `game/save.c:179-200,295-375,500-525,647-685` | One SRAM selection/workspace and one versioned campaign cell per save, not one save per pad. The C path validates then rebuilds the current level in place. Keep shared inventory cells and primary actor tail under the existing layout if player 1 is dropped; remove slot 1 only on successful commit of that load. |
| `game/sound.c:18-68`; `engine/snd/snd.c:238-252,285-336,512-523`; `engine/snd/spatial.c:23-46` | There are two stages to audit: game-side range/gain/chance before the sound event, then mixer spatialization/listener orientation. Keep one event and one RNG decision; do not emit a copy per view. |
| `game/trace.c:254-285,327-349,452-479,491-566` | The schema, writer and hash share a canonical record path; the cinematic eye is explicit, while ring ownership/flow/burn details are not all explicit. Use §3's opt-in trace contract rather than copying a host object layout. |

The pad journal is outside the four searched production directories:
`tools/ship/drive.py:26-48` writes each value and
`test/run/ares-remote.sh:50-57,65-75` forwards each entry until two
`play-frame` acknowledgements or timeout. `tools/ship/boot.py:48-74`
associates gate presses with observed menu acknowledgements. A second pad
needs distinct identity in journal, forwarding and acknowledgement, without
changing old single-pad journals. The renderer must likewise perform
per-view culling and projection (`engine/render/render.c:314-324`), not
merely duplicate HUD drawing.


## 3. Oracle inventory

The oracle exports schema 2 by default and accepts schemas 2–4
(`test/oracle/oracle.cpp:419-434`). Setup `give`, `item` and `activate` each
take two integers, `start` takes one room/position/yaw, and each tick takes
one mask; `IN_START` is rejected (`test/oracle/oracle.cpp:443-479`). A
co-op extension must make player identity and both input streams explicit
while retaining the exact old serialization and script meaning in one-player
mode. This is a compatibility requirement, not a claim that a schema design
has already been selected. The oracle applies the ordered patch set to an
export of the fixed game (`test/oracle/README.md:38-45,160-170`).
### Fixed source inventory

The following search is against the **committed snapshot**, not the modified
OpenLara checkout:
`rg -c 'players\[|playersExtra|gLaraHazardState|JOYPAD_PORT|->inventory|\.inventory|gSaveGame' src/fixed/*.h`.
It returns 167 matching lines across 19 files. This literal deliberately
matches comments and unrelated regression records and misses global
`inventory.` references: those require separate inspection.

| Fixed header | Lines | Fixed header | Lines |
|---|---:|---|---:|
| `common.h` | 4 | `encounter_spawners.h` | 4 |
| `enemy.h` | 4 | `hazard_lara.h` | 6 |
| `hazard_slam.h` | 1 | `item.h` | 3 |
| `lara_autopilot.h` | 4 | `nav.h` | 1 |
| `game.h` | 71 | `object.h` | 14 |
| `hazard_flame.h` | 2 | `room.h` | 4 |
| `world_state.h` | 2 | `progression.h` | 2 |
| `level_stats.h` | 14 | `hazard_thor_lightning.h` | 5 |
| `hazard_damocles.h` | 2 | `lara.h` | 22 |
| `draw.h` | 2 | **Total** | **167** |

Paths in this table are prefixed `src/fixed/` at OpenLara 2333adef. The
“Shared” means keep one world/inventory, not player 0. Upstream source
regressions in `game.h` are not oracle runtime logic.

| Fixed location | Assumption; required co-op change; player-0-only? |
|---|---|
| `common.h:481,1621-1623,1930,2756`; `lara.h:148-150` | `MAX_PLAYERS` is one; arrays scale, cinematic alias uses 0, save is global. Expand storage, retain primary cinematic and shared save. |
| `game.h:91-155,386-504` | Shared V3 header, 64 inventory cells, authored-item records, flip map and precreated-child extensions. Validate the candidate before preparing/reloading; preserve the one shared format and authored Lara record. |
| `game.h:530,635-667` | Reset has an index and initialization clears all entries, but the authored Lara is installed as 0. Reserve a separate dynamic join slot. |
| `game.h:868,1020,1119-1164,1388-1424` | Source platform regression placements/observations and synthetic pickup gates, not the gameplay pickup implementation. Leave old assertions primary; add separate actor-labelled scenarios. |
| `game.h:1509-1524,1566-1567,1619,1659-1665` | Update loop already indexes actors; time is shared; regression observations and seeded statistics use 0. Expand loop bounds and keep legacy test observations unchanged. |
| `game.h:1709-1791,1869,1995,2040-2043,2084-2115,2140-2170` | Regression-only primary actor, synthetic inventory/stats and world checks; extend explicit two-player scenarios without mutating old assertions; shared world, primary legacy. |
| `game.h:2356-2385`; `draw.h:988-1047` | A loop around one full viewport is **not** fixed co-op rendering (`TODO set viewports`), and `DRAW_LARA_FIRST` only changes the draw order. Render the fixed cinematic pass once if expanding that renderer; the headless oracle never calls it. |
| `enemy.h:121-154,478,3240-3255`; `encounter_spawners.h:128-130,453-454` | AI-slot eviction uses camera distance (its threshold is reset each camera); Pierre uses either camera's clear trace once the bounds expand. These are distinct from the spawners' primary proximity tests and the shared zone flip. |
| `lara.h:2794-2805,3239-3253,3327-3345,4081-4103` | Initialization identifies slot; flow uses shared flip; weapon, medipack and distance statistics are common. Expand player lookup but retain global world/stats. |
| `lara.h:4293-4312,4344-4366` | Hazard save/load is indexed by actor. Reference saves only player 0; serialize only that actor under unchanged legacy layout, discard player 1 at reload. |
| `lara.h:4476,4583-4608,4627`; `item.h:66,594-595` | Item sound attenuation and drop-sand sound origin use primary camera; `getLara` ignores position; arm target cleanup loops slots. Shared music flags are not listener state. |
| `object.h:493-523,542-567,811-835,1019-1058,1857-2160` | Fixed source already sweeps all configured player slots for first projectile contact and iterates slots for non-direct splash; the C port currently collapsed those loops. Cinematic actor still uses primary camera; pickups/world rebuild use shared state. |
| `hazard_lara.h:10-104`; `hazard_flame.h:52,77` | Hazard records have player-indexed helpers, but flame victim is 0. Expand owner lookup and use owner/nearest rules; not primary. |
| `hazard_slam.h:24`; `hazard_damocles.h:51,92`; `hazard_thor_lightning.h:81,168-192` | Victims use player 0 while flip is global; select victim per reference nearest target and keep shared flip. |
| `room.h:949,1126,1157-1159`; `world_state.h:277-309`; `nav.h:81` | Trigger actor camera or primary heavy fallback; tracks/secrets, flip transaction and navigation zones are shared. Preserve primary heavy fallback and shared world. |
| `progression.h:114,251`; `level_stats.h:20-38` | Shared pickup/statistics and primary bounce. Keep common stats; reference camera-shake behavior applies to both cameras (`src/level.h:713-714,739-743`). |
| `lara_autopilot.h:1433,1458-1464,1553` | Optional autopilot selects 0 and reads common stats. The oracle build does not enable autopilot (`test/oracle/README.md:154-155`); keep legacy path primary unless the feature is separately extended. |

The supplemental literal `rg -n 'inventory\.' src/fixed/*.h` matches 120
lines in five files: `draw.h` 3, `progression.h` 13, `lara.h` 15,
`game.h` 80 and `object.h` 9; counts include three `#include "inventory.h"`
lines and overlaps with the preceding search. These are not additional
independent assumption counts.

| Fixed location | Shared inventory use and actor requirement |
|---|---|
| `draw.h:15,288,1105` | Include, pickup-slot mapping and ammo label: keep counts shared but display each current player's selected weapon. |
| `progression.h:45-72,112` | Midas selection and Scion pickup use one ring/use-slot; tag the acting Lara and serialize competing uses while keeping resource counts shared. |
| `object.h:6,1017-1056,1119-1141` | Pickup addition and keyhole selection: retain one inventory and one object, attribute the selection to its Lara. |
| `lara.h:8,2939,3338-3350,3832-3834,4053-4081,4132-4141` | Debug START/menu gating, shared ammunition and medipacks, actor-specific death/inventory opening: use the owner's input and health, but shared quantities. |
| `game.h:11,75-125,410-517,645-648,902,1028-1030,1161-1163,1248-1250,1409` | Include, common transition/save/init and source regression setup; retain one inventory and preserve legacy test state. |
| `game.h:1566-1678,1757-1786,1877-1889,2091-2102,2142` | Regression observations, setup and chosen-item actor: preserve old fixtures, add separate actor-indexed scenarios. |
| `game.h:2299-2323,2363-2387` | Menu tick operates on one stored owner and pauses normal simulation; retain one menu owner and deterministic arbitration, keep shared world/menu. |

| Fixed location | Further ownership missed by the searches |
|---|---|
| `common.h:2745-2757`; `common.cpp:5-19`; `lara.h:2833-2947,4110-4142`; `camera.h:261-293` | One global `keys` word is the input adapter and the free camera also reads hardware bits from it; per-Lara `lastInput` already exists. Fixed hit rumble uses pad 0, and each Lara's unmodified update would independently try to open a death page. Do not enable that for player 1 just by raising `MAX_PLAYERS`. |
| `common.h:1599-1618`; `lara.h:148-150,354-376,4200-4208`; `enemy.h:165-218`; `room.h:39-61` | Arms/lock, camera/nav and health timer are embedded per Lara. `cinfo`, floor outputs, and `tinfo` are shared ordered scratch; `cinfo.trigger` is read later in the same actor's update. Check the lifetime of live pointers and values before reusing scratch for slot 1. |
| `inventory.h:119-142,2021`; `object.h:1017-1065`; `draw.h:1075-1124`; `game.h:2299-2314` | One inventory with one `lara` and one pending use slot; one HUD draw call is parameterized by Lara but screen anchors are absolute. Fixed pickup collection mutates shared counts/stats; the owner-tagged notice feed in §2 is a C-side presentation addition, not a legacy oracle field. |
| `common.h:2745-2753`; `camera.h:66,416-423,534-537`; `draw.h:988-1047` | One transient view/matrix workspace used for gameplay, cinematic and inventory passes. Do not treat its last writer as the active listener or camera of both players. |
| `level.h:37-38`; `world_state.h:114-131,179-211`; `lara.h:3232-3284,4429-4479` | Flow latch and effect/bounce state are globals. Water movement consumes flow in an actor path; flood reads the saved actor pointer, sand reads 0. Split actor-owned input/flow from shared effect execution; distinguish shared map state from actor references. |
| `common.h:2755-2757`; `game.h:109-155,386-498`; `lara.h:4293-4312,4344-4366`; `test/oracle/oracle.cpp:25-26,408-416` | One save header/payload and one emulated medium cell in the oracle; for each legacy authored Lara record only that Lara's indexed hazard is saved. Keep the cell and encoding common to both masks. |

### Oracle changes and byte identity

The patch's input adapter bypasses hardware translation inside
`Lara::updateInput` (`test/oracle/host.patch:123-148`). It currently consumes
one mask per tick. Extend the adapter to select an independently supplied
mask by Lara slot and preserve player-0 input processing and update order
exactly when player 1 is absent. The new join/leave mechanism must not
reinterpret the legacy START bit, rejected by the readable parser and
the executable (`test/oracle/input.py:43-44`; `test/oracle/oracle.cpp:474-479`).

`start` presently places player 0 once, without consuming a tick; `give`
adds to common inventory; `item` and `activate` address authored world item
indices (`test/oracle/oracle.cpp:503-560`; `test/oracle/README.md:492-534,542-565`).
Extend placement with explicit slot and second-player creation. Keep `give`
shared and `item`/`activate` world operations. Preserve spelling, tick-0
effects and each driver's existing ordering: the oracle groups `give`,
`item`, `activate`; the native driver applies file order. Comparable mixed
inputs must list them in that group order. Co-op input needs distinct
framing (new two-mask syntax or an explicit version), not guessing from
the number of fields on a whitespace-delimited legacy line.

Schema 4 adds `world` to schema 3: sector, box and flip hashes plus a
room-list validator verdict, not a fourth hash or the full world state
(`test/oracle/README.md:724-756`). The exporter emits one Lara/camera
and hashes the canonical record with FNV-1a, before the hash field and LF
(`test/oracle/oracle.cpp:254-306,383-389`; `test/oracle/README.md:668-676`).
The port's default `game_state_hash` uses schema 2 (`game/trace.c:491-566`).
Add an opt-in schema with slot presence/identity, both existing actor and
camera payloads, owned pending state and common world/inventory/RNG once.
Keep schemas 2–4, defaults and field order byte-identical: TITLE has null
Lara and camera; a cutscene has null Lara **and a non-null cinematic
camera**. Compare full traces as well as hashes; inactive items, effect
payloads, actual audio/video and other exclusions are not made observable
just by changing the schema (`test/oracle/README.md:838-866`).

The oracle build exports the pinned fixed game and applies sorted patch files
(`test/oracle/README.md:38-45`); `host.patch` groups source changes into
diff hunks (`test/oracle/host.patch:5-7,58-60,101-103`). The README labels
29 semantic hunk groups (`test/oracle/README.md:160-170,420-439`); these
are not a count of literal `@@` headers. Implement co-op behavior identically
in source patch and C port, preserve existing repaired behavior and publish a
separate oracle identity. An absent second player must not consume RNG, alter
allocation/update order, change serialized bytes, or add an implicit tick.


## 4. Set pieces and single-Lara takeover paths

“Closest” uses §1's exact selection, including ties and both-dead. An
object that chooses closest before a collision test does **not** test
the other Lara if that test fails. “Open” identifies an actual missing
rule or a conflict with fixed behaviour, not a reason to make unrelated
changes to the existing solo simulation.

| Port path | Floating-point behaviour and fixed/port differences |
|---|---|
| Doppelganger mirror, damage and landing (`game/enemy_doppelganger.c:10-14,57-115`) | Port looks up a Lara every update and on hit. Floating caches closest until cleared by leave and reflects 10× to the hit caller (the shooting Lara passes `this`), **not** to its mirror target. The fall/land fires heavy through primary. A hit without an attacker is not guarded: open (`src/enemy.h:2450-2495`; `src/lara.h:1094-1099`; `src/level.h:733-735,1132-1143`). |
| Midas death, lead and animation (`game/object_progression.c:7-45`) | Tests closest living candidate on the ground and in the same room; same sector requests a lethal hit that sets only the victim's special animation/camera. Lead exchange animates the closest eligible Lara but opens the ring from **player 0** and leaves the pending choice on the hand, so input owner and recipient can differ. Player-1 use explicitly unfinished (`src/objects.h:1347-1383`; `src/lara.h:1606-1609,1682-1688,1744-1756`). |
| Rex and Adam fatal strikes (`game/enemy_rex.c:59-79`; `game/enemy_adam.c:102-171`) | Each samples the ordinary AI target and poses only that victim on a lethal special hit; Adam's fatal state requires contact and a non-hanging target. The float hit handler does not itself relink rooms as the C code does, and does not set a separate camera for the partner (`src/enemy.h:1667-1689,1718-1724,2181-2207,2231-2236`; `src/lara.h:1671-1680,1690-1716`). |
| Adam death heavy trigger and enemy death bursts (`game/enemy_adam.c:16-49`; `game/enemy_mutant.c:86-111`; `game/enemy_centaur.c:90-112`; `game/enemy.c:139-158`) | Adam checks heavy through primary. Floating Adam, centaur and mutant deaths launch damaging parts; direction can bias toward closest, but damage checks **only player 0**. Fixed C `Enemy_explode` instead emits visual explosion items. Co-op compatibility for that discrepancy is open (`src/enemy.h:1921-1934,2150-2168,2383-2399`; `src/controller.h:1214-1255,1351-1376`; `src/level.h:733-735`). |
| Natla, Larson and Pierre (`game/enemy_natla.c:97-105,145`; `game/enemy_larson.c:47-118`; `game/enemy_pierre.c:29-98`) | Ordinary targeting applies; Pierre tests clear line from **either** camera, not visibility from only his target or primary, before retiring, and death drops world items (`src/enemy.h:530-545,2626-2628,2704-2728,3085-3089,3143-3152`). Larson's authored FINISH_LEVEL animation uses the general completion path below, not a player-specific ending (`test/oracle/README.md:230-240`). |
| Small/big eggs, centaur statue and spawned enemy (`game/enemy_encounter.c:38-85`; `game/enemy_mutant.c:109-118`) | Only proximity checks use closest; small egg marked once and big egg can hatch without that check. Spawned enemies use ordinary AI. `enemy_mutant.c` is already the spawned mutant's movement/target path, not the proximity gate (`src/objects.h:1443-1458,1537-1548`; `src/enemy.h:332-365`). |
| Falling floor / bridge / solid movers (`game/object_trap.c:4-28,95-106`; `game/object_block.c:112-121`; `game/object_common.c:126-164`) | Falling floor's activation tests closest Lara's height before shaking. Drawbridge and moving bar change shared state; eligible solid contact is the Lara doing her own collision pass, not nearest lookup (`src/objects.h:792-835,842-858,1406-1427`; `src/lara.h:3543-3606`). |
| Pushable and rolling blocks (`game/object_block.c:26-108`) | The push/pull action animates the actor and moves one block (§1). Floating moving block also fires heavy at the end of its slide; the repaired port rolling block does not. Preserve solo; whether to add that extra trigger for co-op is open (`src/objects.h:541-605,642-668`; `src/level.h:733-735`). |
| Boulder and spikes (`game/hazard_boulder.c:52-61`; `game/hazard_dispatch.c:21-28`) | Floating selects closest; boulder runs a heavy test and distinguishes grounded fatal animation from airborne damage. Spikes distinguish fatal fall from speed-gated running damage, and a lethal hit snaps only the victim's Y to spike height. Port contact uses the collision actor; do not copy the float damage tests into solo (`src/objects.h:497-506,982-1003`; `src/lara.h:1644-1668`). |
| Falling ceiling, swinging blade, dart, slam and sword (`game/hazard_dispatch.c:29-31`; `game/object_trap.c:31-56`; `game/hazard_contact.c:4-63`) | Each floating controller selects one closest candidate. Dart disarms on hit; slam latches its bite; sword samples closest on both fall setup and contact. Floating ceiling does 1000 (port repair is 300); sword's proposed push is a TODO, not a relocation rule (`src/objects.h:84-98,952-975,1017-1042,1057-1074,1088-1114`). |
| Thor and lightning (`game/hazard_thor_lightning.c:10-26,47-83`) | Hammer selects closest to the handle, then tests the footprint; its lethal float hit uses boulder death, and landing uses primary heavy. Lightning selects closest for damage and sets/resets one shared flip; its rendering can aim at a different nearest Lara later. Port uses a distinct hammer cause, not the boulder animation (`src/objects.h:1169-1186,1205-1241,1321-1339`; `src/lara.h:1644-1662`; `game/lara_health.c:50-74`). |
| Lava floor, wedge, particles and burning (`game/trigger.c:110-117`; `game/lara_health.c:19-144`; `game/hazard_dispatch.c:37-40`; `game/hazard_flame.c:33-82`; `game/object_trap.c:133-171`) | Floor lava tests the handler Lara's height (normally the actor; the global heavy entry instead reads the object's sector through primary). Wedge selects closest and gives a lethal hit on collision. The emitter chooses closest to **its own origin** once for spawn gating and all its particles that update, not closest to each particle. Bound flame follows its Lara; emitter flame selects closest and uses shared cooldown. Floating lava death requests joint flames, unlike the fixed burn record (`src/lara.h:1701-1713,2011-2017`; `src/level.h:733-735`; `src/objects.h:133-199,314-323,373-409,1386-1402`). |
| Bullet contact/splash and muzzle flash (`game/effect.c:122-183,249-369`) | Floating projectile uses closest for endpoint collision; fixed source instead sweeps for earliest crossing across configured slots (§3), while C sweep only tests 0. Centaur **non-direct** blast may hit both; direct hit damages only its victim. Mutant miss uses primary's ricochet helper in floating; C produces it from projectile room. Flashes follow the firing actor (lifetime note in §1) (`src/objects.h:229-275,1787-1837`; `src/level.h:797-803`). |
| Ordinary pickup, switches, holes and doors (`game/object_interact.c:9-122`; `game/object_common.c:76-121`; `game/object_dispatch.c:46-57`; `game/trigger.c:124-186`) | Alignment/animation belongs to the actor; stock, switch state/timer, hole and door are shared. The reference does not reserve pickup lists or pending keyhole use across actors; the shared active switch and the delayed hole rule are distinct (§1). Doors update shared floor/portal state (`src/lara.h:1802-1862,2032-2113,3196-3249`; `src/objects.h:9-49,672-768`). |
| Scion pickup and holder (`game/object_progression.c:47-96`) | Qualopec and the holder align/animate the interacting actor and start her camera; holder rewrites her pose and is skipped by the ordinary collection frame. Other Lara keeps her camera; simultaneous cinematics sharing `cutMatrix` are unsolved (§1) (`src/lara.h:1846-1880,3196-3229`; `src/camera.h:170-177,372-417`). |
| Scion target destruction (`game/object_progression.c:97-131`) | Heavy uses primary regardless of who shot; the floating explosion shakes both cameras (`src/enemy.h:2512-2533`; `src/level.h:733-743`). |
| Cabin, trigger flips and flip effects (`game/object_trap.c:110-127`; `game/trigger.c:262-283`; `game/world_state.c:1-59,61-127`) | Cabin consumes shared activation mask, advances stages and flips the map at ground. FLIP/ON/OFF and timed flicker update one room map without a partner position check. Floating flood is a global fade sound, not the port's actor-relative emitter; actor choice for port-only flood/sand/bounce details is open where there is no matching owner rule (`src/objects.h:1463-1489`; `src/lara.h:2190-2216,2272-2275`; `src/level.h:480-505,705-743,2369-2401`). |
| END, FINISH_LEVEL, HOME tutorial and actual cutscene (`game/trigger.c:19-55,267-269`; `game/animation.c:124-170`; `game/cut.c:45-126`; `game/game.c:535-538`; `game/frontend.c:157-168`) | END, animation finish and track-50 tutorial (timer on the Lara evaluating it, track flags shared) request one next level. Turning and reset-to-stand effects act on the animation owner; the floating hands-free effect itself is a no-op. Cutscene completion or Inventory skip requests the next level, with one authored player slot in float and no gameplay Lara in the fixed port; all old players are destroyed when the destination is constructed (§1) (`src/lara.h:1588-1596,1883-1904,2222-2227`; `src/controller.h:1150-1175`; `src/camera.h:372-389`; `src/level.h:1083-1091,1105-1106,2222-2229`; `src/game.h:60-86`). |

## 5. Host sizes and budget

A C99 host probe including `game/internal.h`, compiled with `cc -m32` and the
existing `$TR64_WORK/builds/Ship/generated` schema include, reported:

| Type | Bytes (32-bit host ABI) |
|---|---:|
| `Lara` (`ItemObj`) | 76 |
| `ExtraInfoLara` | 276 |
| `Camera` | 88 (already inside `ExtraInfoLara`) |
| `LaraHazardState` | 8 |
| `Nav_Cell` | 8 per box |
| `GameState` | 8392 |
| pointer | 4 |

These are host measurements, not a measurement of the N64 binary's layout.
The additional Lara may use an existing free item slot rather than requiring
another 76 bytes in the preallocated 256-item pool; the second navigation
array costs eight bytes per level box before alignment. This does not include
rendering, input, sound, save, or other co-op memory. The state pool is 192 KiB
(`include/tr64/pools.def:20`); `docs/architecture.md:274` records a measured
165,984-byte game state in LEVEL3A plus a 4,352-byte drawable chunk and an
8 KiB save buffer. Those are existing measurements, not a fresh co-op test.

The serialized Lara-specific tail is 64 bytes, in addition to the base item
record (`game/save.c:99-105,295-349`). The current payload enumerates authored
level items (`game/save.c:500-505`), so a dynamically allocated player 1 must
not accidentally be added to that enumeration if she is deliberately omitted.
Keeping the reference's player-1 exclusion requires no new serialized Lara
record or save-layout version; it still requires removing player 1 and her
owned effects on load and handling any transient pointers to her. This is a
design implication, not a measured save-roundtrip result.

## 6. Decisions for the project lead

These are co-op-only choices; none changes the legacy one-player hash,
save or input contracts. Reference rules that are settled (shared pause,
slot-0 priority for simultaneous Inventory presses, one world/transition,
stats owned by slot 0) are not unresolved rules.

1. **Death and join controls:** In an active co-op session, should death
   keep the reference's manual reset/rejoin and passport behaviour, or
   should the port's automatic death page open only when the last living
   Lara dies? If a menu is requested on a death, use the reference's
   primary-health test or the actual requesting actor's health? Which
   distinct cart gesture supplies join/reset/leave without taking away
   current Start/inventory (or the existing L+Start restart)?
2. **Actor claims:** Should the Midas ring remain primary-initiated, or
   should either eligible actor be able to request and finish her own
   exchange? For same-object pickup, hole or block contention, reject
   the second attempt at reservation or arbitrate at commit? The source
   does not supply a general reservation; duplicate awards are not an
   acceptable interpretation of a common inventory.
3. **Mirror, damage and shared set pieces:** Use a cached mirror with
   damage returned to the shooter as in float, or keep damage on the
   mirrored actor as an extension of the fixed solo rule? For existing
   fixed collision/sweep hazards, retain per-actor or earliest-contact
   tests, or use the floating preselected victim where they differ?
   Keep repaired rolling-block behaviour and non-damaging port debris,
   or opt into the reference's additional heavy trigger and slot-0
   debris damage in co-op only? For fixed-only presentation effects,
   retain their existing owner/fallback or extend them to both views?
4. **Lifecycle and cinematics:** Drop slot 1 on fresh levels/loads as the
   reference does, or explicitly carry a party across normal progression
   and/or into a successful checkpoint reload? For *in-level* cinematics,
   run an independent view with an actor-owned transform or serialize
   cinematic takeover if a second actor starts one? This is separate
   from the shared cutscene-level path.
5. **Presentation under the existing budget:** Use reference side-by-side
   or top/bottom world views, and one shared full-screen ring or repeat
   the same ring in both viewports as the reference's split setting can?
   If measured co-op exceeds the stock 4 MiB or frame budget, which
   render costs can be reduced without changing solo, or should co-op
   be unavailable in that configuration? The existing gates are not
   relaxed by this spec.

## Verification

- The OpenLara checkout was dirty. Exported the pinned headers using
  `git -C $OPENLARA_SOURCE archive 2333adef src` into
  `$TR64_WORK/tmp/CoopCheck` (excluding `src/platform`); no source edits.
  Ran `grep -n -E 'players\[|getLara\(|cameraIndex|playerIndex|MAX_PLAYERS|listener\[|listenersCount|Input::(state|lastState|joy)\[' src/*.h`
  there and on the requested checkout files: 228 pinned matches, 229 in
  the checkout (one extra `game.h` hit, shifted lines). The original
  `rg -n` search without `listenersCount` repeated at 224. Read the
  pinned hit contexts; §1 classifies all 228.
- Repeated both §2 `rg -c` commands (61 lines/20 files and 223/36), both
  §3 searches (167/19 and 120/5), and checked matched line numbers against
  the inventory rows. Also searched camera/view, input/edges,
  pickup/HUD, arm targets, collision/AI scratch, flow/hazards, listener,
  save and trace symbols outside those patterns.
- Screened 38 files (5,479 lines): `game/{enemy_*,hazard_*,object_*}.c`
  plus `game/{trigger,effect,cut,world_state}.c`. Used `rg -n` on the
  declarations, damage, actor mutations and calls to animation, heavy,
  completion and flip helpers; read the matching paths and followed
  them into their callers and pinned floating counterparts. Checked
  citation paths, line bounds and source context.
- Replaced 15 draft rule bullets and 13 set-piece rows. Substantive
  corrections/removals: 16, counting independently the scan tally,
  leave cleanup, death side effects, menu death-page/owner asymmetry,
  pickup duplication, switch lockout, block landing, fixed camera
  one-shot scope, in-level cinematic ownership, stats eligibility,
  load/rebuild, fixed viewport scope, setup ordering, schema-4 verdict,
  cutscene trace and encounter-vs-mutant source attribution.
- Repeated the §5 `cc -m32 -std=c99` `sizeof` probe against the existing
  generated schema: `76 276 88 8 8 8392 4` in table order. Checked
  provenance with `make provcheck` and the checker on this file alone.
  No gameplay code changed or ran. Runtime join, contention, save/load,
  trace identity and cart budgets are untested here. Unread: unrelated
  reference internals, non-TR1/platform controllers, and data-placed
  reachability of the reviewed branches; this is a source audit, not a
  level-data scan or a playthrough.
