# SPDX-License-Identifier: MIT
"""The gate's runs that are not the base run: the power-cycle load and the death/level-end run.

power_cycle  delivered ROM, SRAM imported from the base run's export (the power cycle):
             title passport -> picker (must offer exactly the saved slot) -> load -> Lara is
             where she was saved; she then walks, so the loaded state is live.
death_end    gate ROM (tools/ship/gate-place.c): Lara placed in a tall fall dies, the death page
             waits and restarts the level on B; Lara placed on the level's end trigger ends it,
             the statistics page waits and START dismisses it, and the next level starts with no
             ring although START is still held.
pace         gate ROM, the load knob (tools/ship/gate-place.c): Lara runs a circle for ten seconds of the hardware
             clock with 0 ms, then 40 ms (and any other cost asked) burned after every drawn frame; in each run
             game ticks, Lara's path and the mixer's output must follow the clock, and the runs must agree.
Each returns {"pass": bool, "checks": {name: evidence}, "failure": str|None}; every check must
be true, and a step that never happens is a failure naming it.
"""
import re
import time

import gate_logs as logs
from drive import Drive, GateFailure, IDLE, FORWARD, skip_films, word

DEATH_WAIT_TICKS = 45     # the death page must stay up this long without input (1.5 s of play)
STATS_WAIT_TICKS = 30
START_HOLD_TICKS = 8      # START still held this many ticks into the next level
WALK_TICKS = 40
WALK_MIN = 256            # units Lara must have walked after a load (a block is 1024)
FILMS = ("ESCAPE", "CORE", "CAFE", "SNOW")  # skipped with START: the base run owns the video verdict


PACE_RUN_STICK = "0 -80 80\n"      # forward and left: a circle of about 560 units, inside the 3x3 flat patch
PACE_TICK_US = 1_000_000 / 30
PACE_AUDIO_BUFFER_US = 70_000       # the mixer runs up to its three-buffer queue ahead of the DAC, at both ends
PACE_MARGIN_US = 4_000_000          # clock past the window, so that audio reports span it


def verdict(text, expected, checks, failure, **evidence):
    identity = logs.shipping_identity(text, *expected)
    passed = failure is None and identity["identity_failure"] is None and bool(checks) and all(checks.values())
    return dict({"pass": passed, "checks": checks, "failure": failure}, **identity, **evidence)


def title_new_game(drive):
    """From boot to the first tick of LEVEL1 through the title passport (no usable save)."""
    drive.wait(r"^play: ready=title hz=")
    drive.wait_ticks(40)
    drive.press(word("B"), "B: open title passport", hold=4, gap=45)
    drive.press(word("B"), "B: new game", hold=4, gap=4)
    return drive.wait(r"^play: ready=level1 hz=")


def power_cycle(root, rom, out, *, expected, sram, saved_text, ares, timeout):
    drive = Drive(root, rom, out, timeout=timeout, ares=ares, sram_in=sram)
    drive.servicers.append(skip_films(FILMS))
    checks, failure = {}, None
    try:
        drive.wait(r"^play: ready=title hz=")
        drive.wait_ticks(40)
        drive.press(word("B"), "B: open title passport (Current Position)", hold=4, gap=45)
        passport = drive.wait(r"^ui: menu frame=\d+ state=9 page=0 passport=0 ")
        drive.press(word("B"), "B: open the picker", hold=4, gap=10)
        # Exactly the slot the base run saved into is offered; the others are empty.
        opened = drive.wait(r"^ui: picker open load=1 valid=001 unusable=000 index=2$", after=passport)
        drive.press(word("B"), "B: accept the saved slot", hold=4, gap=4)
        accepted = drive.wait(r"^ui: picker accept slot=2 load=1$", after=opened)
        restored = drive.wait(r"^play-lara: restored tick=\d+ us=\d+ \{", after=accepted)
        loaded = drive.wait(r"^play: ready=\w+ hz=", after=accepted)
        drive.wait_frames(loaded, 5)
        drive.inject(FORWARD, "stick forward: the loaded state is live")
        drive.wait_frames(loaded, 5 + WALK_TICKS)
        drive.inject(IDLE, "stop")
        drive.wait_frames(loaded, 5 + WALK_TICKS + 12)
    except GateFailure as error:
        failure = str(error)
    drive.stop()
    text = drive.text()
    saved, restored_sample = logs.saved_and_restored(saved_text, text)
    walked = None
    if restored_sample:
        after = [s for s in logs.lara_samples(text) if s["offset"] > restored_sample["offset"] and s["pos"]]
        walked = logs.horizontal(restored_sample["pos"], after[-1]["pos"]) if after else None
    checks.update({
        "saved_in_base_run": saved is not None,
        "restored_in_second_run": restored_sample is not None,
        "restored_is_saved": bool(saved and restored_sample and logs.same_lara(saved, restored_sample)),
        "sram_slot_bound": "save: bind status=0 slots=1 " in text,
        "level_loaded": bool(logs.levels_ready(text)[1:]),
        "walks_after_load": walked is not None and walked >= WALK_MIN,
        "no_fatal": "tr64 fatal:" not in text})
    result = verdict(text, expected, checks, failure)
    result.update(saved=saved and {k: saved[k] for k in ("tick", "pos", "rot", "room", "health", "anim", "state")},
                  restored=restored_sample and {k: restored_sample[k] for k in ("tick", "pos", "rot", "room", "health", "anim", "state")},
                  walked_units=walked, picker_log=[l for l in text.splitlines() if l.startswith("ui: picker")],
                  rom=str(rom))
    return result


