# tr64 Player Manual

A quick guide to playing **Tomb Raider I, II, and III** on the Nintendo 64 with **tr64**.

---

## Hardware & Emulator Setup

- **Memory**: Runs on a **stock 4 MiB Nintendo 64** (no Expansion Pak required).
- **Saves**: Uses **256 Kibit (32 KiB) cartridge SRAM** (`sram256k`), auto-configured via ED64 ROM header.
- **Flashcarts**: Copy the built `.z64` to an **EverDrive-64** or **SummerCart64**.
- **Emulators**: Tested on **ares** in standard 4 MiB N64 mode.
- **Accessories**: Supports **Rumble Pak** (toggle in *Options -> Controls*) and a **second controller** for Tomb Raider I split-screen co-op.

---

## Controls

### Basic Controls (All Games)

| N64 Button | Action |
| --- | --- |
| **Control Stick** or **D-Pad** | Run, turn, swim, climb |
| **A** | Jump / swim forward *(swappable with B in Options)* |
| **B** | Action — grab ledge, climb, pick up, pull switch, fire drawn weapon |
| **Z** | Draw / holster weapon |
| **R** (hold) | Walk carefully (won't step off ledges); **R + Left/Right** sidesteps |
| **C-Up** (hold) | Look around (move Stick/D-Pad to aim camera) |
| **C-Down** | 180° Roll *(and mid-air roll in TR II / TR III moves)* |
| **Start** | Open / close the 3D Inventory & Options Ring |

### Game-Specific Controls

| N64 Button | Tomb Raider I *(TR I / TR II moves)* | Tomb Raider II *(TR II moves)* | Tomb Raider III *(or TR I/II with TR III moves)* |
| --- | --- | --- | --- |
| **L** | *(shortcut modifier — see below)* | Light / throw Flare | **Sprint** (hold while running) |
| **C-Left** | Sidestep Left | Sidestep Left | **Crouch / Crawl** (hold; tap while sprinting to roll) |
| **C-Right** | Sidestep Right | Sidestep Right | Light / throw **Flare** *(TR II / TR III)* |
| **L + Z** | — | — | Quick-swap weapon *(TR III)* |

*Tip*: Even when **C-Left / C-Right** are assigned to Crouch and Flare in **TR III** mode, holding **R + Left/Right** always sidesteps.

---

## Inventory, Options & Saving

Press **Start** during play to open the 3D ring menu:
- **Left / Right**: Rotate the ring.
- **Up / Down**: Switch between **Main Inventory**, **Items/Keys**, and **Options** rings.
- **A / B**: Select / go back *(respects the **Swap A B** setting)*.

### Saving & Loading (Passport)
- Open the **Passport** in the Options ring (or from the Title screen) to **Load Game**, **Save Game**, **New Game**, **Restart Level**, or **Exit to Title**.
- **3 save slots** are stored in cartridge SRAM. You can save at any time during normal gameplay.
- Saves are tied to the ROM's packed level list — if you rebuild a ROM with a different set of levels, old slots from another pack won't match.

### Options Ring

- **Graphics**
  - **Gamma**: Adjust brightness.
  - **Draw Distance**: Manual visibility range.
  - **Auto Distance**: Automatically tunes draw distance in heavy outdoor scenes to keep framerate smooth.
  - **Filtering**: Toggle N64 bilinear texture filtering vs sharp point sampling.
  - **Show FPS**: Display live framerate counter.
  - **VSync**: Frame pacing lock *(TR I / TR II)*.
- **Sound**
  - **SFX** / **Music**: Toggle sound effects and music playback.
- **Controls**
  - **Rumble**: Enable/disable Rumble Pak feedback.
  - **Swap A B**: Swap Jump (**A**) and Action (**B**).
  - **Lara Moves** *(TR I / TR II builds cooked with donor levels)*: Switch live between **TR I**, **TR II** (ladders, mid-air roll), and **TR III** (sprint, crouch/crawl, monkeyswing) movesets.

---

## 2-Player Split-Screen Co-op (Tomb Raider I)

Plug a second N64 controller into Port 2 during any Tomb Raider I level:

| Input | Action |
| --- | --- |
| **P2 Start** | Join split-screen co-op |
| **P2 L + Start** | Leave co-op (returns to full screen) |
| **P1 or P2: L + A** | Warp to your partner |

---

## Quick Shortcuts

### Level Select Chords
Jump directly between packed levels without finishing the current one:

| Action | Tomb Raider I | Tomb Raider II & III |
| --- | --- | --- |
| **Next level** | `L + C-Right` | `L + R + C-Right` |
| **Previous level** | `L + C-Left` | `L + R + C-Left` |
| **Restart current level** | `L + Start` | `L + R + Start` |
| **Jump to first level** | `L + C-Up` | `L + R + C-Up` |
| **Jump to Lara's Home** | `L + C-Down` | `L + R + C-Down` |

### Performance Profiler Overlay
- **Tomb Raider I**: `L + D-Pad Down` *(stick centered)* toggles the profiler overlay (`L + D-Pad Up` toggles preview mode).
- **Tomb Raider II & III**: `R + Start` toggles the profiler overlay.

---

## Troubleshooting

- **Slow framerate in large outdoor rooms (e.g. Great Wall)**: Turn **Auto Distance** *On* or lower **Draw Distance** under *Options -> Graphics*.
- **Controls feel swapped**: Toggle **Swap A B** in *Options -> Controls*.
- **Save slot says unusable after rebuilding**: SRAM slots verify the pack's level list; rebuild with the same `--levels` set or start a fresh slot.
- **No music or FMVs**: Tomb Raider I music and FMVs are optional cook flags (`--music`, `--fmv` in [BUILDING.md](BUILDING.md)). Tomb Raider II/III builds currently include full level geometry, gameplay, title screens, and sound effects (`MAIN.SFX`).
