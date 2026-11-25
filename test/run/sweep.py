#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Original sweep driver; no OpenLara implementation code. Inputs and hashes:
# test/game/README.md (random alphabet, schema 3), test/oracle/README.md.
"""Whole-campaign cart sweep: every level of one pack, 600 ticks, ares at 4 MiB.

For each level and run number this
  1. draws a seeded script from the campaign's menu-free alphabet
     (test/game/random-inputs.py `generated`): run 1 starts where the level
     starts, runs 2 and 3 place Lara in a seeded sector of a seeded room;
  2. produces the reference hashes: the OpenLara oracle for a PC pack (host
     executable compared with it record by record), the host executable of the
     same pack for a PlayStation pack;
  3. builds and runs two carts through test/run/scene.sh --ares (ARES_EXPANSION_PAK
     false, sound on): a hash cart (schema 3, cart_actions.c for level
     transitions) that gives hashes, camera room and eye per frame, and a
     performance cart (test/snd/on.c: no hash, no verbose renderer log) that
     gives `scene-work` per frame;
  4. keeps a small JSON per run with guest and launcher identity evidence,
     a last-frame PNG for accepted runs, and a gzipped log for failed runs.
`report` turns the JSON into markdown tables and contact sheets.

Every cart mode shares a build tree. A resume rebuilds the intended cart and
validates the saved boot and launcher receipt without running ares again.
Use --force to replace a rejected run. Run from the repository root:
  rtk python3 test/run/sweep.py run --source pc --pack PACK --host HOST_EXE \
      --oracle ORACLE --build-root DIR --out DIR [--levels a,b] [--runs 1,2,3]
  rtk python3 test/run/sweep.py report --out DIR --md-dir DIR
"""
import argparse
import gzip
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
from PIL import Image
import subprocess
import sys
import time
sys.dont_write_bytecode = True
import guest_identity

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / "test/game"), str(ROOT / "tools/cook")]
import src_pc  # noqa: E402
import src_psx  # noqa: E402
from compare import validate  # noqa: E402
from stream import Run  # noqa: E402

_spec = importlib.util.spec_from_file_location("sweep_random_inputs", ROOT / "test/game/random-inputs.py")
random_inputs = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(random_inputs)

PC_DATA = Path("/tmp/tr64/data/pc")
PSX_DATA = Path("/tmp/tr64/data/psx/PSXDATA")
LEVELS = ["title", "gym", "level1", "level2", "level3a", "level3b", "level4", "level5", "level6",
          "level7a", "level7b", "level8a", "level8b", "level8c", "level10a", "level10b", "level10c"]
CUTS = ["cut1", "cut2", "cut3", "cut4"]
SOURCES = {
    "pc": dict(levels=LEVELS + CUTS, loader=src_pc.load, path=lambda n: PC_DATA / f"{n}.phd"),
    # The 17-level PlayStation pack has no cutscene levels; a 21-level pack may be swept with --levels.
    "psx": dict(levels=LEVELS, extra=CUTS, loader=src_psx.load, path=lambda n: PSX_DATA / f"{n.upper()}.PSX"),
}
FRAME_US = 33333
RUNS = {1: "start", 2: "placed", 3: "placed"}
SCENE = "sweep"
MODES = {"hash": ["test/game/cart_actions.c", "test/game/save-io.c"],
         "perf": ["test/game/cart_actions.c", "test/game/save-io.c", "test/snd/on.c"]}
POOLS = ("level", "state", "code", "audio", "gpu")


def digest(text):
    return hashlib.sha256(text.encode()).hexdigest()


def percentile(values, p):
    values = sorted(values)
    return values[math.ceil(len(values) * p / 100) - 1] if values else None


# ---------------------------------------------------------------- scripts

def has_hud(level):
    return level != "title" and not level.startswith("cut")


def draw_script(level, run):
    """Yield (ordinal, masks_text) candidates for one run in a fixed order."""
    rooms = len(level.rooms)
    seed = run
    ordinal = 1 if run == 1 else (run - 1) * rooms // 3 + 1
    while ordinal < rooms * 20 + 40:
        this = ordinal
        ordinal += 1
        if this % 4 == 0:  # the alphabet's zero-input drop case: not a script
            continue
        text = random_inputs.generated(level, this, seed)
        if text is None:
            continue
        lines = text.splitlines()
        if run == 1:
            lines = lines[1:]
        words = [w for w in lines if not w.startswith("start")]
        events = 1 + sum(1 for a, b in zip(words, words[1:]) if a != b)
        if events > 64:  # a scene admits 64 input events
            continue
        yield this, "\n".join(lines) + "\n"


