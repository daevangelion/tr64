# SPDX-License-Identifier: MIT
"""Synthetic route compiler controls; no retail content."""
import importlib.util
import os
import pathlib
import struct
import tempfile
import zlib
import sys
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("routec", pathlib.Path(__file__).with_name("routec.py"))
routec = importlib.util.module_from_spec(spec)
spec.loader.exec_module(routec)
good = "scene synthetic\nstart boot\nticks 8\nseed 1\nobserve state\nat 0 pad 1 16 -16\nexpect-state 00000000\nexpect-shot " + "0" * 64 + "\n"
cases = [good.replace("ticks 8", "ticks 0"), good.replace("ticks 8", "ticks 601"),
         good.replace("pad 1", "pad 65536"), good.replace("16 -16", "81 -16"),
         good.replace("at 0", "at 8"), good + "at 0 pad 0 0 0\n",
         good + "seed 2\n", good.replace("start boot", "start unknown"),
         good.replace("expect-state 00000000", "expect-state xyz"), good + "unknown x\n",
         good.replace("observe state\n", ""), good + "observe state\n",
         good + "load level UPPER\n", good + "load level " + "a" * 32 + "\n",
         good + "load level level1\nload level level2\n"]
game = good.replace("pad 1 16 -16", "pad 65535 0 0") + "load level level1\ngame\n"
placed = game + "place 4294967295 -2147483648 2147483647 -7 -32768\n"
cases += [good + "game\n", game + "game\n", game + "game extra\n",
          good + "place 0 1 2 3 4\n", placed + "place 0 0 0 0 0\n",
          game + "place -1 0 0 0 0\n", game + "place 4294967296 0 0 0 0\n",
          game + "place 0 -2147483649 0 0 0\n", game + "place 0 0 2147483648 0 0\n",
          game + "place 0 0 0 0 32768\n", game + "place 0 0 0 0 -32769\n",
          game + "place 0 0 0 0\n", game.replace("65535 0 0", "65535 1 0")]
cases += [good + "probe reload\n", game + "probe reload\n", good + "probe video\n", game + "probe video\n"]
cases += [good + "hash-schema 3\n", game + "hash-schema 1\n",
          game + "hash-schema 4\n", game + "hash-schema 3\nhash-schema 3\n",
          game + "hash-schema 3 extra\n"]
tr2 = game.replace("65535 0 0", "65536 0 0").replace("level1", "wall").replace("ticks 8", "ticks 659") + "hash-schema 20\n"
cases += [good + "hash-schema 20\n", game + "hash-schema 20\n",
          tr2.replace("ticks 659", "ticks 901"), tr2.replace("65536 0 0", "65537 0 0")]
cases += [game + "at 1 ring 158 90\n", tr2 + "at 1 key 198 50\n",
          tr2 + "at 1 ring 265 1\n", tr2 + "at 1 ring 158 2097152\n",
          tr2 + "at 1 ring 0 1\nat 1 ring 158 2\n", tr2 + "at 659 ring 0 1\n"]
with tempfile.TemporaryDirectory(prefix="routec-", dir=os.environ["TMPDIR"]) as temp:
    path = pathlib.Path(temp) / "synthetic.route"
    path.write_text(good)
    meta, data = routec.parse(path)
    assert meta["ticks"] == "8" and meta["level"] is None and not meta["game"] and meta["place"] is None
    assert data[:4] == b"TR6R" and len(data) == 108
    assert struct.unpack_from(">I", data, 4)[0] == 3
    assert struct.unpack_from(">I", data, 24)[0] == zlib.crc32(data[32:])
    path.write_text(good + "probe clock-wrap\n")
    assert struct.unpack_from(">I", routec.parse(path)[1], 28)[0] == 5
    path.write_text(good + "load level level1\n")
    loaded, loaded_data = routec.parse(path)
    assert loaded["level"] == "level1" and loaded_data[32:64] == b"level1".ljust(32, b"\0")
    assert struct.unpack_from(">I", loaded_data, 24)[0] == zlib.crc32(loaded_data[32:])
    path.write_text(good + "load level level1\nprobe reload\n")
    assert struct.unpack_from(">I", routec.parse(path)[1], 28)[0] == 6
    path.write_text(good + "load level core\nprobe video\n")
    assert struct.unpack_from(">I", routec.parse(path)[1], 28)[0] == 7
    path.write_text(placed)
    gameplay, game_data = routec.parse(path)
    assert gameplay["game"] and gameplay["place"] == (4294967295, -2147483648, 2147483647, -7, -32768)
    assert struct.unpack_from(">IIIiiiiI", game_data, 64) == (1, 1, 4294967295, -2147483648, 2147483647, -7, -32768, 0)
    assert struct.unpack_from(">I", game_data, 24)[0] == zlib.crc32(game_data[32:])
    path.write_text(placed + "hash-schema 2\n")
    explicit_two, same_data = routec.parse(path)
    assert explicit_two["hash_schema"] == 2 and same_data == game_data
    path.write_text(placed + "hash-schema 3\n")
    three, three_data = routec.parse(path)
    assert three["hash_schema"] == 3 and struct.unpack_from(">I", three_data, 92)[0] == 3
    assert struct.unpack_from(">I", three_data, 24)[0] == zlib.crc32(three_data[32:])
    path.write_text(tr2)
    sequel, sequel_data = routec.parse(path)
    assert sequel["hash_schema"] == 20 and sequel["ticks"] == "659"
    assert struct.unpack_from(">I", sequel_data, 4)[0] == 4
    assert struct.unpack_from(">IIbbH", sequel_data, 96) == (0, 65536, 0, 0, 0)
    assert struct.unpack_from(">I", sequel_data, 92)[0] == 20
    assert struct.unpack_from(">I", sequel_data, 24)[0] == zlib.crc32(sequel_data[32:])
    path.write_text(tr2 + "at 1 pad 128 0 0\nat 1 key 198 50\nat 2 ring 0 15\n")
    _, ring_data = routec.parse(path)
    assert struct.unpack_from(">III", ring_data, 108) == (1, 128, 50*2048+1024+512+198)
    assert struct.unpack_from(">III", ring_data, 120) == (2, 128, 15*2048+512)
    for bad in cases:
        path.write_text(bad)
        try:
            routec.parse(path)
        except ValueError:
            continue
        raise AssertionError("malformed route escaped")
print(f"routec PASS (valid synthetic route + {len(cases)} rejected inputs)")
