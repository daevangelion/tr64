# Hardware probe ROM

Plain libdragon from the pinned SDK (kseg0 text, no `N64_TLBCODE`): it does
not depend on any mechanism it tests. It installs its own exception vectors,
so a faulting step is recorded (vector, Cause, EPC, BadVAddr, Context,
XContext, EntryHi) and the next step runs.

```
mkdir -p $OUT && make -C $OUT -f <repo>/tools/hwprobe/Makefile OUT=$OUT   # $OUT/hwprobe.z64
```

## Default build: the deciding tests (text only)

| Line | Test | VR4300 / ares |
|---|---|---|
| `FETCH C0200000 KX1` / `KX0` / `50000000 KX1` | a 4 KiB pair at that VA maps a frame holding `jr ra; li v0,1`; physical `VA & 0x1FFFFFFF` (0x00200000) holds `jr ra; li v0,2`; call it with `jalr`. `V` is the value returned, `T` the Count ticks of 1000 calls | `V1` (mapped fetch). `V2` = the fetch ignored the TLB and used the address as physical |
| `ICACHE HIT IDX ALL` | one frame, a stub (`v0=1`) run so its line is cached, the frame rewritten in RDRAM with `v0=2`, then `Hit_Invalidate_I`, `Index_Invalidate_I` (range) or the whole cache, and run again. The frame's physical colour (PA bits 13:12 = 1) differs from the virtual one (0) | `2 2 2`; a `1` is a stale line |
| `K0 / K2` | 1,000,000-iteration loop in kseg0 and through the identity-mapped kseg2 alias | about equal; a core that fetches kseg2 uncached is many times slower |
| `INFO TLBR PFN` | writes `0x3FFFFFC0` to an invalid entry and reads it back | informational: ares itself reads `03FFFFC0` |
| `PRID CFG SR RDRAM0 RDRAMC` | platform fingerprint | not discriminating by themselves |

The headline is `PASS AS VR4300` or `NOT A VR4300 FETCH PATH`; the harness exit code
is 0 / 1. The log (`engine.log` under ares) carries every value and every fault record.

## `HWPROBE_DEFS=-DHWPROBE_FULL`: the full battery

The tests above plus: TLB read-back of a 1 MiB and a 4 KiB entry, TLBP, data reads
through the mapping, instruction fetch through it (JR and LW stubs; EntryHi by
`mtc0` and by `dmtc0`; KX/SX/UX set and clear; EntryLo `0x1B` and `0x1F`; with and
without `Hit_Invalidate_I`; frame filled by PI DMA and by CPU copy), execution
through the 1 MiB mapping, seven deliberate TLB exceptions (vectors 0x000, 0x080, 0x180;
EXL clear and set), and tlbcode's own register sequence: the entrypoint TLB block,
`tlbcode_load`, `sys_code_probe`'s `snapshot`, then the unit call. Result letters:
`P` pass, `F` wrong value, `X` fault, `D` frame differs from the image after the fill.
The symbol (`TR64.P1.`, 97 bytes) is read with `decode.py [--image shot.png]`.

`page.py` writes the 8 KiB page pair the DMA fills come from (`.hwprobe` file in the ROM).