def pose_for(level, masks_text):
    """Bind-time frame: it only names the level; the follow camera replaces it."""
    first = masks_text.splitlines()[0].split()
    if first and first[0] == "start":
        room, x, y, z = map(int, first[1:5])
    else:
        items = level.data["items"]
        pick = next((i for i in items if i["type"] == 0), items[0] if items else None)
        if pick is None:
            raise ValueError(f"{level.name}: no item to pose the bind frame")
        room, x, y, z = pick["room"], pick["x"], pick["y"], pick["z"]
    return dict(room=room, position=[x, y - 768, z], target=[x, y - 768, z + 100])


# -------------------------------------------------------------- references

def read_trace(command, option, name):
    run = Run(command, option)
    rows = []
    try:
        for line in run.trace:
            rows.append(line)
    finally:
        code, log = run.finish()
    return rows, code, log


def reference(cfg, level_name, masks_path, ticks):
    """Return (hashes, facts). hashes are the reference per-tick state hashes."""
    host_cmd = [str(cfg["host"]), "--game-level", level_name, "--game-input", str(masks_path),
                "--game-ticks", str(ticks), "--game-trace-only", "--game-trace-schema", "3"]
    host_rows, host_code, host_log = read_trace(host_cmd, "--game-trace", "host")
    facts = dict(host_records=len(host_rows), host_exit=host_code)
    if host_code:
        facts["host_stop"] = (re.search(r"tr64 fatal: ([^\n]+)", host_log) or re.search(r"([^\n]+)\Z", host_log.strip() or "?"))[1][:200]
    host_hashes = []
    for tick, raw in enumerate(host_rows):
        validate(json.loads(raw), raw.decode("ascii"), tick, "sweep host reference", schema=3)
        host_hashes.append(json.loads(raw)["hash"])
    if cfg["oracle"] is None:
        facts["reference"] = "host (same pack)"
        return host_hashes, facts, host_rows
    ref = [cfg["oracle"], "--level", str(cfg["source"]["path"](level_name)), "--input", str(masks_path),
           "--ticks", str(ticks), "--trace-schema", "3"]
    rows, code, log = read_trace(ref, "--out", "oracle")
    facts.update(reference="oracle", oracle_records=len(rows), oracle_exit=code)
    if code and not rows:
        facts["oracle_stop"] = log.strip()[-200:]
        return [], facts, host_rows
    hashes = [json.loads(raw)["hash"] for raw in rows]
    n = min(len(hashes), len(host_hashes))
    bad = next((i for i in range(n) if hashes[i] != host_hashes[i]), None)
    if bad is not None:
        facts["host_vs_oracle"] = f"first difference at tick {bad}"
    elif len(host_hashes) != len(hashes):
        facts["host_vs_oracle"] = f"host stopped after {len(host_hashes)-1} ticks, oracle ran {len(hashes)-1}"
    else:
        facts["host_vs_oracle"] = "equal"
    return hashes, facts, host_rows


# ------------------------------------------------------------------- carts

def cart_env(cfg, mode, pose_json):
    env = dict(os.environ, BUILD_DIR=str(cfg["build_root"] / mode), TR64_LEVEL_PACK=str(cfg["pack"]),
               TR64_SCENE_TEST_SOURCES=" ".join(MODES[mode]), TR64_RENDER_SCENE=str(pose_json),
               ARES_EXPANSION_PAK="false", TR64_TEST="0", TR64_PLAY="0",
               ARES_TIMEOUT=str(cfg["ares_seconds"]), TMPDIR=str(cfg["tmp"]))
    return env


def prepare_cart(cfg, mode, level_name, masks_text, place, pose, hud):
    root = cfg["build_root"] / mode
    scene_build = root / "scene-builds" / SCENE
    (scene_build / "generated").mkdir(parents=True, exist_ok=True)
    (scene_build / "generated/cart_scene.h").write_text(
        "/* Generated by test/run/sweep.py. */\n#define TR64_CART_SAVE_TICK UINT32_MAX\n"
        "#define TR64_CART_LOAD_TICK UINT32_MAX\n")
    pose_json = root / "sweep.scene.json"
    meta = dict(level=level_name.upper(), camera=pose)
    if hud:
        meta["hud"] = True
    pose_json.write_text(json.dumps(meta) + "\n")
    values = masks_text.split()
    if values and values[0] == "start":
        values = values[6:]
    lines = ["# SPDX-License-Identifier: MIT", f"scene {SCENE}", "start boot", f"load level {level_name}",
             "game", f"ticks {len(values)}", "seed 1", "hash-schema 3"]
    if place:
        lines.append("place " + " ".join(map(str, place)))
    previous = None
    for tick, value in enumerate(values):
        if value != previous:
            lines.append(f"at {tick} pad {int(value)} 0 0")
            previous = value
    lines += ["observe pools", "observe placement"]
    route = root / "sweep.route"
    route.write_text("\n".join(lines) + "\n")
    return cart_env(cfg, mode, pose_json), route, scene_build


