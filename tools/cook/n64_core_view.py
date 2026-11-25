# SPDX-License-Identifier: MIT
"""Test decoder written from n64-pack.md/n64-core.md, no writer imports."""
from dataclasses import dataclass
import hashlib
import struct
import zlib
from n64_tex_view import Section as TextureSection

# Literal wire layouts, intentionally independent of writer/IR schema tables.
LAYOUTS=[
 ('rooms','6i2Hh3H10I4x','x z y_bottom y_top bounds_bottom bounds_top z_sectors x_sectors alternate_room flags ambient reserved sector_start sector_count portal_start portal_count light_start light_count instance_start instance_count sprite_start sprite_count'),
 ('sectors','HHBbBb','floor_index box_index room_below floor room_above ceiling'),
 ('portals','H15h','room normal_x normal_y normal_z x0 y0 z0 x1 y1 z1 x2 y2 z2 x3 y3 z3'),
 ('lights','3iHHII','x y z intensity reserved radius reserved_present'),
 ('static_instances','3ih3HI','x y z angle intensity mesh reserved reserved_present'),
 ('floor_data','H',''),('mesh_pointers','I',''),
 ('animations','IBBHii8H','frame_offset frame_rate frame_size state speed acceleration frame_start frame_end next_animation next_frame state_count state_offset command_count command_offset'),
 ('states','3H','state range_count range_offset'),('ranges','4H','low high next_animation next_frame'),
 ('commands','h',''),('bones','i',''),
 ('models','IHHIIHHI','type mesh_count mesh_start bone_offset frame_offset animation reserved reserved_present'),
 ('frames','H',''),
 ('static_meshes','IH12hH','id mesh visible_min_x visible_max_x visible_min_y visible_max_y visible_min_z visible_max_z collision_min_x collision_max_x collision_min_y collision_max_y collision_min_z collision_max_z flags'),
 ('sprite_sequences','IhH','type count first'),('cameras','3ihH','x y z room_or_speed flags'),
 ('sound_sources','3iHH','x y z sound flags'),('boxes','4ihH','min_z max_z min_x max_x floor overlap'),
 ('overlaps','H',''),('zones','H',''),('items','HH3ihhH2x','type room x y z angle intensity flags'),
 ('cinematic_frames','8h','target_x target_y target_z camera_x camera_y camera_z fov roll'),
 ('demo_data','B',''),('sound_map','h',''),('sound_details','4H','sample volume chance flags'),('node_flags','I',''),
 ('room_sprites','3h3H','x y z gain descriptor reserved'),
 ('mesh_spheres','3hH','x y z radius')]


@dataclass
class NativeLevel:
    sections:dict
    checksums:dict
    game:int=0


def lz4(data,size):
    output=bytearray(); cursor=0
    def extended(value):
        nonlocal cursor
        if value==15:
            while True:
                if cursor==len(data):raise ValueError('independent LZ4 truncated length')
                part=data[cursor]; cursor+=1; value+=part
                if part<255:break
        return value
    while cursor<len(data):
        token=data[cursor]; cursor+=1
        literals=extended(token>>4)
        if literals>len(data)-cursor or literals>size-len(output):raise ValueError('independent LZ4 literal bound')
        output+=data[cursor:cursor+literals]; cursor+=literals
        if cursor==len(data):break
        if cursor+2>len(data):raise ValueError('independent LZ4 distance truncated')
        distance=int.from_bytes(data[cursor:cursor+2],'little'); cursor+=2
        count=extended(token&15)+4
        if not distance or distance>len(output) or count>size-len(output):raise ValueError('independent LZ4 match bound')
        for _ in range(count):output.append(output[len(output)-distance])
    if len(output)!=size:raise ValueError('independent LZ4 output size')
    return bytes(output)


