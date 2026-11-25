# SPDX-License-Identifier: MIT
"""Drive the test-only two-pad LEVEL1 cart; replay actual inputs on the host."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
from drive import Drive, GateFailure, IDLE, SampledEffectFailure, pad_journal, word
from gate_logs import guest_identity, lara_samples, same_lara

ROOT = Path(__file__).resolve().parents[2]
TICK = re.compile(r"^coop-tick: epoch=(\d+) tick=(\d+) input=([0-9a-f]{4}),([0-9a-f]{4}) "
                  r"present=(\d+) alive=(\d+) room=(\d+),(-?\d+) hash=([0-9a-f]{8})$", re.M)
PLACE = re.compile(r"^coop-place: epoch=(\d+) tick=(\d+) slot=([01]) pose=([-\d ]+)$", re.M)
SHOT = re.compile(r"^\[ares\] screenshot \S*/([^ /]+\.ppm) \(320x240 ", re.M)
FRAME = re.compile(r"^coop-frame: epoch=(\d+) tick=(\d+) views=([12])$", re.M)
PPM_HEADER = b"P6\n320 240\n255\n"
HALF_RGB = 320 * 120 * 3
PICTURE_VIEWS = {"joined": 2, "apart": 2, "pickup": 2, "ring": 1, "one-dead": 2,
                 "no-large-medipack": 2, "both-dead": 1, "left": 1}
COMMAND_EVENTS = re.compile(r"^play-coop: (no-medipack player=([12])|leave busy)$|" + TICK.pattern, re.M)
# Two RGBA5551 channel steps in mean RGB error leave room for host/cart
# sampling and dithering differences. Gate each half separately so a bad
# viewport cannot hide behind agreement in the other one.
MAX_RGB_MAE = 16
# HUD borders and fills are unfiltered RGB5551 rectangles: one channel
# step is enough for a present bar; background uses the world tolerance.
HUD_RGB_MAE = 8
BAR_X, BAR_WIDTH, BAR_HEIGHT = 212, 102, 7
BAR_ROWS = (4, 14, 124, 134)
BAR_LIGHT, BAR_DARK = (74, 82, 74), (115, 132, 115)
PORT_RELAY = ROOT / "tools/ares/pad_relay.py"
WOLF_HIT = r"^coop-wolf: hit={} health_before=(-?\d+) health_after=(-?\d+)$"


def require(ok, why):
    if not ok:
        raise GateFailure(why)


def verify_restored_poses(text, saved):
    first = next(TICK.finditer(text), None)
    require(first and first.groups()[:2] == ("4", "0") and first.groups()[4:6] == ("3", "3"),
            "load did not rejoin both players before the first simulation tick")
    following = TICK.search(text, first.end())
    require(following and following.groups()[:2] == ("4", "1"), "missing first tick after load")
    poses = re.findall(r"^coop-start-pose: epoch=4 slot=([01]) (\{.+\})$",
                       text[first.end():following.start()], re.M)
    require(sorted(slot for slot, _ in poses) == ["0", "1"], "expected one restored pose for each slot at tick zero")
    for slot, pose in poses:
        p = json.loads(pose)
        require(tuple(p["pos"]) == saved["pos"] and p["room"] == saved["room"] and p["rot"][1] == saved["rot"][1],
                f"slot {int(slot) + 1} is not at the saved off-spawn checkpoint")


def verify_wolf_intervals(first, second):
    for victim, interval in ((1, first), (0, second)):
        def damaged(slot):
            return any(int(after) < int(before) for before, after in
                       re.findall(WOLF_HIT.format(slot), interval, re.M))

        require(damaged(victim), f"wolf did not bite slot {victim} when it was nearer")
        require(not damaged(1 - victim), f"wolf damaged the distant slot {1 - victim}")


def rows(text, epoch):
    return [m.groups()[1:] for m in TICK.finditer(text) if int(m[1]) == epoch]


def command_masks(log, epoch, end, output):
    ticks = [r for r in rows(log, epoch) if int(r[0]) <= end]
    require([int(r[0]) for r in ticks] == list(range(end + 1)), f"missing epoch {epoch} trace rows")
    directives = []
    present = 1
    for tick, _, _, now, *_ in ticks:
        now = int(now)
        if now != present:
            require({now, present} == {1, 3}, f"unsupported presence transition: {present}->{now}")
            directives.append(f"{'join' if now == 3 else 'leave'} {tick}")
        present = now
    for match in PLACE.finditer(log):
        if int(match[1]) == epoch and int(match[2]) <= end:
            directives.append(f"place {match[2]} {match[3]} {match[4]}")
    pending = None
    for match in COMMAND_EVENTS.finditer(log):
        if match[1]:
            require(pending is None, "two co-op command results before a tick")
            pending = ("warp", int(match[2]) - 1) if match[2] else ("leave-result", 1)  # LEAVE_BUSY
        elif pending is not None:
            if int(match[3]) == epoch and int(match[4]) <= end:
                directives.append(f"{pending[0]} {match[4]} {pending[1]}")
            pending = None
    directives.sort(key=lambda line: int(line.split()[1]))
    output.write_text("coop 1\n" + "".join(f"{line}\n" for line in directives) +
                      "".join(f"{int(r[1], 16)} {int(r[2], 16)}\n" for r in ticks[1:]))
    return [r[-1] for r in ticks]


def replay(host, cart_log, epoch, end, out, *, label=None, ui=True, coop_world=False):
    stem = label or f"host-{epoch}"
    script, trace, shot = [out / f"{stem}.{ext}" for ext in ("masks", "jsonl", "ppm")]
    expected = command_masks(cart_log, epoch, end, script)
    command = ["rtk", "proxy", host, "--game-live", "--game-level", "level1",
               "--game-input", script, "--game-ticks", str(end), "--game-trace-schema", "5",
               "--game-trace", trace, "--game-shot", shot]
    mode = ["--game-ui"] if ui else ["--game-coop-world"] if coop_world else []
    result = subprocess.run(command + mode, capture_output=True)
    (out / f"{stem}.log").write_bytes(result.stdout + result.stderr)
    require(result.returncode == 0, f"host replay failed for epoch {epoch}: {result.stderr[-1500:]!r}")
    actual = [f"{r['hash']:08x}" if isinstance(r['hash'], int) else r['hash'].lower()
              for r in map(json.loads, trace.read_text().splitlines())]
    require(len(actual) == len(expected), f"wrong host trace length in epoch {epoch}")
    for tick, (a, b) in enumerate(zip(actual, expected, strict=True)):
        require(a == b, f"epoch {epoch} tick {tick}: host hash {a} != cart hash {b}")
    return len(actual)


def read_picture(path):
    data = path.read_bytes()
    require(data.startswith(PPM_HEADER) and len(data) == len(PPM_HEADER) + 2 * HALF_RGB,
            f"invalid 320x240 VI picture: {path}")
    return data


def rgb_mae(actual, reference):
    return sum(abs(a - b) for a, b in zip(actual, reference, strict=True)) / len(reference)


def picture_region(rgb, x, y, width, height):
    return b"".join(rgb[i:i + 3 * width] for i in
                    (3 * (320 * row + x) for row in range(y, y + height)))


def bar_present(rect):
    # draw.c paints 100 flat pixels on each horizontal edge. Check both
    # edges, not a colour count that could confuse a world texture for HUD.
    return all(sum(tuple(row[i:i + 3]) == colour for i in range(3, 303, 3)) >= 98
               for row, colour in ((rect[:306], BAR_LIGHT), (rect[-306:], BAR_DARK)))


def verify_bars(label, actual, wanted):
    errors = {}
    for y in BAR_ROWS:
        observed = picture_region(actual, BAR_X, y, BAR_WIDTH, BAR_HEIGHT)
        expected = picture_region(wanted, BAR_X, y, BAR_WIDTH, BAR_HEIGHT)
        shown = bar_present(expected)
        error = rgb_mae(observed, expected)
        limit = HUD_RGB_MAE if shown else MAX_RGB_MAE
        require(bar_present(observed) == shown and error <= limit,
                f"{label}: HUD bar row {y}, expected {'present' if shown else 'empty'}, RGB MAE {error:.2f}/{limit}")
        errors[f"bar-y{y}"] = error
    return errors


def verify_notice(label, actual, wanted, world):
    """Compare the actual overlay's footprint, and the mirror that must stay empty."""
    # draw.c centers the revive string at local y=45. The animated pickup
    # is below the bars; its footprint comes from two host renders.
    revive = label == "no-large-medipack"
    y0, y1 = (29, 46) if revive else (21, 120)
    locations = []
    for y in range(y0, y1):
        for x in range(320):
            i = HALF_RGB + 3 * (320 * y + x)
            if max(abs(a - b) for a, b in zip(wanted[i:i + 3], world[i:i + 3], strict=True)) >= 24:
                locations.append(i)
    require(locations, f"{label}: host rendered no player-2 HUD notice")

    def masked(pixels, offset):
        return b"".join(pixels[i + offset:i + offset + 3] for i in locations)

    visible = masked(actual, 0)
    error = rgb_mae(visible, masked(wanted, 0))
    absent_error = rgb_mae(visible, masked(world, 0))
    other_error = rgb_mae(masked(actual, -HALF_RGB), masked(world, -HALF_RGB))
    # Font is unfiltered; the moving 3D silhouette and RDP texture sampling
    # get three channel steps, plus the picture must beat the no-HUD render.
    limit = HUD_RGB_MAE if revive else 3 * HUD_RGB_MAE
    require(error <= limit and error < absent_error,
            f"{label}: missing player-2 HUD notice, RGB MAE {error:.2f}/{limit} (world {absent_error:.2f})")
    require(other_error <= MAX_RGB_MAE,
            f"{label}: unexpected player-1 HUD notice, RGB MAE {other_error:.2f}/{MAX_RGB_MAE}")
    return {"notice-p2": error, "empty-p1-notice": other_error, "notice-pixels": len(locations)}


