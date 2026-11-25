# SPDX-License-Identifier: MIT
"""Native consumer invariants and shared admission bounds.

Rules: OpenLara 2333adef fixed/fmt/phd.h:15-210,1275-1387,
fixed/common.h and fixed/room.h:1120-1166. No reader code copied.
"""
from sizing import pools

# The C side gets TR64_BYTES_io from the same selected table via tr64/mem.h.
IO_BYTES = pools()['io']
CATALOG = ('title','gym','level1','level2','level3a','level3b','cut1','level4',
           'level5','level6','level7a','level7b','cut2','level8a','level8b',
           'level8c','level10a','cut3','level10b','cut4','level10c')
# (TR1 engine bound, TR2 bound). UINT32_MAX means the resident arena
# and the table's stride bound the count. TR2 items stay at that arena
# bound: the shared loader compiles TR64_LIMIT_items_2 into the first cart.
LIMITS = dict(
    rooms=(139,255), items=(256,0xffffffff), models=(191,65536),
    mesh_pointers=(512,0xffffffff), static_meshes=(50,65536), cameras=(16,0xffffffff),
    boxes=(1024,1<<14), animations=(65535,65536), sprite_sequences=(65535,0xffffffff),
    sound_sources=(65535,0xffffffff), cinematic_frames=(65535,65535),
    type_ids=(191,1<<16), static_ids=(50,1<<16), model_meshes=(127,65535),
    room_grid=(255,65535), room_subtables=(255,65535), room_sprites=(65535,65535),
    zone_arrays=(6,10), sound_map=(256,370), states=(255,65535), state_ranges=(255,65535),
    object_descriptors=(1536,65535), sprite_descriptors=(180,65535),
    descriptor_ids=(65535,65535), clusters=(65535,65536),
    palettes=(65535,65536), split_lines=(65535,65535),
    sound_samples=(256,256), sound_missing=(256,0), sound_blocks=(1024,1024),
    sound_metadata=(4096,64+256*32), sound_emitters=(64,0xffffffff),
)
CORE_COUNTS = ('rooms','items','models','mesh_pointers','static_meshes','cameras','boxes',
               'animations','sprite_sequences','sound_sources','cinematic_frames')

# PC sequel floor records: docs/formats/src-tr2-tr3.md and the wall
# triggers specification. Shared by source decoding and native admission.
TR2_FLOOR = dict(kind_mask=255, max_function=6, max_trigger=9, max_action=11,
                 controller_mask=(1 << 2) | (1 << 3) | (1 << 4),
                 payload_mask=(1 << 1) | (1 << 2) | (1 << 3),
                 action_kind_mask=15, fixed_action=1, target_action=6,
                 extra_mask=1 << 1, item_mask=(1 << 0) | (1 << 6),
                 camera_mask=(1 << 1) | (1 << 2),
                 flip_mask=(1 << 3) | (1 << 4) | (1 << 5), flip_count=10)


def limit(name, game):
    return LIMITS[name][game in (2, 3)]


ARGS = (0,3,2,0,0,2,2)
EFFECTS = frozenset((5,6,7,8,9,11,15,16))


def fail(table, reason):
    raise ValueError(f'{table}: {reason}')


def span(table, first, count, total):
    if first<0 or count<0 or first>total or count>total-first:
        fail(table,'referenced offset/count out of range')

def admit(game, **counts):
    for name, count in counts.items():
        bound=limit(name,game)
        if count>bound:fail(name,f'count/size {count} exceeds limit {bound}')



