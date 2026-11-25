# SPDX-License-Identifier: MIT
"""Classic PC TR2/TR3 reader. Layout references: docs/formats/src-tr2-tr3.md."""
import hashlib
from collections import Counter
from contextlib import contextmanager
from pathlib import Path

from binary import FormatError, Reader, array
from decode import faces, read_meshes
from ir import Level, Room, SCHEMAS, Texture
from n64_validate import TR2_FLOOR

VERSIONS = {0x2d: 2, 0xff080038: 3, 0xff180038: 3, 0xff180034: 3}
BOX_FIELDS = SCHEMAS['boxes'][1]
COMMAND_ARGUMENT_WORDS = (0, 3, 2, 0, 0, 2, 2)


class TextureOnly(FormatError):
    """A recognized TR3 prefix with no room-count or later level sections."""

    def __init__(self, level):
        self.level = level
        super().__init__(f'{level.name}: level_reserved at {level.file_bytes}: '
                         'EOF immediately after ARGB1555 pages; no level body')


class Cursor(Reader):
    @contextmanager
    def section(self, name, count=None, prefix=0):
        try:
            with super().section(name, count, prefix) as meta:
                yield meta
        except FormatError as error:
            raise FormatError(f'{name}: {error}') from error


def tagged(value, encoding):
    return dict(value=value, encoding=encoding)


def room(r, index, game):
    stem = f'rooms/{index}/'
    ext = {}
    with r.section(stem+'info', 1):
        info = dict(zip(('x', 'z', 'y_bottom', 'y_top'), r.unpack('4i')))
    with r.section(stem+'geometry', prefix=4) as meta:
        words = r.count('I', 2)
        g = Cursor(r.take(words*2), r.label, r.origin+r.pos-words*2)
        with g.section('vertices'):
            nv = g.count('H', 12)
            vertices = []
            for x,y,z,legacy,attributes,value in g.rows('3h3H', nv):
                light = tagged(value, 'tr2_darkness_13bit' if game == 2 else 'tr3_rgb555')
                vertices.append(dict(x=x, y=y, z=z, lighting=light,
                                     legacy_lighting=legacy, attributes=attributes))
        with g.section('quads'):
            nq = g.count('H', 10)
            quads = faces(g, nq, 4, False)
        with g.section('triangles'):
            nt = g.count('H', 8)
            triangles = faces(g, nt, 3, False)
        with g.section('sprites'):
            ns = g.count('H', 4)
            sprites = [dict(vertex=v, texture=t) for v,t in g.rows('2H', ns)]
        g.finish()
        meta.update(count=words, vertices=nv, quads=nq, triangles=nt,
                    sprites=ns, triangle_equivalents=2*nq+nt)
    with r.section(stem+'portals', prefix=2) as meta:
        meta['count'] = r.count('H', 32)
        portals = r.records('portals', meta['count'])
    with r.section(stem+'sectors', prefix=4) as meta:
        nz,nx = r.unpack('2H')
        meta['count'] = nz*nx
        sectors = r.records('sectors', nz*nx)
        if game == 3:
            for sector in sectors:
                raw = sector['box_index']
                box = (raw >> 4) & 2047
                sector.update(box_word=raw, material=raw & 15,
                              box_index=65535 if box == 2047 else box)
    with r.section(stem+'ambient', 1):
        ambient = tagged(r.scalar('H'), 'tr2_room_ambient' if game == 2 else 'tr3_room_ambient_word')
        ext['ambient2'] = r.scalar('H')
        if game == 2:
            ext['light_mode'] = r.scalar('H')
    with r.section(stem+'lights', prefix=2) as meta:
        meta['count'] = r.count('H', 24)
        lights = []
        for _ in range(meta['count']):
            x,y,z = r.unpack('3i')
            light = dict(x=x, y=y, z=z, reserved=None)
            if game == 2:
                i1,i2,fade1,fade2 = r.unpack('2H2I')
                light.update(intensity=tagged(i1, 'tr2_point_intensity'), radius=fade1,
                             intensity2=i2, radius2=fade2)
            else:
                rgb = r.unpack('3B')
                kind = r.scalar('B')
                if kind not in (0, 1):
                    raise FormatError(f'{r.label}: unsupported TR3 light type {kind} at {r.pos-1}')
                light.update(rgb=rgb, kind=kind, sun_or_point=r.take(8))
            lights.append(light)
    with r.section(stem+'static_instances', prefix=2) as meta:
        meta['count'] = r.count('H', 20)
        instances = [dict(x=x, y=y, z=z, angle=a, intensity=i, intensity2=j, mesh=m,
                          reserved=None, intensity_encoding='tr2_darkness_13bit' if game == 2 else 'tr3_rgb555')
                     for x,y,z,a,i,j,m in r.rows('3i4H', meta['count'])]
    with r.section(stem+'flags', 1):
        alternate,flags = r.unpack('hH')
    if game == 3:
        with r.section(stem+'tr3_water_reverb_filter', 1):
            ext['water_scheme'],ext['reverb'],ext['filter'] = r.unpack('3B')
    return Room(info, vertices, quads, triangles, sprites, portals, nz, nx,
                sectors, ambient, lights, instances, alternate, flags, ext)


