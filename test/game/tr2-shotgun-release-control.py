#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""(a) and (d) from shotgun-release-repress; (b) and (c) are synthetic."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def pose(rows, tick):
    arm = rows[tick]['lara']['armR']
    return arm['anim'], arm['frame']



def run(host, pack, fixture, scratch, mode, input_path, masks=None, placement=None):
    out = scratch / 'out.jsonl'
    out.unlink(missing_ok=True)
    env = os.environ | ({'TR64_SHOTGUN_CONTROL': mode} if mode else {})
    cmd = [str(host), '--game-tr2-schema21', str(pack), str(input_path), str(out),
           '--shotgun-selection', '146', '158', '87']
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=40, env=env)
    if p.returncode:
        raise RuntimeError(f'{mode or "record"}: {p.stderr[-400:]}')
    spec = __import__('importlib.util').util.spec_from_file_location(
        'compare', Path(__file__).resolve().parents[2] / 'tools/tr2-compare.py')
    c = __import__('importlib.util').util.module_from_spec(spec)
    spec.loader.exec_module(c)
    if masks is None:
        _, masks, placement, _ = c._fixture_data(fixture, metadata=True)
    port = list(c.records(c.port_body(out.read_bytes(), 21)[0], masks, 'port', placement, 21))
    return port, c


def main():
    host, control, pack, fixtures, scratch = map(Path, sys.argv[1:6])
    fixture = fixtures / 'schema21' / 'shotgun-release-repress'
    scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=scratch, prefix='release-') as td:
        td = Path(td)
        rows, c = run(host, pack, fixture, td, None, fixture / 'input.txt')
        retail, masks, placement, _ = c._fixture_data(fixture, metadata=True)
        retail_rows = list(c.records(retail, masks, 'retail', placement, 21, shotgun={
            'tick': 146, 'type': 158, 'pausedTicks': 87}))
        for tick, (left, right) in enumerate(zip(retail_rows, rows, strict=True)):
            for field in ('lara', 'camera', 'active', 'save', 'inventory', 'world'):
                diff, _ = c.first_difference(left[field], right[field], field, set())
                if diff:
                    raise RuntimeError(f'record {field} tick {tick}: {diff}')
        expect = {204: (224, 12), 205: (222, 0), 240: (224, 22),
                  250: (224, 32), 251: (224, 0), 252: (226, 0), 265: (222, 0)}
        for tick, want in expect.items():
            if pose(rows, tick) != want:
                raise RuntimeError(f'record tick {tick} pose {pose(rows, tick)}')
        if rows[205]['lara']['ammo'][3] != rows[199]['lara']['ammo'][3]:
            raise RuntimeError('re-press issued a shell')
        if rows[265]['lara']['ammo'][3] != 12:
            raise RuntimeError('late release issued a shell')
        print('PASS (a) re-press keeps the exit: 224/12 then aim, no shell')
        print('PASS (d) release after absolute 60 finishes, lowers, then aims')

        gained, _ = run(control, pack, fixture, td, 'g', fixture / 'input.txt')
        if pose(gained, 204) != (224, 12) or pose(gained, 205) != (222, 0):
            raise RuntimeError(f'(b) pose {pose(gained, 204)} {pose(gained, 205)}')
        if gained[205]['lara']['ammo'][3] != gained[199]['lara']['ammo'][3]:
            raise RuntimeError('(b) a gained lock fired')
        print('PASS (b) a lock gained on the re-press does not clear goal 4 or fire')

        lines = (fixture / 'input.txt').read_text().splitlines()
        # Tick 240 is line 241. Hold Action through the loop back to frame 0.
        for tick in range(240, 256):
            lines[tick] = '128'
        held = td / 'held.txt'
        held.write_text('\n'.join(lines) + '\n')
        held_masks = masks[:]
        for tick in range(239, 255):
            held_masks[tick] = 128
        lost, _ = run(control, pack, fixture, td, 'l', held, held_masks, placement)
        if pose(lost, 230) != (224, 12) or pose(lost, 250) != (224, 32):
            raise RuntimeError(f'(c) lost-lock cut the recoil {pose(lost, 230)} {pose(lost, 250)}')
        if pose(lost, 251) != (224, 0) or pose(lost, 252) != (226, 0):
            raise RuntimeError(f'(c) lost-lock frame 0 {pose(lost, 251)} {pose(lost, 252)}')
        if lost[252]['lara']['ammo'][3] != lost[219]['lara']['ammo'][3]:
            raise RuntimeError('(c) lost-lock fired at the loop')
        print('PASS (c) lock lost with Action held completes, then lowers, no shell')

        released = (fixture / 'input.txt').read_text().splitlines()
        for tick in range(195, 231):
            released[tick] = '0'
        rel = td / 'released.txt'
        rel.write_text('\n'.join(released) + '\n')
        rel_masks = masks[:]
        for tick in range(194, 230):
            rel_masks[tick] = 0
        kept, _ = run(control, pack, fixture, td, 'k', rel, rel_masks, placement)
        if pose(kept, 205) == (222, 0):
            raise RuntimeError('(c) a kept lock took the aim exit')
        if pose(kept, 224) != (224, 32) or pose(kept, 225) != (224, 0):
            raise RuntimeError(f'(c) kept-lock did not finish the clip {pose(kept, 224)} {pose(kept, 225)}')
        if pose(kept, 226)[0] != 224:
            raise RuntimeError(f'(c) goal 0 selected aim {pose(kept, 226)}')
        if kept[226]['lara']['ammo'][3] != kept[193]['lara']['ammo'][3]:
            raise RuntimeError('(c) a kept lock fired at the loop')
        print('PASS (c) a release that keeps a lock sets goal 0 and loops, no shell')


if __name__ == '__main__':
    main()