def build_cart(mode, env, route, scene_build):
    # Cold or incremental, this make is the scene runner's own; running it first
    # keeps the runner's 120 s build deadline for a no-op.
    build = subprocess.run(["make", "--no-print-directory", f"BUILD_DIR={scene_build}", f"SCENE={route}", "rom", "-j2"],
                           cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=1800)
    if build.returncode:
        raise RuntimeError(f"{mode} cart build failed:\n" + build.stdout.decode(errors="replace")[-1500:])
    rom = scene_build / "tr64.z64"
    with rom.open("rb") as stream:
        sha = hashlib.file_digest(stream, "sha256").hexdigest()
    return guest_identity.expected_identity(scene_build), sha


def verify_cart_evidence(evidence, expected, rom_sha):
    if not re.fullmatch(guest_identity.SHA256, rom_sha) or evidence.get("rom_sha256") != rom_sha:
        raise ValueError("ROM SHA-256 is absent or differs from the intended cart")
    if guest_identity.rom_receipt(evidence.get("launcher", "")) != rom_sha:
        raise ValueError("launcher ROM SHA-256 differs from the intended cart")
    guest_identity.require_identity(evidence.get("guest_boot", ""), expected)
    if (evidence.get("build_id"), evidence.get("config_id")) != expected:
        raise ValueError("stored guest build or configuration identity differs")


def run_cart(cfg, mode, env, route, scene_build):
    expected, rom_sha = build_cart(mode, env, route, scene_build)
    out = scene_build.parent.parent / "scenes/ares" / SCENE
    receipt = out / "ares-host.txt"
    receipt.unlink(missing_ok=True)
    start = time.time()
    scene = subprocess.run([str(ROOT / "test/run/scene.sh"), "--ares", str(route)], cwd=ROOT, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=cfg["ares_seconds"] + 600)
    tail = scene.stdout.decode(errors="replace").strip().splitlines()
    verdict = tail[-1][:400] if tail else ""
    log_path = out / "engine.log"
    text = log_path.read_text(errors="replace") if log_path.is_file() else ""
    shots = sorted(out.glob("ares-test-*.ppm"))
    observed, _ = guest_identity.build_identity(text, expected)
    evidence = dict(build_id=observed[0], config_id=observed[1], rom_sha256=rom_sha,
                    guest_boot="".join(guest_identity.records(text, "cart: build_id=")),
                    launcher=receipt.read_text() if receipt.is_file() else "")
    code = scene.returncode
    if not code:
        try:
            verify_cart_evidence(evidence, expected, rom_sha)
        except ValueError as error:
            code, verdict = 1, str(error)
    return dict(exit=code, verdict=verdict, text=text, shot=shots[-1] if len(shots) == 1 else None,
                shots=len(shots), wall_s=round(time.time() - start, 1), out=out, identity=evidence)


def parse_log(text):
    r = {}
    fatal = re.search(r"^tr64 fatal: ([^\n]*)", text, re.M)
    r["fatal"] = fatal[1] if fatal else None
    ctx = re.search(r"^fatal-context: ([^\n]*)", text, re.M)
    r["fatal_context"] = ctx[1] if ctx and fatal else None
    r["hashes"] = {int(t): h for t, h in re.findall(r"^game-tick: tick=(\d+) hash=([0-9a-f]{8}) tick_us=\d+ hash_schema=3$", text, re.M)}
    r["tick_us"] = [int(u) for u in re.findall(r"^game-tick: tick=\d+ .*?tick_us=(\d+)", text, re.M)]
    work = []
    for m in re.finditer(r"^scene-work: tick=(\d+) total_us=(\d+) wall_us=(\d+) diagnostics_us=(\d+) pacing_us=(\d+) "
                         r"game_us=(\d+) audio_us=(\d+)(?: room=(\d+))?$", text, re.M):
        work.append(dict(tick=int(m[1]), total=int(m[2]), game=int(m[6]), audio=int(m[7]),
                         room=None if m[8] is None else int(m[8])))
    r["work"] = work
    r["render"] = [dict(room=int(a), eye=[int(b), int(c), int(d)], done=int(e)) for a, b, c, d, e in
                   re.findall(r"^game-render: room=(\d+) eye=(-?\d+),(-?\d+),(-?\d+) .*?done_us=(\d+)", text, re.M)]
    for w in work:  # the hash cart's scene-work has no room: the same tick's frame has it
        if w["room"] is None and w["tick"] < len(r["render"]):
            w["room"] = r["render"][w["tick"]]["room"]
    end = re.search(r"^scene: ticks=(\d+) state=", text, re.M)
    r["scene_ticks"] = int(end[1]) if end else None
    pools = {}
    for m in re.finditer(r"^pool: (\w+) base=([0-9a-f]+) size=(\d+) used=(\d+) peak=(\d+) kind=(\d+)$", text, re.M):
        pools[m[1]] = dict(size=int(m[3]), used=int(m[4]), peak=int(m[5]))
    r["pools"] = pools
    audio = re.findall(r"^audio: (.*)$", text, re.M)
    r["audio"] = {k: int(v) for k, v in re.findall(r"(\w+)=(\d+)", audio[-1])} if audio else None
    out = [l for l in re.findall(r"^audio-output: cart (.*)$", text, re.M) if "underruns" in l]
    r["audio_output"] = {k: int(v) for k, v in re.findall(r"(\w+)=(\d+)", out[-1])} if out else None
    r["log_dropped"] = sum(int(n) for n in re.findall(r"^log: (\d+) bytes dropped$", text, re.M))
    notes = {}
    for line in re.findall(r"^\[(?:unusual|WARN|ERROR)\][^\n]*", text, re.M):
        if "Stalled compile" in line:
            continue
        notes[line[:120]] = notes.get(line[:120], 0) + 1
    r["ares_notes"] = notes
    r["level_loads"] = re.findall(r"^scene: load level (\w+)", text, re.M)
    r["transition_ticks"] = [int(t) for t in re.findall(r"^game-service: tick=(\d+) transition ", text, re.M)]
    r["transitions"] = len(r["transition_ticks"])
    return r


