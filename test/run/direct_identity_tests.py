# SPDX-License-Identifier: MIT
"""Drive the direct verdict entry points with only build/emulator I/O replaced."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import runpy
import subprocess
import sys
from types import SimpleNamespace
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.append(str(ROOT / "tools/cook"))
from pool_rows import pool_rows
BUILD, CONFIG = "0500E430EC0B", "ab" * 32
BOOT = f"cart: build_id={BUILD} config_id={CONFIG}\n"
PLAY = f"play: build_id={BUILD} config_id={CONFIG} mode=live\n"
IDENTITY = f'#define TR64_TEST 0\n#define TR64_BUILD_ID "{BUILD}"\n#define TR64_CONFIG_ID "{CONFIG}"\n'
PPM = b"P6\n320 240\n255\n"
ROM = bytes(63) + b"\x30"
# Recorded by ares for the M0 unloaded-code-unit negative.
FATAL_EXIT = "TESTHARNESS exit 1 (arg1=0x014000f0 arg2=0x00000002)\n"


def header(build):
    path = build / "generated/cart_identity.h"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(IDENTITY)
    path.with_name("build_id.h").write_text(IDENTITY)
    return path


def module(name):
    spec = importlib.util.spec_from_file_location("direct_fixture", ROOT / "test" / name)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def reload_log():
    text = "level-reload PASS loads=3 repeated_hash=aaaaaaaa distinct_gym=bbbbbbbb planners=3\n"
    for i, (name, hash_) in enumerate((("level1", "aaaaaaaa"), ("gym", "bbbbbbbb"), ("level1", "aaaaaaaa"))):
        text += f"level-reload: loaded={name} ordinal={i} hash={hash_}\n"
        text += f"level-reload: released={name} level=0 state=0 code=0 io=0\n"
    base = 0x80000000
    for name, size, kind in pool_rows():
        text += f"pool: {name} base={base:x} size={size} used=0 peak=0 kind={('IMAGE', 'FIXED', 'CARVE').index(kind)}\n"
        base += size if kind != "IMAGE" else 16
    text += "libheap: used=0 peak=0 calls=0 post_boot_allocations=0 frozen=1\n"
    return text + ("placement: fb0=0\nplacement: fb1=0\nplacement: z=0\nplacement: stack=0 "
                   "hardware_rdram=4194304 sdk_rdram=4194304 guard_checked=0 guard_changed=0\n")


def fatal_picture():
    pixels = bytearray(320 * 240 * 3)
    pixels[:60] = b"\xff\x00\x00" * 20
    pixels[60:360] = b"\xff\xff\xff" * 100
    for column, glyph in enumerate((31207, 23213, 31015, 31207, 31716, 29842, 29847, 31599, 24445, 0, 23497)):
        for row in range(5):
            for x in range(3):
                if glyph & (1 << (14 - row * 3 - x)):
                    at = ((16 + row) * 320 + 4 + column * 4 + x) * 3
                    pixels[at:at + 3] = b"\xff\xff\xff"
    return PPM + pixels


BURST = ("log-burst: loaded=24 called=24 frames=24 peak=128 lost=0\n"
         "log-burst: released=24 frames=0 peak=128 lost=0\n"
         "code: frames used=24 free=0 tlb_entries=25 wired=1\n"
         "code: frames used=0 free=24 tlb_entries=1 wired=1\n")


def run_synthetic(lane, root, marker, affected):
    exe, out, build = root / "relocated.z64", root / "out", root / "build"
    exe.write_bytes(ROM)
    identity_path = header(root / "independent")
    data = {"run/level-reload.py": ["--ares"], "run/log-burst-tests.py": ["--ares"],
            "run/fatal-tests.py": ["--ares", "--only", "nested", "--no-qr"],
            "run/ares-binary-tests.py": ["--rom", exe, "--identity-header", identity_path],
            "ui/save-cart-test.py": ["--ares", exe, "--host", exe, "--pack", exe, "--build", build, "--out", out],
            "ui/menu-flow-test.py": ["--ares", exe, "--rom", exe, "--identity-header", identity_path, "--out", out]}

    def build_fixture(argv):
        directory = Path(next(arg.split("=", 1)[1] for arg in argv if arg.startswith("BUILD_DIR=")))
        header(directory)
        (directory / "tr64.z64").write_bytes(ROM)
        (directory / "size.json").write_text(json.dumps({"objects": []}))

    def launch(argv, env=None, **_):
        status, output = 0, b"fixture\n"
        if argv[0] == "make":
            build_fixture(argv)
        elif Path(argv[0]).name == "tr64-host":
            output = BURST.encode()
        elif Path(argv[0]).name == "ares.sh":
            outdir, log = Path(argv[1]), Path(argv[3])
            if lane == "run/ares-binary-tests.py" and outdir.name != "explicit":
                status, output = 2, b"ares missing:\n"
            else:
                body, picture = "TESTHARNESS exit 0\n", b"present"
                if lane == "run/level-reload.py":
                    body += reload_log()
                elif lane == "run/log-burst-tests.py":
                    body += BURST
                elif lane == "run/fatal-tests.py":
                    body = ("tr64 fatal: EXCEPTION 4\nfatal-nested: first report retained\n"
                            "badvaddr=00000001 fatal-register: R30\n" + FATAL_EXIT)
                    status, picture = 1, fatal_picture()
                outdir.mkdir(parents=True, exist_ok=True)
                log.write_text(marker + body)
                (outdir / "ares-test-0000.ppm").write_bytes(picture)
        return mock.Mock(communicate=lambda **_: (output, None), returncode=status)

    def execute(argv, *, env=None, expected=0, stdout=None):
        argv = list(map(str, argv))
        if argv[0] == "make":
            build_fixture(argv)
        elif argv[0] == "test/run/ares.sh":
            directory, log = Path(argv[1]), Path(argv[3])
            if expected == 2:
                log.write_text("not a readable 32768-byte image\n")
            else:
                name = "write" if directory.name == "write" else "load"
                log.write_text((marker if directory.name == affected else BOOT)
                               + f"SRAM probe: {name} PASS\nTESTHARNESS SRAM out image\n")
                Path(env["ARES_SRAM_OUT"]).write_bytes(b"\x01" * 32768)
                (directory / "ares-test-0000.ppm").touch()
        elif argv[1] == "write":
            Path(argv[2]).write_bytes(b"\x01" * 32768)

    def menu_run(rom, ares, directory, sram_in, sram_out=None):
        (directory / "vi").mkdir(parents=True)
        marks = []
        if sram_out:
            sram_out.write_bytes(b"\x01" * 32768)
            for name, color in (("before-ring", b"\x00"), ("ring", b"\xff")):
                picture = name + ".ppm"
                (directory / "vi" / picture).write_bytes(PPM + color * (320 * 240 * 3))
                marks.append(dict(name=name, picture=picture))
        text = (marker if directory.name == affected else PLAY)
        return mock.Mock(marks=marks, text=lambda: text + "game-render: room=1 eye=74000,2216,4500\n"
                         "save: bind status=0 slots=1\n")

    with mock.patch.dict(os.environ, {"BUILD_DIR": str(build), "TR64_LEVEL_PACK": str(exe),
                                      "ARES_EXPANSION_PAK": "false"}), \
         mock.patch.object(sys, "argv", [lane, *map(str, data[lane])]), \
         mock.patch.object(subprocess, "Popen", launch), contextlib.redirect_stdout(io.StringIO()):
        if lane == "ui/save-cart-test.py":
            mod = module(lane)
            with mock.patch.object(mod, "execute", execute):
                mod.main()
        elif lane == "ui/menu-flow-test.py":
            mod = module(lane)
            with mock.patch.object(mod, "Run", menu_run), mock.patch.object(mod, "pad_write"):
                mod.main()
        else:
            runpy.run_path(str(ROOT / "test" / lane), run_name="__main__")


def run_controls(root):
    lanes = (("run/level-reload.py", ""), ("run/log-burst-tests.py", ""), ("run/fatal-tests.py", ""),
             ("run/ares-binary-tests.py", ""), ("ui/save-cart-test.py", "write"), ("ui/save-cart-test.py", "read"),
             ("ui/menu-flow-test.py", "first"), ("ui/menu-flow-test.py", "second"))
    for index, (lane, affected) in enumerate(lanes):
        good = PLAY if "menu-flow" in lane else BOOT
        for case, marker, reason in (("match", good, None), ("wrong", good.replace(BUILD, "000000000000"), "guest build id mismatch"),
                                     ("missing", "", "missing guest build id")):
            directory = root / f"{index}-{case}"
            directory.mkdir()
            try:
                run_synthetic(lane, directory, marker, affected)
            except ValueError as error:
                assert reason and reason in str(error), (lane, affected, case, error)
            else:
                assert reason is None, (lane, affected, case, "accepted")
        print(f"direct identity PASS {lane} {affected}: matched, wrong and missing boot id")

    with mock.patch.dict(os.environ, {"BUILD_DIR": str(root)}):
        control = module("run/controls.py")
    build, out = root / "stale-build", root / "stale-out"
    header(build)
    out.mkdir()
    reason = "expected fatal reason"
    missing_exit = "missing fresh guest exit 1"
    def fatal_launch(argv, **kwargs):
        if fixture is not None:
            Path(argv[3]).write_text(fixture + reason + "\n" + record)
        return mock.Mock(communicate=lambda **_: (b"launcher status 1\n", None), returncode=1)
    with mock.patch.object(subprocess, "Popen", fatal_launch), mock.patch.object(control, "no_leak"):
        for fixture, record, rejection in (
            (BOOT, FATAL_EXIT, None),
            (BOOT, "TESTHARNESS exit 1\n", None),
            (None, "", missing_exit),
            (BOOT, FATAL_EXIT.rstrip("\n"), missing_exit),
            (BOOT, "TESTHARNESS exit 1 (arg1=0x014000f0\n", missing_exit),
            (BOOT, FATAL_EXIT.replace("exit 1", "exit 0"), missing_exit),
            (BOOT, FATAL_EXIT.replace("exit 1", "exit 10"), missing_exit),
            (BOOT, "diagnostic: " + FATAL_EXIT, missing_exit),
            (BOOT, FATAL_EXIT.rstrip("\n") + " trailing text\n", missing_exit),
            (BOOT, FATAL_EXIT * 2, missing_exit),
            (BOOT.replace(BUILD, "000000000000"), FATAL_EXIT, "guest build id mismatch"),
            ("", FATAL_EXIT, "missing guest build id"),
        ):
            # A previous run would pass every observable if its log were reused.
            (out / "engine.log").write_text(BOOT + reason + "\n" + FATAL_EXIT)
            try:
                control.cart_fatal_probe("pool", reason, build, out)
            except (RuntimeError, ValueError) as error:
                assert rejection and rejection in str(error), (rejection, error)
            else:
                assert rejection is None, rejection
    print("fatal route controls PASS: fresh bare and ares exits; stale, truncated, wrong-status, embedded, "
          "duplicate and wrong/missing identity records rejected")


def sweep_controls(root):
    sweep = module("run/sweep.py")
    sha = "7" * 64
    identities = dict(build_id=BUILD, config_id=CONFIG, rom_sha256=sha, guest_boot=BOOT,
                      launcher=f"local rom_sha256={sha}\n")
    hashes = ["11111111", "22222222"]
    text = ("game-tick: tick=0 hash=11111111 tick_us=1 hash_schema=3\n"
            "game-tick: tick=1 hash=22222222 tick_us=1 hash_schema=3\n"
            "scene-work: tick=1 total_us=999900 wall_us=999900 diagnostics_us=0 pacing_us=0 game_us=1 audio_us=0 room=2\n"
            "scene: ticks=1 state=11111111\n")
    pack = root / "pack"
    pack.write_bytes(b"pack")
    args = SimpleNamespace(source="psx", host=pack, pack=pack, oracle=None, build_root=root,
                           out=root, ares_seconds=1, perf_runs="", levels="level1", runs="1", force=False,
                           baseline=None, md_dir=root / "report")
    fresh = dict(exit=0, verdict="scene PASS", text=text, shot=None, shots=0, wall_s=1,
                 out=root, identity=identities)
    path = root / "runs/psx-level1-r1.json"
    source = dict(levels=["level1"], loader=lambda _: object(), path=lambda _: pack)
    with mock.patch.dict(os.environ, {"TMPDIR": str(root)}), mock.patch.dict(sweep.SOURCES, {"psx": source}), \
         mock.patch.object(sweep, "draw_script", return_value=[(1, "0\n")]), \
         mock.patch.object(sweep, "reference", return_value=(hashes, {"host_records": 2}, [{}])) as reference, \
         mock.patch.object(sweep, "pose_for", return_value={}), \
         mock.patch.object(sweep, "prepare_cart", return_value=({}, root / "route", root)), \
         mock.patch.object(sweep, "build_cart", return_value=((BUILD, CONFIG), sha)) as built, \
         mock.patch.object(sweep, "run_cart", return_value=fresh) as ran, \
         contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        assert sweep.command_run(args) == 0
        original = path.read_text()
        assert sweep.command_run(args) == 0 and ran.call_count == 1 and built.call_count == 1
        reference.return_value = (["11111111", "22222223"], {"host_records": 2}, [{}])
        assert sweep.command_run(args) == 1 and ran.call_count == 1 and built.call_count == 1
        assert "cached reference hashes differ" in json.loads(path.read_text())["rejected"]
        reference.return_value = (hashes, {"host_records": 2}, [{}])
        for expected, path_text, reason in (
            ((("000000000000", CONFIG), sha), original, "guest build id mismatch"),
            (((BUILD, "0" * 64), sha), original, "guest configuration digest mismatch"),
            (((BUILD, CONFIG), "0" * 64), original, "ROM SHA-256"),
            (((BUILD, CONFIG), sha), original.replace("rom_sha256=" + sha, "rom_sha256=" + "0" * 64), "launcher ROM SHA-256"),
        ):
            built.return_value = expected
            path.write_text(path_text)
            assert sweep.command_run(args) == 1 and ran.call_count == 1
            assert reason in json.loads(path.read_text())["rejected"]
            assert sweep.command_report(args) == 1
            report = (args.md_dir / "psx-table.md").read_text()
            assert "999.9" not in report and reason in report
        args.force = True
        fresh.update(exit=1, verdict="fresh scene failure")
        assert sweep.command_run(args) == 1 and ran.call_count == 2
        assert sweep.command_report(args) == 1
        report = (args.md_dir / "psx-table.md").read_text()
        assert "999.9" not in report and "fresh scene failure" in report
    print("sweep cache and report PASS: reference hashes, source, configuration, ROM and receipt controls; failures exclude timing")
