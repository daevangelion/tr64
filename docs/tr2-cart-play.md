# M10: The Great Wall on the 4 MiB play cart

This is the first controller-driven Tomb Raider II cart, not a playable
campaign. It boots directly into The Great Wall. Its simulation is the
reviewed Great Wall profile at `195312c`, including tiger and small
spider navigation, contact and pistol combat. The host keeps both
games; the two carts fold `TR64_RULE` at compile time.
This patch does not implement or retune movement, camera, arms, creature,
interaction or progression rules. See [D17](architecture.md#11-decisions)
and the [cart image measurement](tr2-cart-image.md).

The ROM and its proof live under **`$TR64_WORK/evidence/m10/cart-play/v2/`**
(`E` below). The external PC Wall pack is the previously admitted
`$TR64_WORK/evidence/m10/wall/wall.pack`, SHA-256
`d5e1cb90646be0bdb94545e369eca0c118a5684a68cbcc9e2c454de73a59a02e`.
The 4 MiB emulator reports both hardware and SDK sizes as 4,194,304 B.
No assets, ROMs, screenshots or retail inputs are committed.

## Runtime boundary

* The front door and scenes share `tr64_wall_load`: refuse non-Wall names
  before the pack bind; bind live rendering; load Core/TX64, RG64 and MG64;
  start the existing rules; then close the load lease before play. The
  extra native model-domain and floor/topology checks stay in `load`, called through
  its normal dispatch table with a nested lease. An ordinary Wall boot
  acquires the unit **once**, even though admission calls it again. The
  headless API can still acquire its own scope. No resident relocation
  into validator code and no allow-list entry were added.
* The cartridge play loop samples the pad, runs the existing 30 Hz pacing
  accumulator and draws once after due ticks. A jumps, B is Action, Z
  selects pistols, L selects a flare, R is Walk; C-Up Look, C-Down Roll,
  C-Left/Right sidestep. START opens a limitations card instead of
  starting a TR1 inventory; a second press resumes. The boot card and
  startup log name unavailable sound playback, saves, inventory ring,
  HUD, title, level flow and cut scenes. Other maps remain unsupported
  for gameplay; the posed scene path can still inspect their cooked data.
  The native audio service initializes its existing workspaces for SN64
  admission; no sequel effect bank, mixer tick or soundtrack is played.
* The renderer consumes the bounded sequel rotation stream after its
  nine-word header, reusing the packed pose/arm helpers already used by
  the simulation. It renders Lara and live/placed items, not the old
  posed camera. Key/dragon sprites in the sequel ID range are not silently
  dropped. Procedural flare light, gun flash, blood, ricochets, spider
  shards, bubbles and the sky dome are not rendered here. Giant spiders
  refuse at wake; the Tyrannosaurus controller refuses the fatal special
  Lara pose. The cart cannot reach Venice or open a usable inventory,
  including the native ring used for shotgun selection. The scene-only
  authored placement hook is not an interactive teleport command.
* `TR64_TR1_CODE_UNIT_FRAGMENTS` and `TR64_TR2_CODE_UNIT_FRAGMENTS` select
  game-specific overlays using the existing unit generator, with a shared
  list for the boot/resource units. The host takes both registries and
  gets missing page sizes from an ordinary link of the other game's cart;
  common unit sizes and the host carve's image extent come from the
  selected cart. This avoids fake zero-sized unit entries. Beyond
  `init`, `test`, and `load`, the TR2 cart packs `tr2_wall` and
  `tr2_creatures`. After native admission and before placed-item
  initialization (which can wake items), one scan selects the former
  for placed helicopter 259 or campfire 253, and the latter for small
  spider 36, crow 38, tiger 39 or Tyrannosaurus 214. No creature code is
  loaded during a frame.
  The first unit runs helicopter and campfire updates. The second runs
  species control, wake, navigation, stride and creature-only contact
  scans. Type and slot lookup remain resident because the arms and
  sphere paths use them. The contact reset, geometry, pistol/flare
  and shared room-effect routines stay resident too: a pistol effect
  can occur without a creature, and a spider's shards are effects,
  not dynamically spawned controller-bearing items. The loose flare
  (152) likewise shares the resident arms path. The inert disc launcher
  (62) will need its spawned disc (61) in the same family closure when
  implemented. TR1 encounter constructors, child spawning, gameplay
  dispatch and inventory/save/media roots do not link. Missing sequel
  operations refuse by name rather than through TR1's numeric namespace.
* Route v4 gives TR2 schemas 20 and 21 a 32-bit input word so
  C-Left's `65536` round-trips. Admission rejects undefined bits in
  the event even with a valid CRC. TR1's v3 wire and cart input ABI
  are unchanged.
* The host and TR2 cart preflight each unit against currently free
  code frames, naming the unit and level. Open item: the first-game
  cart keeps the total-frames comparison in `code.c`; fold the
  free-frames preflight in the next time its resident image changes
  for another reason.

## Memory and placement

No budget row changed. The live ELF is **461,128 / 622,592 B** resident,
with **161,464 B** free in the existing image row. The resident-creature
control link was 469,320 B; selecting its controller family saves 8,192 B
of physical extent and 12,160 B of resident text, for 16,384 B more
page-rounded ROM payload (`layout-comparison.tsv`). The 70 KiB reserve
remains unassigned. The cart's hot render window is 16,288 / 16,384 B;
its hot data phase stays 1,024 and all carved pools stay 4,096 modulo
8 KiB. Its scene draw stack path is 1,008 / 3,360 B (room/item), eight
bytes shallower than TR1 after removing the split-preview wrapper edge;
the executable gate checks the profile-specific value with the same
control-flow analysis. `refs-check`: five dispatch-table references and
zero allow-listed references on game 2. `link-check.log` verifies the
positive and negative symbol inventory in the actual ELF.

| Live cart row | Budget B | Peak B | Free at peak B |
|---|---:|---:|---:|
| Image | 622,592 | 461,128 | 161,464 |
| Stack and boot | 65,536 | 65,536 | 0 |
| Framebuffers | 307,200 | 307,200 | 0 |
| Z buffer | 153,600 | 153,600 | 0 |
| Code | 98,304 | 61,440 | 36,864 |
| GPU | 237,568 | 230,608 | 6,960 |
| Audio | 131,072 | 127,584 | 3,488 |
| Library heap | 65,536 | 1,328 | 64,208 |
| Level | 2,088,960 | 1,406,320 | 682,640 |
| State | 303,104 | 148,384 | 154,720 |
| I/O | 49,152 | 43,600 | 5,552 |

Code peaks at **15 of 24 frames** during the overlap of `load` (10 pages),
the world-item unit (one page), and the creature unit (four pages).
That's eight TLB pairs plus the wired entry. The two placed units leave
five frames and three pairs mapped in play. The live `load` acquisition
was 8,466 µs and release 344 µs on ares; that is not a measurement of
the isolated creature-unit load cost. Stack and surfaces use their
reserved sizes. The measured state includes five sequel navigation
slots, room effects, the live renderer workspace and drawable chunk;
it is not a whole-campaign projection. `live-memory.tsv` comes from
`live/ares.log`; `live-events.txt` records leases and pad transitions.

## Verification

The same route bytes drive host and cartridge in `test/run/scene.py`'s
scene format. The cart calls the existing scene tick; the interactive
boot is checked separately with sampled controller input. Each schema-21
hash comes from the shared writer with its output callback null, not a
fixture. It includes the camera, Lara and arms, active items and creature
brains, inventory, stats, world digests and RNG. This is host/cart
agreement, not a new retail comparison or a full memory hash.

| Scene | Equal host/cart state hashes (including tick zero) |
|---|---:|
| Stand, authored start | 301 / 301 |
| Movement: walk, run, jump, turn, roll and right sidestep | 274 / 274 |
| Ledge grab, shimmy, release and pull-up | 356 / 356 |
| Pool: wade, surface and dive | 659 / 659 |
| Flare | 127 / 127 |
| Pistols | 116 / 116 |
| Left sidestep | 79 / 79 |
| Tiger cave approach | 464 / 464 |
| Pistols against small spiders | 381 / 381 |
| **Total** | **2,757 / 2,757** |

The tiger acquired its first brain at tick 396 and moved; the spider
route recorded two kills, 26 shots and 11 hits. Both complete direct
native traces match the resident-creature control byte-for-byte and
their record hashes match the host and ares scenes. Clearing only bit
16 in the left-sidestep fixture changes the hash at tick 1. See
`scenes.log`, `parity.log` and `native-witness.log`.

The route plugin requires contiguous hashes, nonempty world draws and a
320×240 multicolour image, one measured frame per simulation tick, and a
single balanced `load` lease plus both placed controller units. The
artifact contains host/ares views of the world and Lara; the final
interactive card and scene composition were inspected. No exact pixel
equality with ares or retail is asserted. The booted live ROM responded
to Forward (Lara moved from `62976,4083,25040` to `63926,4815,25284`),
showed the inventory refusal on START, then resumed and rendered.
`live/vi` contains the real limitation card and world frames.

| ares frame work, moving frames only | Frames | Mean | Worst |
|---|---:|---:|---:|
| Opening area (`movement`, Lara moves in the starting room) | 232 | 8.220 ms | 11.043 ms |
| Pool cave (`pool-swim`, Lara moves in Wade/Surface/Under) | 195 | 10.622 ms | 11.841 ms |

Frame work is the existing scene tick-to-GPU-completion timestamp
minus observed framebuffer pacing and state-hash/log diagnostics.
`metrics.py` verifies the host trace hashes before using its positions
and medium to select the cart samples. These are ares work intervals
with audio inactive and no live catch-up; not readback/presentation
latency, worst case over all camera poses, or VR4300 hardware timing.
The scene ELF's image is 461,112 B (16 B below the interactive link);
loading and scene-state peaks are the same as above.

**Regression controls.** Both native selectors passed 113 production
runs each. The admission suite covers 20 cases, including unsupported
model IDs 265 and 65,535 in CRC-valid Core tables. The 314-check
native unit suite covers the v4 high input bit, undefined input with
a valid CRC, and a video unit that fits alone but not beside `load`;
the error must name `video` and `code-overlap`. A non-Wall start names
`boat`, the hot-stack gate rejects the TR1 depth for the TR2 link, and
the parity comparator rejects a substituted hash at tick 1.
Selection controls preserve the full `placed-travel` trace and prove
that removing either placed family from an integrity-valid native pack
maps only the other family. See `build.log`, `selftest.log`,
`negative.log`, `overlay-control.log` and `native-witness.log`.

TR1's shipping `.text` is byte-identical at 437,744 B (`0aacd23c…`);
the original hot window, cache phase, carve, and 1,016 / 3,368 B entry
stack are unchanged. Its 25 solo fixtures match in each of schemas 2–5.
The vectors, boot and all 21 paged executable sections also match v1
byte-for-byte at the same addresses. The full `tools/ship/ship.sh check`
ran on this rebased tree and passed with Expansion Pak disabled:
video, cutscene, death/end, pacing, sound, and save/reload including
power-cycle. TR1 shipping ROM MD5 `98d2ab17a66432a8cbeece83476f2f39`.
See `tr1-gates.log`, `tr1/text.delta`, `tr1-ship.log` and
`tr1/ship-summary.json`.

## Reproduce

From a disposable worktree at the base with the reviewed patch applied:

```sh
export TMPDIR=$TR64_WORK/tmp/T2Cart PYTHONDONTWRITEBYTECODE=1
export TR64_ARES_HOST=<remote-host> TR64_TIMEOUT_SCALE=8
E=$TR64_WORK/evidence/m10/cart-play/v2
B=$TR64_WORK/builds/T2Cart
W=$TR64_WORK/evidence/m10/wall/wall.pack
rtk mkdir -p "$TMPDIR"
rtk proxy make -j2 BUILD_DIR="$B" TR64_GAME=2 TR64_PLAY=1 TR64_TEST=0 \
    TR64_LEVEL_PACK="$W" rom host n64-core-tests
rtk bash "$E/snapshot-live.sh"
PYTHONPATH=tools/ship:test/run rtk python3 "$E/live-pad.py" "$B" "$E/live-replay"
rtk bash "$E/live-rows.sh" "$E/live-replay/ares.log"
PYTHONPATH=tools/cook rtk python3 "$E/overlay-control.py" "$B/host/tr64-host" "$W"
rtk bash "$E/run-scenes.sh" stand movement ledge-grab pool-swim flare pistols \
    sidestep-blocked tiger-cave spider-shoot
rtk python3 test/game/tr2-cart-test.py compare "$E"
rtk python3 "$E/native-witness.py" "$B/host/tr64-host" "$W" "$TMPDIR"
rtk python3 "$E/metrics.py" "$B/host/tr64-host" "$W" "$TMPDIR"
rtk bash "$E/negative.sh"
rtk bash "$E/tr1-gates.sh"
rtk bash "$E/tr1-ship.sh"
```

Only controlled fixtures were exercised. No real-console run,
end-to-end Great Wall completion, retail presentation parity, or
isolated creature-overlay load/dispatch timing was measured.
