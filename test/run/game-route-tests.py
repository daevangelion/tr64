# SPDX-License-Identifier: MIT
"""Asset-free dispatch controls, never Tomb Raider simulation/performance proof.

scene-work measures tick start to actual backend completion, less measured
diagnostic and framebuffer-acquisition spans; render-only timing stays separate.
Its audio_us is the immediate post-tick hooks, not total mixer CPU during waits.
An absent provider emits no work metric, never a fabricated zero.
"""
import importlib.util
import os
import pathlib
import re
import sys
sys.dont_write_bytecode = True
ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("scene", ROOT / "test/run/scene.py")
scene = importlib.util.module_from_spec(spec)
spec.loader.exec_module(scene)
BUILD = pathlib.Path(os.environ["BUILD_DIR"]) / "game-route-tests"
BUILD.mkdir(parents=True, exist_ok=True)
base = "scene game-route-probe\nstart boot\nload level level1\nticks 2\nseed 1\nobserve pools\nat 0 pad 65535 0 0\nat 1 pad 8 0 0\n"
placement = "place 7 -2147483648 2147483647 -7 -32768\n"
frames = [("0", "1", "1"), ("1", "71", "2"), ("2", "141", "3")]


def compile_route(name, text):
    source = BUILD / (name + ".route")
    source.write_text(text)
    binary = BUILD / (name + ".rbc")
    binary.write_bytes(scene.routec.parse(source)[1])
    return source, binary


def build(name, source, probe, disabled=(), audio=False, perf=False, ui=None, boundary=None, schema3=False):
    directory = BUILD / name
    # Isolate optional modules; preserve the actual Makefile/32-bit link path.
    options = ["TR64_LEVEL_LOADER=0", "TR64_ENGINE_SOURCES=", "TR64_HOST_SOURCES=",
               "TR64_CART_SOURCES=", "TR64_UNIT_SOURCES=", "TR64_GENERATED_HEADERS=",
               "TR64_CODE_UNIT_FRAGMENTS=", "TR64_ROM_FILES=", "TR64_ROM_DEPS=",
               "TR64_SCENE_TEST_SOURCES=" + ("engine/test/game-route-probe.c" if probe else "") +
               (" engine/test/audio-hooks-probe.c" if audio else "") +
               (" test/snd/on.c" if perf else "")]
    definitions = ["-DTR64_GAME_ROUTE_NO_" + hook for hook in disabled]
    if ui is not None:
        definitions.append("-DTR64_GAME_ROUTE_UI=" + str(ui))
    if boundary is not None:
        definitions.append("-DTR64_GAME_ROUTE_BOUNDARY=" + str(boundary))
    if schema3:
        definitions.append("-DTR64_GAME_ROUTE_SCHEMA3=1")
    if definitions:
        options.append("HOST_CC=" + os.environ.get("HOST_CC", "cc") + " " + " ".join(definitions))
    scene.command(["make", "--no-print-directory", "-j1", f"BUILD_DIR={directory}",
                   f"SCENE={source}", *options, "host"], BUILD / (name + "-build.log"), timeout=120)
    return directory / "host/tr64-host"


def run(executable, name, binary, reason=None):
    log = BUILD / (name + ".log")
    scene.command([executable, "--route", binary], log, timeout=10, expected=1 if reason else 0)
    text = log.read_text()
    if reason and reason not in text:
        raise AssertionError("wrong game rejection: " + text)
    return text


def check_work(text, paced=False):
    work = re.findall(r"^scene-work: tick=(\d+) total_us=(\d+) wall_us=(\d+) diagnostics_us=(\d+) pacing_us=(\d+) game_us=(\d+) audio_us=(\d+)$", text, re.M)
    ticks = {int(t): int(us) for t, us in re.findall(r"^game-tick: tick=(\d+) hash=[0-9a-f]{8} tick_us=(\d+)(?: hash_schema=3)?$", text, re.M)}
    if len(work) != 2:
        raise AssertionError("missing tick1/2 direct completion intervals")
    for expected, record in enumerate(work, 1):
        tick, total, wall, diagnostics, pacing, game, audio = map(int, record)
        if tick != expected or total + diagnostics + pacing != wall or game != ticks[tick]:
            raise AssertionError("completion interval/exclusion identity differs")
        if (paced and pacing < 2000) or (not paced and pacing):
            raise AssertionError("actual synthetic framebuffer wait was not excluded")


source, binary = compile_route("placed", base + "game\n" + placement)
executable = build("linked", source, True)
text = run(executable, "placed", binary)
records = re.findall(r"^game-tick: tick=(\d+) hash=([0-9a-f]{8}) tick_us=(\d+)$", text, re.M)
if [(tick, digest) for tick, digest, _ in records] != [("0", "70000100"), ("1", "70000101"), ("2", "70000102")]:
    raise AssertionError("placement/raw-mask tick hashes differ")
check_work(text)
performance=build("performance",source,True,perf=True)
perf_text=run(performance,"performance",binary)
if re.findall(r"^game-tick: tick=(\d+) hash=omitted/perf-only tick_us=\d+$",perf_text,re.M)!=["0","1","2"]:
    raise AssertionError("performance probe failed to omit tick0/later diagnostic hashes")