def read_pack(blob):
    if len(blob)<32:raise ValueError('independent pack header truncated')
    magic,version,total,levels,directory,stride,crc,zero=struct.unpack_from('>4s7I',blob)
    if magic!=b'TR6P' or version!=2 or total!=len(blob) or directory!=32 or stride!=48 or zero:
        raise ValueError('independent pack header')
    if levels>(len(blob)-32)//48:raise ValueError('independent directory bounds')
    if zlib.crc32(blob[32:32+48*levels])!=crc:raise ValueError('independent directory CRC')
    result={}
    for i in range(levels):
        name,offset,count,crc,game=struct.unpack_from('>32s4I',blob,32+i*48)
        name=name.split(b'\0',1)[0].decode()
        if game not in (0,2):
            raise ValueError(f'independent pack {name}: unsupported game version {game}')
        if not name or name in result or offset%16 or count>len(blob)//64 or offset>len(blob)-count*64:
            raise ValueError('independent section table bounds')
        table=blob[offset:offset+64*count]
        if zlib.crc32(table)!=crc:raise ValueError('independent section table CRC')
        sections={}; checksums={}; previous=0
        for j in range(count):
            kind,destination,physical,stored,raw,alignment,codec,checksum,digest=struct.unpack_from('>8I32s',table,j*64)
            if (kind<previous or not kind or alignment!=16 or codec>1 or
                    not 0<stored<=16384 or not 0<raw<=16384 or physical%16 or
                    physical>len(blob)-((stored+1)&~1)):
                raise ValueError('independent physical section bounds')
            previous=kind
            output=sections.setdefault(kind,bytearray())
            if destination!=len(output):raise ValueError('independent destination partition')
            payload=blob[physical:physical+stored]
            payload=lz4(payload,raw) if codec else payload
            if len(payload)!=raw or zlib.crc32(payload)!=checksum or hashlib.sha256(payload).digest()!=digest:
                raise ValueError('independent content hash')
            output.extend(payload); checksums[kind]=zlib.crc32(struct.pack('>I',checksum),checksums.get(kind,0))
        result[name]=NativeLevel({k:bytes(v) for k,v in sections.items()},checksums,game)
    return result


