# SPDX-License-Identifier: MIT
"""Production controls, including shared-slot and emulator process lifetime."""
import argparse
import ctypes
import fcntl
import importlib.util
import os
import pathlib
import re
import signal
import shutil
import subprocess
import sys
import time
sys.dont_write_bytecode = True
ROOT = pathlib.Path(__file__).resolve().parents[2]
BUILD = pathlib.Path(os.environ["BUILD_DIR"])
TMP = pathlib.Path(os.environ["TMPDIR"])
SLOTS = [pathlib.Path(f"/tmp/tr64-locks/ares-slot{i}") for i in (0, 1)]
spec = importlib.util.spec_from_file_location("scene", ROOT / "test/run/scene.py")
scene = importlib.util.module_from_spec(spec)
spec.loader.exec_module(scene)


def pool_reason(*, game=None):
    # No-level host probes always use the stock table.
    size = next(size for name, size, _ in scene.pool_rows(game=game) if name == "state")
    return f"pool state overflow request={size} used=0 budget={size-1}"


class SlotsBusy(RuntimeError):
    pass


def reject(args, reason, artifact):
    scene.command(args, artifact, timeout=100, expected=1)
    if reason not in artifact.read_text(errors="replace"):
        raise RuntimeError(f"negative control wrong reason: {artifact}")
    print(f"negative control PASS exit=1 reason='{reason}' artifact={artifact}")

def missing_loader_route():
    lines = (ROOT / "test/scenes/boot.route").read_text().splitlines()
    lines = [line for line in lines if not line.startswith(("observe boot-card", "expect-shot"))]
    lines = ["scene missing-loader" if line == "scene boot" else line for line in lines]
    return "\n".join(lines) + "\nload level level1\n"


def groups(out):
    return [int(path.read_text()) for path in (out / "xvfb.pid", out / "ares.pid") if path.exists()]


def own_slot_holders(out):
    ours = set(groups(out))
    result = subprocess.run(["fuser", *map(str, SLOTS)], capture_output=True)
    if result.returncode not in (0, 1):
        raise RuntimeError("fuser failed: " + result.stderr.decode())
    found = []
    for value in result.stdout.split():
        pid = int(value)
        try:
            group = os.getpgid(pid)
        except ProcessLookupError:
            continue
        if group in ours:
            found.append(pid)
    return found


def no_leak(out):
    # The test controller is a subreaper, so SIGKILLed harnesses' children can
    # be reaped here instead of depending on init's orphan-reaping latency.
    until = time.monotonic() + 5
    while True:
        live = []
        for group in groups(out):
            try:
                while os.waitpid(-group, os.WNOHANG)[0]:
                    pass
            except ChildProcessError:
                pass
            try:
                os.killpg(group, 0)
                live.append(group)
            except ProcessLookupError:
                pass
        if not live:
            break
        if time.monotonic() >= until:
            raise RuntimeError(f"surviving own process groups: {live}")
        time.sleep(0.02)
    if own_slot_holders(out):
        raise RuntimeError("own slot holder after cleanup")


def stop_groups(out):
    for group in groups(out):
        try:
            os.killpg(group, signal.SIGTERM)
        except ProcessLookupError:
            pass


def wait_file(process, path, timeout=20):
    until = time.monotonic() + timeout
    while not path.exists():
        if process.poll() is not None or time.monotonic() >= until:
            raise RuntimeError(f"harness did not reach {path.name}")
        time.sleep(0.02)


def harness_tests(rom):
    # Linux-only harness already depends on Xvfb, flock, setsid and fuser.
    if ctypes.CDLL(None).prctl(36, 1, 0, 0, 0):
        raise RuntimeError("cannot enable child subreaper for SIGKILL control")
    for mode in ("deadline", "SIGTERM", "lock-wait", "SIGKILL"):
        out = BUILD / "controls" / ("harness-" + mode)
        out.mkdir(parents=True, exist_ok=True)
        for name in ("ares.pid", "xvfb.pid", "waiting.pid", "slot.txt", "xvfb.dir"):
            (out / name).unlink(missing_ok=True)
        args = [ROOT / "test/run/ares.sh", out, rom, out / "engine.log", "0.1" if mode == "deadline" else "90"]
        if mode == "deadline":
            scene.command(args, out / "runner.log", timeout=20, expected=124)
        else:
            held = []
            process = None
            try:
                if mode == "lock-wait":
                    # Wait for and hold both shared slots, never disturb a
                    # foreign holder. The queued harness must die before launch.
                    deadline = time.monotonic() + 15
                    for path in SLOTS:
                        file = path.open("a")
                        held.append(file)
                        while True:
                            try:
                                fcntl.flock(file, fcntl.LOCK_EX | fcntl.LOCK_NB)
                                break
                            except BlockingIOError:
                                if time.monotonic() >= deadline:
                                    raise SlotsBusy("ares harness control: slots busy (15 second queue deadline)")
                                time.sleep(0.02)
                process = subprocess.Popen(list(map(str, args)), cwd=ROOT, start_new_session=True,
                                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, close_fds=True)
                wait_file(process, out / ("waiting.pid" if mode == "lock-wait" else "ares.pid"))
                if mode == "SIGKILL":
                    until = time.monotonic() + 10
                    while not own_slot_holders(out):
                        if process.poll() is not None or time.monotonic() >= until:
                            raise RuntimeError("emulator never inherited slot lock")
                        time.sleep(0.01)
                    process.kill()
                else:
                    os.killpg(process.pid, signal.SIGTERM)
                output, _ = process.communicate(timeout=10)
                (out / "runner.log").write_bytes(output)
                expected = -9 if mode == "SIGKILL" else 143
                if process.returncode != expected:
                    raise RuntimeError(f"{mode}: expected {expected}, got {process.returncode}")
                if mode == "SIGKILL":
                    if not own_slot_holders(out):
                        raise RuntimeError("SIGKILL released slot while emulator survived")
                    slot = int((out / "slot.txt").read_text())
                    lock = subprocess.run(["flock", "-n", str(SLOTS[slot]), "true"])
                    if lock.returncode != 1:
                        raise RuntimeError("SIGKILL allowed the occupied emulator slot to be reused")
                    stop_groups(out)
                if mode == "lock-wait" and groups(out):
                    raise RuntimeError("signal during lock wait launched children")
            finally:
                for file in held:
                    file.close()
                if process is not None and process.poll() is None:
                    os.killpg(process.pid, signal.SIGTERM)
                    process.communicate(timeout=10)
                stop_groups(out)
        no_leak(out)
        directory = out / "xvfb.dir"
        if directory.exists():
            scratch = pathlib.Path(directory.read_text().strip())
            if scratch.parent.resolve() != TMP.resolve() or not scratch.name.startswith("xvfb."):
                raise RuntimeError("invalid own Xvfb scratch directory")
            if scratch.exists():
                shutil.rmtree(scratch)
        print(f"ares harness selftest PASS {mode}: own groups reaped, own slot FDs released (foreign holders ignored)")


