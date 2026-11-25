#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Boot a shipping cart at 4 MiB through ares.sh; leave pictures for human review.

Verdict exit status: 0 pass, 1 failed gate, 2 CLI usage, 3 invalid run (rerun).
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time
from typing import NamedTuple, Optional

import gate_logs
import gate_runs
from drive import QUEUE_LIMIT, ShotPruner, GateFailure, pad_start, pad_write, stop_grace  # noqa: F401  (ShotPruner is re-exported for test_boot)

SOUND_FIELDS = {"ticks", "events", "played", "underruns", "frames", "mixer_us", "mixer_peak_buffer_us"}
SOUND_OUTPUT_FIELDS = {"frames", "underruns", "mixer_total_us", "mixer_peak_pump_us"}
MUSIC_FIELDS = {"starts", "loops", "unavailable", "underruns", "frames", "reads", "bytes", "arena", "mix_us",
                "peak_buffer_us", "frame_peak_us", "startup_peak_us", "codec", "rate", "current", "phase"}
SOUND_ROW = re.compile(r"^(?:music: |audio: |audio-ambient: |audio-output: cart )[^\r\n]*\n", re.M)
NEXT_LOADING = re.compile(r"^play: loading=", re.M)
SOUND_FIELD = re.compile(r"([A-Za-z_][A-Za-z_0-9]*)=(-?[0-9]+)")
SOUND_ROW_TOKEN = re.compile(r"(?<!-)(?:audio:|audio-ambient:|audio-output: cart\b)")
OUTPUT_PREFIX = "audio-output: cart "
EMULATOR_OUTPUT = re.compile(r"\[ares\]|TESTHARNESS ")
INVALID_RUN_EXIT = 3
PLAY_MARKER = re.compile(
    r"^play: (?:ready|loading)=|^play-(?:select|stop): |^play-pace: level ", re.M)
STOP_TICK = re.compile(
    r"play-pace: level us=[0-9]+ tick=([0-9]+) frames=[0-9]+ ticks=[0-9]+ "
    r"hist=[0-9]+/[0-9]+/[0-9]+/[0-9]+ dropped=[0-9]+ restarts=[0-9]+")
SOUND_PACKET = ("music: ", "audio: ", "audio-ambient: ", OUTPUT_PREFIX, "audio-time: ")
# Recognize whole report tokens and visible pieces of torn headers. A
# lone initial letter is ambiguous unless immediately followed by a notice.
_SOUND_HEADERS = tuple(prefix.split(" ", 1)[0] for prefix in SOUND_PACKET)
_SOUND_HEADER_PIECES = {head[:n] for head in _SOUND_HEADERS for n in range(2, len(head) + 1)}
_SOUND_HEADER_PIECES.update(head[n:] for head in _SOUND_HEADERS for n in range(1, len(head) - 1))
SOUND_FRAGMENT = re.compile(
    r"^[ \t]*[am](?=$|\[|[A-Z])|(?<![\w-])(?:"
    + "|".join(map(re.escape, sorted(_SOUND_HEADER_PIECES))) + r")(?![a-z0-9_-])")


def read_log(path):
    # Do not translate a bare CR into LF or conceal undecodable bytes.
    # Route predicates use text offsets; packet verification requires ASCII.
    return path.read_bytes().decode("utf-8", "surrogateescape") if path.exists() else ""


def sound_route(expected, cut_path, terminal):
    """The title exit, three reload commands, cutscene path and terminal request."""
    commands = [None, 1, 3, 2]
    if cut_path:
        commands.extend({0x210000: 3, 0x220000: 2, 0x240000: 5}[int(word.split()[0], 16)]
                        for word, _, _ in cut_path)
        commands.append(None)
    targets = expected[1:]
    if terminal:
        commands.append(1)
        targets = [*targets, expected[-1]]
    return list(zip(expected[:len(targets)], targets, commands, strict=True))


