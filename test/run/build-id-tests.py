# SPDX-License-Identifier: MIT
"""Content identity controls; synthetic tool bytes are not compilation evidence."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="tr64-build-id-", dir=os.environ.get("TMPDIR")) as tmp:
    root = Path(tmp) / "source"
    tool = root / "test/run/build-id.py"
    tool.parent.mkdir(parents=True)
    shutil.copyfile(ROOT / "test/run/build-id.py", tool)
    sources = [root / "Makefile", root / "engine/probe.c", root / "engine/code/units.py", root / "tools/routec/routec.py"]
    for path in sources:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("# synthetic identity fixture\n")
    prefix = Path(tmp) / "toolchain"
    (prefix / "bin").mkdir(parents=True)
    cc1 = prefix / "cc1"
    cc1.write_bytes(b"synthetic compiler body")
    gcc = prefix / "bin/mips64-elf-gcc"
    gcc.write_text("#!/bin/sh\nprintf '%s\\n' '" + str(cc1) + "'\n")
    gcc.chmod(0o755)
    assembler = prefix / "bin/mips64-elf-as"
    linker = prefix / "bin/mips64-elf-ld"
    assembler.write_bytes(b"synthetic assembler")
    linker.write_bytes(b"synthetic linker")
    out = Path(tmp) / "build_id.h"
    command = [sys.executable, tool, "--toolchain-prefix", prefix, "-o", out]
    def identity():
        subprocess.run(command, check=True)
        return out.read_bytes()
    previous = identity()
    stamp = out.stat().st_mtime_ns
    assert identity() == previous and out.stat().st_mtime_ns == stamp
    for path in [*sources, cc1, assembler, linker, gcc, tool]:
        path.write_bytes(path.read_bytes() + b"\n")
        current = identity()
        assert current != previous, str(path)
        previous = current
    print("build-id PASS: stable content/mtime; C/Makefile/unit/route generators/compiler/assembler/linker/identity-generator changes detected")

with tempfile.TemporaryDirectory(prefix="tr64-toolfile-id-", dir=os.environ.get("TMPDIR")) as tmp:
    root = Path(tmp)
    build = root / "build"
    # There is no TR64_ROM_DEPS entry for this existing, externally supplied member.
    member = root / "extra ROM member.bin"
    member.write_bytes(b"first")
    stamp = member.stat()
    command = ["rtk", "make", "--no-print-directory", f"BUILD_DIR={build}",
               f'N64_TOOLFILES=--align 256 "{member}"', "cart-identity"]

    def cart_identity():
        subprocess.run(command, cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
        return (build / "generated/cart_identity.h").read_text().splitlines()

    before = cart_identity()
    assert cart_identity() == before
    member.write_bytes(b"other")
    os.utime(member, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
    after = cart_identity()
    assert before[:2] == after[:2], (before, after)
    assert before[2] != after[2], "in-place N64_TOOLFILES replacement escaped cart identity"
    print("cart identity PASS: in-place toolfile bytes changed the configuration, not the source id")

with tempfile.TemporaryDirectory(prefix="tr64-cart-flag-id-", dir=os.environ.get("TMPDIR")) as tmp:
    root = Path(tmp)
    build = root / "build"
    probe = root / "probe.inc"
    probe.write_text(".pushsection .boot\n.word 0x12345678\n.popsection\n")
    stamp = probe.stat()
    obj = build / "cart/plat/n64/start.o"
    cart_header = build / "generated/cart_identity.h"
    make = ["rtk", "proxy", "make", "--no-print-directory", f"BUILD_DIR={build}"]
    asm_flags = f"N64_ASFLAGS=-march=vr4300 -mabi=o64 -include {probe}"

    def refused(setting, option, target="cart-identity"):
        result = subprocess.run([*make, setting, target], cwd=ROOT, capture_output=True, text=True)
        variable = setting.split("=", 1)[0]
        assert result.returncode != 0 and f"{variable}: {option}" in result.stderr, result.stderr
        return next(line for line in result.stderr.splitlines() if f"{variable}: {option}" in line)

    print(refused(asm_flags, "-include", str(obj)))
    assert not obj.exists() and not cart_header.exists()
    # Reproduce direct assembly inputs, not the Python predicate in isolation.
    external = root / "home/external"
    external.mkdir(parents=True)
    (external / "probe.inc").write_bytes(probe.read_bytes())
    with_tilde = "N64_ASFLAGS=-march=vr4300 -mabi=o64 -I ~/external"
    result = subprocess.run([*make, with_tilde, str(obj)], cwd=ROOT,
                            env=dict(os.environ, HOME=str(external.parent)), capture_output=True, text=True)
    message = "N64_ASFLAGS: shell metacharacter '~' is not permitted"
    assert result.returncode != 0 and message in result.stderr, result.stderr
    print(message)
    cc = Path(os.environ.get("N64_GCCPREFIX", "/opt/mips64-toolchain")) / "bin/mips64-elf-gcc"
    print(refused(f"N64_CC={cc} -include {probe}", "expected exactly one pinned tool path", str(obj)))
    assert not obj.exists() and not cart_header.exists()
    for var in ("N64_CXX", "N64_AS", "N64_LD", "N64_OBJCOPY", "N64_OBJDUMP", "N64_NM", "N64_SIZE",
                "N64_STRIP", "N64_TLBCODE_TOOL", "N64_SYM", "N64_ELFCOMPRESS", "N64_TOOL", "N64_ED64ROMCONFIG", "CXX"):
        refused(f"{var}=/bin/true", "expected exactly one pinned tool path")
    for character in ("~", "$$", "`", "*", "?", "[", "]", "{", "}", "\\", ";", "|", "&", "<", ">", "!", "#", "(", ")"):
        refused(f"N64_TOOLFLAGS=--title 'a{character}b'", "shell metacharacter")
    refused('N64_TOOLFLAGS="--header" /dev/null', "quote may not hide an option")
    refused('N64_ASFLAGS=-I"not-a-standalone-word"', "unsupported quoting")
    refused("N64_TOOLFLAGS=--toc\nN64_TOOLFLAGS=--title spoof", "newlines are not permitted")
    for setting, option in (
        (f"N64_CFLAGS=-imacros{probe}", "-imacros"),
        (f"N64_CFLAGS=-isystem {root}", "-isystem"),
        (f"N64_CFLAGS=-I{root}", "-I"),
        (f"N64_LDFLAGS=-L{root}", "-L"),
        (f"N64_ASFLAGS=@{probe}", "@"),
        (f"N64_ASFLAGS=-Wa,@{probe}", "-Wa,@"),
        (f"N64_LDFLAGS=--wrap @{probe}", "@"),
        (f"N64_CFLAGS=-specs={probe}", "-specs="),
        (f"LDFLAGS=-T {probe}", "-T"),
        (f"N64_CFLAGS=-Wl,-T,{probe}", "-Wl,-T"),
        (f"N64_CFLAGS=-Wl,--just-symbols={probe}", "-Wl,--just-symbols"),
        (f"N64_CFLAGS=-Wl,-R,{probe}", "-Wl,-R"),
        (f"N64_TOOLFLAGS=--header {probe}", "--header"),
        (f"N64_TOOLFILES=@{probe}", "@"),
    ):
        refused(setting, option)

    def assemble():
        subprocess.run([*make, asm_flags, f"TR64_CART_INPUTS={probe}", str(obj)], cwd=ROOT, check=True,
                       stdout=subprocess.DEVNULL)
        return cart_header.read_text().splitlines(), obj.read_bytes()

    before, first = assemble()
    probe.write_text(".pushsection .boot\n.word 0x87654321\n.popsection\n")
    os.utime(probe, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
    after, second = assemble()
    assert before[:2] == after[:2] and before[2] != after[2]
    assert b"\x12\x34\x56\x78" in first and b"\x87\x65\x43\x21" in second and first != second
    print("cart flags PASS: undeclared inputs refused; registered in-place -include changed config and rebuilt start.o")
