#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""One frozen executable, streamed campaign differential; never writes traces."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys
sys.dont_write_bytecode = True
import src_pc


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--pack', required=True, type=Path)
    ap.add_argument('--executable', required=True, type=Path)
    ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--data', type=Path, default=Path('/tmp/tr64/data/pc'))
    ap.add_argument('--oracle', type=Path, default=Path('/tmp/tr64/fixtures/oracle/m2-start/tr1-oracle'))
    ap.add_argument('--triggers', required=True, type=Path)
    ap.add_argument('--count', type=int, default=100)
    ap.add_argument('--seed', type=int, default=61)
    ap.add_argument('--fixtures', action='store_true', help='include the immutable 25-fixture floor and verifier controls')
    ap.add_argument('--levels', help='comma-separated subset of the sixteen levels (default: all)')
    ap.add_argument('--schema', type=int, choices=(3, 4), default=3,
                    help='trace schema compared (4 needs an oracle and executable with the world field)')
    ap.add_argument('--legacy', action='store_true', help='also regenerate the original M3 alphabet as a separate regression column')
    args = ap.parse_args()
    if args.count <= 0: ap.error('--count must be positive')
    args.out.mkdir(parents=True, exist_ok=True)
    random = module('campaign_random', 'random-inputs.py')
    catalog = module('campaign_pack', 'campaign-pack.py')
    executable = args.executable.resolve()
    report = dict(pack_sha256=digest(args.pack), oracle_sha256=digest(args.oracle),
                  executable=str(executable),executable_sha256=digest(executable),
                  trace_schema=args.schema,alphabet='M5 SELECT/navigation/death input',
                  count=args.count, seed=args.seed, levels=[])
    failed = False

    def save():
        (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        lines = ['| Level | Menu equal/loud/silent | Legacy equal/loud/silent | Oracle boundary | Error | Trigger equal/loud/silent | State B | Code B |',
                 '|---|---|---|---:|---:|---|---:|---:|']
        for row in report['levels']:
            r,old,t = row['random'],row['legacy'],row['triggers']
            lines.append(f'| {row["level"]} | {r["equal"]}/{r["loud"]}/{r["silent"]} | {old["equal"]}/{old["loud"]}/{old["silent"]} | {r["oracle_boundary"]+old["oracle_boundary"]+t["oracle_boundary"]} | {r["error"]+old["error"]+t["error"]} | {t["equal"]}/{t["loud"]}/{t["silent"]} | {row.get("state_bytes", "—")} | {row.get("code_bytes", "—")} |')
        lines += ['', '## Named-loud reasons']
        for row in report['levels']:
            for group in ('random', 'legacy', 'triggers'):
                for reason, count in sorted(row[group]['loud_reasons'].items()):
                    lines.append(f'- {row["level"]} {group}: {count} — {reason}')
        lines += ['', '## First differences and reference boundaries']
        for row in report['levels']:
            for group in ('random', 'legacy', 'triggers'):
                for failure in row[group]['failures']:
                    lines.append(f'- {row["level"]} {group}: {failure["input"]}: {failure["reason"]}')
        (args.out / 'table.md').write_text('\n'.join(lines) + '\n')

    def counters():
        return dict(equal=0, loud=0, silent=0, oracle_boundary=0, error=0, invalid=0, loud_reasons={}, failures=[],cases=[])

    for name in catalog.PLAYABLE_LEVELS:
        if args.levels and name not in args.levels.split(','):
            continue
        folder = args.out / name
        folder.mkdir(exist_ok=True)
        level_path = args.data / (name + '.phd')
        level = src_pc.load(level_path)
        row = dict(level=name, source_sha256=level.sha256, random=counters(),
                   legacy=counters(),triggers=counters())
        report['levels'].append(row)
        row['executable_sha256']=report['executable_sha256']
        empty = folder / 'empty.masks'
        empty.write_text('')
        init = subprocess.run(['rtk', str(executable), '--game-level', name,
            '--game-input', str(empty), '--game-ticks', '0', '--game-trace-only'],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
        log = init.stdout.decode(errors='replace')
        (folder / 'init.log').write_text(log)
        if init.returncode:
            row['initial_frame_error'] = init.returncode
            failed = True
        for pool, field in [('state', 'state_bytes'), ('code', 'code_bytes'), ('image', 'resident_bytes')]:
            matches = re.findall(r'pool: ' + pool + r' .*? used=(\d+)', log)
            if matches: row[field] = int(matches[-1])
        loaded = {}
        for line in log.splitlines():
            load = re.search(r'code: load (\w+) pages=(\d+)', line)
            unload = re.search(r'code: unload (\w+)', line)
            if load: loaded[load.group(1)] = int(load.group(2)) * 4096
            if unload: loaded.pop(unload.group(1), None)
        row['loaded_units'] = loaded
        if args.fixtures and name in ('gym', 'level1'):
            with (folder / 'fixtures.log').open('wb') as fixture_log:
                fixture = subprocess.run(['rtk', sys.executable, str(Path(__file__).with_name('fixtures.py')),
                    '--executable', str(executable), '--level', name, '--out', str(folder / 'fixtures'),
                    '--negative'], stdout=fixture_log, stderr=subprocess.STDOUT, timeout=180)
            row['fixtures_exit'] = fixture.returncode
            if fixture.returncode:
                failed = True

        def compare(path, group):
            nonlocal failed
            result = row[group]
            try:
                status, records, reason = random.compare_case(executable, args.oracle, level_path, path, path.name,schema=args.schema)
                result['cases'].append(dict(input=str(path),input_sha256=digest(path),
                                            status=status,records=records,reason=reason))
                if status == 'invalid-placement':
                    result['invalid'] += 1
                    return False
                result[status] += 1
                if reason: result['loud_reasons'][reason] = result['loud_reasons'].get(reason, 0) + 1
                return True
            except (ValueError, OSError, subprocess.SubprocessError) as error:
                text = str(error)
                kind = 'silent' if 'tick=' in text and 'field=' in text else 'oracle_boundary' if ('oracle stopped' in text or 'oracle malformed record' in text or re.search(r'oracle_exit=(?!0\b)-?\d+', text)) else 'error'
                result[kind] += 1
                result['failures'].append(dict(input=str(path), input_sha256=digest(path), reason=text))
                failed = True
                return True

        for group,menus in [('random',True)]+([('legacy',False)] if args.legacy else []):
            ordinal = completed = 0
            while completed < args.count and ordinal < args.count * 20:
                text = random.generated(level, ordinal, args.seed,menus=menus)
                ordinal += 1
                if text is None: continue
                path = folder / f'{group}-{ordinal-1}.masks'
                path.write_text(text)
                completed += compare(path,group)
            if completed != args.count:
                row[group]['error'] += args.count - completed
                failed = True
        for path in sorted(args.triggers.glob(name + '-*.masks')):
            compare(path, 'triggers')
        if row['executable_sha256'] != digest(executable): raise ValueError('executable changed during batch')
        save()
        print('campaign ' + name + ' ' + json.dumps({group:{key:value for key,value in row[group].items() if key not in ('cases','failures')} for group in ('random','legacy','triggers')}, sort_keys=True), flush=True)
    if report['pack_sha256'] != digest(args.pack) or report['oracle_sha256'] != digest(args.oracle):
        raise ValueError('immutable campaign input changed during batch')
    print((args.out / 'table.md').read_text(), flush=True)
    return int(failed)


if __name__ == '__main__':
    sys.exit(main())