def account_sound(text, stops, terminal_after=None):
    """Bind one LF-terminated guest packet to each scenario stop."""
    samples = []
    last_tick = -1
    frames = pumps = 0
    marks = iter(PLAY_MARKER.finditer(text))

    def line_at(at):
        end = text.find("\n", at)
        return text[at:end] if end != -1 else ""

    def invalid(at, reason):
        row = line_at(at)
        if EMULATOR_OUTPUT.search(row):
            reason = "guest report interleaved with emulator output"
        shown = row if row.isprintable() else repr(row)
        return f"line {text.count(chr(10), 0, at) + 1}: {reason}: rerun: {shown}"

    def take(predicate, reason):
        mark = next(marks, None)
        at = mark.start() if mark else len(text)
        if not predicate(line_at(at)):
            raise ValueError(invalid(at, reason))
        return at

    try:
        take(lambda row: row == "play: loading=title", "expected initial title load")
        for i, (level, target, command) in enumerate(stops):
            terminal = terminal_after is not None and i == len(stops) - 1
            ready = take(lambda row: row == f"play: ready={level} hz=30", f"expected {level} ready")
            if command is None:
                exit_row = re.compile(rf"play-stop: level={level} kind=2 next_level_id=[0-9]+ death_ready=0 name=.+")
                leave = take(exit_row.fullmatch, f"expected {level} transition")
            else:
                leave = take(lambda row: row == f"play-select: from={level} to={target} command={command}",
                             f"expected {level} → {target} command {command}")
            if terminal and leave < terminal_after:
                raise ValueError(invalid(leave, "terminal restart preceded its request"))
            pace = take(STOP_TICK.fullmatch, f"expected {level} stop tick")
            tick = int(STOP_TICK.fullmatch(line_at(pace))[1])
            if tick <= last_tick:
                raise ValueError(invalid(pace, f"non-increasing {level} stop tick"))
            last_tick = tick

            packet, cursor = {}, text.index("\n", pace) + 1
            for prefix in SOUND_PACKET:
                while True:
                    end = text.find("\n", cursor)
                    row = text[cursor:end] if end != -1 else ""
                    # Skip only whole unrelated lines and never move a report
                    # across another load, transition or stop marker.
                    if (end == -1 or row.startswith(prefix) or PLAY_MARKER.match(row)
                            or SOUND_FRAGMENT.search(row)
                            or (row and not row.isprintable() and not row.replace("\t", " ").isprintable())):
                        break
                    cursor = end + 1
                # With surrogateescape a byte that failed UTF-8 decoding has
                # a surrogate; require strict ASCII on the guest wire rows.
                if (not row.isascii() or not row.isprintable() or not row.startswith(prefix)
                        or EMULATOR_OUTPUT.search(row)):
                    raise ValueError(invalid(cursor, f"expected intact {prefix.strip()} row for {level} tick {tick}"))
                packet[prefix] = (cursor, row[len(prefix):])
                cursor += len(row) + 1

            def counters(prefix, required, *, signed_key=None):
                at, fields = packet[prefix]
                result = sound_numbers(fields, signed_key=signed_key)
                if not required <= result.keys():
                    raise ValueError(invalid(at, f"malformed {prefix.strip()} row for {level} tick {tick}"))
                return result

            music = counters("music: ", MUSIC_FIELDS, signed_key="current")
            snd = counters("audio: ", SOUND_FIELDS)
            counters("audio-ambient: ", {"sources", "started", "stopped"})
            output = counters(OUTPUT_PREFIX, SOUND_OUTPUT_FIELDS | {"pumps"})
            counters("audio-time: ", {"active_us"})
            if snd["ticks"] != tick:
                raise ValueError(invalid(packet["audio: "][0], f"sound tick does not match {level} stop tick {tick}"))
            # The output report carries no stop tick. Require fresh
            # cumulative counters so the preceding report cannot stand in.
            if output["frames"] <= frames or output["pumps"] <= pumps:
                raise ValueError(invalid(packet[OUTPUT_PREFIX][0],
                                         f"audio-output counters did not advance for {level} tick {tick}"))
            frames, pumps = output["frames"], output["pumps"]
            samples.append((snd, output, music))
            if terminal:
                # An intact extra cannot be ignored, including on copy-back.
                # Unrecognizable bytes after the terminal packet are outside
                # coverage: the cart may draw the reloaded level before TERM.
                if extra := SOUND_ROW.search(text, cursor):
                    raise ValueError(invalid(extra.start(), "extra audio row after terminal packet"))
                if load := NEXT_LOADING.search(text, cursor):
                    row = line_at(load.start())
                    if row and row != f"play: loading={target}":
                        raise ValueError(invalid(load.start(), f"expected terminal reload of {target}"))
            else:
                loading = take(lambda row: row == f"play: loading={target}", f"expected {target} load")
                if loading < cursor:
                    raise ValueError(invalid(loading, f"{target} loaded before the {level} packet ended"))
            # An intact duplicate before the pace marker is extra as well.
            end = cursor if terminal else loading
            if sum(1 for _ in SOUND_ROW.finditer(text, ready, end)) != 4:
                raise ValueError(invalid(leave, f"expected exactly one audio packet for {level} tick {tick}"))
    except ValueError as error:
        return samples, str(error)
    return samples, None


def sound_numbers(fields, *, signed_key=None):
    """Parse one complete numeric report, rejecting malformed or repeated keys."""
    values = {}
    for field in fields.split():
        match = SOUND_FIELD.fullmatch(field)
        if match is None or match[1] in values or (match[2].startswith("-") and match[1] != signed_key):
            return {}
        values[match[1]] = int(match[2])
    return values


def sound_evidence(text, required_reports):
    reports, output, playing = [], [], False
    invalid_reason = None
    for number, line in enumerate(text.split("\n")[:-1], 1):
        if line.startswith("play: ready=level1"):
            playing = True
        if not playing:
            continue
        marker = SOUND_ROW_TOKEN.search(line)
        if marker is None:
            continue
        if invalid_reason is None and (marker.start() != 0 or EMULATOR_OUTPUT.search(line)):
            invalid_reason = f"line {number}: guest report interleaved with emulator output: rerun: {line}"
        if marker.start() != 0:
            continue
        if marker[0] == "audio:":
            reports.append(sound_numbers(line[len("audio: "):]) if line.startswith("audio: ") else {})
        elif marker[0] == OUTPUT_PREFIX.rstrip():
            output.append(sound_numbers(line[len(OUTPUT_PREFIX):]) if line.startswith(OUTPUT_PREFIX) else {})
    return sound_verdict(reports, output, required_reports, invalid_reason)


def sound_verdict(reports, output, required_reports, invalid_reason, *, music=(), prelude=()):
    valid = bool(len(reports) >= required_reports and len(output) >= required_reports
                 and all(SOUND_FIELDS <= row.keys() for row in reports)
                 and all(SOUND_OUTPUT_FIELDS <= row.keys() for row in output))
    passed = bool(valid and invalid_reason is None and reports[0]["ticks"] >= 70 and reports[0]["events"] > 0
                  and reports[0]["played"] > 0 and reports[0]["frames"] > 0
                  and all(row["underruns"] == 0 for rows in (reports, output, music, prelude) for row in rows)
                  and all(row["frames"] > 0 for row in output))
    return {"pass": passed, "invalid_reason": invalid_reason, "reports": reports, "output_reports": output,
            "mixer_peak_buffer_us": max((row["mixer_peak_buffer_us"] for row in reports
                                         if "mixer_peak_buffer_us" in row), default=None),
            "mixer_peak_pump_us": max((row["mixer_peak_pump_us"] for row in output
                                       if "mixer_peak_pump_us" in row), default=None)}


