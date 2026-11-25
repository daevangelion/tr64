# SPDX-License-Identifier: MIT
"""Relay positives and negative controls without launching an emulator."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import tempfile
import time

from pad_relay import RelayError, relay, verify

IDLE = "0 0 0"
START = "10000000 0 0"
IDENTITY = "2" * 64


def expect_failure(run, reason):
    try:
        run()
    except RelayError as error:
        assert reason in str(error), error
    else:
        raise AssertionError(f"expected {reason}")


def case(root, lines, *, delay=.01, forged=None, ignore=False):
    root.mkdir()
    pads = tuple(root / f"pad{port}" for port in (1, 2))
    log, started, stopped, journal, windows = (root / name for name in ("log", "started", "stopped", "pad.seq", "windows"))
    log.write_text(f"TR64 sampled ares {IDENTITY}\n")
    started.touch()
    journal.write_text("pads-v2\n" + "".join(f"{p} {v}\n" for p, v in lines))

    def fake_reader():
        previous = [None, None]
        for i, (port, value) in enumerate(lines, 1):
            deadline = time.monotonic() + 2
            method_word = int(value.split()[0], 16)
            wire = f"{method_word:08x} 0 0 {i}\n"
            pad = pads[port - 1]
            while not pad.exists() or pad.read_text() != wire:
                assert time.monotonic() < deadline, f"relay never exposed entry {i}"
                time.sleep(.005)
            time.sleep(delay)
            assert pad.read_text() == wire, f"relay overwrote unacknowledged entry {i}"
            other = pads[2 - port]
            assert (other.read_text() if other.exists() else None) == previous[2 - port], "other port advanced before ack"
            previous[port - 1] = wire
            # Independent protocol fixture, not a production formatter.
            n = time.monotonic_ns()
            line = (f"TESTHARNESS pad-sampled port={port} entry={i} buttons={method_word:08x} stick=0,0 ns={n}"
                    if not forged else forged.format(ns=n))
            with log.open("a") as stream:
                stream.write(line + "\n")
            if forged:
                return

    def run(source, timeout):
        relay(source, pads, log, started, stopped, windows, IDENTITY, timeout)

    def check(text):
        verify(text, journal, windows, IDENTITY)

    with ThreadPoolExecutor(1) as pool, journal.open() as source:
        fake = pool.submit(fake_reader) if not ignore else None
        if forged or ignore:
            expect_failure(lambda: run(source, .25), "port=1 entry=1")
        else:
            run(source, 2)
        if fake:
            fake.result()
    if forged or ignore:
        assert pads[0].read_text().endswith(" 1\n") and not pads[1].exists()
        return
    text = log.read_text()
    check(text)
    expect_failure(lambda: check(text[:-1]), f"port={lines[-1][0]} entry={len(lines)}")
    fields = text.splitlines(keepends=True)
    bad_ns = int(fields[-1].split(" ns=")[1]) + 3_000_000_000
    expect_failure(lambda: check("".join(fields[:-1]) + fields[-1].split(" ns=")[0] + f" ns={bad_ns}\n"),
                   f"port={lines[-1][0]} entry={len(lines)}")
    expect_failure(lambda: check("".join(fields[:-1])),
                   f"port={lines[-1][0]} entry={len(lines)}")
    journal.write_text("pads-v2\n1 0 0 0\n")
    expect_failure(lambda: check(text), "port=2: no entries")
    journal.unlink()
    expect_failure(lambda: check(text), "port=2: missing")


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as scratch:
        root = Path(scratch)
        first = [(1, START), (2, IDLE)]
        case(root / "never", first, ignore=True)
        case(root / "late-valid", first, delay=.15)
        case(root / "films", [(1, START), (2, IDLE), (1, IDLE), (1, START), (1, IDLE)])
        case(root / "wrong-port", first,
             forged="TESTHARNESS pad-sampled port=2 entry=1 buttons=10000000 stick=0,0 ns={ns}")
        case(root / "wrong-entry", first,
             forged="TESTHARNESS pad-sampled port=1 entry=2 buttons=10000000 stick=0,0 ns={ns}")
        case(root / "wrong-value", first,
             forged="TESTHARNESS pad-sampled port=1 entry=1 buttons=00000000 stick=0,0 ns={ns}")
        case(root / "bad-row", first,
             forged="TESTHARNESS pad-sampled port=1 entry=1 buttons=10000000 stick=0,0 ns={ns} TRUNCATED")
        case(root / "future-time", first,
             forged="TESTHARNESS pad-sampled port=1 entry=1 buttons=10000000 stick=0,0 ns={ns}000")
    print("pad relay PASS: global order, late-valid samples, missing pad, wrong port/entry/value, malformed and out-of-deadline samples")
