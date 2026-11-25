#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Surface ledge climb: the flat rim accepts Action, and the stated rejects do not."""
import argparse
import json
import os
import subprocess
import tempfile
from pathlib import Path

FORWARD, ACTION, BACK, JUMP = 8, 128, 16, 32


def hold(mask, count):
    return [mask] * count


def replay(host, pack, place, masks, out, env=None):
    script = out / 'input.txt'
    trace = out / 'trace.jsonl'
    script.write_text('place ' + ' '.join(map(str, place)) + '\n' +
                      '\n'.join(map(str, masks)) + '\n')
    proc = subprocess.run(['rtk', str(host), '--game-tr2', str(pack), str(script), str(trace)],
                          capture_output=True, timeout=60, env=env)
    if proc.returncode:
        raise RuntimeError((proc.stdout + proc.stderr).decode(errors='replace')[-400:])
    rows = []
    for line in trace.read_text().splitlines():
        if not line.startswith('{'):
            continue
        row = json.loads(line)
        if 'lara' in row:
            rows.append(row)
    return rows


def climb(rows):
    return next((row for row in rows
                 if row['lara']['state'] == 55 and row['lara']['anim'] in (111, 191, 193)),
                None)


def plant(host, pack, place, rise):
    masks = rise + hold(FORWARD | ACTION, 40)
    x = 16384
    cases = {
        'h=-511': 111, 'h=-129': 111, 'h=-128': 191, 'h=127': 191, 'h=128': 193,
        'h=316': 193, 'h=316,type=0': 193, 'yaw=6370': 191, 'yaw=-6370': 191,
        f'yaw={x + 6370}': 191, f'yaw={x - 6370}': 191,
        f'yaw={-x + 6370}': 191, f'yaw={-x - 6370}': 191,
        'yaw=-26397': 191, 'yaw=26397': 191, 'weapon=flare': 191, 'side=59': 191,
        'h=-512': None, 'h=317': None, 'h=317,type=0': None,
        'yaw=6371': None, 'yaw=-6371': None,
        f'yaw={x + 6371}': None, f'yaw={x - 6371}': None,
        f'yaw={-x + 6371}': None, f'yaw={-x - 6371}': None,
        'yaw=-26396': None, 'yaw=26396': None, 'weapon=ready': None,
        'action=0': None, 'slant=2': None, 'side=60': None, 'ceiling=1': None,
        'floor=wall': None,
    }
    with tempfile.TemporaryDirectory(prefix='tr2-water-exit-plant-') as work:
        out = Path(work)
        for spec, want in cases.items():
            env = os.environ.copy()
            env['TR64_CLIMB_CASE'] = spec
            got = climb(replay(host, pack, place, masks, out, env))
            anim = None if got is None else got['lara']['anim']
            if anim != want:
                raise SystemExit(f'planted {spec}: got {anim}, want {want}')
    print('PASS planted bounds: -512/317 rejected, -511..316 accepted with the band clip, '
          'untyped floor -384 accepted, facing and hands at the stated tolerances')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('host', type=Path)
    parser.add_argument('pack', type=Path)
    parser.add_argument('control_host', type=Path)
    args = parser.parse_args()
    place = (50688, 200, 36352, -16384, 3)
    rise = hold(0, 12) + hold(BACK | JUMP, 160)
    with tempfile.TemporaryDirectory(prefix='tr2-water-exit-') as work:
        out = Path(work)
        held = replay(args.host, args.pack, place, rise + hold(FORWARD | ACTION, 120), out)
        late = replay(args.host, args.pack, place,
                      rise + hold(FORWARD, 50) + hold(FORWARD | ACTION, 80), out)
        plain = replay(args.host, args.pack, place, rise + hold(FORWARD, 120), out)
        # 6371 from -X is the first rejected facing.
        off = replay(args.host, args.pack, (50688, 200, 36352, -10013, 3),
                     rise + hold(FORWARD | ACTION, 180), out)
        # Same pool, but the steep rim is outside the height window.
        steep = replay(args.host, args.pack, (68096, 200, 34304, -16384, 36),
                       hold(0, 12) + hold(BACK | JUMP, 140) + hold(FORWARD | ACTION, 180), out)
    got = climb(held)
    if got is None or got['lara']['anim'] != 191 or got['lara']['water'] != 0 or got['lara']['pos'][1] != -5:
        raise SystemExit(f'held Action did not take the waterline clip: {None if got is None else got["lara"]}')
    if got['lara']['weaponState'] != 1 or got['lara']['gravity'] or got['lara']['room'] != 3:
        raise SystemExit(f'climb pose was not dry, busy and upright: {got["lara"]}')
    delayed = climb(late)
    if delayed is None or delayed['lara']['anim'] != 191 or delayed['lara']['pos'] != got['lara']['pos']:
        raise SystemExit('Action pressed at the wall did not take the same climb')
    if climb(plain) is not None or plain[-1]['lara']['water'] != 2:
        raise SystemExit('Forward without Action climbed or left the surface')
    if climb(off) is not None:
        raise SystemExit('facing 6371 off -X was accepted')
    if climb(steep) is not None:
        raise SystemExit('rim outside the height window was accepted')
    plant(args.control_host, args.pack, place, rise)
    print('PASS surface climb: held and late Action take 191 at Y -5; '
          'no Action, 6371 off-axis and the high rim stay in the water')


if __name__ == '__main__':
    main()
