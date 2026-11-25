# SPDX-License-Identifier: MIT
"""Fail closed even with PYTHONOPTIMIZE set."""
import json
import pathlib
import sys
path, dragon, tiny = sys.argv[1:]
manifest = json.loads(pathlib.Path(path).read_text())
if manifest.get("libdragon_rev") != dragon or manifest.get("tiny3d_rev") != tiny:
    raise SystemExit("sdk-check FAIL: manifest revision mismatch")
print(f"sdk-check PASS libdragon={dragon} tiny3d={tiny}")