def accounted_sound_verdict(samples, required_reports, invalid_reason):
    # Keep the gameplay report lists scoped as before. The title's stop
    # packet is still part of the checked run, including its three counters.
    reports, output, music = map(list, zip(*samples[1:])) if len(samples) > 1 else ([], [], [])
    result = sound_verdict(reports, output, required_reports, invalid_reason,
                           music=music, prelude=samples[0] if samples else ())
    result["music_reports"] = music
    return result


def terminal_restart(text, after, level):
    return re.compile(rf"^play-select: from={level} to={level} command=1\n", re.M).search(text, after)


def terminal_reply_seen(text, after, level):
    # A complete trailer acknowledges a normal stop; a new load also
    # acknowledges the restart if corruption hid the trailer. Only the
    # finalized log can supply a verdict.
    restart = terminal_restart(text, after, level)
    return restart is not None and (
        re.compile(r"^audio-time: active_us=[0-9]+\n", re.M).search(text, restart.end()) is not None
        or NEXT_LOADING.search(text, restart.end()) is not None)


class Step(NamedTuple):
    """One injected save step: a press the cart must acknowledge in its log before the gate releases.
    `wrong` is a log line that, once seen, fails the step at once; `repeat` marks the cursor step, which presses
    again (up to MAX_PRESSES) until the cart's logged cursor is the wanted slot."""
    word: str
    label: str
    ack: str
    wrong: Optional[str] = None
    repeat: bool = False


# The save: ring -> options ring -> passport (Save Game) -> picker -> slot 3 -> accept. Words: B=40000000,
# Start=10000000, Up=08000000, Down=04000000. The picker opens with the cursor on the remembered slot
# (0); Up wraps to slot 3 (index 2) and the cart logs `ui: picker index=N` each time the cursor moves.
SAVE_SLOT = 2
SAVE_STEPS = [
    Step("10000000", "Start: open ring for the save", r"state=4 page=5 "),
    Step("04000000", "Down: options ring", r"state=4 page=7 "),
    Step("40000000", "B: select the passport (Save Game)", r"state=9 page=7 passport=1 "),
    Step("40000000", "B: open the slot picker", r"^ui: picker open load=0 valid=000 unusable=000 index=0$"),
    Step("08000000", "Up: cursor to slot 3", rf"^ui: picker index={SAVE_SLOT}$", repeat=True),
    Step("40000000", "B: accept slot 3", rf"^ui: picker accept slot={SAVE_SLOT} load=0$",
         wrong=rf"^ui: picker accept slot=(?!{SAVE_SLOT} )\d+ "),
]
CURSOR_LINE = r"^ui: picker index=(\d+)$"
MAX_PRESSES = 6          # presses of the cursor step before the gate gives up
CURSOR_RETRY_FRAMES = 120  # cart ticks without any cursor line after a press: it was lost, press again
STALL_FRAMES = 300       # cart ticks a step may wait for its acknowledgement (the game runs, the line never comes)
STALL_SECONDS = 1200     # wall seconds a step may wait when the game does not tick (a level load)
# The stages of main()'s loop that wait for the cart to acknowledge an injected press (a log line, a menu state, a
# level, 60 forward ticks, the release seen as an idle frame); any other stage waits only for game ticks.
AWAITING = frozenset({1, 3, 11, 13, 15, 17, 19, 21, 31, 34, 36, 51, 54})


def cursor_step(region, wanted, answered, waited, presses):
    """The cursor step's decision from the log since its first press: ('done'|'again'|'wait'|'fail', answered).
    `answered` is how many cursor lines the gate had already acted on, `waited` the cart ticks since the latest
    press, `presses` the presses so far. The last logged cursor is the cursor: a lost press adds no line, a
    doubled one moves it past the slot, and either is pressed again until it is on the wanted slot."""
    moves = re.findall(CURSOR_LINE, region, re.M)
    if moves and moves[-1] == str(wanted):
        return "done", len(moves)
    if len(moves) > answered or waited >= CURSOR_RETRY_FRAMES:
        return ("fail" if presses >= MAX_PRESSES else "again"), len(moves)
    return "wait", answered


FORWARD_MIN_UNITS = 512  # Lara runs forward 60 ticks; a quarter of that is far above standing still


def pack_video_names(pack):
    """Names of the VIDEO_<NAME> entries in a campaign pack's directory (empty: no videos)."""
    names, tail = set(), b""
    with Path(pack).open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            data = tail + block
            names.update(m.decode() for m in re.findall(rb"VIDEO_([A-Z]+)", data))
            tail = data[-16:]
    return names


def catalog(root):
    """The play loop's level-select order, read from its source (TITLE excluded)."""
    body = re.search(r"levels\[\]=\{(.*?)\};", (root / "engine/in/play.c").read_text(), re.S)[1]
    return re.findall(r'"(\w+)"', body)[:-1]


def cutscene_path(order, target):
    """Shortest L+C-button path from LEVEL1 to `target`: previous/next, optionally via GYM."""
    if target not in order or not target.startswith("cut"):
        raise SystemExit(f"--cutscene {target!r} is not a cutscene of {order}")
    n, goal, start = len(order), order.index(target), order.index("level1")
    right, left, down = "00210000 0 0\n", "00220000 0 0\n", "00240000 0 0\n"
    candidates = [
        [(right, "L+C-right", (start + i + 1) % n) for i in range((goal - start) % n)],
        [(left, "L+C-left", (start - i - 1) % n) for i in range((start - goal) % n)],
        [(down, "L+C-down GYM", 0)] + [(right, "L+C-right", i + 1) for i in range(goal)],
        [(down, "L+C-down GYM", 0)] + [(left, "L+C-left", (n - i - 1) % n) for i in range((0 - goal) % n)],
    ]
    return min(candidates, key=len)

