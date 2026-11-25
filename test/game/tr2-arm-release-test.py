#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Published arm joints after holster must match a route that never drew.

The control binary wraps game_tr2_tick and prints BODY and ARM lines.
Q-ARM-01: the first game clears the arm index on holster completion, which
is the publication gate. The sequel keeps that index for the record. An
optional second host is the unfixed tree: its first shared free frame
after the pistol holster must still publish all six arm joints.
"""
import subprocess
import sys
from pathlib import Path

DRAW, LOOK, UP, ACTION, FLARE, SELECT = 256, 512, 8, 128, 8192, 2048
FREE, BUSY, GUN = 0, 1, {2, 3, 4}
PACK = Path('/tmp/tr64/evidence/m10/wall/wall.pack')
WADE = Path('/tmp/tr64/fixtures/oracle-wall/tr2/pool-wade/input.txt')


def run(host, scratch, name, lines, extra=()):
    inp = scratch / f'{name}.txt'
    inp.write_text('\n'.join(lines) + '\n')
    out = scratch / f'{name}.jsonl'
    process = subprocess.run(
        ['rtk', str(host), '--game-tr2-schema21', str(PACK), str(inp), str(out), *extra],
        capture_output=True, timeout=90, check=False)
    if process.returncode:
        raise RuntimeError(f'{name} failed: {(process.stdout + process.stderr)[-240:]}')
    body, arms = {}, {}
    for line in process.stdout.decode().splitlines():
        part = line.split()
        if not part:
            continue
        if part[0] == 'BODY':
            body[int(part[1])] = tuple(int(n) for n in part[2:])
        elif part[0] == 'ARM':
            arms.setdefault(int(part[1]), {})[int(part[2])] = tuple(part[3:])
    return body, arms


def pairs(active, quiet, states, weapon=None):
    found = []
    for tick, row in active[0].items():
        if row[2] not in states or (weapon is not None and row[3] != weapon):
            continue
        match = [other for other, ref in quiet[0].items()
                 if ref[0] == row[0] and ref[1] == row[1] and ref[5] == row[5]]
        if match:
            found.append((tick, match[0]))
    return found


def mismatches(active, quiet, tick, other):
    return [bone for bone in range(8, 14)
            if active[1].get(tick, {}).get(bone) != quiet[1].get(other, {}).get(bone)]


def require_match(name, active, quiet, states, weapon=None):
    found = pairs(active, quiet, states, weapon)
    if not found:
        raise RuntimeError(f'{name}: no shared body frame')
    bad = [(tick, other, mismatches(active, quiet, tick, other)) for tick, other in found]
    bad = [row for row in bad if row[2]]
    if bad:
        raise RuntimeError(f'{name}: joints still published {bad[:3]}')
    print('PASS', name, len(found), 'shared frames')
    return found


def require_differ(name, active, quiet, states, weapon=None):
    found = pairs(active, quiet, states, weapon)
    if not found:
        raise RuntimeError(f'{name}: no shared body frame')
    if not any(mismatches(active, quiet, tick, other) for tick, other in found):
        raise RuntimeError(f'{name}: overlay was clear while the arm should stay published')
    print('PASS', name, 'overlay remains')
    return found


def first_free_after_holster(body):
    seen_gun = False
    for tick in sorted(body):
        if body[tick][2] in GUN:
            seen_gun = True
        elif seen_gun and body[tick][2] == FREE:
            return tick
def require_unfixed_first_tick(unfixed, fixed, quiet):
    found = sorted(pairs(unfixed, quiet, {FREE}))
    if not found:
        raise RuntimeError('unfixed: no shared free frame after holster')
    tick, other = found[0]
    if tick < first_free_after_holster(unfixed[0]):
        raise RuntimeError('shared free frame precedes the holster')
    bones = mismatches(unfixed, quiet, tick, other)
    if bones != list(range(8, 14)):
        raise RuntimeError(f'unfixed first free tick {tick} mismatched {bones}, not all six joints')
    if mismatches(fixed, quiet, tick, other):
        raise RuntimeError(f'fixed tree still publishes joints at the first free tick {tick}')
    print('PASS unfixed first free tick', tick, 'publishes all six joints; fixed does not')



def main():
    host, scratch = Path(sys.argv[1]), Path(sys.argv[2])
    unfixed_host = Path(sys.argv[3]) if len(sys.argv) > 3 else None
    scratch.mkdir(parents=True, exist_ok=True)
    holster_lines = [str(DRAW)] * 30 + [str(LOOK)] * 20 + [str(UP)] * 8
    holster = run(host, scratch, 'holster', holster_lines)
    plain = run(host, scratch, 'plain', [str(0)] * 8)
    require_match('pistol holster then run', holster, plain, {FREE})
    if not any(row[4] == 219 and row[2] == FREE for row in holster[0].values()):
        raise RuntimeError('pistol record dropped the terminal arm selection')
    if unfixed_host is not None:
        unfixed = run(unfixed_host, scratch, 'holster-unfixed', holster_lines)
        require_unfixed_first_tick(unfixed, holster, plain)
    shot = run(host, scratch, 'shotgun',
               ['0', str(SELECT), '0'] + [str(DRAW)] * 40 + [str(LOOK)] * 30 + ['0'] * 8,
               ('--shotgun-selection', '3', '158', '87'))
    placed = run(host, scratch, 'shotgun-ref',
                 ['place 63526 4599 25190 16384 35'] + ['0'] * 6)
    require_match('shotgun stow', shot, placed, {FREE})
    if not any(row[4] == 225 and row[2] == FREE for row in shot[0].values()):
        raise RuntimeError('shotgun record dropped the terminal arm selection')
    flare = run(host, scratch, 'flare',
                ['0'] * 4 + [str(FLARE)] + ['0'] * 70 + [str(FLARE)] + ['0'] * 40)
    flare_ref = run(host, scratch, 'flare-ref',
                    ['place 63176 4374 25190 16384 35'] + ['0'] * 8)
    require_differ('held flare while free', flare, flare_ref, {FREE}, weapon=8)
    dropped = sorted(t for t, row in flare[0].items()
                     if row[2] == FREE and row[3] == 0 and row[4] == 255)
    if len(dropped) < 2:
        raise RuntimeError('flare was not thrown and dropped with the arm index retained')
    if flare[1][dropped[0]] == flare[1][dropped[1]]:
        raise RuntimeError('dropped flare still publishes a frozen drawn pose')
    print('PASS flare thrown and dropped', dropped[0], 'index retained, joints follow the body')
    pickup = run(host, scratch, 'pickup',
                 ['place 64000 408 36252 0 38'] + [str(DRAW)] * 30 + [str(LOOK)] * 25 +
                 [str(ACTION)] * 8)
    pickup_ref = run(host, scratch, 'pickup-ref',
                     ['place 64000 408 36252 0 38'] + [str(ACTION)] * 8)
    require_match('pickup with weapons holstered', pickup, pickup_ref, {BUSY})
    prefix = WADE.read_text().splitlines()[:568]
    wading = run(host, scratch, 'wade-draw', prefix + [str(DRAW)] * 12)
    wade_ref = run(host, scratch, 'wade-ref', prefix + ['0'] * 12)
    require_differ('drawing while wading', wading, wade_ref, GUN)


if __name__ == '__main__':
    main()
