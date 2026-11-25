# Building tr64

`tr64` builds playable **stock 4 MiB Nintendo 64 ROMs** (`.z64`) for **Tomb Raider I**, **Tomb Raider II**, and **Tomb Raider III** from your own **Steam** or **CD-ROM** copy of the game. No retail game data is included in this repository.

---

## 1. Repositories Used

| Component | Repository | Branch / Commit |
| --- | --- | --- |
| **tr64** | [`daevangelion/tr64`](https://github.com/daevangelion/tr64) | `main` |
| **libdragon** | [`daevangelion/libdragon`](https://github.com/daevangelion/libdragon/tree/tlbcode) | `tlbcode` (`dde6197a28aaa26b721685bfd5f2eabd32e89aa5`) |
| **tiny3d** | [`daevangelion/tiny3d`](https://github.com/daevangelion/tiny3d/tree/tr64) | `tr64` (`485fe3ad0dfc5458d84aeac8b41b9f2427e50c71`) |

---

## 2. Host Prerequisites (Linux)

Install standard host build tools and Python packages:

```bash
# Debian / Ubuntu example
sudo apt install build-essential git make python3 python3-numpy python3-pil liblz4-1
```

Optional:
- `ffmpeg` — only needed if you want to cook Tomb Raider I CD-DA music (`--music`) or FMV cutscenes (`--fmv`).

You also need a **`mips64-elf` GCC toolchain** (with `bin/mips64-elf-gcc`, `as`, `ld`, `objcopy`, `objdump`, `nm`, `size`, `strip`) — for example from libdragon's `toolchain-continuous-prerelease` release or built via libdragon's `tools/build-toolchain.sh`.

---

## 3. One-Time Setup: Clone & Build the Pinned N64 SDK

```bash
mkdir -p ~/src ~/sdks

# Clone tr64 and the two pinned SDK forks
git clone https://github.com/daevangelion/tr64.git                              ~/src/tr64
git clone -b tlbcode https://github.com/daevangelion/libdragon.git              ~/src/libdragon-tr64
git clone -b tr64    https://github.com/daevangelion/tiny3d.git                 ~/src/tiny3d-tr64

# Point to your mips64-elf toolchain prefix
export N64_GCCPREFIX=/path/to/mips64-toolchain

# Build and install the pinned libdragon + tiny3d SDK
cd ~/src/tr64
python3 plat/n64/build_sdk.py \
  --libdragon-repo ~/src/libdragon-tr64 \
  --tiny3d-repo ~/src/tiny3d-tr64 \
  --gcc-prefix "$N64_GCCPREFIX" \
  --libdragon-rev dde6197a28aaa26b721685bfd5f2eabd32e89aa5 \
  --tiny3d-rev 485fe3ad0dfc5458d84aeac8b41b9f2427e50c71 \
  --work /tmp/n64-sdk-work \
  --prefix ~/sdks/n64-sdk-tr64 \
  --jobs "$(nproc)"

rm -rf /tmp/n64-sdk-work
export N64_INST=~/sdks/n64-sdk-tr64
```

---

## 4. Build a ROM (One Command)

Point `tools/build.py --input` straight at your Steam install folder, CD-ROM folder, `.iso`, `GAME.GOG`, or PlayStation `.cue`. It auto-detects the game, edition, sound bank, and title screen case-insensitively.

### Tomb Raider I
```bash
# Steam install (reads GAME.GOG directly — no manual extraction needed) or PC CD-ROM folder
python3 tools/build.py \
  --input "/path/to/Steam/steamapps/common/Tomb Raider (I)" \
  --out tr64-tr1.z64

# PlayStation CD-ROM BIN/CUE (with optional CD-DA music and FMV cutscenes)
python3 tools/build.py \
  --input /path/to/tr1-psx.cue \
  --music --fmv \
  --out tr64-tr1-psx.z64
```

### Tomb Raider II
```bash
python3 tools/build.py \
  --input "/path/to/Steam/steamapps/common/Tomb Raider (II)" \
  --out tr64-tr2.z64
```
Default levels: Title, Great Wall, Venice, Bartoli's Hideout, Opera House, Lara's Home. Pass `--levels all` or a comma-separated list (e.g. `--levels wall,boat,venice,opera`) to change what is packed.

### Tomb Raider III
```bash
python3 tools/build.py \
  --input "/path/to/Steam/steamapps/common/Tomb Raider (III)" \
  --out tr64-tr3.z64
```
Default levels: Title, Jungle, Temple Ruins. Pass `--levels all` or a comma-separated list to customize.

---

## 5. Optional: Cross-Game Lara Moves

To unlock the **Lara Moves** option (`TR I` / `TR II` / `TR III`) in the in-game Options ring of TR1 or TR2, pass your TR2 and/or TR3 install folders (or `WALL.TR2` / `JUNGLE.TR2` files) as donors:

```bash
# TR1 with TR2 + TR3 moves available in Options
python3 tools/build.py \
  --input "/path/to/Tomb Raider (I)" \
  --lara-tr2-donor "/path/to/Tomb Raider (II)" \
  --lara-tr3-donor "/path/to/Tomb Raider (III)" \
  --out tr64-tr1-moves.z64

# TR2 with TR3 moves available in Options
python3 tools/build.py \
  --input "/path/to/Tomb Raider (II)" \
  --lara-tr3-donor "/path/to/Tomb Raider (III)" \
  --out tr64-tr2-moves.z64
```

---

## 6. Supported Input Layouts

| Game | Steam Install | CD-ROM / Disc Image |
| --- | --- | --- |
| **Tomb Raider I** | `Tomb Raider (I)/` (contains `GAME.GOG`) | **PC CD**: `DATA/*.PHD`, `DATA/TITLEH.PCX`, optional `FMV/*.RPL`<br>**PSX CD**: `PSXDATA/*.PSX`, `DELDATA/PSXTIT.RAW`, or single-file `.cue`/`.bin` |
| **Tomb Raider II** | `Tomb Raider (II)/` (`data/*.TR2`, `data/MAIN.SFX`, `data/TITLE.PCX`) | Same `DATA/*.TR2`, `DATA/MAIN.SFX`, `DATA/TITLE.PCX` layout |
| **Tomb Raider III** | `Tomb Raider (III)/` (`data/*.TR2`, `data/MAIN.SFX`, `pix/TITLEUK.BMP`) | Same `DATA/*.TR2`, `DATA/MAIN.SFX`, `PIX/TITLE*.BMP` layout |

### Useful `tools/build.py` Flags
- `--dry-run`: print detected layout and planned commands without building
- `--pack-only`: build only the `.pack` asset bundle (no N64 SDK needed)
- `--levels LIST`: comma-separated level stems or `all`
- `--build-dir PATH`: intermediate build directory (defaults to `$TMPDIR/tr64-build-tr<N>`)
- `--n64-inst PATH` / `--n64-gccprefix PATH`: override `N64_INST` / `N64_GCCPREFIX`
- `--jobs N`: parallel jobs for cooking and `make`

---

## 7. Manual Pipeline (Under the Hood)

`tools/build.py` wraps three commands you can also run directly:

1. **Cook each level**: `tools/cook/pack_level.py --source pc|psx --input <level> [--main-sfx MAIN.SFX] [--title-image <img>] --out <level>.pack`
2. **Merge campaign pack**: `tools/cook/pack_merge.py --game <0|2|3> --out campaign.pack --inputs *.pack` (`0` = TR1, `2` = TR2, `3` = TR3)
3. **Build ROM**:
   ```bash
   make BUILD_DIR=/tmp/tr64-rom TR64_GAME=<1|2|3> TR64_PLAY=1 TR64_LEVEL_PACK=/path/to/campaign.pack rom -j4
   ```

For controls, menus, saving, co-op, and options, see **[MANUAL.md](MANUAL.md)**.
