# SPDX-License-Identifier: MIT
"""Actual CPU/assert/error/RSP failures through the one fatal path (shipping by
default; --test-profile builds the same probes as TR64_TEST=1).

Checks useful log context, real register/return addresses and fresh framebuffer
pixels. Early ctor failure additionally proves the weak SDK default resolves to
our strong replacement before code_init or emulog initialization.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import sys
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("scene", ROOT / "test/run/scene.py")
scene = importlib.util.module_from_spec(spec)
spec.loader.exec_module(scene)
BAD = {"libdragon.a(inspector.o)", "libdragon.a(console.o)",
       "libdragon.a(graphics.o)", "libdragon.a(symtable.o)"}


def retained(path):
    return BAD.intersection(json.loads(path.read_text())["objects"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ares", action="store_true")
    parser.add_argument("--float-bits", action="store_true")
    parser.add_argument("--test-profile", action="store_true", help="build the probes as TR64_TEST=1 instead of shipping")
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--only", help="comma-separated control names to run (default: all)")
    parser.add_argument("--no-qr", action="store_true", help="skip the QR decode checks (need a venv with decoders)")
    parser.add_argument("--evidence", type=Path, help="copy each control's screenshot here as <profile>-<name>.png")
    args = parser.parse_args()
    build = Path(os.environ["BUILD_DIR"]).resolve() / "fatal-tests"
    build.mkdir(parents=True, exist_ok=True)
    cc = shlex.split(os.environ.get("HOST_CC", "cc"))
    scene.command([*cc, "-m32", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                   "-I", ROOT, ROOT / "engine/core/unwind.c", ROOT / "engine/test/unwind-test.c",
                   "-o", build / "unwind-test"], build / "unwind-build.log", timeout=60)
    scene.command([build / "unwind-test"], build / "unwind.log", timeout=10)
    print((build / "unwind.log").read_text().strip())
    if args.baseline:
        before = retained(args.baseline)
        if not before:
            raise AssertionError("cluster gate negative did not reject actual old image")
        print("fatal cluster gate negative PASS: old linked image retains " + ",".join(sorted(before)))
    if not args.ares:
        return
    cases = [("exception", "EXCEPTION 4"), ("irq", "EXCEPTION 4"),
             ("nested", "EXCEPTION 4"), ("assert", "fatal-probe assert marker=17"),
             ("c-assert", "ASSERT condition"), ("sys-error", "fatal-probe sys_error marker=37"),
             ("rsp", "RSP FATAL"), ("audio", "fatal-probe audio DMA"),
             ("unloaded", "code unit test not loaded"), ("early", None)]
    if args.float_bits:
        cases.insert(-1, ("float-assert", "FP64:0x3ff8000000000000"))
    args.profile = "test" if args.test_profile else "shipping"
    negatives_done = False
    if not args.no_qr:
        qr_spec = importlib.util.spec_from_file_location("fatal_qr", ROOT / "test/run/fatal_qr.py")
        qr = importlib.util.module_from_spec(qr_spec)
        qr_spec.loader.exec_module(qr)
    for name, reason in cases:
        if args.only and name not in args.only.split(","):
            continue
        directory = build / name
        out = directory / "run"
        out.mkdir(parents=True, exist_ok=True)
        for pattern in ("ares-test-*.ppm", "engine.log", "runner.log"):
            for stale in out.glob(pattern):
                stale.unlink()
        route = directory / "fatal.route"
        if name == "unloaded":
            text = (ROOT / "test/scenes/boot.route").read_text() + "\nprobe unloaded\n"
        else:
            text = f"scene fatal-{name}\nstart boot\nticks 2\nseed 1\nobserve pools\n"
            if name != "early":
                text += "load level fatal-" + name + "\n"
        route.write_text(text)
        probe = "plat/n64/fatal-probe.c plat/n64/fatal-nested-probe.c"
        if name == "early":
            probe += " engine/test/fatal-early-probe.c"
        options = [f"TR64_TEST={int(args.test_profile)}", "TR64_PLAY=0", "TR64_LEVEL_LOADER=0", "TR64_LEVEL_PACK=",
                   "TR64_ENGINE_SOURCES=", "TR64_HOST_SOURCES=", "TR64_CART_SOURCES=",
                   "TR64_UNIT_SOURCES=", "TR64_GENERATED_HEADERS=", "TR64_CODE_UNIT_FRAGMENTS=",
                   "TR64_ROM_FILES=", "TR64_ROM_DEPS=", "TR64_SCENE_TEST_SOURCES=" + probe]
        scene.command(["make", "--no-print-directory", "-j1", f"BUILD_DIR={directory}",
                       f"SCENE={route}", *options, "rom"], directory / "build.log", timeout=120)
        leftovers = retained(directory / "size.json")
        if leftovers:
            raise AssertionError(f"shipping fatal cluster still linked: {sorted(leftovers)}")
        env = dict(os.environ, ARES_EXPANSION_PAK="false")
        expected_build = scene.guest_identity.expected_identity(directory)
        # Two ares slots are shared host-wide: ares.sh waits for one, so a
        # deadline (slot wait plus run exceeds 40 s) means "slots busy": the run
        # itself ends by its own 30 s timeout, which is a different error.
        for attempt in range(600):
            (out / "engine.log").unlink(missing_ok=True)
            (out / "slot.txt").unlink(missing_ok=True)
            try:
                scene.command([ROOT / "test/run/ares.sh", out, directory / "tr64.z64", out / "engine.log", "30"],
                              out / "runner.log", timeout=40, expected=1, env=env)
                break
            except RuntimeError as error:
                if "command deadline" not in str(error) or attempt == 599:
                    raise
        log = (out / "engine.log").read_text(errors="replace")
        # The constructor fault deliberately never reaches the cart boot marker.
        if name != "early":
            scene.guest_identity.require_identity(log, expected_build)
        if not scene.guest_identity.has_exit_record(log, 1):
            raise AssertionError("crash did not exit1 through the guest harness")
        if name == "nested":
            if log.count("tr64 fatal: EXCEPTION 4") != 1 or log.count("fatal-nested: first report retained") != 1:
                raise AssertionError("nested fault lost/replaced the first fatal log")
            if "badvaddr=00000001" not in log or "fatal-register: R30" not in log:
                raise AssertionError("first fault context was not flushed before unwinding")
        elif reason is not None:
            if reason not in log or "NESTED FATAL" in log:
                raise AssertionError("wrong or recursive fatal reason: " + log)
            registers = re.findall(r"^fatal-register: R(\d+) ([0-9A-F]{16}) R(\d+) ([0-9A-F]{16})$", log, re.M)
            numbers = [int(i) for record in registers for i in (record[0], record[2])]
            if numbers != [i for i in range(32) if i not in (26, 27)] or "fatal-register: R26 NOT SAVED R27 NOT SAVED" not in log:
                raise AssertionError("missing GPR dump / unsaved SDK k0-k1 marker")
            if not re.search(r"badvaddr=[0-9a-f]{8}", log):
                raise AssertionError("missing BadVAddr context")
            audio = re.search(r"fatal-audio: status_before=([0-9a-f]+) status_after=([0-9a-f]+) length_stopped=(\d+) length_done=(\d+)", log)
            if not audio or int(audio[2], 16) & (1 << 25) or audio[3] != audio[4]:
                raise AssertionError("fatal failed to disable and stop AI DMA")
            if name == "audio" and (not int(audio[1], 16) & (1 << 25) or not int(audio[3])):
                raise AssertionError("audio negative did not begin with queued enabled DMA")
            trace = re.findall(r"^fatal-backtrace: frame=(\d+) pc=([0-9a-f]{8})$", log, re.M)
            if not 1 <= len(trace) <= 8 or [int(i) for i, _ in trace] != list(range(len(trace))):
                raise AssertionError("missing/bad bounded address trace")
            if not re.search(r"^fatal-screen: buffer=a037f800 vi_origin=0037f800 width=320 frames=\d+$", log, re.M):
                raise AssertionError("fatal did not program the fixed stock framebuffer")
            ring = re.findall(r"^log-ring: bytes=(\d+) peak_pending=(\d+) dropped=(\d+)$", log, re.M)
            if not ring or any(int(lost) or int(peak) > 4096 for _, peak, lost in ring):
                raise AssertionError("fatal diagnostics exceeded4KiB or dropped ring bytes")
            if name == "irq":
                nm = Path(os.environ.get("N64_GCCPREFIX", "/opt/mips64-toolchain")) / "bin/mips64-elf-nm"
                scene.command([nm, "-S", directory / "tr64.elf"], directory / "symbols.log", timeout=10)
                symbol = re.search(r"^([0-9a-f]+) ([0-9a-f]+) T tr64_fatal_test_irq_wait$", (directory / "symbols.log").read_text(), re.M)
                if not symbol or not any(int(symbol[1], 16) <= int(pc, 16) < int(symbol[1], 16) + int(symbol[2], 16) for _, pc in trace):
                    raise AssertionError("IRQ backtrace skipped the interrupted function")
        shots = list(out.glob("ares-test-*.ppm"))
        if len(shots) != 1:
            raise AssertionError("missing one fresh compact fatal screenshot")
        data = shots[0].read_bytes()
        header = b"P6\n320 240\n255\n"
        if not data.startswith(header) or len(data) != len(header) + 320 * 240 * 3:
            raise AssertionError("invalid fatal screenshot extent")
        pixels = data[len(header):]
        # Expected colours come from the owned glyph renderer, not a capture fit.
        if pixels.count(bytes((255, 0, 0))) < 20 or pixels.count(bytes((255, 255, 255))) < 100:
            raise AssertionError("fatal glyph framebuffer was blank/unreadable")
        if name == "nested":
            # Semantic glyph check, not a captured image: second fault must not
            # replace the first message at row16 with its own fatal context.
            bits = [31207,23213,31015,31207,31716,29842,29847,31599,24445,0,23497]
            for column, glyph in enumerate(bits):
                for row in range(5):
                    for x in range(3):
                        at = ((16 + row) * 320 + 4 + column * 4 + x) * 3
                        white = pixels[at:at+3] == b"\xff\xff\xff"
                        if white != bool(glyph & (1 << (14-row*3-x))):
                            raise AssertionError("nested fault erased the first on-screen message")
        print(f"fatal PASS {name}: actual exit1, compact screen {shots[0]}")
        if not args.no_qr:
            print(qr.check(name, directory, log, shots[0], args))
            if not negatives_done:
                print(qr.negatives(name, directory, log, shots[0], args))
                negatives_done = True
    print(f"fatal {args.profile} controls PASS (exception/assert/sys_error/RSP, unloaded fetch, pre-main default; no inspector/console/graphics/symbolizer)")


if __name__ == "__main__":
    main()
