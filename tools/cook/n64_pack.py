# SPDX-License-Identifier: MIT
"""Content-addressed native pack; ABI: docs/formats/n64-pack.md."""
import ctypes
import ctypes.util
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import zlib
from dataclasses import dataclass

BLOCK = 16384
HEADER = struct.Struct('>4s7I')
LEVEL = struct.Struct('>32s4I')
ENTRY = struct.Struct('>8I32s')
SDK_SOURCE = Path(os.environ.get('TR64_LIBDRAGON_SRC', str(Path.home() / 'src/libdragon-tr64/tools/common')))
LZ4_FILES = ('lz4_compress.c', 'lz4/lz4.c', 'lz4/lz4.h', 'lz4/lz4hc.c', 'lz4/lz4hc.h')


def _cmd(*args):
    return (['rtk'] if shutil.which('rtk') else []) + list(args)


def _find_sdk_lz4_sources():
    candidates = []
    env_src = os.environ.get('TR64_LIBDRAGON_SRC')
    if env_src:
        p = Path(env_src)
        candidates.extend([p / 'tools/common', p])
    candidates.extend([
        SDK_SOURCE,
        Path.home() / 'src/libdragon-hl64/tools/common',
        Path.home() / 'src/libdragon/tools/common',
    ])
    n64_inst = os.environ.get('N64_INST')
    if n64_inst:
        p = Path(n64_inst)
        candidates.extend([p / 'tools/common', p.parent / 'libdragon/tools/common',
                           p.parent / 'libdragon-tr64/tools/common'])
    for base in candidates:
        sources = [base / n for n in LZ4_FILES]
        if all(s.is_file() for s in sources):
            return sources
    return None


def align(n, alignment=16):
    return (n + alignment - 1) & -alignment


@dataclass(frozen=True)
class Section:
    kind: int
    data: bytes
    alignment: int = 16
    header_bytes: int = 0
    chunk_bytes: int = BLOCK
    content_defined: bool = False
    compression: str = 'auto'
    boundaries: tuple = ()

def level_layout(sections, budget=None):
    """Directory-equivalent fixed-order resident layout; no per-kind caps."""
    if budget is None:
        from sizing import pools
        budget=pools()['level']
    sizes=dict(core=0,tex=0,rooms=0,meshes=0)
    for section in sorted(sections,key=lambda s:s.kind):
        kind=section.kind; raw=len(section.data)
        if 0<kind<0x10000:sizes['core']+=align(raw)
        elif kind in (0x10000,0x20000,0x30000,0x50000):
            name='tex' if kind==0x50000 else ('tex','rooms','meshes')[(kind>>16)-1]
            sizes[name]=align(sizes[name])+raw
    total=sum(align(n) for n in sizes.values())
    if total>budget:raise ValueError(f'level resident total {total} exceeds arena {budget}')
    return dict(sections=sizes,bytes=total,budget_bytes=budget,margin_bytes=budget-total)


def compressor(cache):
    """Build a version-keyed cached host LZ4 compressor, or fall back to system liblz4."""
    sources = _find_sdk_lz4_sources()
    if sources is not None:
        cc = os.environ.get('HOST_CC', 'cc')
        compiler = subprocess.check_output(_cmd(cc, '--version'))
        target = subprocess.check_output(_cmd(cc, '-dumpmachine'))
        digest = hashlib.sha256(b''.join(p.read_bytes() for p in sources)
                                +compiler+target+b'-shared -fPIC -O2').hexdigest()
        cache = Path(cache)
        cache.mkdir(parents=True, exist_ok=True)
        library = cache / f'lz4-{digest}.so'
        if not library.exists():
            temporary = library.with_suffix(f'.{os.getpid()}.tmp')
            subprocess.run(_cmd(cc, '-shared', '-fPIC', '-O2',
                                str(sources[0]), '-o', str(temporary)), check=True)
            temporary.replace(library)
        lib = ctypes.CDLL(str(library))
        compiler_id = (compiler + target).decode()
    else:
        lib_name = ctypes.util.find_library('lz4')
        lib = None
        for candidate in ([lib_name] if lib_name else []) + ['liblz4.so.1', 'liblz4.so', 'liblz4.dylib']:
            try:
                lib = ctypes.CDLL(candidate)
                break
            except OSError:
                continue
        if lib is None:
            raise FileNotFoundError(
                'LZ4 compressor unavailable: set TR64_LIBDRAGON_SRC to a libdragon checkout '
                'or install system liblz4 (e.g. liblz4-1)'
            )
        ver = 0
        if hasattr(lib, 'LZ4_versionNumber'):
            lib.LZ4_versionNumber.restype = ctypes.c_int
            ver = lib.LZ4_versionNumber()
        digest = hashlib.sha256(f'system-liblz4:{ver}'.encode()).hexdigest()
        compiler_id = f'system-liblz4-{ver}'
    lib.LZ4_compressBound.argtypes = [ctypes.c_int]
    lib.LZ4_compressBound.restype = ctypes.c_int
    lib.LZ4_compress_HC.argtypes = [ctypes.c_void_p, ctypes.c_void_p,
                                  ctypes.c_int, ctypes.c_int, ctypes.c_int]
    lib.LZ4_compress_HC.restype = ctypes.c_int
    compressed_cache = {}
    def compress(raw):
        digest = hashlib.sha256(raw).digest()
        if digest in compressed_cache:
            return compressed_cache[digest]
        out = ctypes.create_string_buffer(lib.LZ4_compressBound(len(raw)))
        n = lib.LZ4_compress_HC(raw, out, len(raw), len(out), 12)
        if n <= 0:
            raise ValueError('SDK LZ4 compressor failed')
        compressed_cache[digest] = out.raw[:n]
        return compressed_cache[digest]
    compress.identity = digest
    compress.compiler = compiler_id
    return compress


