# Private ares SRAM persistence harness

This patch is for the locally patched ares source at revision
`d9117003192cd43806ddc24d20c764ddb9afc965`. The existing
`ares/n64/cartridge/testharness.cpp` preimage has Git blob hash
`10449ea6c4ddaa54fac83cbe24b440f8f32fa4ad`; this matters because the existing
local test harness is not described by the upstream revision alone.

Only test-host file import/export is added. No CPU, PI, SRAM, RDP, timing,
cartridge-detection or save emulation logic changes. With both variables unset,
the additional path does nothing. With the existing `ARES_TEST_HARNESS` enabled:

- `ARES_SRAM_IN=/absolute/input.ram` imports exactly the emulated SRAM size
  before boot. Wrong length, missing SRAM or an unreadable file exits with an
  error before guest execution.
- `ARES_SRAM_OUT=/absolute/output.ram` exports at the guest's test exit, before
  that exit terminates the emulator, and also when ares receives TERM or INT
  (a shipping cart has no test exit; `test/run/ares.sh` terminates with TERM).
  The signal path prints `TESTHARNESS SRAM out <file> (32768 bytes) on signal N`
  and leaves with status 128+N, without the emulator's own unload. Take the
  signal only after the guest finished its save (wait for its log line).
  Write failure fails the process.
- Files contain canonical guest-order bytes, accessed through the SRAM byte
  API rather than dumping the host's 16-bit backing array.
- OUT alone does not erase/reset/import SRAM. To start deterministically empty,
  supply an explicit 32 KiB input fixture (for example all `0xff`).

The ROM must separately declare `N64_ROM_SAVETYPE=sram256k`. ares' existing
Advanced Homebrew Header parser maps the save nibble 3 to 32 KiB SRAM. This
patch does not guess or override a ROM's declared medium.

## Build without touching shared output

The existing cache is CMake/Ninja, not make. `build-sram.py` extracts the exact
cached cartridge compile command and desktop link command, copies only the
small cartridge source directory, patches it and compiles one private object.
It links that object before the existing read-only core archive. All original
libraries/objects stay shared and read-only. Output, dependency file, link map
and temporary files are private. All post-link CMake/copy/install commands from
the shared recipe are discarded. LTO remains enabled, with its parallelism
limited to two jobs. The shared executable's SHA256 is checked before/after.

```
rtk python3 tools/ares/build-sram.py \
  --source $ARES_SRC \
  --cache $ARES_SRC/build \
  --scratch $TR64_WORK/tmp/MenuUI/ares-private2 \
  --output $TR64_WORK/builds/MenuUI/ares-sram2/ares
```

The observed entire shared cache was 265 MiB; it is not copied. The private
source view was 80 KiB. The builder emits an adjacent JSON manifest containing
revision, patch/source hashes, exact argument vectors and both executable
hashes. This is not an installation: other projects keep their existing ares.

Select the private executable for tr64 only with
`ARES_BINARY=/absolute/private/ares` through `test/run/ares.sh`. Its normal
private Xvfb, process groups, deadlines and shared
`$TR64_ARES_LOCKS` slot locks still apply. Do not start ares directly.

## Exercised power-cycle gate

`test/ui/save-cart-test.py` builds two explicit test-only main wrappers:
the first writes through the real pageable medium and SRAM PI backend, exits
the emulator, then the second imports that saved32KiB file and loads all three
slots plus settings. Its expected device is independently produced by the host
backend; all32768 guest-order bytes must match after both runs. A one-byte input
image must fail before guest execution. Pass an unused evidence directory:

```
rtk make BUILD_DIR=$TR64_WORK/builds/UI/save-tests save-test
rtk python3 test/ui/save-cart-test.py \
  --ares $TR64_WORK/builds/MenuUI/ares-sram2/ares \
  --host $TR64_WORK/builds/UI/save-tests/save-host-test \
  --pack $TR64_WORK/tmp/Cook/m5-title/campaign-title.pack \
  --build $TR64_WORK/builds/UI/save-cart \
  --out /absolute/fresh/evidence-directory
```


## Full game power cycle

`test/ui/menu-flow-test.py --rom <shipping-like ROM> --ares <private ares> --out
<fresh dir>` plays title, new game, a walk, the ring and the passport save in one
process, exports SRAM on TERM, then imports it in a second process, loads
"Current Position" from the title passport and checks the restored camera.
Both boots must report the expected source and configuration identities.

## Cart verdict identities