def work_summary(work, load_ticks):
    """Percentiles over every frame except the ones that carry a level load."""
    loads = [dict(tick=w["tick"], total_us=w["total"]) for w in work if w["tick"] in load_ticks]
    work = [w for w in work if w["tick"] not in load_ticks]
    t = [w["total"] for w in work]
    if not t:
        return None
    worst = max(work, key=lambda w: w["total"])
    top = sorted(work, key=lambda w: -w["total"])[:5]
    return dict(frames=len(t), mean=round(sum(t) / len(t)), p50=percentile(t, 50), p90=percentile(t, 90),
                p99=percentile(t, 99), max=max(t), over_33ms=sum(1 for v in t if v > FRAME_US),
                worst_tick=worst["tick"], worst_room=worst["room"], load_frames=loads,
                top=[(w["tick"], w["total"], w["room"]) for w in top],
                game_p99=percentile([w["game"] for w in work], 99), game_max=max(w["game"] for w in work),
                audio_max=max(w["audio"] for w in work))


def compare_hashes(observed, expected, ticks):
    """Observed {tick: hash} against reference list; returns (completed, verdict)."""
    done = 0
    for tick in range(ticks + 1):
        if tick not in observed:
            return done, f"no cart record for tick {tick}" if tick < len(expected) else "equal"
        if tick >= len(expected):
            return done, f"cart ran past reference at tick {tick}"
        if observed[tick] != expected[tick]:
            return done, f"first difference at tick {tick}: expected {expected[tick]} cart {observed[tick]}"
        done += 1
    return done, "equal"


def pool_summary(pools):
    return {name: dict(peak=pools[name]["peak"], size=pools[name]["size"]) for name in POOLS if name in pools}


def pool_flags(pools):
    flags = []
    for name, row in pools.items():
        if name != "image" and row["peak"] > row["size"]:
            flags.append(f"{name} peak {row['peak']} over {row['size']}")
    return flags


def summarize_cart(result, parsed, expected, ticks, mode):
    s = dict(exit=result["exit"], verdict=result["verdict"], wall_s=result["wall_s"], fatal=parsed["fatal"],
             fatal_context=parsed["fatal_context"], scene_ticks=parsed["scene_ticks"], pools=pool_summary(parsed["pools"]),
             pool_flags=pool_flags(parsed["pools"]), audio=parsed["audio"], audio_output=parsed["audio_output"],
             log_dropped=parsed["log_dropped"], ares_notes=parsed["ares_notes"], transitions=parsed["transitions"],
             level_loads=parsed["level_loads"], tick_us_max=max(parsed["tick_us"]) if parsed["tick_us"] else None,
             transition_ticks=parsed["transition_ticks"], work=work_summary(parsed["work"], set(parsed["transition_ticks"])))
    s["identity"] = result["identity"]
    if mode == "hash":
        done, verdict = compare_hashes(parsed["hashes"], expected, ticks)
        s.update(hash_records=len(parsed["hashes"]), hash_equal_to_tick=done - 1, hash_verdict=verdict)
    return s