def textures(r, level, game, sprite):
    key = 'sprite_textures' if sprite else 'object_textures'
    with r.section(key, prefix=4) as meta:
        meta['count'] = r.count('I', 16 if sprite else 20)
        for _ in range(meta['count']):
            pos = r.pos
            if sprite:
                tile,u,v,w,h,*bounds = r.unpack('HBBHH4h')
                uv = ((u,v), ((u+(w>>8))&255, (v+(h>>8))&255))
                attr, bounds = None, tuple(bounds)
                ext = dict(width_fixed=w, height_fixed=h)
            else:
                attr,tile = r.unpack('2H')
                if attr not in (range(2) if game == 2 else range(3)):
                    raise FormatError(f'{r.label}: unsupported blend mode {attr} at {pos}')
                coords = r.rows('4B', 4)
                uv = tuple((u,v) for _,u,_,v in coords)
                bounds = None
                ext = dict(coordinate_adjustments=tuple((a,b) for a,_,b,_ in coords))
            ext['tile_flags'] = tile & 0xc000
            getattr(level, key).append(Texture(tile & 0x3fff, None, uv, attr, bounds, ext))


def floor_codes(level, offset, game, finding):
    """Validate the reachable lists; count physical opcode locations once."""
    data = level.data
    words = data['floor_data']
    functions, triggers, actions = Counter(), Counter(), Counter()
    records, action_ends = set(), {}
    covered = set()
    opaque_actions = Counter()

    def fail(pos, why):
        raise FormatError(f'{level.name}: floor_data at {offset+2*pos} (word {pos}): {why}')

    def take(pos):
        if pos >= len(words):
            fail(pos, 'unterminated/truncated stream')
        covered.add(pos)
        return words[pos]

    def ref(pos, value, size, name):
        if not 0 <= value < size:
            fail(pos, f'{name} reference {value} outside {size} entries')

    def action_list(pos):
        path = []
        while pos not in action_ends:
            path.append(pos)
            word = take(pos)
            if game == 3 and word == 0xffff:
                finding('floor_data', offset+2*pos, 'opaque trigger action word 0xffff')
                opaque_actions[31] += 1
                pos += 1  # Only the set end bit is known; do not invent an action.
                break
            kind, argument = (word>>10)&(TR2_FLOOR['action_kind_mask'] if game == 2 else 31), word&1023
            if kind > (TR2_FLOOR['max_action'] if game == 2 else 11):
                fail(pos, f'unsupported trigger action {kind:#x}')
            actions[kind] += 1
            if (TR2_FLOOR['item_mask'] & (1 << kind)) if game == 2 else kind in (0, 6):
                ref(pos, argument, len(data['items']), 'item')
            if (TR2_FLOOR['camera_mask'] & (1 << kind)) if game == 2 else kind in (1, 2):
                ref(pos, argument, len(data['cameras']), 'camera/sink')
            if game == 2 and TR2_FLOOR['flip_mask'] & (1 << kind):
                ref(pos, argument, TR2_FLOOR['flip_count'], 'flip-map')
            if (TR2_FLOOR['extra_mask'] & (1 << kind)) if game == 2 else kind == 1:
                pos += 1
                word = take(pos)
            pos += 1
            if word & 0x8000:
                break
        else:
            pos = action_ends[pos]
        for start in path:
            action_ends[start] = pos
        return pos

    starts = {s['floor_index'] for room in level.rooms for s in room.sectors} - {0}
    for pos in sorted(starts):
        while pos not in records:
            records.add(pos)
            word = take(pos)
            kind = word & (TR2_FLOOR['kind_mask'] if game == 2 else 31)
            if kind > (TR2_FLOOR['max_function'] if game == 2 else 21):
                fail(pos, f'unsupported floor function {kind:#x}')
            functions[kind] += 1
            pos += 1
            if ((TR2_FLOOR['payload_mask'] & (1 << kind)) if game == 2
                    else kind in (1, 2, 3) or 7 <= kind <= 18):
                arg = take(pos)
                if kind == 1:
                    ref(pos, arg, len(level.rooms), 'portal room')
                pos += 1
            elif kind == 4:
                trigger = (word>>8)&127
                if trigger > (TR2_FLOOR['max_trigger'] if game == 2 else 12):
                    fail(pos-1, f'unsupported trigger type {trigger:#x}')
                triggers[trigger] += 1
                take(pos)
                pos += 1
                if (TR2_FLOOR['controller_mask'] & (1 << trigger)) if game == 2 else trigger in (2, 3, 4, 10):
                    controller = take(pos)
                    ref(pos, controller&1023, len(data['items']), 'trigger controller item')
                    pos += 1
                    if not controller & 0x8000:
                        pos = action_list(pos)
                else:
                    pos = action_list(pos)
            if word & 0x8000:
                break
    unreferenced = []
    for word in range(len(words)):
        if word in covered:
            continue
        if unreferenced and unreferenced[-1][0]+unreferenced[-1][1] == word:
            unreferenced[-1][1] += 1
        else:
            unreferenced.append([word,1])
    return dict(functions=dict(functions), trigger_types=dict(triggers), actions=dict(actions),
                starts=len(starts), reachable_words=len(covered),
                unreferenced_words=len(words)-len(covered),
                unreferenced_spans=unreferenced,
                opaque_actions=dict(opaque_actions))


