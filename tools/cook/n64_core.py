# SPDX-License-Identifier: MIT
"""Level IR -> native immutable core plus real TX64 textures."""
import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys
import time
import zlib
sys.dont_write_bytecode=True
from ir import SCHEMAS
import n64_pack as pack
import n64_tex
import n64_validate
import src_pc
import src_psx
from pool_rows import pool_table

ROOT=Path(__file__).resolve().parents[2]
PREFIX=struct.Struct('>4s7I')
CORE_ABI=3
CATALOG=('title','gym','level1','level2','level3a','level3b','cut1','level4',
         'level5','level6','level7a','level7b','cut2','level8a','level8b','level8c',
         'level10a','cut3','level10b','cut4','level10c')


@dataclass(frozen=True)
class Table:
    name:str
    fmt:str
    fields:tuple=()


ROOM_FIELDS=('x','z','y_bottom','y_top','bounds_bottom','bounds_top','z_sectors',
    'x_sectors','alternate_room','flags','ambient','reserved','sector_start',
    'sector_count','portal_start','portal_count','light_start','light_count',
    'instance_start','instance_count','sprite_start','sprite_count')
TR2_ROOM_FIELDS=('ambient2','light_mode')
TABLES=[
    Table('rooms','6i2Hh3H10I2H',ROOM_FIELDS+TR2_ROOM_FIELDS),
    Table('sectors',*SCHEMAS['sectors']),Table('portals',*SCHEMAS['portals']),
    Table('lights','3iHHII',('x','y','z','intensity','reserved','radius','reserved_present')),
    Table('static_instances','3ih3HI',('x','y','z','angle','intensity','mesh','reserved','reserved_present')),
    Table('floor_data','H'),Table('mesh_pointers','I'),Table('animations',*SCHEMAS['animations']),
    Table('states',*SCHEMAS['states']),Table('ranges','4H',SCHEMAS['ranges'][1]),
    Table('commands','h'),Table('bones','i'),
    Table('models','IHHIIHHI',SCHEMAS['models'][1]+('reserved','reserved_present')),
    Table('frames','H'),Table('static_meshes',*SCHEMAS['static_meshes']),
    Table('sprite_sequences',*SCHEMAS['sprite_sequences']),Table('cameras',*SCHEMAS['cameras']),
    Table('sound_sources',*SCHEMAS['sound_sources']),Table('boxes',*SCHEMAS['boxes']),
    Table('overlaps','H'),Table('zones','H'),Table('items','HH3ihhHh',SCHEMAS['items'][1]+('intensity2',)),
    Table('cinematic_frames',*SCHEMAS['cinematic_frames']),Table('demo_data','B'),
    Table('sound_map','h'),Table('sound_details',*SCHEMAS['sound_details']),Table('node_flags','I'),
    Table('room_sprites','3h3H',('x','y','z','gain','descriptor','reserved')),
    Table('mesh_spheres','3hH',('x','y','z','radius'))]
KINDS={t.name:i+1 for i,t in enumerate(TABLES)}
# Fixed fmt/phd.h:1389-1425 and common.h ITEM_TYPES order; layout facts only.
HEAD_MASKS={7:((2,8),),8:((13,8),),10:((7,8),),11:((7,8),),
    12:((19,8),),13:((19,8),),14:((19,8),),15:((13,8),),16:((1,8),),17:((1,8),),
    18:((10,8),(11,8)),19:((21,8),),20:((0,8),(2,8)),21:((0,8),(2,8)),
    22:((0,8),(2,8)),23:((10,12),),24:((2,8),),27:((6,8),),28:((6,8),),
    30:((0,8),),31:((0,8),),32:((0,8),),33:((2,20),),34:((1,8),)}


def signed16(n):
    return (n&32767)-(n&32768)


def tr3_rgb555_luma(value):
    r5,g5,b5=(value>>10)&31,(value>>5)&31,value&31
    return (r5*77+g5*150+b5*29+128)>>8


def optional_word(row):
    row=dict(row); value=row['reserved']
    row['reserved_present']=int(value is not None)
    row['reserved']=int.from_bytes(value,'big') if value is not None else 0
    return row