def decode_core(native):
    values={}
    for kind,(name,fmt,fields) in enumerate(LAYOUTS,1):
        if native.game==2:
            if name=='rooms':
                fmt,fields=fmt[:-2]+'2H',fields+' ambient2 light_mode'
            elif name=='items':
                fmt,fields=fmt[:-2]+'h',fields+' intensity2'
            elif name=='lights':
                fields=fields.replace('reserved ','intensity2 ').replace('reserved_present','radius2')
            elif name=='static_instances':
                fields=fields.replace('reserved ','intensity2 ')
        raw=native.sections[kind]
        if len(raw)<32:raise ValueError(f'{name}: independent header truncated')
        magic,ident,count,stride,present,offset,total,abi=struct.unpack_from('>4s7I',raw)
        size=struct.calcsize('>'+fmt)
        if (magic!=b'TR6C' or ident!=kind or stride!=size or present not in (0,1) or offset!=32 or
                total!=len(raw) or abi!=3 or count>(len(raw)-32)//size or count*size!=len(raw)-32):
            raise ValueError(f'{name}: independent table layout')
        rows=list(struct.iter_unpack('>'+fmt,raw[32:]))
        values[name]=[dict(zip(fields.split(),r)) for r in rows] if fields else [r[0] for r in rows]
        if name=='demo_data':values[name]=bytes(values[name])
        if not present:
            if name!='demo_data' or count:raise ValueError('independent invalid absence')
            values[name]=None
        if native.game==0 and name in ('rooms','items'):
            start,length=(76,4) if name=='rooms' else (22,2)
            if any(any(raw[32+i*size+start:32+i*size+start+length]) for i in range(count)):
                raise ValueError('independent nonzero record padding')
    return values


def equal_ir(level,native):
    """Compare source fields directly, never calling writer projection/decode."""
    v=decode_core(native); d=level.data
    def equal(name,actual,expected):
        if actual!=expected:raise ValueError(f'{level.source}/{level.name}/{name}: independent IR mismatch')
    for name in ('floor_data','mesh_pointers','animations','states','commands','bones','frames',
                 'static_meshes','cameras','sound_sources','boxes','overlaps','cinematic_frames',
                 'demo_data','sound_map','sound_details'):
        equal(name,v[name],d[name])
    spheres=[]
    for offset in d['mesh_pointers']:
        mesh=level.meshes[offset]
        spheres.append(dict(zip(('x','y','z','radius'),(*mesh.center,mesh.radius))))
    equal('mesh_spheres',v['mesh_spheres'],spheres)
    signed=lambda n:(n&32767)-(n&32768)
    equal('ranges',[{k:signed(n) for k,n in r.items()} for r in v['ranges']],d['ranges'])
    equal('zones',[signed(n) for n in v['zones']],d['zones'])
    equal('items',[dict(r,angle=r['angle']&65535) for r in v['items']],d['items'])
    def reserved(row):
        row=dict(row); present=row.pop('reserved_present')
        row['reserved']=row['reserved'].to_bytes(2,'big') if present else None
        return row
    equal('models',[reserved(r) for r in v['models']],d['models'])
    equal('sectors',v['sectors'],[s for r in level.rooms for s in r.sectors])
    equal('portals',v['portals'],[p for r in level.rooms for p in r.portals])
    lights=[]
    for r in v['lights']:
        r=reserved(r); r['intensity']={'value':r['intensity'],'encoding':'tr1_darkness_13bit'}; lights.append(r)
    equal('lights',lights,[l for r in level.rooms for l in r.lights])
    instances=[dict(reserved(r),angle=r['angle']&65535) for r in v['static_instances']]
    equal('static_instances',instances,[m for r in level.rooms for m in r.static_instances])
    equal('room count',len(v['rooms']),len(level.rooms))
    starts=dict(sector=0,portal=0,light=0,instance=0,sprite=0)
    for record,room in zip(v['rooms'],level.rooms):
        for field in ('x','z','y_bottom','y_top'):equal('room '+field,record[field],room.info[field])
        for field in ('x_sectors','z_sectors','alternate_room','flags'):equal('room '+field,record[field],getattr(room,field))
        equal('room ambient',record['ambient'],room.ambient['value']); equal('room reserved',record['reserved'],0)
        ys=[vertex['y'] for vertex in room.vertices]
        equal('room bounds bottom',record['bounds_bottom'],max(ys) if ys else room.info['y_bottom'])
        equal('room bounds top',record['bounds_top'],min(ys) if ys else room.info['y_top'])
        for short,key in (('sector','sectors'),('portal','portals'),('light','lights'),('instance','static_instances'),('sprite','sprites')):
            equal('room '+short+' start',record[short+'_start'],starts[short])
            equal('room '+short+' count',record[short+'_count'],len(getattr(room,key)))
            starts[short]+=len(getattr(room,key))
    texture=TextureSection(native.sections[65536])
    object_map={r['source_index']:i for i,r in enumerate(texture.descriptors) if r['kind']=='object'}
    sprite_map={r['source_index']:i for i,r in enumerate(texture.descriptors) if r['kind']=='sprite'}
    room_sprites=[]
    for room in level.rooms:
        for sprite in room.sprites:
            vertex=room.vertices[sprite['vertex']]; light=vertex['lighting']
            numerator=light['value']*32 if light['encoding']=='psx_vertex_8bit' else 8191-light['value']
            room_sprites.append(dict(x=vertex['x'],y=vertex['y'],z=vertex['z'],
                gain=min(8192,max(0,numerator)),descriptor=sprite_map[sprite['texture']],reserved=0))
    equal('room_sprites',v['room_sprites'],room_sprites)
    restored=[]
    for row in v['sprite_sequences']:
        first=row['first']; count=-row['count']
        if first<texture.objects or first+count>len(texture.descriptors):raise ValueError('independent sprite range')
        source=texture.descriptors[first]['source_index']
        for i in range(count):
            equal('sprite sequence member',texture.descriptors[first+i]['source_index'],source+i)
        restored.append(dict(row,first=source))
    equal('sprite_sequences',restored,d['sprite_sequences'])
    referenced={f.texture for room in level.rooms for f in room.quads+room.triangles if f.texture is not None}
    referenced.update(f.texture for mesh in level.meshes.values() for f in mesh.faces if f.texture is not None)
    sprites={s['texture'] for room in level.rooms for s in room.sprites}
    for row in d['sprite_sequences']:
        sprites.update(range(row['first'],row['first']-row['count']))
    groups=[]; words=d['animated_textures']
    pos=1
    for _ in range(words[0] if words else 0):
        count=words[pos]+1; pos+=1
        groups.append(words[pos:pos+count]); pos+=count
    changed=True
    while changed:
        changed=False
        for group in groups:
            if any(i in referenced for i in group):
                before=len(referenced); referenced.update(group)
                changed|=len(referenced)!=before
    equal('referenced native object domain',set(object_map),referenced)
    equal('referenced native sprite domain',set(sprite_map),sprites)
    active=[group for group in groups if any(i in referenced for i in group)]
    expected=[len(active)]
    for group in active:
        expected.append(len(group)); expected.extend(object_map[i] for i in group)
    equal('native animated texture ranges',texture.animations,expected)
    # Independent transcription of the fixed loader's named head-joint rules.
    rules={7:((2,8),),8:((13,8),),10:((7,8),),11:((7,8),),12:((19,8),),
        13:((19,8),),14:((19,8),),15:((13,8),),16:((1,8),),17:((1,8),),
        18:((10,8),(11,8)),19:((21,8),),20:((0,8),(2,8)),21:((0,8),(2,8)),
        22:((0,8),(2,8)),23:((10,12),),24:((2,8),),27:((6,8),),28:((6,8),),
        30:((0,8),),31:((0,8),),32:((0,8),),33:((2,20),),34:((1,8),)}
    flags=[d['bones'][i]&0xffffffff for i in range(0,len(d['bones']),4)]
    for model in d['models']:
        for joint,mask in rules.get(model['type'],()):
            flags[model['bone_offset']//4+joint]|=mask
    equal('cooked node flags',v['node_flags'],flags)
    return v
