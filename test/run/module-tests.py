# SPDX-License-Identifier: MIT
"""Exercise the real Makefile module discovery and unit-fragment generator."""
import json
import os
import pathlib
import subprocess
import sys
import tempfile
ROOT = pathlib.Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="module-tests-", dir=os.environ["TMPDIR"]) as temporary:
    root = pathlib.Path(temporary)
    for directory in ("engine/test", "engine/code", "engine/res", "plat/host", "game", "tools/cook"):
        (root / directory).mkdir(parents=True, exist_ok=True)
    for name in ("units.py", "units.def"):
        (root / "engine/code" / name).symlink_to(ROOT / "engine/code" / name)
    (root / "engine/test/module.mk").write_text("# SPDX-License-Identifier: MIT\nTR64_ENGINE_SOURCES += engine/test/common.c\nTR64_UNIT_SOURCES += engine/test/unit-extra.c\nTR64_CODE_UNIT_FRAGMENTS += engine/test/extra.def\nTR64_GENERATED_HEADERS += $(GEN)/module-extra.h\n$(GEN)/module-extra.h:\n\t@mkdir -p $(dir $@)\n\t@printf '/* synthetic */\\n' >$@\n.PHONY: inspect\ninspect:\n\t@printf '%s\\n' 'HOST=$(HOST_OBJECTS)' 'CART=$(ROM_OBJECTS)' 'LIBS=$(N64_LDFLAGS)' 'SDK=$(TR64_SDK_DEPS)' 'COOK=$(TR64_COOK_TARGETS):$(EXTRA_COOK)'\n")
    (root / "engine/test/extra.def").write_text("# SPDX-License-Identifier: MIT\ninit *extra-init.o\nfixture *fixture.o\n")
    (root / "plat/host/module.mk").write_text("# SPDX-License-Identifier: MIT\nTR64_HOST_SOURCES += plat/host/extra.c\nTR64_CART_SOURCES += plat/n64/extra.S\nTR64_CART_LIBS += -lmodule_fixture\nTR64_SDK_DEPS += sdk-fixture\n")
    (root / "game/module.mk").write_text("# SPDX-License-Identifier: MIT\nTR64_ENGINE_SOURCES += game/extra.c\n")
    (root / "engine/res/module.mk").write_text("# SPDX-License-Identifier: MIT\nifeq ($(TR64_LEVEL_LOADER),1)\nTR64_ENGINE_SOURCES += engine/res/scene.c\nendif\n")
    (root / "tools/cook/cook.mk").write_text("# SPDX-License-Identifier: MIT\nTR64_COOK_TARGETS := 1\n")
    (root / "tools/cook/extra.mk").write_text("# SPDX-License-Identifier: MIT\nEXTRA_COOK := included\n")
    build = root / "build"
    def make(*targets):
        return subprocess.check_output(["rtk", "proxy", "make", "--no-print-directory", "-f", ROOT / "Makefile", f"BUILD_DIR={build}", "TR64_LEVEL_LOADER=1", *targets], cwd=root, text=True)
    report = make("inspect")
    lines = dict(line.split("=", 1) for line in report.splitlines())
    if "engine/res/scene.o" not in lines["HOST"] or "engine/res/scene.o" not in lines["CART"]:
        raise AssertionError("default loader module not selected")
    isolated = make("TR64_LEVEL_LOADER=0", "TR64_SCENE_TEST_SOURCES=engine/test/probe.c", "inspect")
    if "engine/res/scene.o" in isolated or "engine/test/probe.o" not in isolated or "engine/test/common.o" not in isolated:
        raise AssertionError("loader isolation clobbered common modules or lost test source")
    for source in ("engine/test/common.o", "game/extra.o"):
        if source not in lines["HOST"] or source not in lines["CART"]:
            raise AssertionError("common module source missing")
    if "plat/host/extra.o" not in lines["HOST"] or "unit-extra.o" not in lines["HOST"] or "unit-extra.o" in lines["CART"]:
        raise AssertionError("host/unit module source mismatch")
    if "plat/n64/extra.o" not in lines["CART"] or "plat/n64/extra.o" in lines["HOST"]:
        raise AssertionError("cart assembly module source mismatch")
    if "-lmodule_fixture" not in lines["LIBS"] or lines["SDK"] != "sdk-fixture" or lines["COOK"] != "1:included":
        raise AssertionError("SDK/link/cook module hooks missing")
    make(str(build / "generated/module-extra.h"), str(build / "generated/units.h"))
    units = json.loads((build / "units.json").read_text())
    if list(units) != ["init", "test", "fixture"] or units["init"][-1] != "*extra-init.o" or units["fixture"] != ["*fixture.o"]:
        raise AssertionError("unit fragments were not merged")
    if "X(fixture)" not in (build / "generated/units.h").read_text():
        raise AssertionError("fragment unit missing from dispatch list")
    (root / "engine/test/extra.def").write_text("invalid-unit! *x.o\n")
    rejected = subprocess.run(["rtk", "proxy", sys.executable, ROOT / "engine/code/units.py", ROOT / "engine/code/units.def", build / "bad.json", build / "bad.h", root / "engine/test/extra.def"], capture_output=True, text=True)
    if rejected.returncode == 0 or "invalid code-unit row" not in rejected.stderr:
        raise AssertionError("malformed unit fragment escaped")
    (root / "engine/test/module.mk").unlink()
    make(str(build / "generated/units.h"))
    if list(json.loads((build / "units.json").read_text())) != ["init", "test"]:
        raise AssertionError("removed module left stale code units")
print("module fragments PASS (common/host/cart/unit sources, SDK/libs, headers, merged/removed units, cook wildcard, malformed control)")