def run_one(cfg, level_name, run, cached=None):
    level = cfg["source"]["loader"](cfg["source"]["path"](level_name))
    kind = RUNS[run]
    tmp = cfg["tmp"]
    chosen = None
    for ordinal, text in draw_script(level, run):
        masks = tmp / f"{cfg['name']}-{level_name}-r{run}.masks"
        masks.write_text(text)
        values = text.split()
        place = None
        if values[0] == "start":
            place = [int(v) for v in values[1:6]]
            values = values[6:]
        ticks = len(values)
        hashes, facts, host_rows = reference(cfg, level_name, masks, ticks)
        if not host_rows and "host_stop" in facts and run != 1:
            continue  # both programs refuse this placement: take the next ordinal
        chosen = (ordinal, text, place, ticks, hashes, facts)
        break
    if chosen is None:
        raise ValueError(f"{level_name} run {run}: no admissible script")
    ordinal, text, place, ticks, hashes, facts = chosen
    pose = pose_for(level, text)
    hud = has_hud(level_name)
    result = dict(source=cfg["name"], level=level_name, run=run, kind=kind, seed=run, ordinal=ordinal, place=place,
                  ticks=ticks, masks_sha256=digest(text), reference=facts, hud=hud, pack_sha256=cfg["pack_sha256"])
    # Facts such as record counts are diagnostic; bind the entire ordered reference.
    result["reference_sha256"] = digest("\n".join(hashes))
    cart = {}
    parsed_by_mode = {}
    modes = ["hash"] + (["perf"] if run in cfg["perf_runs"] else [])
    if cached is not None:
        if cached.get("reference_sha256") != result["reference_sha256"]:
            raise ValueError("cached reference hashes differ from the recomputed reference")
        if any(cached.get(k) != v for k, v in result.items()) or set(cached.get("cart", ())) != set(modes):
            raise ValueError("cached inputs or cart modes differ")
        notes = row_status(cached)
        if notes:
            raise ValueError("; ".join(notes))
        for mode in modes:
            env, route, scene_build = prepare_cart(cfg, mode, level_name, text, place, pose, hud)
            expected, rom_sha = build_cart(mode, env, route, scene_build)
            verify_cart_evidence(cached["cart"][mode]["identity"], expected, rom_sha)
        return cached
    hash_shot = None
    for mode in modes:
        env, route, scene_build = prepare_cart(cfg, mode, level_name, text, place, pose, hud)
        for attempt in range(3):
            # The harness deadline includes the wait for one of the two shared ares
            # slots; on a loaded host it says nothing about the cart. Run it again.
            res = run_cart(cfg, mode, env, route, scene_build)
            if "command deadline" not in res["verdict"]:
                break
        parsed = parse_log(res["text"])
        cart[mode] = summarize_cart(res, parsed, hashes, ticks, mode)
        cart[mode]["shots"] = res["shots"]
        if res["shot"]:
            cart[mode]["shot_sha256"] = hashlib.sha256(res["shot"].read_bytes()).hexdigest()
            if mode == "hash":
                hash_shot = res["shot"]
            else:
                res["shot"].unlink()
        parsed_by_mode[mode] = parsed
        if cart[mode]["exit"] or cart[mode]["fatal"] or cart[mode].get("hash_verdict", "equal") != "equal":
            gz = cfg["out"] / "logs" / f"{cfg['name']}-{level_name}-r{run}-{mode}.log.gz"
            gz.parent.mkdir(parents=True, exist_ok=True)
            noisy = ("core-table:", "render-phases:", "sprites:", "texture-animation:", "game-tick:", "scene-work:")
            keep = "\n".join(l for l in res["text"].splitlines() if not l.startswith(noisy))
            with gzip.open(gz, "wt") as f:
                f.write(keep[-300000:] + "\n")
        (res["out"] / "engine.log").unlink(missing_ok=True)
    result["cart"] = cart
    png = cfg["out"] / "shots" / f"{cfg['name']}-{level_name}-r{run}.png"
    if row_status(result):
        png.unlink(missing_ok=True)
        if hash_shot:
            hash_shot.unlink()
        return result
    if hash_shot:
        png.parent.mkdir(parents=True, exist_ok=True)
        with Image.open(hash_shot) as picture:
            picture.convert("RGB").save(png, optimize=True)
        hash_shot.unlink()
    # Worst frame: the clean performance cart when there is one, else the hash cart.
    # Both give the room per frame; the eye is the hash cart's `game-render` line
    # for the same tick (the game state is identical, ticks being hash-equal).
    primary = "perf" if "perf" in cart else "hash"
    summary = cart[primary]["work"]
    if summary:
        tick = summary["worst_tick"]
        renders = parsed_by_mode["hash"]["render"]
        eye = renders[tick]["eye"] if tick < len(renders) else None
        result["worst_frame"] = dict(tick=tick, total_us=summary["max"], room=summary["worst_room"], eye=eye,
                                     source=primary)
    if "perf" in parsed_by_mode and not (cart["hash"]["fatal"] or cart["perf"]["fatal"]):
        a, b = parsed_by_mode["hash"]["render"], parsed_by_mode["perf"]["work"]
        result["room_agrees"] = bool(a and b and all(a[w["tick"]]["room"] == w["room"] for w in b if w["tick"] < len(a)))
        result["shot_equal"] = cart["hash"].get("shot_sha256") == cart["perf"].get("shot_sha256")
    return result


