# SPDX-License-Identifier: MIT
"""Hand-authored synthetic TR1 fixtures and source-format encoder, no game data.

The encoder is test-only. It deliberately does not share the readers' cursor,
field schemas, counts or section accounting. It can encode the reader's IR
back to bytes so tests cover every synthetic section in a byte round trip.
"""
import hashlib
import struct
from ir import Level,Room,Mesh,Face,Texture


def pack(fmt,*values):
    return struct.pack('<'+fmt,*values)


def count_bytes(data,stride=1,width='I'):
    assert len(data)%stride==0
    return pack(width,len(data)//stride)+data


def words(values,fmt):
    return b''.join(pack(fmt,v) for v in values)


def fixture(source):
    psx = source=='psx'
    l = Level(source,'synthetic','',0)
    def light(v,vertex=False):
        return dict(value=v,encoding='psx_vertex_8bit' if psx and vertex else 'tr1_darkness_13bit')
    vertices = [dict(x=x,y=y,z=z,lighting=light(100+i,True))
                for i,(x,y,z) in enumerate(((0,0,0),(128,0,0),(128,128,0),(0,128,0)))]
    quad = Face((0,1,2,3),256,None,256,(0,1,3,2) if psx else (0,1,2,3))
    tri = Face((0,2,3),257,None,257)
    portal = dict(zip(('room','normal_x','normal_y','normal_z','x0','y0','z0','x1','y1','z1',
                      'x2','y2','z2','x3','y3','z3'),(0,0,0,1,0,0,0,128,0,0,128,128,0,0,128,0)))
    sector = dict(floor_index=1,box_index=0,room_below=255,floor=-4,room_above=255,ceiling=-8)
    room = Room(dict(x=1024,z=2048,y_bottom=4096,y_top=-4096),vertices,[quad],[tri],
        [dict(vertex=1,texture=0)],[portal],1,1,[sector],light(200),
        [dict(x=10,y=20,z=30,intensity=light(300),radius=4096,reserved=b'\x34\x12' if psx else None)],
        [dict(x=40,y=50,z=60,angle=16384,intensity=400,mesh=7,reserved=b'\x78\x56' if psx else None)],
        -1,1,dict(geometry_prefix=pack('H',4),geometry_padding=b'') if psx else dict(geometry_padding=b''))
    l.rooms = [room]
    mesh0 = Mesh((1,2,3),500,0,[(0,0,0),(64,0,0),(64,64,0),(0,64,0)],
                 [(0,0,16384)]*4,None,[quad,tri],0,
                 dict(padding=b'\0\0' if not psx else b'',vertex_w=[9]*4 if psx else None,
                      normal_w=[10]*4 if psx else None))
    mesh1 = Mesh((4,5,6),600,0,[(0,0,0),(32,0,0),(0,32,0)],None,[100,200,300],
                 [Face((0,1,2),None,17,17)],0,
                 dict(padding=b'',vertex_w=[11]*3 if psx else None,normal_w=None))
    l.meshes = {0:mesh0,1:mesh1}  # Encoder computes true byte offsets.
    l.object_textures = [Texture(0,0 if psx else None,((2,2),(9,2),(2,9),(9,9)) if psx else ((2,2),(9,2),(9,9),(2,9)),1,None,
        dict(word=0x1234,tile_flags=0) if psx else dict(coordinate_adjustments=((0,0),)*4,tile_flags=0)) for _ in range(257)]
    l.object_textures.append(Texture(0,0 if psx else None,((2,2),(9,2),(9,9),(0,0)),1,None,
        dict(word=0x1234,tile_flags=0) if psx else dict(coordinate_adjustments=((0,0),)*4,tile_flags=0)))
    l.sprite_textures = [Texture(0,0 if psx else None,((12,12),(19,19)),None,(-8,-9,10,11),
        dict(tile_flags=0) if psx else dict(width_fixed=7<<8,height_fixed=7<<8,tile_flags=0))]
    l.images = [bytes((i%16 for i in range(65536)))] if not psx else [bytes([0x21])*32768 for _ in range(13)]
    l.palette = None if psx else bytes(i%64 for i in range(768))
    l.cluts = [words(range(16),'H')]*1024 if psx else None
    l.light_map = None if psx else bytes(i%256 for i in range(8192))
    l.data = dict(floor_data=[0,0x8005],mesh_pointers=[],
        animations=[dict(frame_offset=0,frame_rate=1,frame_size=12,state=3,speed=-65536,
            acceleration=32768,frame_start=0,frame_end=0,next_animation=0,next_frame=0,
            state_count=1,state_offset=0,command_count=1,command_offset=0)],
        states=[dict(state=3,range_count=1,range_offset=0)],
        ranges=[dict(low=0,high=1,next_animation=0,next_frame=0)],
        commands=[1,-2,3],bones=[1,-10,20,30],frames=[0,128,0,128,0,128,10,20,30,1,0x1234,0x5678],
        models=[dict(type=0,mesh_count=2,mesh_start=0,bone_offset=0,frame_offset=0,animation=0,
                     reserved=b'\xcd\xab' if psx else None)],
        static_meshes=[dict(id=7,mesh=0,visible_min_x=-1,visible_max_x=1,visible_min_y=-2,visible_max_y=2,
            visible_min_z=-3,visible_max_z=3,collision_min_x=-4,collision_max_x=4,collision_min_y=-5,
            collision_max_y=5,collision_min_z=-6,collision_max_z=6,flags=3)],
        sprite_sequences=[dict(type=2,count=-1,first=0)],
        cameras=[dict(x=-10,y=20,z=-30,room_or_speed=0,flags=0x100)],
        sound_sources=[dict(x=40,y=-50,z=60,sound=3,flags=1)],
        boxes=[dict(min_z=0,max_z=1024,min_x=0,max_x=1024,floor=-1024,overlap=0)],
        overlaps=[0x8000],zones=[-1,0,1,2,3,4],animated_textures=[1,0,256],
        items=[dict(type=0,room=0,x=1,y=-2,z=3,angle=32768,intensity=-1,flags=0x100)],
        cinematic_frames=[dict(target_x=1,target_y=2,target_z=3,camera_x=4,camera_y=5,camera_z=6,fov=7,roll=8)],
        demo_data=None if psx else b'\x01\x02\x03',sound_map=[-1]*255+[0],
        sound_details=[dict(sample=0,volume=32767,chance=65535,flags=4)],
        sample_offsets=None if psx else [0],sample_data=None)
    l.samples = [bytes(range(16))] if psx else [b'RIFF\x08\x00\x00\x00WAVEtest']
    l.sample_encoding = 'psx_spu_adpcm' if psx else 'pc_wave'
    l.reserved = dict(level_word=0x12345678,cinematic_word=0xabcdef01) if psx else dict(level_word=0x12345678)
    return l


def encode_mesh(m,psx):
    n = len(m.vertices)
    raw = pack('3hHHh',*m.center,m.radius,m.flags,n if m.normals is not None or not psx else -n)
    for i,v in enumerate(m.vertices):
        raw += pack('3h',*v)+(pack('h',m.reserved['vertex_w'][i]) if psx else b'')
    if not psx:
        raw += pack('h',n if m.normals is not None else -n)
    if m.normals is not None:
        for i,v in enumerate(m.normals):
            raw += pack('3h',*v)+(pack('h',m.reserved['normal_w'][i]) if psx else b'')
    else:
        raw += words(m.lighting,'h')
    groups = [(4,False),(3,False)] if psx else [(4,False),(3,False),(4,True),(3,True)]
    for corners,colored in groups:
        fs = [f for f in m.faces if len(f.vertices)==corners and (psx or (f.color is not None)==colored)]
        raw += pack('H',len(fs))
        for f in fs:
            v = f.vertices
            raw += pack(f'{corners+1}H',*v,f.flags)
    return raw+m.reserved['padding']


def encode(l, cutscene=False):
    psx = l.source=='psx'
    if psx:
        if cutscene:
            prefix = bytes(8)
        else:
            # 4-byte VH-size word + 32-byte VAB header + 128 programs + 16
            # tones for one program + 256 sample-size words + sample data.
            audio = b''.join(l.samples)
            bank_size = 2592+512+len(audio)
            prefix = pack('I4sIII4H4BI',2592+512,b'pBAV',6,0,bank_size,
                0xeeee,1,1,len(l.samples),127,64,0,0,0xffffffff)
            prefix += bytes(2048+512)+words([0]+[len(s)//8 for s in l.samples]+[0]*(255-len(l.samples)),'H')
            prefix += pack('I',len(audio))+audio
        raw = prefix+b''.join(l.images)+b''.join(l.cluts)
    else:
        raw = pack('I',32)+count_bytes(b''.join(l.images),65536)
    raw += pack('IH',l.reserved['level_word'],len(l.rooms))
    for r in l.rooms:
        raw += pack('4i',*(r.info[k] for k in ('x','z','y_bottom','y_top')))
        geom = r.reserved['geometry_prefix'] if psx else b''
        geom += pack('H',len(r.vertices))
        geom += b''.join(pack('3hH',v['x'],v['y'],v['z'],v['lighting']['value']) for v in r.vertices)
        for fs,corners in ((r.quads,4),(r.triangles,3)):
            geom += pack('H',len(fs))
            for f in fs:
                v = f.vertices
                if psx and corners==4:
                    v = (v[0],v[1],v[3],v[2])
                geom += pack(f'{corners+1}H',*v,f.flags)
        geom += count_bytes(b''.join(pack('2H',s['vertex'],s['texture']) for s in r.sprites),4,'H')
        geom += r.reserved['geometry_padding']
        raw += count_bytes(geom,2)
        raw += count_bytes(b''.join(pack('H15h',*(p[k] for k in ('room','normal_x','normal_y','normal_z','x0','y0','z0','x1','y1','z1','x2','y2','z2','x3','y3','z3'))) for p in r.portals),32,'H')
        raw += pack('2H',r.z_sectors,r.x_sectors)
        raw += b''.join(pack('HHBbBb',*(s[k] for k in ('floor_index','box_index','room_below','floor','room_above','ceiling'))) for s in r.sectors)
        raw += pack('2H',r.ambient['value'],len(r.lights))
        for light in r.lights:
            raw += pack('3iH',light['x'],light['y'],light['z'],light['intensity']['value'])
            raw += (light['reserved'] if psx else b'')+pack('I',light['radius'])
        raw += pack('H',len(r.static_instances))
        for s in r.static_instances:
            raw += pack('3i3H',*(s[k] for k in ('x','y','z','angle','intensity','mesh')))+(s['reserved'] if psx else b'')
        raw += pack('hH',r.alternate_room,r.flags)
    raw += count_bytes(words(l.data['floor_data'],'H'),2)
    mesh_blobs = [encode_mesh(m,psx) for m in l.meshes.values()]
    pointers,offset = [],0
    for blob in mesh_blobs:
        pointers.append(offset)
        offset += len(blob)
    raw += count_bytes(b''.join(mesh_blobs),2)+count_bytes(words(pointers,'I'),4)
    specs = [('animations','IBBHii8H',('frame_offset','frame_rate','frame_size','state','speed','acceleration','frame_start','frame_end','next_animation','next_frame','state_count','state_offset','command_count','command_offset')),
        ('states','3H',('state','range_count','range_offset')),
        ('ranges','4h',('low','high','next_animation','next_frame'))]
    for name,fmt,keys in specs:
        raw += count_bytes(b''.join(pack(fmt,*(r[k] for k in keys)) for r in l.data[name]),struct.calcsize('<'+fmt))
    for name,fmt in [('commands','h'),('bones','i'),('frames','H')]:
        raw += count_bytes(words(l.data[name],fmt),struct.calcsize('<'+fmt))
    raw += pack('I',len(l.data['models']))
    for m in l.data['models']:
        raw += pack('IHHIIH',*(m[k] for k in ('type','mesh_count','mesh_start','bone_offset','frame_offset','animation')))+(m['reserved'] if psx else b'')
    raw += count_bytes(b''.join(pack('IH12hH',*(s[k] for k in ('id','mesh','visible_min_x','visible_max_x','visible_min_y','visible_max_y','visible_min_z','visible_max_z','collision_min_x','collision_max_x','collision_min_y','collision_max_y','collision_min_z','collision_max_z','flags'))) for s in l.data['static_meshes']),32)
    raw += pack('I',len(l.object_textures))
    for t in l.object_textures:
        tile = t.page|t.reserved['tile_flags']
        if psx:
            raw += pack('BBHBBHBBHBBH',*t.uv[0],t.palette,*t.uv[1],tile,*t.uv[2],t.reserved['word'],*t.uv[3],t.attribute)
        else:
            raw += pack('2H',t.attribute,tile)
            raw += b''.join(pack('4B',a[0],uv[0],a[1],uv[1]) for uv,a in zip(t.uv,t.reserved['coordinate_adjustments']))
    raw += pack('I',len(l.sprite_textures))
    for t in l.sprite_textures:
        if psx:
            raw += pack('4hHH4B',*t.bounds,t.palette,t.page,*t.uv[0],*t.uv[1])
        else:
            raw += pack('HBBHH4h',t.page,*t.uv[0],t.reserved['width_fixed'],t.reserved['height_fixed'],*t.bounds)
    for name,fmt,keys in [('sprite_sequences','IhH',('type','count','first')),
        ('cameras','3ihH',('x','y','z','room_or_speed','flags')),
        ('sound_sources','3iHH',('x','y','z','sound','flags')),
        ('boxes','4ihH',('min_z','max_z','min_x','max_x','floor','overlap'))]:
        raw += count_bytes(b''.join(pack(fmt,*(s[k] for k in keys)) for s in l.data[name]),struct.calcsize('<'+fmt))
    raw += count_bytes(words(l.data['overlaps'],'H'),2)+words(l.data['zones'],'h')
    raw += count_bytes(words(l.data['animated_textures'],'H'),2)
    raw += count_bytes(b''.join(pack('HH3iHhH',*(s[k] for k in ('type','room','x','y','z','angle','intensity','flags'))) for s in l.data['items']),22)
    cine = count_bytes(b''.join(pack('8h',*(s[k] for k in ('target_x','target_y','target_z','camera_x','camera_y','camera_z','fov','roll'))) for s in l.data['cinematic_frames']),16,'H')
    if not psx:
        raw += l.light_map+l.palette+cine+count_bytes(l.data['demo_data'],1,'H')
    raw += words(l.data['sound_map'],'h')
    raw += count_bytes(b''.join(pack('4H',*(s[k] for k in ('sample','volume','chance','flags'))) for s in l.data['sound_details']),8)
    if psx:
        raw += pack('I',l.reserved['cinematic_word'])+cine
    else:
        raw += count_bytes(b''.join(l.samples))+count_bytes(words([0],'I'),4)
    return raw