def verify_picture(label, picture, reference, solo=None, world=None, *, hud=None):
    actual, wanted = (read_picture(path)[len(PPM_HEADER):] for path in (picture, reference))
    layout = read_picture(solo)[len(PPM_HEADER):] if label == "left" and solo is not None else wanted
    errors = [rgb_mae(actual[i:i + HALF_RGB], layout[i:i + HALF_RGB]) for i in (0, HALF_RGB)]
    require(all(e <= MAX_RGB_MAE for e in errors),
            f"{label}: picture does not match the {PICTURE_VIEWS[label]}-view host render "
            f"(half RGB MAE {errors[0]:.2f}/{errors[1]:.2f}, limit {MAX_RGB_MAE})")
    if solo is not None and PICTURE_VIEWS[label] == 2:
        # Reject a shared presentation bug which makes both the host's
        # supposed split render and the cart look like the plain renderer.
        mono = read_picture(solo)[len(PPM_HEADER):]
        solo_errors = [rgb_mae(actual[i:i + HALF_RGB], mono[i:i + HALF_RGB]) for i in (0, HALF_RGB)]
        require(all(w < s for w, s in zip(errors, solo_errors, strict=True)),
                f"{label}: a half is not closer to the split projection than to the plain one-view render")
    if label in ("ring", "both-dead"):
        # game/common/menu_text.c places the title at y=20 and selected label at
        # y=232. Check the warm foreground of the host glyphs, not the
        # background, at the full-screen coordinates. Missing or half-view
        # menu text cannot hide behind mostly unchanged world pixels.
        bands = (("title", 4, 24), ("item label", 216, 236)) if label == "ring" else (
                 ("Restart Level label", 216, 236),)
        for name, y0, y1 in bands:
            pixels_a, pixels_b = bytearray(), bytearray()
            for y in range(y0, y1):
                for x in range(80, 240):
                    i = 3 * (320 * y + x)
                    r, g, b = wanted[i:i + 3]
                    if r >= 128 and g >= 96 and r - g >= 8 and g - b >= 32:
                        pixels_a.extend(actual[i:i + 3])
                        pixels_b.extend(wanted[i:i + 3])
            require(pixels_b and rgb_mae(pixels_a, pixels_b) <= MAX_RGB_MAE,
                    f"{label}: missing full-screen {name} in the expected glyph positions")
    focused = verify_bars(label, actual, wanted)
    if label in ("pickup", "no-large-medipack"):
        require(world is not None, f"{label}: missing no-HUD host reference")
        focused.update(verify_notice(label, actual, wanted, read_picture(world)[len(PPM_HEADER):]))
    if hud is not None:
        hud.update(focused)
    return errors


