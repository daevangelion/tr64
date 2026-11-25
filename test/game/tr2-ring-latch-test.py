#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Ring gun latch controls (Q-ARM-02) with a negative control for each branch.

usage: tr2-ring-latch-test.py [--game N] HOST_BUILD_DIR WALL_PACK FIXTURES SCRATCH

HOST_BUILD_DIR is a make BUILD_DIR whose host/ objects are already built
(`make host BUILD_DIR=... TR64_LEVEL_PACK=...`); the test only compiles
test/game/tr2-rex-control.c, wraps game_tr2_tick around the objects and, for
the mutants, swaps in a copy of game/tr2/lara_weapon.c with one branch changed.
No tracked file is changed. --game is the build's TR64_GAME (default 1); the
test refuses to run when an unmodified copy of lara_weapon.c compiled here does
not have the section sizes of the build's own object.

Modes (see the header of the control's ring block):
  ring-busy   pickup fixture, pistols chosen with busy hands, drawn at tick 66
  ring-flare  schema21/pool-swim, flare chosen over a pending pistol choice
  ring-land   stand fixture, pistols chosen and the flare control held in the
              same tick on dry land: gun access is open, the pistols are drawn

Every mutant removes exactly the branch or condition a mode relies on and must
stop by name at the tick and with the message given below. The unmodified copy
(null mutant) must pass all three modes."""
import argparse
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
WEAPON = 'game/tr2/lara_weapon.c'
OBJECT = 'game/tr2/lara_weapon.o'

PASS_LINE = {
    'ring-busy': 'PASS pistols chosen with busy hands are kept and drawn at tick 66 once free; unsealed',
    'ring-flare': 'PASS a flare chosen with gun access closed is drawn and drops the pending pistol choice; unsealed',
    'ring-land': 'PASS a flare requested on dry land with pistols chosen in the same tick leaves the pistols drawn; unsealed',
}
FIXTURE = {  # mode: (fixture directory, host flag)
    'ring-busy': ('pickup', '--game-tr2'),
    'ring-flare': ('schema21/pool-swim', '--game-tr2-schema21'),
    'ring-land': ('stand', '--game-tr2'),
}

MUTANTS = {
    'null': ('', ''),
    'busy clause': ("""    gs->tr2->arms.gunRequested=(e->weaponState!=WEAPON_STATE_DRAW &&
        e->weaponState!=WEAPON_STATE_READY) || e->weapon!=weapon;""",
                    """    gs->tr2->arms.gunRequested=(e->weaponState!=WEAPON_STATE_DRAW &&
        e->weaponState!=WEAPON_STATE_READY && e->weaponState!=WEAPON_STATE_BUSY) || e->weapon!=weapon;"""),
    'flare branch': ("""    } else if (flare && e->weaponState == WEAPON_STATE_FREE && !gunCanDraw(lara) &&""",
                     """    } else if (0 && flare && e->weaponState == WEAPON_STATE_FREE && !gunCanDraw(lara) &&"""),
    'pending choice kept': ("""        arms->gunRequested = false;
        e->goalWeapon = TR2_WEAPON_FLARE;
    }""", """        e->goalWeapon = TR2_WEAPON_FLARE;
    }"""),
    'gun access condition': ("""    } else if (flare && e->weaponState == WEAPON_STATE_FREE && !gunCanDraw(lara) &&""",
                             """    } else if (flare && e->weaponState == WEAPON_STATE_FREE &&"""),
}

# (name, mode, mutant, expected stop or None for a pass)
CASES = (
    ('busy hands, same gun', 'ring-busy', None, None),
    ('flare replaces a pending gun', 'ring-flare', None, None),
    ('flare with gun access open keeps the choice', 'ring-land', None, None),
    ('unmodified copy: busy hands, same gun', 'ring-busy', 'null', None),
    ('unmodified copy: flare replaces a pending gun', 'ring-flare', 'null', None),
    ('unmodified copy: flare with gun access open', 'ring-land', 'null', None),
    ('busy hands, same gun: without the busy clause', 'ring-busy', 'busy clause',
     'ring latch control: tick 40: pistol choice with busy hands was not kept'),
    ('flare replaces a pending gun: without the branch', 'ring-flare', 'flare branch',
     'ring latch control: tick 640: flare was not drawn in place of the pending pistol choice'),
    ('flare replaces a pending gun: pistol choice not dropped', 'ring-flare', 'pending choice kept',
     'ring latch control: tick 640: flare was not drawn in place of the pending pistol choice'),
    ('flare with gun access open: without the gun access condition', 'ring-land', 'gun access condition',
     'ring latch control: tick 30: pistols were not drawn: a flare replaced a gun choice with gun access open'),
)


def run(command, **kwargs):
    return subprocess.run(['rtk', *map(str, command)], capture_output=True, text=True, timeout=300, **kwargs)


def compile_object(build, game, source, out):
    gen = build / 'generated'
    result = run(['cc', '-m32', '-std=c99', '-O2', '-g', '-Wall', '-Wextra', '-Werror', f'-I{gen}/host',
                  f'-I{ROOT}', f'-I{ROOT}/include', f'-I{ROOT}/{Path(WEAPON).parent}', f'-I{gen}',
                  '-ffp-contract=off', '-fno-fast-math', f'-DTR64_GAME={game}',
                  '-D_POSIX_C_SOURCE=200809L', '-DTR64_HOST=1', '-c', source, '-o', out])
    if result.returncode:
        raise RuntimeError(f'compile {source}: {result.stderr[-1500:]}')


def sections(obj):
    return run(['size', obj]).stdout.split('\n')[1].split()[:3]


def link(build, scratch, name, control, swapped):
    own = build / 'host' / OBJECT
    objects = sorted(str(p) for p in (build / 'host').rglob('*.o') if swapped is None or p != own)
    out = scratch / name
    result = run(['cc', '-m32', '-Wl,--wrap=game_tr2_tick', '-o', out, *objects, control,
                  *([swapped] if swapped else [])])
    if result.returncode:
        raise RuntimeError(f'link {name}: {result.stderr[-1500:]}')
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--game', default='1', choices=('1', '2'))
    for name in ('build', 'pack', 'fixtures', 'scratch'):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    build, pack, fixtures, scratch = args.build, args.pack, args.fixtures, args.scratch
    scratch.mkdir(parents=True, exist_ok=True)
    control = scratch / 'tr2-rex-control.o'
    compile_object(build, args.game, ROOT / 'test/game/tr2-rex-control.c', control)
    source = (ROOT / WEAPON).read_text()
    hosts = {None: link(build, scratch, 'host-control', control, None)}
    for index, (name, (old, new)) in enumerate(MUTANTS.items()):
        if old and source.count(old) != 1:
            raise RuntimeError(f'{name}: the text to change is not in {WEAPON} exactly once')
        path = scratch / f'mutant{index}.c'
        path.write_text(source.replace(old, new) if old else source)
        obj = scratch / f'mutant{index}.o'
        compile_object(build, args.game, path, obj)
        if name == 'null' and sections(obj) != sections(build / 'host' / OBJECT):
            raise RuntimeError(f'an unmodified lara_weapon.c compiled here has sections {sections(obj)}, the '
                               f'build has {sections(build / "host" / OBJECT)}: wrong --game or flags')
        hosts[name] = link(build, scratch, f'host-mutant{index}', control, obj)
    failures = 0
    for name, mode, mutant, stop in CASES:
        fixture, flag = FIXTURE[mode]
        trace = scratch / 'trace.jsonl'
        trace.unlink(missing_ok=True)
        result = run([hosts[mutant], flag, pack, fixtures / fixture / 'input.txt', trace],
                     env=os.environ | {'TR64_REX_CONTROL': mode})
        log = result.stdout + result.stderr
        if stop is None:
            ok = result.returncode == 0 and PASS_LINE[mode] in result.stdout.splitlines()
        else:
            ok = result.returncode != 0 and stop in log and PASS_LINE[mode] not in result.stdout
        print(('PASS' if ok else 'FAIL') + f' {name}' + ('' if stop is None else ' (must stop by name)'), flush=True)
        if not ok:
            failures += 1
            print(f'  rc={result.returncode} stdout={result.stdout[-300:]!r} stderr={result.stderr[-400:]!r}')
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
