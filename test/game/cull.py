#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Prove conservative item spheres against actual posed MG64 vertices.

Run from the repository root. Cases are existing 600-tick production mask files,
optionally with their normal start directive; no trace or retail data is stored.
Example: --case level1:/outside/level1-draw-fire-holster.masks.
The caller supplies a real native campaign pack. Build outputs and logs stay in
--build-dir/--out. This host-only driver is not linked into the shipping game.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pack', type=Path, required=True)
    p.add_argument('--build-dir', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--case', action='append', required=True, metavar='LEVEL:INPUT')
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    command = ['rtk', 'make', 'host', f'BUILD_DIR={args.build_dir.resolve()}',
               f'TR64_LEVEL_PACK={args.pack.resolve()}',
               'TR64_SCENE_TEST_SOURCES=$(filter-out $(TR64_ENGINE_SOURCES),game/common/cull.c test/game/cull.c)']
    build = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
    (args.out/'build.log').write_bytes(build.stdout)
    if build.returncode:
        p.exit(1, build.stdout.decode(errors='replace')[-4000:])
    executable = args.build_dir.resolve()/'host/tr64-host'
    report = dict(pack_sha256=digest(args.pack), executable_sha256=digest(executable), cases=[])
    for ordinal, spec in enumerate(args.case):
        level, text = spec.split(':', 1)
        path = Path(text).resolve()
        route = args.out/f'case-{ordinal}.route'
        bytecode = route.with_suffix('.rbc')
        route.write_text(f'# SPDX-License-Identifier: MIT\nscene cull-proof\nstart boot\n'
                         f'load level {level}\nprobe reload\nticks 2\nseed 1\nobserve pools\n')
        subprocess.run(['rtk', 'python3', 'tools/routec/routec.py', str(route), '-o', str(bytecode)],
                       check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        run_command = ['rtk', str(executable), '--route', str(bytecode)]
        environment = {**os.environ, 'TR64_CULL_INPUT': str(path)}
        environment.pop('TR64_CULL_NEGATIVE', None)
        run = subprocess.run(run_command, env=environment, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, timeout=120)
        log = run.stdout.decode(errors='replace')
        (args.out/f'case-{ordinal}.log').write_text(log[-16384:])
        match = re.search(r'^cull proof PASS (.+)$', log, re.M)
        if run.returncode or not match:
            p.exit(1, f'cull proof FAIL {level}:{path}\n{log[-2000:]}')
        row = dict(level=level, input=str(path), input_sha256=digest(path))
        row.update({key:int(value) for key,value in re.findall(r'(\w+)=(\d+)', match[1])})
        report['cases'].append(row)
        print(match[0], flush=True)
        if ordinal == 0:
            negative = subprocess.run(run_command, env={**environment, 'TR64_CULL_NEGATIVE':'1'},
                                      stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
            negative_log = negative.stdout.decode(errors='replace')
            (args.out/'negative.log').write_text(negative_log[-16384:])
            if not negative.returncode or 'cull containment FAIL' not in negative_log:
                p.exit(1, f'cull proof negative failed for wrong reason\n{negative_log[-2000:]}')
            report['negative'] = 'real posed vertices reject a zero Lara radius'
    if digest(args.pack) != report['pack_sha256'] or digest(executable) != report['executable_sha256']:
        p.exit(1, 'cull proof input binary/pack changed during cases')
    (args.out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print('cull proof PASS '+json.dumps(report, sort_keys=True))


if __name__ == '__main__':
    main()
