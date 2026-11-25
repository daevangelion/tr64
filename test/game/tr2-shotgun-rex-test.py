#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check the normal sealed shotgun and Tyrannosaurus routes."""
import argparse
import importlib.util
from pathlib import Path
import subprocess
import tempfile


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for key in ('host', 'pack', 'fixtures', 'scratch'):
        ap.add_argument(key, type=Path)
    args = ap.parse_args()
    repo = Path(__file__).resolve().parents[2]
    spec = importlib.util.spec_from_file_location('compare', repo / 'tools/tr2-compare.py')
    c = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(c)
    routes = ('shotgun-tiger', 'rex-mask', 'rex-gold', 'shotgun-ammo',
              'shotgun-no-target', 'shotgun-release-repress')
    with tempfile.TemporaryDirectory(dir=args.scratch, prefix='shotgun-rex-') as td:
        for name in routes:
            fixture = args.fixtures / 'schema21' / name
            if name == 'shotgun-ammo' and not fixture.is_dir():
                fixture = args.fixtures / name
            retail, masks, placement, manifest = c._fixture_data(fixture, metadata=True)
            schema = manifest['schema']
            out = Path(td) / 'output.jsonl'
            extra = []
            selection = manifest.get('shotgunSelection')
            if selection:
                extra = ['--shotgun-selection', *(str(selection[k]) for k in ('tick', 'type', 'pausedTicks'))]
            mode = '--game-tr2-schema21' if schema==21 else '--game-tr2'
            cmd = ['rtk', str(args.host), mode, str(args.pack), str(fixture / 'input.txt'), str(out)]
            p = subprocess.run(cmd + extra, capture_output=True, text=True, timeout=40)
            if p.returncode:
                raise RuntimeError(f'{name}: exit {p.returncode}: {p.stderr[-1000:]}')
            port = out.read_bytes()
            if name == 'shotgun-no-target':
                # tr2-lara-arms.md:81 and :85. rng[1] is Q-SG-1: the text names
                # the fire-tick presentation counts but not their draws, and
                # does not name the idle advance from tick 2.
                retail_rows = list(c.records(retail, masks, 'retail', placement, 21, shotgun=selection))
                port_rows = list(c.records(c.port_body(port, 21)[0], masks, 'port', placement, 21))
                for tick, (left, right) in enumerate(zip(retail_rows, port_rows, strict=True)):
                    for field in ('lara', 'camera', 'active', 'save', 'inventory', 'world'):
                        diff, _ = c.first_difference(left[field], right[field], field, {'lara.timer', 'lara.vehicle'})
                        if diff:
                            raise RuntimeError(f'{name}: {field} tick {tick}: {diff}')
                    if left['rng'][0] != right['rng'][0]:
                        raise RuntimeError(f'{name}: gameplay RNG tick {tick}')
                cited = {193: (224, 1), 226: (224, 1), 237: (224, 12), 238: (222, 0),
                         250: (222, 0), 264: (224, 1), 272: (224, 9), 297: (226, 0),
                         310: (222, 0), 311: (225, 0), 332: (225, 21), 345: (225, 33)}
                for tick, (anim, frame) in cited.items():
                    arm = port_rows[tick]['lara']['armR']
                    if (arm['anim'], arm['frame']) != (anim, frame):
                        raise RuntimeError(f'{name}: tick {tick} pose {arm}')
                if port_rows[350]['lara']['ammo'][3] != 6 or port_rows[350]['lara']['health'] != 1000:
                    raise RuntimeError(f'{name}: closing reserve or health')
                print(f"PASS {name}: {len(port_rows)} logic samples; presentation stream is Q-SG-1")
                continue
            if name == 'shotgun-release-repress':
                # tr2-lara-arms.md:81 and :85. Rows equal on the named
                # families. rng[1] is reported, not tuned.
                retail_rows = list(c.records(retail, masks, 'retail', placement, 21, shotgun=selection))
                port_rows = list(c.records(c.port_body(port, 21)[0], masks, 'port', placement, 21))
                present = []
                for tick, (left, right) in enumerate(zip(retail_rows, port_rows, strict=True)):
                    for field in ('lara', 'camera', 'active', 'save', 'inventory', 'world'):
                        diff, _ = c.first_difference(left[field], right[field], field, set())
                        if diff:
                            raise RuntimeError(f'{name}: {field} tick {tick}: {diff}')
                    if left['rng'][0] != right['rng'][0]:
                        raise RuntimeError(f'{name}: gameplay RNG tick {tick}')
                    if left['rng'][1] != right['rng'][1]:
                        present.append(tick)
                cited = {193: (224, 1), 204: (224, 12), 205: (222, 0),
                         219: (224, 1), 240: (224, 22), 250: (224, 32),
                         251: (224, 0), 252: (226, 0), 265: (222, 0)}
                for tick, (anim, frame) in cited.items():
                    arm = port_rows[tick]['lara']['armR']
                    if (arm['anim'], arm['frame']) != (anim, frame):
                        raise RuntimeError(f'{name}: tick {tick} pose {arm}')
                if port_rows[265]['lara']['ammo'][3] != 12:
                    raise RuntimeError(f'{name}: reserve after the late release')
                note = 'rows equal' if not present else f'presentation rng differs on {len(present)} ticks'
                print(f'PASS {name}: {len(port_rows)} rows; {note}')
                continue
            score = c.compare(fixture, port, 'tr64-tr2-shotgun-rex-v2' if schema==21 else 'tr64-tr2-shotgun-rex20-v2')
            for field, known in score['families'].items():
                if field != 'rng' and known['knownEqual'] != score['records']:
                    raise RuntimeError(f'{name}: {field}: {known}')
            if name == 'shotgun-ammo':
                rows = list(c.records(c.port_body(port,schema)[0],masks,'port',placement,schema))
                assert rows[42]['lara']['ammo'][3] == 12 and rows[43]['lara']['ammo'][3] == 24
                assert [172, 1] in rows[42]['inventory']['items'] and [172, 2] in rows[43]['inventory']['items']
                print(f"PASS {name} (schema {schema}): {score['records']} logic samples, both pickups on tick 43")
                continue
            rows = list(c.records(c.port_body(port,21)[0], masks, 'port', placement, 21))
            for a, b in zip(c.records(retail, masks, 'retail', placement, 21, shotgun=selection), rows, strict=True):
                if a['save'] != b['save']:
                    raise RuntimeError(f"{name}: save counter differs on {a['tick']}: {c.first_difference(a['save'],b['save'],'save',set())[0]}")
            if name == 'shotgun-tiger':
                assert [(rows[t]['save']['ammoUsed'], rows[t]['save']['ammoHits'], rows[t]['rng'][0]) for t in (64,97)] == [
                    (6,4,802531111), (12,10,1884979983)]
                assert rows[98]['lara']['weaponItem'] == rows[97]['lara']['weaponItem'] == 106
                assert rows[179]['lara']['weaponItem'] == -1
                draw = rows[2]['rng'][1]
                for _ in range(91):
                    draw = (draw*0x41c64e6d+0x3039) & 0xffffffff
                assert rows[3]['rng'][1] == draw and rows[3]['save']['time']-rows[2]['save']['time'] == 88
                rejected = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
                text = rejected.stderr
                if rejected.returncode == 0 or (
                        'shotgun menu marker mismatch' not in text and
                        'undefined control or mask separator' not in text and
                        'inventory marker requires next-tick main-ring selection' not in text):
                    raise RuntimeError('shotgun menu marker without its manifest parameters was not refused')
            print(f"PASS {name}: {score['records']} combat, camera and gameplay RNG samples")


if __name__ == '__main__':
    main()
