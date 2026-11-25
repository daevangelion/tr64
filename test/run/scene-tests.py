# SPDX-License-Identifier: MIT
"""Observable selection and external-check controls, without cart builds."""
import contextlib
import hashlib
import io
import importlib.util
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
from unittest import mock
sys.dont_write_bytecode = True
ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("scene", ROOT / "test/run/scene.py")
scene = importlib.util.module_from_spec(spec)
spec.loader.exec_module(scene)


def rejected(call, reason):
    try:
        call()
    except ValueError as error:
        if reason in str(error):
            return
        raise
    raise AssertionError("observable control escaped: " + reason)


with tempfile.TemporaryDirectory(prefix="scene-tests-", dir=os.environ["TMPDIR"]) as temporary:
    root = pathlib.Path(temporary)
    route = root / "synthetic.route"
    route.write_text("scene synthetic\nstart boot\nticks 8\nseed 1\nobserve missing-check\n")
    result = subprocess.run(["rtk", "proxy", sys.executable, ROOT / "test/run/scene.py", "--host", route], capture_output=True, text=True, env=dict(os.environ, BUILD_DIR=str(root / "unused-build")))
    if result.returncode != 1 or "unknown observable: missing-check" not in result.stderr or (root / "unused-build").exists():
        raise AssertionError(f"unknown observable did not fail before build: {result}")
    selection = subprocess.run(["rtk", "proxy", "make", "--no-print-directory", "TR64_GAME=4",
                                f"BUILD_DIR={root / 'unused-build'}", "cart-identity"], cwd=ROOT,
                               capture_output=True, text=True)
    if selection.returncode != 2 or "TR64_GAME=4: expected 1, 2 or 3" not in selection.stderr or (root / "unused-build").exists():
        raise AssertionError(f"unknown game did not fail by name before build: {selection}")
    print("game selection PASS (unknown game rejected before build)")
    plugin = root / "checks.py"
    plugin.write_text("def check(ctx, args):\n    if args != ['expected'] or ctx['log'] != 'production log':\n        raise ValueError('custom observable mismatch')\nCHECKS = {'custom': check}\n")
    checks = scene.load_checks([plugin])
    meta = {"observables": [("custom", ["expected"])]}
    scene.validate_observables(meta, checks)
    checks["custom"]({"log": "production log"}, ["expected"])
    rejected(lambda: checks["custom"]({"log": "wrong log"}, ["expected"]), "custom observable mismatch")
    rejected(lambda: scene.load_checks([plugin, plugin]), "duplicate observable")
    rejected(lambda: scene.validate_observables({"observables": [("state", [])]}, checks), "requires expect-state")
    rejected(lambda: scene.validate_observables({"observables": [("pools", ["extra"])]}, checks), "requires 0 operands")
    rejected(lambda: scene.validate_observables({"level": "level1", "expect-shot": "0"*64, "observables": [("boot-card", [])]}, checks), "requires a no-level scene")
    route.write_text("scene synthetic\nstart boot\nticks 8\nseed 1\nobserve log 'production log'\n")
    meta, _ = scene.routec.parse(route)
    scene.validate_observables(meta, checks)
    checks["log"]({"log": "production log"}, meta["observables"][0][1])
    rejected(lambda: checks["log"]({"log": "wrong"}, ["production log"]), "missing log observable")
    # Exercise main's active launcher call, not a detached timeout helper.
    wanted_build = "0500E430EC0B"
    wanted_config = "ab" * 32
    build_line = f"cart: build_id={wanted_build} config_id={wanted_config}\n"
    cart_boot = build_line
    calls = []
    def launch(args, log=None, **kwargs):
        calls.append((args, kwargs))
        if args[0] == "make":
            header = pathlib.Path(str(args[2]).split("=", 1)[1]) / "generated/cart_identity.h"
            header.parent.mkdir(parents=True, exist_ok=True)
            header.write_text(f'#define TR64_BUILD_ID "{wanted_build}"\n#define TR64_CONFIG_ID "{wanted_config}"\n')
        if pathlib.Path(args[0]).name == "ares.sh":
            pathlib.Path(args[3]).write_text(cart_boot + "production log\n")
        elif args[0] != "make":
            pathlib.Path(log).write_text("production log")
        return b""
    def invoke(configured, host=False):
        calls.clear()
        with mock.patch.dict(os.environ, {"BUILD_DIR": str(root / "deadline")}):
            if configured is None:
                os.environ.pop("ARES_TIMEOUT", None)
            else:
                os.environ["ARES_TIMEOUT"] = configured
            with mock.patch.object(sys, "argv", ["scene.py", "--host" if host else "--ares", str(route)]), \
                 mock.patch.object(scene, "command", launch), contextlib.redirect_stdout(io.StringIO()):
                assert scene.main() == 0
    for configured, seconds in ((None, 90), ("180", 180), ("0.25", 0.25)):
        invoke(configured)
        assert len(calls) == 2 and calls[0][1]["timeout"] == 120
        assert pathlib.Path(calls[1][0][0]).name == "ares.sh"
        assert float(calls[1][0][-1]) == seconds and calls[1][1]["timeout"] == seconds + 10
    for configured in ("0", "-1", "nan", "inf", "-inf", "1e309"):
        rejected(lambda: invoke(configured), "positive finite seconds")
        assert not calls
    rejected(lambda: invoke("invalid"), "could not convert")
    assert not calls
    invoke("invalid", host=True)
    assert len(calls) == 2 and calls[1][1]["timeout"] == 8
    for cart_boot, reason in ((build_line.replace(wanted_build, "000000000000"), "guest build id mismatch"),
                              (build_line.replace(wanted_config, "0" * 64), "guest configuration digest mismatch"),
                              ("", "missing guest build id"),
                              (build_line.rstrip("\n"), "incomplete guest build identity record"),
                              (build_line + build_line.rstrip("\n"), "multiple guest build ids")):
        rejected(lambda: invoke(None), reason)
    print("scene guest identity PASS (wrong, missing, incomplete and duplicate boot records rejected)")

    # Route observables are deliberately satisfied regardless of probe evidence.
    route.write_text("scene synthetic\nstart boot\nload level level1\nticks 2\nseed 1\nobserve log 'production log'\n")
    measure = ("render-measure: views=2 rnd_us=1 gpu_us=2 tri=3 tex=4 tex_bytes=5 pal=6 mat=7 rms=8 "
               "record_peak=9 record_total=10 spills=0 spill_us=0")
    def probe(extra, row, picture):
        def launch_probe(args, log=None, **kwargs):
            if args[0] == "make":
                assert "TR64_RENDER_MEASURE=1" in args
            else:
                pathlib.Path(log).write_text(f"production log\n{row}\n")
                if picture:
                    pathlib.Path(args[-1]).write_bytes(picture)
        with mock.patch.dict(os.environ, {"BUILD_DIR": str(root / "probe")}), \
             mock.patch.object(sys, "argv", ["scene.py", "--host", *extra, str(route)]), \
             mock.patch.object(scene, "command", launch_probe), contextlib.redirect_stdout(io.StringIO()):
            assert scene.main() == 0

    shot = b"P6\n320 240\n255\n" + bytes(320 * 240 * 3)
    split = ["--split-camera", "second.json"]
    probe(split, measure, shot)
    probe(["--render-measure"], measure.replace("views=2", "views=1"), shot)
    rejected(lambda: probe(split, measure, None), "requires a fresh screenshot")
    rejected(lambda: probe(split, measure, shot[:-1]), "canonical 320x240")
    rejected(lambda: probe(split, measure.replace("views=2", "views=1"), shot), "for 2 view(s)")
    rejected(lambda: probe(split, measure.rsplit(" ", 1)[0], shot), "complete render-measure records")
    rejected(lambda: probe(["--render-measure"], "", shot), "complete render-measure records")
    print("scene render evidence PASS (split and measure; missing/stale or short picture, missing/incomplete or wrong-view counters rejected)")

    # Don't let an absent renderer section turn a required ELF check into a skip.
    hot = root / "renamed-hot.ld"
    hot.write_text(".text : {\n *renderer.o(.text.renamed_draw)\n}\n")
    link_map = root / "renamed.map"
    link_map.write_text(".text 0xc0001000 0x20\n"
                        " 0xc0001000 __text_start = .\n"
                        " .text.renamed_draw 0xc0001000 0x20 /test/renderer.o\n")
    elf = root / "test.elf"
    elf.touch()
    objdump = root / "empty-objdump"
    objdump.write_text("#!/bin/sh\nexit 0\n")
    objdump.chmod(0o755)
    guard = [sys.executable, ROOT / "test/run/hot-check.py", link_map, hot]
    with link_map.open("a") as stream:
        stream.write(" 0x800ae000 __bss_end = .\n")
    for game, status, marker in [
        ("1", 0, "hot pools: table=pools.def"),
        ("2", 1, "pools-tr2.def: pool io overlaps fixed rows"),
    ]:
        result = subprocess.run(guard + ["--check-pools", "--no-render-stack"],
                                capture_output=True, text=True, env=dict(os.environ, TR64_GAME=game))
        assert result.returncode == status and marker in result.stdout + result.stderr, result
    print("hot pool selection PASS (same image fits TR1, TR2 carve overlaps fixed rows)")
    # Keep the ELF-size case and the declared budget distinct: the tiny
    # stand-in image leaves room for this carve, but the table is over 4 MiB.
    pool_dir = root / "tr64"
    pool_dir.mkdir()
    table = pool_dir / "pools-tr2.def"
    stock = scene.pool_table(2).read_text()
    over = re.sub(r"(TR64_POOL\(level,\s*)\d+", r"\g<1>2112", stock)
    assert over != stock
    unit = '#include "tr64/mem.h"\n'
    for definition, status in [(stock, 0), (over, 1)]:
        table.write_text(definition)
        result = subprocess.run(["rtk", "cc", "-I", root, "-I", ROOT / "include",
                                 "-DTR64_GAME=2", "-x", "c", "-fsyntax-only", "-"],
                                input=unit, capture_output=True, text=True)
        if result.returncode != status or (status and "tr64_declared_pools_exceed_rdram" not in result.stderr):
            raise AssertionError(f"declared row budget compile control escaped: {result}")
    gate_spec = importlib.util.spec_from_file_location("hot_check", guard[1])
    gate = importlib.util.module_from_spec(gate_spec)
    gate_spec.loader.exec_module(gate)
    with mock.patch.object(gate, "pool_table", return_value=table):
        small_image = " 0x80089000 __bss_end = .\n"
        rejected(lambda: gate.check_pools(small_image, 4096), "declared pools total")
        # Exactly 4 MiB declared. The image fits 608 KiB but the carve
        # alignment adds a gap, which would encroach on the fixed rows.
        table.write_text(re.sub(r"(TR64_POOL\(level,\s*)\d+", r"\g<1>2110", stock))
        image_at_budget = " 0x80098000 __bss_end = .\n"
        rejected(lambda: gate.check_pools(image_at_budget, 4096), "pool io overlaps fixed rows")
    print("declared pool total PASS (4098 KiB rejected independently of linker placement)")
    for flags, diagnostic in [
        (["--elf", elf, "--objdump", objdump, "--expect-render-stack", "1", "2"], "missing .text disassembly"),
        ([], "renderer build requires --elf disassembly"),
    ]:
        result = subprocess.run(guard + flags, capture_output=True, text=True)
        assert result.returncode == 1 and diagnostic in result.stderr and "remeasure" in result.stderr, result
    print("hot entry SP evidence PASS (renamed map and empty disassembly, missing ELF rejected)")

    names = ("tr64_scene_tick", "game_scene_render", "tr64_render_game_scene_frame",
             "tr64_render_game_frame", "frame", "tr64_render_draw_items",
             "tr64_render_draw_object", "tr64_snd_pump")
    addresses = {name: 0xc0001000 + 0x100 * i for i, name in enumerate(names)}

    def direct(op, name):
        return f"{op} {addresses[name]:x} <{name}>"

    def scene_target(index):
        offset = index * 4
        return f"{addresses[names[0]] + offset:x} <{names[0]}+0x{offset:x}>"

    def wrapper(size, *callees):
        return [f"addiu sp,sp,-{size}",
                *(insn for name in callees for insn in (direct("jal", name), "nop")),
                "jr ra", f"addiu sp,sp,{size}"]

    # Real tr64_scene_tick shape: setup, conditional early exit to an
    # out-of-function sibling tail jump, normal checked call and return.
    scene_body = ["lw v0,0(gp)", "nop", "addiu sp,sp,-144",
                  f"beqz v0,{scene_target(9)}", "nop", direct("jal", names[1]),
                  "nop", "jr ra", "addiu sp,sp,144", "ld ra,136(sp)",
                  direct("j", names[7]), "addiu sp,sp,144"]
    bodies = dict(zip(names, [
        scene_body, wrapper(112, names[2]), wrapper(216, names[3]), wrapper(280, names[4]),
        wrapper(264, names[6], names[5], names[6], names[5]), wrapper(2352, names[6]),
        ["jr ra", "nop"], ["jr ra", "nop"],
    ], strict=True))

    def guard_result(body, *, omit="", flags=()):
        listed = {**bodies, names[0]: body}
        listed.pop(omit, None)
        symbols, disassembly = [], []
        for name, instructions in listed.items():
            address = addresses[name]
            symbols.append(f"{address:x} g     F .text\t{4 * len(instructions):08x} {name}")
            disassembly.extend([f"\n{address:x} <{name}>:",
                                *(f"{address + 4 * i:x}:\t{instruction}" for i, instruction in enumerate(instructions))])
        text = "\n".join(["SYMBOL TABLE:", *symbols, "\nDisassembly of section .text:", *disassembly, ""])
        objdump.write_text(f"#!{sys.executable}\nprint({text!r}, end='')\n")
        return subprocess.run(guard + ["--elf", elf, "--objdump", objdump, *flags], capture_output=True, text=True)

    straight_restore = [*scene_body[:10], "addiu sp,sp,144", direct("j", names[7]), "nop"]
    for body in (scene_body, straight_restore):
        result = guard_result(body)
        assert result.returncode == 0 and "room=1016 item=3368" in result.stdout, result
    for body, omit, flags, diagnostic in (
        ([f"b {scene_target(3)}", *scene_body[1:]], "", (), "straight-line entry prologue"),
        ([*scene_body[:10], f"b {scene_target(5)}", "addiu sp,sp,-32"], "", (), "unexpected SP adjustment"),
        ([*scene_body[:10], f"j {scene_target(5)}", "addiu sp,sp,144"], "", (), "not on a terminal return"),
        ([*scene_body[:10], direct("j", names[1]), "addiu sp,sp,144"], "", (), "sibling jump into guarded renderer chain"),
        ([*scene_body[:10], direct("j", names[6]), "addiu sp,sp,144"], "", (), "sibling jump into guarded renderer chain"),
        ([*scene_body[:3], f"beqz v0,{scene_target(11)}", *scene_body[4:]], "", (), "branch into the prologue or a delay slot"),
        ([*scene_body[:3], f"beqz v0,{scene_target(2)}", *scene_body[4:]], "", (), "branch into the prologue or a delay slot"),
        ([*straight_restore[:3], f"beqz v0,{scene_target(11)}", *straight_restore[4:]], "", (), "not on a straight-line terminal return"),
        (scene_body, names[1], (), "missing linked disassembly"),
        (scene_body, "", ("--expect-render-stack", "1", "2"), "expected 1/2"),
    ):
        result = guard_result(body, omit=omit, flags=flags)
        assert result.returncode == 1 and diagnostic in result.stderr and "remeasure" in result.stderr, result
    print("hot entry SP control flow PASS (terminal sibling jumps accepted; skipped debit, late delay-slot SP write, local tail and bypassed restores rejected)")

    # TR64_TIMEOUT_SCALE multiplies every command deadline (the real command, a sleeping child)
    with mock.patch.dict(os.environ, {"TR64_TIMEOUT_SCALE": "1"}):
        try:
            scene.command(["sleep", "5"], timeout=0.2)
        except RuntimeError as error:
            assert "command deadline" in str(error)
        else:
            raise AssertionError("deadline not enforced at scale 1")
    with mock.patch.dict(os.environ, {"TR64_TIMEOUT_SCALE": "100"}):
        scene.command(["sleep", "0.3"], timeout=0.01)
    for bad in ("0.5", "nan", "inf", "x"):
        with mock.patch.dict(os.environ, {"TR64_TIMEOUT_SCALE": bad}):
            rejected(lambda: scene.command(["true"], timeout=1), "TR64_TIMEOUT_SCALE" if bad != "x" else "could not convert")
