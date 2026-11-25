#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Bounded scene runner (ported from MIT ac3n64 04134cc).

Routes select checks with `observe NAME [operands...]`. Built-ins are state,
boot-card, screenshot SHA256, pools, code, placement and log REGEX.
Later gates add --checks FILE.py (repeatable); each file exports a CHECKS dict
mapping new names to check(context, operands). Raise ValueError on failure.
Context keys: meta, scene, mode, cart, build, executable, artifacts, log, shot,
results. shot is a Path or None; results is the printable result dictionary.
Unknown/duplicate names fail before building; plugins cannot replace checks.
Every cart scene must report its build directory's generated build id.
"""
import argparse
import hashlib
import importlib.util
import json
import math
import os
import pathlib
import re
import subprocess
import signal
import sys
import zlib
sys.dont_write_bytecode = True

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.append(str(ROOT / "tools/cook"))
from n64_pack import HEADER, LEVEL
from pool_rows import pool_rows, pool_table
spec = importlib.util.spec_from_file_location("routec", ROOT / "tools/routec/routec.py")
routec = importlib.util.module_from_spec(spec)
spec.loader.exec_module(routec)
spec = importlib.util.spec_from_file_location("guest_identity", ROOT / "test/run/guest_identity.py")
guest_identity = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guest_identity)


def remote_ares():
    """True when TR64_ARES_HOST sends the emulator to another machine: stopping a run then also copies its results back."""
    return os.environ.get("TR64_ARES_HOST", "") not in ("", "local", "localhost")


def timeout_scale():
    """TR64_TIMEOUT_SCALE (default 1): every command deadline of the harness scripts is multiplied by it.

    The deadlines are hang guards written for an idle machine; a loaded one (several agents building at
    once) needs more. The emulator's own deadline (ARES_TIMEOUT, ares.sh) is not scaled."""
    text = os.environ.get("TR64_TIMEOUT_SCALE", "1")
    scale = float(text)
    if not math.isfinite(scale) or scale < 1:
        raise ValueError("TR64_TIMEOUT_SCALE must be finite and at least 1")
    return scale


def command(args, log=None, **kwargs):
    timeout = kwargs.pop("timeout", None)
    if timeout is not None:
        timeout *= timeout_scale()
    expected = kwargs.pop("expected", 0)
    # Launch the harness itself, not an rtk wrapper; its trap owns separate
    # emulator/display groups. TERM allows that trap to reap both groups.
    argv = list(map(str, args))
    process = subprocess.Popen(argv, cwd=ROOT, start_new_session=True,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **kwargs)
    try:
        output, _ = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGTERM)
        try:
            output, _ = process.communicate(timeout=5 if not remote_ares() else 900)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            output, _ = process.communicate()
        if log:
            pathlib.Path(log).write_bytes(output)
        raise RuntimeError(f"command deadline: {argv}")
    result = subprocess.CompletedProcess(argv, process.returncode, output)
    if log:
        pathlib.Path(log).write_bytes(result.stdout)
    if result.returncode != expected:
        raise RuntimeError(f"command failed ({result.returncode}): {' '.join(map(str, args))}\n" + result.stdout.decode(errors="replace"))
    return result.stdout


def boot_shot_hash(data):
    header = b"P6\n320 240\n255\n"
    if not data.startswith(header) or len(data) != len(header) + 320 * 240 * 3:
        raise ValueError("boot screenshot must be canonical 320x240 RGB P6")
    # Only build identity and per-platform numeric accounting differ. Labels,
    # route tick digits, layout and background remain a strict pixel golden.
    # Numeric fields are independently checked against the selected table below.
    canonical = bytearray(data)
    count = len(pool_rows())
    rectangles = [(64, 42, 96, 10)] + [(60, 76 + i * 9, 28, 5) for i in range(count)]
    for x, y0, width, height in rectangles:
        for y in range(y0, y0 + height):
            start = len(header) + (y * 320 + x) * 3
            canonical[start:start + width * 3] = bytes(width * 3)
    return hashlib.sha256(canonical).hexdigest()


