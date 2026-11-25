#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Drive the real Wall pad and observe decoded actions without test hooks."""
import argparse
import hashlib
from pathlib import Path
import re

from drive import Drive, word
from guest_identity import expected_identity, require_identity, rom_receipt

DRAW = r'^game-render:[^\n]*\n(?:(?!game-render:)[^\n]*\n)*'
PERF = r'^perf: FRM=[^\n]*\bOVL=[^\n]*\bframes=[1-9]\d* glyphs=[1-9]\d*$'
MASK = re.compile(r'^play-tr2: input=([0-9a-f]+) Lara=', re.M)
PAUSE = r'^play-tr2: inventory ring unavailable; showing limitations$'


def check_masks(text, expected):
    # The shipping loop logs every change before ticking, not repeated masks.
    observed = [int(m, 16) for m in MASK.findall(text)]
    if any(m != expected for m in observed) or re.search(PAUSE, text, re.M):
        raise AssertionError(f'expected continuous 0x{expected:05x} with no pause; mask changes: {observed}')


def check(rom, identity, output):
    d = Drive(Path.cwd(), rom, output, timeout=240, two_pads=True)

    def draws(n, after):
        return d.wait(f'(?:{DRAW}){{{n}}}', after=after)

    def hold(label, *buttons):
        d.wait_sample(1, d.inject(word(*buttons), label))
        return len(d.text())

    def release():
        d.wait_sample(1, d.inject_idle_reads('neutral before edge'))

    try:
        ready = d.wait(r'^play-tr2: ready=wall hz=30$')
        require_identity(d.text(), expected_identity(output, identity))
        assert 'hardware_rdram=4194304 sdk_rdram=4194304' in d.text()
        mark = draws(60, ready)
        assert not re.search(PERF, d.text()[ready:mark], re.M), 'profiler initially on'

        walk = d.press_until_effect(word('R'), 'hold Walk', r'^play-tr2: input=00040 Lara=[^\n]+$')
        on = d.press_until_effect(word('R','START'), 'add Start to held Walk', PERF)
        d.wait(PERF, after=on)  # Keep Start held across two windows: one edge only.
        kept_walk = draws(20, hold('release Start, keep Walk', 'R'))
        check_masks(d.text()[walk:kept_walk], 0x40)
        print('decoded mask 0x00040 throughout R -> R+Start -> R; no pause')

        release()
        paused = d.press_until_effect(word('START'), 'pause with profiler on', PAUSE)
        release()
        resumed = d.press_until_effect(word('START'), 'resume', r'^play-tr2: resumed$')
        assert 'game-render:' not in d.text()[paused:resumed]
        assert 'perf:' not in d.text()[paused:resumed]
        d.wait(PERF, after=resumed)

        action = d.press_until_effect(word('R','B'), 'hold Walk and Action', r'^play-tr2: input=000c0 Lara=[^\n]+$')
        off = hold('add Start to held Walk and Action', 'R', 'B', 'START')
        stable = draws(12, off)
        end = draws(60, stable)
        assert not re.search(PERF, d.text()[stable:end], re.M), 'profiler still on after toggle off'
        kept_action = draws(20, hold('release Start, keep Walk and Action', 'R', 'B'))
        check_masks(d.text()[action:kept_action], 0xc0)
        print('decoded mask 0x000c0 throughout R+B -> R+B+Start -> R+B; no pause')

        release()
        d.press_until_effect(word('L'), 'ordinary flare', r'^play-tr2: input=02000 Lara=[^\n]+$')
    finally:
        d.stop()

    receipt = rom_receipt((d.out / 'ares-host.txt').read_text())
    with rom.open('rb') as source:
        assert receipt == hashlib.file_digest(source, 'sha256').hexdigest()
    assert len(list(d.vi.glob('*.ppm'))) >= 2
    print('TR2 profiler PASS: default off, held edge, on/off, pause/resume, L flare; ROM', receipt)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--identity-header', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    check(args.rom.resolve(), args.identity_header, args.out.resolve())