def native_values(level,texture_manifest):
    game=level.reserved.get('game',1)
    tr2=game==2; tr3=game==3; sequel=game in (2,3)
    n64_validate.validate(level)
    out={t.name:level.data.get(t.name) for t in TABLES}
    for name in ('rooms','sectors','portals','lights','static_instances','room_sprites'):out[name]=[]
    for room in level.rooms:
        header=dict(room.info,z_sectors=room.z_sectors,x_sectors=room.x_sectors,
            alternate_room=room.alternate_room,flags=room.flags,ambient=room.ambient['value'],
            reserved=(room.reserved['water_scheme']|(room.reserved['reverb']<<8)) if tr3 else 0)
        if tr2:
            header.update((key, room.reserved[key]) for key in TR2_ROOM_FIELDS)
        elif tr3:
            header.update(ambient2=room.reserved['ambient2'], light_mode=room.reserved['filter'])
        else:
            header.update(ambient2=0, light_mode=0)
        ys=[v['y'] for v in room.vertices]
        header['bounds_bottom']=max(ys) if ys else room.info['y_bottom']
        header['bounds_top']=min(ys) if ys else room.info['y_top']
        for name,short in (('sectors','sector'),('portals','portal'),('lights','light'),('static_instances','instance')):
            values=getattr(room,name)
            header[short+'_start']=len(out[name]); header[short+'_count']=len(values)
            for value in values:
                if tr3 and name=='lights':
                    r,g,b=value['rgb']; sop=value['sun_or_point']
                    value=dict(x=value['x'],y=value['y'],z=value['z'],
                               intensity=(r<<8)|g,reserved=(b<<8)|value['kind'],
                               radius=int.from_bytes(sop[:4],'little'),
                               reserved_present=int.from_bytes(sop[4:8],'little'))
                elif tr3 and name=='static_instances':
                    raw_rgb=value['intensity']
                    d=max(0,min(8191,8191-tr3_rgb555_luma(raw_rgb)*256))
                    value=dict(x=value['x'],y=value['y'],z=value['z'],
                               angle=signed16(value['angle']),intensity=d,
                               mesh=value['mesh'],reserved=raw_rgb,
                               reserved_present=value['intensity2'])
                elif name in ('lights','static_instances'):
                    value=optional_word(value)
                    intensity=value['intensity']
                    value['intensity']=intensity['value'] if isinstance(intensity,dict) else intensity
                    if name=='static_instances':value['angle']=signed16(value['angle'])
                    if tr2:
                        value['reserved'] = value['intensity2']
                        if name == 'lights':
                            value['reserved_present'] = value['radius2']
                out[name].append(value)
        header['sprite_start']=len(out['room_sprites']); header['sprite_count']=len(room.sprites)
        for sprite in room.sprites:
            vertex=room.vertices[sprite['vertex']]; light=vertex['lighting']
            if light['encoding']=='psx_vertex_8bit':gain=light['value']*32
            elif light['encoding']=='tr3_rgb555':gain=tr3_rgb555_luma(light['value'])*256
            else:gain=8191-light['value']
            try:descriptor=texture_manifest['sprite_descriptor_map'][str(sprite['texture'])]
            except KeyError as error:raise ValueError('room_sprites: native descriptor missing') from error
            out['room_sprites'].append(dict(x=vertex['x'],y=vertex['y'],z=vertex['z'],
                gain=max(0,min(8192,gain)),descriptor=descriptor,reserved=0))
        out['rooms'].append(header)
    out['mesh_spheres']=[]
    for offset in level.data['mesh_pointers']:
        try:mesh=level.meshes[offset]
        except KeyError as error:raise ValueError(f'mesh_spheres: unknown mesh pointer {offset}') from error
        x,y,z=mesh.center
        out['mesh_spheres'].append(dict(x=x,y=y,z=z,radius=mesh.radius))
    out['models']=[optional_word(m) for m in level.data['models']]
    out['items']=[dict(row,angle=signed16(row['angle']),intensity2=row['intensity2'] if sequel else 0)
                  for row in level.data['items']]
    anim_count=len(level.data['animations'])
    out['ranges']=[]
    for row in level.data['ranges']:
        r={key:value&65535 for key,value in row.items()}
        if tr3 and r['next_animation']>=anim_count:
            # Sanitize documented retail TR3 unreachable out-of-range dispatch
            # (e.g. JUNGLE.TR2 ranges[416..417] on unused state 73) so native
            # validation stays strict and the entry can never fire at runtime.
            r=dict(low=65535,high=65535,next_animation=0,next_frame=0)
        out['ranges'].append(r)
    if tr3 and level.reserved.get('findings'):
        overlaps=level.data['overlaps']
        valid=[False]*(len(overlaps)+1)
        for pos in range(len(overlaps)-1,-1,-1):
            w=overlaps[pos]
            valid[pos]=(w&0x3fff)<len(out['boxes']) and (bool(w&0x8000) or valid[pos+1])
        out['boxes']=[dict(b,overlap=65535) if (b['overlap']==2047 and ((b['overlap']&0x3fff)>=len(overlaps) or not valid[b['overlap']&0x3fff])) else b
                      for b in out['boxes']]
    if tr3:
        out['sound_details']=[dict(sample=row['sample'],
                                   volume=row['volume']|(row['range']<<8),
                                   chance=row['chance']|(row['pitch']<<8),
                                   flags=row['flags'])
                              for row in level.data['sound_details']]
    out['zones']=[v&65535 for v in level.data['zones']]
    if sequel:
        n = len(out['boxes'])
        normal, extra = out['zones'], [v&65535 for v in level.data['extra_ground_zones']]
        out['zones'] = normal[:2*n] + extra[:2*n] + normal[2*n:5*n] + extra[2*n:] + normal[5*n:]
    out['sprite_sequences']=[]
    mapping=texture_manifest['sprite_descriptor_map']
    for row in level.data['sprite_sequences']:
        count=-row['count']
        if not count:raise ValueError('sprite_sequences: zero sequence count')
        try:first=mapping[str(row['first'])]
        except KeyError as error:raise ValueError('sprite_sequences: source descriptor missing') from error
        if any(mapping.get(str(row['first']+i))!=first+i for i in range(count)):
            raise ValueError('sprite_sequences: native descriptors are not contiguous')
        out['sprite_sequences'].append(dict(row,first=first))
    bones=level.data['bones']; flags=[bones[i]&0xffffffff for i in range(0,len(bones),4)]
    for model in (() if sequel else out['models']):
        for joint,mask in HEAD_MASKS.get(model['type'],()):
            index=model['bone_offset']//4+joint
            if index>=len(flags):raise ValueError('node_flags: head node out of range')
            flags[index]|=mask
    out['node_flags']=flags
    return out


