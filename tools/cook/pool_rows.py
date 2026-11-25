# SPDX-License-Identifier: MIT
"""Read the build-selected RDRAM rows used by tr64/mem.h."""
import os
from pathlib import Path
import re


RDRAM_BYTES = 4 * 1024 * 1024
LAYOUT = (('image','IMAGE'), ('stack','FIXED'), ('fb','FIXED'), ('zbuf','FIXED'),
          *((name,'CARVE') for name in ('code','gpu','audio','libheap','level','state','io')))


def pool_table(game=None):
    game = os.environ.get('TR64_GAME', '1') if game is None else str(game)
    if game not in ('1', '2', '3'):
        raise ValueError(f'TR64_GAME={game}: expected 1, 2 or 3')
    directory = Path(__file__).resolve().parents[2] / 'include/tr64'
    return directory / {'1': 'pools.def', '2': 'pools-tr2.def', '3': 'pools-tr3.def'}[game]


def pool_rows(path=None, *, game=None):
    path = pool_table(game) if path is None else Path(path)
    rows = [(name, int(kib)*1024, kind) for name, kib, kind in re.findall(
        r'^TR64_POOL\(\s*(\w+),\s*(\d+),\s*(IMAGE|FIXED|CARVE)\)\s*$', path.read_text(), re.M)]
    if tuple((name, kind) for name, _, kind in rows) != LAYOUT or not all(size for _, size, _ in rows):
        raise ValueError(f'{path.name}: missing, invalid or misordered pool rows')
    return rows


def reserve_bytes(rows, path):
    total = sum(size for _, size, _ in rows)
    if total > RDRAM_BYTES:
        raise ValueError(f'{Path(path).name}: declared pools total {total} bytes exceeds {RDRAM_BYTES} bytes of RDRAM')
    return RDRAM_BYTES - total
