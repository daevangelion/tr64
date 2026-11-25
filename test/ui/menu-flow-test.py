#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Real cart flow on ares: title, new game, ring, passport save into slot 2, power cycle, load.

Needs the private ares from tools/ares/build-sram.py (SRAM import, export on TERM).
Two emulator processes: the second imports the first's exported SRAM. Pictures
are kept for human review; the script only proves control flow and restoration.
"""
import argparse, json, os, re, signal, subprocess, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/ship"))
from drive import pad_start, pad_write  # noqa: E402
from gate_logs import guest_identity  # noqa: E402
BTN = dict(B=0x40000000, START=0x10000000, DOWN=0x04000000, UP=0x08000000)


class Run:
    def __init__(self, rom, ares, out, sram_in, sram_out=None):
        self.out, self.marks = out, []
        out.mkdir(parents=True)
        (out / "vi").mkdir()
        self.pad, self.log = out / "pad.in", out / "ares.log"
        pad_start(self.pad)
        env = dict(os.environ, ARES_BINARY=str(ares), ARES_EXPANSION_PAK="false",
                   ARES_TEST_INPUT=str(self.pad), ARES_SRAM_IN=str(sram_in),
                   ARES_HOST_SCREENSHOT="1", ARES_HOST_SHOT_EVERY_MS="400",
                   ARES_HOST_SHOT_DIR=str(out / "vi"), ARES_HOST_SHOT_MAX="400",
                   TMPDIR=str(out / "tmp"))
        if sram_out:
            env["ARES_SRAM_OUT"] = str(sram_out)
        self.proc = subprocess.Popen(["bash", str(ROOT / "test/run/ares.sh"), str(out), str(rom),
                                      str(self.log), "300"], cwd=ROOT, env=env, start_new_session=True)

    def text(self):
        return self.log.read_text(errors="replace") if self.log.exists() else ""

    def ticks(self):
        found = re.findall(r"play-frame: tick=(\d+)", self.text())
        return int(found[-1]) if found else 0

    def check(self):
        text = self.text()
        if "tr64 fatal:" in text or self.proc.poll() is not None:
            raise RuntimeError("fatal or early exit: " + text[-500:])

    def wait(self, pattern, timeout=180):
        end = time.monotonic() + timeout
        while not re.search(pattern, self.text(), re.M):
            self.check()
            if time.monotonic() > end:
                raise TimeoutError(pattern)
            time.sleep(0.1)

    def idle(self, ticks):
        target = self.ticks() + ticks
        while self.ticks() < target:
            self.check()
            time.sleep(0.05)

    def press(self, *names, hold=3, gap=4):
        value = sum(BTN[name] for name in names)
        pad_write(self.pad, f"{value:x} 0 0\n")
        self.idle(hold)
        pad_write(self.pad, "0 0 0\n")
        self.idle(gap)

    def shot(self, name):
        count = len(list((self.out / "vi").glob("*.ppm")))
        end = time.monotonic() + 10
        while len(list((self.out / "vi").glob("*.ppm"))) <= count + 1 and time.monotonic() < end:
            time.sleep(0.1)
        picture = sorted((self.out / "vi").glob("*.ppm"))[-1]
        self.marks.append(dict(name=name, tick=self.ticks(), picture=picture.name))

    def stop(self):
        if self.proc.poll() is None:
            os.killpg(self.proc.pid, signal.SIGTERM)
        self.proc.wait(timeout=30)
        keep = {m["picture"] for m in self.marks}
        for picture in (self.out / "vi").glob("*.ppm"):
            if picture.name not in keep:
                picture.unlink()


def ppm(path):
    data = path.read_bytes()
    fields, at = [], 0
    while len(fields) < 4:
        while data[at:at + 1].isspace():
            at += 1
        end = at
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[at:end])
        at = end
    assert fields[0] == b"P6" and fields[1:] == [b"320", b"240", b"255"], fields
    return data[at + 1:]


def ring_pixels(before, after):
    """Pixels changed in the centre band (y 40..199, away from the title and
    label rows): the ring items. Compass alone is about 25x6 px at 768 units
    (docs/formats/n64-ui.md, ring size); an absent or off-screen ring fails."""
    changed = 0
    for y in range(40, 200):
        for x in range(40, 280):
            i = 3 * (320 * y + x)
            changed += before[i:i + 3] != after[i:i + 3]
    return changed


def eye(text):
    return tuple(map(int, re.findall(r"game-render: room=\d+ eye=(-?\d+),(-?\d+),(-?\d+)", text)[-1]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--identity-header", type=Path, help="independent cart_identity.h for a relocated ROM")
    parser.add_argument("--ares", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        expected_build = guest_identity.expected_identity(args.rom.parent, args.identity_header)
    except ValueError as error:
        parser.error(str(error))
    out = args.out.resolve()
    out.mkdir(parents=True)
    erased = out / "erased.srm"
    erased.write_bytes(b"\xff" * 32768)
    saved = out / "saved.srm"
    first = Run(args.rom.resolve(), args.ares.resolve(), out / "first", erased, saved)
    try:
        first.wait(r"play: ready=title")
        first.idle(40)
        first.shot("title")
        first.press("B")            # open the title passport
        first.idle(40)
        first.shot("title-passport")
        first.press("B")            # new game
        first.wait(r"play: ready=level1")
        first.idle(30)
        pad_write(first.pad, f"{BTN['UP']:x} 0 0\n")
        first.idle(50)
        pad_write(first.pad, "0 0 0\n")
        first.idle(30)
        saved_eye = eye(first.text())
        first.shot("before-ring")
        first.press("START")
        first.idle(40)
        first.shot("ring")
        first.press("DOWN")
        first.idle(30)
        first.press("B")            # options ring: passport
        first.idle(50)
        first.shot("passport-save")
        first.press("B")            # opens the three-slot picker
        first.idle(30)
        first.shot("picker-save")
        first.press("DOWN", gap=10)  # slot 2
        first.press("B")
        first.wait(r"save: slot=1 bytes=\d+ status=0")
        first.idle(40)
    finally:
        first.stop()
    build_id, config_id = guest_identity.require_identity(first.text(), expected_build, play=True)
    assert saved.stat().st_size == 32768 and set(saved.read_bytes()) != {0xFF}, "SRAM was not exported"
    vi = out / "first" / "vi"
    marks = {m["name"]: vi / m["picture"] for m in first.marks}
    ring_changed = ring_pixels(ppm(marks["before-ring"]), ppm(marks["ring"]))
    assert ring_changed >= 100, f"ring covers too little of the centre band: {ring_changed} px"
    second = Run(args.rom.resolve(), args.ares.resolve(), out / "second", saved)
    try:
        second.wait(r"play: ready=title")
        second.idle(40)
        second.press("B")           # title passport opens on 'Current Position'
        second.idle(50)
        second.shot("load-list")
        second.press("B")           # picker: only slot 2 holds a save
        second.idle(30)
        second.shot("picker-load")
        second.press("B")
        second.wait(r"play: ready=level1")
        second.idle(30)
        second.shot("loaded")
        loaded_eye = eye(second.text())
    finally:
        second.stop()
    guest_identity.require_identity(second.text(), expected_build, play=True)
    assert "save: bind status=0 slots=1" in second.text(), "second process did not see the saved slot"
    start_eye = (73754, 2216, 3577)  # LEVEL1 start camera, seen unplayed
    assert abs(loaded_eye[0] - saved_eye[0]) < 64 and abs(loaded_eye[2] - saved_eye[2]) < 64 and \
        abs(loaded_eye[2] - start_eye[2]) > 512, f"position not restored {saved_eye} {loaded_eye}"
    for run in (first, second):
        assert "tr64 fatal:" not in run.text()
    report = dict(pass_=True, build_id=build_id, config_id=config_id, ring_changed_pixels=ring_changed, saved_eye=saved_eye,
                  loaded_eye=loaded_eye,
                  first=first.marks, second=second.marks,
                  scope="control flow and restored camera across two ares processes; pictures need review")
    (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
