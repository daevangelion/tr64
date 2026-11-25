#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check that the linked cart places plat/n64/hot.ld exactly as written.

usage: hot-check.py LINKER_MAP HOT_LD [--elf CART_ELF --objdump MIPS_OBJDUMP | --no-render-stack]

Reads the linker map's .text output section. Every input section matching an
entry of the `.text` block of HOT_LD must come first in .text, starting at
__text_start, in the order of the entries, with nothing else between them. The
`.sbss` and `.bss` blocks are checked the same way for the hot data group
(section 6.2): the start anchor is the first object of .sbss on an 8 KiB
boundary, the listed statics come first in .bss in order and start where .sbss
ends, and the group's extent (anchor to the end anchor) is reported against
the 8 KiB data cache. The line `@window` in a
comment of HOT_LD marks where the render window starts: its extent (to the end
of the list) is reported against the I-cache size, as is what precedes it. Also
reported: entries that matched nothing (a renamed or removed function). Fails
only when the placement differs from the file; the extent is a report, not a
gate (docs/architecture.md section 6.1).
Renderer carts also require an ELF entry-stack-pointer check on the one-view
bridge call chain. Only level-loader-free carts opt out.
The ROM rule additionally checks the selected pool table against the actual
aligned image end and records the cache phase of the carved rows.
"""
import argparse
import fnmatch
import pathlib
import re
import shutil
import subprocess
import sys
sys.dont_write_bytecode = True

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.append(str(ROOT / "tools/cook"))
from pool_rows import RDRAM_BYTES, pool_rows, pool_table, reserve_bytes

ICACHE = 16384
DCACHE = 8192

# d7e6432153, /tmp/tr64/evidence/m8/split/stack-match/stack-placement.md.
# Re-derive with MIPS objdump -d: from tr64_scene_tick, follow the direct bridge
# path through game_scene_render and tr64_render_game_scene_frame. Sum the
# callers' SP reservations (144 + 112 + 216 + 280 + 264, plus 2352 for items).
# This checks entry SP relative to the scene tick, not local variable offsets,
# upstream/boot stack placement, or runtime interrupt entry.
RENDER_ENTRY_SP_DEPTHS = (1016, 3368)  # room/static, live item, modulo 8 KiB
# The Great Wall's single-player render wrapper has no TR1 split preview
# call, so its linked room/item path reserves eight fewer bytes.
TR2_RENDER_ENTRY_SP_DEPTHS = (1008, 3360)
# Shipping play carts omit the route harness (tr64_scene_tick 144 B +
# game_scene_render 112 B = 256 B); verify the live renderer chain from
# tr64_render_game_scene_frame onward.
PLAY_RENDER_ENTRY_SP_DEPTHS = (760, 3112)
TR2_PLAY_RENDER_ENTRY_SP_DEPTHS = (752, 3104)
# Explicit fixture profiles change the scene wrapper's register saves or
# reserve a copy of the camera. Evidence: review-fixes/entry-sp/wrapper-fixture-depths.log.
FIXTURE_SCENE_FRAME_ADJUSTMENT = {(0, 0): 0, (1, 0): -8, (0, 1): 72, (1, 1): 72}

# Only the instruction shapes used by the guarded VR4300 callers are proved.
# Ordinary operations write at most their first operand; these stores, nop,
# and dmult do not write a GPR. Reject unfamiliar encodings rather than
# assume they preserve SP or ordinary control flow.
GPR_OPS = frozenset("""
    addiu addu and andi daddiu daddu dsll dsll32 dsra dsra32 dsubu
    lbu ld ldl ldr lh lhu li lui lw lwl lwr lwu mflo move negu or ori
    sll sllv slt slti sltiu sltu sra srl srlv subu xor
""".split())
NON_GPR_OPS = frozenset("dmult nop sb sd sdl sdr sh sw".split())
BRANCH_OPS = frozenset("""
    b beq beql beqz beqzl bgez bgezl bgtz bgtzl blez blezl
    bltz bltzl bne bnel bnez bnezl
