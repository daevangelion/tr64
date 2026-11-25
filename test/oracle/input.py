#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compile hold/tap scripts into OpenLara InputState masks (one per tick)."""
import argparse
from pathlib import Path

BUTTONS = dict(NONE=0, LEFT=2, RIGHT=4, FORWARD=8, BACK=16, JUMP=32,
               WALK=64, ACTION=128, WEAPON=256, LOOK=512, START=1024,
               SELECT=2048, ROLL=24)


def assemble(text, setup=None):
    """Returns (start, masks). `give TYPE COUNT` / `item INDEX HEALTH` lines (test-only
    placed-state setup, see README) are collected into the `setup` list if one is passed."""
    result = []
    start = None
    for line_number, line in enumerate(text.splitlines(), 1):
        words = line.split('#', 1)[0].split()
        if not words:
            continue
        try:
            if words[0] == 'start' and len(words) == 6:
                if start is not None or result:
                    raise ValueError('start must appear once before all ticks')
                start = [int(word, 10) for word in words[1:]]
                if any(not -2147483648 <= value <= 2147483647 for value in start):
                    raise ValueError('start fields must fit int32')
                continue
            if words[0] in ('give', 'item', 'activate') and len(words) == 3:
                if setup is None or result:
                    raise ValueError('give/item/activate need a setup list and must precede all ticks')
                setup.append((words[0], int(words[1], 10), int(words[2], 10)))
                continue
            if words[0] == 'hold' and len(words) == 3:
                count = int(words[2], 10)
            elif words[0] == 'tap' and len(words) == 2:
                count = 1
            else:
                raise ValueError('expected hold BUTTON[+BUTTON] TICKS or tap BUTTON')
            mask = 0
            for button in words[1].split('+'):
                mask |= BUTTONS[button]
            if mask & BUTTONS['START']:
                raise ValueError('START/free camera is unsupported by this oracle')
            if not 1 <= count <= 600 or len(result) + count > 600:
                raise ValueError('scene must fit 1..600 ticks')
            result.extend([mask] * count)
        except (KeyError, ValueError) as error:
            raise ValueError(f'line {line_number}: {error}') from error
    return start, result


def write_masks(path, masks, start=None, setup=()):
    prefix = '' if start is None else 'start ' + ' '.join(map(str, start)) + '\n'
    prefix += ''.join(f'{kind} {a} {b}\n' for kind, a, b in setup)
    Path(path).write_text(prefix + ''.join(f'{mask}\n' for mask in masks), encoding='ascii')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('script', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    setup = []
    start, masks = assemble(args.script.read_text(), setup)
    write_masks(args.output, masks, start, setup)
