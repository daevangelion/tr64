# SPDX-License-Identifier: MIT
"""TR1 layout facts from OpenLara 2333adef src/format.h; original Python."""
import struct
from binary import Reader, FormatError, array
from ir import Room, Face, Mesh, Texture


def illumination(value, psx=False, vertex=False):
    return {'value': value, 'encoding': 'psx_vertex_8bit' if psx and vertex else 'tr1_darkness_13bit'}


def faces(r, count, corners, psx, colored=False, entity=False):
    result = []
    for row in r.rows(f'{corners+1}H', count):
        indices, flags = row[:-1], row[-1]
        if psx and corners == 4 and not entity:
            indices = (indices[0], indices[1], indices[3], indices[2])
        is_color = colored or (psx and entity and (flags & 0x7fff) < 256)
        result.append(Face(indices, None if is_color else flags & 0x7fff,
                           flags & 0x7fff if is_color else None, flags,
                           None if is_color else ((0,1,3,2) if psx and corners==4 else tuple(range(corners)))))
    return result


def read_room(r, index, psx):
    stem = f'rooms/{index}/'
    with r.section(stem+'info', 1):
        x, z, bottom, top = r.unpack('4i')
    with r.section(stem+'geometry', prefix=4) as meta:
        words = r.count('I', 2)
        start = r.pos
        reserved = {'geometry_prefix': r.take(2)} if psx else {}
        nv = r.count('H', 8)
        if psx and int.from_bytes(reserved['geometry_prefix'],'little') != nv:
            raise FormatError(f'{r.label}: geometry prefix does not equal vertex count')
        vertices = [{'x': x, 'y': y, 'z': z, 'lighting': illumination(light, psx, True)}
                    for x, y, z, light in r.rows('3hH', nv)]
        nq = r.count('H', 10)
        quads = faces(r, nq, 4, psx)
        nt = r.count('H', 8)
        triangles = faces(r, nt, 3, psx)
        ns = r.count('H', 4)
        sprites = [dict(zip(('vertex', 'texture'), row)) for row in r.rows('2H', ns)]
        used = r.pos - start
        if used > words*2:
            raise FormatError(f'{r.label}: room {index} geometry count mismatch: '
                              f'{used} decoded bytes > {words*2} declared')
        # Alignment is a format field, not arbitrary unread geometry.
        padding = words*2-used
        if padding > (2 if psx else 0):
            raise FormatError(f'{r.label}: room {index} geometry count mismatch: '
                              f'{padding} unexplained bytes')
        reserved['geometry_padding'] = r.take(padding)
        meta.update(count=words, vertices=nv, quads=nq, triangles=nt,
                    sprites=ns, triangle_equivalents=nq*2+nt)
    with r.section(stem+'portals', prefix=2) as meta:
        n = r.count('H', 32)
        portals = r.records('portals', n)
        meta['count'] = n
    with r.section(stem+'sectors', prefix=4) as meta:
        nz, nx = r.unpack('2H')
        sectors = r.records('sectors', nz*nx)
        meta['count'] = nz*nx
    with r.section(stem+'ambient', 1):
        ambient = illumination(r.scalar('H'))
    with r.section(stem+'lights', prefix=2) as meta:
        n = r.count('H', 20 if psx else 18)
        lights = []
        for _ in range(n):
            lx, ly, lz, intensity = r.unpack('3iH')
            pad = r.take(2) if psx else None
            radius = r.scalar('I')
            lights.append(dict(x=lx, y=ly, z=lz, intensity=illumination(intensity),
                               radius=radius, reserved=pad))
        meta['count'] = n
    with r.section(stem+'static_instances', prefix=2) as meta:
        n = r.count('H', 20 if psx else 18)
        instances = []
        for _ in range(n):
            sx, sy, sz, angle, intensity, mesh = r.unpack('3i3H')
            pad = r.take(2) if psx else None
            instances.append(dict(x=sx, y=sy, z=sz, angle=angle,
                                  intensity=intensity, mesh=mesh, reserved=pad))
        meta['count'] = n
    with r.section(stem+'flags', 1):
        alternate, flags = r.unpack('hH')
    return Room(dict(x=x, z=z, y_bottom=bottom, y_top=top), vertices, quads,
                triangles, sprites, portals, nz, nx, sectors, ambient, lights,
                instances, alternate, flags, reserved)


def common_arrays(r, level, psx):
    with r.section('level_reserved', 1):
        level.reserved['level_word'] = r.scalar('I')
    with r.section('room_count', 1):
        nr = r.count('H', 20)
    level.rooms = [read_room(r, i, psx) for i in range(nr)]
    array(r, level, 'floor_data', 'H')
    with r.section('mesh_data', prefix=4) as meta:
        nw = r.count('I', 2)
        level.data['mesh_data'] = r.take(nw*2)
        meta['count'] = nw
    array(r, level, 'mesh_pointers', 'I')
    for name, fmt in [('animations', None), ('states', None), ('ranges', None),
                      ('commands', 'h'), ('bones', 'i'), ('frames', 'H')]:
        array(r, level, name, fmt)
    with r.section('models', prefix=4) as meta:
        n = r.count('I', 20 if psx else 18)
        models = []
        for _ in range(n):
            model = r.records('models', 1)[0]
            model['reserved'] = r.take(2) if psx else None
            models.append(model)
        level.data['models'] = models
        meta['count'] = n
    array(r, level, 'static_meshes')
    read_meshes(level, psx)


