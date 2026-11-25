# Great Wall block, hinged trapdoor and collapsing slabs

The Wall rules below are shared with TR1. No second-game-only controller
copy remains. Citations are to `docs/specs/wall/tr2-great-wall-objects.md`
(“Objects”), `tr2-water-and-flares.md` (“Water”) and
`tr2-triggers-and-interaction.md` (“Interaction”). The port
uses the public TR2 slot IDs from `game/tr2.h` and the placed models'
authored clip banks; Lara's shared block-ready ID is `Lara_ANIM_BLOCK_READY`.

| Inherited routine | TR2 rule or binding | New work and why |
|---|---|---|
| `Block_collide` in `game/object_block.c` | Objects, Grasp and move: pre-step stand, held Forward or Back, block's own room, cardinal alignment; commit at the authored end of readiness; push has priority. | The pre-step reach helper and exact cardinal alignment are conditional; the shared goal selection, activation and authored advancement still perform the move. No TR1 co-op claim or inventory is used in TR2. |
| `Block_checkObstacles` and `Block_checkBlocking` | Objects, Candidate move: both directions require the same base heights and clearance; push rejects a slope and probes 100 lower; pull additionally checks farther Lara retreat. | TR2 branches add scenery-volume tests and named stops for a creature in the destination or a wet destination. Missing or lower destination floor rejects a move before it starts. Loss of support on an active tick, before occupancy, stops by name. |
| `Block_updateFloor`, `Block_update` | Objects, Occupied and vacated support; Recorded push, pull and refused retreat. | Resident `game_tr2_block_occupancy` changes the sector and TR2 box flag together; it also rebuilds resting cells on a flip. The block settles before the existing heavy-trigger handoff; `tr2_wall` returns before the resident floor evaluator can wake a creature. |
| Original door/hinge animation in `Object_update` | Objects, Trapdoor opening and reset: opening and closing update the same state machine as in I; an item's current state selects support, not its goal. | `Door_animate` factors the old inline animation body; the TR1 ordinary-door topology update stays at its old site. TR2 binds the shared body to the hinge; timed re-closing is checked with a directed pack. |
| `ItemObj_getTrapDoorFloor` and `ItemObj_getItemFloorCeiling` | Objects, exact collapsing slab queries (Q-INT-07) and hinge support. | TR2 maps 55/57 and 114 to the shared substitution logic. Both planes use the linked floor record; type 57 raises the slab to 1024 above origin. The existing local hinge footprint check is unchanged. |
| `TrapFloor_update` in `game/object_trap.c` | Objects, What starts a tile; Wake is not the falling tick: 512/1024 exact feet-height gate, pre-update state 1 requests 2, then the existing fall, rest and retirement. | TR2 supplies its sole Lara and its type's height. The state-1 request and end-of-clip deactivation were already present; no new timer or slab controller is needed. |
| `game_tr2_water_status` in `game/lara.c` | Water, dry entry into a deep column: current 35 and goal 17. | TR2 explicitly assigns the current dive state rather than inferring it from an animation's state label. |
| Existing pickup scan and credit | Interaction, Shared pickup frame: both eligible co-located pickups commit without held Action. | No runtime edit. `shotgun-ammo` now has a positive two-pickup control and a separately displaced-medikit negative control. No shotgun weapon-selection or firing logic is part of this change. |

TR2 blocks and slabs share the existing `tr2_wall` placement-selected
lease; block contact also enters it through `code_call`. TR1 keeps the
original `objects` mapping. Block occupancy, slab queries and the heavy
action interpreter are resident in TR2, including after a placed actor
returns from the unit. An in-progress block leaves both occupancy and
its navigation box clear on topology reconstruction. Save/reload stays
unavailable via the existing TR2 frontend boundary; no new loading API
or save round-trip is claimed.

## Published record outcomes

`$TR64_WORK/evidence/m10/interact-followups/` contains the replay
and comparison logs. Counts include tick zero. Gameplay RNG is gated;
the presentation stream stays visible and ungated.

| Record | Outcome |
|---|---|
| `block-cycle`, `block-refused` | 527 and 92 equal rows, sealed |
| `trapdoor-fall`, `collapse-cross` | 131 and 131 equal rows, sealed |
| `collapse-stand` | 46 equal rows, then named refusal at spike 45 on tick 46 |
| `collapse-air` | 73 equal rows, then named refusal at spike 52 on tick 73 |
| `shotgun-ammo` | 81 equal rows on the published schema-20 cassette; both pickups commit on tick 43 |

The two spike traces are not sealed. No blood, damage, post-spike slab
settlement or RNG draw was synthesized in this slice. The independently
crossed slab completes its fall and deactivation on the sealed route.

Directed controls cover both end strides, the refused retreat and
authored hands-free cue, a blocked sloped push with both directions
held, a block landing on a heavy trigger that wakes a tiger, a tiger
already standing in the push destination, the slab height gate for
both types, retirement after crossing, and timed hinge re-closing.
Load-time negatives reject a resting state on a moving block clip, an
end move that is not one tile, a hinge or tile state outside 0/1 or
0–3, and a hinge frame link outside its target clip. The published
route or synthetic pack is named in each control; the synthetic
results are not retail parity claims.

## Size comparison

The discarded dedicated-TR2 draft was built far enough to obtain its
TR2 ELF gates (`initial-build.log`): 493,896 B physical resident image,
486,396 B unpadded resident sections, one `tr2_wall` page. It is a
link-only control, not an accepted gameplay build. The post-split
shared-controller link (`split-layout.log`) keeps the physical image at
493,896 B and uses 483,980 B unpadded resident sections (2,416 B less).
`tr2_wall` is 5,184 B before padding and uses two pages, one more than
the draft; `tr2_creatures` remains four pages (14,528 B before padding).
The load-time walk of the block, hinge and both tile banks rejects a
state outside the consumer's set, a frame link outside the target clip,
and a block end move that is not one tile. That check is resident.
The image after it (`slab` host link) is still 493,896 B physical and
484,876 B unpadded (896 B more than the post-split link). `tr2_wall`
stays two pages and `tr2_creatures` four. The hot window remains
16,288 B and the paging gate reports five dispatch references and no
allow-list use. No budget row changed.
No emulator was run.
