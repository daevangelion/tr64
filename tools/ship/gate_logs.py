# SPDX-License-Identifier: MIT
"""Predicates and identity evidence for a shipping cart's ares log.

Every function takes the log text (and an offset to look after) and returns
evidence; the callers decide pass/fail from it, and test_boot.py runs each
against synthetic logs, positive and negative.

Lines read (all written by the production play loop and front end):
  play-lara: <what> tick=<n> us=<clock> {"schema":2,"tick":..,"lara":{...,"pos":[x,y,z],...
      Lara's state, every tenth tick and after a save ("saved") or a load ("restored")
  play-pace: <level|tick> us=<clock> tick=<n> frames=<f> ticks=<t> hist=<a>/<b>/<c>/<d> dropped=<n> restarts=<n>
      the play loop's tick accumulator for this level (engine/in/pace.h): frames that ran 1..4 ticks, ticks
      dropped past the catch-up bound; us is the hardware clock at the frame's start; every 30 ticks and at
      the level's end
  play-frame: tick=<n> input=<hex> tick_us=<us> ticks=<k>   one per drawn frame (k ticks ran before it)
  gate-run: tick=<n> us=<frame start> path=<units> burn_ms=<n>      gate ROM, after L+B: Lara's path so far
  gate-audio: us=<clock>, then the audio-output/music report lines     gate ROM: the mixer's frames against the clock
  ui: menu frame=<n> state=<s> page=<p> passport=<q> item=<i>   when the ring changes
  ui: picker open load=<0|1> valid=<abc> ...   /   ui: picker index=<n> (the cursor moved)   /   ui: picker accept slot=<n> load=<0|1>
  save: slot=<n> bytes=<n> status=<0> serialize_us=<n> write_us=<n>   a completed SRAM save and its cost
  play-stop: level=<name> kind=<k> next_level_id=<n> ...       kind 2 = a level ends
  play: ready=<name> hz=30                     a level is loaded and ticking
  gate: place <death|end> ok=<0|1>             gate ROM only (tools/ship/gate-place.c)
"""
import hashlib
import importlib.util
import math
from pathlib import Path
import re

spec = importlib.util.spec_from_file_location(
    "guest_identity", Path(__file__).resolve().parents[2] / "test/run/guest_identity.py")
guest_identity = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guest_identity)


def expected_identity(rom, pack, header=None):
    """Read an independent build header and hash the intended pack, not the ROM."""
    identity = guest_identity.expected_identity(rom.parent, header)
    with pack.open("rb") as stream:
        return (*identity, hashlib.file_digest(stream, "sha256").hexdigest())


def shipping_identity(text, expected_build, expected_config, expected_pack):
    """Evidence from one boot; every pack bind in it must name the intended pack."""
    (build_id, config_id), build_failure = guest_identity.build_identity(
        text, (expected_build, expected_config), play=True)
    records = guest_identity.records(text, "level-pack: sha256=")
    packs = [re.fullmatch(rf"level-pack: sha256=({guest_identity.SHA256})\n", line) for line in records]
    actual_pack = packs[-1][1] if packs and packs[-1] else None
    wrong = next((m[1] for m in packs if m and m[1] != expected_pack), None)
    if not packs:
        pack_failure = f"missing guest pack SHA-256 (expected {expected_pack})"
    elif not all(packs):
        pack_failure = "malformed or incomplete guest pack SHA-256 record"
    elif wrong is not None:
        pack_failure = f"guest pack SHA-256 mismatch: actual={wrong} expected={expected_pack}"
    else:
        pack_failure = None
    return {"build_id": build_id, "config_id": config_id, "pack_sha256": actual_pack,
            "identity_failure": "; ".join(reason for reason in (build_failure, pack_failure) if reason) or None}


# Ring states and pages (include/tr64/game.h TR64_GAME_MENU_*).
STATE_SHOW, PAGE_DEATH, PAGE_STATS, PAGE_MAIN, PAGE_OPTIONS = 9, 1, 3, 5, 7
LARA = re.compile(r"^play-lara: (\w+) tick=(\d+) (?:us=(\d+) )?(\{.*)$", re.M)
RUN = re.compile(r"^gate-run: tick=(\d+) us=(\d+) path=(\d+) burn_ms=(\d+)$", re.M)
PACE = re.compile(r"^play-pace: (\w+) us=(\d+) tick=(\d+) frames=(\d+) ticks=(\d+) hist=(\d+)/(\d+)/(\d+)/(\d+) "
                  r"dropped=(\d+) restarts=(\d+)$", re.M)