def encode(level,texture_manifest):
    values=native_values(level,texture_manifest); result=[]
    for kind,table in enumerate(TABLES,1):
        rows=values[table.name]; present=rows is not None
        rows=[] if rows is None else rows
        record=struct.Struct('>'+table.fmt)
        if table.name=='demo_data':raw=bytes(rows)
        elif table.fields:raw=b''.join(record.pack(*(row[f] for f in table.fields)) for row in rows)
        else:raw=b''.join(record.pack(v) for v in rows)
        header=PREFIX.pack(b'TR6C',kind,len(rows),record.size,int(present),32,32+len(raw),CORE_ABI)
        bundle={'animations':512,'states':384,'ranges':512,'commands':512,'bones':512,'node_flags':512}.get(table.name,pack.BLOCK)
        result.append(pack.Section(kind,header+raw,header_bytes=32,chunk_bytes=bundle,content_defined=table.name=='frames'))
    return result


def mutable_bytes(level,texture_manifest):
    rows=level.data
    types=[r['type'] for name in ('models','sprite_sequences') for r in rows[name] if r['type']<191]
    sizes=dict(sector_full_copy_upper_bound=sum(len(r.sectors) for r in level.rooms)*8,
        room_bindings_and_item_heads=len(level.rooms)*16,box_overlap_flags=len(rows['boxes'])*2,
        box_baseline_bits=(len(rows['boxes'])+7)//8,camera_flags=len(rows['cameras'])*2,
        model_records_by_type=(max(types,default=-1)+1)*24,
        texture_animation_remap=len(texture_manifest['object_descriptor_map'])*2)
    total=sum(pack.align(n) for n in sizes.values())
    return dict(tables=sizes,total=total,budget=128*1024,remaining_for_items_ai_effects=128*1024-total)