def pools_check(text, cart, *, game=None):
    expected = pool_rows(game=game if game is not None or cart else 1)
    found = {}
    for match in re.finditer(r"^pool: (\w+) base=([0-9a-f]+) size=(\d+) used=(\d+) peak=(\d+) kind=(\d+)$", text, re.M):
        found[match[1]] = tuple(int(match[i], 16 if i == 2 else 10) for i in range(2, 7))
    if set(found) != {r[0] for r in expected}:
        raise ValueError("pool report missing or extra rows")
    previous_end = None
    intervals = []
    for name, budget, kind in expected:
        base, size, used, peak, actual_kind = found[name]
        if size != budget or actual_kind != {"IMAGE": 0, "FIXED": 1, "CARVE": 2}[kind] or not base or not 0 <= used <= peak:
            raise ValueError("pool accounting mismatch: " + name)
        if kind != "IMAGE" and peak > size:
            raise ValueError("pool overflow: " + name)
        if kind == "CARVE":
            if previous_end is not None and base != previous_end:
                raise ValueError("noncontiguous carve: " + name)
            previous_end = base + size
        if cart and not 0x80000000 <= base < base + max(size, used) <= 0x80400000:
            raise ValueError("pool outside stock 4 MiB: " + name)
        # Image's linked extent, not its budget, owns physical addresses.
        intervals.append((base, base + (used if kind == "IMAGE" else size), name))
    intervals.sort()
    if any(a[1] > b[0] for a, b in zip(intervals, intervals[1:])):
        raise ValueError("overlapping pool placements")
    return found


def code_check(text):
    loads = re.findall(r"code: load test pages=(\d+) pairs=(\d+) first=([0-9a-f]+) last=([0-9a-f]+)", text)
    if len(loads) != 2 or any(int(r[0]) < 4 or int(r[1]) < 2 for r in loads) or loads[0][2:] == loads[1][2:]:
        raise ValueError("test unit did not reload into different frames")
    if text.count("code: unload test released=") != 2 or "code: unload init released=" not in text:
        raise ValueError("missing unit release")
    if not re.search(r"code: frames used=0 free=\d+ tlb_entries=1 wired=1", text):
        raise ValueError("missing released init frames / wired TLB report")
    calls = re.findall(r"code: call test tick=(\d+) value=(\d+)", text)
    if calls != [(str(i), str(156 + i)) for i in (0, 1, 4, 5)]:
        raise ValueError("unit text/rodata call results differ")


def observe_state(ctx, args):
    match = re.search(r"scene: ticks=(\d+) state=([0-9a-f]{8})", ctx["log"])
    if not match or match[1] != ctx["meta"]["ticks"] or match[2] != ctx["meta"]["expect-state"]:
        raise ValueError("scene state/tick mismatch")
    ctx["results"]["state"] = match[2]


def observe_card(ctx, args):
    if ctx["shot"] is None:
        raise ValueError("boot-card observable requires a screenshot")
    actual = boot_shot_hash(ctx["shot"].read_bytes())
    if actual != ctx["meta"]["expect-shot"]:
        raise ValueError(f"boot-card mismatch: actual={actual} expected={ctx['meta']['expect-shot']}")
    ctx["results"]["shot"] = actual


def observe_screenshot(ctx, args):
    if ctx["shot"] is None or hashlib.sha256(ctx["shot"].read_bytes()).hexdigest() != args[0]:
        raise ValueError("screenshot SHA256 mismatch")


def observe_log(ctx, args):
    if not re.search(args[0], ctx["log"], re.M):
        raise ValueError(f"missing log observable: {args[0]}")


def observe_placement(ctx, args):
    if not ctx["cart"]:
        return
    text = ctx["log"]
    if not re.search(r"libheap: used=\d+ peak=\d+ calls=\d+ post_boot_allocations=0 frozen=1", text):
        raise ValueError("missing measured frozen heap counters")
    if not all(f"placement: {label}=" in text for label in ("fb0", "fb1", "z", "stack")):
        raise ValueError("missing actual platform placements")
    hardware = 8388608 if ctx["mode"] == "ares-pak" else 4194304
    if f"hardware_rdram={hardware} sdk_rdram=4194304" not in text:
        raise ValueError("emulated Pak setting / SDK 4 MiB clamp mismatch")
    config = (ctx["build"] / "generated/build_id.h").read_text()
    flag = re.search(r"^#define TR64_TEST ([01])$", config, re.M)
    if not flag:
        raise ValueError("missing ROM build test flag")
    checked = 1048576 if hardware == 8388608 and flag[1] == "1" else 0
    if f"guard_checked={checked} guard_changed=0" not in text:
        raise ValueError("above-4-MiB write guard failed")


