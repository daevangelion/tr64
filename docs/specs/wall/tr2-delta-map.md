# Tomb Raider II against Tomb Raider I: behaviour delta map
Source projects: TR2Main, `f38bc90438e21e8a37e915a812cade52efce1d68`; Tomb1Main, `3b2664bfc4db8e8f5f8d425109d89f3164128a6a`.
Files read (II): `README.md`, `TR2_progress.txt`, `global/{types,vars}.h`, `game/{boat,box,camera,cinema,collide,control,gameflow,health,inventory,invfunc,items,lara,lara1gun,lara2gun,laraclimb,larafire,laraflare,laramisc,larasurf,laraswim,lot,missile,objects,people,savegame,setup,traps}.cpp`, `specific/game.cpp`; (I): `README.md`, `src/global/const.h`, and the existing wall specifications below.
Reader: Wall23
Date: 2026-11-21

The comparison is with the game's existing solo rules, not a proposal to change co-op. See [Lara and enemies](lara-and-enemies.md), [object rules](objects-missing.md), [triggers](triggers-and-effects.md), [drawbridge](drawbridge.md), [tr64's gap ledger](../../game-gaps.md) and [co-op set pieces](../coop.md#4-set-pieces-and-single-lara-takeover-paths). Units: 1,024 per sector, 256 per click, positive Y down; 30 logic ticks per second. **Unchanged** means the particular rule in the row, not its entire subsystem. **New** is relative to I's campaign; **changed** includes cases where the port of I happens to have a partial counterpart. Sizes (line, paragraph, state machine, subsystem) describe the breadth of rules, not code or memory estimates. An executable was not used to fill gaps. “Not established by this source” is not permission to copy I's rule.

Published evidence, separately from source-backed rules: [original PC manual](https://tombraiders.net/stella/files/manuals/TR2/Tomb_Raider_2_PC.pdf) (controls, inventory and saves); [classic controls](https://tombraiders.net/stella/walks/controls/TR2controls.html); [classic campaign and placements](https://tombraiders.net/stella/tomb2.html#walk); [enemy field guide](https://www.tombraidercie.com/jeux/tr2/ennemis/) (appearances and attack kinds, not timing); [TRosettaStone §§2.1, 4.4–4.6, 7 and 9.9](https://opentomb.github.io/TRosettaStone3/trosettastone.html) (format and public effect meanings). The two-level companion separates scene evidence from parameters even when a public guide shows the visible outcome.

## Lara, health and water

| Rule | Delta | What to size |
|---|---|---|
| Run, walk, sidestep, hop back, ordinary jump, vault, ledge grab, shimmy, ground roll | unchanged (controls) | The ordinary controls and releasing Action to drop still apply. Exact per-state speed, jump windows and ground collision differences are not established by this source. Do not equate the ledge-grab state with the new ladder grab. |
| Ordinary ground collision and body dimensions | unknown | The I baseline uses radius 100, height 762 and a 384-unit step test when a body pushes Lara. II's ordinary ground probes, body dimensions, edge and slope limits, and push-out acceptance are not established by this source. The 762-unit low-ceiling jump test below does not establish her standing collision height. |
| Hit and injury reaction | retained (contact marks); unknown (response) | Some II hazards still mark Lara as hit, separately from taking health. I's directional bump limits, body-slap cue and 34-frame cap are not established for II. |
| Wall/ladder climb | new | State machine: catch a marked face from ground, ledge or jump; hold Action to stay on, move in four directions, pull over a lip, drop by releasing Action or backflip with Jump. Sector face, clearance and hand/feet checks are a separate problem from ledge hanging. Exact offsets and test thresholds are not established by this source. |
| Reversal, falls and dives | changed | Air reversal with Roll or the opposite direction after forward/backward takeoff; underwater reversal with Roll; the swan dive can somersault. State-machine additions; animation data governs the change windows. I's gravity was +6 through speed 128 then +1, fast-fall state at 131, no landing loss through 140 and outright death at 155. A common II gravity or fall-damage rule is not established by this source. A jump under 762 units of clearance can send Lara into fast fall in the opposite direction at quarter speed, with downward speed at least 1. An optional variant bypasses that response; the bypass is not established as original retail behaviour. |
| Wade | new in retail; changed against tr64 | State machine, not just a slow animation. Dry-to-wade requires water more than 384 above the feet and total depth at most 474. At feet depth at least 384 and total depth above 474, a water room permits ordinary submerged entry instead. Wade leaves for land below 384, or surface swim above 730; surface in an air room can become wade, not necessarily a fall. Compare tr64's inherited shallow-pool threshold of 730 for dry-to-wade in the gap ledger. Underwater-to-surface still requires the water-room exit. See companion for exact state and camera changes. |
| Waterline actions | changed | Published controls add wading, the underwater roll and using the harpoon gun below water. Underwater swim accelerates by 8 up to 200, pitches at 2° per tick and banks at 3° into the turn; yaw rate builds by 2.25° per tick to ±6°. The usual surface/dive and underwater pickup/lever inputs remain. Exact surface ledge exits, bottom contact and current strength are not established by this source. |
| Air and recovery | unchanged versus I wall baseline | Air 1,800; lose 1/tick below water, then 5 health/tick when the air first becomes negative; surface +10/tick; dry or wading resets to 1,800. Not the manual's approximate two minutes or the port's zero-tick drowning onset. |
| Health, first death frame, healing | unchanged | 1,000 full; nonpositive becomes −1; death stops music once. Small and large packs add 500 and 1,000 (cap 1,000), usable only while alive and not full. The manual confirms full health after successful level completion. |
| Death-menu delay | unchanged | The two thresholds remain 300 death ticks automatically or 60 with input. II requests the menu after the applicable threshold has been exceeded; this is a transition to the death menu, not an automatic restart. |
| Death at Home and after Lara disappears | changed | Death at the training home returns to title; when Lara has disappeared the death counter advances twice per tick. |
| Special takeovers | retained in outline; new encounters | A fatal two-actor pose still takes away ordinary player movement and puts Lara in the attacker's position, orientation and room. II retains a T. rex takeover and has fatal poses for the yeti and shark. Exact entry, exit and camera rules are not established, and neither are any differences between the I and II T. rex poses. The I-specific takeovers and what is known of their II counterparts are listed below. |
| Takeover with a second player | unknown | II evidence here is solo. It does not establish who owns a forced pose, camera, inventory action or level change if the port adds a second Lara. |
| Environmental burn | changed | Ordinary heat 3 → 5/tick within 600; ignition radius 300 → strictly under 450 horizontally. Dormancy stays 100 ticks. Attached burn 5 → 7/tick; ordinary ignition does not attach a second effect if already burning. Water extinguishes the attached flame when the flame's own point goes below the surface. True-floor lava still kills outright and can attach up to ten separate effects, unlike ordinary ignition. |
| Scripted environmental limits | new | A nonzero authored no-floor height kills at or below it and starts the death counter at 270. Sunsets and deadly-water sequences can be selected by the script; the exact deadly-water damage/collision path is not established by this source. |
| Flares, braid, local light | new | Flares are a hand-and-world-item state machine: draw/light, keep while moving, drop or throw, recover spent time from save. A box is six; a loose flare counts as one. Braid is visual, and flare light in dark or underwater rooms is not. Exact burn schedule and hand/world physics are not established by this source. The muzzle flash of handgun/shotgun/M16 also produces real dynamic light. |
| Zip line | new | State machine: approach and grab its handle; slide while holding Action; let go to fall. See the companion for placement; speed, limits and reset rules are not established by this source. |
| Speedboat and snowmobile | new | Two subsystems, not reskins of the non-ridable Mines boat. Side boarding, vehicle-relative Lara animation, drive/turn, Walk slows, Action boosts unless used for guns, side jump off. The speedboat also allows boarding from water and can launch off a ramp. Hull/water dynamics, snow traction, camera, rider transfer and high-speed impacts cannot be substituted with Lara's walk rules. Exact dynamics are not established by this source. |

## Weapons and ammunition

The nominal initial aim limits of paired weapons are ±60° in yaw and pitch. The tracking limits are −170° to +60° in yaw for the left arm, −60° to +170° for the right, and ±80° in pitch for both, as in [I's weapons](lara-and-enemies.md#2-weapons). For long guns, the nominal yaw limits are ±60° initially and ±80° while tracking; pitch limits are ±55° and ±65°, or ±65° and ±75° for the harpoon. Nominal aim slew is 10° per tick. Neither these angles nor nominal range guarantee lock or a hit. Holding Action retains the selected target even if the arms lose lock; releasing Action lets Lara select another. The complete lock, sight and damage tests, allowed draw states and arm-cycle transitions are not established by this source.

| Weapon | Delta | II rule and internal rounds per pickup |
|---|---|---|
| Pistols | unchanged (values); changed (light) | Infinite, 1 per hit; 8,192 range, muzzle height 650, recoil setting 9 ticks, flash 3. Flash can illuminate the room. |
| Automatic pistols | changed | Replace magnums; 2 per hit, 8,192 range, 650 muzzle height, recoil setting 9; reference I clip 50 → II 40. |
| Uzis | changed | 1 per hit, 8,192 range, 650 muzzle height, recoil setting 3; reference I pickup 100 → II 80. |
| Shotgun | changed | 3 per pellet instead of 4; six pellets remain, scattered on each aim axis; muzzle height 500, range 8,192. 12 internal units per box (two shells). The exact trajectory tests for individual pellets and the firing cycle are not established by this source. |
| M16 | new | Paragraph-plus animation: nominal 3 damage, range 12,288, muzzle height 500, 40 rounds per pickup. An optional running variant makes shots less accurate (4° → 12°) and weaker (3 → 1) on the move; this is not established as original retail behaviour. |
| Harpoon gun | new | Projectile state machine, usable underwater, 3 bolts per bundle, nominal 4 damage and range 8,192. Launch speed 150 split by aim angle, aiming at the current target if present; one shot consumes a bolt unless bonus mode. Player-bolt room/water/collision rules are not established by this source. |
| Grenade launcher | new | Projectile and collision rules: 2 grenades per pickup, launch speed 200, speed drops 1/tick; once speed is below 190, downward velocity increases 1/tick. Room geometry impact starts a 512 expanded collision probe against nearby bodies; direct creature bounds can end the projectile too. An eligible active target loses 30; death generally shatters it, with exceptions for the Dragon and Guardian of the Talion. The breakable window can shatter without ending the grenade. Not a general-purpose radial damage aura in flight. |
| Armed snowmobile | new | Twin machine-guns, nominal damage 3, targeting range 8,192 and ±30° yaw; Action fires instead of boost. |

Ammunition without the gun remains as inventory pickups. Acquiring a gun grants its pickup load, converts accumulated loose ammo, and replaces further authored pickups of that weapon with matching ammo. This follows I's stock/weapon distinction but must include the new weapon families and quantities. Table figures are inventory arithmetic, not necessarily the UI count of clips or shells.

## Creatures

| Shared rule | Delta | Scope |
|---|---|---|
| Heading, enemy zones, turn, route and mood | changed | Ground data has four sets of ground zone assignments for each map state, instead of I's two; flying has its own zones. Authored blocks, rooms and movement capabilities still matter. II's exact slot count and eviction policy, per-family step/drop limits, mood transitions and target-selection formula are not established by this source. Do not reuse I's eight awake slots or 1/16 hit-induced flight probability as measured II numbers. |
| Friendly and competing targets | new | Monks can fight the invading mercenaries without being Lara's enemies; provoking them can turn them against her. The save retains whether the monks have been angered. Exact target priorities and hostility latching triggers are not established by this source. |
| Humans with guns | changed | Enemy shot/ricochet and hit effects are retained, but the new gunmen have different behaviours and some carry pickups; their exact firing windows, ranges, damage, hit probability and checks for movement/cover are not established by this source. I's 7,168 test and range-only roll are not verified for II. |
| Enemy projectiles | changed | Knife and hostile harpoon each hit for 50 within the 200 near test; either ricochets at floor/ceiling. A hostile harpoon in air droops 1° per tick until about −67.5°. A flame missile within 350 hits for 3 and ignites Lara. These replace I's shard/rocket assumptions for the new enemies, not Lara's own projectile rules. |
| Bodies and rewards | changed | Ordinary creatures can carry a chain of level pickups. At death these emerge at the corpse position and in its room. The ordinary retirement rule releases AI and disables collision; marked corpses can be cleaned up later. Do not key all drops to enemy type as in I. Corpse queue cleaning is a floor action, not a drop action. |

The health figures below are starting hit points, not damage to Lara. The field guide supplies public attack kinds where detailed decision rules are not established by this source. Different costumes may share a family; equal health does not establish equal behaviour.

| Family | Delta and breadth | Behaviour |
|---|---|---|
| Doberman (10) | new, state machine | Pursues and bites, often released with a club thug or gunman. |
| Rat (4) | changed, state machine | Land vermin approach and bite; not I's 5-HP rat/vole pair. No aquatic counterpart is evidenced in Venice. |
| Tiger / snow leopard / white tiger (20) | new, state machine | Large cat rushes and lunges; terrain heights can separate it from Lara. |
| Small spider (5), giant spider (40) | new, two state machines | Small spiders rush or leap from dark recesses and ledges; giant spiders are a later, stronger encounter. |
| Crow (15), eagle (20) | new, shared flying state machine | Birds take to the air and attack Lara close to her head; they fall after being shot. |
| Tyrannosaurus (100) | changed, state machine | Still chases and can grab Lara in its mouth; radius 341 and HP unchanged, forward pivot 2,000 → 1,800. Do not assume I's damage, turn rates, death collision or mouth mask. |
| Masked Venice gunmen (25) | new, state machine | Three costumes share one silenced-pistol family: balcony and mobile encounters. |
| Club / iron-bar / wrench thugs (27) | new, state machine | Close in on foot, can vault small obstacles, attack with melee weapons. |
| Fiamma Nera boss (150) | new, state machine | Heavy twin-shotgun attacker, encountered in Opera House and again in the epilogue. |
| Knife-throwing cultists (60) | new, state machine | Ranged knives rather than hitscan guns. |
| Offshore workers: shotgun (25), automatic rifle and flamethrower (20 each) | new, two firearm variants plus flame attack | Attack from range; flames can set Lara alight, and water extinguishes the resulting burn. |
| Diver (20) | new, swimming state machine | Pursues and fires hostile harpoons; distinct from the human gunmen on land. |
| Shark (30) | new, swimming state machine | Pursues and bites; has a special Lara death. |
| Barracuda / catfish (12) | new, swimming state machine | Fish pursue and attack, sometimes as a group. |
| Moray eel (5) / big moray (20) | new, two trap-like state machines | Strike from recesses rather than following normal free-swimming routes; nominal health is not evidence of targetability. |
| Jellyfish (10) | new roster entry | Exact base-campaign placement and attack rules are not established by this source. |
| Uzi mercenary (45) and two pistol mercenary variants (50) | new, two state machines | Ranged fighters; monastery encounters can involve monks as opponents. |
| Armed snowmobile and rider (vehicle enemy 100) | new, subsystem | Mounted gunner can pursue, ram and shoot; enemy rider and armed vehicle have separate identities. |
| Monks (30 each, two appearances) | new, shared state machine | Staff fighters whose target allegiance matters. |
| Yeti (30) and Guardian of the Talion (200) | new, two state machines | Yeti pursue in dark caverns with melee attacks; the Guardian is a distinct arena boss. |
| Xian spear and sword guardians (100 / 80) | new, two state machines | Statue encounters wake into melee; the sword fighter flies between attacks. Do not reuse the first game's centaur-statue mechanism as the full fight. |
| Marco Bartoli / Dragon (300 for the front; front and rear together) | new, boss subsystem | Transformation, two physical halves, flame attacks, temporary collapse and a dagger-removal finish; [classic finale](https://tombraiders.net/stella/walks/TR2-classic/17dragonslair.html). |
| Winston | new, state machine | Follows Lara at home and is untargetable (−16,384). |
| Golden Mask wolf and bear (10 / 30) | changed, campaign variants | The expansion reuses the small/large spider positions in the roster for wolf/bear. Not base II placements; compare I's wolf 6, bear 20. |

### Which of I's enemies and encounter rules carry over?

Dispositions distinguish the base campaign from Golden Mask. A replacement is a new encounter with a similar role, not evidence that it behaves like the old one.

| I family or encounter | II disposition |
|---|---|
| Wolf | Absent from base II; Golden Mask has its own 10-HP wolf, not I's 6-HP wolf. The old sleep, howl and pounce rules are unknown there. |
| Bear | Absent from base II; Golden Mask has its own 30-HP bear, not I's 20-HP bear. The old rearing and collapse rules are unknown there. |
| Bat | Replaced in the aerial-attacker role by crow and eagle; I's 2-per-tick bite is not established for them. |
| Crocodile and alligator | Absent from the II creature roster; shark and fish are separate underwater families, not a replacement for the one creature that swaps form between land and water. |
| Lion, lioness and puma | Replaced in the large-cat role by tiger, snow leopard and white tiger; I's pounce and bite values are not established for them. |
| Ape | Absent from the II creature roster; the ape's threat poses, bounds and vault are not a generic melee rule. |
| Rat and vole | Rat retained with 4 HP instead of 5; a distinct vole and the land/water form swap are not established for II. |
| Tyrannosaurus and raptor | Tyrannosaurus retained, with forward pivot 1,800 instead of 2,000. It retains a two-actor fatal pose; the exact differences from I are unknown. Raptor is absent from the II creature roster. |
| Flying mutant and ground warriors 2 and 3 | Absent from the II creature roster; Xian guardians are different enemies. Whether another II encounter reuses I's bursts of shards or rockets, flight rules or hatching is not established. |
| Centaur and its statue | Centaur absent from the II creature roster. Replaced in the living-statue role by Xian guardians; retention of I's 3,584-unit wake-up rule and statue debris is unknown. |
| Mummy | Absent from the II creature roster. Reuse of I's stationary reaction to shots or touch is not established. |
| Larson, Pierre, skate kid, cowboy, bald mercenary | Absent as I encounters. II's gunmen, thugs and mercenaries fill the human-combat role. Whether a II encounter reuses Pierre's staged retreat, disappearance, three fixed drops or later replacement is not established. The skate kid's skateboard was not a player vehicle. |
| Natla and giant mutant (Adam) | Absent as I bosses. Whether a II boss reuses Natla's timed resurrection rule or Adam's fall, grab or heavy-trigger death rules is not established. |
| Lara's doppelganger | Absent as the I encounter. No II counterpart is established for mirroring Lara, reflecting tenfold damage or falling onto a heavy pad. |

## Traps, movers and interaction

| Rule or family | Delta | What to size |
|---|---|---|
| Ordinary spikes, rolling boulders, collapsing floors, hinged trapdoors, bridges and pushable blocks | unchanged (roles); numeric parity not established | Great Wall uses walkable breakaway tiles, stationary spikes and rolling boulders with follow-on triggers. The thresholds, motion, reset and the exact heights where a surface supports Lara or blocks her from above or below are not established for II. Save/load does rebuild the solid floor of a resting pushable (height 1,024). |
| Pendulum | changed | Contact 100 → 50/tick. A running pendulum can hurt on touch without waiting for I's state 2. |
| Teeth trap | unchanged | Trigger opens/closes the jaws; 400 per touch tick while triggered and biting (state 1). |
| Falling ceiling and darts | changed | Ceiling still deals 300 per contact frame, but refreshes its room/floor and rests at that floor. Darts keep the first-frame launch, 512 up/412 back offset and 50 contact damage; in II the visible flying dart spins 22.5° per tick, and the shot plays sound 254 without the I launch flash. The dart is not consumed by contact; it ricochets at the floor. |
| Spiked wall / ceiling spikes | new | Two short movement rules: horizontal wall 16/tick, stops at a changed floor; ceiling 5 down/tick, stops when the floor is less than 1,024 below its proposed position. Both deal 20 per touched tick. |
| Spinning/rolling blade and guillotine blade | new | Separate families, not swinging pendulums. Mobile blade looks 1,536 ahead for missing floor and reverses on its turnaround animation; 100/tick on touch while rolling. Fixed blade hits with its blade mesh only in the cutting animation, 100/tick. |
| Hook, icicle, armed statue | new | Paragraph each: hook contact 50; falling icicle starts with downward speed 50 and deals 200 while falling; statue's extended weapon deals 20 during the active cycle, only on its weapon mesh. |
| Fans/propellers, springboard, port bell, suspended bowl | new | Separate hazards or set pieces: moving fan/blade motion, launch pad, shootable bell, spilling bowl. Execution and exact numbers are not established by this source. |
| Mine and detonator | new | Stateful minefield: a boat within 512 succeeds where a swimmer does not; first blast removes that boat and kills its rider, then other mines chain with probability 1/16 per tick each. A key-started detonator appears in [Bartoli's Hideout](https://tombraiders.net/stella/walks/TR2-classic/03bartoli.html), not in the Venice placement evidence. Exact detonator interaction is not established by this source. |
| Breakable windows and gondolas | new | Shoot or ram suitable glass; broken window bits persist on reload. Gondola can shatter and sink (50/tick in sinking state); the impact speed threshold is not established by this source. |
| Doors, locks, switches, lift and drawbridge | changed/new | Ordinary levers/buttons, submerged levers, keyholes, timed doors and animation-led movers build on I. Venice includes a flip that changes the level of the boat lock, and a timed exit with a start threshold separate from its button. Lift is new and saves motion state; exact cabin floor/ceiling rules and switch/door timer details are not established by this source. |
| Ordinary pickup/key/puzzle interface | unchanged (controls); changed (stock) | Use Action to collect/interact, then the items ring for keys/puzzles; wrong key is not a generic use-any-item action. New stock includes six-flare boxes, one loose flare, new guns/ammo, and real statue pickups for secrets. Bone/room alignment, pickup/insert commit frames and switch variants are not established by this source. |

### Which of I's moving surfaces and set pieces carry over?

| I object or takeover | II disposition |
|---|---|
| Flat bridge and both sloped bridges | Unknown individually: the II heights where Lara is supported or blocked overhead, including I's 256-thick underside and its 1-click and 2-click inclines, are not established. A bridge surface is not interchangeable with a hinged drawbridge. |
| Hinged drawbridge | Retained as an object family; I's state-1 walkable deck, two-sector footprint, underside height, and raised collision are not established in II. |
| Rolling block, moving bar, cogs | Unknown for each. The confirmed movable blocks, doors and lift do not establish I's 2,048-unit rolling-block floor change, sliding-bar collision, or linked cogs. |
| Natla's Mines cabin and scenic boat | Cabin absent as the I encounter; whether an equivalent three-stage mover exists in II is unknown. The non-ridable scenic boat is replaced in the boat role by a rideable speedboat, not by an animation that plays once. |
| Damocles sword | Unknown for a corresponding falling-sword trap. The II guillotine and the weapon-bearing statue do not establish I's overhead release, drift or 100-point hit. |
| Thor's hammer and lightning | Absent as the I encounters. A separate II hammer rule, or recurrence of I's lightning range, flip cycle and damage, is unknown. |
| Lava wedge and lava particles | Unknown for each. Retention of the lethal real-floor burn does not establish a sliding wedge, plume emitter or damaging particles. |
| Earthquake, block-land shake and floor shake | Camera bounce retained as a view behaviour; retention of the I earthquake item and its sounds, or the exact block and animation cues which set off a shake, is unknown. |
| Scion pickups, holder, shootable target and cinematics | Absent as I set pieces. II has different quest objects; neither a key nor a secret dragon reproduces the Scion taking over the view or the shootable Scion firing a heavy trigger. |
| Midas hand, golden death and lead-to-gold exchange | Absent as the I set piece. Special II deaths and quest-item use are not evidence of the same gold transformation, camera or exchange. |
| Mutant eggs and centaur statue | Eggs absent as I encounters; Xian statues replace the statue-ambush role. I's hidden creature choices, hatch distances, thrown debris and activation timing do not follow from the II statue encounters. |
| Boulder flattening, spike impalement | Objects retained; exact II fatal-pose conditions, damage and camera takeover are unknown. |
| Save crystal | Absent as a save gate in II; the passport can save during play. |

## Floor, camera, music and progression

| Rule | Delta | What to size |
|---|---|---|
| Sector surface and floor triggers | new / unchanged (format where marked) | Unchanged: rectangular sector grid, portal/slant entries, separate trigger type and action. New climb entry 6 uses +Z/+X/−Z/−X mask 1/2/4/8. New trigger subtype 9 (antitrigger) deactivates object actions without requiring pad contact; distinguish it from antipad. No TR III diagonal floor functions or TR IV flyby/cutscene actions in II. |
| Object activation, heavy, flip, clear corpses | changed | Heavy is still a separate evaluation mode (including after the first mine explodes). Clear-bodies action 11 matters for marked II corpses; do not leave it as tr64's I no-op. Scripted or flip effects include Home's course timer and new mesh/gun/light effects. Normal activation masks, timer 1, exact one-shot rules and effect/flip ordering are not established by this source. |
| Camera | unchanged core; changed rig/set pieces | Chase, Look, combat, fixed and heavy camera keep familiar roles. Fixed camera counts down to −1, heavy persists until then; the ordinary view clears its target overrides after use. Bounce still applies a positive Y kick once, or randomized three-axis shake with amplitude falling by 5/tick. Base chase distance is 1,536; wading requests elevation −22°. Separate driven vehicles, the zip line and in-level effects need their own camera readings. Trigger re-arm/refresh rules and rig offsets for new states are not established by this source. |
| Soundtrack | changed | Track actions accept 2–63. Outside switch triggers, a previously marked overlapping mask suppresses replay; a one-shot records the supplied bits. A nonzero timer defers playback; the recorded countdown is an 8-bit count of the authored timer ×30, advanced by re-evaluation for the pending track. Do not require all five bits or use I's Home tutorial track special-cases. The II save retains the per-track state. |
| Campaign and scenes | changed | Script drives filenames, names, cinematics, movies, music, inventory grants/removal, stats and credits. Home training is separate; campaign order is Great Wall, Venice, Bartoli's Hideout, Opera House, Offshore Rig, Diving Area, 40 Fathoms, Wreck of the Maria Doria, Living Quarters, The Deck, Tibetan Foothills, Barkhang Monastery, Catacombs of the Talion, Ice Palace, Temple of Xian, Floating Islands, Dragon's Lair, Home Sweet Home. Story scenes are separate authored cutscene levels or FMVs, skipped on the saved-game entry sequence. The PC cutscene follows synced audio, and Action or Inventory can cut it short. Home Sweet Home is an epilogue with its own rearmament, not unlocked by dragons. |
| Starting stock | changed | New campaign begins at the Great Wall with holstered pistols selected, shotgun with two shells, two flares, one small and one large pack. Training home starts unarmed, no ammo, flares or medikits. Carry guns, ammo, flares and medikits forward, but allow scripted stripping, gifts and special start animations; do not blindly carry the level's keys/puzzle contents forward. |
| Secrets and bonuses | changed | I counted secret numbers in floor data; II's silver, jade and gold dragons set three distinct bits on pickup. The script defaults to three expected secrets but can specify another total. All three in a normal three-secret level give a scripted inventory reward (different for each level; see companion). Rewards are not a global fixed number of shells. |
| New Game Plus / bonus level | new | Bonus replay and Golden Mask's bonus level are separate. In the checked PC behaviour, bonus replay arms every weapon, chooses the grenade launcher, seeds 10,001 internal rounds for each finite-ammo gun and 255 flares; the exact original unlock rule and all original ammo exceptions are not established by this source. Golden Mask's Nightmare in Vegas is expansion-only; [classic campaign/expansion index](https://tombraiders.net/stella/tomb2.html#walk). |
| Statistics | changed | In addition to elapsed time and kills, retain shots, hits, cumulative 3D movement distance, medikits used and secret bits in per-level stats. II's record does not have I's ordinary pickup count. Display seconds from ticks divided by 30. The pistol/long-gun distinction matters when checking what counts as a shot. |
| Saving | changed | Same user-facing passport pattern, but classic II PC and PlayStation support save anywhere; no save-crystal gate. Save placed-item state selectively, then also creature mood/turn and carried drops, boat/snowmobile/lift state, active thrown flares with age and motion, current drawn long-gun animation, puzzle/keys, music flags, flipped room and fixed camera flags, and shared monk hostility. Some live effects and navigation are rebuilt or absent rather than a full-world snapshot. Do not infer that tr64's I save schema fits II or that the PC save's 6,272-byte world payload bound applies to a different schema. |

### Floor entries and trigger types

See the [I floor and trigger rules](triggers-and-effects.md#1-floor-data-entry-layout-facts-of-the-level-data). These dispositions distinguish a known field or visible outcome from the exact conditions for it.

| I floor entry | II disposition |
|---|---|
| 1 room portal | Retained as a sector entry. Exact room traversal and the height at which a door blocks or clears a portal are unknown. |
| 2 floor slope; 3 ceiling slope | Retained as sector entries. Exact walkability, collision and height composition with bridges and movable objects are unknown. |
| 4 trigger | Retained; the types and actions below are distinct, as in I. |
| 5 lethal floor | Retained in outcome: the lethal real-floor check and burn are established; the exact trigger conditions and interaction with an object floor are unknown beyond that check. |
| 6 climbable faces | New in II; see the climb mask above. Diagonal floor and ceiling triangles are absent from II. |

| Trigger type | II disposition |
|---|---|
| 0 ordinary | Retained as unqualified activation; exact per-tick activation, rearming and one-shot gates are unknown. |
| 1 pad | Unknown in detail: the exact floor-contact test and which new vehicle/ride states can press it are not established. |
| 2 switch | Retained in use; exact settle, timer/pulse, repeat, state release and mask toggle are unknown. CD-track handling distinguishes switch actions. |
| 3 key | Retained in use; the exact relationship between inserting a key, when Lara's hands are free, and when the target reacts is unknown. |
| 4 pickup | Unknown as an exact floor rule: Great Wall's bank tiger appears on taking the key, but the precise authored trigger and its once-only rule are not established. |
| 5 heavy | Retained; the first mine evaluates its sector in heavy mode. Which of I's other heavy activators still qualify is unknown. |
| 6 antipad | Retained as a distinct type in the published format; the exact II grounded condition, mask clearing and timing are unknown. |
| 7 combat | Unknown: neither I's drawn-weapon test nor which actions it allows is established for II. |
| 8 dummy | Unknown: retention of I's camera-refresh-only behaviour is not established. |
| 9 antitrigger | New in II; deactivates the listed object actions without requiring the antipad's floor contact. |

### Trigger actions

| Action | II disposition |
|---|---|
| 0 object | Retained; start/stop of authored items still matters. The exact mask, reverse, timer (including 1), one-shot and invisible-item wake-up rules are unknown. |
| 1 fixed camera | Retained; fixed and heavy cameras count down and end at −1. Exact trigger refresh, one-shot, arming timer and movement settings are unknown. |
| 2 sink/current | Unknown: I's next-waypoint pull, range and strength are not established for II. |
| 3 flip map | Retained; Venice's boat-lock change and restoring a saved alternate room show why it matters. Exact mask and flip ordering are unknown. |
| 4 force flip on; 5 force flip off | Unknown for each: neither I's complete-mask requirement nor the exact forced state changes are established. |
| 6 look-at target | Retained as a camera role; the trigger's target selection, hold/re-arm, “already looked at” conditions, and tracking limits are unknown. |
| 7 finish | Retained as a gameplay outcome: reaching the authored exit completes these levels; the exact action/animation dispatch and transition tick are unknown. |
| 8 soundtrack | Retained with changed timing and one-shot handling, as above. Ambient loops remain separate from individual track requests; exact ambient restart and fade on flip are unknown. |
| 9 effect | Retained as an authored special effect, e.g. the Home course timer. The exact relation between effect start and a room flip, including whether it can run without a flip in a given setup, is unknown. |
| 10 secret | Replaced as the ordinary collection/reward rule by picking up distinct dragon objects. Whether action 10 itself still has an effect in II is unknown. |
| 11 clear bodies | New relative to tr64's I campaign; removes corpses marked for cleanup. Exact eligibility and restore interaction are unknown. |
| 12 flyby; 13 cutscene | Absent in II as floor actions; separate scripted cinematics are retained. |
| 14–15 | Unknown in II; the I baseline assigns neither a behaviour. |

### Animation commands and effects

For command and effect numbers in I, see [I's animation commands](triggers-and-effects.md#11-animation-commands-executed-by-the-animation-of-any-item) and [I's effect list](triggers-and-effects.md#82-the-effect-list). A visible result does not prove that II uses the same numbered command or effect.

| I animation command | II disposition |
|---|---|
| 1 origin translation | Unknown: the exact end-frame, offsets and rotation rule are not established. |
| 2 launch velocity | Unknown: the exact animation-driven jump, gravity and stored launch override are not established. |
| 3 hands free | Unknown as a command: II still distinguishes available and occupied hands, but the frame and trigger for release are not established. |
| 4 deactivate | Unknown as a numbered command. The gondola stops moving after settling and ceases to update when disabled. Whether a numbered animation command causes that change is not established. |
| 5 animation sound | Unknown as a command: several traps have known sounds, but exact animation frame and land/water audibility rules are not established. |
| 6 animation effect | Unknown as a numbered command. Authored effects exist, including the Home course timer, but that does not establish the numbered animation command or when it invokes an effect. |

| I effect | II disposition |
|---|---|
| 0 turn 180° | Unknown as an animation effect. Turning around remains possible; the known blade turnaround is not proof of this effect. |
| 1 floor shake | Camera bounce retained; which animation frames request it and its old magnitude/distance rule are unknown. |
| 2 reset Lara to standing and restore view | Unknown for the same reset pose, view and timing; the in-level cinematics and their return are not proof of it. |
| 3 bubbles | Unknown for the I bubble effect number, emission cadence and lifetime. The documented II entry splash is distinct. |
| 4 finish level | Unknown when specified by an animation effect; successful exit and scripted level change remain. |
| 5 earthquake; 6 flood; 7 raising block; 8 stairs to slope; 9 drop sand; 10 power up; 11 explosion | Unknown for each numbered effect and its I sound/timing rule. The visible Venice flood and explosion effects do not establish I's flood or explosion effect numbers. |
| 12 hands free | Unknown as an effect; do not assume it is equivalent to animation command 3. |
| 13 flip map | Room flip retained; use of this animation effect number and exact side effects are unknown. |
| 14 right gun mesh swap | Unknown for this effect number and its I timing. II's shown weapons and flashes are separate evidence. |
| 15 chain block; 16 flicker | Unknown for each I timing and sound/flip series. |
| 17–32 and later effects | No I wall effect is established above 16. II has mesh, weapon and light effects and the Home course timer; do not map these to a number by analogy. At Home the start shows and starts a zeroed timer, stop leaves the elapsed time visible, and reset hides it and stops timing. Exact numbered assignments and all later effect rules are unknown. |

### Sizing and unknowns

The largest distinct additions, in rough descending implementation breadth (reader estimate, not measurements), are (1) speedboat and rider, (2) snowmobile and mounted AI, (3) wall climbing, (4) enemy target/hostility plus human combat, (5) the dragon finish encounter, (6) wading and water-transition integration, (7) long-gun and projectile combat, (8) held/world flares and lighting, (9) campaign/reward/starting-stock policy, and (10) extended save/restore. Zip-line mechanics and linked movers are smaller but can block the first levels. Exact geometry, trigger placement, animation counts and peak simultaneous populations require the player's level data. No memory budget, performance limit or Expansion Pak decision follows from this map.
