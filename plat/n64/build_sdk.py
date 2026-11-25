#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Adapted from MIT hl64 plat/n64/build_sdk.sh: archive pinned sources; no checkout edits.
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess


def _prefix():
    return ['rtk', 'proxy'] if shutil.which('rtk') else []


def run(*args, **kw):
    return subprocess.run([*_prefix(), *map(str, args)], check=True, **kw)


def archive(repo, revision, destination):
    destination.mkdir(parents=True)
    process = subprocess.Popen([*_prefix(), 'git', '-C', str(repo), 'archive', revision],
                               stdout=subprocess.PIPE)
    try:
        run('tar', '-x', '-C', destination, stdin=process.stdout)
    finally:
        process.stdout.close()
    if process.wait():
        raise RuntimeError('git archive failed')


def main():
    home = Path.home()
    default_tc = Path(os.environ['N64_GCCPREFIX']) if os.environ.get('N64_GCCPREFIX') else home/'sdks/mips64-toolchain'
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--libdragon-rev', required=True)
    p.add_argument('--tiny3d-rev', required=True)
    p.add_argument('--libdragon-repo', default=home/'src/libdragon-hl64', type=Path)
    p.add_argument('--tiny3d-repo', default=home/'src/tiny3d-tr64', type=Path)
    p.add_argument('--gcc-prefix', default=default_tc, type=Path)
    p.add_argument('--work', required=True, type=Path)
    p.add_argument('--prefix', required=True, type=Path)
    p.add_argument('--jobs', default=4, type=int)
    a = p.parse_args()
    for rev in (a.libdragon_rev, a.tiny3d_rev):
        if len(rev) != 40 or any(c not in '0123456789abcdef' for c in rev):
            p.error('revisions must be full lowercase commit ids')
    if a.work.exists() or a.prefix.exists() or a.jobs < 1:
        p.error('work and prefix must be new owned paths; jobs must be positive')
    ld = a.libdragon_repo.expanduser().resolve()
    tiny = a.tiny3d_repo.expanduser().resolve()
    tc = a.gcc_prefix.expanduser().resolve()
    for repo, rev in ((ld, a.libdragon_rev), (tiny, a.tiny3d_rev)):
        run('git', '-C', repo, 'cat-file', '-e', rev+'^{commit}')
    archive(ld, a.libdragon_rev, a.work/'libdragon')
    archive(tiny, a.tiny3d_rev, a.work/'tiny3d')
    # The same GCC16 diagnostic adaptation as the pinned hl64 SDK builder.
    makefile = a.work/'tiny3d/Makefile'
    with makefile.open('a') as f:
        f.write('\nN64_CFLAGS += -Wno-error=type-limits -Wno-error=format\n')
    a.prefix.mkdir(parents=True)
    env = dict(os.environ, N64_INST=str(a.prefix), N64_GCCPREFIX=str(tc))
    run('make', '-C', a.work/'libdragon', '-j'+str(a.jobs), 'install-mk', 'libdragon', env=env)
    run('make', '-C', a.work/'libdragon', '-j1', 'tools', env=env)
    run('make', '-C', a.work/'libdragon', 'install', 'tools-install', env=env)
    run('make', '-C', a.work/'tiny3d', '-j'+str(a.jobs), env=env)
    run('make', '-C', a.work/'tiny3d', 'install', env=env)
    compiler = run(tc/'bin/mips64-elf-gcc', '--version', capture_output=True, text=True).stdout.splitlines()[0]
    manifest = dict(package='n64-sdk-tr64', libdragon_rev=a.libdragon_rev,
                    tiny3d_rev=a.tiny3d_rev, compiler=compiler,
                    adaptation='GCC16 warning severity; pinned archives, no source checkout edits')
    (a.prefix/'MANIFEST.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print('installed', a.prefix, flush=True)


if __name__ == '__main__':
    main()