MENU = re.compile(r"^ui: menu frame=(\d+) state=(\d+) page=(\d+) passport=(\d+) item=(\d+)", re.M)
POS = re.compile(r'"lara":\{[^{}]*?"pos":\[(-?\d+),(-?\d+),(-?\d+)\]')
FIELDS = {name: re.compile(rf'"lara":\{{[^{{}}]*?"{name}":(-?\d+)') for name in ("room", "anim", "frame", "state", "health")}
ROT = re.compile(r'"lara":\{[^{}]*?"rot":\[(-?\d+),(-?\d+),(-?\d+)\]')


def lara_samples(text, after=0):
    """Lara's logged states in order: kind, play tick, offset, and the fields of the record head
    (None when the record has none: the title)."""
    found = []
    for match in LARA.finditer(text, after):
        head = match[4]
        pos, rot = POS.search(head), ROT.search(head)
        sample = {"kind": match[1], "tick": int(match[2]), "us": int(match[3]) if match[3] else None, "offset": match.start(),
                  "pos": tuple(map(int, pos.groups())) if pos else None,
                  "rot": tuple(map(int, rot.groups())) if rot else None}
        for name, pattern in FIELDS.items():
            field = pattern.search(head)
            sample[name] = int(field[1]) if field else None
        found.append(sample)
    return found


def same_lara(a, b):
    """The loaded Lara is the saved one: position, rotation, room and health."""
    return all(x is not None and x == y for x, y in
               ((a["pos"], b["pos"]), (a["rot"], b["rot"]), (a["room"], b["room"]), (a["health"], b["health"])))


def horizontal(a, b):
    return math.hypot(a[0] - b[0], a[2] - b[2])


def displacement(samples, low, high):
    """Lara's horizontal travel between the first and last sample whose tick lies in [low, high]
    (None: fewer than two samples there)."""
    inside = [s for s in samples if s["pos"] and low <= s["tick"] <= high]
    return horizontal(inside[0]["pos"], inside[-1]["pos"]) if len(inside) >= 2 else None


def menu_events(text, after=0):
    return [{"state": int(m[2]), "page": int(m[3]), "passport": int(m[4]), "offset": m.start()}
            for m in MENU.finditer(text, after)]


def ring_open_after(text, after):
    """Ring/passport states (anything but closed) logged after `after`."""
    return [e for e in menu_events(text, after) if e["state"] != 0]


def levels_ready(text):
    return re.findall(r"^play: ready=(\w+) hz=", text, re.M)


def find(text, pattern, after=0):
    """End offset of the first match at or after `after`, None when absent."""
    match = re.compile(pattern, re.M).search(text, after)
    return match.end() if match else None


def saved_and_restored(text_saved, text_loaded):
    """(saved sample, restored sample): the last 'saved' of the first run, the first 'restored' of the second."""
    saved = [s for s in lara_samples(text_saved) if s["kind"] == "saved"]
    restored = [s for s in lara_samples(text_loaded) if s["kind"] == "restored"]
    return (saved[-1] if saved else None), (restored[0] if restored else None)


def death_flow(text, placed_at):
    """Evidence for the death page from the placement on: alive before, dead after, page shown."""
    samples = lara_samples(text)
    before = [s for s in samples if s["offset"] < placed_at and s["health"] is not None]
    after = [s for s in samples if s["offset"] >= placed_at and s["health"] is not None]
    shown = [e for e in menu_events(text, placed_at)
             if e["state"] == STATE_SHOW and e["page"] == PAGE_DEATH and e["passport"] == 1]
    return {"alive_before": bool(before and before[-1]["health"] > 0),
            "dead_after": any(s["health"] <= 0 for s in after),
            "page_shown": bool(shown), "shown_at": shown[0]["offset"] if shown else None}


def waited(text, shown_at, ticks):
    """The page waited: at least `ticks` game ticks were played after it was shown and neither a stop,
    a load nor any change of the ring was logged. Returns (waited_ticks, interrupted)."""
    window = text[shown_at:]
    played = [int(t) for t in re.findall(r"^play-frame: tick=(\d+)", window, re.M)]
    interrupted = bool(re.search(r"^play-stop: |^play: loading=|^play: ready=", window, re.M) or menu_events(window))
    return (played[-1] - played[0] + 1 if played else 0), interrupted


def stats_flow(text, placed_at):
    """After the end placement: the statistics page appears; returns its offset (None when it never does)."""
    shown = [e for e in menu_events(text, placed_at) if e["page"] == PAGE_STATS]
    return shown[0]["offset"] if shown else None


TICK_US = 1_000_000 / 30


