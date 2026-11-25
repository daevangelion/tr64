#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Original comparison tooling; no OpenLara implementation code.
# Field contract: src/fixed/common.h at 2333adef30ae2a5f3b32c5355e6e343d6aa0531a;
# schema/record hashing follows test/oracle/README.md and oracle.cpp.
"""Compare schema-2 traces; shared validation also admits explicit schemas 3–5."""
import argparse
from contextlib import nullcontext
import itertools
import json
import re
from pathlib import Path


def first_difference(expected, actual, field=''):
    if type(expected) is not type(actual):
        return field, expected, actual
    if isinstance(expected, dict):
        if list(expected) != list(actual):
            return field + '.keys', list(expected), list(actual)
        for key, value in expected.items():
            difference = first_difference(value, actual[key], f'{field}.{key}' if field else key)
            if difference:
                return difference
    elif isinstance(expected, list):
        if len(expected) != len(actual):
            return field + '.length', len(expected), len(actual)
        for index, (a, b) in enumerate(zip(expected, actual)):
            difference = first_difference(a, b, f'{field}[{index}]')
            if difference:
                return difference
    elif expected != actual:
        return field, expected, actual
    return None


def validate(record, line, tick, name, schema=2):
    if schema not in (2,3,4,5) or record.get('schema') != schema or type(record.get('tick')) is not int or record['tick'] != tick:
        raise ValueError(f'{name}: record {tick}: schema/tick')
    canonical = json.dumps(record, separators=(',', ':'), ensure_ascii=True)
    if line != canonical + '\n':
        raise ValueError(f'{name}: tick {tick}: noncanonical serialization')
    if not re.fullmatch('[0-9a-f]{8}', record.get('hash', '')):
        raise ValueError(f'{name}: tick {tick}: malformed hash')
    body = dict(record)
    stored = body.pop('hash')
    value = 2166136261
    for byte in json.dumps(body, separators=(',', ':'), ensure_ascii=True).encode('ascii'):
        value = ((value ^ byte) * 16777619) & 0xffffffff
    if stored != f'{value:08x}':
        raise ValueError(f'{name}: tick {tick}: record hash does not cover its fields')


def compare(oracle, trace):
    count = 0
    with Path(oracle).open(encoding='ascii', newline='') as expected, (nullcontext(trace) if hasattr(trace, 'read') else Path(trace).open(encoding='ascii', newline='')) as actual:
        for tick, (a, b) in enumerate(itertools.zip_longest(expected, actual)):
            if a is None or b is None:
                raise ValueError(f'tick={tick} field=record expected={"EOF" if a is None else "record"} actual={"EOF" if b is None else "record"}')
            left, right = json.loads(a), json.loads(b)
            difference = first_difference(left, right)
            if difference:
                field, wanted, got = difference
                raise ValueError(f'tick={tick} field={field} expected={wanted!r} actual={got!r}')
            validate(left, a, tick, str(oracle))
            validate(right, b, tick, str(trace))
            count += 1
    if not count:
        raise ValueError('empty traces')
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('oracle', type=Path)
    parser.add_argument('trace', type=Path)
    args = parser.parse_args()
    try:
        count = compare(args.oracle, args.trace)
    except (ValueError, OSError, UnicodeError) as error:
        parser.exit(1, f'game trace FAIL {error}\n')
    print(f'game trace PASS records={count} ticks={count-1} oracle={args.oracle} trace={args.trace}')


if __name__ == '__main__':
    main()