def decompress(raw, size):
    """Independent bounded LZ4 block decode, not the source reader."""
    out = bytearray()
    pos = 0
    def length(n):
        nonlocal pos
        if n == 15:
            while True:
                if pos >= len(raw):
                    raise ValueError('LZ4 truncated length')
                b = raw[pos]
                pos += 1
                n += b
                if b != 255:
                    break
        return n
    while pos < len(raw):
        token = raw[pos]
        pos += 1
        n = length(token >> 4)
        if n > len(raw) - pos or n > size - len(out):
            raise ValueError('LZ4 literal bounds')
        out.extend(raw[pos:pos+n])
        pos += n
        if pos == len(raw):
            break
        if len(raw) - pos < 2:
            raise ValueError('LZ4 truncated distance')
        distance = raw[pos] | raw[pos+1] << 8
        pos += 2
        n = length(token & 15) + 4
        if not distance or distance > len(out) or n > size - len(out):
            raise ValueError('LZ4 match bounds')
        for _ in range(n):
            out.append(out[-distance])
    if len(out) != size:
        raise ValueError('LZ4 decoded size')
    return bytes(out)


# Deterministic gear values; content, not source offsets, chooses frame cuts.
GEAR = tuple(int.from_bytes(hashlib.sha256(bytes([i])).digest()[:4], 'big')
             for i in range(256))


def chunks(raw, content_defined=False, block=BLOCK):
    start = 0
    rolling = 0
    for pos in range(0, len(raw), 2):
        rolling = ((rolling << 1) + GEAR[raw[pos]]) & 0xffffffff
        if pos + 1 < len(raw):
            rolling = ((rolling << 1) + GEAR[raw[pos+1]]) & 0xffffffff
        end = min(pos + 2, len(raw))
        length = end - start
        if length >= block or (content_defined and length >= 2048 and not rolling & 2047):
            yield start, raw[start:end]
            start = end
            rolling = 0
    if start < len(raw):
        yield start, raw[start:]


def parts(section):
    if (not 2 <= section.chunk_bytes <= BLOCK or section.chunk_bytes % 2
            or not 0 <= section.header_bytes <= min(BLOCK,len(section.data))):
        raise ValueError('pack section partition bounds')
    if section.boundaries:
        start=0
        if section.header_bytes and (section.header_bytes==len(section.data) or section.boundaries[0]!=section.header_bytes):
            raise ValueError('pack explicit boundaries split header')
        for end in (*section.boundaries,len(section.data)):
            limit=section.header_bytes if start==0 and section.header_bytes else section.chunk_bytes
            if not isinstance(end,int) or end<=start or end>len(section.data) or end-start>limit:
                raise ValueError('pack explicit boundary bounds')
            yield start,section.data[start:end]
            start=end
        return
    if section.header_bytes:
        yield 0,section.data[:section.header_bytes]
    for offset,raw in chunks(section.data[section.header_bytes:],
                             section.content_defined,section.chunk_bytes):
        yield section.header_bytes+offset,raw


