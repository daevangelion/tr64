# SPDX-License-Identifier: MIT
"""TR1 US retail PSX reader, original Python, no OpenLara reader code copied.
Layout facts: OpenLara 2333adef src/format.h and psx_level.[ch]; VAB bank
header/table structure follows Sony's public VAB format (psx-spx).
"""
import hashlib
from pathlib import Path
from binary import Reader, FormatError, array
from ir import Level
from decode import common_arrays, read_textures, gameplay_tail, validate


def read(data, name='synthetic'):
    r = Reader(data, name)
    level = Level('psx', name, hashlib.sha256(data).hexdigest(), len(data))
    level.data['vab_programs'] = None
    level.data['vab_tones'] = None
    cutscene = data[:8] == bytes(8)
    if cutscene:
        with r.section('texture_header', 1):
            level.reserved['texture_header'] = r.take(8)
        level.reserved['vab'] = None
    else:
        with r.section('vab_header', 1):
            header_size = r.scalar('I')
            magic, version, bank, bank_size, reserved, programs, tones, vags, volume, pan, attr1, attr2, reserved2 = r.unpack('4sIII4H4BI')
            if magic != b'pBAV' or version != 6 or programs > 128 or vags > 255:
                raise FormatError(f'{name}: invalid VAB header')
            if header_size != 2592+programs*512:
                raise FormatError(f'{name}: VAB header size count mismatch')
            level.reserved['vab'] = dict(header_size=header_size, version=version,
                bank=bank, bank_size=bank_size, reserved=reserved, programs=programs,
                tones=tones, vags=vags, volume=volume, pan=pan, attributes=(attr1,attr2),
                reserved2=reserved2)
        with r.section('vab_programs', 128):
            level.data['vab_programs'] = r.rows('6BH2I', 128)
        with r.section('vab_tones', programs*16):
            level.data['vab_tones'] = r.rows('16B8h', programs*16)
        with r.section('vab_sample_sizes', 256):
            sizes = [v[0] for v in r.rows('H', 256)]
            level.reserved['vab_sample_sizes'] = sizes
            if sizes[0] or any(sizes[vags+1:]):
                raise FormatError(f'{name}: nonzero unused VAB sample-size entry')
        with r.section('vab_body_size', 1):
            body_size = r.scalar('I')
            if body_size != sum(sizes[1:vags+1])*8:
                raise FormatError(f'{name}: VAB body-size mismatch')
            level.reserved['vab_body_size'] = body_size
        with r.section('samples', vags):
            level.samples = [r.take(size*8) for size in sizes[1:vags+1]]
        if r.pos != bank_size+8:
            raise FormatError(f'{name}: VAB tail slack must be zero')
        level.reserved['texture_header'] = None
    with r.section('texture_pages', 13):
        level.images = [r.take(32768) for _ in range(13)]
    with r.section('cluts', 1024):
        level.cluts = [r.take(32) for _ in range(1024)]
    common_arrays(r, level, True)
    read_textures(r, level, True)
    gameplay_tail(r, level)
    level.light_map = None
    level.palette = None
    level.data['demo_data'] = None
    array(r, level, 'sound_map', 'h', count=256)
    array(r, level, 'sound_details')
    with r.section('cinematic_reserved', 1):
        level.reserved['cinematic_word'] = r.scalar('I')
    array(r, level, 'cinematic_frames', count_fmt='H')
    level.sample_encoding = 'psx_spu_adpcm'
    level.data['sample_offsets'] = None
    level.data['sample_data'] = None
    r.finish()
    level.sections = r.sections
    validate(level)
    return level


def load(path):
    path = Path(path)
    return read(path.read_bytes(), path.stem.lower())
