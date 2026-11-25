#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Thin a run's host pictures on the emulator host, as a local caller does.

usage: ares-prune.py ARES.LOG PID

tools/ship/boot.py and drive.py delete the pictures their ShotPruner does not keep
as the log names them. On a remote run those pictures are on the host, and thousands
of them must not cross the wire: test/run/ares-remote.sh stages tools/ship/drive.py
next to this file and, when TR64_SHOT_PRUNE is set (boot.py and drive.py set it), runs
this beside ares. It applies the same ShotPruner to the log until PID has exited,
then once more over the whole log, so the pictures that come back are the ones a
local run keeps.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from drive import ShotPruner  # noqa: E402


def main():
    log, pid = sys.argv[1], int(sys.argv[2])
    pruner, alive = ShotPruner(), True
    while alive:
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            alive = False
        try:
            with open(log, errors="replace") as stream:
                pruner.run(stream.read())
        except FileNotFoundError:
            pass
        if alive:
            time.sleep(1)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