def picture_tick(text, basename, views):
    shot = next((m for m in SHOT.finditer(text) if m[1] == basename), None)
    require(shot, f"VI log missing {basename}")
    frames = list(FRAME.finditer(text, 0, shot.start()))
    require(len(frames) >= 2, f"{basename}: no settled frame before VI capture")
    epoch, tick, mode = map(int, frames[-1].groups())
    require(mode == views and frames[-2][1] == str(epoch) and frames[-2][3] == str(mode),
            f"{basename}: expected a settled {views}-view frame in epoch {epoch}")
    return epoch, tick


def verify_pictures(host, text, photos, out):
    refs = out / "picture-reference"
    refs.mkdir()
    result = {}
    for label, views in PICTURE_VIEWS.items():
        epoch, tick = picture_tick(text, photos[label], views)
        if label == "apart":
            position = next((t for t in rows(text, epoch) if int(t[0]) == tick), None)
            require(position and position[3:5] == ("3", "3") and position[5] != position[6],
                    "apart VI picture has no two living players in distinct camera rooms")
        # Use the same input stream up to the preceding completed frame.
        # Solo bypasses the co-op presentation selector entirely. For each
        # joined/apart also render an independent single-view counterexample.
        replay(host, text, epoch, tick, refs, label=label)
        solo = world = None
        if label in ("joined", "apart", "left"):
            replay(host, text, epoch, tick, refs, label=f"{label}-solo", ui=False)
            solo = refs / f"{label}-solo.ppm"
        if label in ("pickup", "no-large-medipack"):
            replay(host, text, epoch, tick, refs, label=f"{label}-world", ui=False, coop_world=True)
            world = refs / f"{label}-world.ppm"
        hud = {}
        # Leave uses the plain renderer for the broad layout and the drawn
        # solo HUD reference for the narrow bar check.
        ref = refs / f"{label}.ppm"
        errors = verify_picture(label, out / "pictures" / f"{label}.ppm", ref, solo, world, hud=hud)
        result[label] = dict(epoch=epoch, tick=tick, views=views, capture=photos[label], half_rgb_mae=errors, hud=hud)
    return result


