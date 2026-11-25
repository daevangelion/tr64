# SPDX-License-Identifier: MIT
"""Acknowledged N64 pad relay for the private ares two-pad variant."""
import argparse
from pathlib import Path
import re
import sys
import time

ACK = re.compile(r"^TESTHARNESS pad-sampled port=([12]) entry=(\d+) buttons=([0-9a-f]{8}) stick=(-?\d+),(-?\d+) ns=(\d+)\n", re.M)
IDENTITY = re.compile(r"^TR64 sampled ares ([0-9a-f]{64})\n", re.M)
WINDOW = re.compile(r"([12]) (\d+) (\d+) (\d+)\n")
VERSION = "pads-v2"
POLL = 0.025
DWELL = 30.0


class RelayError(ValueError):
    pass


class IdentityError(ValueError):
    pass


def state(line):
    fields = line.split()
    if len(fields) != 3:
        raise ValueError("expected buttons-hex x y")
    buttons, x, y = int(fields[0], 16), int(fields[1]), int(fields[2])
    if not 0 <= buttons <= 0xffffffff or not -85 <= x <= 85 or not -85 <= y <= 85:
        raise ValueError("pad value out of range")
    return buttons, x, y


def records(log):
    for port, entry, word, x, y, ns in ACK.findall(log):
        yield int(port), int(entry), (int(word, 16), int(x), int(y)), int(ns)


def matches(record, expected, start, deadline):
    return record[:3] == expected and start <= record[3] <= deadline


def require_identity(log, expected, *, pending=False):
    announced = re.findall(r"^TR64 sampled ares [^\n]*\n", log, re.M)
    if pending and not announced:
        return False
    if len(announced) != 1 or IDENTITY.findall(log) != [expected]:
        raise IdentityError(f"sampled ares runtime identity missing or mismatched (expected {expected}); "
                            "rebuild with tools/ares/build-remote.sh tr64-ares-coop")
    return True


def wait_identity(log, stopped, identity, timeout):
    deadline = time.monotonic() + timeout
    while True:
        if require_identity(log.read_text(errors="replace"), identity, pending=True):
            return
        if stopped.exists() or time.monotonic() >= deadline:
            raise IdentityError(f"sampled ares runtime identity missing (expected {identity}); "
                                "rebuild with tools/ares/build-remote.sh tr64-ares-coop")
        time.sleep(POLL)


def replace(pad, entry, value):
    buttons, x, y = value
    tmp = pad.with_suffix(pad.suffix + ".tmp")
    tmp.write_text(f"{buttons:08x} {x} {y} {entry}\n")
    tmp.replace(pad)


def values(lines):
    lines = iter(lines)
    if next(lines, "").rstrip("\n") != VERSION:
        raise RelayError(f"pad-unsampled: missing {VERSION} journal header")
    for entry, line in enumerate(lines, 1):
        port, _, word = line.partition(" ")
        try:
            if port not in ("1", "2"):
                raise ValueError("expected port 1 or 2")
            buttons, x, y = state(word)
            if buttons & 0xffff:
                raise ValueError("buttons must be in the high half of the pad word")
            yield int(port), entry, (buttons, x, y)
        except ValueError as error:
            raise RelayError(f"pad-unsampled port={port} entry={entry}: {error}") from None


def relay(lines, pads, log, started, stopped, windows, identity, timeout=DWELL):
    """Do not expose numbered input before a runtime hello, or advance without a timely ack."""
    while not started.exists():
        if stopped.exists():
            raise IdentityError("sampled ares did not start")
        time.sleep(POLL)
    wait_identity(log, stopped, identity, timeout)
    with windows.open("x") as ledger, log.open(errors="replace") as stream:
        stream.seek(0, 2)
        for port, entry, value in values(lines):
            start = time.monotonic_ns()
            deadline = start + int(timeout * 1e9)
            ledger.write(f"{port} {entry} {start} {deadline}\n")
            ledger.flush()
            replace(pads[port - 1], entry, value)
            partial = ""
            while True:
                partial += stream.read()
                if any(matches(row, (port, entry, value), start, deadline) for row in records(partial)):
                    break
                partial = partial[partial.rfind("\n") + 1:]
                if stopped.exists() or time.monotonic_ns() >= deadline:
                    raise RelayError(f"pad-unsampled port={port} entry={entry}: no timely controller read of {value}")
                time.sleep(POLL)


def journal_entries(journal):
    if not journal.is_file():
        raise RelayError(f"pad-unsampled port=2: missing two-pad journal {journal}")
    wanted = list(values(journal.read_text().splitlines(keepends=True)))
    if {port for port, _, _ in wanted} != {1, 2}:
        missing = 2 if any(port == 1 for port, _, _ in wanted) else 1
        raise RelayError(f"pad-unsampled port={missing}: no entries in {journal}")
    return wanted


def verify(log, journal, windows, identity):
    """Recheck the copied log against the relay's emitted time windows and the complete journal."""
    require_identity(log, identity)
    wanted = journal_entries(journal)
    got = list(records(log))
    ledger = windows.read_text().splitlines(keepends=True)
    previous = 0
    for i, entry in enumerate(wanted):
        port, sequence, value = entry
        window = WINDOW.fullmatch(ledger[i]) if i < len(ledger) else None
        if not window:
            raise RelayError(f"pad-unsampled port={port} entry={sequence}: missing relay time window")
        p, e, start, deadline = map(int, window.groups())
        if (p, e) != (port, sequence) or not previous <= start <= deadline:
            raise RelayError(f"pad-unsampled port={port} entry={sequence}: invalid relay time window")
        if i >= len(got) or not matches(got[i], entry, start, deadline):
            raise RelayError(f"pad-unsampled port={port} entry={sequence}: no timely controller read of {value}")
        previous = got[i][3]
    if len(got) != len(wanted) or len(ledger) != len(wanted):
        raise RelayError("pad-unsampled: unexpected controller read or relay time window")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    writer = commands.add_parser("relay")
    for name in ("first", "second", "log", "started", "stopped"):
        writer.add_argument(name, type=Path)
    writer.add_argument("windows", type=Path)
    writer.add_argument("identity")
    checker = commands.add_parser("journal")
    checker.add_argument("journal", type=Path)
    verifier = commands.add_parser("verify")
    verifier.add_argument("log", type=Path)
    verifier.add_argument("journal", type=Path)
    verifier.add_argument("windows", type=Path)
    verifier.add_argument("identity")
    args = parser.parse_args()
    try:
        if args.command == "relay":
            relay(sys.stdin, (args.first, args.second), args.log, args.started, args.stopped, args.windows, args.identity)
        elif args.command == "journal":
            journal_entries(args.journal)
        else:
            verify(args.log.read_text(errors="replace"), args.journal, args.windows, args.identity)
    except IdentityError as error:
        print(error, file=sys.stderr)
        return 67
    except (RelayError, OSError) as error:
        print(error, file=sys.stderr)
        return 66
    return 0


if __name__ == "__main__":
    sys.exit(main())