def death_end(root, rom, out, *, expected, sram, ares, timeout, next_level="level2"):
    drive = Drive(root, rom, out, timeout=timeout, ares=ares, sram_in=sram)
    drive.servicers.append(skip_films(FILMS))
    mark, failure = {}, None
    try:
        ready = title_new_game(drive)
        drive.wait_frames(ready, 20)
        # --- death: placed in the air, she falls and dies; the page waits; B restarts.
        drive.inject(word("L", "Z"), "L+Z: place Lara in a tall fall")
        mark["placed_death"] = drive.wait(r"^gate: place death ok=\d$")
        drive.wait_ticks(2)
        drive.inject(IDLE, "release")
        mark["death_page"] = drive.wait(r"^ui: menu frame=\d+ state=9 page=1 passport=1 ", after=mark["placed_death"])
        drive.wait_ticks(DEATH_WAIT_TICKS)
        mark["death_wait_end"] = len(drive.text())
        drive.press(word("B"), "B: restart level (death page)", hold=4, gap=4)
        mark["restarted"] = drive.wait(r"^play: ready=level1 hz=", after=mark["death_page"])
        drive.wait_frames(mark["restarted"], 30)
        # --- natural level end: placed on the end trigger; statistics page; START dismisses it.
        drive.inject(word("L", "R"), "L+R: place Lara on the end trigger")
        mark["placed_end"] = drive.wait(r"^gate: place end ok=\d$", after=mark["restarted"])
        drive.wait_ticks(2)
        drive.inject(IDLE, "release")
        mark["stats"] = drive.wait(r"^ui: menu frame=\d+ state=\d+ page=3 ", after=mark["placed_end"])
        drive.wait_ticks(STATS_WAIT_TICKS)
        mark["stats_wait_end"] = len(drive.text())
        drive.inject(word("START"), "START: dismiss the statistics page, held into the next level")
        mark["next_ready"] = drive.wait(rf"^play: ready={next_level} hz=", after=mark["stats"])
        drive.wait_frames(mark["next_ready"], START_HOLD_TICKS)
        mark["start_released"] = len(drive.text())
        drive.inject(IDLE, "release START")
        drive.wait_frames(mark["next_ready"], START_HOLD_TICKS + 30)
    except GateFailure as error:
        failure = str(error)
    drive.stop()
    text = drive.text()
    return verdict(text, expected, death_end_checks(text, mark, next_level), failure, rom=str(rom), marks=mark)


def death_end_checks(text, mark, next_level="level2"):
    """(checks, evidence) from the final log and the offsets the run recorded (keys it never reached are absent)."""
    ready = logs.levels_ready(text)
    checks = {"levels_title_level1_restart_next": ready[-4:] == ["title", "level1", "level1", next_level],
              "no_fatal": "tr64 fatal:" not in text}
    if "placed_death" in mark:
        death = logs.death_flow(text, mark["placed_death"])
        checks["death_placed"] = "gate: place death ok=1" in text
        checks["alive_before_the_fall"] = death["alive_before"]
        checks["dies"] = death["dead_after"]
        checks["death_page_shown"] = death["page_shown"]
    if "death_wait_end" in mark:
        waited, interrupted = logs.waited(text[:mark["death_wait_end"]], mark["death_page"], DEATH_WAIT_TICKS)
        checks["death_page_waits"] = waited >= DEATH_WAIT_TICKS - 1 and not interrupted
    if "restarted" in mark:
        window = text[mark["death_page"]:mark["restarted"]]
        checks["restart_is_a_level_end"] = re.search(r"^play-stop: level=level1 kind=2 ", window, re.M) is not None
        after = text[mark["restarted"]:mark.get("placed_end", len(text))]
        alive = [s for s in logs.lara_samples(after) if s["health"] is not None]
        checks["alive_after_restart"] = bool(alive) and all(s["health"] > 0 for s in alive)
        checks["no_ring_after_restart"] = not logs.ring_open_after(after, 0)
    if "placed_end" in mark:
        checks["end_placed"] = "gate: place end ok=1" in text
    if "stats" in mark:
        checks["statistics_page_shown"] = True
    if "stats_wait_end" in mark:
        waited, interrupted = logs.waited(text[:mark["stats_wait_end"]], mark["stats"], STATS_WAIT_TICKS)
        checks["statistics_page_waits"] = waited >= STATS_WAIT_TICKS - 1 and not interrupted
    if "next_ready" in mark:
        window = text[mark["stats"]:mark["next_ready"]]
        checks["start_ends_the_level"] = re.search(r"^play-stop: level=level1 kind=2 ", window, re.M) is not None
        held = text[mark["next_ready"]:mark.get("start_released", len(text))]
        frames = re.findall(r"^play-frame: tick=\d+ input=([0-9a-f]+)", held, re.M)
        checks["next_level_ticks_with_start_held"] = len(frames) >= START_HOLD_TICKS - 1
        checks["no_select_while_start_held"] = all(not int(f, 16) & 0x800 for f in frames)
        checks["no_ring_in_next_level"] = not logs.ring_open_after(text, mark["next_ready"])
    return checks


