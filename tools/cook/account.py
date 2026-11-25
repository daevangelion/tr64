# SPDX-License-Identifier: MIT
"""Independent count-only file walk. Does not use Reader, IR or decoder.

Count widths/record strides below are independently transcribed TR1 layout
facts (OpenLara 2333adef format.h and public VAB layout). No data decoding.
"""
import struct
from binary import FormatError


class Audit:
    def __init__(self, data):
        self.data, self.pos, self.sections = data, 0, []

    def number(self, width):
        if self.pos+width > len(self.data):
            raise FormatError('account: truncated count')
        n = int.from_bytes(self.data[self.pos:self.pos+width], 'little')
        self.pos += width
        return n

    def skip(self, size):
        if size < 0 or self.pos+size > len(self.data):
            raise FormatError('account: invalid count or truncated payload')
        self.pos += size

    def emit(self, name, start, count, prefix=0):
        self.sections.append(dict(name=name, offset=start, count=count,
            prefix_bytes=prefix, bytes=self.pos-start,
            payload_bytes=self.pos-start-prefix))

    def fixed(self, name, count, stride):
        start = self.pos
        self.skip(count*stride)
        self.emit(name,start,count)

    def counted(self, name, stride, width=4):
        start = self.pos
        n = self.number(width)
        self.skip(n*stride)
        self.emit(name,start,n,width)
        return n


def sections(data, source):
    a = Audit(data)
    psx = source == 'psx'
    if not psx:
        a.fixed('version',1,4)
        a.counted('texture_pages',65536)
    else:
        if data[:8] == bytes(8):
            a.fixed('texture_header',1,8)
        else:
            if len(data) < 36:
                raise FormatError('account: truncated VAB')
            programs = int.from_bytes(data[22:24],'little')
            vags = int.from_bytes(data[26:28],'little')
            bank_size = int.from_bytes(data[16:20],'little')
            a.fixed('vab_header',1,36)
            a.fixed('vab_programs',128,16)
            a.fixed('vab_tones',programs*16,32)
            sizes_start = a.pos
            a.fixed('vab_sample_sizes',256,2)
            size = sum(int.from_bytes(data[sizes_start+i*2:sizes_start+i*2+2],
                                      'little')*8 for i in range(1,vags+1))
            body_start = a.pos
            if a.number(4) != size:
                raise FormatError('account: VAB body-size mismatch')
            a.emit('vab_body_size',body_start,1)
            start = a.pos
            a.skip(size)
            a.emit('samples',start,vags)
            if a.pos != bank_size+8:
                raise FormatError('account: VAB tail slack must be zero')
        a.fixed('texture_pages',13,32768)
        a.fixed('cluts',1024,32)
    a.fixed('level_reserved',1,4)
    start = a.pos
    rooms = a.number(2)
    a.emit('room_count',start,1)
    for index in range(rooms):
        stem = f'rooms/{index}/'
        a.fixed(stem+'info',1,16)
        start = a.pos
        words = a.number(4)
        geometry_start = a.pos
        if psx:
            a.skip(2)
        geometry_counts = []
        for stride in (8,10,8,4):
            n = a.number(2)
            a.skip(n*stride)
            geometry_counts.append(n)
        remainder = words*2-(a.pos-geometry_start)
        if remainder not in ((0,2) if psx else (0,)):
            raise FormatError('account: room geometry count mismatch')
        a.skip(remainder)
        a.emit(stem+'geometry',start,words,4)
        nv,nq,nt,ns = geometry_counts
        a.sections[-1].update(vertices=nv,quads=nq,triangles=nt,sprites=ns,
                              triangle_equivalents=2*nq+nt)
        a.counted(stem+'portals',32,2)
        start = a.pos
        nz,nx = a.number(2),a.number(2)
        a.skip(nz*nx*8)
        a.emit(stem+'sectors',start,nz*nx,4)
        a.fixed(stem+'ambient',1,2)
        a.counted(stem+'lights',20 if psx else 18,2)
        a.counted(stem+'static_instances',20 if psx else 18,2)
        a.fixed(stem+'flags',1,4)
    for name,stride in [('floor_data',2),('mesh_data',2),('mesh_pointers',4),
        ('animations',32),('states',6),('ranges',8),('commands',2),('bones',4),
        ('frames',2),('models',20 if psx else 18),('static_meshes',32),
        ('object_textures',16 if psx else 20),('sprite_textures',16),
        ('sprite_sequences',8),('cameras',16),('sound_sources',16)]:
        a.counted(name,stride)
    boxes = a.counted('boxes',20)
    a.counted('overlaps',2)
    a.fixed('zones',6*boxes,2)
    a.counted('animated_textures',2)
    a.counted('items',22)
    if not psx:
        a.fixed('light_map',8192,1)
        a.fixed('palette',256,3)
        a.counted('cinematic_frames',16,2)
        a.counted('demo_data',1,2)
    a.fixed('sound_map',256,2)
    a.counted('sound_details',8)
    if psx:
        a.fixed('cinematic_reserved',1,4)
        a.counted('cinematic_frames',16,2)
    else:
        a.counted('samples',1)
        a.counted('sample_offsets',4)
    if a.pos != len(data):
        raise FormatError(f'account: trailing bytes {len(data)-a.pos}')
    return a.sections


def check(level, data):
    audited = sections(data, level.source)
    if len(audited) != len(level.sections):
        raise FormatError(f'{level.name}: independent section count mismatch')
    for parsed,independent in zip(level.sections,audited):
        for key in independent:
            if parsed[key] != independent[key]:
                raise FormatError(f'{level.name}: independent accounting mismatch '
                                  f'{parsed["name"]}.{key}')
    total = sum(s['bytes'] for s in audited)
    if total != len(data):
        raise FormatError(f'{level.name}: unexplained file bytes')
    return dict(source=level.source, level=level.name, sha256=level.sha256,
                file_bytes=len(data), explained_bytes=total, unexplained_bytes=0,
                sections=level.sections, independent_sections=audited)