if re.search(r"^game-tick: .*hash=[0-9a-f]{8} ",perf_text,re.M):
    raise AssertionError("performance probe masqueraded as gameplay hash evidence")
if re.findall(r"^game route probe render tick=(\d+) drawables=(\d+) pulls=(\d+)$",perf_text,re.M)!=frames or len(re.findall(r"^scene-work: ",perf_text,re.M))!=2:
    raise AssertionError("performance-only hash omission changed rendering/work stamps")
if len(re.findall(r"^scene-work: .* room=5$",perf_text,re.M))!=2:
    raise AssertionError("performance work records lost the actual existing camera room")
capacity = re.findall(r"^game route probe capacity=64 bytes=(\d+)$", text, re.M)
if len(capacity) != 1 or records[0][2] != "0":
    raise AssertionError("missing one fixed chunk/tick0 accounting")
used = 48 + int(capacity[0])
if not re.search(rf"pool: state .* used={used} peak={used} ", text):
    raise AssertionError("renderer/game workspace + one fixed chunk accounting differs")
if re.findall(r"^game route probe render tick=(\d+) drawables=(\d+) pulls=(\d+)$", text, re.M) != frames:
    raise AssertionError("more than64 drawables did not stream through the one chunk")
if len(re.findall(r"^game route probe prepare$", text, re.M)) != 1:
    raise AssertionError("renderer preparation was not once before tick0")
for ui in (0, 1):
    ui_binary = build("ui-" + str(ui), source, True, ui=ui)
    ui_text = run(ui_binary, "ui-" + str(ui), binary)
    if re.findall(r"^game route probe UI bound=(\d+) tick=(\d+)$", ui_text, re.M) != [(str(ui), str(t)) for t in range(3)]:
        raise AssertionError("UI hook skipped/duplicated a frame or lost explicit binding")
    if re.findall(r"^game route probe render tick=(\d+) drawables=(\d+) pulls=(\d+)$", ui_text, re.M) != frames:
        raise AssertionError("UI bound/unbound path double-presented or changed camera/chunk")
    check_work(ui_text)
schema_source, schema_binary = compile_route("schema3", base + "game\n" + placement + "hash-schema 3\n")
schema_engine = build("schema3", schema_source, True, schema3=True)
schema_text = run(schema_engine, "schema3", schema_binary)
if re.findall(r"^game-tick: tick=(\d+) hash=([0-9a-f]{8}) tick_us=\d+ hash_schema=3$", schema_text, re.M) != [("0","73000100"),("1","73000101"),("2","73000102")]:
    raise AssertionError("schema3 did not query the selected producer at every tick")
check_work(schema_text)
run(executable, "missing-schema3", schema_binary, "scene game hash schema3 query not linked")
for boundary in (1, 2, 3, 4, 5):
    boundary_engine = build("boundary-" + str(boundary), source, True, boundary=boundary, audio=boundary==5)
    reason = "scene boundary reload requires released and replaced Core" if boundary in (2,3) else "scene boundary released Core without replacement" if boundary==4 else None
    boundary_text = run(boundary_engine, "boundary-" + str(boundary), binary, reason)
    if reason:
        continue
    if re.findall(r"^game route probe boundary tick=(\d+) input=(\d+)$", boundary_text, re.M) != [("0","0"),("1","65535"),("2","8")]:
        raise AssertionError("boundary lost route tick/raw-input metadata")
    if re.findall(r"^game route probe render tick=(\d+) drawables=(\d+) pulls=(\d+)$", boundary_text, re.M) != frames:
        raise AssertionError("boundary retried/rendered an extra tick0 or lost the new chunk")
    if boundary==1:
        if boundary_text.count("game route probe prepare\n")!=2 or boundary_text.count("game route probe tick=1\n")!=2:
            raise AssertionError("real lifecycle replacement failed one same-input retry/rebind")
    else:
        order = ["audio hook level-start\n","game route probe boundary tick=0 input=0\n","game-tick: tick=0 "]
        for tick in (1,2):
            order += [f"game route probe tick={tick}\n",f"game route probe boundary tick={tick} ",f"audio hook tick={tick}\n",f"game-tick: tick={tick} "]
        positions = [boundary_text.index(token) for token in order]
        if positions != sorted(positions):
            raise AssertionError("boundary moved before game/sound-start or after stop/hash/sound")
    check_work(boundary_text)
_, binary = compile_route("unplaced", base + "game\n")
text = run(executable, "unplaced", binary)
if re.findall(r"^game-tick: tick=(\d+) hash=([0-9a-f]{8})", text, re.M) != [("0", "70000000"), ("1", "70000001"), ("2", "70000002")]:
    raise AssertionError("optional placement changed default start")
_, binary = compile_route("empty", (base + "game\n").replace("load level level1", "load level empty-view"))
text = run(executable, "empty", binary)
if re.findall(r"^game route probe render tick=(\d+) drawables=(\d+) pulls=(\d+)$", text, re.M) != [("0", "0", "1"), ("1", "0", "1"), ("2", "0", "1")] or not re.search(rf"pool: state .* used={used} peak={used} ", text):
    raise AssertionError("empty room changed fixed allocation or failed to end cursor")