def read_meshes(level, psx):
    raw = level.data['mesh_data']
    offsets = sorted(set(level.data['mesh_pointers']))
    if offsets and offsets[0] != 0:
        raise FormatError(f'{level.name}: mesh data has unexplained prefix')
    if not offsets and raw:
        raise FormatError(f'{level.name}: mesh data has no pointers')
    for i, offset in enumerate(offsets):
        end = offsets[i+1] if i+1 < len(offsets) else len(raw)
        if offset >= end or end > len(raw) or offset % 2:
            raise FormatError(f'{level.name}: invalid mesh pointer {offset}')
        m = Reader(raw[offset:end], f'{level.name}:mesh/{offset}', offset)
        center = m.unpack('3h')
        radius, flags, signed_nv = m.unpack('HHh')
        nv = abs(signed_nv) if psx else signed_nv
        if nv < 0:
            raise FormatError(f'{m.label}: invalid vertex count {nv}')
        vertices = m.rows('4h' if psx else '3h', nv)
        ncount = signed_nv if psx else m.scalar('h')
        if abs(ncount) != nv:
            raise FormatError(f'{m.label}: normal/light count does not match vertices')
        normals = m.rows('4h' if psx else '3h', nv) if ncount > 0 else None
        lighting = [m.scalar('h') for _ in range(nv)] if ncount <= 0 else None
        all_faces = []
        for corners, colored in [(4, False), (3, False)] + ([] if psx else [(4, True), (3, True)]):
            count = m.count('H', (corners+1)*2)
            all_faces.extend(faces(m, count, corners, psx, colored, True))
        for face in all_faces:
            if any(v >= nv for v in face.vertices):
                raise FormatError(f'{m.label}: face vertex outside mesh')
        remaining = len(m.data)-m.pos
        if remaining > 3:
            raise FormatError(f'{m.label}: {remaining} unexplained mesh bytes')
        padding = m.take(remaining)
        if any(padding):
            raise FormatError(f'{m.label}: nonzero mesh padding')
        m.finish()
        level.meshes[offset] = Mesh(center, radius, flags, [v[:3] for v in vertices],
            [n[:3] for n in normals] if normals is not None else None,
            lighting, all_faces, offset, {'padding': padding,
            'vertex_w': [v[3] for v in vertices] if psx else None,
            'normal_w': [n[3] for n in normals] if psx and normals is not None else None})


def read_textures(r, level, psx):
    with r.section('object_textures', prefix=4) as meta:
        n = r.count('I', 16 if psx else 20)
        for _ in range(n):
            if psx:
                u0,v0,clut,u1,v1,tile,u2,v2,res,u3,v3,attr = r.unpack('BBHBBHBBHBBH')
                uv = ((u0,v0),(u1,v1),(u2,v2),(u3,v3))
                extra = {'word': res, 'tile_flags': tile & 0xc000}
            else:
                attr,tile = r.unpack('2H')
                coords = r.rows('4B', 4)
                uv = tuple((c[1],c[3]) for c in coords)
                clut = None
                extra = {'coordinate_adjustments': tuple((c[0],c[2]) for c in coords),
                         'tile_flags': tile & 0xc000}
            level.object_textures.append(Texture(tile & 0x3fff, clut, uv, attr, None, extra))
        meta['count'] = n
    with r.section('sprite_textures', prefix=4) as meta:
        n = r.count('I', 16)
        for _ in range(n):
            if psx:
                left,top,right,bottom,clut,tile,u0,v0,u1,v1 = r.unpack('4hHH4B')
                extra = {'tile_flags': tile & 0xc000}
            else:
                tile,u0,v0,w,h,left,top,right,bottom = r.unpack('HBBHH4h')
                u1,v1 = (u0+(w>>8)) & 255, (v0+(h>>8)) & 255
                clut = None
                extra = {'width_fixed': w, 'height_fixed': h, 'tile_flags': tile & 0xc000}
            level.sprite_textures.append(Texture(tile & 0x3fff, clut,
                ((u0,v0),(u1,v1)), None, (left,top,right,bottom), extra))
        meta['count'] = n
    array(r, level, 'sprite_sequences')


def gameplay_tail(r, level):
    for name in ('cameras', 'sound_sources', 'boxes'):
        array(r, level, name)
    array(r, level, 'overlaps', 'H')
    array(r, level, 'zones', 'h', count=6*len(level.data['boxes']))
    array(r, level, 'animated_textures', 'H')
    array(r, level, 'items')


def validate(level):
    for index, room in enumerate(level.rooms):
        for face in room.quads+room.triangles:
            if any(i >= len(room.vertices) for i in face.vertices):
                raise FormatError(f'{level.name}: room {index} face vertex outside room')
        for sprite in room.sprites:
            if sprite['vertex'] >= len(room.vertices) or sprite['texture'] >= len(level.sprite_textures):
                raise FormatError(f'{level.name}: room {index} sprite reference out of bounds')
    for mesh in level.meshes.values():
        for face in mesh.faces:
            if face.texture is not None and face.texture >= len(level.object_textures):
                raise FormatError(f'{level.name}: mesh texture {face.texture} out of bounds')
    for texture in level.object_textures+level.sprite_textures:
        if texture.page >= len(level.images):
            raise FormatError(f'{level.name}: texture page {texture.page} out of bounds')
        if texture.palette is not None and texture.palette >= len(level.cluts):
            raise FormatError(f'{level.name}: CLUT {texture.palette} out of bounds')
