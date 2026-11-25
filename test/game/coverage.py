#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Line and function coverage of game/ from a gcov host build (HOST_CC='cc --coverage ...').

  coverage.py replay --executable COV_EXE --inputs DIR [DIR ...] [--jobs N]
      runs every *.masks file (name `LEVEL-TICKS-HASH.masks`, or `LEVEL/NAME.masks`
      with --level) through the coverage executable, trace-only, no oracle; profile
      counters accumulate in the build's .gcda files.
  coverage.py report --objdir BUILD/host/game --source game [--functions]
      per-file and total line coverage of game/*.c, optionally the functions never called.

Scratch (.gcda) is the build directory's; delete it with the build.
"""
import argparse
import concurrent.futures as futures
import json
import re
import subprocess
import sys
from pathlib import Path
sys.dont_write_bytecode = True


# Levels whose scripts may exceed 600 ticks (they end in a cutscene, test/game/README.md).
CUT_FEEDERS = {'level3b', 'level7b', 'level10a', 'level10b'}


def replay_one(exe, path, schema='4', saves=False, trace=False):
    """Level from `LEVEL-...` in the name or else the parent directory; ticks = mask lines."""
    match = re.match(r'(gym|level\w+?|cut\d)-', path.name) or re.fullmatch(r'(gym|level\w+|cut\d)', path.parent.name.lower())
    if not match:
        return path, 'skipped', ''
    level = match.group(1)
    words = path.read_text().split()
    ticks = 0
    i = 0
    while i < len(words):      # `start R X Y Z A`, `give T N`, `item I H`, `activate I T` are not masks
        if words[i] == 'start':
            i += 6
        elif words[i] in ('give', 'item', 'activate'):
            i += 3
        else:
            ticks += 1
            i += 1
    if ticks > (4096 if level.startswith('cut') or level in CUT_FEEDERS else 600):
        return path, 'skipped', ''
    runs = [[]]
    if saves and ticks > 40:     # the real save and load at three fixed points of the script (no oracle here)
        runs += [['--game-save-tick', str(t), '--game-load-tick', str(t + 1)] for t in (ticks // 3, 2 * ticks // 3, ticks - 30)]
    for extra in runs:
        p = subprocess.run([str(exe), '--game-level', level, '--game-input', str(path), '--game-ticks', str(ticks),
                            '--game-trace-only', '--game-trace-schema', schema] + (['--game-trace', '/dev/null'] if trace else []) + extra,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
        log = p.stdout.decode(errors='replace')
        if p.returncode and not re.search(r'tr64 fatal: game (?:.*?(?:not ported|outside M2)|placement)', log):
            return path, 'exit', f'{p.returncode} {extra}: {log[-200:]}'
    return path, 'ok', ''


def replay(args):
    files = [f for d in args.inputs for f in sorted(Path(d).glob('*.masks'))]
    bad = 0
    with futures.ThreadPoolExecutor(args.jobs) as pool:
        for path, status, why in pool.map(lambda f: replay_one(args.executable, f, args.schema, args.saves, args.trace), files):
            if status == 'exit':
                bad += 1
                print('FAIL', path.name, why)
    print(f'replayed {len(files)} scripts, {bad} unexpected exits')
    return int(bad > 0)


def report(args):
    totals = [0, 0]
    funcs_total = funcs_hit = 0
    uncalled = []
    rows = []
    for src in sorted(Path(args.source).glob('*.c')):
        out = subprocess.run(['gcov', '-j', '-t', '-o', args.objdir, str(src)], stdout=subprocess.PIPE,
                             stderr=subprocess.DEVNULL, cwd=args.cwd)
        if not out.stdout.strip():
            continue
        for line in out.stdout.decode().splitlines():
            data = json.loads(line)
            for f in data.get('files', []):
                if Path(f['file']).name != src.name:
                    continue
                lines = f['lines']
                hit = sum(1 for l in lines if l['count'] > 0)
                rows.append((src.name, hit, len(lines)))
                totals[0] += hit
                totals[1] += len(lines)
                for fn in f.get('functions', []):
                    funcs_total += 1
                    if fn['execution_count']:
                        funcs_hit += 1
                    else:
                        uncalled.append(f"{src.name}:{fn['name']}")
    for name, hit, total in rows:
        print(f'{name:28s} {hit:5d}/{total:5d} {100.0 * hit / max(total, 1):5.1f}%')
    print(f'TOTAL lines {totals[0]}/{totals[1]} = {100.0 * totals[0] / max(totals[1], 1):.1f}%  '
          f'functions {funcs_hit}/{funcs_total}')
    if args.functions:
        print('\n'.join(sorted(uncalled)))
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='cmd', required=True)
    r = sub.add_parser('replay')
    r.add_argument('--executable', required=True, type=Path)
    r.add_argument('--inputs', required=True, nargs='+')
    r.add_argument('--jobs', type=int, default=3)
    r.add_argument('--schema', default='4', help='trace schema the host is asked for (4 also runs the world hashes)')
    r.add_argument('--saves', action='store_true', help='also run each script with a real save and load at 1/3, 2/3 and 30 ticks before the end')
    r.add_argument('--trace', action='store_true', help='also write the trace (to /dev/null): the schema writers of game/common/trace.c run only then')
    q = sub.add_parser('report')
    q.add_argument('--objdir', required=True)
    q.add_argument('--source', default='game')
    q.add_argument('--cwd', default=None)
    q.add_argument('--functions', action='store_true')
    args = p.parse_args()
    return replay(args) if args.cmd == 'replay' else report(args)


if __name__ == '__main__':
    raise SystemExit(main())