def pace_window(text, after=0, until=None, *, settle_us=1_000_000, window_us=10_000_000, buffer_us=70_000):
    """The pace gate's evidence for one run (gate ROM log from `after` to `until`): over the first window of
    at least `window_us` of the hardware clock, starting `settle_us` after Lara first moved, between two
    frame starts a and b:
      ticks      ticks run (b.tick - a.tick), which must be what the clock owes: 30 a second, plus the ticks the
                 loop reported dropping in between (0 expected), to within one tick;
      travel     Lara's path over the window and its value scaled to exactly the window's length: the same at any
                 frame cost to within a tick's travel (checked across runs);
      histogram  frames by ticks run, from the gate-run lines;
      audio      the mixer's output frames over the same stretch of the clock (within one buffer queue), and
                 the music stream's when it plays.
    None when the log holds no such window."""
    chunk = text[after:until]
    runs = [tuple(map(int, m.groups())) for m in RUN.finditer(chunk)]
    moving = next((r for r in runs if r[2] > 0), None)
    if not moving:
        return None
    a = next((r for r in runs if r[1] - moving[1] >= settle_us), None)
    b = next((r for r in runs if a and r[1] - a[1] >= window_us), None)
    if not (a and b):
        return None
    inside = [r for r in runs if a[1] <= r[1] <= b[1]]
    histogram = {}
    for before, now in zip(inside, inside[1:]):
        histogram[now[0] - before[0]] = histogram.get(now[0] - before[0], 0) + 1
    elapsed, ticks, path = b[1] - a[1], b[0] - a[0], b[2] - a[2]
    paces = [tuple(map(int, m.groups()[1:])) for m in PACE.finditer(chunk) if m[1] == "tick"]
    before = [p for p in paces if p[0] <= a[1]]
    after_b = [p for p in paces if p[0] >= b[1]]
    dropped = (after_b[0][8] - before[-1][8]) if before and after_b else None
    owed = elapsed * 30 / 1_000_000
    result = {"burn_ms": a[3], "elapsed_us": elapsed, "ticks": ticks, "ticks_owed": owed, "dropped": dropped,
              "ticks_follow_clock": dropped is not None and abs(ticks + dropped - owed) <= 1,
              "path": path, "path_per_tick": path / ticks if ticks else None,
              "path_per_window": path * window_us / elapsed, "frames": len(inside) - 1,
              "ticks_per_frame": dict(sorted(histogram.items())), "frame_us_mean": elapsed / max(1, len(inside) - 1),
              "moved": path / max(1, ticks) >= 10}
    audio, current, last_run = [], None, None
    for line in chunk.splitlines():
        run = RUN.match(line)
        if run:
            last_run = tuple(map(int, run.groups()))
            continue
        mark = re.match(r"gate-audio: us=(\d+)$", line)
        if mark:
            current = {"us": int(mark[1]), "tick": last_run[0] if last_run else None}
            audio.append(current)
            continue
        if current is None:
            continue
        out = re.match(r"audio-output: cart rate=(\d+) frames=(\d+) ", line)
        if out:
            current["rate"], current["frames"] = int(out[1]), int(out[2])
        music = re.match(r"music: .* frames=(\d+) .*rate=(\d+) ", line)
        if music:
            current["music_frames"], current["music_rate"] = int(music[1]), int(music[2])
    audio = [x for x in audio if "frames" in x and x["tick"] is not None and a[1] <= x["us"] <= b[1] + 4 * 1_000_000]
    result["audio"] = None
    if len(audio) >= 2 and audio[-1]["us"] - audio[0]["us"] >= window_us:
        first, last = audio[0], audio[-1]
        span = last["us"] - first["us"]
        mixer_us = (last["frames"] - first["frames"]) * 1_000_000 / first["rate"]
        info = {"span_us": span, "mixer_us": mixer_us, "mixer_follows_clock": abs(mixer_us - span) <= buffer_us,
                "game_ticks_us": (last["tick"] - first["tick"]) * TICK_US}
        m0, m1 = first.get("music_frames"), last.get("music_frames")
        if m0 is not None and m1 is not None and m1 - m0 > 0:
            music_us = (m1 - m0) * 1_000_000 / first["music_rate"]
            info.update(music_us=music_us, music_follows_clock=abs(music_us - span) <= buffer_us)
        else:
            info.update(music_us=None, music_follows_clock=None)
        result["audio"] = info
    return result


def pace_runs_agree(windows, *, tick_travel):
    """Across the runs at different frame costs: Lara's path over the same stretch of the clock agrees to within
    one tick's travel (the path of the first run's tick, `tick_travel`)."""
    base = windows[0]["path_per_window"]
    return all(abs(w["path_per_window"] - base) <= tick_travel for w in windows[1:])
