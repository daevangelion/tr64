#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Original verifier; schema/hash contract: test/oracle/README.md.
"""Compare two frozen native executables, one per level source, on one input set.

The oracle reads PC levels only, so this gate runs the same native game on a
PC-cooked pack and a PlayStation-cooked pack and compares complete schema-3
records. No builds, input generation or trace files: both streams are pipes,
drained to EOF after a first difference. Inputs are the frozen M5 campaign
report (`--campaign`, every case equal to the oracle on PC) and optionally the
save/load cases (`--saves`, qualified cells and the source-complete matrix).

Startup logs give each Core table's count, bytes and hash for both sources, so
a trace difference can be tied to a table that really differs.
Exit 0: every case equal; 1: complete, some different; 2: runtime/evidence error.
--self-test falsifies the verdict logic without running native engines.
"""
import argparse
import concurrent.futures
import hashlib
import io
import itertools
import json
from pathlib import Path
import re
import subprocess
import sys
sys.dont_write_bytecode = True
from compare import first_difference, validate
from stream import Run

LEVELS = ('gym', 'level1', 'level2', 'level3a', 'level3b', 'level4', 'level5',
          'level6', 'level7a', 'level7b', 'level8a', 'level8b', 'level8c',
          'level10a', 'level10b', 'level10c')
HEX64 = re.compile('[0-9a-f]{64}')


