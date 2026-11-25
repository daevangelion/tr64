#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Real N64 medium write, emulator power-off, and second-run SRAM load."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("guest_identity", ROOT / "test/run/guest_identity.py")
guest_identity = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guest_identity)


def execute(command, *, env=None, expected=0, stdout=None):
    result = subprocess.run(["rtk", *map(str, command)], cwd=ROOT, env=env,
                            stdout=stdout, stderr=subprocess.STDOUT)
    if result.returncode != expected:
        raise AssertionError(f"exit {result.returncode}, expected {expected}: {command}")


def sha(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1048576), b""):
            digest.update(block)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ares", type=Path, required=True)
    parser.add_argument("--host", type=Path, required=True)
    parser.add_argument("--pack", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    expected = args.out / "host-expected.srm"
    if expected.exists():
        raise ValueError("fresh evidence directory required; never overwrite an earlier save device")
    execute([args.host, "write", expected])
    erased = args.out / "erased.srm"
    erased.write_bytes(b"\xff" * 32768)
    short = args.out / "short.srm"
    short.write_bytes(b"\xff")
    manifest = {"ares": str(args.ares), "ares_sha256": sha(args.ares), "roms": {},
                "kind": "medium storage fixture, not a Game-state serializer or menu acceptance"}
    for operation, source, destination in (
            ("write", erased, args.out / "written.srm"),
            ("read", args.out / "written.srm", args.out / "loaded.srm")):
        with (args.out / f"{operation}-build.log").open("w") as log:
            execute(["make", "-j2", f"BUILD_DIR={args.build}", "TR64_TEST=1",
                     f"TR64_SCENE_TEST_SOURCES=test/ui/save-{operation}.c",
                     f"TR64_LEVEL_PACK={args.pack}", "rom"], stdout=log)
        rom = args.build / "tr64.z64"
        expected_build = guest_identity.expected_identity(args.build)
        with rom.open("rb") as stream:
            header = stream.read(64)
        assert header[0x3f] >> 4 == 3, "ROM does not declare SRAM 256K"
        manifest["roms"][operation] = {"sha256": sha(rom), "save_nibble": 3}
        env = os.environ.copy()
        env.update(ARES_BINARY=str(args.ares), ARES_SRAM_IN=str(source), ARES_SRAM_OUT=str(destination),
                   ARES_EXPANSION_PAK="false", TMPDIR=str(args.out / "tmp"))
        if operation == "write":
            bad = args.out / "short-input"
            bad.mkdir()
            badenv = dict(env, ARES_SRAM_IN=str(short))
            execute(["test/run/ares.sh", bad, rom, bad / "ares.log", "90"], env=badenv, expected=2)
            assert "not a readable 32768-byte image" in (bad / "ares.log").read_text()
            assert not destination.exists(), "bad import reached guest exit/export"
        run = args.out / operation
        run.mkdir()
        execute(["test/run/ares.sh", run, rom, run / "ares.log", "90"], env=env)
        log = (run / "ares.log").read_text()
        build_id, config_id = guest_identity.require_identity(log, expected_build)
        manifest["roms"][operation].update(build_id=build_id, config_id=config_id)
        assert "SRAM probe: " + ("write" if operation == "write" else "load") + " PASS" in log
        assert "TESTHARNESS SRAM out " in log
        assert destination.read_bytes() == expected.read_bytes(), "host/native wire or persisted bytes differ"
        manifest["roms"][operation]["sram_sha256"] = sha(destination)
        assert list(run.glob("ares-test-*.ppm")), "cart picture missing"
    execute([args.host, "read", args.out / "loaded.srm"])
    manifest["pass"] = True
    (args.out / "report.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print("N64 SRAM power-cycle PASS: private ares, 4MiB, 3 real medium slots/settings, host/native identical bytes, bad-import control")


if __name__ == "__main__":
    main()