_, binary = compile_route("pacing", (base + "game\n").replace("load level level1", "load level pacing-view"))
check_work(run(executable, "pacing", binary), paced=True)
for name, identity, reason in [
        ("oversized-item", "bad-chunk", "game route probe whole item exceeds chunk"),
        ("stuck-cursor", "bad-cursor", "game route probe cursor did not advance"),
        ("named-stop", "stop-view", "game route probe named stop"),
        ("lost-stop", "null-stop", "game stop query lost active level"),
        ("invalid-work-span", "bad-stamp", "scene-work invalid completion span")]:
    _, binary = compile_route(name, (base + "game\n").replace("load level level1", "load level " + identity))
    text = run(executable, name, binary, reason)
    if identity in ("stop-view", "null-stop") and (re.search(r"^game-tick: tick=1 ", text, re.M) or re.search(r"^game route probe render tick=1 ", text, re.M)):
        raise AssertionError("stopped game was hashed/rendered after its stopping tick")
_, binary = compile_route("ordinary", base)
text = run(executable, "ordinary", binary)
if "game-tick:" in text or "game route probe render tick=" in text or "game route probe prepare" in text or not re.search(r"pool: state .* used=17 peak=17 ", text):
    raise AssertionError("ordinary level invoked gameplay")
_, binary = compile_route("invalid-place", base + "game\n" + placement.replace("place 7", "place 8"))
run(executable, "invalid-place", binary, "game placement: room/grid/floor mismatch")
_, binary = compile_route("placed", base + "game\n" + placement)
missing = build("missing", source, False)
run(missing, "missing-hooks", binary, "scene game hooks not linked")
for hook in ("RENDER", "CAMERA", "ROOM", "STOP"):
    missing = build("missing-" + hook.lower(), source, True, (hook,))
    run(missing, "missing-" + hook.lower(), binary,
        "scene game hooks not linked" if hook == "STOP" else "scene game view hooks not linked")
optional_prepare = build("optional-prepare", source, True, ("PREPARE",))
text = run(optional_prepare, "optional-prepare", binary)
if re.findall(r"^game route probe render tick=(\d+) drawables=(\d+) pulls=(\d+)$", text, re.M) != frames or "game route probe prepare" in text:
    raise AssertionError("missing optional prepare hook changed game dispatch")
optional_work = build("optional-work", source, True, ("WORK_STAMP",))
text = run(optional_work, "optional-work", binary)
if "scene-work:" in text or re.findall(r"^game route probe render tick=(\d+) drawables=(\d+) pulls=(\d+)$", text, re.M) != frames:
    raise AssertionError("missing work provider fabricated performance or changed dispatch")
audio = build("audio-hooks", source, True, audio=True)
text = run(audio, "audio-placed", binary)
order = ["audio hook boot\n", "game route probe start\n", "game route probe placed\n",
         "audio hook level-start\n", "game route probe prepare\n", "game-tick: tick=0 "]
for tick in (1, 2):
    order += [f"game route probe tick={tick}\n", f"audio hook tick={tick}\n",
              f"audio hook pump-tick={tick}\n", f"game-tick: tick={tick} ",
              f"game route probe render tick={tick} "]
positions = [text.index(token) for token in order]
if positions != sorted(positions) or "audio hook report boot=1 starts=1 ticks=2 pumps=2 stops=0 active=1" not in text:
    raise AssertionError("audio boot/start/tick/pump/report ordering differs")
_, binary = compile_route("audio-terminal", (base + "game\n").replace("load level level1", "load level stop-view"))
text = run(audio, "audio-terminal", binary, "game route probe named stop")
if not (text.index("game route probe tick=1\n") < text.index("audio hook tick=1\n") <
        text.index("audio hook pump-tick=1\n") < text.index("tr64 fatal: game route probe named stop")) or "game-tick: tick=1 " in text:
    raise AssertionError("terminal game tick lost its sound consumption or hashed after stopping")
for identity in ("audio-bump", "audio-reset"):
    _, binary = compile_route(identity, (base + "game\n").replace("load level level1", "load level " + identity))
    run(audio, identity, binary, "pool audio owned by library allocator")
_, binary = compile_route("audio-ordinary", base)
text = run(audio, "audio-ordinary", binary)
if "audio hook level-start" in text or "audio hook tick=" in text or "audio hook report boot=1 starts=0 ticks=0 pumps=2 stops=0 active=0" not in text:
    raise AssertionError("non-game frames failed to pump or consumed game events")
print("audio hooks PASS (boot before level; start after game/place; exact per-tick events including terminal; pump before stop/hash/frame; claimed audio pool rejects late bump/reset; non-game silence pumping)")
print("scene-work PASS (actual completion stamp; direct wall minus measured diagnostics/pacing; 2ms wait exclusion; invalid-span negative; missing provider emits no metric)")
print("game route PASS (one fixed64 drawable chunk, camera-only view, 1/71/141 streamed drawables; stop-before-hash/frame; whole-item/cursor/missing-hook controls; ordinary levels unchanged)")