def wait_rooms(drive, epoch, after=0):
    while True:
        text = drive.text()
        for match in TICK.finditer(text, after):
            if int(match[1]) == epoch and match[5] == "3" and match[7] != match[8]:
                return match.end()
        drive.service(text)
        drive.wait(r"^coop-tick: ", after=len(text))


def stage_shot(drive, label, after):
    drive.wait(SHOT.pattern, after=after)
    match = SHOT.search(drive.text(), after)
    return label, match[1]

def settled_shot(drive, label, after):
    drive.wait_frames(after, 4)
    completed = list(FRAME.finditer(drive.text(), after))
    require(len(completed) >= 4, f"{label}: missing settled co-op frame")
    return stage_shot(drive, label, completed[3].end())



def verify_transport(drive):
    receipt = (drive.out / "ares-identity.txt").read_text()
    ares = re.fullmatch(r"ares_identity=([0-9a-f]{64}) ares_sha256=([0-9a-f]{64})\n", receipt)
    require(ares, "missing sampled ares admission identity")
    status = (drive.out / "pad-relay-status.txt").read_text()
    require(status == "0\n", f"pad relay failed (status {status.strip()})")
    subprocess.run(["rtk", "proxy", "python3", PORT_RELAY, "verify", drive.out / "pad-samples.txt", pad_journal(drive.pad),
                    drive.out / "pad-windows.txt", ares[1]], check=True)
    return receipt