def clock_of(text, after):
    """(first, last) hardware clock of the gate-run lines after offset `after`, from Lara's first movement."""
    runs = [tuple(map(int, m.groups())) for m in logs.RUN.finditer(text, after)]
    moving = next((r for r in runs if r[2] > 0), None)
    return (moving[1], runs[-1][1]) if moving else (None, None)


def pace_run(root, rom, out, *, expected, sram, ares, timeout, burns=(0, 40)):
    """Boot the gate ROM, reach LEVEL1, and for each burn (ms per drawn frame; multiples of 20, 0..60) run Lara in a
    circle for the window plus margin of clock. Returns pace_checks of the log."""
    drive = Drive(root, rom, out, timeout=timeout, ares=ares, sram_in=sram)
    drive.servicers.append(skip_films(FILMS))
    marks, failure, current = [], None, 0
    need_us = 1_000_000 + 10_000_000 + PACE_MARGIN_US
    try:
        ready = title_new_game(drive)
        drive.wait_frames(ready, 20)
        for burn in burns:
            steps = ((burn - current) // 20) % 4
            knob_from = len(drive.text())
            for _ in range(steps):
                drive.press(word("L", "A"), "L+A: next load knob step", hold=3, gap=3)
            if steps:
                drive.wait(rf"^gate: burn_ms={burn}$", after=knob_from)
            current = burn
            began = len(drive.text())
            drive.inject(word("L", "B"), "L+B: place Lara on the flat patch, start the odometer")
            placed = drive.wait(r"^gate: place run ok=1$", after=began)
            drive.wait_ticks(2)
            drive.inject(IDLE, "release")
            drive.wait_ticks(2)
            drive.inject(PACE_RUN_STICK, f"stick forward+left: a circle, burn {burn} ms")
            while True:
                first, last = clock_of(drive.text(), placed)
                if first is not None and last - first >= need_us:
                    break
                drive.service(drive.text())
                time.sleep(0.2)
            drive.inject(IDLE, "stop")
            drive.wait_ticks(3)
            marks.append({"burn_ms": burn, "begin": placed, "end": len(drive.text())})
    except GateFailure as error:
        failure = str(error)
    drive.stop()
    text = drive.text()
    checks, failure, evidence = pace_checks(text, marks, burns, failure)
    return verdict(text, expected, checks, failure, rom=str(rom), marks=marks, **evidence)


def pace_checks(text, marks, burns, failure=None):
    """(checks, failure, **evidence) of the pace gate from the final log and the run's offsets."""
    checks, windows = {"no_fatal": "tr64 fatal:" not in text,
                       "all_runs_reached": len(marks) == len(burns) and failure is None}, []
    for mark in marks:
        burn = mark["burn_ms"]
        window = logs.pace_window(text, mark["begin"], mark["end"])
        tag = f"burn_{burn}ms"
        checks[f"{tag}_window"] = window is not None
        if window is None:
            continue
        windows.append(window)
        # Game time followed the clock: the ticks run equal what 30 Hz owes, with nothing dropped.
        checks[f"{tag}_ticks_follow_clock"] = window["ticks_follow_clock"]
        checks[f"{tag}_nothing_dropped"] = window["dropped"] == 0
        checks[f"{tag}_lara_ran"] = window["moved"]
        # The knob took effect: frames cost at least the burn, so slow runs carry several ticks per frame.
        checks[f"{tag}_frames_slow_enough"] = window["frame_us_mean"] >= burn * 1000
        if burn >= 40:
            checks[f"{tag}_several_ticks_a_frame"] = any(k >= 2 for k in window["ticks_per_frame"])
        audio = window["audio"]
        checks[f"{tag}_audio_reports"] = audio is not None
        if audio:
            checks[f"{tag}_mixer_follows_clock"] = audio["mixer_follows_clock"]
            checks[f"{tag}_ticks_agree_with_audio"] = (audio["game_ticks_us"] is not None and
                                                       abs(audio["game_ticks_us"] - audio["span_us"]) <= PACE_AUDIO_BUFFER_US + 2 * PACE_TICK_US)
            checks[f"{tag}_music_follows_clock"] = audio["music_follows_clock"] is not False
    if len(windows) >= 2:
        tick_travel = windows[0]["path_per_tick"] or 0
        checks["same_distance_within_a_tick"] = logs.pace_runs_agree(windows, tick_travel=tick_travel)
    return checks, failure, {"windows": windows}
