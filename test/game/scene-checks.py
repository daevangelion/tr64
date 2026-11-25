# SPDX-License-Identifier: MIT
# Original scene observable; no OpenLara implementation code.
# Numeric state contract: src/fixed/common.h at
# 2333adef30ae2a5f3b32c5355e6e343d6aa0531a; schema2 is test/oracle/README.md.
"""Scene-runner observable for every canonical host/cart state hash."""
import importlib.util
import json
from pathlib import Path
import re

_spec = importlib.util.spec_from_file_location('game_trace_compare', Path(__file__).with_name('compare.py'))
_compare = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_compare)


def hashes(context, operands):
    if len(operands) != 1:
        raise ValueError('game-hashes requires the verified host JSONL trace path')
    expected = []
    schema=int(context['meta'].get('hash_schema',2))
    with Path(operands[0]).open(encoding='ascii') as source:
        for tick, line in enumerate(source):
            record = json.loads(line)
            _compare.validate(record, line, tick, str(operands[0]),schema=schema)
            expected.append(record['hash'])
    if len(expected) != int(context['meta']['ticks']) + 1:
        raise ValueError('host trace does not cover the complete scene window')
    suffix=r' hash_schema=3' if schema==3 else ''
    observed = re.findall(r'^game-tick: tick=(\d+) hash=([0-9a-f]{8}) tick_us=(\d+)'+suffix+'$', context['log'], re.M)
    for tick, wanted in enumerate(expected):
        if tick >= len(observed):
            raise ValueError(f'tick={tick} field=hash expected={wanted} actual=missing')
        number, got, _ = observed[tick]
        if int(number) != tick:
            raise ValueError(f'tick={tick} field=tick expected={tick} actual={number}')
        if wanted != got:
            raise ValueError(f'tick={tick} field=hash expected={wanted} actual={got}')
    if len(observed) != len(expected):
        raise ValueError(f'game hash record count expected={len(expected)} actual={len(observed)}')
    context['results']['game-ticks'] = len(expected)-1
    context['results']['game-tick-max-us'] = max(int(row[2]) for row in observed)
    context['results']['game-final-hash'] = expected[-1]


CHECKS = {'game-hashes': hashes}