def cart_fatal_probe(probe, reason, build, out):
    expected = scene.guest_identity.expected_identity(build)
    for name in ("engine.log", "ares-host.txt", "slot.txt", "waiting.pid", "ares.pid", "xvfb.pid", "xvfb.dir", "exit.txt"):
        (out / name).unlink(missing_ok=True)
    log = out / "engine.log"
    env = dict(os.environ, ARES_EXPANSION_PAK="true" if probe == "highmem" else "false")
    scene.command([ROOT / "test/run/ares.sh", out, build / "tr64.z64", log, "90"],
                  out / "runner.log", timeout=100, expected=1, env=env)
    text = log.read_text(errors="replace") if log.is_file() else ""
    if not scene.guest_identity.has_exit_record(text, 1):
        raise RuntimeError(f"missing fresh guest exit 1: {log}")
    scene.guest_identity.require_identity(text, expected)
    if reason not in text or (probe == "unloaded" and "EXCEPTION 2 EPC" not in text) or (probe == "heap" and "post_boot_allocations=1 frozen=1" not in text):
        raise RuntimeError(f"cart negative control wrong reason: {log}\n{text}")
    no_leak(out)
    print(f"negative control PASS cart {probe} exit=1 reason='{reason}' artifact={log}")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    modes = ap.add_mutually_exclusive_group(required=True)
    modes.add_argument("--host", action="store_true")
    modes.add_argument("--cart", action="store_true")
    ap.add_argument("--skip-harness-selftests", action="store_true",
                    help="explicitly exclude lifecycle tests while shared slots are busy")
    ns = ap.parse_args()
    if ns.host:
        executable = BUILD / "host/tr64-host"
        reject([executable, "--pool-small"], pool_reason(game=1), BUILD / "pool-negative.log")
        reject([executable, "--unloaded"], "code unit test not loaded", BUILD / "unit-negative-host.log")
        route = TMP / "missing-loader.route"
        bytecode = TMP / "missing-loader.rbc"
        route.write_text(missing_loader_route())
        scene.command(["python3", ROOT / "tools/routec/routec.py", route, "-o", bytecode])
        isolated = BUILD / "controls/missing-loader"
        isolated.mkdir(parents=True, exist_ok=True)
        scene.command(["make", "--no-print-directory", f"BUILD_DIR={isolated}", f"SCENE={route}", "TR64_LEVEL_LOADER=0", "TR64_SCENE_TEST_SOURCES=", "host"], isolated / "host-build.log", timeout=120)
        reject([isolated / "host/tr64-host", "--route", bytecode], "scene level loader not linked: level1", BUILD / "loader-negative-host.log")
        alternate = TMP / "small-image.def"
        alternate.write_text(re.sub(r"TR64_POOL\(image,\s*\d+,", "TR64_POOL(image, 1,", scene.pool_table().read_text()))
        reject(["python3", ROOT / "test/run/size_gate", BUILD / "tr64.elf", "--pools", alternate], "size_gate OVER pool=image", BUILD / "image-negative.log")
        return 0
    if ns.skip_harness_selftests:
        print("SKIP four harness lifecycle self-tests (explicit exclusion; quiet-window integration gate still required)")
    else:
        harness_tests(BUILD / "scene-builds/boot/tr64.z64")
    for probe, reason in [("unloaded", "code unit test not loaded"), ("pool", pool_reason()),
                          ("heap", "allocation after boot"), ("highmem", "highmem guard changed words=1"),
                          ("missing-loader", "scene level loader not linked: level1")]:
        build = BUILD / "controls" / probe
        out = build / "run"
        out.mkdir(parents=True, exist_ok=True)
        route = TMP / (probe + ".route")
        source = missing_loader_route() if probe == "missing-loader" else (ROOT / "test/scenes/boot.route").read_text() + f"\nprobe {probe}\n"
        route.write_text(source)
        options = ["TR64_LEVEL_LOADER=0", "TR64_SCENE_TEST_SOURCES="] if probe == "missing-loader" else []
        scene.command(["make", "--no-print-directory", f"BUILD_DIR={build}", f"SCENE={route}", *options, "rom"], build / "build.log", timeout=120)
        cart_fatal_probe(probe, reason, build, out)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except SlotsBusy as error:
        print(error, file=sys.stderr)
        raise SystemExit(75)
