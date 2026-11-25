#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Source-content identity. Ported from MIT ac3n64 04134cc."""
import argparse
import hashlib
import pathlib
import subprocess
ap = argparse.ArgumentParser()
ap.add_argument("-o", required=True)
ap.add_argument("--test", choices=("0", "1"), default="0")
ap.add_argument("--level-loader", choices=("0", "1"), default="1")
ap.add_argument("--game", choices=("1", "2", "3"), default="1")
ap.add_argument("--scene-test-source", action="append", default=[])
ap.add_argument("--toolchain-prefix", required=True)
args = ap.parse_args()
root = pathlib.Path(__file__).resolve().parents[2]
hash_ = hashlib.sha256()
hash_.update(f"TR64_TEST={args.test}".encode())
hash_.update(f"TR64_LEVEL_LOADER={args.level_loader}".encode())
hash_.update(f"TR64_GAME={args.game}".encode())
for source in sorted(args.scene_test_source):
    hash_.update(b"SCENE_TEST_SOURCE=" + source.encode() + b"\0")
prefix = pathlib.Path(args.toolchain_prefix)
cc = prefix / "bin/mips64-elf-gcc"
cc1 = pathlib.Path(subprocess.check_output([cc, "-print-prog-name=cc1"], text=True).strip())
for name, binary in (("gcc", cc), ("cc1", cc1),
                     ("as", prefix / "bin/mips64-elf-as"),
                     ("ld", prefix / "bin/mips64-elf-ld")):
    with binary.open("rb") as source:
        hash_.update(b"TOOLCHAIN:" + name.encode() + b"\0" +
                     hashlib.file_digest(source, "sha256").digest())
paths = [root / "Makefile"]
for directory in ("engine", "plat", "game", "include/tr64", "tools"):
    paths.extend(p for p in (root / directory).rglob("*") if p.suffix in (".c", ".h", ".S", ".def", ".mk", ".py", ".ld"))
paths.append(pathlib.Path(__file__).resolve())
for path in sorted(paths):
    hash_.update(str(path.relative_to(root)).encode() + b"\0" + path.read_bytes())
text = f'#define TR64_TEST {args.test}\n#define TR64_BUILD_ID "{hash_.hexdigest()[:12].upper()}"\n'
out = pathlib.Path(args.o)
out.parent.mkdir(parents=True, exist_ok=True)
if not out.exists() or out.read_text() != text:
    out.write_text(text)
