#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Real music/query/stream/IO regressions, with synthetic or original catalog.

Models three448-frame DAC buffers and competing4KiB requests. This is a
transport/sample test, not N64 CPU or physical-AI performance evidence.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import n64_music as music
import n64_pack as pack

ROOT = Path(__file__).resolve().parents[2]


def tone(track, frames, rate=22050, vary=False):
    units = (frames + 27) // 28
    if vary:  # every unit differs, so a wrong block or ROM offset changes samples
        raw = b''.join(bytes([3, 0]) + bytes((u * 131 + k * 17) & 255 for k in range(14))
                       for u in range(units))
    else:
        raw = (bytes([3, 0]) + bytes([0x17, 0xf8]) * 7) * units
    return music.HEADER.pack(b'MU64', 1, 64 + len(raw), 0, track, 1, rate, 1,
                            frames, 64, len(raw), 16, 28, 0, 0, 0) + raw


def fixtures(catalog, edge):
    result = music.read_music_catalog(catalog) if catalog else {
        f'MUSIC_{t:02}': [music.section(tone(t, 896), t)] for t in range(2, 57)}
    if not catalog:
        for track, units in [(4, 3 * 256 + 64), (5, 4 * 256 + 112)]:
            result[f'MUSIC_{track:02}'] = [music.section(tone(track, units * 28), track)]
        # 599 data blocks: the full 600-slot index, whose compacted records fill
        # exactly the bytes ahead of the overlaid loop window.
        result['MUSIC_06'] = [music.section(tone(6, (598 * 256 + 100) * 28, vary=True), 6)]
        result['MUSIC_57'] = [music.section(music.unavailable(57), 57)]
    if edge:
        result['MUSIC_10'] = [music.section(tone(10, 1), 10)]
        result['MUSIC_11'] = [music.section(tone(11, 14111, vary=True), 11)]
        result['MUSIC_13'] = [music.section(tone(13, 14336), 13)]
        result['MUSIC_12'] = [music.section(tone(12, 32004, 16000), 12)]
    return result


def write_pack(root, entries):
    blob = pack.build(entries)
    pack.inspect(blob)
    if len(entries) != 56:
        raise ValueError('transport fixture requires complete56-slot catalog')
    (root / 'pack.bin').write_bytes(blob)
    (root / 'dir.bin').write_bytes(blob[32:32 + 56 * 48])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--catalog', type=Path, help='optional original MU64 manifest FILE')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    binary = args.out / 'transport'
    sources = ['test/media/transport.c', 'engine/snd/music_decode.c', 'engine/snd/music_format.c',
               'engine/res/query.c', 'engine/res/stream.c', 'engine/io/io.c', 'engine/res/filetable.c']
    subprocess.run(['rtk', 'cc', '-O2', '-g', '-m32', '-std=gnu11', '-include', 'include/tr64/res.h',
                    '-I.', '-Iinclude', *sources, '-lm', '-o', str(binary)], cwd=ROOT, check=True)
    results = []
    with tempfile.TemporaryDirectory(dir=args.out, prefix='transport-input-') as temp:
        regular, edge = Path(temp) / 'regular', Path(temp) / 'edge'
        regular.mkdir(); edge.mkdir()
        write_pack(regular, fixtures(args.catalog, False))
        write_pack(edge, fixtures(args.catalog, True))
        cases = [('loop5', regular, 'loop', 5, 22050, 10000, 0, 33333),
                 ('long6', regular, 'loop', 6, 22050, 10000, 0, 33333),
                 ('loop4', regular, 'loop', 4, 22050, 10000, 0, 33333),
                 ('seam5', regular, 'loop', 5, 22047, 40000, 2, 40000),
                 ('seam4', regular, 'loop', 4, 22047, 40000, 2, 40000),
                 ('cues', regular, 'cues', 5, 22050, 10000, 0, 33333),
                 ('storm', regular, 'storm', 5, 22050, 10000, 13, 33333),
                 ('idle', regular, 'idle', 5, 22047, 10000, 0, 33333),
                 ('edge-cue', edge, 'edge-cue', 13, 22050, 10000, 0, 33333),
                 ('edge-ambient', edge, 'edge-ambient', 13, 22047, 10000, 0, 33333),
                 ('restore', edge, 'restore', 5, 22047, 10000, 0, 33333),
                 ('resample16k', edge, 'resample', 12, 22047, 10000, 0, 33333),
                 ('resample-fast', edge, 'resample', 11, 16000, 10000, 0, 33333),
                 ('resample-short', edge, 'resample-short', 10, 16000, 10000, 0, 33333)]
        for name, work, mode, track, rate, render, fake, frame in cases:
            command = [str(binary.resolve()), mode, str(rate), str(render), str(fake), '0', str(frame)]
            run = subprocess.run(command, cwd=work, env=dict(os.environ, T=str(track)),
                                 capture_output=True, text=True, timeout=90)
            (args.out / (name + '.log')).write_text(run.stdout + run.stderr)
            if run.returncode:
                raise ValueError(f'{name}: exit{run.returncode}: {run.stderr}')
            values = {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', run.stdout)}
            if mode == 'idle':
                if run.stdout.count('reads delta=0') != 2:
                    raise ValueError('unavailable ambience submitted further IO')
            else:
                if any(values.get(k, -1) != 0 for k in ['mismatches', 'zero_tail_bad']):
                    raise ValueError(f'{name}: sample mismatch: {run.stdout}')
                if mode == 'loop' and values.get('seam', -1):
                    raise ValueError(f'{name}: loop seam silence: {run.stdout}')
                if mode == 'loop' and values.get('loop_late', -1):
                    raise ValueError(f'{name}: loop block 1 requested in the last block: {run.stdout}')
                if not values.get('checked'):
                    raise ValueError(f'{name}: no samples compared')
                if name not in ('seam4', 'seam5') and values.get('underruns'):
                    raise ValueError(f'{name}: unexpected stream underrun')
            results.append(dict(case=name, values=values, command=command))
            print(name + ' PASS ' + run.stdout.strip(), flush=True)
    (args.out / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