""".split())
TRANSFER_OPS = BRANCH_OPS | {"j", "jal", "jr"}
TARGET = re.compile(r"([0-9a-f]+)(?: <([^>]+)>)?")
SP_DELTA = re.compile(r"sp,sp,(-?[0-9]+)")
TEXT_FUNCTION = re.compile(r"^([0-9a-f]+)[ \t]+[lgw][ \t]+F \.text[ \t]+([0-9a-f]+)[ \t]+(\S+)$", re.M)


def render_frame_depth(functions, caller, callees, guarded_entries):
    """Prove a fixed SP at the named calls, not just a textual stack debit."""
    for name in (caller, *callees):
        if name not in functions or not re.search(r"^\s*[0-9a-f]+:", functions[name][2], re.M):
            raise ValueError(f"missing linked disassembly for {name}")

    entry, end, text = functions[caller]
    rows = [(int(address, 16), op, operands) for address, op, operands in
            re.findall(r"^[ \t]*([0-9a-f]+):[ \t]+([\w.]+)(?:[ \t]+([^\n]*))?$", text, re.M)
            if int(address, 16) < end]
    addresses = [address for address, _, _ in rows]
    if not rows or (end - entry) % 4 or addresses != list(range(entry, end, 4)):
        raise ValueError(f"{caller}: incomplete instruction stream")
    index = {addr: i for i, addr in enumerate(addresses)}
    transfers, slots, targets, returns = set(), set(), set(), set()
    writes = {}
    calls = dict.fromkeys(callees, 0)

    def reject(reason):
        raise ValueError(f"{caller}: {reason}")

    for i, (_, op, operands) in enumerate(rows):
        if op in GPR_OPS:
            if operands.split(",", 1)[0].strip() == "sp":
                delta = SP_DELTA.fullmatch(operands)
                if op not in ("addiu", "daddiu") or delta is None:
                    reject(f"unexpected SP write at {addresses[i]:08x}")
                writes[i] = int(delta[1])
        elif op in TRANSFER_OPS:
            if i in slots or i + 1 == len(rows):
                reject(f"invalid control transfer/delay slot at {addresses[i]:08x}")
            transfers.add(i)
            slots.add(i + 1)
            if op == "jr":
                if operands != "ra":
                    reject(f"unproved indirect jump at {addresses[i]:08x}")
                returns.add(i)
                continue
            target = TARGET.fullmatch(operands.rsplit(",", 1)[-1].strip())
            if target is None:
                reject(f"unproved branch target at {addresses[i]:08x}")
            address, name = int(target[1], 16), target[2]
            if op != "jal" and address in index:
                targets.add(index[address])
            elif op in ("j", "jal") and name in functions and functions[name][0] == address and address not in index:
                if op == "j":
                    if address in guarded_entries:
                        reject(f"sibling jump into guarded renderer chain: {name} at {addresses[i]:08x}")
                    returns.add(i)
                elif name in calls:
                    calls[name] += 1
            else:
                reject(f"transfer does not reach a local instruction or another function entry at {addresses[i]:08x}")
        elif op not in NON_GPR_OPS:
            reject(f"unproved instruction {op} at {addresses[i]:08x}")

    if calls != callees:
        reject(f"required direct calls {callees}, found {calls}")
    debit = next(iter(writes), None)
    prologue_end = min(transfers)
    if debit is None or debit >= prologue_end or writes[debit] >= 0:
        reject("missing unconditional SP debit in the straight-line entry prologue")
    if targets & slots or any(t < prologue_end for t in targets):
        reject("branch into the prologue or a delay slot")

    # A terminal return is jr ra or a sibling j to a resolved function entry
    # outside the guarded chain, never a local jump. The whole frame must be
    # restored in its delay slot or on a suffix that branches cannot enter.
    size = -writes[debit]
    restored = set()
    for i, delta in writes.items():
        if i == debit:
            continue
        if delta != size:
            reject(f"unexpected SP adjustment at {addresses[i]:08x}")
        if i - 1 in returns:
            ret = i - 1
        elif i not in slots:
            ret = next((j for j in range(i + 1, len(rows)) if j in transfers), None)
            if ret not in returns or any(i <= t <= ret for t in targets):
                reject("SP restore is not on a straight-line terminal return")
        else:
            reject("SP restore is not on a terminal return")
        if ret in restored:
            reject("multiple SP restores on one terminal return")
        restored.add(ret)
    if not returns or restored != returns:
        reject("terminal return without matching SP restore")
    return size


def expected_render_entry_sp(pose_header, base=RENDER_ENTRY_SP_DEPTHS):
    if pose_header is None:
        return base
    header = pose_header.read_text()
    flags = []
    for name in ("TR64_RENDER_SPLIT", "TR64_RENDER_POSED_GAME_CAMERA"):
        matches = re.findall(rf"^#define {name} ([01])$", header, re.M)
        if len(matches) != 1:
            raise ValueError(f"missing or ambiguous {name} in {pose_header}")
        flags.append(int(matches[0]))
    delta = FIXTURE_SCENE_FRAME_ADJUSTMENT[tuple(flags)]
    return tuple((value + delta) % DCACHE for value in base)


def check_render_entry_sp(elf, objdump, pose_header, expected, *, play=False):
    """Verify the entry SP displacement along the linked one-view bridge path."""
    guide = ("remeasure the one-view bridge room/item entry stack pointer; "
             "this guard does not check local offsets")
    chain = (("tr64_render_game_scene_frame", "tr64_render_game_frame", "frame") if play else
             ("tr64_scene_tick", "game_scene_render", "tr64_render_game_scene_frame",
              "tr64_render_game_frame", "frame"))
    draw, items = "tr64_render_draw_object", "tr64_render_draw_items"
    try:
        if elf is None:
            raise ValueError("renderer build requires --elf disassembly")
        if expected is None:
            base = PLAY_RENDER_ENTRY_SP_DEPTHS if play else RENDER_ENTRY_SP_DEPTHS
            expected = expected_render_entry_sp(pose_header, base)
        objdump_cmd = (["rtk"] if shutil.which("rtk") else []) + [
            str(objdump), "-tdz", "-j", ".text", "--no-show-raw-insn", elf]
        text = subprocess.check_output(objdump_cmd, text=True)
        chunks = re.split(r"^([0-9a-f]+) <([^>]+)>:\n", text, flags=re.M)
        bodies = {(int(addr, 16), name): body for addr, name, body in
                  zip(chunks[1::3], chunks[2::3], chunks[3::3], strict=True)}
        functions = {}
        for addr, size, name in TEXT_FUNCTION.findall(chunks[0]):
            entry = int(addr, 16)
            functions[name] = (entry, entry + int(size, 16), bodies.get((entry, name), ""))
        if not functions:
            raise ValueError("missing .text disassembly")
        guarded = (*chain, draw, items)
        for name in guarded:
            if name not in functions:
                raise ValueError(f"missing linked disassembly for {name}")
        entries = {functions[name][0] for name in guarded}
        base = sum(render_frame_depth(functions, caller, {callee: 1}, entries)
                   for caller, callee in zip(chain, chain[1:]))
        base += render_frame_depth(functions, chain[-1], {draw: 2, items: 2}, entries)
        room = base % DCACHE
        item = (base + render_frame_depth(functions, items, {draw: 1}, entries)) % DCACHE
        if (room, item) != expected:
            raise ValueError(f"room/item depths modulo {DCACHE}: {room}/{item}, expected {expected[0]}/{expected[1]}")
    except (OSError, subprocess.CalledProcessError, ValueError) as error:
        raise ValueError(f"renderer entry stack pointer: {error}; {guide}") from error
    print(f"hot entry SP: room={room} item={item} bytes below {chain[0]} entry modulo {DCACHE}")


def hot_entries(text):
    """(entries, index of the first window entry or None). `@window` in a
    comment marks the entry after it as the start of the render window."""
    window = None
    entries = []
    output = None
    for line in re.sub(r"/\*(.*?)\*/", lambda m: " @window " if "@window" in m[1] else "", text, flags=re.S).splitlines():
        line = line.strip()
        if line == "@window":
            window = len(entries)
            continue
        match = re.fullmatch(r"(\.\w+)\s*:\s*\{", line)
        if match:
            output = match[1]
            continue
        match = re.fullmatch(r"(\S+?)\((\S+)\)", line)
        if match:
            if output is None:
                raise ValueError(f"{line}: entry outside an output section block")
            entries.append((match[1], match[2], line, output))
    return entries, window


def output_sections(map_text, output):
    """Input sections of one output section: (name, address, size, path); also
    __text_start when present (.text)."""
    lines = map_text.splitlines()
    start = next((i for i, l in enumerate(lines) if re.match(r"^" + re.escape(output) + r"\s", l)), None)
    if start is None:
        raise ValueError(f"linker map has no {output} output section")
    text_start = None
    sections = []
    i = start + 1
    pending = None
    while i < len(lines):
        line = lines[i]
        i += 1
        if line and not line[0].isspace():
            break  # next output section
        match = re.match(r"^\s+0x([0-9a-f]+)\s+__text_start = \.$", line)
        if match:
            text_start = int(match[1], 16)
            continue
        match = re.match(r"^ (\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$", line)
        if match and match[1] != "*fill*" and not match[1].startswith("*"):
            sections.append((match[1], int(match[2], 16), int(match[3], 16), match[4]))
            pending = None
            continue
        match = re.match(r"^ (\.\S+)$", line)
        if match:
            pending = match[1]
            continue
        match = re.match(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$", line)
        if match and pending:
            sections.append((pending, int(match[1], 16), int(match[2], 16), match[3]))
            pending = None
    return text_start, sections


def matches(entry, section):
    pattern, names, _, _ = entry
    name, _, _, path = section
    if ":" in pattern:
        archive, member = pattern.split(":", 1)
        pattern = f"{archive}({member})"
    return fnmatch.fnmatchcase(path, pattern) and any(fnmatch.fnmatchcase(name, n) for n in names.split())


def check_block(map_text, entries, output):
    """Verify one output section's block; returns (first address, end, window
    index into the expected list, expected, unmatched, text_start)."""
    start, sections = output_sections(map_text, output)
    # Zero-size sections never matter in code; in data, the anchors are empty.
    wanted = [s for s in sections if s[2] or any(matches(e, s) for e in entries)]
    expected, unmatched = [], []
    marks = []
    for entry in entries:
        found = [s for s in wanted if matches(entry, s)]
        marks.append(len(expected))
        if not found:
            unmatched.append(entry[2])
        expected += [s for s in found if s not in expected]
    actual = wanted[:len(expected)]
    if actual != expected:
        for want, got in zip(expected, actual):
            if want != got:
                raise ValueError(f"{output} order differs at {want[0]} ({want[3].rsplit('/', 1)[-1]}): "
                                 f"found {got[0]} ({got[3].rsplit('/', 1)[-1]}) at {got[1]:08x}")
        raise ValueError(f"hot sections are not first in {output}")
    return start, expected, unmatched, marks


def check_pools(map_text, phase):
    # Same alignment as sys_memory_boot; surfaces.c places FIXED rows at
    # the top of stock RDRAM. The linked image end, not the image budget,
    # is the start constraint for carving.
    if phase is None:
        defaults = (ROOT / "plat/n64/hotdata.h").read_text()
        phase = int(re.search(r"^#define TR64_POOL_PHASE (\d+)u$", defaults, re.M)[1])
    if phase < 0 or phase % (DCACHE // 2):
        raise ValueError("TR64_POOL_PHASE must be a nonnegative multiple of 4096")
    ends = re.findall(r"^\s+0x([0-9a-f]+)\s+__bss_end = \.$", map_text, re.M)
    if len(ends) != 1:
        raise ValueError("linker map must define a single __bss_end")
    image_end = int(ends[0], 16)
    base = ((image_end-phase+DCACHE-1) & -DCACHE) + phase
    path = pool_table()
    table = pool_rows(path)
    reserve = reserve_bytes(table, path)
    fixed = sum(size for _, size, kind in table if kind == "FIXED")
    fixed_start = 0x80000000+RDRAM_BYTES-fixed
    cursor = base
    phases = []
    for name, size, kind in table:
        if kind != "CARVE":
            continue
        phases.append(f"{name}={cursor % DCACHE}")
        cursor += size
        if cursor > fixed_start:
            raise ValueError(f"{path.name}: pool {name} overlaps fixed rows")
    print(f"hot pools: table={path.name} carve={base:08x}-{cursor:08x}"
          f" fixed={fixed_start:08x} gap={fixed_start-cursor} reserve={reserve} cache phases " + " ".join(phases))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("map", type=pathlib.Path)
    ap.add_argument("hot", type=pathlib.Path)
    ap.add_argument("--budget", type=int, default=ICACHE)
    ap.add_argument("--check-pools", action="store_true")
    ap.add_argument("--pool-phase", type=int, help="TR64_POOL_PHASE build override")
    render = ap.add_mutually_exclusive_group()
    render.add_argument("--elf", type=pathlib.Path)
    render.add_argument("--no-render-stack", action="store_true", help="level-loader-free cart only")
    ap.add_argument("--objdump", type=pathlib.Path, default="mips64-elf-objdump")
    ap.add_argument("--render-pose", type=pathlib.Path, help="generated pose header (fixture stack profile)")
    ap.add_argument("--play", action="store_true",
                    help="shipping play build without route harness; start entry SP chain at tr64_render_game_scene_frame")
    ap.add_argument("--expect-render-stack", type=int, nargs=2,
                    help="override stack depth phases for the negative control")
    ns = ap.parse_args()
    entries, window = hot_entries(ns.hot.read_text())
    if not entries:
        raise ValueError(f"{ns.hot}: no entries")
    map_text = ns.map.read_text()
    if ns.check_pools:
        check_pools(map_text, ns.pool_phase)
    unmatched_all = []
    text_entries = [e for e in entries if e[3] == ".text"]
    if text_entries:
        text_start, expected, unmatched, marks = check_block(map_text, text_entries, ".text")
        unmatched_all += unmatched
        if text_start is None:
            raise ValueError("linker map has no __text_start")
        if expected[0][1] != text_start:
            raise ValueError(f"hot region starts at {expected[0][1]:08x}, not __text_start {text_start:08x}")
        window_at = marks[window] if window is not None else None
        end = expected[-1][1] + expected[-1][2]
        # The budget applies to the render window (from its marker to the end of the
        # list, else the whole list); what precedes it is deliberately placed to be
        # overlaid by the window's tail.
        first = expected[window_at or 0][1]
        extent = end - first
        print(f"hot: {len(expected)} sections {text_start:08x}-{end:08x} ordered={end - text_start}"
              + (f" prefix={first - text_start} window={extent}" if window_at else "")
              + f" icache={ns.budget} {'fits' if extent <= ns.budget else 'EXCEEDS by %d' % (extent - ns.budget)}")
    sbss_entries = [e for e in entries if e[3] == ".sbss"]
    bss_entries = [e for e in entries if e[3] == ".bss"]
    if sbss_entries or bss_entries:
        _, small, unmatched, _ = check_block(map_text, sbss_entries, ".sbss")
        unmatched_all += unmatched
        _, big, unmatched, _ = check_block(map_text, bss_entries, ".bss")
        unmatched_all += unmatched
        anchors = [s for s in small if s[0] == ".sbss.tr64_hot_data_start"]
        ends = [s for s in big if s[0] == ".bss.tr64_hot_data_end"]
        if len(anchors) != 1 or anchors[0] is not small[0] or len(ends) != 1 or ends[0] is not big[-1]:
            raise ValueError("hot data group lacks its start anchor first in .sbss or its end anchor last in the .bss list")
        base = anchors[0][1]
        end = ends[0][1]
        if base % DCACHE:
            raise ValueError(f"hot data group {base:08x} is not on a {DCACHE}-byte boundary")
        _, all_small = output_sections(map_text, ".sbss")
        small_end = max(s[1] + s[2] for s in all_small)
        gap = big[0][1] - small_end
        if not 0 <= gap < 16:
            raise ValueError(f".bss hot statics start {gap} bytes after the end of .sbss: the group is not contiguous")
        extent = end - base
        print(f"hot data: {base:08x}-{end:08x} bytes={extent} phase={small[0][2]} sbss={small_end - small[0][1] - small[0][2]}"
              f" bss={end - big[0][1]} dcache={DCACHE} {'fits' if extent <= DCACHE else 'EXCEEDS by %d' % (extent - DCACHE)}")
    if unmatched_all:
        print("hot: entries matching no linked section: " + ", ".join(unmatched_all), file=sys.stderr)
    if not ns.no_render_stack:
        expected = tuple(ns.expect_render_stack) if ns.expect_render_stack is not None else None
        if expected is None and pool_table().name == "pools-tr2.def":
            base = TR2_PLAY_RENDER_ENTRY_SP_DEPTHS if ns.play else TR2_RENDER_ENTRY_SP_DEPTHS
            expected = expected_render_entry_sp(ns.render_pose, base)
        check_render_entry_sp(ns.elf, ns.objdump, ns.render_pose, expected, play=ns.play)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"hot-check FAIL: {error}")