def build(levels, compress=None, deduplicate=True, *, games=None):
    if not levels:
        raise ValueError('pack requires a level')
    games = {} if games is None else games
    if games.keys() - levels.keys() or any(game not in (2, 3) for game in games.values()):
        raise ValueError('pack game version')
    directories = []
    contents = {}
    unique = []
    names = set()
    for name, sections in sorted(levels.items()):
        encoded = name.encode('utf-8')
        if not encoded or len(encoded) >= 32 or b'\0' in encoded or name in names:
            raise ValueError('pack level name')
        names.add(name)
        records = []
        kinds = set()
        for section in sorted(sections, key=lambda s: s.kind):
            if not 0 < section.kind <= 0xffffffff or section.kind in kinds or section.alignment != 16:
                raise ValueError('pack section kind/alignment')
            if section.compression not in ('auto','none'):
                raise ValueError('pack section compression')
            kinds.add(section.kind)
            for destination, raw in parts(section):
                digest = hashlib.sha256(raw).digest()
                key = digest if deduplicate else (name, section.kind, destination)
                if key not in contents:
                    stored = compress(raw) if compress and section.compression=='auto' else raw
                    codec = 1 if align(len(stored)) < align(len(raw)) else 0
                    if not codec:
                        stored = raw
                    contents[key] = [raw, stored, codec, 0]
                    unique.append(key)
                elif contents[key][0] != raw:
                    raise ValueError('SHA256 content collision')
                elif section.compression=='none':
                    # One physical encoding per raw digest. A stream reference
                    # requiring direct reads wins over automatic compression.
                    contents[key][1]=raw; contents[key][2]=0
                records.append((section.kind, destination, key, digest))
        directories.append((encoded, records, games.get(name, 0)))
    cursor = align(HEADER.size + LEVEL.size * len(directories))
    table_offsets = []
    for _, records, _ in directories:
        table_offsets.append(cursor)
        cursor = align(cursor + ENTRY.size * len(records))
    for key in unique:
        item = contents[key]
        item[3] = cursor
        cursor = align(cursor + len(item[1]))
    if cursor > 64 * 1024 * 1024:
        raise ValueError('pack exceeds 64 MiB')
    result = bytearray(cursor)
    directory = bytearray()
    for (name, records, game), offset in zip(directories, table_offsets):
        table = bytearray()
        for kind, destination, key, digest in records:
            raw, stored, codec, physical = contents[key]
            table += ENTRY.pack(kind, destination, physical, len(stored), len(raw),
                                16, codec, zlib.crc32(raw), digest)
        result[offset:offset+len(table)] = table
        directory += LEVEL.pack(name, offset, len(records), zlib.crc32(table), game)
    result[:HEADER.size] = HEADER.pack(b'TR6P', 2, cursor, len(directories),
                                       32, 48, zlib.crc32(directory), 0)
    result[32:32+len(directory)] = directory
    for key in unique:
        _, stored, _, offset = contents[key]
        result[offset:offset+len(stored)] = stored
    return bytes(result)


def inspect(blob):
    if len(blob) < HEADER.size:
        raise ValueError('pack truncated header')
    magic, version, total, count, offset, stride, crc, reserved = HEADER.unpack_from(blob)
    if magic != b'TR6P' or version != 2 or total != len(blob) or reserved or offset != 32 or stride != 48:
        raise ValueError('pack header')
    if count > (len(blob)-32)//48:
        raise ValueError('pack directory bounds')
    directory = blob[32:32+count*48]
    if zlib.crc32(directory) != crc:
        raise ValueError('pack directory CRC')
    levels = {}
    for i in range(count):
        name, offset, entries, crc, game = LEVEL.unpack_from(directory, i*48)
        if b'\0' not in name:
            raise ValueError('pack level record')
        name = name.split(b'\0')[0].decode('utf-8')
        if game not in (0, 2, 3):
            raise ValueError(f'pack {name}: unsupported game version {game}')
        if not name or name in levels or offset % 16 or entries > len(blob)//64 or offset > len(blob)-entries*64:
            raise ValueError('pack section table bounds')
        table = blob[offset:offset+entries*64]
        if zlib.crc32(table) != crc:
            raise ValueError('pack section table CRC')
        sections = {}
        last_kind = 0
        for j in range(entries):
            kind, dest, off, stored, raw, alignment, codec, checksum, digest = ENTRY.unpack_from(table, j*64)
            if (not kind or kind < last_kind or alignment != 16 or codec not in (0,1)
                    or not 0 < raw <= BLOCK or not 0 < stored <= BLOCK or off % 16
                    or off > len(blob)-align(stored,2) or (not codec and stored != raw)):
                raise ValueError(f'pack section {kind}: descriptor bounds')
            last_kind = kind
            assembled = sections.setdefault(kind, bytearray())
            if dest != len(assembled):
                raise ValueError(f'pack section {kind}: destination offset')
            payload = blob[off:off+stored]
            decoded = decompress(payload, raw) if codec else payload
            if zlib.crc32(decoded)!=checksum or hashlib.sha256(decoded).digest() != digest:
                raise ValueError(f'pack section {kind}: corrupt hash')
            assembled.extend(decoded)
        levels[name] = {kind: bytes(data) for kind, data in sections.items()}
    return levels
