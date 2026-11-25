# tr64

A Nintendo 64 engine for classic Tomb Raider (I–III) that runs in the console's
stock 4 MiB of memory, cooked from the player's own PC (Steam or CD-ROM) or
PlayStation copy of the game. The engine, renderer and tools are new code; the
game logic in `game/` is a C99 port of [OpenLara](https://github.com/XProger/OpenLara)'s
fixed-point game.

## Documentation

- **[Building Guide (docs/BUILDING.md)](docs/BUILDING.md)** — prerequisites, pinned SDK setup, and one-command ROM builds from Steam or CD-ROM.
- **[Player Manual (docs/MANUAL.md)](docs/MANUAL.md)** — N64 controls, options, cross-game Lara moves, SRAM saving, co-op, and shortcuts.

### Quick Start
With the pinned SDK built ([docs/BUILDING.md](docs/BUILDING.md)):

```bash
python3 tools/build.py --input "/path/to/Tomb Raider (I|II|III)" --out tr64.z64
```

### Technical Reference
For engine internals and contributor rules, see [docs/architecture.md](docs/architecture.md),
[docs/PROVENANCE.md](docs/PROVENANCE.md), and [AGENTS.md](AGENTS.md).

No Tomb Raider data is in this repository, and none is built into anything
you can download from it. You need your own copy of the game.

## Licence

Everything outside `game/` is MIT ([LICENSE](LICENSE)). `game/` and the
OpenLara-derived oracle code in `test/oracle/` are BSD-2-Clause
([game/LICENSE-OPENLARA](game/LICENSE-OPENLARA)). See [NOTICE.md](NOTICE.md).