The build generates `generated/cart_identity.h` with the existing source
ID (`TR64_BUILD_ID`, still the fatal QR and archive key) and a separate
configuration SHA-256 (`TR64_CONFIG_ID`). `make cart-identity` generates
the sidecar from the same inputs without launching ares. The digest covers
declared inputs by content and resolved cart variables, with the toolchain
identified by the SDK pin and the source build's compiler fingerprint. It
shares the object and link prerequisite lists and sees the actual
`N64_TOOLFLAGS` and `N64_TOOLFILES` arguments. Register files read by cart flags
in `TR64_CART_INPUTS`, and generated packer members in `TR64_ROM_DEPS`; their
content changes invalidate cart objects. Undeclared flag inputs and search
paths outside the worktree, build directory and pinned SDK/toolchain roots
are refused, as are response files and specs. Flag values reject shell
metacharacters (`~ $`, backticks, `; | & < > * ? [ ] { } \ ! # ( )` and line
breaks). Quotes may not hide options or parts of words; only the SDK's literal
prefix map and whole packer operands such as the title and filenames can be
quoted. SDK tool variables must equal their one-word pinned executable paths;
wrappers and appended arguments are refused. This checks declared build inputs;
it is not a defence against edits to the build system itself.

Scene and direct-cart verdicts check both values from their build directory.
For a relocated ROM, `tools/ship/boot.py`, `tools/ship/perf_overlay.py`,
`test/ui/menu-flow-test.py` and `test/run/ares-binary-tests.py` accept
`--identity-header /path/to/cart_identity.h`; use the sidecar from the intended
build, not a header extracted from the ROM under test. `boot.py` also accepts
`--gate-identity-header` for a relocated gate ROM. Shipping additionally
requires the guest pack SHA-256 to match the supplied pack.

Sweep resume recomputes the ordered reference hashes and rebuilds the intended
cart locally. It checks the stored reference digest, guest boot record and
launcher ROM SHA-256 receipt. Legacy or rejected rows need
`sweep.py run --force` to replace them; reports omit their timings and pictures.

Exceptions: launcher lifecycle and preboot rejection controls need not reach
a guest boot and remain exempt. The fatal probe in a global constructor
never reaches the boot line; it is bound by the fatal QR check by default
and unbound when explicitly run with `--no-qr`.

## The same ares on the remote emulator host

`TR64_ARES_HOST=<remote-host>` makes `test/run/ares.sh` (and so `scene.sh --ares`,
`m0.sh --ares`, `fatal-tests.py`, `tools/ship/boot.py`) run ares on that
machine (`ssh <remote-ssh>`, 12 cores) through `test/run/ares-remote.sh`; see
that file's header for the contract and `test/run/ares-options.sh` for the one
list of ares options both machines use.

Built from the same ares revision and patches, not copied across (the hosts run
different distributions): `tools/ares/build-remote.sh` takes the files of
revision `d9117003192cd43806ddc24d20c764ddb9afc965` (`git archive` of
`$ARES_SRC`), applies `local-tree.patch` (the
uncommitted modifications of that shared checkout the shared binary and the
test harness rely on: host shots, test input, FlashRAM, the 64DD clock; the
preimage of the SRAM patch) and then `sram-persistence.patch`. It builds in
a private temporary directory and publishes `build/desktop-ui/ares`, `xdg/`
and `manifest.txt` under `~/tr64-ares`, not its source or build trees:

```
cmake -S src -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DARES_CORES=n64 -DARES_BUILD_LOCAL=ON
nice -n 19 ninja -j 4 -C build
```

