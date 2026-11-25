# SPDX-License-Identifier: MIT
"""Relink the pinned cached ares into an explicit private output; never build/install shared targets."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess

REVISION = 'd9117003192cd43806ddc24d20c764ddb9afc965'
PREIMAGE = '10449ea6c4ddaa54fac83cbe24b440f8f32fa4ad'
POSTIMAGE = '16a8f5052b9b2040e0da9f44f3ee82dc8e5cdc5542f029ad94efefa9d0d34c33'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(argv, cwd, env=None, capture=False, input=None):
    command = ['rtk', 'proxy', *map(str, argv)]
    print(shlex.join(command), flush=True)
    return subprocess.run(command, cwd=cwd, env=env, input=input,
                          text=True, check=True, capture_output=capture)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--cache', type=Path, required=True)
    parser.add_argument('--scratch', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source, cache, scratch, output = (p.resolve() for p in
        (args.source, args.cache, args.scratch, args.output))
    for writable in (scratch, output):
        if writable.is_relative_to(source) or writable.is_relative_to(cache):
            raise ValueError('private scratch/output must be outside the shared source/build')
    revision = run(['git', 'rev-parse', 'HEAD'], source, capture=True).stdout.strip()
    if revision != REVISION:
        raise ValueError(f'ares revision differs: {revision}')
    original = source/'ares/n64/cartridge/testharness.cpp'
    data = original.read_bytes()
    blob = hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
    if blob != PREIMAGE:
        raise ValueError('existing local harness preimage differs')
    shared = cache/'desktop-ui/ares'
    before = sha(shared)
    scratch.mkdir(parents=True, exist_ok=True)
    output.parent.mkdir(parents=True, exist_ok=True)
    private = scratch/'cartridge'
    patch = Path(__file__).with_name('sram-persistence.patch')
    if not private.exists():
        shutil.copytree(original.parent, private)
        run(['patch', '--batch', '--forward', '-p4'], private, input=patch.read_text())
    if sha(private/'testharness.cpp') != POSTIMAGE:
        raise ValueError('private patched harness differs')
    for item in original.parent.iterdir():
        if item.is_file() and item.name != 'testharness.cpp':
            if sha(item) != sha(private/item.name):
                raise ValueError(f'private cartridge source differs: {item.name}')
    lines = run(['ninja', '-C', cache, '-t', 'commands', '-s',
                 'ares/CMakeFiles/ares.dir/n64/cartridge/cartridge.cpp.o',
                 'desktop-ui/ares'], cache, capture=True).stdout.splitlines()
    if len(lines) != 2:
        raise ValueError('unexpected cached command graph')
    compile = shlex.split(lines[0])
    tokens = shlex.split(lines[1])
    if tokens[:2] != [':', '&&']:
        raise ValueError('unexpected cached link wrapper')
    stop = tokens.index('&&', 2)
    link = tokens[2:stop]  # Intentionally discard ALL shared install/copy actions.
    obj = output.with_name(output.name+'.cartridge.o')
    for flag, value in (('-o', obj), ('-MT', obj), ('-MF', str(obj)+'.d'),
                        ('-c', private/'cartridge.cpp')):
        compile[compile.index(flag)+1] = str(value)
    link[link.index('-o')+1] = str(output)
    dependency_flags = [i for i, value in enumerate(link)
                        if value.startswith('-Wl,--dependency-file=')]
    if len(dependency_flags) != 1:
        raise ValueError('unexpected cached link dependency output')
    link[dependency_flags[0]] = '-Wl,--dependency-file='+str(output)+'.link.d'
    # Keep LTO/optimization settings; limit only parallel compilation pressure.
    link = ['-flto=2' if value == '-flto=auto' else value for value in link]
    archive = link.index('ares/libares.a')
    link.insert(archive, str(obj))
    link.insert(archive+1, '-Wl,-Map='+str(output)+'.map')
    environment = dict(os.environ, TMPDIR=str(scratch))
    run(compile, cache, env=environment)
    run(link, cache, env=environment)
    after = sha(shared)
    if after != before:
        raise ValueError('shared ares binary changed during private build')
    report = dict(ares_revision=revision, harness_preimage=blob,
                  harness_postimage=POSTIMAGE, patch_sha256=sha(patch),
                  shared_binary=str(shared), shared_sha256_before=before,
                  shared_sha256_after=after, private_binary=str(output),
                  private_sha256=sha(output), compile=compile, link=link,
                  source_bytes=sum(p.stat().st_size for p in private.iterdir() if p.is_file()))
    output.with_name(output.name+'.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({k: report[k] for k in ('ares_revision', 'private_binary',
          'private_sha256', 'shared_sha256_after', 'source_bytes')}, indent=2))


if __name__ == '__main__':
    main()
