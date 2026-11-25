# SPDX-License-Identifier: MIT
"""Integer-format differential/overflow/stream tests; optional real old-ELF negative."""
import argparse
import importlib.util
import os
from pathlib import Path
import shlex
import sys
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("scene", ROOT / "test/run/scene.py")
scene = importlib.util.module_from_spec(spec)
spec.loader.exec_module(scene)
p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--old-elf", type=Path)
a = p.parse_args()
build = Path(os.environ["BUILD_DIR"]).resolve() / "fmt-tests"
build.mkdir(parents=True, exist_ok=True)
cc = shlex.split(os.environ.get("HOST_CC", "cc"))
scene.command([*cc, "-m32", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-I", ROOT,
               ROOT / "engine/core/fmt.c", ROOT / "engine/test/fmt-test.c", "-o", build / "fmt-test"],
              build / "build.log", timeout=60)
scene.command([build / "fmt-test"], build / "unit.log", timeout=5)
print((build / "unit.log").read_text().strip())
if a.old_elf:
    scene.command([sys.executable, ROOT / "test/run/fmt-link-gate.py", a.old_elf],
                  build / "link-negative.log", timeout=10, expected=1)
    text = (build / "link-negative.log").read_text()
    if "formatter_link OVER: retained libc printf/float engines:" not in text:
        raise AssertionError("old-ELF negative failed for wrong reason")
    print("formatter link negative PASS: real old ELF retains forbidden engines")
