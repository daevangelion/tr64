#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Merge multiple cooked native level packs into one multi-level cart pack.

Every level keeps its original section boundaries, alignment, game tag, and
raw/compressed storage class. Identical 16 KiB blocks across levels are
deduplicated automatically by n64_pack.build() using SHA-256 content addressing.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
import n64_core
import n64_music
import n64_pack as pack
import n64_snd
from pool_rows import pool_table
from sizing import pools

RAW_KINDS = (n64_snd.KIND, n64_music.KIND)
CART_BYTES = 64 * 1024 * 1024


def sha256_hex(data):
    return hashlib.sha256(data).hexdigest()


def extract_pack(blob):
    """Validate a pack blob and extract (entries, games, decoded) preserving wire layout."""
    decoded = pack.inspect(blob)
    entries, games = {}, {}
    count = struct.unpack_from('>I', blob, 12)[0]
    for i in range(count):
        raw_name, offset, size, _, game = pack.LEVEL.unpack_from(blob, 32 + i * 48)
        name = raw_name.split(b'\0', 1)[0].decode('utf-8')
        if game:
            games[name] = game
        groups = {}
        for j in range(size):
            row = pack.ENTRY.unpack_from(blob, offset + 64 * j)
            groups.setdefault(row[0], []).append(row)
        rows = []
        for kind, wire in sorted(groups.items()):
            wire.sort(key=lambda r: r[1])
            is_raw = kind in RAW_KINDS and all(r[6] == 0 and r[3] == r[4] for r in wire)
            rows.append(pack.Section(
                kind,
                decoded[name][kind],
                alignment=wire[0][5],
                compression='none' if is_raw else 'auto',
                boundaries=tuple(r[1] for r in wire[1:]),
            ))
        entries[name] = rows
    return entries, games, decoded


def merge_blobs(blobs, codec, *, expected_game=None):
    """Merge pack blobs into one validated multi-level pack blob + report dict."""
    if not blobs:
        raise ValueError('pack_merge requires at least one input pack')
    merged_entries = {}
    merged_games = {}
    merged_decoded = {}
    input_bytes = 0
    for idx, blob in enumerate(blobs):
        input_bytes += len(blob)
        entries, games, decoded = extract_pack(blob)
        for name, rows in entries.items():
            game = games.get(name, 0)
            if expected_game is not None and game != expected_game and not name.startswith(('MUSIC_', 'VIDEO_', 'FILM_')):
                raise ValueError(f'pack entry {name}: game {game} != expected {expected_game}')
            if name in merged_entries:
                if name.startswith(('MUSIC_', 'VIDEO_', 'FILM_')) and merged_decoded[name] == decoded[name]:
                    continue
                raise ValueError(f'duplicate pack entry across inputs: {name} (input #{idx})')
            merged_entries[name] = rows
            merged_decoded[name] = decoded[name]
            if game:
                merged_games[name] = game

    # Verify the merged directory + each level's retained section table fits the I/O pool.
    dir_bytes = pack.align(48 * len(merged_entries))
    for name, rows in merged_entries.items():
        game = merged_games.get(name, 1)
        io_limit = pools(pool_table(game if game in (2, 3) else 1))['io']
        part_count = sum(1 for section in rows for _ in pack.parts(section))
        needed = dir_bytes + 32 * part_count + pack.BLOCK
        if needed > io_limit:
            raise ValueError(
                f'merged pack {name} section table: needs {needed} io bytes, limit {io_limit}'
            )

    blob = pack.build(merged_entries, codec, games=merged_games or None)
    if len(blob) >= CART_BYTES:
        raise ValueError(f'merged pack {len(blob)} bytes does not fit a 64 MiB cart')

    after = pack.inspect(blob)
    if after.keys() != merged_decoded.keys():
        raise ValueError('merged pack level set mismatch after independent decode')
    for name, kinds in merged_decoded.items():
        if after[name] != kinds:
            raise ValueError(f'merged pack independent decode mismatch: {name}')

    report = dict(
        levels=sorted(merged_entries.keys()),
        level_count=len(merged_entries),
        games={k: merged_games.get(k, 0) for k in sorted(merged_entries.keys())},
        input_bytes=input_bytes,
        pack_bytes=len(blob),
        dedup_saved_bytes=input_bytes - len(blob),
        pack_sha256=sha256_hex(blob),
        compressor_sha256=codec.identity,
        headroom_to_64MiB=CART_BYTES - len(blob),
    )
    return blob, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inputs', type=Path, nargs='+', required=True,
                        help='cooked input .pack files to merge')
    parser.add_argument('--game', type=int, choices=(0, 2, 3), default=None,
                        help='optional expected directory game tag (0=TR1, 2=TR2, 3=TR3)')
    parser.add_argument('--cache', type=Path, help='version-keyed host compressor cache')
    parser.add_argument('--out', type=Path, required=True, help='output merged .pack path')
    args = parser.parse_args()

    codec = pack.compressor(args.cache or args.out.parent / 'compressor')
    blobs = [p.read_bytes() for p in args.inputs]
    blob, report = merge_blobs(blobs, codec, expected_game=args.game)
    report['inputs'] = [str(p) for p in args.inputs]
    n64_core.write_changed(args.out, blob)
    n64_core.write_changed(
        args.out.with_suffix('.json'),
        (json.dumps(report, indent=2, sort_keys=True) + '\n').encode('utf-8'),
    )
    print(json.dumps(report, sort_keys=True))


if __name__ == '__main__':
    main()
