# SPDX-License-Identifier: MIT
"""Independent TR2/TR3 PC section walk from TRosettaStone §§3.2, 10.2–3."""
from account import Audit
from binary import FormatError


def walk(data):
    a = Audit(data)
    a.fixed('version',1,4)
    signature = int.from_bytes(data[:4],'little')
    if signature == 0x2d:
        third = False
    elif signature in (0xff080038,0xff180038,0xff180034):
        third = True
    else:
        raise FormatError(f'sequel account: unsupported version {signature:#x} at 0')
    a.fixed('palette',256,3)
    a.fixed('solid_colour_palette',256,4)
    pages = a.counted('texture_pages',65536)
    a.fixed('argb1555_pages',pages,131072)
    if signature == 0xff180034 and a.pos == len(data):
        return a.sections
    a.fixed('level_reserved',1,4)
    start = a.pos
    rooms = a.number(2)
    a.emit('room_count',start,1)
    for i in range(rooms):
        stem = f'rooms/{i}/'
        a.fixed(stem+'info',1,16)
        start = a.pos
        size = a.number(4)
        vertices = a.number(2)
        a.skip(12*vertices)
        quads = a.number(2)
        a.skip(10*quads)
        triangles = a.number(2)
        a.skip(8*triangles)
        sprites = a.number(2)
        a.skip(4*sprites)
        if a.pos != start+4+size*2:
            raise FormatError(f'sequel account: {stem}geometry length disagreement at {start}')
        a.emit(stem+'geometry',start,size,4)
        a.sections[-1].update(vertices=vertices,quads=quads,triangles=triangles,
                              sprites=sprites,triangle_equivalents=2*quads+triangles)
        a.counted(stem+'portals',32,2)
        start = a.pos
        cells = a.number(2)*a.number(2)
        a.skip(8*cells)
        a.emit(stem+'sectors',start,cells,4)
        a.fixed(stem+'ambient',1,4 if third else 6)
        a.counted(stem+'lights',24,2)
        a.counted(stem+'static_instances',20,2)
        a.fixed(stem+'flags',1,4)
        if third:
            a.fixed(stem+'tr3_water_reverb_filter',1,3)
    for key,stride in (('floor_data',2),('mesh_data',2),('mesh_pointers',4),('animations',32),
                       ('states',6),('ranges',8),('commands',2),('bones',4),('frames',2),
                       ('models',18),('static_meshes',32)):
        a.counted(key,stride)
    if not third:
        a.counted('object_textures',20)
    for key,stride in (('sprite_textures',16),('sprite_sequences',8),('cameras',16),('sound_sources',16)):
        a.counted(key,stride)
    boxes = a.counted('boxes',8)
    a.counted('overlaps',2)
    a.fixed('zones',10*boxes,2)
    a.counted('animated_textures',2)
    if third:
        a.counted('object_textures',20)
    a.counted('items',24)
    a.fixed('light_map',8192,1)
    a.counted('cinematic_frames',16,2)
    a.counted('demo_data',1,2)
    a.fixed('sound_map',370,2)
    a.counted('sound_details',8)
    a.counted('sample_ordinals',4)
    if a.pos != len(data):
        raise FormatError(f'sequel account: trailing bytes at {a.pos}')
    return a.sections


def check(level, data):
    audited = walk(data)
    for record, other in zip(level.sections,audited):
        if record != other:
            raise FormatError(f'{level.name}: independent account disagrees at {other["offset"]} '
                              f'for {other["name"]}: {record} != {other}')
    if len(level.sections) != len(audited):
        raise FormatError(f'{level.name}: independent count of sections differs')
    return len(data)
