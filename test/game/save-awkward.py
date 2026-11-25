#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Real-input awkward Save/Load matrix; plan never executes a game.

Use explicit plan/discover/compare phases, one immutable native executable and
one pinned source oracle. Discovery freezes source-state witnesses before any
native comparison. All trace transport uses stream.Run pipes; no JSONL files.
Run with PYTHONPATH=tools/cook for the read-only PC metadata consumer.
"""
import argparse
import json
from pathlib import Path
import sys
sys.dont_write_bytecode = True
from compare import first_difference
from stream import Run
from save_coverage import CATEGORIES, Coverage, validate_cell_metadata
from save_scenes import LEVELS, LEVEL_IDS, plan, sha, static_counts, na_reason


def record(raw, tick, producer):
    try:
        row = json.loads(raw)
    except (ValueError, UnicodeError) as error:
        raise ValueError(f'{producer} tick={tick}: malformed JSON') from error
    if not isinstance(row, dict) or type(row.get('schema')) is not int or row['schema'] != 3 \
            or type(row.get('tick')) is not int or row['tick'] != tick:
        raise ValueError(f'{producer} tick={tick}: schema/sequence')
    if raw != json.dumps(row, separators=(',', ':'), ensure_ascii=True).encode() + b'\n':
        raise ValueError(f'{producer} tick={tick}: noncanonical bytes')
    body = dict(row)
    stored = body.pop('hash', None)
    value = 2166136261
    for byte in json.dumps(body, separators=(',', ':'), ensure_ascii=True).encode():
        value = ((value ^ byte) * 16777619) & 0xffffffff
    if stored != f'{value:08x}':
        raise ValueError(f'{producer} tick={tick}: uncovered schema3 field/hash')
    return row


def oracle_identity(args):
    report = json.loads(args.oracle_report.read_text())
    oracle = (args.oracle or Path(report['binary'])).resolve()
    if sha(oracle) != report['binary_sha256']:
        raise ValueError('source binary does not match the immutable report')
    return oracle, report['binary_sha256']


def validate_matrix(matrix, data):
    import src_pc
    expected = {(level, category) for level in LEVELS for category in CATEGORIES}
    actual = [(cell['level'], cell['category']) for cell in matrix['cells']]
    bound = matrix.get('max_candidates')
    if matrix.get('version') != 1 or len(actual) != len(expected) or set(actual) != expected:
        raise ValueError('matrix must contain each of the 16x10 category cells exactly once')
    if type(bound) is not int or not 1 <= bound <= 8:
        raise ValueError('invalid source discovery candidate bound')
    counts = {}
    hashes = {}
    for name in LEVELS:
        level = src_pc.load(data / (name + '.phd'))
        counts[name], hashes[name] = static_counts(level), level.sha256
    for cell in matrix['cells']:
        name, category = cell['level'], cell['category']
        reason = na_reason(name, category, counts[name])
        if cell['source_sha256'] != hashes[name] or cell['asset_counts'] != counts[name]:
            raise ValueError(name + ': matrix eligibility differs from actual source assets')
        if (cell['status'] == 'not-applicable') != (reason is not None):
            raise ValueError(name + '/' + category + ': fabricated or missing data-backed N/A')
        if reason and cell.get('reason') != reason:
            raise ValueError('N/A reason does not match actual source data')
        if len(cell['candidates']) > bound:
            raise ValueError('source discovery exceeds the declared candidate bound')
        validate_cell_metadata(cell, LEVEL_IDS[name])


def source_command(oracle, level, candidate, save=None, load=None):
    command = ['rtk', str(oracle), '--level', str(level), '--input', candidate['input'],
               '--ticks', '600', '--trace-schema', '3']
    if save is not None:
        command += ['--save-tick', str(save), '--load-tick', str(load)]
    return command


def discover_one(oracle, level, candidate, animations):
    run = Run(source_command(oracle, level, candidate), '--out')
    observation = Coverage(candidate, animations)
    count, complete = 0, False
    try:
        for raw in run.trace:
            observation.observe(record(raw, count, 'source'))
            count += 1
        complete = True
    finally:
        code, log = run.finish(kill=not complete)
    if code or count != 601:
        return dict(status='source-incomplete', records=count, exit=code, log=log[-1200:],
                    observed=observation.summary())
    if observation.witness is None:
        return dict(status='predicate-miss', records=count, reason=observation.last_reason,
                    observed=observation.summary())
    return dict(status='source-qualified', records=count, witness=observation.witness,
                observed=observation.summary())


def compare_one(executable, oracle, level, candidate, animations, save, load):
    source = Run(source_command(oracle, level, candidate, save, load), '--out')
    try:
        native = Run(['rtk', str(executable), '--game-level', candidate['level'],
                      '--game-input', candidate['input'], '--game-ticks', '600', '--game-trace-only',
                      '--game-trace-schema', '3', '--game-save-tick', str(save),
                      '--game-load-tick', str(load)], '--game-trace')
    except BaseException:
        source.finish(kill=True)
        raise
    observation = Coverage(dict(candidate, save_tick=save), animations)
    count, complete, at_save = 0, False, False
    try:
        while True:
            a, b = source.trace.readline(), native.trace.readline()
            if not a or not b:
                if a or b:
                    raise ValueError(f'tick={count} field=record source/native EOF mismatch')
                complete = True
                break
            left, right = record(a, count, 'source'), record(b, count, 'native')
            if a != b:
                field = first_difference(left, right) or ('serialization', 'source bytes', 'native bytes')
                raise ValueError(f'tick={count} field={field[0]} expected={field[1]!r} actual={field[2]!r}')
            qualified = observation.observe(left)
            if count == save:
                at_save = qualified
            count += 1
    finally:
        source_code, source_log = source.finish(kill=not complete)
        native_code, native_log = native.finish(kill=not complete)
    if source_code or native_code or count != 601:
        raise ValueError(f'records={count} source_exit={source_code} native_exit={native_code}\n'
                         f'{source_log[-800:]}\n{native_log[-800:]}')
    if not at_save:
        raise ValueError('601 equal rows but save-boundary category NOT exercised: ' + observation.last_reason)
    return dict(status='equal-and-covered', records=count, witness=observation.witness,
                save_tick=save, load_tick=load)


def run(args):
    if args.phase == 'plan':
        matrix = plan(args.data, args.out / 'inputs', args.max_candidates,
                      args.enemy_manifest, args.object_manifest, args.weapons, args.movement)
        args.out.mkdir(parents=True, exist_ok=True)
        (args.out / 'matrix.json').write_text(json.dumps(matrix, indent=2) + '\n')
        print('save awkward PLAN only: cells=160; no scene run or coverage PASS claimed')
        return
    import src_pc
    matrix = json.loads(args.matrix.read_text())
    matrix_sha = sha(args.matrix)
    validate_matrix(matrix, args.data)
    oracle, oracle_sha = oracle_identity(args)
    wanted = set(args.case or [])
    cells = [cell for cell in matrix['cells']
             if not wanted or cell['level'] + '/' + cell['category'] in wanted]
    if wanted != {cell['level'] + '/' + cell['category'] for cell in cells} and wanted:
        raise ValueError('requested category cell missing')
    args.out.mkdir(parents=True, exist_ok=True)
    report = dict(phase=args.phase, matrix_sha256=matrix_sha, oracle_sha256=oracle_sha,
                  scope='selected cells only' if wanted else 'complete 16-level matrix', cells=[])
    executable = args.executable.resolve() if args.executable else None
    if args.phase == 'compare':
        if executable is None:
            raise ValueError('compare requires an explicit immutable executable')
        report['executable_sha256'] = sha(executable)
        missing = [cell['level'] + '/' + cell['category'] for cell in cells
                   if cell['status'] not in ('source-qualified', 'not-applicable')]
        if missing:
            raise ValueError('unqualified category cells: ' + ', '.join(missing))
    cached_name, level = None, None
    failures = []
    for cell in cells:
        key = cell['level'] + '/' + cell['category']
        result = dict(level=cell['level'], category=cell['category'])
        if cell['status'] == 'not-applicable':
            result.update(status='not-applicable', reason=cell['reason'], asset_counts=cell['asset_counts'])
        else:
            path = args.data / (cell['level'] + '.phd')
            if sha(path) != cell['source_sha256']:
                raise ValueError(key + ': authored source data changed')
            if cached_name != cell['level']:
                level = src_pc.load(path)
                cached_name = cell['level']
            if args.phase == 'discover':
                attempts = []
                for candidate in cell['candidates']:
                    candidate = dict(candidate, level=cell['level'], category=cell['category'])
                    if sha(candidate['input']) != candidate['input_sha256']:
                        raise ValueError(key + ': planned input changed')
                    attempt = discover_one(oracle, path, candidate, level.data['animations'])
                    attempts.append(attempt)
                    if attempt['status'] == 'source-qualified':
                        save = attempt['witness']['tick']
                        cell.update(status='source-qualified', candidate=candidate, witness=attempt['witness'],
                                    save_tick=save, load_tick=save + 30, oracle_sha256=oracle_sha)
                        break
                else:
                    cell['status'] = 'reference-required'
                    failures.append(key)
                result.update(status=cell['status'], attempts=attempts)
            else:
                if cell.get('oracle_sha256') != oracle_sha:
                    raise ValueError(key + ': source oracle changed since qualification')
                candidate = dict(cell['candidate'], level=cell['level'], category=cell['category'])
                if sha(candidate['input']) != candidate['input_sha256']:
                    raise ValueError(key + ': qualified input changed')
                try:
                    result.update(compare_one(executable, oracle, path, candidate, level.data['animations'],
                                              cell['save_tick'], cell['load_tick']))
                except ValueError as error:
                    result.update(status='FAIL', error=str(error))
                    failures.append(key)
        report['cells'].append(result)
        print('save awkward ' + result['status'] + ' ' + key, flush=True)
        (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    if sha(oracle) != oracle_sha or sha(args.matrix) != matrix_sha:
        raise ValueError('source/matrix changed during qualification/comparison')
    if executable is not None and sha(executable) != report.get('executable_sha256'):
        raise ValueError('native executable changed during matrix')
    if args.phase == 'discover':
        (args.out / 'qualified-matrix.json').write_text(json.dumps(matrix, indent=2) + '\n')
    report['failed_cells'] = failures
    (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    if failures:
        raise ValueError('unqualified/differing cells: ' + ', '.join(failures))
    print('save awkward ' + args.phase.upper() + ' PASS covered_or_data_na=' + str(len(cells)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('phase', choices=('plan', 'discover', 'compare'))
    parser.add_argument('--data', type=Path, default=Path('/tmp/tr64/data/pc'))
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--matrix', type=Path)
    parser.add_argument('--max-candidates', type=int, default=3)
    parser.add_argument('--enemy-manifest', action='append', type=Path)
    parser.add_argument('--object-manifest', type=Path)
    parser.add_argument('--weapons', type=Path)
    parser.add_argument('--movement', type=Path)
    parser.add_argument('--oracle', type=Path)
    parser.add_argument('--oracle-report', type=Path)
    parser.add_argument('--executable', type=Path)
    parser.add_argument('--case', action='append')
    args = parser.parse_args()
    if args.phase != 'plan' and (args.matrix is None or args.oracle_report is None):
        parser.error('discover/compare require --matrix and --oracle-report')
    try:
        run(args)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, 'save awkward FAIL ' + str(error) + '\n')


if __name__ == '__main__':
    main()
