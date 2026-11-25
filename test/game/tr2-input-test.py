#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare documented input aliases through the real Great Wall host scene."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def check(exe, pack, scratch):
    with tempfile.TemporaryDirectory(dir=scratch, prefix='tr2-input-') as root:
        directory = Path(root)
        script = directory / 'input.txt'
        trace = directory / 'trace.jsonl'
        for flag in ('--game-tr2', '--game-tr2-schema21'):
            def replay(masks, invalid=False):
                script.write_text('0\n' * 142 + ''.join(f'{mask}\n' for mask in masks), encoding='ascii')
                proc = subprocess.run(['rtk', str(exe), flag, str(pack), str(script), str(trace)],
                                      capture_output=True, text=True, check=False)
                if invalid:
                    if not proc.returncode or 'undefined control' not in proc.stdout + proc.stderr:
                        raise RuntimeError(f'{flag}: reserved mask not rejected by name')
                    return None
                if proc.returncode:
                    raise RuntimeError(f'{flag}: scene failed\n{proc.stdout}\n{proc.stderr}')
                rows = [json.loads(line)['lara'] for line in trace.read_text().splitlines()[:-1]]
                if len(rows) != 143 + len(masks):
                    raise RuntimeError(f'{flag}: wrong tick count')
                for row, mask in zip(rows[143:], masks):
                    if row.pop('input') != mask:
                        raise RuntimeError(f'{flag}: trace did not preserve raw input {mask}')
                return rows[143:]

            idle = replay([0] * 30)
            opposite = replay([2 | 4] * 30)
            left = replay([2] * 30)
            if idle != opposite or left[-1]['rot'] == idle[-1]['rot']:
                raise RuntimeError(f'{flag}: opposite-key cancellation or its one-sided control failed')
            for dedicated, chord in ((16384, 8 | 16), (32768, 64 | 4), (65536, 64 | 2)):
                pulses = 1 if dedicated == 16384 else 21
                span = lambda key: [key] * pulses + [0] * (30 - pulses)
                actual = replay(span(dedicated))
                if actual != replay(span(chord)) or not any(a['pos'] != b['pos'] for a, b in zip(actual, idle)):
                    raise RuntimeError(f'{flag}: dedicated alias failed or did not move: {dedicated}')
            replay([2048], invalid=True)
    print('PASS: cancelled opposites, one-sided turn, three dedicated aliases, raw 17-bit input, reserved mask refusal')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('pack', type=Path)
    options = parser.parse_args()
    if not os.environ.get('TMPDIR'):
        parser.error('set TMPDIR to the assigned tr64 scratch directory')
    check(options.executable, options.pack, os.environ['TMPDIR'])