def command_run(args):
    source = SOURCES[args.source]
    cfg = dict(name=args.source, source=source, host=args.host.resolve(), pack=args.pack.resolve(),
               oracle=args.oracle.resolve() if args.oracle else None, build_root=args.build_root.resolve(),
               out=args.out.resolve(), tmp=Path(os.environ.get("TMPDIR", "/tmp/tr64/tmp/Sweep")).resolve(),
               ares_seconds=args.ares_seconds, perf_runs=set(map(int, args.perf_runs.split(","))) if args.perf_runs else set(),
               pack_sha256=hashlib.sha256(args.pack.read_bytes()).hexdigest())
    if (args.source == "pc") != (cfg["oracle"] is not None):
        raise SystemExit("a PC sweep requires --oracle; a PlayStation sweep has none")
    levels = args.levels.split(",") if args.levels else source["levels"]
    for name in levels:
        if name not in source["levels"] + source.get("extra", []):
            raise SystemExit(f"{args.source} pack has no level {name}")
    (cfg["out"] / "runs").mkdir(parents=True, exist_ok=True)
    failed = False
    for run in map(int, args.runs.split(",")):
        for name in levels:
            if run != 1 and (name == "title" or name.startswith("cut")):  # no game camera / no Lara to place
                continue
            path = cfg["out"] / "runs" / f"{args.source}-{name}-r{run}.json"
            if path.exists() and not args.force:
                cached = None
                try:
                    cached = json.loads(path.read_text())
                    if not isinstance(cached, dict):
                        raise ValueError("cached run must be a JSON object")
                    run_one(cfg, name, run, cached)
                except (ValueError, KeyError, TypeError, OSError, RuntimeError) as error:
                    failed = True
                    cached = cached if isinstance(cached, dict) else {}
                    cached.update(source=args.source, level=name, run=run, rejected=str(error))
                    path.write_text(json.dumps(cached, indent=1) + "\n")
                    print(f"sweep REJECTED {path}: {error}; use --force to rerun", file=sys.stderr)
                else:
                    print(f"sweep reused {path} (guest identities and ROM SHA-256 verified)", flush=True)
                continue
            begin = time.time()
            result = run_one(cfg, name, run)
            result["elapsed_s"] = round(time.time() - begin, 1)
            path.write_text(json.dumps(result, indent=1, sort_keys=True) + "\n")
            notes = row_status(result)
            if notes:
                failed = True
                print(f"sweep FAIL {path}: {'; '.join(notes)}", file=sys.stderr)
            summary = {m: (c["verdict"][:50], c.get("hash_verdict"), c["work"] and c["work"]["p99"]) for m, c in result["cart"].items()}
            print(f"sweep {args.source} {name} r{run} {result['elapsed_s']}s {summary}", flush=True)
    return int(failed)


# ----------------------------------------------------------------- report

def fmt_ms(us):
    return "-" if us is None else f"{us/1000:.1f}"


def row_status(r):
    if "rejected" in r:
        return [r["rejected"]]
    if "hash" not in r.get("cart", {}):
        return ["missing hash cart"]
    notes = []
    for mode, c in r["cart"].items():
        if c["fatal"]:
            notes.append(f"{mode}: FATAL {c['fatal']}")
        elif c["exit"]:
            notes.append(f"{mode}: {c['verdict']}")
        else:
            identity = c.get("identity", {})
            try:
                verify_cart_evidence(identity, (identity.get("build_id"), identity.get("config_id")),
                                     identity.get("rom_sha256", ""))
            except (TypeError, ValueError) as error:
                notes.append(f"{mode}: {error}")
        if mode == "hash" and c.get("hash_verdict") != "equal":
            notes.append(f"{mode}: {c.get('hash_verdict', 'missing hash comparison')}")
        if c["pool_flags"]:
            notes.append(f"{mode}: " + "; ".join(c["pool_flags"]))
        if c["scene_ticks"] != r["ticks"] and not c["fatal"]:
            notes.append(f"{mode}: ran {c['scene_ticks']} of {r['ticks']} ticks")
    reference = r.get("reference", {}).get("host_vs_oracle", "equal")
    if reference != "equal":
        notes.append(f"host vs oracle: {reference}")
    return notes


def hash_cell(r):
    c = r["cart"].get("hash")
    if c is None:
        return "-"
    n = c["hash_equal_to_tick"]
    if c["hash_verdict"] == "equal":
        return f"{n}/{r['ticks']} equal"
    return f"{max(n,0)} then {c['hash_verdict'].split(':')[0]}"


