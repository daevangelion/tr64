#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Directed verification for Tomb Raider III Slice T3-2a (TR64_GAME=3):
- TR3 floor data (box unpack, split triangles 0x07..0x12, portal halves, monkey-swing 0x13)
- TR3 Lara movement & status (sprint 120 full-bar gate, crouch/crawl headroom < 762,
  quicksand 0x80 sinking & suffocation, poison pulse & cure, Save Crystal 180, Small Flame 331)
- Jungle & Temple entities (Monkey ally-hostility & AI waypoints, Tiger 24 HP, Piranhas 338,
  Secrets action 10, Doors/Switches/Boulders/Save)
- Real host live render of jungle.pack and temple.pack (> 70% non-black, skybox 355, Lara drawn)
- Stock 4 MiB N64 TR3 play cart builds for jungle.pack and temple.pack (size_gate, hot-check,
  formatter_link, refs-check, sram256k header 45440036, <= 24 code frames).
"""
import argparse
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/cook"))
import n64_core
import n64_pack

JUNGLE_PACK = Path("/tmp/tr64/evidence/m11/jungle/jungle.pack")
TEMPLE_PACK = Path("/tmp/tr64/evidence/m11/temple/temple.pack")
PPM_HEADER = b"P6\n320 240\n255\n"


def run_cmd(cmd, *, env=None, timeout=240):
    res = subprocess.run(cmd, cwd=str(ROOT), env=env, capture_output=True, text=True, timeout=timeout)
    if res.returncode != 0:
        raise RuntimeError(
            f"command failed ({res.returncode}): {' '.join(map(str, cmd))}\n"
            f"stdout:\n{res.stdout}\nstderr:\n{res.stderr}"
        )
    return res.stdout + res.stderr


def inspect_pack_secrets(pack_path: Path, level_name: str, expected_count: int) -> None:
    kinds = {t.name: i + 1 for i, t in enumerate(n64_core.TABLES)}
    secs = n64_pack.inspect(pack_path.read_bytes())[level_name]
    sec_data = secs[kinds["sectors"]][32:]
    fd_data = secs[kinds["floor_data"]][32:]
    fd = [struct.unpack_from(">H", fd_data, i)[0] for i in range(0, len(fd_data), 2)]
    secrets = set()
    split_ops = set()
    for i in range(0, len(sec_data), 8):
        fi = struct.unpack_from(">H", sec_data, i)[0]
        if not fi:
            continue
        p = fi
        while p < len(fd):
            w = fd[p]
            p += 1
            k = w & 0x1F
            if 0x07 <= k <= 0x12:
                split_ops.add(k)
                p += 1
            elif k in (1, 2, 3):
                p += 1
            elif k == 4:
                sub = (w >> 8) & 0x7F
                p += 1
                if sub in (2, 3, 4):
                    ctrl = fd[p]
                    p += 1
                    if ctrl & 0x8000:
                        if w & 0x8000:
                            break
                        continue
                while p < len(fd):
                    a = fd[p]
                    p += 1
                    ak = (a >> 10) & 0x1F
                    if ak == 10:
                        secrets.add(a & 0x3FF)
                    if ak in (1, 12):
                        ex = fd[p]
                        p += 1
                        if ex & 0x8000:
                            break
                    if a & 0x8000:
                        break
            if w & 0x8000:
                break
    assert len(secrets) == expected_count, (
        f"{level_name}: expected {expected_count} secrets, got {sorted(secrets)}"
    )
    assert len(split_ops) >= 8, f"{level_name}: too few split-triangle opcodes: {sorted(split_ops)}"
    print(f"PASS {level_name}.pack floor_data: secrets={sorted(secrets)} ({len(secrets)}), split_ops={len(split_ops)}")


def analyze_ppm(ppm_path: Path) -> dict:
    data = ppm_path.read_bytes()
    assert data.startswith(PPM_HEADER) and len(data) == len(PPM_HEADER) + 320 * 240 * 3
    rgb = data[len(PPM_HEADER):]
    non_black = 0
    colors = set()
    sky_nb = 0
    for y in range(240):
        row = y * 320 * 3
        for x in range(320):
            p = row + x * 3
            c = (rgb[p], rgb[p + 1], rgb[p + 2])
            if c != (0, 0, 0):
                non_black += 1
                colors.add(c)
                if y < 80:
                    sky_nb += 1
    return {"non_black": non_black, "distinct": len(colors), "sky_non_black": sky_nb}


def verify_cart_build(build_dir: Path, pack_path: Path, label: str, env: dict) -> None:
    rom_path = build_dir / "tr64.z64"
    rom_path.unlink(missing_ok=True)
    log = run_cmd([
        "make", "--no-print-directory", "-j4",
        f"BUILD_DIR={build_dir}",
        "TR64_GAME=3", "TR64_PLAY=1", "TR64_TEST=0",
        f"TR64_LEVEL_PACK={pack_path}",
        "rom",
    ], env=env, timeout=300)
    m_sz = re.search(r"size_gate PASS pool=image resident=(\d+) .* budget=(\d+)", log)
    m_hot = re.search(r"hot: .* window=(\d+) icache=(\d+) fits", log)
    m_fmt = re.search(r"formatter_link PASS", log)
    m_ref = re.search(r"refs-check PASS", log)
    pages = [int(x) for x in re.findall(r"tlbcode \S+: VA [0-9a-f]+, (\d+) pages", log)]
    assert m_sz and int(m_sz.group(1)) <= 622592, f"{label}: size_gate failed:\n{log}"
    assert m_hot and int(m_hot.group(1)) <= 16384, f"{label}: hot-check failed:\n{log}"
    assert m_fmt and m_ref, f"{label}: formatter_link/refs-check failed:\n{log}"
    assert sum(pages) <= 24, f"{label}: code frames {sum(pages)} > 24"

    rom_path = build_dir / "tr64.z64"
    rom_hdr = rom_path.read_bytes()[:64]
    assert rom_hdr[0x3C:0x40].hex() == "45440036", f"{label}: expected sram256k 45440036, got {rom_hdr[0x3C:0x40].hex()}"
    print(
        f"PASS TR3 4 MiB cart ({label}): rom={rom_path.stat().st_size} B, "
        f"resident={m_sz.group(1)}/{m_sz.group(2)} B, hot_window={m_hot.group(1)}/16384 B, "
        f"code_pages={sum(pages)}/24, sram=45440036"
    )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--build-dir", type=Path, required=True)
    ap.add_argument("--work-dir", type=Path, required=True)
    args = ap.parse_args()

    build = args.build_dir.resolve()
    work = args.work_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)
    work.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1", TMPDIR=str(work))

    # 1. Verify authored TR3 secrets & split-triangle opcodes in Jungle (6) and Temple (4).
    inspect_pack_secrets(JUNGLE_PACK, "jungle", 6)
    inspect_pack_secrets(TEMPLE_PACK, "temple", 4)

    # 2. Build & run live TR3 host behavioral tests + live 320x240 PPM renders for Jungle and Temple.
    for name, pack, expect_room, need_sky in (
        ("jungle", JUNGLE_PACK, 4, True),
        ("temple", TEMPLE_PACK, 18, False),
    ):
        hdir = build / f"host-{name}"
        run_cmd([
            "make", "--no-print-directory", "-j4",
            f"BUILD_DIR={hdir}", "TR64_GAME=3",
            f"TR64_LEVEL_PACK={pack}",
            "host",
        ], env=env, timeout=300)
        host_bin = hdir / "host/tr64-host"

        tlog = run_cmd([str(host_bin), "--tr3-test", name], env=env, timeout=60)
        assert "PASS synthetic floor tests" in tlog and "PASS live TR3 behavioral suite" in tlog, tlog
        print(f"PASS host --tr3-test {name}")

        ppm = work / f"{name}-live.ppm"
        plog = run_cmd([str(host_bin), "--tr3-play", name, "35", "--input", "8", "--shot", str(ppm)],
                       env=env, timeout=60)
        m_play = re.search(r"tr3-play: level=(\w+) ticks=35 .* room=(\d+) hp=1000", plog)
        m_ren = re.search(r"game-render: room=(\d+) .* drawables=(\d+) entities=(\d+) triangles=(\d+)", plog)
        assert m_play and m_ren, f"{name}: missing play/render log:\n{plog}"
        assert int(m_play.group(2)) == expect_room, f"{name}: expected room {expect_room}, got {m_play.group(2)}"
        assert int(m_ren.group(2)) >= 15 and int(m_ren.group(4)) >= 500, f"{name}: Lara/scene not drawn: {m_ren.groups()}"
        stats = analyze_ppm(ppm)
        pct = 100.0 * stats["non_black"] / 76800.0
        assert pct > 70.0 and stats["distinct"] >= 40, f"{name}: frame too blank: {stats}"
        if need_sky:
            assert stats["sky_non_black"] >= 10000, f"{name}: skybox 355 missing from top band: {stats}"
        print(
            f"PASS host live render {name}: room={m_play.group(2)} drawables={m_ren.group(2)} "
            f"tris={m_ren.group(4)} non_black={stats['non_black']}/76800 ({pct:.1f}%) "
            f"distinct={stats['distinct']} sky_band={stats['sky_non_black']}/25600"
        )

    # 3. Build stock 4 MiB N64 TR3 shipping play carts for Jungle and Temple.
    verify_cart_build(build / "cart-jungle", JUNGLE_PACK, "jungle", env)
    verify_cart_build(build / "cart-temple", TEMPLE_PACK, "temple", env)

    print("ALL TR3 SLICE T3-2a JUNGLE & TEMPLE VERIFICATION CHECKS PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
