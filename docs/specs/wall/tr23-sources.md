# Tomb Raider II and III: wall source ledger
Source A: Tomb1Main (`<remote-ssh>:~/src/Tomb1Main`), `3b2664bfc4db8e8f5f8d425109d89f3164128a6a`, Tomb Raider I (TombATI); `COPYING.md`: GNU General Public License, version 3. Files read: `README.md`, `COPYING.md`, `src/global/const.h`, checked-out Git ref metadata.
Source B: TR2Main (`<remote-ssh>:~/src/TR2Main`), `f38bc90438e21e8a37e915a812cade52efce1d68`; `COPYING.md`: GNU General Public License, version 3; file notices say version 3 or any later version. Files read: `README.md`, `COPYING.md`, `TR2_progress.txt`, checked-out Git ref metadata, and the selected files cited in the headers of `tr2-delta-map.md` and `tr2-first-levels.md`.
Source C: [Lost Artefacts TRX](https://github.com/LostArtefacts/TRX), `cf5bbaaa8eff9a29c036896b6a7975c58cae635a`, cloned in place at `<remote-ssh>:~/src/TRX`; `COPYING.md`: GNU General Public License, version 3. Files read for this addition: `README.md`, `COPYING.md`, and the checked-out Git revision. Reader for the addition: WallRE2.
Discovery for A and B: top-level directory names in `<remote-ssh>:~/src/`, the I project's configuration names and both projects' README descriptions. For C: public upstream project, checkout and licence verified on `<remote-host>`. No source checkout was copied off that host.
Reader: Wall23
Date: 2026-11-21

Source A covers Tomb Raider I for Windows and Unfinished Business. It is the same commit used by the earlier wall specifications; it does not cover Tomb Raider II. Source B covers parts of Tomb Raider II and the Golden Mask expansion. Its Windows rules draw on the original PC and PlayStation releases, but do not fully establish the behaviour of either. Optional variants are not taken as original retail behaviour.

## What Source B establishes for Tomb Raider II

Knowing an object's name, initial values or visible result does not establish the rule that produces its behaviour. The table distinguishes which rules are established and which remain unknown.

| Subsystem | Behaviour directly established | Behaviour not established by this source |
|---|---|---|
| Lara movement and water | Water-status transitions, wade thresholds, air/death accounting, swim acceleration/steering and roll entry, one jumping collision rule, ordinary splash. | General on-land state machine, animation movement/gravity integration, full landing damage, ladder probes and states, mid-air twist timing, most submerged collision, surface climb-out and currents. |
| Weapons and flares | Nominal weapon attributes, target retention and arm tracking, shotgun emission, M16 option distinction, player harpoon launch, grenade flight and impact, ordinary ammo rules, in-air flare display and active flare persistence. | Complete weapon draw, aiming lock and firing cycle, player harpoon flight/collision, held and thrown flare control, charge, light and bounce rules. |
| Creature AI | Initial stats and family distinctions, common retirement/drop and scripted kill handling, gun hit/miss/muzzle effects, hostile projectile control. | Creature slot and route selection, mood decisions, per-family state transitions and attack numbers, human shot probability and damage, distinguishing exact player/monk/mercenary target priority, and the two-actor dragon sequence. |
| Vehicles and zip line | Boat wake and gondola states, boat/snowmobile persistence, snowmobile visuals, weapons and explosion, mine/boat/rider detection and mine chain. | Boat board/drive/dismount, hull collision and buoyancy, snowmobile driving and mounted enemy AI, and the complete zip-line mount/ride/release rules. |
| Traps, movers, and interactions | Moving spike wall/ceiling, rolling and fixed blades, hook, statue attack, icicle, pendulum, jaws, darts, falling ceiling, fire, mine, some saved pickup/puzzle/window and block consequences. | Flying disc/propeller, boulder, stationary spikes, collapsing floor and trapdoor execution; window shatter, door blocking, lift movement, button/lever/key interactions, ordinary pickup and detonator. |
| World, cameras, progression, saves | Main play-cycle bookkeeping, CD track action, camera modes/Look/fixed movement and bounce, cinematic start/play/skip, game-flow interpretation and inventory effects, start stock and save payload/restoration, training timer. | Core floor/ceiling and trigger evaluation, normal switch/mask/timer and refresh rules, exact authored camera/door/mine events in each edition, all original bonus unlock conditions, original inventory and graphics variants where optional modern fixes differ. |

The table records the limits of source B, not the limits of the current wall record. Source C describes a single reimplementation for I, II and III, and its upstream reports all three playable end to end. Its patches and enhancements are not evidence of what the Windows retail executables do. A reader can use C to guide the work, but must compare reported retail rules with the pinned executable and the relevant authored data. Neither a README nor a walkthrough establishes exact edition parity.

## Tomb Raider III

Source C now provides an in-place Tomb Raider III reference. Neither A nor B covers III. This ledger establishes the availability and stated scope of that reference, not any III behaviour; no III implementation or runtime comparison was performed.

No executable or runtime evidence was used for this ledger, and no PlayStation runtime comparison was made.
