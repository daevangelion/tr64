#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""A stand-in for ares running the shipping cart, for tools/ship/test_boot.py.

Takes the arguments of test/run/ares.sh (DIR ROM LOG TIMEOUT), reads the pad file named by ARES_TEST_INPUT once per
tick as ares does, and writes the log lines tools/ship/boot.py waits for, in the order and with the menu states the real
cart logs for the base run: title passport, new game, forward run, ring open and close, then the save (ring, options ring,
passport, slot picker, cursor, accept). SIM_FAULT injects one pad fault:

  lost-up        the first UP press in the picker never reaches the cart
  doubled-up     the first UP press in the picker is seen twice (the cursor moves twice)
  lost-b-picker  the B press that opens the slot picker never reaches the cart
  cursor-slips   the cart logs the cursor on slot 3, then holds slot 1: accept saves into the wrong slot
  terminal-underrun    only the final restart reports an AI-output underrun
  terminal-missing     the final restart lacks its own audio report
  terminal-no-counter  the final AI row has no underrun count
  terminal-duplicate   append a second terminal report with an underrun on TERM
  terminal-bad-value   the final underrun count has an invalid suffix
  terminal-repeat-key  the final row has conflicting underrun counts
  finalization-failed  the launcher returns copy-back failure after the report
  sound-interleaved    a screenshot splits an earlier guest output row
  sound-prefix-ares     an emulator notice precedes an earlier bad output row
  terminal-prefix-harness  a harness notice precedes a duplicate terminal output row