def table(rows, source):
    ref = "oracle" if source == "pc" else "host build of the same pack"
    out = [f"| level | run | hashes vs {ref} | timing cart | p50 | p90 | p99 | max | >33.3 ms | worst frame (tick room eye) | "
           "level | state | code | audio | gpu | snd drop/unavail/underrun | notes |",
           "|" + "---|" * 17]
    for r in rows:
        perf = (r["cart"].get("perf") or {})
        hc = r["cart"].get("hash") or {}
        timing = "perf" if perf.get("work") else "hash"
        w = (perf.get("work") or hc.get("work") or {})

        pools = (hc.get("pools") or perf.get("pools") or {})
        pool = lambda n: f"{pools[n]['peak']//1024 if pools[n]['peak']%1024==0 else round(pools[n]['peak']/1024,1)}/{pools[n]['size']//1024}" if n in pools else "-"
        wf = r.get("worst_frame")
        worst = f"{wf['tick']} r{wf['room']} {tuple(wf['eye']) if wf['eye'] else '?'}" if wf else "-"
        audio = perf.get("audio") or hc.get("audio") or {}
        out_ = perf.get("audio_output") or hc.get("audio_output") or {}
        snd = (f"{audio.get('dropped','-')}/{audio.get('unavailable','-')}/{max(audio.get('underruns',0), out_.get('underruns',0)) if audio else '-'}")
        notes = []
        if audio.get("clipped"):
            notes.append(f"{audio['clipped']} clipped mixer samples")
        if perf and hc:
            under = lambda c: max((c["audio"] or {}).get("underruns", 0), (c["audio_output"] or {}).get("underruns", 0))
            if under(hc) != under(perf):
                notes.append(f"underruns hash cart {under(hc)}, performance cart {under(perf)} (the hash cart logs every frame)")
        if r.get("room_agrees") is False:
            notes.append("hash/perf room differ")
        if r.get("shot_equal") is False:
            notes.append("hash/perf last frame differ")
        for mode, c in r["cart"].items():
            if c["ares_notes"]:
                notes.append(f"{mode}: ares note x{sum(c['ares_notes'].values())}")
            if c["log_dropped"]:
                notes.append(f"{mode}: log dropped {c['log_dropped']} B")
        for load in (w.get("load_frames") or []):
            notes.append(f"level load in frame {load['tick']} ({fmt_ms(load['total_us'])} ms, not in the percentiles)")
        out.append("| " + " | ".join([r["level"], f"{r['run']} {r['kind']}", hash_cell(r), timing, fmt_ms(w.get("p50")), fmt_ms(w.get("p90")),
                                      fmt_ms(w.get("p99")), fmt_ms(w.get("max")), str(w.get("over_33ms", "-")), worst,
                                      pool("level"), pool("state"), pool("code"), pool("audio"), pool("gpu"), snd,
                                      "; ".join(notes) or "-"]) + " |")
    return "\n".join(out)


def timing_of(r):
    c = r["cart"].get("perf") or r["cart"].get("hash") or {}
    return c.get("work")


def extras(rows):
    """Cross-check accepted carts and their worst frames, as markdown."""
    both = [r for r in rows if "perf" in r["cart"] and r["cart"]["perf"]["work"] and r["cart"]["hash"]["work"]]
    out = []
    if both:
        out += ["", "## Hash cart against performance cart (same script, same level)", "",
                "| level | run | p50 hash/perf ms | p99 hash/perf ms | max hash/perf ms | same last frame | same room per frame |", "|---|---|---|---|---|---|---|"]
        for r in both:
            h, p = r["cart"]["hash"]["work"], r["cart"]["perf"]["work"]
            out.append(f"| {r['level']} | {r['run']} | {fmt_ms(h['p50'])}/{fmt_ms(p['p50'])} | {fmt_ms(h['p99'])}/{fmt_ms(p['p99'])} | "
                       f"{fmt_ms(h['max'])}/{fmt_ms(p['max'])} | {r.get('shot_equal')} | {r.get('room_agrees')} |")
    timed = sorted((r for r in rows if timing_of(r)), key=lambda r: -timing_of(r)["p99"])
    out += ["", "## Worst runs by p99", "", "| level | run | p99 ms | max ms | worst tick | room | eye |", "|---|---|---|---|---|---|---|"]
    for r in timed[:5]:
        w, f = timing_of(r), r["worst_frame"]
        out.append(f"| {r['level']} | {r['run']} {r['kind']} | {fmt_ms(w['p99'])} | {fmt_ms(w['max'])} | {f['tick']} | {f['room']} | {tuple(f['eye']) if f['eye'] else '?'} |")
    return "\n".join(out)


