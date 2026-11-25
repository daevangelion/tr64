# Split-screen co-op

- In a playable level, press START on controller 2 to join as a second Lara; the top view is controller 1, the bottom view controller 2 (not the reference's side-by-side layout).
- Hold L and press START on controller 2 to leave; Lara 1 stays in the level, and a ring Lara 2 was driving passes to Lara 1.
- With two Laras, START on either controller opens the one full-screen inventory ring; the opener drives it, controller 1 wins simultaneous presses, and the ring pauses both players and the world until closed.
- Hold L and press A to warp to the other living Lara with full health and air; living warps are free, and reviving a dead Lara spends one shared large medipack or changes nothing.
- Joining is free; both joining and warping use the partner's last safe pose if it still matches the floor and water. Otherwise they use the partner's current stable pose, then the nearest valid sector centre in the room (including its connected floor or water below a shaft), or refuse if neither is safe.
- Health, air, hands, camera, hazards and input are independent; weapons owned, ammunition, medipacks, pickups, secrets and statistics are shared, and the statistics page is driven by controller 1.
- A dead Lara cannot revive herself with ordinary inventory use; the surviving player can continue, and the death page appears only after both are dead.
- Lara does not push or target her partner; enemies target the nearest living Lara, with ties going to controller 1 instead of controller 2 in the reference.
- The doppelganger mirrors the Lara nearest when it wakes until she leaves, and a shot reflects damage to its shooter, not necessarily to the mirrored Lara.
- Both players activate floor triggers and get their own fixed and look-at cameras; one-shot flags and map changes are shared, and heavy triggers use controller 1's camera.
- Both can use switches, blocks, keyholes, puzzle holes and pickups; for the same object the first actor to reserve it wins (controller 1 updates first), so it cannot grant duplicate awards.
- Leave and warp (including revival) change nothing while that Lara has a committed interaction still in progress. Try again when it settles; the other Lara can still warp to a living busy partner.
- A Lara who dies after committing stays in place until the action settles. Her own sector can fire its linked action at the normal tick, including on her camera. An owed Scion completion still happens once its normal animation time has elapsed and the ring is closed.
- Either eligible Lara can use Midas and gets her own animation and camera; a creature's grab or kill animation acts on its target, and contact hazards use the fixed game's damage rules.
- Lightning and emitter flames select one nearest living victim; contact hazards use each Lara's own collision, with a single-use strike or projectile consumed only once.
- The same level end advances both players regardless of which Lara triggers it; the engine rejoins Lara 2 after level starts and loads while the co-op session is active, but not on the title or in cutscene levels.
- Saves contain Lara 1 and the shared inventory and world. The passport says “Cannot save now” while Lara 2 has committed work or a dead Lara still owes an action. The ring pauses that work; the death page waits for it, but a level end discards work with the old level.
- Positional sound uses the closest present camera (including a fallen player's camera); the game emits each event once.