def observe_pools(ctx, args):
    if ctx["cart"] or not ctx["meta"].get("level"):
        pools_check(ctx["log"], ctx["cart"])
        return
    # Independently select the host's expected rows from the bound pack's
    # validated directory, not from a guest-reported budget or build knob.
    header = (ctx["build"] / "generated/level_pack.h").read_text()
    match = re.search(r'^#define TR64_LEVEL_PACK_PATH (".*")$', header, re.M)
    if not match:
        raise ValueError("missing host pack binding for pool check")
    with pathlib.Path(json.loads(match[1])).open("rb") as pack:
        raw = pack.read(HEADER.size)
        if len(raw) != HEADER.size:
            raise ValueError("host pool check: truncated pack header")
        magic, version, size, count, off, stride, crc, reserved = HEADER.unpack(raw)
        io = next(n for name,n,_ in pool_rows(game=1) if name == "io")
        if (magic,version,off,stride,reserved) != (b"TR6P",2,HEADER.size,LEVEL.size,0) or not 0 < count <= io//stride or off+count*stride > size:
            raise ValueError("host pool check: invalid pack directory")
        directory = pack.read(count*stride)
    if len(directory) != count*stride or zlib.crc32(directory) != crc:
        raise ValueError("host pool check: invalid pack directory CRC")
    target = ctx["meta"]["level"].encode()
    tags = [tag for name, _, _, _, tag in LEVEL.iter_unpack(directory) if name.split(b"\0",1)[0] == target]
    if len(tags) != 1 or tags[0] not in (0,2):
        raise ValueError("host pool check: missing or invalid level game")
    pools_check(ctx["log"], False, game=tags[0] or 1)


BUILTINS = {
    "state": observe_state, "boot-card": observe_card,
    "screenshot": observe_screenshot, "log": observe_log,
    "pools": observe_pools,
    "code": lambda ctx, args: code_check(ctx["log"]),
    "placement": observe_placement,
}


def load_checks(paths):
    checks = dict(BUILTINS)
    for i, path in enumerate(paths):
        spec = importlib.util.spec_from_file_location(f"scene_checks_{i}", path)
        if spec is None or spec.loader is None:
            raise ValueError(f"cannot load observable checks: {path}")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        supplied = getattr(module, "CHECKS", None)
        if not isinstance(supplied, dict):
            raise ValueError(f"{path}: expected CHECKS dictionary")
        for name, check in supplied.items():
            if not isinstance(name, str) or not re.fullmatch(r"[a-z][a-z0-9-]*", name) or not callable(check) or name in checks:
                raise ValueError(f"{path}: invalid/duplicate observable {name}")
            checks[name] = check
    return checks


def validate_observables(meta, checks):
    for name, args in meta["observables"]:
        if name not in checks:
            raise ValueError(f"unknown observable: {name}")
        if name in BUILTINS:
            count = 1 if name in ("screenshot", "log") else 0
            if len(args) != count:
                raise ValueError(f"observable {name} requires {count} operands")
        if name == "state" and "expect-state" not in meta:
            raise ValueError("state observable requires expect-state")
        if name == "boot-card" and "expect-shot" not in meta:
            raise ValueError("boot-card observable requires expect-shot")
        if name == "boot-card" and meta.get("level"):
            raise ValueError("boot-card observable requires a no-level scene")
        if name == "screenshot" and not re.fullmatch(r"[0-9a-f]{64}", args[0]):
            raise ValueError("screenshot observable requires lowercase SHA256")
        if name == "log":
            re.compile(args[0])