def write_changed(path,data):
    path=Path(path); path.parent.mkdir(parents=True,exist_ok=True)
    if not path.exists() or path.read_bytes()!=data:
        temporary=path.with_name(path.name+f'.{os.getpid()}.tmp')
        temporary.write_bytes(data); temporary.replace(path)


def tool_hash():
    files=sorted(Path(__file__).parent.glob('*.py'))+[pool_table()]
    return hashlib.sha256(b''.join(p.name.encode()+p.read_bytes() for p in files)).hexdigest()


def texture(level):
    # Reuse only an exact texture-tool/source/version key. Cache is immutable.
    tool=n64_tex.tool_hash()
    key=hashlib.sha256(json.dumps([tool,level.sha256,1]).encode()).hexdigest()
    directory=Path('/tmp/tr64/content')/key
    binary=directory/'texture.bin'; metadata=directory/'texture.json'
    if binary.exists() and metadata.exists():
        blob=binary.read_bytes(); manifest=json.loads(metadata.read_text())
        if (manifest['content_key']!=key or manifest['source_sha256']!=level.sha256 or
                hashlib.sha256(blob).hexdigest()!=manifest['section_sha256']):
            raise ValueError('TX64 content cache corrupt')
    else:
        blob,manifest=n64_tex.cook(level)
        write_changed(binary,blob); write_changed(metadata,(json.dumps(manifest,sort_keys=True)+'\n').encode())
    return blob,manifest


def checksum(section):
    crc=0
    for _,raw in pack.parts(section):crc=zlib.crc32(struct.pack('>I',zlib.crc32(raw)),crc)
    return f'{crc:08x}'


def cook_sections(level):
    tx,manifest=texture(level)
    core=encode(level,manifest)
    sections=core+[pack.Section(0x10000,tx)]
    pack.level_layout(sections)
    return sections,manifest


def schema_header(path):
    text='/* SPDX-License-Identifier: MIT */\n/* Generated schema only, no game data. */\n#ifndef TR64_CORE_SCHEMA_H\n#define TR64_CORE_SCHEMA_H\n'
    for i,t in enumerate(TABLES,1):text+=f'#define TR64_CORE_{t.name} {i}u\n'
    for name, (tr1,tr2) in n64_validate.LIMITS.items():
        text+=f'#define TR64_LIMIT_{name}_1 {tr1}u\n#define TR64_LIMIT_{name}_2 {tr2}u\n'
    text+='#define TR64_LIMIT(name, tr2) ((tr2) ? TR64_LIMIT_##name##_2 : TR64_LIMIT_##name##_1)\n'
    for name, value in n64_validate.TR2_FLOOR.items():
        text += f'#define TR64_TR2_FLOOR_{name.upper()} {value}u\n'
    text+=f'#define TR64_CORE_ABI {CORE_ABI}u\n#define TR64_CORE_TABLES {len(TABLES)}u\n#define TR64_CORE_SCHEMA(X) \\\n'
    text+=' \\\n'.join(f' X({t.name}, {i}, {struct.calcsize(">"+t.fmt)})' for i,t in enumerate(TABLES,1))+'\n'
    types=dict(B='uint8_t',b='int8_t',H='tr64_u16_be_t',h='tr64_i16_be_t',I='tr64_u32_be_t',i='tr64_i32_be_t')
    for t in TABLES:
        if not t.fields:text+=f'typedef {types[t.fmt]} tr64_core_{t.name}_t;\n'; continue
        fields=iter(t.fields); text+='typedef struct {\n'
        for count,code in re.findall(r'(\d*)([BbHhIix])',t.fmt):
            count=int(count or 1)
            if code=='x':text+=f' uint8_t padding[{count}];\n'
            else:
                for _ in range(count):text+=f' {types[code]} {next(fields)};\n'
        text+=f'}} tr64_core_{t.name}_t;\ntypedef char tr64_size_{t.name}[sizeof(tr64_core_{t.name}_t)=={struct.calcsize(">"+t.fmt)} ? 1 : -1];\n'
    write_changed(path,(text+'#endif\n').encode())