Recorded build (2026-11-18, GCC 16.2.1, 2 min on 12 cores; the host's
`~/tr64-ares/manifest.txt` has the same lines): sha256 of
`local-tree.patch` `1e2b737630255296d5f9586b5324475eec62734666b086e08bf98123963c8f11`,
`sram-persistence.patch` `6bf150358953679b96567b02b8d277aa7480b1e261d947c4c52b8819cfbb9b50`,
the patched `ares/n64/cartridge/testharness.cpp`
`16a8f5052b9b2040e0da9f44f3ee82dc8e5cdc5542f029ad94efefa9d0d34c33` (equal to
`harness_postimage` of the local private build's manifest above),
`settings.bml` (this machine's `~/.local/share/ares`)
`0f5e1125cfd3a1f43f9a06e58ef26b40f9f5136757be1f6952821022b9b73d14`,
binary `b4d998764577d0deb33778b09afde434eb9603d92da3d120950aa9d2131a9655`.
Unlike the local private build (shared core archive, one private object, LTO
limited to two jobs) the host builds the whole tree: one ares, with the SRAM
import/export, for every run there; the host's `-march=native` is its own.

### Sampled two-pad input on <remote-host>

Only remote runs specifying `ARES_TEST_INPUT_2` use `~/tr64-ares-coop`.
The primary `.seq` path carries one append-only `pads-v2` journal: the first
line is `pads-v2`, then each row is `<port> <buttons-hex> <stick_x> <stick_y>`.
The relay acknowledges entries in one global order, including across ports:
before exposing a later row, it requires a full response to the previous
controller read. Renaming either staged temporary over the other pad is
rejected. The legacy one-pad `.seq` bytes and path are unchanged.

The producer records the actual button and axis bytes after writing the
full PIF response, with the global entry index, controller port and host
monotonic timestamp. The sample stream is separate from the guest console
so it cannot split a guest log line or be split by one. The relay records
each dispatch window and accepts only a complete sample line in that window;
the launcher saves and checks both files after the writer terminates. No
timely sample fails with `pad-unsampled` (66, port and entry); ordinary
launcher failures and user signals keep their own status. Before taking
a slot the launcher checks the pinned revision and ordered patch-content
identity, the binary hash and the matching string embedded in the binary.
The relay also requires the running emulator to announce the same identity
before it publishes any numbered input. A missing or mismatched identity
fails with 67. These are drift checks, not protection against a spoofing
binary.

A hardware sample is not a game input edge. The sampled driver primes
neutral for 32 independently acknowledged reads, holds the press until
the in-game effect, then releases for 32 more reads. At 60 Hz, 32 replies
span 517 ms, more than 2.5× the assumed 200 ms between guest polls.
The film decode and service measurements support that assumption but
do not bound it. If the guest still misses neutral, or any sampled press
has no effect, the co-op driver fails closed with `pad-no-effect` (68,
action, port and entry) after 60 host seconds. Film skips require the
actual `skipped` result; a film that plays through is rejected immediately.

```
ARES_JOBS=4 ARES_PATCHES="local-tree sram-persistence two-pad" \
  rtk tools/ares/build-remote.sh tr64-ares-coop
rtk python3 tools/ares/test_pad_relay.py
rtk python3 tools/ares/test_sampled_launcher.py
rtk python3 tools/ship/test_sampled_films.py
```

The default one-pad binary and journal forwarding are unchanged: the old
binary cannot identify sampled entries, so it still uses frame dwell with
a timeout. Moving that lane to sampled input would require selecting the
new binary for one-pad runs and validating boot films there; it is not part
of this cutover. The hardware-likeness variant remains separate.
The only change to one-pad runs is that a pre-launch refusal leaves no
`ares-host.txt` receipt. A missing receipt is a failed harness run, not
evidence of the ROM having launched.

Each remote run starts from a private copy of `~/tr64-ares/xdg` (ares'
settings and system directory, no Saves) and from no save file beside the ROM
(a local run finds `tr64.ram` / `tr64.pak` beside its ROM when an earlier run
left one). ares' RDP runs on the host's llvmpipe Vulkan driver, as locally
(`TR64_ARES_REMOTE_ICD`). The host keeps the last 50 run directories
(`~/tr64-runs`) and bounds its own concurrency by `TR64_ARES_REMOTE_SLOTS`
(default 6), independent of the two slots of this machine.

### Measured equivalence, this machine against the host (2026-11-18)

Same ROM, same command, `TR64_ARES_HOST` unset against `<remote-host>`. Emulated
results are identical wherever the input is fixed:

| run | result | wall, here (incl. queue for the 2 local slots) | wall, host (incl. staging, copy-back) |
|---|---|---|---|
| fatal control (exception ROM, shipping profile), `ares.sh` direct | exit 1 both; `ares-test-0000.ppm` md5 `15a777a9473df0e57d1ca030d7ec3d0e` both; log identical | ares 2.9 s (2 min with the queue) | ares 1.3 s (5.4 s) |
| M0 boot scene `test/scenes/boot.route` | `scene PASS` line identical (state `b84a49c7`, card hash `ca8c4f56...`, raw shot `7b5c69cb...`); picture identical; `engine.log` identical | ares 51 s (13 min 50 s) | ares 2.4 s (32 s) |
| bridge room `m3-level1-bridges-live` (180 ticks, PC pack) | all 181 `game-tick` hashes equal; the 182 `done_us`/`tick_us` frame times equal (they are emulated time); picture md5 `a32cc6efbda5def8923e79767db2795b`, raw shot `f8b6a0c9...` (the value `docs/architecture.md` section 6.1 records); whole log identical | ares 324 s (6 min 10 s) | ares 12.8 s (39 s) |
| shipping gate base run + power cycle (`boot.py --reload --sound --save-load`) | `pass` true both; same level sequence `title, level1, level1, level2, level1`; ring, forward walk, sound (0 underruns) and save/load checks equal | 46 min (first attempt: the 1500 s deadline ran out on the loaded machine at 657 frames) | 2 min 30 s (base 81 s, power cycle 27 s) |

Two lines differ in a log, both host noise: `OpenGL: Failed to load
librashader` (only here) and `[WARN]: Stalled compile (compute, ...)` (host
wall-clock shader-compile times; three lines there, five here). What is not
equal between two live-driven gate runs is the `play: start_hash=level1` of the
new game: it depends on the tick at which the injected New Game press lands
(89 to 101 over five runs, also between two runs on the host), so three runs
on the host gave `be2c2417`, `2bd38d42` and `3de93ec1` and the run here
`6ae75006`. A static-input scene has no such freedom, hence the scenes above.
Run on the host with the NVIDIA ICD (`TR64_ARES_REMOTE_ICD=nvidia_icd.json`) the
bridge scene gives the same log and picture; llvmpipe stays the default because
this machine runs it.

Not runnable on the host: `test/run/controls.py` (local process-group, slot-lock
and SIGKILL controls) and `test/run/ares-binary-tests.py` (a local `ARES_BINARY`
override). `ares.pid`, `xvfb.pid` and `xvfb.dir` are not written for a remote
run. A SIGKILLed local runner leaves the host run to its own deadline.

## Hardware-likeness lane (`TR64_ARES_HW=1`)

The first run on real hardware (a SummerCart64 in an FPGA console, not a VR4300) died at the
first code-unit call although ares ran the same ROM. The cause found from the core's RTL and
confirmed by `tools/hwprobe/`: with Status.KX=1 the core does not translate an instruction fetch
whose address has bits 63:60 >= 8 (every sign-extended kseg2/kseg3 PC, `0xFFFFFFFFC1000000`); it
takes the low 29 bits as a physical, uncached address. Resident text at `0xC0001000` worked by
coincidence (the wired entries map 0xC0000000+x to physical x); a unit at `0xC1000000` ran the
RAM-less region at physical `0x01000000`. ares fetched through the TLB, so nothing could show it.

`hw-likeness.patch` (against revision `d9117003...`, applied after `local-tree.patch` and
`sram-persistence.patch`; it touches only `ares/n64/cpu/cpu.hpp`, `cpu.cpp`,
`interpreter-ipu.cpp`) adds two switches read at power-on from `ARES_HW_LIKE`, a comma list:

- `fetch`: in 64-bit kernel mode a PC in a mapped segment with bits 63:60 >= 8 is fetched at
  `PC & 0x1FFFFFFF`, uncached, ignoring the TLB (the recompiler needs a cached access and so
  falls back to the interpreter for those fetches).
- `icache`: `CACHE Hit_Invalidate_I` clears the line at the index of the translated address,
  without a tag compare (the core indexes cache operations by the physical address and fills by
  the virtual one; `Index_Invalidate_I` is unchanged).

Unset or empty, ares behaves as before. What ares models of the instruction side, by its
source: the instruction cache (512 lines of 32 bytes, indexed by virtual address bits 13:5, tag
the physical page) is emulated in the interpreter, which also fetches stale lines; the
recompiler fetches at block compile time and re-checks coherence against RAM. The TLB is
modelled with the EntryHi region bits in 64-bit mode (`kernelSegment64`), but there is no
instruction micro-TLB, no data-cache aliasing between virtual colours beyond the line index, no
undefined power-on state, no PI timing. `fetch` is the behaviour of the core that explains the
hardware record (read from its source and reproduced here); `icache` is read from the source too
and was not confirmed by any hardware or emulator evidence, so it is opt-in. Nothing else about
the core is modelled.

Build (a separate directory on the host; the shared `~/tr64-ares` is not touched):

```
ARES_PATCHES="local-tree sram-persistence hw-likeness" tools/ares/build-remote.sh tr64-ares-hw
```

Run: `TR64_ARES_HOST=<remote-host> TR64_ARES_HW=1 test/run/ares.sh ...` (any harness that calls it;
`TR64_ARES_HW_LIKE=fetch,icache` adds the second switch). The run uses `~/tr64-ares-hw`,
`ARES_HW_LIKE=fetch` and `--setting General/ForceInterpreter=true`; a local run refuses
(`ares.sh` exits 2). `test/run/m0.sh --hw` (with `TR64_ARES_HOST=<remote-host>`) runs the boot scene
in the lane. Measured (2026-11-19, boot scene, test profile, `fetch` only): the ROM of `main`
(SDK `0822b98aa06b`) fails, `EXCEPTION 2 EPC C3F0000C: Read from invalid memory address`, BadVAddr
`FFFFFFFFFFFFC0C0`, the record of the user's photograph; the same ROM passes with `icache` alone and in
plain ares (`scene PASS`, state `b84a49c7`); a ROM built on SDK `dde6197a28aa` (KX/SX/UX clear in tlbcode
builds) passes in the lane with `fetch` and with `fetch,icache`, same state and picture.
