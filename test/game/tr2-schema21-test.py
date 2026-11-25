#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check schema 21's common projection and optional navigation state."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

PINNED = (
    'crow-bite', 'crow-shoot', 'pool-swim', 'pool-wade', 'spider-bite',
    'spider-shoot', 'tiger-bite', 'tiger-cave', 'tiger-death', 'tiger-shoot',
    'shotgun-tiger', 'rex-mask', 'rex-gold',
)


def load_compare():
    path = Path(__file__).resolve().parents[2] / 'tools/tr2-compare.py'
    spec = importlib.util.spec_from_file_location('tr2_compare', path)
    c = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(c)
    return c


def row_bytes(c, record):
    payload = json.dumps(record,separators=(',',':')).encode('ascii')
    return payload[:-1] + b',"hash":"' + c.fnv32(payload).encode('ascii') + b'"}\n'


def check_key_projection(c, root):
    wire, masks, place, manifest = c._fixture_data(root/'rusty-key', metadata=True)
    key = manifest['keyConfirmation']
    rows = c.records(wire, masks, 'retail', place, key_confirmation=key)
    lifted = b''.join(row_bytes(c, row | {'schema': 21}) for row in rows)
    try:
        list(c.project_schema20(lifted, masks, place))
    except ValueError as error:
        assert str(error) == f"retail: tick {key['tick']}: wrong retail clock or initial seed"
    else:
        raise RuntimeError('accepted the keyed clock without the key confirmation')
    projected = b''.join(c.project_schema20(lifted, masks, place, key_confirmation=key))
    assert projected == wire
    print(f"PASS: rusty-key schema 20 → 21 → 20 byte-identical, including the pause at {key['tick']}")


def main(root):
    c = load_compare()
    projected = fixtures = 0
    sample = None
    captures = root/'schema21'
    unregistered = sorted(p.name for p in captures.iterdir()
                          if (p/'manifest.json').is_file() and p.name not in PINNED)
    if unregistered:
        print(f"Unregistered schema-21 fixtures (not run): {', '.join(unregistered)}")
    for name in PINNED:
        route = captures/name
        wire, masks, place, manifest = c._fixture_data(route,metadata=True)
        key = manifest.get('keyConfirmation')
        rows = list(c.records(wire, masks, 'retail', place, 21, key_confirmation=key,
                              shotgun=manifest.get('shotgunSelection')))
        fixtures += 1
        if route.name == 'tiger-cave':
            sample = next(row for row in rows if any('ai' in i for i in row['active']))
        if 'schema20SHA256' not in manifest:
            continue
        old, old_masks, old_place, old_manifest = c._fixture_data(root/name, metadata=True)
        assert (masks, place, key) == (old_masks, old_place, old_manifest.get('keyConfirmation')), name
        newer = b''.join(c.project_schema20(wire, masks, place, key_confirmation=key))
        assert old == newer and hashlib.sha256(newer).hexdigest() == manifest['schema20SHA256'], route.name
        projected += len(rows)
    assert fixtures == len(PINNED) and projected == 1963 and sample is not None
    print(f'PASS: {fixtures} schema-21 captures validate; {projected:,} projections are byte-identical to schema 20')
    check_key_projection(c, root)

    sample['tick'] = sample['lara']['input'] = 0
    index = next(i for i,a in enumerate(sample['active']) if 'ai' in a)
    item = sample['active'][index]
    ai = item['ai']
    clean = row_bytes(c,sample)
    # A small control can be checked without the retail tick-0 clock/seed rule.
    assert len(list(c.records(clean,[],'control',None,21))) == 1
    for field, value in [('slot',5),('enemy',256),('mood',4),('jointYaw',[-32769,0]),
                         ('maxTurn',-1),('flags',65536),('targetBox',1078),('headBox',-2),
                         ('boxSearch',-1),('step',-1),('drop',1),('fly',32768),
                         ('zoneCount',1079),('waypoint',[0,0,2**31]),('box',1078)]:
        before=ai[field]
        ai[field]=value
        try:
            list(c.records(row_bytes(c,sample),[],'control',None,21))
        except ValueError as error:
            assert f'.ai.{field}' in str(error) or (field == 'box' and 'route link without a stored box' in str(error)), str(error)
        else:
            raise RuntimeError(f'accepted out-of-domain ai.{field}')
        ai[field]=before
    ai_copy=ai.copy()
    ai.clear()
    ai.update(reversed(tuple(ai_copy.items())))
    try:
        list(c.records(row_bytes(c,sample),[],'control',None,21))
    except ValueError as error:
        assert 'field order' in str(error)
    else:
        raise RuntimeError('accepted reordered ai fields')
    reduced = item.copy()
    reduced.pop('ai')
    path = f'active[{index}]'
    first,_=c.first_difference(item,reduced,path,set())
    assert first['path'] == path+'.ai' and first['port'] is None
    assert c.first_difference(item,reduced,path,{path+'.ai'}) == (None,True)
    print('PASS: brain order/domains, missing-vs-present AI compared, named omission respected')


if __name__ == '__main__':
    main(Path(sys.argv[1]))