def cook(source,name,out,cache,compressed=True,data='/tmp/tr64/data'):
    if name not in CATALOG:raise ValueError(f'{name}: not in the fixed game catalog')
    folder=Path(data)/('pc' if source=='pc' else 'psx/PSXDATA')
    matches=[p for p in folder.iterdir() if p.stem.lower()==name and p.suffix.lower() in ('.phd','.psx')]
    if len(matches)!=1:raise ValueError(f'input not unique: {source}/{name}')
    codec=pack.compressor(cache)
    inputs=dict(name=name,source=source,input_sha256=hashlib.sha256(matches[0].read_bytes()).hexdigest(),
        tool_sha256=tool_hash(),compressor_sha256=codec.identity,compressed=compressed)
    key=hashlib.sha256(json.dumps(inputs,sort_keys=True).encode()).hexdigest()
    content=Path('/tmp/tr64/content')/key
    hit=(content/'level.pack').exists() and (content/'level.json').exists()
    if hit:
        blob=(content/'level.pack').read_bytes(); manifest=json.loads((content/'level.json').read_text())
        if hashlib.sha256(blob).hexdigest()!=manifest['pack_sha256']:raise ValueError('native pack cache corrupt')
    else:
        level=(src_pc if source=='pc' else src_psx).load(matches[0])
        sections,tx_manifest=cook_sections(level)
        blob=pack.build({name:sections},codec if compressed else None)
        manifest=dict(inputs,content_key=key,pack_sha256=hashlib.sha256(blob).hexdigest(),pack_bytes=len(blob),
            core_bytes=sum(pack.align(len(s.data)) for s in sections if s.kind<65536),
            room_sprite_count=sum(len(r.sprites) for r in level.rooms),
            room_sprite_added_bytes=pack.align(32+12*sum(len(r.sprites) for r in level.rooms)),
            mesh_sphere_count=len(level.data['mesh_pointers']),
            mesh_sphere_added_bytes=pack.align(32+8*len(level.data['mesh_pointers'])),
            texture_bytes=len(sections[-1].data),checksums={TABLES[s.kind-1].name:checksum(s) for s in sections if s.kind<65536},
            texture_checksum=checksum(sections[-1]),mutable=mutable_bytes(level,tx_manifest))
        manifest['resident_layout']=pack.level_layout(sections)
        write_changed(content/'level.pack',blob)
        write_changed(content/'level.json',(json.dumps(manifest,indent=2)+'\n').encode())
    out=Path(out); out.mkdir(parents=True,exist_ok=True)
    write_changed(out/'level.pack',blob); write_changed(out/'level.json',(json.dumps(manifest,indent=2)+'\n').encode())
    print(f'level cook {source}/{name}: core={manifest["core_bytes"]} tex={manifest["texture_bytes"]} stored={len(blob)} cache={"hit" if hit else "miss"} key={key}')


def comparison_parts(section,mode):
    if mode=='whole':return [(0,section.data)]
    if mode=='native':return pack.parts(section)
    return pack.chunks(section.data,mode=='content_frames' and section.kind==14)


def dedup_tables(levels):
    report={}
    for mode in ('whole','fixed16k','content_frames','native'):
        by_kind={s.kind:{} for sections in levels.values() for s in sections if s.kind<65536}
        totals={kind:0 for kind in by_kind}
        for sections in levels.values():
            for section in sections:
                if section.kind>=65536:continue
                for _,raw in comparison_parts(section,mode):
                    digest=hashlib.sha256(raw).hexdigest(); row=by_kind[section.kind].setdefault(digest,[len(raw),0])
                    row[1]+=1; totals[section.kind]+=len(raw)
        report[mode]={TABLES[kind-1].name:dict(raw_bytes=totals[kind],unique_bytes=sum(n for n,c in hashes.values()),
            unique_sections=len(hashes),repeated_sections=sum(c-1 for n,c in hashes.values()),
            maximum_copies=max((c for n,c in hashes.values()),default=0)) for kind,hashes in by_kind.items()}
    return report