def digest(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def read_record(raw, tick, side):
    text = raw.decode('ascii')
    row = json.loads(text)
    if not isinstance(row, dict) or type(row.get('schema')) is not int:
        raise ValueError('record is not a schema-tagged object')
    validate(row, text, tick, side, schema=3)
    return row


def window(pc, ps, expected):
    """Drain both streams to EOF; a difference never terminates observation."""
    result = {'pc': {'records': 0, 'errors': []}, 'ps': {'records': 0, 'errors': []},
              'first_difference': None}
    for tick, pair in enumerate(itertools.zip_longest(pc, ps)):
        rows = [None, None]
        for index, (side, raw) in enumerate(zip(('pc', 'ps'), pair)):
            if raw is None:
                continue
            facts = result[side]
            facts['records'] += 1
            if index and pair[0] == raw and rows[0] is not None:
                rows[1] = rows[0]  # byte-identical rows are validated once
                continue
            try:
                rows[index] = read_record(raw, tick, side)
            except (ValueError, TypeError, UnicodeError) as error:
                if len(facts['errors']) < 4:
                    facts['errors'].append(f'tick {tick}: {error}')
        if result['first_difference'] is None and all(row is not None for row in rows) and pair[0] != pair[1]:
            difference = first_difference(rows[0], rows[1]) or ('serialization', 'PC bytes', 'PS bytes')
            result['first_difference'] = dict(tick=tick, field=difference[0], pc=difference[1], ps=difference[2])
    for side in ('pc', 'ps'):
        facts = result[side]
        if facts['records'] != expected:
            facts['errors'].append(f"record count {facts['records']}, expected {expected}")
    return result


def verdict(result, pc_exit, ps_exit):
    for side, code in (('pc', pc_exit), ('ps', ps_exit)):
        result[side]['exit'] = code
        if code != 0:
            result[side]['errors'].append(f'process exit {code}')
    result['completed'] = not any(result[side]['errors'] for side in ('pc', 'ps'))
    result['status'] = 'error' if not result['completed'] else (
        'different' if result['first_difference'] else 'equal')
    return result


def command(executable, case, ticks=600):
    line = ['rtk', str(executable), '--game-level', case['level'], '--game-input', str(case['input']),
            '--game-ticks', str(ticks), '--game-trace-only', '--game-trace-schema', '3']
    if case.get('save_tick') is not None:
        line += ['--game-save-tick', str(case['save_tick']), '--game-load-tick', str(case['load_tick'])]
    return line


def finish(run):
    try:
        return run.finish()
    except subprocess.TimeoutExpired:
        code, log = run.finish(kill=True)
        return code, log + '\nprocess failed to exit after trace EOF\n'


def compare_case(pc, ps, case, ps_case=None):
    runs = []
    try:
        for executable, item in ((pc, case), (ps, ps_case or case)):
            runs.append(Run(command(executable, item), '--game-trace'))
        result = window(runs[0].trace, runs[1].trace, 601)
        exits = [finish(run) for run in runs]
    except BaseException:
        for run in runs:
            run.finish(kill=True)
        raise
    verdict(result, exits[0][0], exits[1][0])
    for side, (_, log) in zip(('pc', 'ps'), exits):
        if result[side]['errors']:
            result[side]['log'] = log[-1500:]
    return result


def work(pc, ps, case):
    try:
        return compare_case(pc, ps, case)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        return dict(status='error', error=str(error))


def startup(executable, level, pack_sha, log_path):
    empty = log_path.parent / 'empty.masks'
    empty.write_text('')
    run = Run(command(executable, dict(level=level, input=empty), 0), '--game-trace', log_limit=262144)
    count = 0
    try:
        for raw in run.trace:
            count += 1
        code, log = finish(run)
    except BaseException:
        run.finish(kill=True)
        raise
    log_path.write_text(log)
    packs = re.findall(r'^level-pack: sha256=([0-9a-f]{64})$', log, re.M)
    if packs != [pack_sha]:
        raise ValueError(level + ': runtime level-pack log does not equal the declared pack')
    tables = {}
    for name, rows, size, checksum in re.findall(
            r'^core-table: (\w+) count=(\d+) bytes=(\d+) hash=([0-9a-f]{8})$', log, re.M):
        if name in tables:
            raise ValueError('duplicate runtime Core table ' + name)
        tables[name] = (int(rows), int(size), checksum)
    if not tables or count != 1 or code:
        raise ValueError(f'{level}: zero-init records={count} exit={code}\n{log[-600:]}')
    pools = {name: int(used) for name, used in re.findall(r'^pool: (state|code) .*? used=(\d+)', log, re.M)}
    return dict(tables=tables, pools=pools)


def signature(field):
    return re.sub(r'\[\d+\]', '[]', field)


def load_cases(args):
    groups = {name: [] for name in LEVELS}
    if args.campaign:
        report = json.loads(args.campaign.read_text())
        if report['trace_schema'] not in (3, 4):
            raise ValueError('campaign must declare schema 3 or 4')   # the cases are inputs; both engines are run at schema 3
        for row in report['levels']:
            for group in ('random', 'legacy', 'triggers'):
                for case in row[group]['cases']:
                    if case['status'] == 'invalid-placement':
                        continue  # the oracle itself rejected the placement; no trace to compare
                    if case['status'] != 'equal' or case['records'] != 601:
                        raise ValueError(row['level'] + ': campaign case is not an equal complete oracle case')
                    if digest(case['input']) != case['input_sha256']:
                        raise ValueError(case['input'] + ': input changed since the campaign')
                    groups[row['level']].append(dict(case, level=row['level'], group=group))
    if args.saves:
        matrix = json.loads(args.saves.read_text())
        native = json.loads(args.save_loads.read_text())['cells']
        for row in matrix['cells']:
            if row['status'] != 'source-qualified':
                continue
            candidate = row['candidate']
            loads = [c for c in native if c['level'] == row['level'] and c['category'] == row['category']]
            if len(loads) != 1:
                raise ValueError(f"{row['level']}/{row['category']}: no unique native load tick")
            if digest(candidate['input']) != candidate['input_sha256']:
                raise ValueError(candidate['input'] + ': input changed since qualification')
            groups[row['level']].append(dict(level=row['level'], group='save-cell', input=candidate['input'],
                input_sha256=candidate['input_sha256'], save_tick=loads[0]['save_tick'],
                load_tick=loads[0]['load_tick'], name=row['category']))
    if args.save_matrix:
        for row in json.loads(args.save_matrix.read_text()):
            groups[row['level']].append(dict(level=row['level'], group='save-matrix', input=row['input'],
                input_sha256=digest(row['input']), save_tick=row['save_tick'], load_tick=row['load_tick'],
                name=row['name']))
    return {name: rows for name, rows in groups.items() if rows}


def negative(args, cases):
    """A changed production input on one side must be rejected at its first effect."""
    case = next(row for row in cases['level1'] if row['group'] == 'random')
    words = Path(case['input']).read_text().split('\n')
    index = next(n for n, line in enumerate(words) if line and not line.startswith('start'))
    words[index] = '0' if words[index] != '0' else '2'
    altered = args.out / 'negative-current.masks'
    altered.write_text('\n'.join(words))
    result = compare_case(args.pc, args.pc, case, dict(case, input=altered))
    if result['status'] != 'different':
        raise ValueError(f'changed-input negative was not rejected: {result["status"]}')
    return dict(altered_input_tick=result['first_difference']['tick'],
                altered_input_field=result['first_difference']['field'])


def table(report):
    lines = ['| Level | Cases | Equal | Different | Error | Core tables differing PC/PS (of 30) | First difference |',
             '|---|---:|---:|---:|---:|---|---|']
    for row in report['levels']:
        totals = {key: sum(g[key] for g in row['groups'].values()) for key in ('equal', 'different', 'error')}
        different = [name for name, facts in row['tables'].items() if not facts['equal']]
        first = row['first_difference']
        where = f"tick {first['tick']} `{first['field']}` ({first['group']})" if first else '-'
        lines.append(f"| {row['level']} | {sum(totals.values())} | {totals['equal']} | {totals['different']} | "
                     f"{totals['error']} | {len(different)}: {', '.join(different)} | {where} |")
    lines += ['', 'Groups per level: ' + '; '.join(
        f"{row['level']} " + ' '.join(f"{g}={sum(v.values())}" for g, v in row['groups'].items())
        for row in report['levels'])]
    return '\n'.join(lines) + '\n'


def execute(args):
    for side in ('pc', 'ps'):
        for key in ('', '_pack'):
            if getattr(args, side + key) is None:
                raise ValueError('--pc/--ps executables and --pc-pack/--ps-pack are required')
    packs = {side: digest(getattr(args, side + '_pack')) for side in ('pc', 'ps')}
    exes = {side: digest(getattr(args, side)) for side in ('pc', 'ps')}
    cases = load_cases(args)
    if args.levels:
        cases = {name: rows for name, rows in cases.items() if name in args.levels}
    args.out.mkdir(parents=True, exist_ok=True)
    report = dict(version=2, executables=exes, packs=packs, levels=[], complete=False, verdict='NOT RUN')

    def save():
        (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')

    for name in cases:
        row = dict(level=name, tables={}, startup={}, groups={}, first_difference=None, signatures={}, failures=[])
        report['levels'].append(row)
        facts = {}
        for side in ('pc', 'ps'):
            facts[side] = startup(getattr(args, side), name, packs[side], args.out / f'{name}-{side}-init.log')
            row['startup'][side] = facts[side]['pools']
        for core_table in facts['pc']['tables']:
            a, b = facts['pc']['tables'][core_table], facts['ps']['tables'].get(core_table)
            row['tables'][core_table] = dict(pc=list(a), ps=list(b) if b else None, equal=a == b)
        save()
    print('editions INIT PASS levels=%d engines=2' % len(report['levels']), flush=True)
    if args.negative:
        report['negative'] = negative(args, cases)
        save()
        print('editions negative PASS ' + json.dumps(report['negative']), flush=True)
    for row in report['levels']:
        name = row['level']

        with concurrent.futures.ProcessPoolExecutor(args.jobs) as pool:
            for case, result in zip(cases[name], pool.map(
                    work, [args.pc] * len(cases[name]), [args.ps] * len(cases[name]), cases[name])):
                group = row['groups'].setdefault(case['group'], dict(equal=0, different=0, error=0))
                group[result['status']] += 1
                if result['status'] == 'equal':
                    continue
                entry = dict(group=case['group'], input=case['input'], input_sha256=case['input_sha256'],
                             save_tick=case.get('save_tick'), load_tick=case.get('load_tick'),
                             status=result['status'])
                if result['status'] == 'different':
                    entry.update(result['first_difference'])
                    sig = signature(entry['field'])
                    seen = row['signatures'].setdefault(sig, dict(cases=0, earliest=None))
                    seen['cases'] += 1
                    if seen['earliest'] is None or entry['tick'] < seen['earliest']['tick']:
                        seen['earliest'] = dict(tick=entry['tick'], field=entry['field'], input=case['input'],
                                                pc=entry['pc'], ps=entry['ps'])
                    if row['first_difference'] is None:
                        row['first_difference'] = entry
                else:
                    entry.update(pc=result.get('pc'), ps=result.get('ps'), error=result.get('error'))
                if len(row['failures']) < args.keep:
                    row['failures'].append(entry)
        save()
        totals = {key: sum(g[key] for g in row['groups'].values()) for key in ('equal', 'different', 'error')}
        print(f"editions {name} {json.dumps(totals)}", flush=True)
    for side in ('pc', 'ps'):
        if digest(getattr(args, side)) != exes[side] or digest(getattr(args, side + '_pack')) != packs[side]:
            raise ValueError(side + ': executable or pack changed during the run')
    totals = [row['groups'][g][k] for row in report['levels'] for g in row['groups'] for k in ('different', 'error')]
    errors = sum(row['groups'][g]['error'] for row in report['levels'] for g in row['groups'])
    report['complete'] = True
    report['verdict'] = 'ERROR' if errors else 'DIFFERENT' if any(totals) else 'EQUAL'
    save()
    (args.out / 'table.md').write_text(table(report))
    return 2 if errors else 1 if any(totals) else 0


def self_test():
    def line(tick, value=0, schema=3):
        body = dict(schema=schema, tick=tick, value=value)
        encoded = json.dumps(body, separators=(',', ':')).encode('ascii')
        checksum = 2166136261
        for byte in encoded:
            checksum = ((checksum ^ byte) * 16777619) & 0xffffffff
        body['hash'] = f'{checksum:08x}'
        return json.dumps(body, separators=(',', ':')).encode('ascii') + b'\n'
    pc = [line(tick) for tick in range(601)]
    different = [line(tick, int(tick >= 1)) for tick in range(601)]
    controls = [('equal', pc, 0, 'equal'), ('complete difference', different, 0, 'different'),
                ('late malformed', different[:600] + [b'not JSON\n'], 0, 'error'),
                ('late short', different[:400], 0, 'error'),
                ('late schema', different[:600] + [line(600, 1, 2)], 0, 'error'),
                ('late nonzero exit', different, 42, 'error'),
                ('late extra row', different + [line(601, 1)], 0, 'error'),
                ('late repeated tick', different[:600] + [line(599, 1)], 0, 'error'),
                ('late noncanonical', different[:600] + [line(600, 1).replace(b':', b': ', 1)], 0, 'error'),
                ('late uncovered hash', different[:600] + [line(600, 1).replace(b'"value":1', b'"value":2')], 0, 'error')]
    for name, ps, code, expected in controls:
        result = verdict(window(io.BytesIO(b''.join(pc)), io.BytesIO(b''.join(ps)), 601), 0, code)
        assert result['status'] == expected, (name, result)
        assert result['pc']['records'] == 601, (name, 'PC was not fully drained')
        assert result['ps']['records'] == len(ps), (name, 'PS was not fully drained')
        if name != 'equal':
            assert result['first_difference']['tick'] == 1, (name, result)
        print('editions self-test PASS ' + name)
    assert signature('items[12].pos[1]') == 'items[].pos[]'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('campaign', 'saves', 'save-loads', 'save-matrix', 'pc', 'ps', 'pc-pack', 'ps-pack', 'out'):
        parser.add_argument('--' + name, type=Path)
    parser.add_argument('--levels', nargs='*')
    parser.add_argument('--jobs', type=int, default=3)
    parser.add_argument('--keep', type=int, default=40, help='failure entries retained per level')
    parser.add_argument('--negative', action='store_true')
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--table', type=Path, help='print the table for a finished report.json')
    args = parser.parse_args()
    try:
        if args.self_test:
            self_test()
            return
        if args.table:
            print(table(json.loads(args.table.read_text())), end='')
            return
        if args.out is None or not (args.campaign or args.saves or args.save_matrix):
            parser.error('--out and at least one case source are required')
        if args.saves and not args.save_loads:
            parser.error('--saves requires --save-loads (native qualified report with load ticks)')
        code = execute(args)
    except (ValueError, OSError, KeyError, TypeError, StopIteration, subprocess.SubprocessError) as error:
        parser.exit(2, f'editions ERROR {error!r}\n')
    raise SystemExit(code)


if __name__ == '__main__':
    main()
