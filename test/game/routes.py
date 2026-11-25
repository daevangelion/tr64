#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Original route compilation tooling; no OpenLara implementation code.
# InputState names/values: src/fixed/common.h at
# 2333adef30ae2a5f3b32c5355e6e343d6aa0531a; input protocol: test/oracle/README.md.
"""Translate immutable oracle masks into the production game scene route format."""
import argparse
from pathlib import Path
import re


def route(level, masks, reference, schema=2):
    words = masks.read_text(encoding='ascii').split()
    place = None
    if words and words[0] == 'start':
        if len(words) < 6 or any(not re.fullmatch('[+-]?[0-9]+', word) for word in words[1:6]):
            raise ValueError(f'{masks}: malformed start directive')
        place = [int(word, 10) for word in words[1:6]]
        words = words[6:]
    values = []
    for word in words:
        if not re.fullmatch('(?:[0-9]+|0[xX][0-9a-fA-F]+)', word):
            raise ValueError(f'{masks}: malformed action mask {word}')
        value = int(word, 16 if word.lower().startswith('0x') else 10)
        if value & ~(2|4|8|16|32|64|128|256|512|2048):
            raise ValueError(f'{masks}: undefined action bits or START')
        values.append(value)
    if not 1 <= len(values) <= 600:
        raise ValueError(f'{masks}: scene tick count outside 1..600')
    if schema not in (2,3):
        raise ValueError('game route hash schema must be 2 or 3')
    name = f'm2-{level}-{masks.stem}'
    lines = ['# SPDX-License-Identifier: MIT',
             '# Generated from immutable oracle input; no simulation checkpoints.',
             f'scene {name}', 'start boot', f'load level {level}', 'game',
             f'ticks {len(values)}', 'seed 1']
    if schema==3:
        lines.append('hash-schema 3')
    if place is not None:
        lines.append('place ' + ' '.join(map(str, place)))
    previous = None
    for tick, value in enumerate(values):
        if value != previous:
            lines.append(f'at {tick} pad {value} 0 0')
            previous = value
    # Preserve /proc/<producer-pid>/fd references to memory-only trace handles.
    lines += [f'observe game-hashes {reference.absolute()}', 'observe pools', 'observe placement']
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('level', choices=['gym', 'level1', 'level2', 'level3a', 'level3b',
        'level4', 'level5', 'level6', 'level7a', 'level7b', 'level8a', 'level8b',
        'level8c', 'level10a', 'level10b', 'level10c'])
    parser.add_argument('masks', type=Path)
    parser.add_argument('host_trace', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--schema',type=int,choices=(2,3),default=2)
    args = parser.parse_args()
    try:
        text = route(args.level, args.masks, args.host_trace,args.schema)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding='ascii')
    except (OSError, ValueError) as error:
        parser.exit(1, f'game route FAIL {error}\n')
    print(f'game route {args.output}')


if __name__ == '__main__':
    main()