def sheets(out_dir, md_dir, source, rows, run):
    from PIL import Image, ImageDraw
    tiles = []
    for r in rows:
        png = out_dir / "shots" / f"{source}-{r['level']}-r{r['run']}.png"
        if png.is_file():
            where = f" room {r['place'][0]}" if r["place"] else " start"
            tiles.append((f"{source} {r['level']} run{r['run']}{where} t{r['ticks']}", Image.open(png)))
    per = 9
    written = []
    for page in range(0, len(tiles), per):
        chunk = tiles[page:page + per]
        sheet = Image.new("RGB", (3 * 320, ((len(chunk) + 2) // 3) * 256), (32, 32, 32))
        draw = ImageDraw.Draw(sheet)
        for i, (label, tile) in enumerate(chunk):
            x, y = (i % 3) * 320, (i // 3) * 256
            sheet.paste(tile, (x, y + 16))
            draw.text((x + 4, y + 2), label, fill=(255, 255, 0))
        name = md_dir / f"{source}-run{run}-sheet-{page // per + 1}.png"
        sheet.save(name, optimize=True)
        written.append(name)
    return written


def baseline_table(rows, baseline_rows):
    """Frame time of the same script and level on an earlier build."""
    old = {(r["level"], r["run"]): r for r in baseline_rows}
    out = ["", "## Against the baseline build (same level, same run number)", "",
           "| level | run | script | p50 ms before/after | p99 ms before/after | max ms before/after |", "|---|---|---|---|---|---|"]
    for r in rows:
        b, w = old.get((r["level"], r["run"])), timing_of(r)
        was = b and timing_of(b)
        if not (w and was):
            continue
        same = "same script" if b["masks_sha256"] == r["masks_sha256"] else "different script"
        out.append(f"| {r['level']} | {r['run']} | {same} | {fmt_ms(was['p50'])}/{fmt_ms(w['p50'])} | "
                   f"{fmt_ms(was['p99'])}/{fmt_ms(w['p99'])} | {fmt_ms(was['max'])}/{fmt_ms(w['max'])} |")
    return "\n".join(out)


def command_report(args):
    runs = [json.loads(p.read_text()) for p in sorted((args.out / "runs").glob("*.json"))]
    baseline = [json.loads(p.read_text()) for p in sorted((args.baseline / "runs").glob("*.json"))] if args.baseline else []
    failed = False
    args.md_dir.mkdir(parents=True, exist_ok=True)
    for source in SOURCES:
        candidates = [r for r in runs if r["source"] == source]
        if not candidates:
            continue
        rows, rejected = [], []
        for r in candidates:
            notes = row_status(r)
            if notes:
                rejected.append(f"- {r['level']} run {r['run']}: {'; '.join(notes)}")
            else:
                rows.append(r)
        old_rows = []
        for old in baseline:
            if old["source"] != source:
                continue
            notes = row_status(old)
            if notes:
                rejected.append(f"- baseline {old['level']} run {old['run']}: {'; '.join(notes)}")
            else:
                old_rows.append(old)
        failed |= bool(rejected)
        order = {n: i for i, n in enumerate(SOURCES[source]["levels"] + SOURCES[source].get("extra", []))}
        rows.sort(key=lambda r: (r["run"], order[r["level"]]))
        text = [f"# Cart sweep, {source} pack, 4 MiB, 600 ticks\n", table(rows, source), extras(rows)]
        if old_rows:
            text.append(baseline_table(rows, old_rows))
        text.extend(["", "## Rejected runs", "", *(rejected or ["none"]), ""])
        (args.md_dir / f"{source}-table.md").write_text("\n".join(text))
        for old_sheet in args.md_dir.glob(f"{source}-run*-sheet-*.png"):
            old_sheet.unlink()
        for run in sorted({r["run"] for r in rows}):
            for name in sheets(args.out, args.md_dir, source, [r for r in rows if r["run"] == run], run):
                print("wrote", name)
    return int(failed)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="command", required=True)
    run = sub.add_parser("run")
    run.add_argument("--source", choices=sorted(SOURCES), required=True)
    run.add_argument("--pack", type=Path, required=True)
    run.add_argument("--host", type=Path, required=True)
    run.add_argument("--oracle", type=Path)
    run.add_argument("--build-root", type=Path, required=True)
    run.add_argument("--out", type=Path, required=True)
    run.add_argument("--levels")
    run.add_argument("--runs", default="1")
    run.add_argument("--perf-runs", default="1", help="run numbers that also get the performance cart")
    run.add_argument("--ares-seconds", type=int, default=3000)
    run.add_argument("--force", action="store_true")
    rep = sub.add_parser("report")
    rep.add_argument("--out", type=Path, required=True)
    rep.add_argument("--md-dir", type=Path, required=True)
    rep.add_argument("--baseline", type=Path, help="an earlier sweep's output directory, for a before/after table")
    args = ap.parse_args()
    return command_run(args) if args.command == "run" else command_report(args)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        raise SystemExit(f"sweep FAIL: {error}")