def validate(level):
    d=level.data
    game=level.reserved.get('game',1)
    if game not in (1,2,3):
        fail('core',f'unsupported game version {game}')
    sequel=game in (2,3)
    tr3=game==3
    findings=level.reserved.get('findings',())
    tr3_bad_ranges={f['reason'] for f in findings if f.get('section')=='ranges'} if tr3 else set()
    tr3_bad_boxes={f['reason'] for f in findings if f.get('section')=='boxes'} if tr3 else set()
    cap=lambda name: limit(name,game)
    rooms=level.rooms
    for name in CORE_COUNTS:
        if len(rooms if name=='rooms' else d[name])>cap(name):
            fail(name,'consumer count limit')
    for i,room in enumerate(rooms):
        if not 1<=room.x_sectors<=cap('room_grid') or not 1<=room.z_sectors<=cap('room_grid'):
            fail('rooms','sector grid dimensions')
        if len(room.sectors)!=room.x_sectors*room.z_sectors:
            fail('rooms','sector grid count')
        if max(len(room.portals),len(room.lights),len(room.static_instances))>cap('room_subtables'):
            fail('rooms','consumer subtable count limit')
        if len(room.sprites)>cap('room_sprites'):fail('room_sprites','consumer count limit')
        for sprite in room.sprites:
            span('room_sprites',sprite['vertex'],1,len(room.vertices))
            span('room_sprites',sprite['texture'],1,len(level.sprite_textures))
            light=room.vertices[sprite['vertex']]['lighting']
            allowed=(('tr3_rgb555',) if tr3 else
                     (('tr2_darkness_13bit',) if sequel else ('tr1_darkness_13bit','psx_vertex_8bit')))
            if light['encoding'] not in allowed:
                fail('room_sprites','unknown vertex-light encoding')
        alt=room.alternate_room
        if alt!=-1:
            if not 0<=alt<len(rooms) or alt==i:
                fail('rooms','alternate room topology')
            peer=rooms[alt]
            if (peer.alternate_room!=-1 or peer.info['x']!=room.info['x'] or
                    peer.info['z']!=room.info['z'] or peer.x_sectors!=room.x_sectors or
                    peer.z_sectors!=room.z_sectors or any(r.alternate_room==alt for r in rooms[:i])):
                fail('rooms','alternate room topology')
        for portal in room.portals:
            span('portals',portal['room'],1,len(rooms))
        for sector in room.sectors:
            if sector['floor_index']:
                span('floor_data',sector['floor_index'],1,len(d['floor_data']))
            if sector['box_index']!=65535:
                span('sectors',sector['box_index'],1,len(d['boxes']))
            for key in ('room_above','room_below'):
                if sector[key]!=255:span('sectors',sector[key],1,len(rooms))
    if len(d['zones'])!=6*len(d['boxes']) or (sequel and len(d['extra_ground_zones'])!=(cap('zone_arrays')-6)*len(d['boxes'])):
        fail('zones','zone array count differs from game version')
    if len(d['bones'])%4:fail('bones','partial node')
    anims=d['animations']
    def next_frame(index,frame):
        if not 0<=index<len(anims):return False
        target=anims[index]
        return (target['frame_start']<=frame<=target['frame_end'] or
                (frame>target['frame_end'] and target['next_animation']==index and
                 target['frame_start']<=target['next_frame']<=target['frame_end']))
    for a in anims:
        if a['state']>cap('states'):fail('animations','state id outside consumer domain')
        if not a['frame_rate'] or a['frame_end']<a['frame_start'] or a['frame_offset']%2:
            fail('animations','frame offset/count out of range')
        if sequel and not a['frame_size']:fail('animations','empty packed frame size')
        words=((a['frame_end']-a['frame_start'])//a['frame_rate']+1)*a['frame_size'] if a['frame_size'] else 10
        span('frames',a['frame_offset']//2,words,len(d['frames']))
        if not (0<=a['next_animation']<len(anims) if sequel else next_frame(a['next_animation'],a['next_frame'])):
            fail('animations','next animation/frame out of range')
        span('states',a['state_offset'],a['state_count'],len(d['states']))
    for state in d['states']:
        if state['state']>cap('states') or state['range_count']>cap('state_ranges'):
            fail('states','state/range count outside consumer domain')
        span('ranges',state['range_offset'],state['range_count'],len(d['ranges']))
    for i,r in enumerate(d['ranges']):
        low,high,target,frame=(r[k]&65535 for k in ('low','high','next_animation','next_frame'))
        terminal=False
        if not sequel and anims and i+1==len(d['ranges']):
            a=anims[-1]
            terminal=(low==0 and high==1 and target==len(anims) and frame==1 and
                a['frame_start']==a['frame_end']==0 and a['next_animation']==len(anims)-1 and
                a['next_frame']==0 and a['state_count']==1 and a['state_offset']<len(d['states']))
            if terminal:
                s=d['states'][a['state_offset']]
                terminal=s['state']==1 and s['range_count']==1 and s['range_offset']==i
        bad_tr3_range=(tr3 and target>=len(anims) and
            f'dispatch {i} targets animation {target} outside {len(anims)} entries' in tr3_bad_ranges)
        if low>high or (not bad_tr3_range and (target>=len(anims) if sequel else (not terminal and not next_frame(target,frame)))):
            fail('ranges','target animation/frame out of range')
    commands=d['commands']
    if 2*(len(commands)+1)>IO_BYTES:fail('commands','validation scratch exceeds io pool')
    remaining=[0]*(len(commands)+1)
    for pos in range(len(commands)-1,-1,-1):
        opcode=commands[pos]&65535
        if opcode<len(ARGS) and pos+1+ARGS[opcode]<=len(commands):
            remaining[pos]=1+remaining[pos+1+ARGS[opcode]]
    for a in anims:
        first,count=a['command_offset'],a['command_count']
        span('commands',first,0,len(commands))
        if count>remaining[first]:fail('commands','unknown opcode or truncated command stream')
    types=set()
    for model in d['models']:
        if not 0<=model['type']<cap('type_ids') or model['type'] in types or model['mesh_count']>cap('model_meshes'):
            fail('models','type/mesh count outside consumer domain')
        types.add(model['type'])
        span('mesh_pointers',model['mesh_start'],model['mesh_count'],len(d['mesh_pointers']))
        if model['bone_offset']%4:fail('models','unaligned bone offset')
        span('bones',model['bone_offset']//4,max(model['mesh_count']-1,0),len(d['bones'])//4)
        if model['animation']!=65535:span('models',model['animation'],1,len(anims))
        if model['frame_offset']%2:fail('models','unaligned frame offset')
        span('frames',model['frame_offset']//2,0,len(d['frames']))
    ids=set()
    for mesh in d['static_meshes']:
        if not 0<=mesh['id']<cap('static_ids') or mesh['id'] in ids:
            fail('static_meshes','static id outside consumer domain')
        ids.add(mesh['id'])
        span('static_meshes',mesh['mesh'],1,len(d['mesh_pointers']))
    for room in rooms:
        for instance in room.static_instances:
            if instance['mesh'] not in ids:fail('static_instances','undefined static mesh id')
    for item in d['items']:
        if not 0<=item['type']<cap('type_ids'):fail('items','type outside consumer domain')
        span('items',item['room'],1,len(rooms))
    if not sequel:
        for camera in d['cameras']:span('cameras',camera['room_or_speed'],1,len(rooms))
    for sequence in d['sprite_sequences']:
        if (sequence['type']>65535 or sequence['count']>=0 or
                (not sequel and sequence['type']<cap('type_ids') and sequence['count']<-128)):
            fail('sprite_sequences','type/count outside consumer domain')
        span('sprite_sequences',sequence['first'],-sequence['count'],len(level.sprite_textures))
    overlaps=d['overlaps']
    if (len(overlaps)+7)//8>IO_BYTES:fail('overlaps','validation scratch exceeds io pool')
    valid=[False]*(len(overlaps)+1)
    for pos in range(len(overlaps)-1,-1,-1):
        word=overlaps[pos]
        valid[pos]=(word&0x3fff)<len(d['boxes']) and (bool(word&0x8000) or valid[pos+1])
    for bi,box in enumerate(d['boxes']):
        for axis in ('z','x'):
            low,high=box['min_'+axis],box['max_'+axis]
            if low<0 or high<low or high>=255*1024:
                fail('boxes','grid coordinates outside byte domain')
        if box['overlap']!=65535:
            head=box['overlap']&0x3fff
            if head>=len(overlaps) or not valid[head]:
                if not (tr3 and box['overlap']==2047 and
                        f'box {bi} overlap head 0x07ff does not resolve to a valid chain' in tr3_bad_boxes):
                    fail('overlaps','invalid box chain or missing end bit')
    if not sequel:
        validate_floor_tr1(d,rooms)
    animated=d['animated_textures']
    if animated:
        pos=1
        for _ in range(animated[0]):
            if pos>=len(animated):fail('animated_textures','range count out of bounds')
            count=animated[pos]+1; pos+=1
            span('animated_textures',pos,count,len(animated))
            for index in animated[pos:pos+count]:span('animated_textures',index,1,len(level.object_textures))
            pos+=count
    if len(d['sound_map'])!=cap('sound_map'):fail('sound_map','count differs from game version')
    for index in d['sound_map']:
        if index!=-1:span('sound_map',index,1,len(d['sound_details']))
    if d['sample_offsets'] is not None:
        for detail in d['sound_details']:
            span('sound_details',detail['sample'],(detail['flags']>>2)&15,len(d['sample_offsets']))
    if sequel and level.sample_encoding=='pc_wave':
        for detail in d['sound_details']:
            span('sound_details',detail['sample'],max(1,(detail['flags']>>2)&15),len(level.samples))
    for source in d['sound_sources']:
        span('sound_sources',source['sound'],1,cap('sound_map'))
    return dict(sample_offsets='checked' if d['sample_offsets'] is not None else 'absent-unvalidated')


def validate_floor_tr1(d,rooms):
    floor=d['floor_data']
    if 2*len(floor)+(len(floor)+7)//8>IO_BYTES:fail('floor_data','validation scratch exceeds io pool')
    floor_seen=set(); action_ends={}
    def take(pos):
        if pos>=len(floor):fail('floor_data','unterminated/truncated stream')
        return floor[pos],pos+1
    def actions(pos):
        path=[]
        while pos not in action_ends:
            path.append(pos)
            action,pos=take(pos); kind=(action>>10)&31; argument=action&1023
            if kind in (0,6):span('floor_data',argument,1,len(d['items']))
            if kind in (1,2):span('floor_data',argument,1,len(d['cameras']))
            if kind in (3,4,5) and argument>=10:fail('floor_data','flip-map index out of range')
            if kind==8 and argument>=64:fail('floor_data','soundtrack state index out of range')
            if kind==9 and argument not in EFFECTS:fail('floor_data','unadmitted effect')
            if kind==10 and argument>=8:fail('floor_data','secret bit out of range')
            if kind in (1,12):
                extra,pos=take(pos); action=(action&32767)|(extra&32768)
            elif kind>13:fail('floor_data','unknown trigger action')
            if action&32768:break
        else:pos=action_ends[pos]
        for start in path:action_ends[start]=pos
        return pos
    for room in rooms:
        for sector in room.sectors:
            pos=sector['floor_index']
            if not pos:continue
            while pos not in floor_seen:
                floor_seen.add(pos)
                word,pos=take(pos); kind=word&31
                if kind in (1,2,3):
                    argument,pos=take(pos)
                    if kind==1:span('floor_data',argument,1,len(rooms))
                elif kind==4:
                    trigger=(word>>8)&127
                    if trigger>8:fail('floor_data','unknown trigger type')
                    _,pos=take(pos)
                    if trigger in (2,3,4):
                        item,pos=take(pos); span('floor_data',item&1023,1,len(d['items']))
                    pos=actions(pos)
                elif kind not in (0,5):fail('floor_data','unknown floor command')
                if word&32768:break