def cart_columns(blob):
    """Attribute shared storage to its first level; header32 is source-wide."""
    count=struct.unpack_from('>I',blob,12)[0]; seen=set(); result={}; table_bytes=32+48*count
    for i in range(count):
        name,offset,n,_,_=struct.unpack_from('>32s4I',blob,32+i*48)
        name=name.split(b'\0',1)[0].decode(); table_bytes+=64*n
        row=dict(raw_no_dedup=48+64*n,compressed_no_dedup=48+64*n,
                 raw_dedup=48+64*n,compressed_dedup=48+64*n)
        for j in range(n):
            _,_,_,stored,raw,_,_,_,digest=struct.unpack_from('>8I32s',blob,offset+j*64)
            row['raw_no_dedup']+=pack.align(raw); row['compressed_no_dedup']+=pack.align(stored)
            if digest not in seen:
                seen.add(digest); row['raw_dedup']+=pack.align(raw); row['compressed_dedup']+=pack.align(stored)
        result[name]=row
    return result,table_bytes


def measure(data,out,cache):
    # Decoder has its own literal layouts and imports no writer/schema module.
    import n64_core_view as independent
    import n64_geo
    import n64_snd
    start=time.monotonic(); codec=pack.compressor(cache)
    out=Path(out); out.mkdir(parents=True,exist_ok=True)
    report=dict(tool_sha256=tool_hash(),compressor_sha256=codec.identity,compiler=codec.compiler,python=sys.version,sources={})
    all_levels={}; read_count=0
    for source,pattern in (('pc','pc/*.phd'),('psx','psx/PSXDATA/*.PSX')):
        levels={}; core_levels={}; full_levels={}; rows=[]; excluded=[]
        files=sorted(Path(data).glob(pattern))
        if not files:raise ValueError(f'no {source} inputs')
        for path in files:
            level=(src_pc if source=='pc' else src_psx).load(path); read_count+=1
            if level.name not in CATALOG:
                try:n64_validate.validate(level)
                except ValueError as error:reason=str(error); status='not cookable'
                else:reason='not in fixed game catalog'; status='not selected'
                excluded.append(dict(level=level.name,input_sha256=level.sha256,status=status,reason=reason))
                print(f'{source}/{level.name}: {status}: {reason}')
                continue
            sections,tx_manifest=cook_sections(level)
            rg,mg,geometry=n64_geo.cook(level,tx_manifest)
            sound,sound_report=n64_snd.cook(level)
            full_sections=sections+[pack.Section(0x20000,rg),pack.Section(0x30000,mg),sound]
            layout=pack.level_layout(full_sections)
            full_levels[level.name]=full_sections
            blob=pack.build({level.name:full_sections},codec)
            decoded=independent.read_pack(blob)[level.name]
            independent.equal_ir(level,decoded)
            levels[level.name]=sections; core_levels[level.name]=sections[:-1]; all_levels[source+'/'+level.name]=sections
            core_bytes=sum(pack.align(len(s.data)) for s in sections[:-1])
            rows.append(dict(level=level.name,input_sha256=level.sha256,core_bytes=core_bytes,
                resident_layout=layout,texture_bytes=len(sections[-1].data),
                sound_bytes=len(sound.data),sound_metadata_bytes=sound_report['metadata_bytes'],
                sound_samples=sound_report['sample_count'],sound_unavailable=len(sound_report['unavailable']),
                room_sprite_count=sum(len(r.sprites) for r in level.rooms),
                room_sprite_added_bytes=pack.align(32+12*sum(len(r.sprites) for r in level.rooms)),
                mesh_sphere_count=len(level.data['mesh_pointers']),
                mesh_sphere_added_bytes=pack.align(32+8*len(level.data['mesh_pointers'])),
                cart_raw=len(pack.build({level.name:sections[:-1]})),cart_compressed=len(pack.build({level.name:sections[:-1]},codec)),
                full_cart_raw=len(pack.build({level.name:full_sections})),full_cart_compressed=len(blob),
                mutable=mutable_bytes(level,tx_manifest),checksums={TABLES[s.kind-1].name:checksum(s) for s in sections[:-1]},
                texture_checksum=checksum(sections[-1]),tables={t.name:dict(bytes=len(s.data),count=PREFIX.unpack_from(s.data)[2]) for t,s in zip(TABLES,sections)}))
        if set(levels)!=set(CATALOG):raise ValueError(f'{source}: catalog inputs missing: {set(CATALOG)-set(levels)}')
        totals={}; full_totals={}; level_totals={}
        for compressed in (False,True):
            for dedup in (False,True):
                key=('compressed' if compressed else 'raw')+('_dedup' if dedup else '_no_dedup')
                core_blob=pack.build(core_levels,codec if compressed else None,dedup); totals[key]=len(core_blob)
                blob=pack.build(levels,codec if compressed else None,dedup); full_totals[key]=len(blob)
                level_blob=pack.build(full_levels,codec if compressed else None,dedup); level_totals[key]=len(level_blob)
                if compressed and dedup:
                    write_changed(out/f'{source}.pack',level_blob)
                    columns,core_table_bytes=cart_columns(core_blob); full_columns,full_table_bytes=cart_columns(blob)
        for row in rows:
            row['cart_columns_first_owner']=columns[row['level']]
            row['core_texture_columns_first_owner']=full_columns[row['level']]
        for key in totals:
            if 32+sum(r[key] for r in columns.values())!=totals[key]:raise ValueError('core cart accounting disagreement')
            if 32+sum(r[key] for r in full_columns.values())!=full_totals[key]:raise ValueError('core+texture cart accounting disagreement')
        worst=max(rows,key=lambda r:r['core_bytes']); state=max(rows,key=lambda r:r['mutable']['total'])
        largest=max(rows,key=lambda r:r['resident_layout']['bytes'])
        report['sources'][source]=dict(count=len(rows),source_files_read=len(files),excluded=excluded,levels=rows,
            totals=totals,core_texture_totals=full_totals,worst_core=worst['level'],worst_core_bytes=worst['core_bytes'],
            level_cart_totals=level_totals,worst_level=largest['level'],worst_level_bytes=largest['resident_layout']['bytes'],
            worst_mutable=state['level'],worst_mutable_bytes=state['mutable']['total'],
            core_file_table_bytes=core_table_bytes,core_texture_file_table_bytes=full_table_bytes,
            core_only_remaining_for_textures_geometry_sound=24*1024*1024-totals['compressed_dedup'])
        print(f'{source}: {len(rows)} catalog roundtrips PASS; level {largest["level"]} {largest["resident_layout"]["bytes"]}/{largest["resident_layout"]["budget_bytes"]}; core peak {worst["core_bytes"]}; mutable {state["mutable"]["total"]}/131072; core cart {totals}; core+tex cart {full_totals}; full level cart {level_totals}')
    report['all_catalog_table_dedup']=dedup_tables(all_levels)
    report['source_files_read']=read_count; report['elapsed_seconds']=round(time.monotonic()-start,3)
    write_changed(out/'measurement.json',(json.dumps(report,indent=2)+'\n').encode())
    print(f'level measurement PASS {read_count} source inputs read, {sum(s["count"] for s in report["sources"].values())} catalog levels roundtrip; seconds={report["elapsed_seconds"]}; report={out/"measurement.json"}')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command',choices=('schema','cook','measure'))
    parser.add_argument('--out',required=True); parser.add_argument('--cache')
    parser.add_argument('--source',choices=('pc','psx'),default='pc'); parser.add_argument('--level',default='level1')
    parser.add_argument('--data',default='/tmp/tr64/data'); parser.add_argument('--uncompressed',action='store_true')
    args=parser.parse_args(); cache=args.cache or str(Path(args.out)/'compressor')
    if args.command=='schema':schema_header(args.out)
    elif args.command=='cook':cook(args.source,args.level,args.out,cache,not args.uncompressed,args.data)
    else:measure(args.data,args.out,cache)


if __name__=='__main__':main()
