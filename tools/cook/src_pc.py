# SPDX-License-Identifier: MIT
"""PC retail PHD v32 reader. Original code; OpenLara 2333adef layout facts."""
import hashlib
from pathlib import Path
from binary import Reader, FormatError, array
from ir import Level
from decode import common_arrays, read_textures, gameplay_tail, validate


def read(data, name='synthetic'):
    r = Reader(data, name)
    level = Level('pc', name, hashlib.sha256(data).hexdigest(), len(data))
    level.data['vab_programs'] = None
    level.data['vab_tones'] = None
    level.reserved['vab'] = None
    with r.section('version', 1):
        version = r.scalar('I')
        if version != 32:
            raise FormatError(f'{name}: expected PC version 32, got {version}')
    with r.section('texture_pages', prefix=4) as meta:
        n = r.count('I', 65536)
        level.images = [r.take(65536) for _ in range(n)]
        meta['count'] = n
    common_arrays(r, level, False)
    read_textures(r, level, False)
    gameplay_tail(r, level)
    with r.section('light_map', 8192):
        level.light_map = r.take(8192)
    with r.section('palette', 256):
        level.palette = r.take(768)
    array(r, level, 'cinematic_frames', count_fmt='H')
    with r.section('demo_data', prefix=2) as meta:
        n = r.count('H')
        level.data['demo_data'] = r.take(n)
        meta['count'] = n
    array(r, level, 'sound_map', 'h', count=256)
    array(r, level, 'sound_details')
    with r.section('samples', prefix=4) as meta:
        n = r.count('I')
        level.data['sample_data'] = r.take(n)
        meta['count'] = n
    offsets = array(r, level, 'sample_offsets', 'I')
    raw = level.data['sample_data']
    if offsets != sorted(set(offsets)) or (offsets and offsets[0] != 0):
        raise FormatError(f'{name}: invalid sample offsets')
    if not offsets and raw:
        raise FormatError(f'{name}: sample bytes without offsets')
    for i, offset in enumerate(offsets):
        end = offsets[i+1] if i+1 < len(offsets) else len(raw)
        if offset >= end or end > len(raw):
            raise FormatError(f'{name}: sample offset out of bounds')
        level.samples.append(raw[offset:end])
    level.sample_encoding = 'pc_wave'
    r.finish()
    level.sections = r.sections
    validate(level)
    return level


def load(path):
    path = Path(path)
    return read(path.read_bytes(), path.stem.lower())