def run(rom, out, timeout, expected):
    erased = out / "erased.srm"
    erased.write_bytes(b"\xff" * 32768)
    drive = Drive(ROOT, rom, out / "cart", timeout=timeout, two_pads=True, sram_in=erased)
    shots = []

    def event(value, label, pattern, *, pad=1):
        entry = drive.inject_idle_reads(f"prime {label}", pad=pad)
        drive.wait_sample(pad, entry)
        end = drive.press_until_effect(value, label, pattern, pad=pad)
        entry = drive.inject_idle_reads(f"release {label}", pad=pad)
        drive.wait_sample(pad, entry)
        return end

    def passport():
        event(word("START"), "open pad-2 inventory", r"^ui: menu .* state=4 page=5 ", pad=2)
        event(word("DOWN"), "options ring", r"^ui: menu .* state=4 page=7 ", pad=2)
        show = event(word("B"), "show passport", r"^ui: menu .* state=9 page=7 passport=1 ", pad=2)
        drive.wait_frames(show, 18)

    try:
        drive.wait_sampled_films(("ESCAPE", "CORE", "CAFE"))
        drive.wait(r"^play: ready=title ")
        guest_identity.require_identity(drive.text(), expected, play=True)
        # Manual selection starts a fresh LEVEL1 with no carry or implicit game tick.
        event(word("L", "CUP"), "select LEVEL1", r"^play: ready=level1 ")
        joined = event(word("START"), "join slot 1", r"^play-coop: joined$", pad=2)
        two = drive.wait(r"^coop-frame: epoch=1 tick=\d+ views=2$", after=joined)
        drive.wait_frames(two, 4)
        two = len(drive.text())
        shots.append(stage_shot(drive, "joined", two))
        # From LEVEL1's start, shift left off the blocking rocks, then run toward
        # the first portal. Player 1 walks independently and stops in room 0.
        drive.inject(word("UP"), "walk player 1")
        drive.inject(word("CLEFT"), "sidestep player 2", pad=2)
        drive.wait_ticks(24)
        drive.inject(IDLE, "stop player 1")
        drive.wait_ticks(78)
        drive.inject(IDLE, "finish sidestep", pad=2)
        drive.wait_ticks(9)
        drive.inject(word("UP"), "run player 2 through first portal", pad=2)
        apart = wait_rooms(drive, 1, two)
        drive.wait_sample(2, drive.inject(IDLE, "stop player 2 apart", pad=2))
        drive.wait_frames(apart, 4)
        shots.append(stage_shot(drive, "apart", len(drive.text())))
        epoch1_end = int(rows(drive.text(), 1)[-1][0])

        event(word("L", "START"), "restart LEVEL1", r"^play-coop: rejoined$")
        drive.wait(r"^coop-frame: epoch=2 tick=\d+ views=2$")
        event(word("L", "R"), "place player 2 on actual small medipack", r"^coop-place: epoch=2 .* slot=1 ", pad=2)
        # Different chord, so no neutral prime between award and leave: the
        # actor must still owe the remaining pickup animation at this poll.
        pickup = drive.press_until_effect(word("B"), "pick up with player 2", r"^ui: pickup player=2 ", pad=2)
        busy = drive.press_until_effect(word("L", "START"), "leave while pickup settles",
                                        r"^play-coop: (?:left|leave busy)$", pad=2)
        require(drive.text()[:busy].endswith("play-coop: leave busy"), "leave was accepted during the committed pickup")
        drive.wait_sample(2, drive.inject_idle_reads("release busy leave", pad=2))
        drive.wait(r"^coop-frame: epoch=2 tick=\d+ views=2$", after=busy)
        shots.append(settled_shot(drive, "pickup", pickup))
        drive.wait_ticks(60)  # pickup has had time to reach its authored release
        event(word("L", "START"), "retry leave after pickup", r"^play-coop: left$", pad=2)
        event(word("START"), "rejoin after pickup settles", r"^play-coop: joined$", pad=2)
        opened = event(word("START"), "open ring from pad 2", r"^ui: menu .* state=4 page=5 ", pad=2)
        full = drive.wait(r"^coop-frame: epoch=2 tick=\d+ views=1$", after=opened)
        drive.wait_frames(full, 4)
        shots.append(stage_shot(drive, "ring", len(drive.text())))
        closed = event(word("A"), "close ring from pad 2", r"^ui: menu .* state=0 ", pad=2)
        drive.wait(r"^coop-frame: epoch=2 tick=\d+ views=2$", after=closed)
        dead = event(word("L", "Z"), "drop player 2", r"^coop-place: epoch=2 .* slot=1 ", pad=2)
        dead = drive.wait(r"^coop-tick: epoch=2 .* present=3 alive=1 ", after=dead)
        require(not re.search(r"^ui: menu .* state=[1-9]", drive.text()[dead:], re.M), "death page with a survivor")
        shots.append(settled_shot(drive, "one-dead", dead))
        notice = event(word("L", "A"), "refuse revive without a large medipack", r"^play-coop: no-medipack player=2$", pad=2)
        shots.append(settled_shot(drive, "no-large-medipack", notice))
        event(word("L", "Z"), "drop player 1", r"^coop-place: epoch=2 .* slot=0 ")
        both = drive.wait(r"^coop-tick: epoch=2 .* present=3 alive=0 ")
        require(not re.search(r"^ui: menu .* state=[1-9]\d* page=1 ", drive.text()[closed:both], re.M),
                "death passport opened before both Laras were dead")
        death = drive.wait(r"^ui: menu .* state=9 page=1 ", after=both)
        full = drive.wait(r"^coop-frame: epoch=2 tick=\d+ views=1$", after=death)
        drive.wait_frames(full, 12)  # let the selected passport finish opening
        full = len(drive.text())
        shots.append(stage_shot(drive, "both-dead", full))
        epoch2_end = int(rows(drive.text(), 2)[-1][0])

        event(word("L", "START"), "restart again", r"^play-coop: rejoined$")
        drive.wait(r"^coop-frame: epoch=3 tick=\d+ views=2$")
        left = event(word("L", "START"), "leave slot 1", r"^play-coop: left$", pad=2)
        single = drive.wait(r"^coop-frame: epoch=3 tick=\d+ views=1$", after=left)
        # The first VI capture logged after the one-view marker can still
        # see the previously displayed split buffer. Capture a later VI.
        drive.wait_frames(single, 4)
        shots.append(stage_shot(drive, "left", len(drive.text())))
        epoch3_end = int(rows(drive.text(), 3)[-1][0])

        # Save a distinct primary pose, with the partner present; then walk
        # farther so a no-op load cannot satisfy the restored-position check.
        event(word("START"), "rejoin for save", r"^play-coop: joined$", pad=2)
        drive.inject(word("UP"), "change primary save pose")
        drive.wait_ticks(35)
        drive.inject(IDLE, "stop at save pose")
        drive.wait_ticks(12)
        passport()
        event(word("B"), "open save picker", r"^ui: picker open load=0 valid=000 unusable=000 index=0$", pad=2)
        saved_at = event(word("B"), "save slot 1 with co-op active", r"^play-lara: saved ", pad=2)
        drive.wait(r"^ui: menu .* state=0 ", after=saved_at)
        saved = next(s for s in lara_samples(drive.text()) if s["kind"] == "saved")
        require(saved["pos"] is not None and saved["pos"][2] > 3584, "checkpoint is not beyond the authored LEVEL1 spawn")
        drive.inject(word("UP"), "walk away from save")
        drive.wait_ticks(35)
        drive.inject(IDLE, "stop away from save")
        drive.wait_ticks(12)
        moved = lara_samples(drive.text())[-1]
        require(moved["pos"] != saved["pos"], "did not move away from the saved position")
        passport()
        event(word("LEFT"), "select Load Game", r"^ui: menu .* state=9 page=7 passport=0 ", pad=2)
        event(word("B"), "open load picker", r"^ui: picker open load=1 valid=100 unusable=000 index=0$", pad=2)
        restored_at = event(word("B"), "load saved slot with co-op active", r"^play-lara: restored ", pad=2)
        rejoined = drive.wait(r"^play-coop: rejoined$", after=restored_at)
        loaded = drive.wait(r"^coop-frame: epoch=4 tick=\d+ views=2$", after=rejoined)
        text = drive.text()
        restored = next(s for s in lara_samples(text) if s["kind"] == "restored")
        require(same_lara(saved, restored), "loaded slot 0 does not match its saved pose/health")
        verify_restored_poses(text[restored_at:], saved)
        shots.append(stage_shot(drive, "rejoined-after-load", loaded))
    except BaseException:
        drive.stop(check=False)
        raise
    else:
        drive.stop()
    receipt = verify_transport(drive)
    with rom.open("rb") as stream:
        sha = hashlib.file_digest(stream, "sha256").hexdigest()
    require(guest_identity.rom_receipt((drive.out / "ares-host.txt").read_text()) == sha,
            "cart ROM receipt differs from the requested ROM")
    (out / "controller-samples.log").write_text(receipt + (drive.out / "pad-samples.txt").read_text())
    require(re.findall(r"^video: name=(\w+) result=(\w+) ", drive.text(), re.M) ==
            [(name, "skipped") for name in ("ESCAPE", "CORE", "CAFE")],
            "PC boot films did not all skip in order")
    image_dir = out / "pictures"
    image_dir.mkdir()
    for label, basename in shots:
        source = drive.vi / basename
        require(source.is_file(), f"missing requested VI picture {basename}")
        (image_dir / f"{label}.ppm").write_bytes(read_picture(source))
    return drive.text(), (epoch1_end, epoch2_end, epoch3_end), dict(shots)