def require_render_evidence(meta, log, shot, views):
    if shot is None:
        raise ValueError("render probe requires a fresh screenshot")
    header = b"P6\n320 240\n255\n"
    data = shot.read_bytes()
    if not data.startswith(header) or len(data) != len(header) + 320 * 240 * 3:
        raise ValueError("render probe requires a canonical 320x240 RGB screenshot")
    fields = ("rnd_us", "gpu_us", "tri", "tex", "tex_bytes", "pal", "mat", "rms",
              "record_peak", "record_total", "spills", "spill_us")
    pattern = re.compile(rf"render-measure: views={views}" +
                         "".join(fr" {name}=[0-9]+" for name in fields))
    rows = [line for line in log.splitlines() if line.startswith("render-measure:")]
    initial_frames = 1 if meta.get("hash_schema") in (20, 21, 22) else 2
    expected = int(meta["ticks"], 0) + initial_frames if meta["game"] else 1
    if len(rows) != expected or not all(pattern.fullmatch(line) for line in rows):
        raise ValueError(f"render probe requires {expected} complete render-measure records for {views} view(s)")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    group = ap.add_mutually_exclusive_group(required=True)
    group.add_argument("--host", action="store_true")
    group.add_argument("--ares", action="store_true")
    ap.add_argument("-j", type=int, default=1)
    ap.add_argument("--checks", type=pathlib.Path, action="append", default=[])
    ap.add_argument("--pose", type=pathlib.Path, help="first camera JSON (same level as the route)")
    views = ap.add_mutually_exclusive_group()
    views.add_argument("--split-camera", type=pathlib.Path, help="second camera JSON; one 320x120 half per camera")
    views.add_argument("--half-view", action="store_true", help="standalone top 320x120 view of a posed geometry scene")
    ap.add_argument("--render-measure", action="store_true", help="log raw per-frame profiler counters")
    ap.add_argument("scenes", nargs="+", type=pathlib.Path)
    ns = ap.parse_args()
    if ns.j != 1:
        ap.error("one serial build/run owner is required")
    ares_seconds = float(os.environ.get("ARES_TIMEOUT", "90")) if ns.ares else 0.0
    if ns.ares and (not math.isfinite(ares_seconds) or ares_seconds <= 0):
        raise ValueError("ARES_TIMEOUT must be positive finite seconds")
    checks = load_checks(ns.checks)
    build = pathlib.Path(os.environ["BUILD_DIR"]).resolve()
    mode = "ares-pak" if ns.ares and os.environ.get("ARES_EXPANSION_PAK") == "true" else "ares" if ns.ares else "host"
    scenes, names = [], set()
    for path in ns.scenes:
        path = path.resolve()
        meta, data = routec.parse(path)
        if (ns.pose or ns.split_camera or ns.half_view or ns.render_measure) and not meta.get("level"):
            ap.error("render options require a level route")
        if ns.half_view and meta["game"]:
            ap.error("--half-view requires a posed geometry route without game ticks")
        if ns.ares and meta["game"]:
            game2 = os.environ.get("TR64_GAME") == "2"
            if game2 != (meta["hash_schema"] in (20, 21, 22)):
                ap.error("Tomb Raider II cart gameplay requires TR64_GAME=2 and hash-schema 20, 21 or 22")
        validate_observables(meta, checks)
        if meta["scene"] in names:
            raise ValueError(f"duplicate scene artifact name: {meta['scene']}")
        names.add(meta["scene"])
        scenes.append((path, meta, data))
    for scene, meta, data in scenes:
        out = build / "scenes" / mode / meta["scene"]
        out.mkdir(parents=True, exist_ok=True)
        for pattern in ("ares-test-*.ppm", "boot.ppm", "engine.log", "wall.txt", "exit.txt"):
            for stale in out.glob(pattern):
                stale.unlink()
        (out / "scene.rbc").write_bytes(data)
        scene_build = build / "scene-builds" / meta["scene"]
        target = "rom" if ns.ares else "host"
        flags = []
        if ns.pose:
            flags.append(f"TR64_RENDER_SCENE={ns.pose.resolve()}")
        if ns.split_camera:
            flags.append(f"TR64_RENDER_SPLIT_CAMERA={ns.split_camera.resolve()}")
        if ns.half_view:
            flags.append("TR64_RENDER_HALF_VIEW=1")
        if ns.render_measure or ns.split_camera:
            flags.append("TR64_RENDER_MEASURE=1")
        command(["make", "--no-print-directory", f"BUILD_DIR={scene_build}", f"SCENE={scene}", *flags, target],
                out / "build.log", timeout=120)
        executable = scene_build / ("tr64.z64" if ns.ares else "host/tr64-host")
        if ns.host:
            command([executable, "--route", out / "scene.rbc", "--shot", out / "boot.ppm"], out / "engine.log", timeout=8)
            shot = out / "boot.ppm" if (out / "boot.ppm").is_file() else None
        else:
            expected_identity = guest_identity.expected_identity(scene_build)
            # Let the existing harness deadline and its TERM/KILL cleanup win.
            command([ROOT / "test/run/ares.sh", out, executable, out / "engine.log", str(ares_seconds)], out / "runner.log", timeout=ares_seconds + 10)
            shots = sorted(out.glob("ares-test-*.ppm"))
            if len(shots) > 1:
                raise ValueError("expected at most one fresh guest screenshot")
            shot = shots[0] if shots else None
        ctx = dict(meta=meta, scene=scene, mode=mode, cart=ns.ares, build=scene_build,
                   executable=executable, artifacts=out,
                   log=(out / "engine.log").read_text(errors="replace"), shot=shot, results={})
        if ns.ares:
            ctx["results"]["build_id"], ctx["results"]["config_id"] = guest_identity.require_identity(
                ctx["log"], expected_identity)
        if ns.render_measure or ns.split_camera:
            require_render_evidence(meta, ctx["log"], shot, 2 if ns.split_camera else 1)
        for name, args in meta["observables"]:
            checks[name](ctx, args)
        if shot is not None:
            ctx["results"]["raw-shot"] = hashlib.sha256(shot.read_bytes()).hexdigest()
        values = " ".join(f"{key}={value}" for key, value in ctx["results"].items())
        print(f"scene PASS {meta['scene']} {mode}: {values} artifact={shot or out}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, re.error, subprocess.TimeoutExpired) as error:
        raise SystemExit(f"scene FAIL: {error}")
