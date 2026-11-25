# SPDX-License-Identifier: MIT
"""Generate SDK selectors and a shared dispatch list from base + fragments.
Usage: units.py [--omit UNIT]... base.def output.json output.h [fragment.def ...].
Repeated unit names append selectors; input order and first occurrence win.
"""
import json
import pathlib
import re
import sys
omit = set()
args = sys.argv[1:]
while len(args) >= 2 and args[0] == "--omit":
    omit.add(args[1])
    args = args[2:]
rows = {}
for source in [args[0], *args[3:]]:
    for number, line in enumerate(pathlib.Path(source).read_text().splitlines(), 1):
        words = line.split("#", 1)[0].split()
        if not words:
            continue
        name, *patterns = words
        if not patterns or not re.fullmatch(r"[a-z][a-z0-9_]*", name):
            raise SystemExit(f"{source}:{number}: invalid code-unit row")
        if name in omit:
            continue
        selectors = rows.setdefault(name, [])
        selectors.extend(pattern for pattern in patterns if pattern not in selectors)
for path, text in [(pathlib.Path(args[1]), json.dumps(rows, indent=2) + "\n"),
                   (pathlib.Path(args[2]), "/* generated */\n#define TR64_UNITS(X) " + " ".join(f"X({n})" for n in rows) + "\n")]:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text() != text:
        path.write_text(text)