def run_wolf(rom, out, timeout, expected):
    """Directed PC LEVEL1 wolf activation and swap, with no shipping probe."""
    drive = Drive(ROOT, rom, out / "cart", timeout=timeout, two_pads=True)

    def send(port, buttons, pattern):
        entry = drive.inject_idle_reads("prime edge", pad=port)
        drive.wait_sample(port, entry)
        after = drive.press_until_effect(word(*buttons), "+".join(buttons), pattern, pad=port)
        entry = drive.inject_idle_reads("release", pad=port)
        drive.wait_sample(port, entry)
        return after

    try:
        drive.wait_sampled_films(("ESCAPE", "CORE", "CAFE"))
        drive.wait(r"^play: ready=title ")
        guest_identity.require_identity(drive.text(), expected, play=True)
        send(1, ("L", "CUP"), r"^play: ready=level1 ")
        send(2, ("START",), r"^play-coop: joined$")
        near = send(2, ("L", "B"), r"^coop-wolf: near=1$")
        chosen = drive.wait(r"^coop-wolf: target=1$", after=near)
        drive.wait(WOLF_HIT.format(1), after=chosen)
        switched = send(1, ("L", "B"), r"^coop-wolf: near=0$")
        retarget = drive.wait(r"^coop-wolf: target=0$", after=switched)
        hit = drive.wait(WOLF_HIT.format(0), after=retarget)
        drive.wait_frames(hit, 12)
        text = drive.text()
        verify_wolf_intervals(text[near:switched], text[switched:])
    except BaseException:
        drive.stop(check=False)
        raise
    else:
        drive.stop()
    verify_transport(drive)
    require(re.findall(r"^video: name=(\w+) result=(\w+) ", drive.text(), re.M) ==
            [(name, "skipped") for name in ("ESCAPE", "CORE", "CAFE")],
            "PC boot films did not all skip in order")
    print("co-op fight PASS: wolf bit nearer slot 1, then slot 0 after retargeting; neither distant partner was hurt")



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--host", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("--identity-header", type=Path)
    parser.add_argument("--wolf-only", action="store_true", help="run just the directed wolf retarget")
    args = parser.parse_args()
    if not args.wolf_only and not args.host:
        parser.error("--host is required for the full gameplay and replay lane")
    args.out.mkdir(parents=True)
    rom = args.rom.resolve(strict=True)
    expected = guest_identity.expected_identity(rom.parent, args.identity_header)
    if args.wolf_only:
        run_wolf(rom, args.out.resolve(), args.timeout, expected)
        return
    text, ends, photos = run(rom, args.out.resolve(), args.timeout, expected)
    hashes = {epoch: replay(args.host.resolve(), text, epoch, end, args.out.resolve())
              for epoch, end in enumerate(ends, 1)}
    pictures = verify_pictures(args.host.resolve(), text, photos, args.out.resolve())
    (args.out / "summary.json").write_text(json.dumps(dict(passed=True, hashes=hashes, pictures=pictures), indent=2) + "\n")
    print(f"co-op PASS: {hashes} matching cart/host schema-5 hashes; {args.out / 'pictures'}")


if __name__ == "__main__":
    try:
        main()
    except SampledEffectFailure as error:
        print(error, file=sys.stderr)
        sys.exit(error.exit_status)