def smoke_film_input(text, now, began_at, pressed, released):
    """Allow time for the asynchronously streamed player to sample released START."""
    for name in gate_runs.FILMS:
        if f"video: name={name} result=" in text:
            if name in pressed and name not in released:
                released.add(name)
                yield f"release {name}", "0 0 0\n"
                return
            continue
        if f"video: begin name={name} " in text and name not in pressed:
            began = began_at.setdefault(name, now)
            if now - began >= 1.0:
                pressed.add(name)
                yield f"Start: skip {name}", "10000000 0 0\n"
            return


def main():
    if sys.argv[1:2] == ["--probe-pack"]:  # ship.sh: the films the pack holds (one per line)
        print("\n".join(sorted(pack_video_names(sys.argv[2]))))
        return 0
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--rom", type=Path, required=True)
    ap.add_argument("--identity-header", type=Path, help="independent cart_identity.h for a relocated base ROM")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("--reload", action="store_true",
                    help="also exercise restart, LEVEL2 and back to LEVEL1")
    film_mode = ap.add_mutually_exclusive_group()
    film_mode.add_argument("--video", action="store_true",
                           help="play ESCAPE and CORE through, skip CAFE and SNOW with START 30 s of wall time in (about 3 s of film: ares runs about 10x slower than real time) (pack must hold videos)")
    film_mode.add_argument("--skip-videos", action="store_true",
                           help="skip boot films with START for a smoke run; not a delivery video verdict")
    ap.add_argument("--video-absent", default="", metavar="NAMES",
                    help="comma-separated films the pack lacks (the PlayStation pack has no ESCAPE); with --video each must log result=absent")
    ap.add_argument("--sound", action="store_true",
                    help="require played events and zero stream/output underruns")
    ap.add_argument("--cutscene", metavar="NAME",
                    help="after the base run: level-select to this cutscene, let it play, "
                         "skip it with START and require the level that follows")
    ap.add_argument("--cutscene-ticks", type=int, default=240,
                    help="game ticks (30 per second) the cutscene must play (before the skip)")
    ap.add_argument("--cutscene-natural", action="store_true",
                    help="do not skip: the cutscene must end by itself into the next level")
    ap.add_argument("--save-load", action="store_true",
                    help="save into slot 3 through the picker, power-cycle (SRAM export/import of the private "
                         "ares) and load it back: the loaded Lara must be the saved one (needs --ares)")
    ap.add_argument("--ares", type=Path, default=Path(os.environ.get(
        "ARES_BINARY", "/tmp/tr64/builds/MenuUI/ares-sram2/ares")),
                    help="private ares with SRAM import/export (tools/ares/README.md)")
    ap.add_argument("--gate-rom", type=Path,
                    help="the gate ROM (same sources plus tools/ship/gate-place.c): death page and "
                         "natural level end, with START held into the next level")
    ap.add_argument("--gate-identity-header", type=Path, help="independent cart_identity.h for a relocated gate ROM")
    ap.add_argument("--pace-burn", default="", metavar="MS,MS",
                    help="with --gate-rom: the pace gate. Lara runs a circle for ten seconds of the hardware clock "
                         "once for each burn (0, 20, 40 or 60 ms of clock burned after every drawn frame, set through "
                         "the gate ROM's L+A knob); game ticks, Lara's distance and the mixer's output must follow the "
                         "clock in each, and the runs must agree to within a tick's travel")
    ap.add_argument("--pace-timeout", type=int, default=3600,
                    help="seconds for the pace run (each burn needs about 15 s of emulated clock; ares runs at a "
                         "small fraction of real time on a loaded host)")
    ap.add_argument("--pack", type=Path, required=True, help="the campaign pack the ROM is meant to carry")
    ap.add_argument("--secondary-timeout", type=int, default=1500,
                    help="seconds for each of the power-cycle and gate-ROM runs")
    args = ap.parse_args()
    burns = [int(n) for n in args.pace_burn.split(",") if n]
    if burns and (not args.gate_rom or any(b % 20 or not 0 <= b <= 60 for b in burns) or 40 not in burns):
        ap.error("--pace-burn needs --gate-rom and burns of 0, 20, 40 or 60 that include the 40 ms case")
    if args.sound and not args.reload:
        ap.error("--sound requires --reload to collect level-stop sound reports")
    absent = {n for n in args.video_absent.split(",") if n}
    if absent - {"ESCAPE", "CORE", "CAFE", "SNOW"}:
        ap.error("--video-absent names a film the gate does not play")
    if absent and not args.video:
        ap.error("--video-absent requires --video")
    if args.gate_identity_header and not args.gate_rom:
        ap.error("--gate-identity-header requires --gate-rom")
    try:
        expected_identity = gate_logs.expected_identity(args.rom, args.pack, args.identity_header)
        gate_identity = (*gate_logs.guest_identity.expected_identity(
            args.gate_rom.parent, args.gate_identity_header, option="--gate-identity-header"),
            expected_identity[-1]) if args.gate_rom else None
    except (OSError, ValueError) as error:
        ap.error(str(error))
    pack_videos = pack_video_names(args.pack)
    if pack_videos and not (args.video or args.skip_videos):
        ap.error("the pack holds videos: use --video for delivery or --skip-videos for smoke")
    if args.video:
        expected_films = ["ESCAPE", "CORE", "CAFE", "SNOW"]
        if absent != {n for n in expected_films if n not in pack_videos}:
            ap.error(f"--video-absent {sorted(absent)} does not match the films the pack lacks "
                     f"{sorted(n for n in expected_films if n not in pack_videos)}")
    if args.save_load or args.gate_rom:
        if not args.ares.exists():
            ap.error(f"private ares with SRAM import/export not found: {args.ares}")
    root = Path(__file__).resolve().parents[2]
    cut_path, cut_next = [], None
    if args.cutscene:
        order = catalog(root)
        steps = cutscene_path(order, args.cutscene)
        cut_path = [(word, label, order[index]) for word, label, index in steps]
        cut_next = order[order.index(args.cutscene) + 1]
    rom, out = args.rom.resolve(), args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    log, pad, vi = out / "ares.log", out / "pad.in", out / "vi"
    # Never let a prior run supply images/logs to the verdict.
    if log.exists() or vi.exists() or (out / "summary.json").exists():
        ap.error("output already contains a run; choose a fresh --out")
    vi.mkdir()
    pad_start(pad)
    env = os.environ.copy()
    env.update(ARES_EXPANSION_PAK="false", ARES_TEST_INPUT=str(pad),
               ARES_HOST_SCREENSHOT="1", ARES_HOST_SHOT_EVERY_MS="1000",
               ARES_HOST_SHOT_DIR=str(vi), ARES_HOST_SHOT_MAX="20000",
               TR64_SHOT_PRUNE="1")
    erased, saved_srm = out / "erased.srm", out / "saved.srm"
    if args.save_load or args.gate_rom:
        erased.write_bytes(b"\xff" * 32768)  # a deterministic empty SRAM for every run
        env["ARES_BINARY"] = str(args.ares)
    if args.save_load:
        env.update(ARES_SRAM_IN=str(erased), ARES_SRAM_OUT=str(saved_srm))
    # RTK's wrapper does not forward TERM. Own the real harness process so its
    # trap completes child/slot cleanup before final logs/verdict are consumed.
    command = ["bash", str(root / "test/run/ares.sh"), str(out),
               str(rom), str(log), str(args.timeout)]
    proc = subprocess.Popen(command, env=env, start_new_session=True)
    def terminate_requested(signum, _frame):
        raise SystemExit(128 + signum)
    signal.signal(signal.SIGTERM, terminate_requested)
    finished, stage, wait_tick, base = False, 0, 0, None
    terminal_level = cut_next or "level1"
    save_i, step_mark, stage_seen, stage_at, stage_frames, stalled = 0, 0, 0, time.monotonic(), 0, None
    presses, answered, press_frames, rel_mark, last_label = 0, 0, 0, None, ""
    skipped, released, began_at, held_text = set(), set(), {}, {}
    # The deadline runs from the moment a slot is taken (ares.sh writes slot.txt), not from the queue.
    queued_until, deadline = time.monotonic() + QUEUE_LIMIT, None
    text = ""
    pruner = ShotPruner()
    def inject(value, tick, label):
        nonlocal rel_mark, last_label
        pad_write(pad, value)
        # After a release the next press waits until the cart has ticked with nothing held: a press
        # that follows its release too closely (or reaches the cart in the same burst) is no edge.
        rel_mark, last_label = (len(text) if value == "0 0 0\n" else None), label
        with (out / "input.log").open("a") as stream:
            stream.write(f"after_tick={tick} {label} pad={value}")
    # Pad buttons use ares' word: B=40000000, Start=10000000, L=00200000.
    # Stages: title passport -> new game -> forward run -> ring open/close ->
    # L+Start restart -> L+C-right LEVEL2 -> L+C-left LEVEL1.
    try:
        while proc.poll() is None:
            text = read_log(log)
            pruner.run(text)
            frames = re.findall(r"play-frame: tick=(\d+) input=([0-9a-f]+)", text)
            tick = int(frames[-1][0]) if frames else 0
            levels = re.findall(r"play: ready=(\w+) hz=", text)
            menus = re.findall(r"ui: menu frame=\d+ state=(\d+) page=(\d+)", text)
            def press(value, label, nxt, delay=4, mark=True):
                nonlocal stage, wait_tick, step_mark, press_frames
                inject(value, tick, label)
                stage, wait_tick, press_frames = nxt, tick + delay, len(frames)
                if mark:
                    step_mark = len(text)
            def due(delay=0):
                return tick >= wait_tick + delay and (
                    rel_mark is None or re.search(r"^play-frame: tick=\d+ input=0000 ", text[rel_mark:], re.M) is not None)
            if args.video:
                for name in ("CAFE", "SNOW"):
                    began = f"video: begin name={name} " in text
                    ended = f"video: name={name} result=" in text
                    if began and name not in began_at:
                        began_at[name] = time.monotonic()
                        # A START still held from skipping a cutscene is no edge for the film.
                        inject("0 0 0\n", tick, f"release before {name}")
                    # Mid-play: some pictures are decoded and shown before START arrives.
                    elif began and not ended and name not in skipped and time.monotonic() - began_at[name] >= 30.0:
                        inject("10000000 0 0\n", tick, f"Start: skip {name}"); skipped.add(name)
                    # START stays held after SNOW until LEVEL1 has ticked: the level must
                    # not see it as SELECT (ring) or as an edge.
                    elif ended and name in skipped and name not in released and (
                            name != "SNOW" or (base is not None and tick >= base + 5)):
                        held_text[name] = text.split(f"video: name={name} result=", 1)[1]
                        inject("0 0 0\n", tick, f"release {name}"); released.add(name)
            elif args.skip_videos:
                for name, value in smoke_film_input(text, time.monotonic(), began_at, skipped, released):
                    inject(value, tick, name)
            if levels and levels[0] == "title" and base is None and levels.count("level1") == 0:
                # Each press waits for the cart's own log line (the menu state it causes) before it is released.
                if stage == 0 and tick >= 40:
                    press("40000000 0 0\n", "B: open title passport", 1, 4)
                elif stage == 1 and due() and re.search(r"^ui: menu frame=\d+ state=9 page=0 ", text[step_mark:], re.M):
                    inject("0 0 0\n", tick, "release"); stage, wait_tick = 2, tick + 5
                elif stage == 2 and due():
                    press("40000000 0 0\n", "B: new game", 3, 4)
                elif stage == 3 and due() and re.search(r"^ui: menu frame=\d+ state=8 page=0 ", text[step_mark:], re.M):
                    inject("0 0 0\n", tick, "release"); stage = 4
            if base is None and "level1" in levels:
                base, stage, wait_tick = tick, 10, tick
            if base is not None:
                at = tick - base
                if stage == 10 and at >= 10:
                    inject("0 0 80\n", tick, "movement"); stage, step_mark = 11, len(text)
                # Held until the cart's own frames show 60 ticks of forward input.
                elif stage == 11 and sum(1 for f in re.findall(r"^play-frame: tick=\d+ input=([0-9a-f]+)", text[step_mark:], re.M)
                                         if int(f, 16) & 8) >= 60:
                    inject("0 0 0\n", tick, "movement stop"); stage, wait_tick = 12, tick + 25
                elif stage == 12 and due():
                    press("10000000 0 0\n", "Start: open ring", 13, 5)
                elif stage == 13 and due() and any(m == ("4", "5") for m in menus):
                    inject("0 0 0\n", tick, "release"); stage, wait_tick = 14, tick + 40
                elif stage == 14 and due():
                    press("10000000 0 0\n", "Start: close ring", 15, 5)
                elif stage == 15 and due() and menus and menus[-1][0] == "0":
                    inject("0 0 0\n", tick, "release"); stage, wait_tick = (50 if args.save_load else 16), tick + 40
                # The save: ring -> options ring -> passport (Save Game) -> picker -> slot 3 -> accept.
                # Each step presses, waits for the cart's own log line, releases. The cursor step presses
                # again until the cart's logged cursor is the wanted slot (cursor_step).
                elif stage == 50 and due():
                    presses, answered = 1, 0
                    press(SAVE_STEPS[save_i].word + " 0 0\n", SAVE_STEPS[save_i].label, 51, 5)
                elif stage == 51 and due():
                    step = SAVE_STEPS[save_i]
                    region = text[step_mark:]
                    wrong = step.wrong and re.search(step.wrong, region, re.M)
                    action = "wait"
                    if wrong:
                        stalled = f"{step.label}: the cart logged {wrong[0]!r}"
                    elif step.repeat:
                        action, answered = cursor_step(region, SAVE_SLOT, answered, len(frames) - press_frames, presses)
                    elif re.search(step.ack, region, re.M):
                        action = "done"
                    if action == "fail":
                        stalled = (f"{step.label}: the cart's cursor is not on slot {SAVE_SLOT + 1} after {presses} presses "
                                   f"(logged {re.findall(CURSOR_LINE, region, re.M)})")
                    if stalled:
                        os.killpg(proc.pid, signal.SIGTERM)
                        break
                    if action == "again":
                        inject("0 0 0\n", tick, "release"); stage, wait_tick, presses = 54, tick + 5, presses + 1
                    elif action == "done":
                        inject("0 0 0\n", tick, "release"); save_i += 1
                        stage, wait_tick = (50, tick + 30) if save_i < len(SAVE_STEPS) else (52, tick + 5)
                elif stage == 54 and due():
                    press(SAVE_STEPS[save_i].word + " 0 0\n", SAVE_STEPS[save_i].label + " (again)", 51, 5, mark=False)
                elif stage == 52 and due() and re.search(r"^play-lara: saved ", text, re.M) and menus and menus[-1][0] == "0":
                    stage, wait_tick = 16, tick + 40
                else:
                    n = len(levels)
                    if args.reload and stage == 16 and due():
                        inject("10200000 0 0\n", tick, "L+Start restart"); stage = 17
                    elif args.reload and stage == 17 and n >= 3:
                        inject("0 0 0\n", tick, "release restart"); stage, wait_tick = 18, tick + 30
                    elif args.reload and stage == 18 and due():
                        inject("00210000 0 0\n", tick, "L+C-right LEVEL2"); stage = 19
                    elif args.reload and stage == 19 and n >= 4:
                        inject("0 0 0\n", tick, "release next"); stage, wait_tick = 20, tick + 30
                    elif args.reload and stage == 20 and due():
                        inject("00220000 0 0\n", tick, "L+C-left LEVEL1"); stage = 21
                    elif args.reload and stage == 21 and n >= 5:
                        inject("0 0 0\n", tick, "release previous"); stage, wait_tick = 22, tick + 40
                    elif (stage == 22 or (not args.reload and stage == 16)) and due():
                        if args.cutscene:
                            stage, step_index, entry_levels = 30, 0, n
                        elif args.sound:
                            press("10200000 0 0\n", "L+Start: sample terminal audio", 36)
                        else:
                            finished = True
                            os.killpg(proc.pid, signal.SIGTERM)
                            break
                    # Level select to the cutscene, one L+C-button step per level.
                    elif stage == 30:
                        word, label, target = cut_path[step_index]
                        inject(word, tick, f"{label} -> {target}"); stage = 31
                    elif stage == 31 and levels and levels[-1] == cut_path[step_index][2] and len(levels) > entry_levels:
                        inject("0 0 0\n", tick, "release select"); stage, wait_tick = 32, tick + 30
                        entry_levels = len(levels)
                    elif stage == 32 and due():
                        step_index += 1
                        stage = 30 if step_index < len(cut_path) else 33
                        if stage == 33:
                            cut_tick = tick
                    # In the cutscene: it plays untouched, then START skips it
                    # (or, with --cutscene-natural, its frame table runs out).
                    elif stage == 33 and args.cutscene_natural:
                        stage = 34
                    elif stage == 33 and tick >= cut_tick + args.cutscene_ticks:
                        inject("10000000 0 0\n", tick, "START: skip cutscene"); stage = 34
                    elif stage == 34 and len(levels) > entry_levels and levels[-1] == cut_next:
                        inject("0 0 0\n", tick, "release skip"); stage, wait_tick = 35, tick + 45
                    elif stage == 35 and due():
                        if args.sound:
                            press("10200000 0 0\n", "L+Start: sample terminal audio", 36)
                        else:
                            finished = True
                            os.killpg(proc.pid, signal.SIGTERM)
                            break
                    elif stage == 36 and terminal_reply_seen(text, step_mark, terminal_level):
                        finished = True
                        os.killpg(proc.pid, signal.SIGTERM)
                        break
            if stage != stage_seen:
                stage_seen, stage_at, stage_frames = stage, time.monotonic(), len(frames)
            elif stage in AWAITING and (len(frames) - stage_frames > STALL_FRAMES or time.monotonic() - stage_at > STALL_SECONDS):
                stalled = (f"{last_label}: no acknowledgement from the cart after {len(frames) - stage_frames} ticks, "
                           f"{time.monotonic() - stage_at:.0f} s (stage {stage})")
                os.killpg(proc.pid, signal.SIGTERM)
                break
            if deadline is None and (out / "slot.txt").exists():
                deadline = time.monotonic() + args.timeout + 120
            if "tr64 fatal:" in text or time.monotonic() > (deadline or queued_until):
                os.killpg(proc.pid, signal.SIGTERM)
                break
            time.sleep(0.1)
        launcher_exit = proc.wait(timeout=stop_grace(15))
    finally:
        if proc.poll() is None:
            os.killpg(proc.pid, signal.SIGTERM)
            proc.wait(timeout=stop_grace(15))
    text = read_log(log)
    pruner.run(text)
    frames = re.findall(r"play-frame: tick=(\d+) input=([0-9a-f]+) tick_us=(\d+)", text)
    render = re.findall(r"game-render: room=(\d+) eye=(-?\d+),(-?\d+),(-?\d+).*done_us=(\d+)", text)
    fatal = re.findall(r"tr64 fatal:[^\r\n]*", text)
    shots = sorted(str(p) for p in vi.glob("*.ppm"))
    forward = [f for f in frames if int(f[1], 16) & 8]
    # 'Moved' is Lara's displacement (her logged state, every tenth tick), not the camera's:
    # the held-forward window is [base+10, base+70] play ticks after LEVEL1 is ready.
    lara = gate_logs.lara_samples(text)
    level1 = re.search(r"^play: ready=level1 ", text, re.M)
    walk = gate_logs.displacement([x for x in lara if level1 and x["offset"] > level1.start()],
                                  (base or 0) + 20, (base or 0) + 80)
    moved = walk is not None and walk >= FORWARD_MIN_UNITS
    identity = gate_logs.shipping_identity(text, *expected_identity)
    # The restart only asks for a report; an already-loaded successor is not
    # part of the route, even if it appeared while the runner was stopping.
    restart = terminal_restart(text, step_mark, terminal_level) if stage == 36 else None
    levels = re.findall(r"^play: ready=(\w+) hz=", text if restart is None else text[:restart.start()], re.M)
    expected = ["title", "level1", "level1", "level2", "level1"] if args.reload else ["title", "level1"]
    results = dict(re.findall(r"video: name=(\w+) result=(\w+) ", text))
    shown = dict(re.findall(r"video: name=(\w+) result=\w+ pictures=\d+ shown=(\d+) ", text))
    # PRISON plays before CUT4 on a campaign advance from LEVEL10B; the gate reaches CUT4 by level select, so it is not expected.
    # A pack cooked from the PlayStation disc has no ESCAPE (the PC logo film): --video-absent
    # names the films that pack lacks; each must be logged absent and no other may be.
    video_names = [n for n in ["ESCAPE", "CORE", "CAFE", "SNOW"]
                   if n not in absent]
    absent_logged = set(re.findall(r"video: name=(\w+) result=absent", text))
    video_ok = (not args.video) or (
        [results.get(n) for n in video_names] == ["done" if n in ("ESCAPE", "CORE") else "skipped" for n in video_names]
        and all(int(shown.get(n, 0)) >= 10 for n in video_names if n in ("CAFE", "SNOW"))
        and not re.search(r"ui: menu frame=\d+ state=[1-9]", held_text.get("SNOW", "ui: menu frame=0 state=1"))
        and absent_logged == absent and re.search(r"play: start_hash=level1 [0-9a-f]{8}", text) is not None)
    if args.skip_videos:
        video_ok = all(results.get(n) == "skipped" for n in gate_runs.FILMS if n in pack_videos)
    cutscene = None
    if args.cutscene:
        expected += [step[2] for step in cut_path] + [cut_next]
        entered = levels.index(args.cutscene, 4) if args.cutscene in levels[4:] else None
        skip_logged = re.search(rf"play-skip: level={args.cutscene}\b", text) is not None
        table_end = re.search(rf"play-stop: level={args.cutscene} kind=2 next_level_id=\d+", text) is not None
        # Only the cutscene's own frames: no ring, passport or save page may draw.
        window = text[text.index(f"play: ready={args.cutscene} hz="):] if entered is not None else ""
        window = window[:window.index(f"play: ready={cut_next} hz=")] if f"play: ready={cut_next} hz=" in window else window
        cut_frames = [int(t) for t in re.findall(r"play-frame: tick=(\d+)", window)]
        ring_in_cutscene = re.findall(r"ui: menu frame=\d+ state=[1-9]", window)
        loading = re.search(rf"play: loading={args.cutscene}\b.*?play: ready={args.cutscene} hz=", text, re.S)
        pools = re.findall(r"^(?:pool: (?:code|level|state) |code: frames)[^\n]*$",
                           loading[0] if loading else "", re.M)
        cutscene = {"name": args.cutscene, "next": cut_next, "entered": entered is not None,
                    "ended_by": ("skip" if skip_logged else "table" if table_end else None),
                    "skip_logged": skip_logged, "played_ticks": len(cut_frames),
                    "menu_frames_while_playing": len(ring_in_cutscene),
                    "load_log_pool_lines": pools[-4:]}
    if args.sound and stage == 36:
        stops = sound_route(expected, cut_path, terminal=True)
        samples, invalid_run = account_sound(text, stops, step_mark)
        sound = accounted_sound_verdict(samples, len(stops) - 1, invalid_run)
        sound["terminal_level"] = terminal_level if invalid_run is None else None
    else:
        sound = sound_evidence(text, 3 if args.reload else 1)
        invalid_run = sound["invalid_reason"] if args.sound else None
        sound["terminal_level"] = None
        if args.sound:
            sound["pass"] = False
    if invalid_run:
        print(f"INVALID RUN: {invalid_run}", file=sys.stderr)
    # Both launchers return 128+TERM only for the requested stop. In the
    # remote launcher, failed result copy-back returns 255 instead.
    base_ok = bool(finished and launcher_exit == 128 + signal.SIGTERM
                   and not fatal and not invalid_run and stalled is None and identity["identity_failure"] is None)
    save_load = death_end = None
    if args.save_load:
        saved_line = re.search(rf"^save: slot={SAVE_SLOT} bytes=\d+ status=0 serialize_us=(\d+) write_us=(\d+)$", text, re.M)
        picker = {"picker_opened_empty": "ui: picker open load=0 valid=000 unusable=000 index=0" in text,
                  "picker_accepted_slot_3": f"ui: picker accept slot={SAVE_SLOT} load=0" in text,
                  "saved_to_slot_3": saved_line is not None,
                  "saved_lara_logged": any(x["kind"] == "saved" for x in lara),
                  "sram_exported": "TESTHARNESS SRAM out" in text and saved_srm.exists() and
                                   saved_srm.stat().st_size == 32768 and set(saved_srm.read_bytes()) != {0xFF}}
        save_load = {"base_run": picker, "stalled": stalled,
                     "save_us": {"serialize": int(saved_line[1]), "write": int(saved_line[2])} if saved_line else None}
        if base_ok and all(picker.values()):
            save_load.update(gate_runs.power_cycle(root, rom, out / "power-cycle", expected=expected_identity,
                                                   sram=saved_srm, saved_text=text,
                                                   ares=args.ares, timeout=args.secondary_timeout))
        else:
            save_load.update({"pass": False, "failure": "not run: the base run did not save"})
        save_load["pass"] = bool(save_load["pass"] and all(picker.values()))
    if args.gate_rom and base_ok:
        death_end = gate_runs.death_end(root, args.gate_rom.resolve(), out / "gate", expected=gate_identity, sram=erased,
                                        ares=args.ares, timeout=args.secondary_timeout)
        death_end["rom_md5"] = hashlib.md5(args.gate_rom.read_bytes()).hexdigest()
    elif args.gate_rom:
        death_end = {"pass": False, "failure": "not run: the base run failed"}
    pace = None
    if burns and base_ok:
        pace = gate_runs.pace_run(root, args.gate_rom.resolve(), out / "pace", expected=gate_identity, sram=erased,
                                  ares=args.ares, timeout=args.pace_timeout, burns=burns)
        pace["rom_md5"] = hashlib.md5(args.gate_rom.read_bytes()).hexdigest()
    elif burns:
        pace = {"pass": False, "failure": "not run: the base run failed"}
    passed = bool(base_ok and "tr64 boot ok" in text
                  and "play: ready=title" in text and "play: ready=level1" in text
                  and "hardware_rdram=4194304" in text
                  and "TESTHARNESS inject" in text and forward and moved and len(shots) >= 2
                  and re.search(r"ui: menu frame=\d+ state=4 page=5", text) is not None
                  and levels == expected
                  and video_ok and (not args.sound or sound["pass"])
                  and (cutscene is None or (cutscene["entered"] and cutscene["ended_by"] == (
                           "table" if args.cutscene_natural else "skip")
                       and cutscene["played_ticks"] >= args.cutscene_ticks
                       and cutscene["menu_frames_while_playing"] == 0))
                  and stalled is None and (save_load is None or save_load["pass"])
                  and (death_end is None or death_end["pass"]) and (pace is None or pace["pass"]))
    worst = max(enumerate(render), key=lambda pair: int(pair[1][4])) if render else None
    summary = {"pass": passed, "rom": str(rom),
               "rom_md5": hashlib.md5(rom.read_bytes()).hexdigest(),
               **identity,
               "expansion_pak": False, "frames": len(frames), "fatal": fatal, "launcher_exit": launcher_exit,
               "invalid_run": invalid_run,
               "levels_loaded": levels, "reload_exercised": args.reload,
               "sound_exercised": args.sound, "videos": results, "video_gate": args.video and video_ok, "sound": sound,
               "ring_opened_closed": bool(re.search(r"ui: menu frame=\d+ state=4 page=5", text) and re.search(r"ui: menu frame=\d+ state=0 page=0", text)),
               "injected_forward_frames": len(forward), "forward_displaced": moved,
               "forward_lara_units": walk,
               "pack": str(args.pack), "pack_videos": sorted(pack_videos),
               "stalled": stalled, "save_load": save_load, "death_end": death_end, "pace": pace,
               "pictures": shots, "visual_review_required": True,
               "worst_render_done_us": int(worst[1][4]) if worst else None,
               "worst_render_index": worst[0] if worst else None,
               "worst_render_room_eye": worst[1][:4] if worst else None,
               "worst_tick_us": max((int(f[2]) for f in frames), default=None),
               "cutscene": cutscene}
    (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))
    if invalid_run:
        return INVALID_RUN_EXIT
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