"""
import hashlib
import os
import signal
from pathlib import Path
import sys
import time

B, START, UP, DOWN = 0x40000000, 0x10000000, 0x08000000, 0x04000000
L, CLEFT, CRIGHT = 0x00200000, 0x00020000, 0x00010000
ACTION = {B: 0x80, START: 0x800, UP: 0x100, DOWN: 0x200}


def main():
    out, rom, log_path, _timeout = sys.argv[1:5]
    pad_path = os.environ["ARES_TEST_INPUT"]
    sram_out = os.environ.get("ARES_SRAM_OUT")
    fault = os.environ.get("SIM_FAULT", "")
    lane = Path(out).name
    source, config = ("F00DE430EC0B", "cd" * 32) if lane in ("gate", "pace") else ("0500E430EC0B", "ab" * 32)
    if lane in ("power-cycle", "gate", "pace"):
        # The full input-loop fixture exercises the base only; give each secondary its own boot evidence.
        Path(log_path).write_text(f"play: build_id={source} config_id={config} mode=live\n"
                                  f"level-pack: sha256={hashlib.sha256(b'pack').hexdigest()}\n")
        return 0
    log = open(log_path, "a", buffering=1, encoding="utf-8", errors="surrogateescape")
    previous_output = ""

    def emit(line):
        log.write(line + "\n")

    def audio_report(tick, counter, damage=None):
        music_underruns = 1 if damage == "music-underrun" else 0
        emit("music: " if damage == "music-empty" else
             f"music: starts=1 loops=0 unavailable=0 underruns={music_underruns} frames={tick * 448} "
             "reads=1 bytes=1024 arena=19984 mix_us=100 peak_buffer_us=100 frame_peak_us=100 "
             "startup_peak_us=100 codec=1 rate=22050 current=-1 phase=0")
        emit(f"audio: ticks={tick} events=2 played=1 underruns=0 frames={tick * 448} mixer_us=300 mixer_peak_buffer_us=100")
        emit("audio-ambient: sources=0 started=0 stopped=0")
        if damage == "standalone-screenshot":
            emit("[ares] screenshot test.ppm (320x240 bpp2 fb=0x3a5000 tag=t0144)")
        output_report(tick, counter, damage=damage)
        if damage == "duplicate-split":
            emit(f"aud[ares] screenshot test.ppm\nio: ticks={tick} underruns=1")
            emit("audio-ambi[ares] screenshot test.ppm\nent: sources=0 started=0 stopped=0")
            output_report(tick, " underruns=1", damage="split")
        emit("audio-time: active_us=300")

    def output_report(tick, counter, prefix="", damage=None):
        nonlocal previous_output
        if damage == "copied-output":
            emit(previous_output)
            return
        head = {
            "split": "audio-out[ares] screenshot test.ppm\nput: cart ",
            "non-utf8": "audio-out\udcffput: cart ",
        }.get(damage, "audio-output: cart ")
        row = (f"{prefix}{head}rate=22047 frames={tick * 448}{counter} "
               f"pumps={tick * 4} mixer_total_us=900 mixer_peak_pump_us=200")
        previous_output = row
        if damage == "bare-cr":
            log.write(row + "\r")
        else:
            emit(row)

    def pace(tick):
        emit(f"play-pace: level us={tick * 33333} tick={tick} frames=100 ticks=100 "
             "hist=100/0/0/0 dropped=0 restarts=0")

    def on_term(_signum, _frame):
        if fault == "terminal-duplicate" and selects == 4:
            audio_report(t, " underruns=1")
        if fault == "terminal-prefix-harness" and selects == 4:
            output_report(t, " underruns=1", "TESTHARNESS notice ")
        if sram_out:
            with open(sram_out, "wb") as sram:
                sram.write(b"\x01" * 32768)
            emit(f"TESTHARNESS SRAM out {sram_out} (32768 bytes) on signal 15")
        log.close()
        os._exit(255 if fault == "finalization-failed" else 128 + _signum)
    signal.signal(signal.SIGTERM, on_term)

    def pad():
        try:
            fields = open(pad_path).read().split()
            return int(fields[0], 16), int(fields[2])
        except (OSError, ValueError, IndexError):
            return 0, 0

    emit("tr64 boot ok")
    emit("hardware_rdram=4194304")
    if fault != "missing-id":
        observed = "000000000000" if fault == "wrong-id" else source
        configuration = "0" * 64 if fault == "wrong-config" else config
        emit(f"play: build_id={observed} config_id={configuration} mode=live")
    if fault != "missing-pack":
        emit("level-pack: sha256=" + hashlib.sha256(b"other" if fault == "wrong-pack" else b"pack").hexdigest())
    for i in range(2):
        (Path(out) / "vi" / f"synthetic-{i}.ppm").touch()
    emit("play: loading=title")
    emit("play: ready=title hz=30")
    t, frame, prev, logged = 0, 0, 0, None
    phase, ring, due = "title", "idle", []
    selects = 0
    index, x, lost_up, doubled_up, lost_b = 0, 75000, fault == "lost-up", fault == "doubled-up", fault == "lost-b-picker"

    def later(delay, line):
        due.append((t + delay, line))

    def menu(state, page, passport=0):
        return f"ui: menu frame={t} state={state} page={page} passport={passport} item=0"

    while True:
        t += 1
        word, sy = pad()
        edges = word & ~prev
        prev = word
        if word and word != logged:
            emit(f"TESTHARNESS inject buttons={word:08x} stick=0,{sy}")
            logged = word
        action = sum(bit for key, bit in ACTION.items() if word & key) | (8 if sy > 0 else 0)
        for item in [d for d in due if d[0] <= t]:
            due.remove(item)
            line = item[1]
            if line == "ring:passport-open":
                ring = "passport-open"
            elif line == "ring:title-open":
                ring = "title-open"
            elif line == "phase:title-stop":
                emit("play-stop: level=title kind=2 next_level_id=2 death_ready=0 name=transition")
                pace(t)
                audio_report(t, " underruns=0")
                emit("play: loading=level1")
            elif line == "phase:level1":
                phase, ring = "level1", "idle"
                emit("play: start_hash=level1 b0d81f2f")
                emit("play: ready=level1 hz=30")
            elif line == "ring:idle":
                ring = "idle"
            elif line == "ring:main":
                ring = "main"
            elif line == "ring:options":
                ring = "options"
            elif line == "ring:passport":
                ring = "passport"
            else:
                emit(line)
        if phase == "title":
            if edges & B and ring == "idle" and t >= 40:
                ring = "title-opening"
                emit(menu(7, 0)); later(8, menu(9, 0)); later(8, "ring:title-open")
            elif edges & B and ring == "title-open":
                ring = "title-closing"
                emit(menu(8, 0)); later(20, "phase:title-stop"); later(25, "phase:level1")
        else:
            if word & L:
                command = 1 if edges & START else 2 if edges & CLEFT else 3 if edges & CRIGHT else 0
                if command:
                    selected = "level1" if command == 2 else "level2" if command == 3 else phase
                    selects += 1
                    emit(f"play-select: from={phase} to={selected} command={command}")
                    pace(t)
                    terminal = selects == 4
                    if not terminal or fault != "terminal-missing":
                        counter = ({
                            "terminal-no-counter": "",
                            "terminal-underrun": " underruns=1",
                            "terminal-bad-value": " underruns=0oops",
                            "terminal-repeat-key": " underruns=1 underruns=0",
                        }.get(fault, " underruns=0") if terminal else " underruns=0")
                        if fault == "sound-interleaved" and selects == 2:
                            counter = " [ares] screenshot test.ppm\n underruns=0"
                        if fault == "sound-prefix-ares" and selects == 2:
                            output_report(t, " underruns=1", "[ares] screenshot test.ppm ")
                        damage = None
                        if selects == 2 and fault in ("earlier-split", "earlier-non-utf8"):
                            damage = fault.removeprefix("earlier-")
                            counter = " underruns=1"
                        if terminal and fault in ("terminal-duplicate-split", "terminal-bare-cr"):
                            damage = fault.removeprefix("terminal-")
                        if terminal and fault in ("standalone-screenshot", "copied-output", "music-empty", "music-underrun"):
                            damage = fault
                        audio_report(t, counter, damage)
                        # An unbound extra clean row used to satisfy the
                        # global minimum when this stop's report vanished.
                        if damage in ("split", "non-utf8"):
                            output_report(t, " underruns=0")
                    emit(f"play: loading={selected}")
                    emit(f"play: ready={selected} hz=30")
                    phase = selected
                action = edges = 0
            if sy > 0:
                x += 40
            if edges & START and ring == "idle":
                ring = "opening"
                emit(menu(1, 5)); later(8, menu(4, 5)); later(8, "ring:main")
            elif edges & START and ring == "main":
                ring = "closing"
                emit(menu(2, 5)); later(8, menu(0, 0)); later(8, "ring:idle")
            elif edges & DOWN and ring == "main":
                ring = "to-options"
                emit(menu(1, 7)); later(8, menu(4, 7)); later(8, "ring:options")
            elif edges & B and ring == "options":
                ring = "to-passport"
                emit(menu(7, 7, 1)); later(8, menu(9, 7, 1)); later(8, "ring:passport")
            elif edges & B and ring == "passport":
                if lost_b:
                    lost_b = False
                else:
                    index, ring = 0, "picker"
                    emit("ui: picker open load=0 valid=000 unusable=000 index=0")
            elif edges & UP and ring == "picker":
                if lost_up:
                    lost_up = False
                else:
                    for _ in range(2 if doubled_up else 1):
                        index = (index + 2) % 3
                        emit(f"ui: picker index={index}")
                    doubled_up = False
                    if fault == "cursor-slips":
                        index = 0
            elif edges & B and ring == "picker":
                emit(f"ui: picker accept slot={index} load=0")
                emit(f"save: slot={index} bytes=1968 status=0 serialize_us=2185 write_us=2037")
                emit(f'play-lara: saved tick={t} us={t * 33333} {{"schema":2,"tick":{t},"lara":{{"index":0,"type":0,'
                     f'"pos":[{x},3072,6698],"rot":[0,0,0],"room":0,"anim":103,"frame":0,"state":2,"goal":2,"next":1,'
                     f'"flags":0,"health":1000,"hSpeed":0')
                ring = "closing"
                later(1, menu(8, 7, 1)); later(9, menu(2, 7, 1)); later(17, menu(0, 0)); later(17, "ring:idle")
            if t % 10 == 0:
                emit(f'play-lara: tick tick={t} us={t * 33333} {{"schema":2,"tick":{t},"lara":{{"index":0,"type":0,'
                     f'"pos":[{x},3072,6698],"rot":[0,0,0],"room":0,"anim":103,"frame":0,"state":2,"goal":2,"next":1,'
                     f'"flags":0,"health":1000,"hSpeed":0')
        emit(f"play-frame: tick={t} input={action:04x} tick_us=70 ticks=1")
        time.sleep(0.004)


if __name__ == "__main__":
    raise SystemExit(main())