print("scene deadlines PASS (active cart launcher default90/100, configured180/190, fractional, invalid-before-build; host deadline unchanged)")
print("scene observables PASS (selection, unknown/operand errors, external plugin, failing production-log control)")


def remote_rom_binding(root):
    # Keep the real rsync sender/receiver and both halves of ares-remote.sh,
    # but run the host commands in a private local HOME with no emulator.
    home = root / "remote-home"
    ares = home / "tr64-ares"
    (ares / "xdg").mkdir(parents=True)
    bin_dir = root / "bin"
    bin_dir.mkdir()

    def executable(path, script):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("#!/usr/bin/env bash\nset -euo pipefail\n" + script)
        path.chmod(0o755)

    executable(ares / "build/desktop-ui/ares", 'exec cat -- "${@: -1}"\n')
    executable(bin_dir / "Xvfb", "printf '0\\n' >&3\n")
    executable(bin_dir / "sha256sum", r'''
if [[ ${HOME:-} == "$FIXTURE_REMOTE_HOME" ]]; then
    mode=${FIXTURE_REMOTE_SHA:-}
else
    mode=${FIXTURE_LOCAL_SHA:-}
fi
case "$mode" in
    empty) exit 0 ;;
    short) printf 'abc  -\n' ;;
    nonhex) printf '%064d  -\n' 0 | tr 0 g ;;
    nonascii) printf 'é%.0s' {1..64}; printf '  -\n' ;;
    failed) "$FIXTURE_SHA256SUM" "$@"; exit 1 ;;
    missing) exit 127 ;;
    *) exec "$FIXTURE_SHA256SUM" "$@" ;;
esac
''')
    executable(bin_dir / "ssh", r'''
while [[ ${1:-} == -o ]]; do shift 2; done
[[ ${1:-} != -n ]] || shift
shift # host
export HOME="$FIXTURE_REMOTE_HOME"
cd "$HOME"
if [[ -n ${FIXTURE_TAMPER_WITH:-} && "$*" == *" --remote-side "* ]]; then
    read -r _ _ _ id _ rom _ <<<"$*"
    cp -- "$FIXTURE_TAMPER_WITH" "tr64-runs/$id/rom/$rom"
fi
exec bash -c "$*"
''')
    env = {k: v for k, v in os.environ.items() if not k.startswith(("ARES_", "TR64_"))}
    env.update(PATH=f"{bin_dir}:{os.environ['PATH']}", TMPDIR=str(root), FIXTURE_REMOTE_HOME=str(home),
               FIXTURE_SHA256SUM=shutil.which("sha256sum"),
               TR64_ARES_HOST="fixture", TR64_ARES_REMOTE_SLOTS="1")

    def run(name, rom, *, stale=False, **extra_env):
        output = root / name
        if stale:
            output.mkdir()
            (output / "ares-host.txt").write_text("previous-run\n")
            (output / "slot.txt").write_text("0\n")
        log = output / "engine.log"
        result = subprocess.run([ROOT / "test/run/ares.sh", output, rom, log, "5"],
                                env=dict(env, **extra_env), capture_output=True, timeout=15)
        return result, log, output

    first, second = (root / name / "tr64.z64" for name in ("first", "second"))
    for path, payload in ((first, b"first!\n"), (second, b"second\n")):
        path.parent.mkdir()
        path.write_bytes(payload)
        os.utime(path, (1700000000, 1700000000))
    assert first.stat().st_size == second.stat().st_size
    seeded, seed_log, seed_out = run("seed", first)
    assert seeded.returncode == 0, seeded
    assert seed_log.read_bytes() == first.read_bytes()
    basis = home / "tr64-runs/.basis/rom/tr64.z64"
    assert basis.stat().st_mtime_ns == second.stat().st_mtime_ns
    assert basis.read_bytes() == first.read_bytes()

    result, log, output = run("collision", second)
    assert result.returncode == 0, result
    assert log.read_bytes() == second.read_bytes(), f"same-size/same-mtime ROM alias: ran {log.read_bytes()!r}"
    failures = []
    for rom, out in ((first, seed_out), (second, output)):
        expected = hashlib.sha256(rom.read_bytes()).hexdigest()
        assert f"rom_sha256={expected}" in (out / "ares-host.txt").read_text().split()
        wall = (out / "wall.txt").read_text().strip()
        if not re.fullmatch(r"\d+\.\d{3}", wall):
            failures.append(f"{out.name}: expected wall time in milliseconds, got {wall}")

    def reject_run(name, rom, status, message, **extra_env):
        result, log, output = run(name, rom, stale=True, **extra_env)
        record = output / "ares-host.txt"
        receipt = record.read_text() if record.exists() else ""
        entered = log.exists() and bool(log.read_bytes())
        if (result.returncode != status or message not in result.stderr or entered
                or (output / "slot.txt").exists() or "previous-run" in receipt or "rom_sha256=" in receipt):
            failures.append(f"{name}: expected exit {status}, {message!r}, no slot or ROM receipt; "
                            f"got exit {result.returncode}, stderr={result.stderr!r}, "
                            f"guest={entered}, slot={(output / 'slot.txt').exists()}, receipt={receipt!r}")

    reject_run("tampered", second, 65, b"ROM SHA-256 mismatch", FIXTURE_TAMPER_WITH=str(first))
    reject_run("missing-rom", root / "absent.z64", 2, b"no ROM")
    reject_run("local-ares-missing", second, 2, b"ares missing:", TR64_ARES_HOST="local",
               ARES_BINARY=str(root / "absent-ares"))
    for mode in ("empty", "short", "nonhex", "failed", "missing"):
        reject_run(f"local-{mode}", second, 2, b"cannot hash ROM", FIXTURE_LOCAL_SHA=mode)
        reject_run(f"remote-{mode}", second, 255, b"cannot hash staged ROM", FIXTURE_REMOTE_SHA=mode)
    reject_run("both-empty", second, 2, b"cannot hash ROM", FIXTURE_LOCAL_SHA="empty", FIXTURE_REMOTE_SHA="empty")
    locales = subprocess.check_output(["locale", "-a"], text=True).splitlines()
    utf8 = [name for name in locales if name.lower().replace("-", "").endswith(".utf8")]
    if utf8:
        # Prefer the locale whose collation admitted é through [a-fA-F].
        locale = next((name for name in utf8 if name.lower().startswith("en_us.")), utf8[0])
        reject_run("both-nonascii", second, 2, b"cannot hash ROM", LC_ALL=locale,
                   FIXTURE_LOCAL_SHA="nonascii", FIXTURE_REMOTE_SHA="nonascii")
        reject_run("remote-nonascii", second, 255, b"cannot hash staged ROM", LC_ALL=locale,
                   FIXTURE_REMOTE_SHA="nonascii")
    else:
        print("non-ASCII digest control SKIP (no UTF-8 locale installed)")
    assert not failures, "\n" + "\n".join(failures)
    assert basis.read_bytes() == second.read_bytes()
    if utf8:
        print(f"non-ASCII SHA-256 controls PASS ({locale}, local 2 / remote 255)")


with tempfile.TemporaryDirectory(prefix="ares-remote-tests-", dir=os.environ["TMPDIR"]) as temporary:
    remote_rom_binding(pathlib.Path(temporary))
print("remote ROM binding PASS (collision, verified receipt, millisecond wall time, mismatch 65, "
      "hash failures 2/255, stale receipts cleared before launch)")

from direct_identity_tests import run_controls, sweep_controls
with tempfile.TemporaryDirectory(prefix="cart-identity-tests-", dir=os.environ["TMPDIR"]) as temporary:
    run_controls(pathlib.Path(temporary))
    sweep_controls(pathlib.Path(temporary))
