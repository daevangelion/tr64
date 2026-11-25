# Great Wall cart: HUD and inventory design

Reuse the first game's bar, text and pickup painters, frozen-world composition,
ring transitions and geometry (`game/inventory*.c`, `engine/ui/`), and the
existing inventory view and level-statistics DTO. No new framebuffer, render
path or level pool. Keep the first-game cart's original compilation branches,
unit map and string tables.

TR2 supplies descriptors (authored model IDs; level-specific labels; supported
actions), the carried main and key rings in their existing priority order, and
the options-page boundary. Main starts with stopwatch, pistols, shotgun, flares
and the two medikits. Show the saved clock, distance, kills, shots, hits,
medikits and dragons, not TR1's pickup tally. Weapon/flare choices feed their
existing gameplay controllers; medikits use the inherited heal rule against
sequel stock. A key choice returns to the existing contact/commit point in the
same update; it is not a grant, a preselected matching key, or a new tick.
Presentation drives the same choice operation as the v4 route event; saved
time includes paused menu ticks but menus advance no world ticks. References:
[interaction and ring order](specs/wall/tr2-triggers-and-interaction.md),
[recorded menu events and statistics](specs/wall/tr2-oracle-trace.md),
[Great Wall starting stock](specs/wall/tr2-lara-start.md).

Title, level transition, saving/loading (including the passport) and sound
playback are out of scope and remain absent or refused by name. Unimplemented
weapons and unsupported puzzle use do not borrow another game's rules.

Memory plan (pre-link): keep the existing 622,592 B image row (493,896 B at
base), and borrow the same two overlay leases as TR1: `inv` for ring state
operations and `front` for painting. Their TR1 links use 3 + 2 pages; plan for
at most 6 additional pages (24 KiB) over the five placed-controller pages, so
an open menu uses at most 11 of 24 frames (45,056 B, at least 53,248 B free).
`load` is already released before play, so its overlap remains a separate peak.
The existing `GameInventory` and drawable chunk already exist; no added level
state allocation is planned. UI pickup tracking uses bounded resident state and
the front view one stack frame; measure both at link and report all rows from
the running cart. Front is leased only for the ring or live HUD paint that needs
it; no ring-open copy of the play scene. No budget row change is proposed.
