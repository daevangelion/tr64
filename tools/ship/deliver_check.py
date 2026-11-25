#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Delivery's reading of the gate summary: every part of the gate ran on these ROMs and this pack.

The summary is written by boot.py from the base run on the delivered ROM, the power-cycle load run
on the same ROM, the death/level-end run and the pace run (game time against the clock under a 40 ms per frame load) on the gate ROM. Delivery recomputes what it can: the
ROM's md5, both build ids from the sources now (a changed source changes them), the pack's hash,
and whether the pack holds videos (then the video verdict is required)."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

sys.dont_write_bytecode = True
from boot import INVALID_RUN_EXIT, pack_video_names  # noqa: E402
from gate_logs import guest_identity  # noqa: E402


def sha256(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def failures(summary, *, rom_md5, build, config, gate_build, gate_config, pack_sha, gate_rom_md5, pack_videos):
    """What is wrong with the summary (empty: deliverable)."""
    if summary.get("invalid_run"):
        return [f"INVALID RUN: {summary['invalid_run']}"]
    bad = []
    def need(ok, what):
        if not ok:
            bad.append(what)
    save_load, death_end, pace = summary.get("save_load") or {}, summary.get("death_end") or {}, summary.get("pace") or {}
    need(summary.get("pass") is True, "the base run did not pass")
    need(summary.get("reload_exercised"), "level reload not exercised")
    need(summary.get("sound_exercised") and (summary.get("sound") or {}).get("pass"), "sound not verified")
    need((summary.get("cutscene") or {}).get("skip_logged"), "cutscene skip not exercised")
    need(summary.get("rom_md5") == rom_md5, "the summary is for another ROM")
    need(summary.get("build_id") == build, "the summary is for other sources")
    need(summary.get("config_id") == config, "the summary is for another configuration")
    need(summary.get("pack_sha256") == pack_sha, "the summary is for another pack")
    need(save_load.get("pass") is True, "the save, power cycle and load did not pass")
    need(save_load.get("build_id") == build, "the power cycle is for other sources")
    need(save_load.get("config_id") == config, "the power cycle is for another configuration")
    need(save_load.get("pack_sha256") == pack_sha, "the power cycle is for another pack")
    need(death_end.get("pass") is True, "the death page and level end did not pass")
    need(death_end.get("rom_md5") == gate_rom_md5, "the gate run is for another gate ROM")
    need(death_end.get("build_id") == gate_build, "the gate run is for other sources")
    need(death_end.get("config_id") == gate_config, "the gate run is for another configuration")
    need(death_end.get("pack_sha256") == pack_sha, "the gate run is for another pack")
    need(pace.get("pass") is True, "game time did not follow the clock (pace gate)")
    need(pace.get("rom_md5") == gate_rom_md5, "the pace run is for another gate ROM")
    need(pace.get("build_id") == gate_build, "the pace run is for other sources")
    need(pace.get("config_id") == gate_config, "the pace run is for another configuration")
    need(pace.get("pack_sha256") == pack_sha, "the pace run is for another pack")
    need(40 in [m.get("burn_ms") for m in pace.get("marks") or []], "the pace run lacks the 40 ms case")
    if pack_videos:
        need(summary.get("video_gate") is True, "the pack holds videos: the video verdict is required")
        need(sorted(summary.get("pack_videos") or []) == sorted(pack_videos), "the summary's video list is not the pack's")
    return bad


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--summary", type=Path, required=True)
    parser.add_argument("--rom-md5", required=True)
    parser.add_argument("--identity-header", type=Path, required=True)
    parser.add_argument("--gate-identity-header", type=Path, required=True)
    parser.add_argument("--gate-rom", type=Path, required=True)
    parser.add_argument("--pack", type=Path, required=True)
    args = parser.parse_args()
    build, config = guest_identity.expected_identity(None, args.identity_header)
    gate_build, gate_config = guest_identity.expected_identity(None, args.gate_identity_header)
    summary = json.loads(args.summary.read_text())
    bad = failures(summary, rom_md5=args.rom_md5, build=build, config=config,
                   gate_build=gate_build, gate_config=gate_config, pack_sha=sha256(args.pack),
                   gate_rom_md5=hashlib.md5(args.gate_rom.read_bytes()).hexdigest(),
                   pack_videos=pack_video_names(args.pack))
    if bad:
        print("delivery refused: " + "; ".join(bad), file=sys.stderr)
        return INVALID_RUN_EXIT if summary.get("invalid_run") else 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
