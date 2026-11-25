#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Every trace on which two oracles differ, and why (oracle host.patch hunk 28, trigger-activated invisible items).

Runs each input of a directory on an old and a new oracle (schema 3, all records) and lists, for every input whose
traces differ: the first differing tick, the items whose records differ (index, type, what they do once visible, their
status in each oracle at the end), Lara's first difference, her lowest health in each, and whether the final state hash
differs. An identical input is only counted. Exit 0 whatever the number of differences: the table is the result.

  hunk28.py --old OLD --new NEW --inputs DIR [--inputs DIR ...] --out DIR

An input is `<level>-....masks`; `--inputs` may be repeated. Run with PYTHONPATH=tools/cook:test/game.
"""
import argparse
import json
from pathlib import Path
import sys
sys.dont_write_bytecode = True
import invisible
from stream import Run

DATA = Path('/tmp/tr64/data/pc')


def mask_count(text):
    words, i = text.split(), 0
    while i < len(words) and words[i] in ('start', 'give', 'item', 'activate'):
        i += {'start': 6, 'give': 3, 'item': 3, 'activate': 3}[words[i]]
    return len(words) - i


def records(exe, level, path, ticks):
    run = Run([str(exe), '--level', str(DATA / f'{level}.phd'), '--input', str(path), '--ticks', str(ticks),
               '--trace-schema', '3'], '--out')
    rows = [json.loads(line) for line in run.trace]
    code, log = run.finish()
    if code or not rows:
        raise ValueError(f'{exe} failed on {path}: {code} {log[-200:]}')
    return rows


def compare(old, new):
    """(first differing tick or None, {item: first tick it differs}, first Lara tick or None)."""
    first, items, lara = None, {}, None
    for a, b in zip(old, new):
        if a == b:
            continue
        if first is None:
            first = a['tick']
        if a['lara'] != b['lara'] and lara is None:
            lara = a['tick']
        ia = {x['index']: x for x in a['active']}
        ib = {x['index']: x for x in b['active']}
        for k in sorted(set(ia) | set(ib)):
            if ia.get(k) != ib.get(k) and k not in items:
                items[k] = a['tick']
    return first, items, lara


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--old', required=True, type=Path)
    p.add_argument('--new', required=True, type=Path)
    p.add_argument('--inputs', required=True, type=Path, action='append')
    p.add_argument('--out', required=True, type=Path)
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    rows, same = [], 0
    for directory in args.inputs:
        for path in sorted(directory.glob('*.masks')):
            level = path.name.split('-')[0]
            ticks = mask_count(path.read_text())
            old, new = records(args.old, level, path, ticks), records(args.new, level, path, ticks)
            if old == new:
                same += 1
                continue
            first, items, lara = compare(old, new)
            detail = []
            for k, tick in sorted(items.items(), key=lambda kv: kv[1]):
                a = next((x for r in old for x in r['active'] if x['index'] == k), None)
                b = next((x for r in new for x in r['active'] if x['index'] == k), None)
                t = (b or a)['type']
                detail.append(dict(item=k, type=t, name=invisible.NAMES.get(t, str(t)), first_tick=tick,
                                   does=invisible.DOES.get(t, '?')))
            hs = lambda rs: min(r['lara']['health'] for r in rs if r['lara'])
            rows.append(dict(input=path.name, level=level, first_tick=first, lara_first_tick=lara, items=detail,
                             old_min_health=hs(old), new_min_health=hs(new), old_final_health=old[-1]['lara']['health'],
                             new_final_health=new[-1]['lara']['health'], final_hash_differs=old[-1]['hash'] != new[-1]['hash'],
                             records=len(new)))
            print(json.dumps(rows[-1]), flush=True)
    (args.out / 'hunk28.json').write_text(json.dumps(dict(old=str(args.old), new=str(args.new), identical=same, differing=rows),
                                                     indent=1) + '\n')
    lines = ['| Input | First difference | Lara first differs | Items that differ (what they do once visible) | Lara min health old / new | Final hash differs |',
             '|---|---:|---:|---|---|---|']
    for r in rows:
        it = '; '.join(f'item {x["item"]} {x["name"]} from tick {x["first_tick"]}: {x["does"]}' for x in r['items'])
        lines.append(f'| {r["input"]} | {r["first_tick"]} | {r["lara_first_tick"]} | {it} | {r["old_min_health"]} / {r["new_min_health"]} | '
                     f'{"yes" if r["final_hash_differs"] else "no"} |')
    (args.out / 'hunk28.md').write_text('\n'.join(lines) + '\n')
    print(f'hunk28: {same} identical, {len(rows)} differing')
    return 0


if __name__ == '__main__':
    sys.exit(main())
