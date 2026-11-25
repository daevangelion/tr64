# SPDX-License-Identifier: MIT
"""Calibrate the 32-bit host against this cart ELF, not a guessed image."""
import importlib.machinery
import importlib.util
import pathlib
import sys
sys.dont_write_bytecode = True
loader = importlib.machinery.SourceFileLoader("size_gate", str(pathlib.Path(__file__).with_name("size_gate")))
spec = importlib.util.spec_from_loader(loader.name, loader)
gate = importlib.util.module_from_spec(spec)
loader.exec_module(gate)
elf, output, *other_games = map(pathlib.Path, sys.argv[1:])
extent = next(symbol["address"] for symbol in gate.symbols(elf) if symbol["name"] == "__bss_end") - 0x80000000
text = f"/* generated from linked cart ELF */\n#define TR64_CART_EXTENT {extent}u\n#define TR64_CART_CARVE_START {(extent+4095)&~4095}u\n"
units = {}
for image in (elf, *other_games):
    for row in gate.sections(image):
        if row["unit"]:
            units.setdefault(row["name"].split(".")[-1], row["bytes"] // 4096)
for name, pages in units.items():
    text += f"#define TR64_UNIT_PAGES_{name} {pages}u\n"
output.parent.mkdir(parents=True, exist_ok=True)
if not output.exists() or output.read_text() != text:
    output.write_text(text)