def validate(level, game, bank, census):
    d = level.data
    sections = {row['name']:row for row in level.sections}
    findings = []

    def finding(section, offset, reason):
        if not census:
            raise FormatError(f'{level.name}: {section} at {offset}: {reason}')
        findings.append(dict(section=section, offset=offset, reason=reason))

    def span(key, first, count, size):
        if first < 0 or count < 0 or first+count > size:
            row = sections[key]
            raise FormatError(f'{level.name}: {key} at {row["offset"]}: '
                              f'reference {first}+{count} outside {size} entries')

    statics = {row['id'] for row in d['static_meshes']}
    for i,r in enumerate(level.rooms):
        stem = f'rooms/{i}/'
        if r.alternate_room != -1:
            span(stem+'flags', r.alternate_room, 1, len(level.rooms))
        for portal in r.portals:
            span(stem+'portals', portal['room'], 1, len(level.rooms))
        for face in r.quads+r.triangles:
            for index in face.vertices:
                span(stem+'geometry', index, 1, len(r.vertices))
            span(stem+'geometry', face.texture, 1, len(level.object_textures))
        for sprite in r.sprites:
            span(stem+'geometry', sprite['vertex'], 1, len(r.vertices))
            span(stem+'geometry', sprite['texture'], 1, len(level.sprite_textures))
        for sector in r.sectors:
            if sector['floor_index']:
                span(stem+'sectors', sector['floor_index'], 1, len(d['floor_data']))
            if sector['box_index'] != 65535:
                span(stem+'sectors', sector['box_index'], 1, len(d['boxes']))
            for key in ('room_above', 'room_below'):
                if sector[key] != 255:
                    span(stem+'sectors', sector[key], 1, len(level.rooms))
        for instance in r.static_instances:
            if instance['mesh'] not in statics:
                raise FormatError(f'{level.name}: {stem}static_instances at '
                                  f'{sections[stem+"static_instances"]["offset"]}: undefined static {instance["mesh"]}')
    for mesh in level.meshes.values():
        for face in mesh.faces:
            if face.texture is not None:
                span('mesh_data', face.texture, 1, len(level.object_textures))
    for key in ('object_textures','sprite_textures'):
        for texture in getattr(level, key):
            span(key, texture.page, 1, len(level.images))
    for s in d['sprite_sequences']:
        span('sprite_sequences', s['first'], -s['count'], len(level.sprite_textures))
    for row in d['static_meshes']:
        span('static_meshes', row['mesh'], 1, len(d['mesh_pointers']))
    for i,row in enumerate(d['models']):
        if row['bone_offset'] % 4 or row['frame_offset'] % 2:
            raise FormatError(f'{level.name}: models at {sections["models"]["offset"]+4+18*i}: '
                              'unaligned bone or frame offset')
        span('models', row['mesh_start'], row['mesh_count'], len(d['mesh_pointers']))
        span('models', row['bone_offset'], 4*max(0,row['mesh_count']-1), len(d['bones']))
        span('models', row['frame_offset'], 0, 2*len(d['frames']))
        if row['animation'] != 65535:
            span('models', row['animation'], 1, len(d['animations']))
    commands = d['commands']
    for i,row in enumerate(d['animations']):
        if row['frame_rate'] == 0 or row['frame_offset'] & 1 or row['frame_end'] < row['frame_start']:
            raise FormatError(f'{level.name}: animations at {sections["animations"]["offset"]}: invalid frame descriptor {row}')
        frames = 1 + (row['frame_end']-row['frame_start'])//row['frame_rate']
        span('animations', row['frame_offset']//2, frames*row['frame_size'], len(d['frames']))
        span('animations', row['next_animation'], 1, len(d['animations']))
        span('animations', row['state_offset'], row['state_count'], len(d['states']))
        pos = row['command_offset']
        span('animations', pos, row['command_count'], len(commands))
        for _ in range(row['command_count']):
            span('commands', pos, 1, len(commands))
            opcode = commands[pos]
            if not 0 <= opcode < len(COMMAND_ARGUMENT_WORDS):
                offset = sections['commands']['offset']+4+2*pos
                raise FormatError(f'{level.name}: commands at {offset}: animation {i}, unsupported opcode {opcode}')
            pos += 1+COMMAND_ARGUMENT_WORDS[opcode]
            span('commands', pos, 0, len(commands))
    for row in d['states']:
        span('states', row['range_offset'], row['range_count'], len(d['ranges']))
    for i,row in enumerate(d['ranges']):
        target = row['next_animation'] & 65535
        if game == 3 and target >= len(d['animations']):
            finding('ranges', sections['ranges']['offset']+4+8*i+4,
                    f'dispatch {i} targets animation {target} outside {len(d["animations"])} entries')
        else:
            span('ranges', target, 1, len(d['animations']))
    for item in d['items']:
        span('items', item['room'], 1, len(level.rooms))
    overlaps = d['overlaps']
    valid = [False]*(len(overlaps)+1)
    for i in range(len(overlaps)-1, -1, -1):
        valid[i] = (overlaps[i]&0x3fff) < len(d['boxes']) and (bool(overlaps[i]&0x8000) or valid[i+1])
    for i,box in enumerate(d['boxes']):
        if box['overlap'] != 65535:
            head = box['overlap'] & 0x3fff
            if game == 3 and box['overlap'] == 2047 and (head >= len(overlaps) or not valid[head]):
                finding('boxes', sections['boxes']['offset']+4+8*i+6,
                        f'box {i} overlap head 0x07ff does not resolve to a valid chain')
                continue
            span('overlaps', head, 1, len(overlaps))
            if not valid[head]:
                raise FormatError(f'{level.name}: overlaps at {sections["overlaps"]["offset"]}: bad chain at word {head}')
    animated = d['animated_textures']
    if animated:
        pos = 1
        for _ in range(animated[0]):
            span('animated_textures', pos, 1, len(animated))
            count = animated[pos]+1
            pos += 1
            span('animated_textures', pos, count, len(animated))
            for member in animated[pos:pos+count]:
                span('animated_textures', member, 1, len(level.object_textures))
            pos += count
        if any(animated[pos:]):
            raise FormatError(f'{level.name}: animated_textures at {sections["animated_textures"]["offset"]+4+2*pos}: nonzero tail')
    for sound in d['sound_map']:
        if sound != -1:
            span('sound_map', sound, 1, len(d['sound_details']))
    for source in d['sound_sources']:
        span('sound_sources', source['sound'], 1, 370)
    for detail in d['sound_details']:
        span('sound_details', detail['sample'], max(1,(detail['flags']>>2)&15), len(d['sample_ordinals']))
    if bank is not None:
        for ordinal in d['sample_ordinals']:
            span('sample_ordinals', ordinal, 1, len(bank))
    level.reserved['floor_codes'] = floor_codes(level, sections['floor_data']['offset']+4, game, finding)
    level.reserved['findings'] = findings


def read(data, name='synthetic', bank=None, *, census=False):
    """Strict Level IR by default; census preserves named unresolved TR3 records."""
    r = Cursor(data, name)
    level = Level('pc', name, hashlib.sha256(data).hexdigest(), len(data))
    d = level.data
    with r.section('version', 1):
        version = r.scalar('I')
        if version not in VERSIONS:
            raise FormatError(f'{name}: unsupported PC level version {version:#x} at 0')
        game = VERSIONS[version]
    level.reserved.update(game=game, version=version)
    with r.section('palette', 256):
        level.palette = r.take(768)
    with r.section('solid_colour_palette', 256):
        level.reserved['solid_colour_palette'] = r.take(1024)
    with r.section('texture_pages', prefix=4) as meta:
        meta['count'] = n = r.count('I', 3*65536)
        level.images = [r.take(65536) for _ in range(n)]
    with r.section('argb1555_pages', n):
        level.reserved['argb1555_pages'] = [r.take(131072) for _ in range(n)]
    if version == 0xff180034 and r.pos == len(r.data):
        r.finish()
        level.sections = r.sections
        raise TextureOnly(level)
    with r.section('level_reserved', 1):
        level.reserved['level_word'] = r.scalar('I')
    with r.section('room_count', 1):
        nr = r.count('H', 48)
    level.rooms = [room(r, i, game) for i in range(nr)]
    array(r, level, 'floor_data', 'H')
    with r.section('mesh_data', prefix=4) as meta:
        meta['count'] = r.count('I', 2)
        d['mesh_data'] = r.take(2*meta['count'])
    array(r, level, 'mesh_pointers', 'I')
    try:
        read_meshes(level, False)
    except FormatError as error:
        raise FormatError(f'mesh_data at {r.sections[-2]["offset"]+4}: {error}') from error
    for key,fmt in (('animations', None), ('states', None), ('ranges', None),
                    ('commands', 'h'), ('bones', 'i'), ('frames', 'H'),
                    ('models', None), ('static_meshes', None)):
        array(r, level, key, fmt)
    for model in d['models']:
        model['reserved'] = None
    if game == 2:
        textures(r, level, game, False)
    textures(r, level, game, True)
    for key in ('sprite_sequences','cameras','sound_sources'):
        array(r, level, key)
    with r.section('boxes', prefix=4) as meta:
        meta['count'] = n = r.count('I', 8)
        d['boxes'] = [dict(zip(BOX_FIELDS, tuple(v*1024 for v in xyz[:4])+xyz[4:]))
                      for xyz in r.rows('4BhH', n)]
    array(r, level, 'overlaps', 'H')
    with r.section('zones', 10*n):
        zones = [row[0] for row in r.rows('h', 10*n)]
        d['zones'] = zones[:2*n] + zones[4*n:7*n] + zones[9*n:]
        d['extra_ground_zones'] = zones[2*n:4*n] + zones[7*n:9*n]
    array(r, level, 'animated_textures', 'H')
    if game == 3:
        textures(r, level, game, False)
    with r.section('items', prefix=4) as meta:
        meta['count'] = n = r.count('I', 24)
        fields = (*SCHEMAS['items'][1][:-1], 'intensity2', 'flags')
        d['items'] = [dict(zip(fields,row)) for row in r.rows('HH3iH2hH', n)]
    with r.section('light_map', 8192):
        level.light_map = r.take(8192)
    array(r, level, 'cinematic_frames', count_fmt='H')
    with r.section('demo_data', prefix=2) as meta:
        meta['count'] = r.count('H')
        d['demo_data'] = r.take(meta['count'])
    array(r, level, 'sound_map', 'h', count=370)
    if game == 2:
        array(r, level, 'sound_details')
    else:
        with r.section('sound_details', prefix=4) as meta:
            meta['count'] = n = r.count('I', 8)
            fields = ('sample','volume','range','chance','pitch','flags')
            d['sound_details'] = [dict(zip(fields,row)) for row in r.rows('H4BH', n)]
    array(r, level, 'sample_ordinals', 'I')
    d.update(sample_data=None, sample_offsets=None, vab_programs=None, vab_tones=None)
    r.finish()
    level.sections = r.sections
    validate(level, game, bank, census)
    level.sample_encoding = 'pc_wave' if bank is not None else 'pc_external_wave_unresolved'
    if bank is not None:
        level.samples = [bank[i] for i in d['sample_ordinals']]
    return level


def read_bank(data, name='MAIN.SFX'):
    """Partition and check the external bank; keep complete RIFF records."""
    r = Cursor(data, name)
    samples = []
    while r.pos < len(r.data):
        start = r.pos
        with r.section(f'sample/{len(samples)}', 1):
            magic,size,wave = r.unpack('4sI4s')
            if magic != b'RIFF' or wave != b'WAVE' or size < 4:
                raise FormatError(f'{name}: not RIFF WAVE at {start}')
            end = start+8+size
            if end > len(r.data):
                raise FormatError(f'{name}: RIFF WAVE at {start} exceeds the bank')
            fmt, pcm_bytes = None, None
            body = Cursor(r.data[r.pos:end], name, r.pos)
            while body.pos < len(body.data):
                at = body.origin+body.pos
                chunk, length = body.unpack('4sI')
                if chunk == b'fmt ' and fmt is None and length == 16:
                    fmt = body.unpack('HHIIHH')
                elif chunk == b'data' and pcm_bytes is None:
                    pcm_bytes = length
                    body.take(length + (length & 1))
                else:
                    raise FormatError(f'{name}: unsupported/duplicate WAVE chunk {chunk!r} at {at}')
            body.finish()
            if fmt is None or pcm_bytes is None:
                raise FormatError(f'{name}: missing WAVE format or data at {start}')
            codec, channels, rate, byte_rate, stride, bits = fmt
            if (codec != 1 or channels != 1 or bits not in (8, 16) or not rate or
                    stride != bits//8 or byte_rate != rate*stride or pcm_bytes % stride or not pcm_bytes):
                raise FormatError(f'{name}: unsupported/inconsistent WAVE PCM at {start}: {fmt}')
            r.pos = end
            samples.append(bytes(r.data[start:r.pos]))
    r.finish()
    return samples


def load(path, bank=None, *, census=False):
    path = Path(path)
    return read(path.read_bytes(), str(path), bank, census=census)
