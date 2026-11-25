# SPDX-License-Identifier: MIT
"""Independent roundtrips and correctly hashed hostile tables via one loader."""
import argparse
import copy
import json
from pathlib import Path
import re
import struct
import hashlib
import subprocess
import sys
import time
import zlib
sys.dont_write_bytecode=True
import n64_core as cook
import n64_core_view as view
import n64_pack as pack
import synthetic
import n64_snd
import n64_geo
import n64_validate
import n64_tex
import src_pc
import src_psx
from pool_rows import pool_rows


def fixture(source,rooms=1):
    level=synthetic.fixture(source)
    # Accounting fixtures deliberately omit audio payload semantics. Supply
    # valid, hand-authored PCM/SPU samples for the real native-bank loader.
    if source=='pc':
        pcm=bytes(range(256))*2
        level.samples=[b'RIFF'+struct.pack('<I',36+len(pcm))+b'WAVEfmt '+struct.pack('<IHHIIHH',16,1,1,22050,22050,1,8)+b'data'+struct.pack('<I',len(pcm))+pcm]
    else:level.samples=[bytes([12,1])+bytes([0x78]*14)]
    level.data['commands']=[1,-2,3,4]
    level.data['items'][0]['angle']=0xe000
    level.rooms[0].static_instances[0]['angle']=0xe000
    level.meshes[0].center=(-32768,123,32767); level.meshes[0].radius=65535
    second=list(level.meshes.values())[1]; second.center=(4,-5,6); second.radius=0
    level.rooms.extend(copy.deepcopy(level.rooms[0]) for _ in range(rooms-1))
    level=(src_pc if source=='pc' else src_psx).read(synthetic.encode(level))
    level.data['mesh_pointers']+=level.data['mesh_pointers'][:]
    if source=='psx':
        tones=list(level.data['vab_tones']);tone=list(tones[0])
        tone[4:6]=[84,0];tone[18:20]=[0,1];tones[0]=tuple(tone)
        level.data['vab_tones']=tones
    return level


def run(harness,path,name='synthetic',mode='retail',expected=None):
    result=subprocess.run([str(harness),str(path),name,mode],capture_output=True,text=True,timeout=12)
    text=result.stdout+result.stderr
    path.with_suffix('.log').write_text(text)
    if expected is None:
        if result.returncode:raise AssertionError(text)
        markers=('ready=core+textures sound_refs=unvalidated','ready=core+textures+sound sound_refs=validated')
        if mode=='save-picker':markers=('save picker: TR1 offered',)
        if mode=='save-picker-tr2':markers=('save: level1 is render-only; not offered for load',)
        if mode=='sound-lifecycle':markers=('TR1 -> TR2 idle cache borrow -> TR1 restored resident sample bytes',)
        if not any(marker in text for marker in markers) and mode not in ('opaque','stream','row-switch'):raise AssertionError('missing honest readiness')
        if 'pool: io ' not in text or not re.search(r'pool: io .* used=0 ',text):raise AssertionError('io lease not released')
    elif not result.returncode or expected not in text:
        raise AssertionError(f'expected {expected}: status={result.returncode}\n{text}')
    else:print(f'negative PASS {path.stem}: {text.splitlines()[-1]}')
    return text


def checks(text,native):
    found={m[1]:m[2] for m in re.finditer(r'^core-table: (\w+) count=\d+ bytes=\d+ hash=([0-9a-f]{8})$',text,re.M)}
    expected={name:f'{native.checksums[i]:08x}' for i,(name,_,_) in enumerate(view.LAYOUTS,1)}
    if found!=expected:raise AssertionError('per-table verified-content checksum mismatch')
    if f'hash={native.checksums[65536]:08x}' not in text:raise AssertionError('texture verified-content checksum mismatch')
    if 0x40000 in native.sections:
        raw=native.sections[0x40000]
        metadata=struct.unpack_from('>I',raw,12)[0];samples=struct.unpack_from('>I',raw,16)[0];missing=struct.unpack_from('>I',raw,28)[0]
        found=re.search(r'^level-sound: metadata=(\d+) blocks=(\d+) total=(\d+) metadata_crc=([0-9a-f]{8}) samples=(\d+) unavailable=(\d+) refs=validated$',text,re.M)
        if not found or int(found[1])!=metadata or int(found[2])<=0 or int(found[3])!=len(raw) or found[4]!=f'{zlib.crc32(raw[:metadata]):08x}' or int(found[5])!=samples or int(found[6])!=missing:
            raise AssertionError('sound metadata/reference readiness mismatch')
        if 'ready=core+textures+sound sound_refs=validated' not in text:raise AssertionError('sound bank not ready')
    elif 'ready=core+textures sound_refs=unvalidated' not in text:
        raise AssertionError('no-bank sound domain falsely validated')


def changed(sections,kind,offset,fmt,value):
    result=dict(sections); data=bytearray(result[kind]); struct.pack_into('>'+fmt,data,32+offset,value); result[kind]=bytes(data)
    return result


def words(sections,values):
    result=dict(sections); raw=struct.pack('>'+str(len(values))+'H',*values)
    result[6]=struct.pack('>4s7I',b'TR6C',6,len(values),2,1,32,32+len(raw),cook.CORE_ABI)+raw
    return result


def sequel(harness,out,codec):
    level=fixture('pc')
    room=level.rooms[0]
    level.reserved.update(game=2,argb1555_pages=[b'\xff\xff'*65536],
                          solid_colour_palette=bytes(1024))
    room.flags=0
    room.reserved.update(ambient2=456,light_mode=2)
    for vertex in room.vertices:
        vertex.update(legacy_lighting=0,attributes=0x8000,
                      lighting=dict(value=4096,encoding='tr2_darkness_13bit'))
    room.lights[0].update(intensity2=800,radius2=2048)
    room.static_instances[0].update(intensity=6144,intensity2=4096)
    level.data['items'][0]['intensity2']=-3
    level.data['extra_ground_zones']=[10,11,12,13]
    level.data['sound_map'] += [-1]*114
    level.sprite_textures[0].uv=((13,12),(15,19))
    tx,manifest=n64_tex.cook(level)  # This synthetic IR was changed after source hashing.
    sections=cook.encode(level,manifest)+[pack.Section(0x10000,tx)]
    rg,_=n64_geo.section(level,manifest,True)
    vertices=struct.unpack_from('>I',rg,48)[0]
    if rg[vertices+16:vertices+24]!=bytes((171,171,171,255))*2:
        raise AssertionError('TR2 RG64 shade did not use the second light word')
    blob=pack.build({'synthetic':sections},codec,games={'synthetic':2})
    native=view.read_pack(blob)['synthetic']
    core=view.decode_core(native)
    if (native.game,core['rooms'][0]['ambient2'],core['rooms'][0]['light_mode'],
        core['lights'][0]['radius2'],core['static_instances'][0]['intensity2'],
        core['items'][0]['intensity2'],len(core['zones']))!=(2,456,2,2048,4096,-3,10):
        raise AssertionError('TR2 Core extensions not carried')
    path=out/'tr2-valid.pack';path.write_bytes(blob)
    checks(run(harness,path),native)
    zone=cook.KINDS['zones']
    broken=[]
    for section in sections:
        if section.kind==zone:
            payload=bytearray(section.data[:-2])
            struct.pack_into('>I',payload,8,9)
            struct.pack_into('>I',payload,24,len(payload))
            section=pack.Section(zone,bytes(payload))
        broken.append(section)
    path=out/'tr2-truncated-zones.pack'
    path.write_bytes(pack.build({'synthetic':broken},codec,games={'synthetic':2}))
    run(harness,path,expected='core zones: zone array count differs from game version')
    level.data['extra_ground_zones'].pop()
    try:
        n64_validate.validate(level)
    except ValueError as error:
        if not str(error).startswith('zones:'):raise
    else:
        raise AssertionError('TR2 native cook accepted truncated zones')
    print('TR2 second room-light word, Core fields and cook/loader zone controls PASS')
    legacy,_=cook.cook_sections(fixture('pc'))
    def bank(frames,part,frequent=0):
        metadata=n64_snd.HEADER.size+n64_snd.SAMPLE.size
        total=metadata+pack.align(frames)
        header=n64_snd.HEADER.pack(b'SN64',1,total,metadata,1,32,64,0,metadata,4096,*([0]*6))
        descriptor=n64_snd.SAMPLE.pack(metadata,frames,22050,1,0,frames,frequent,0)
        return pack.Section(n64_snd.KIND,header+descriptor+b'\x5a'*frames+bytes(total-metadata-frames),
                            header_bytes=metadata,chunk_bytes=part,compression='none')
    for game,rows in ((0,legacy),(2,sections)):
        games={'synthetic':2} if game else None
        for block in (8192,4096):
            path=out/f'game-{game}-sample-5000-part-{block}.pack'
            path.write_bytes(pack.build({'synthetic':rows+[bank(5000,block)]},codec,games=games))
            run(harness,path,expected='audio block partition' if block==8192 else None)
        for_save=pack.build({'level1':rows},codec,games={'level1':2} if game else None)
        path=out/f'game-{game}-save-picker.pack'
        path.write_bytes(for_save)
        identity=struct.unpack_from('>I',for_save,72)[0] or 1
        text=run(harness,path,name=f'{identity:x}',mode='save-picker-tr2' if game else 'save-picker')
        if game and 'save picker: TR2 refused' not in text:
            raise AssertionError('TR2 catalog refused but slot was not checked')
    path=out/'tr1-tr2-tr1-sound.pack'
    path.write_bytes(pack.build({'legacy':legacy+[bank(32,4096,1)],
                                 'sequel':sections+[bank(32,4096)]},codec,games={'sequel':2}))
    run(harness,path,mode='sound-lifecycle')
    return 9


def wrong_texture_game(out):
    path=out/'tr1-source.phd'
    path.write_bytes(synthetic.encode(synthetic.fixture('pc')))
    level=src_pc.load(path)
    tx,manifest=n64_tex.cook(level)
    tx=bytearray(tx)
    struct.pack_into('>I',tx,68,1)
    manifest['section_sha256']=hashlib.sha256(tx).hexdigest()
    textures=out/'wrong-texture-game'
    textures.mkdir(exist_ok=True)
    (textures/'section.bin').write_bytes(tx)
    (textures/'manifest.json').write_text(json.dumps(manifest))
    result=subprocess.run(['rtk',sys.executable,str(Path(__file__).with_name('pack_level.py')),
                           '--source','pc','--input',str(path),'--textures',str(textures),
                           '--out',str(out/'wrong-texture-game.pack')],capture_output=True,text=True,timeout=12)
    if result.returncode!=1 or 'explicit texture input game version / TX64 flags mismatch' not in result.stderr:
        raise AssertionError(result.stdout+result.stderr)
    print('negative PASS explicit texture game version mismatch')


def tests(harness,out,cache):
    out.mkdir(parents=True,exist_ok=True); codec=pack.compressor(cache); count=0
    count+=sequel(harness,out,codec)
    wrong_texture_game(out)
    for source in ('pc','psx'):
        level=fixture(source); sections,manifest=cook.cook_sections(level)
        base={s.kind:s.data for s in sections}
        sound,_=n64_snd.cook(level)
        bank_sections=sections+[sound]
        bank_blob=pack.build({'synthetic':bank_sections},codec)
        bank_native=view.read_pack(bank_blob)['synthetic']
        path=out/f'{source}-sound.pack';path.write_bytes(bank_blob)
        checks(run(harness,path,mode='values'),bank_native);count+=1
        bad=changed(dict((s.kind,s.data) for s in bank_sections),26,0,'H',65535)
        path=out/f'{source}-sound-unlisted.pack'
        path.write_bytes(pack.build({'synthetic':[sound if k==0x40000 else pack.Section(k,v) for k,v in bad.items()]},codec))
        run(harness,path,expected='core sound_details: unlisted missing sample');count+=1
        for compressed in (False,True):
            blob=pack.build({'synthetic':sections},codec if compressed else None)
            native=view.read_pack(blob)['synthetic']; view.equal_ir(level,native)
            path=out/f'{source}-{compressed}.pack'; path.write_bytes(blob)
            checks(run(harness,path,mode='values'),native); count+=1
        mutations=[
          ('old-core-abi',changed(base,1,-4,'I',2),'core rooms:'),
          ('tr1-secondary-ambient',changed(base,1,76,'H',456),'core rooms: TR1 reserved lighting'),
          ('tr1-room-light-mode',changed(base,1,78,'H',2),'core rooms: TR1 reserved lighting'),
          ('tr1-secondary-intensity',changed(base,22,22,'h',-3),'core items: TR1 reserved intensity'),
          ('room-sprite-span',changed(base,1,68,'I',0xffffffff),'core room_sprites:'),
          ('room-sprite-gain',changed(base,28,6,'H',8193),'core room_sprites:'),
          ('room-sprite-descriptor',changed(base,28,8,'H',0),'core room_sprites:'),
          ('room-sprite-reserved',changed(base,28,10,'H',1),'core room_sprites:'),
          ('table-offset',changed(base,1,-12,'I',0xfffffff0),'core rooms:'),
          ('frame-offset',changed(base,8,0,'I',0xfffffffe),'core animations:'),
          ('animation-next',changed(base,8,20,'H',65535),'core animations:'),
          ('animation-next-frame',changed(base,8,22,'H',0xfff0),'core animations:'),
          ('range-target',changed(base,10,4,'H',32767),'core ranges:'),
          ('command-runoff',changed(changed(base,11,6,'h',1),8,30,'H',3),'core commands:'),
          ('command-opcode',changed(base,11,0,'h',0x7777),'core commands:'),
          ('model-animation',changed(base,13,16,'H',32767),'core models:'),
          ('model-type',changed(base,13,0,'I',0xabcdef),'core models:'),
          ('box-head',changed(base,19,18,'H',0x3fff),'core overlaps:'),
          ('overlap-target',changed(base,20,0,'H',65535),'core overlaps:'),
          ('overlap-end',changed(base,20,0,'H',0),'core overlaps:'),
          ('floor-end',words(base,[0,0]),'core floor_data:'),
          ('floor-portal',words(base,[0,0x8001,0x7fff]),'core floor_data:'),
          ('trigger-item',words(base,[0,0x8004,0,0x83ff]),'core floor_data:'),
          ('trigger-camera',words(base,[0,0x8004,0,0x7ff,0x8000]),'core floor_data:'),
          ('alternate-self',changed(base,1,28,'h',0),'core rooms:'),
          ('item-type',changed(base,22,0,'H',65535),'core items:'),
          ('camera-room',changed(base,17,12,'h',32767),'core cameras:'),
          ('static-id',changed(base,15,0,'I',0x7fffffff),'core static_meshes:'),
          ('static-instance',changed(base,5,16,'H',65535),'core static_instances:'),
          ('sprite-count',changed(base,16,4,'h',-30000),'core sprite_sequences:'),
          ('state-id',changed(base,9,0,'H',0x1234),'core states:'),
          ('sector-dimensions',changed(base,1,26,'H',300),'core rooms:')]
        for sphere_count in (3,5):
            bad=dict(base); raw=base[29][32:32+sphere_count*8]
            if sphere_count==5:raw+=raw[:8]
            bad[29]=struct.pack('>4s7I',b'TR6C',29,sphere_count,8,1,32,32+len(raw),cook.CORE_ABI)+raw
            mutations.append((f'mesh-sphere-count-{sphere_count}',bad,'core mesh_spheres: count differs'))
        for offset,fmt in ((0,'h'),(2,'h'),(4,'h'),(6,'H')):
            bad=changed(base,29,offset,fmt,0)
            altered=view.read_pack(pack.build({'synthetic':[pack.Section(k,v) for k,v in bad.items()]}))['synthetic']
            try:view.equal_ir(level,altered)
            except ValueError as error:
                if 'mesh_spheres' not in str(error):raise
            else:raise AssertionError('independent decoder accepted changed sphere field')
        badtex=dict(base); tx=bytearray(badtex[65536]); animation=struct.unpack_from('>I',tx,64)[0]
        struct.pack_into('>H',tx,animation,65535); badtex[65536]=bytes(tx)
        mutations.append(('animated-texture-count',badtex,'textures:'))
        # Topology failures need genuine multi-room packs, not synthetic rejects.
        multi=fixture(source,3); more,_=cook.cook_sections(multi); multi_base={s.kind:s.data for s in more}
        bad=changed(changed(multi_base,1,28,'h',1),1,80,'i',1025)
        mutations.append(('alternate-dimensions',bad,'core rooms:'))
        bad=changed(changed(multi_base,1,28,'h',2),1,80+28,'h',2)
        mutations.append(('alternate-used-twice',bad,'core rooms:'))
        for label,values,reason in mutations:
            path=out/f'{source}-{label}.pack'; path.write_bytes(pack.build({'synthetic':[pack.Section(k,v) for k,v in values.items()]},codec))
            run(harness,path,expected=reason); count+=1
        blob=bytearray(pack.build({'synthetic':sections},codec)); table=struct.unpack_from('>I',blob,64)[0]; entries=struct.unpack_from('>I',blob,68)[0]
        blob[table+28]^=1
        struct.pack_into('>I',blob,72,zlib.crc32(blob[table:table+64*entries])); struct.pack_into('>I',blob,24,zlib.crc32(blob[32:80]))
        path=out/f'{source}-checksum.pack'; path.write_bytes(blob); run(harness,path,expected='corrupt checksum'); count+=1
        blob=bytearray(pack.build({'synthetic':sections},codec))[:-64]; struct.pack_into('>I',blob,8,len(blob))
        path=out/f'{source}-truncated.pack'; path.write_bytes(blob); run(harness,path,expected='truncated section'); count+=1
        run(harness,out/f'{source}-False.pack',mode='small',expected='pool level overflow'); count+=1
        # The cook must also reject invalid IR, not merely serialize a bad graph.
        for table,key,value in [('animations','next_animation',65535),('models','type',0xabcdef),('items','type',65535),('cameras','room_or_speed',32767),('states','state',0x1234),('sprite_sequences','count',-30000)]:
            bad=copy.deepcopy(level); bad.data[table][0][key]=value
            try:cook.encode(bad,manifest)
            except ValueError:pass
            else:raise AssertionError(f'cook accepted bad {table}')
        bad=copy.deepcopy(level); bad.data['mesh_pointers'].append(0xffffffff)
        try:cook.encode(bad,manifest)
        except ValueError as error:
            if 'mesh_spheres: unknown mesh pointer' not in str(error):raise
        else:raise AssertionError('cook accepted missing mesh sphere')
    opaque=[pack.Section(65536,b'texture'*1000),pack.Section(131072,b'room'*100)]
    blob=pack.build({'one':opaque,'two':opaque},codec); path=out/'opaque.pack'; path.write_bytes(blob)
    run(harness,path,name='one',mode='opaque'); count+=1
    run(harness,path,name='one',mode='repeat',expected='pool level/tex overflow'); count+=1
    # A section may exceed its former partition when the combined level fits.
    path=out/'tex-shared-room.pack'
    path.write_bytes(pack.build({'one':[pack.Section(65536,bytes(512*1024+1)),opaque[1]]},codec))
    run(harness,path,name='one',mode='opaque'); count+=1
    assert len(blob)<len(pack.build({'one':opaque,'two':opaque},codec,False))
    malformed=bytearray(pack.build({'one':[pack.Section(65536,bytes(1000)),opaque[1]]},codec))
    table=struct.unpack_from('>I',malformed,64)[0]
    physical=struct.unpack_from('>I',malformed,table+8)[0]
    malformed[physical:physical+3]=bytes(3)
    path=out/'malformed-lz4.pack'; path.write_bytes(malformed)
    run(harness,path,name='one',mode='opaque',expected='LZ4 decode bounds'); count+=1
    # The same host must enforce the level entry's row, not its build knob.
    # The cook uses the explicitly selected target table.
    budgets={game:{name:size for name,size,_ in pool_rows(game=game)} for game in (1,2)}
    for game, rows in budgets.items():
        budget=rows['level']
        tex=pack.Section(0x10000,bytes(budget//2))
        for extra in (0,1):
            rooms=pack.Section(0x20000,bytes(budget-len(tex.data)+extra))
            resident=[tex,rooms]
            if extra:
                try:pack.level_layout(resident,budget)
                except ValueError as error:
                    if str(error)!=f'level resident total {budget+16} exceeds arena {budget}':raise
                else:raise AssertionError('cook accepted one raw byte over the shared row')
            elif pack.level_layout(resident,budget)['margin_bytes']!=0:
                raise AssertionError('cook did not account for an exact fit')
            path=out/f'level-row-{game}-{extra}.pack'
            path.write_bytes(pack.build({'one':resident},codec,games={'one':2} if game==2 else None))
            if extra:
                run(harness,path,name='one',mode='opaque',expected=f'pool level overflow level=one section=rooms request={len(rooms.data)+15} reserved={len(tex.data)} budget={budget}')
            else:
                text=run(harness,path,name='one',mode='opaque')
                if f'size={budget} used={budget} peak={budget}' not in text:
                    raise AssertionError('native level mount did not reserve the exact row')
                if game==2:
                    path=out/'large-legacy-entry.pack'
                    path.write_bytes(pack.build({'one':resident},codec))
                    run(harness,path,name='one',mode='opaque',expected=f'pool level overflow level=one section=rooms request={len(rooms.data)} reserved={len(tex.data)} budget={budgets[1]["level"]}')
                    count+=1
            count+=1
    path=out/'mixed-game-switch.pack'
    path.write_bytes(pack.build({'legacy':opaque,'sequel':opaque},codec,games={'sequel':2}))
    text=run(harness,path,mode='row-switch')
    expected=[f'{name} level={budgets[game]["level"]} state={budgets[game]["state"]}'
              for name,game in (('legacy',1),('sequel',2),('legacy',1))]
    if re.findall(r'^level rows: (.+)$',text,re.M)!=expected:
        raise AssertionError('mixed-game host did not change row budgets at mounted entry')
    count+=1
    layout=[pack.Section(1,bytes(17)),pack.Section(65536,bytes(17)),pack.Section(131072,bytes(17)),pack.Section(196608,bytes(17))]
    if pack.level_layout(layout,128)['bytes']!=128:raise AssertionError('level layout alignment')
    try:pack.level_layout(layout,127)
    except ValueError:pass
    else:raise AssertionError('cook accepted aggregate level overflow')
    stream_data=b'H'*16+b'A'*4096+b'odd'
    stream_section=pack.Section(0x40000,stream_data,header_bytes=16,chunk_bytes=4096,
        compression='none',boundaries=(16,4112))
    blob=pack.build({'one':[stream_section]},codec); path=out/'stream.pack'; path.write_bytes(blob)
    text=run(harness,path,name='one',mode='stream'); count+=1
    if 'stream: blocks=3 bytes=4115 unit_loaded=0' not in text:raise AssertionError('resident stream path')
    run(harness,path,name='one',mode='stream-small-index',expected='stream index capacity'); count+=1
    blob=pack.build({'one':[pack.Section(0x40000,stream_data,header_bytes=16,chunk_bytes=4096)]},codec)
    path=out/'stream-compressed.pack'; path.write_bytes(blob)
    run(harness,path,name='one',mode='stream',expected='stream block compressed'); count+=1
    bad=bytearray(pack.build({'one':[stream_section]},codec))
    table=struct.unpack_from('>I',bad,64)[0]; physical=struct.unpack_from('>I',bad,table+8)[0]; bad[physical]^=1
    path=out/'stream-checksum.pack'; path.write_bytes(bad)
    run(harness,path,name='one',mode='stream',expected='corrupt checksum'); count+=1
    # Wire table plus staging exceeds48KiB; compact retained records must fit.
    large=pack.build({'one':[pack.Section(0x40000,bytes(700*16),chunk_bytes=16,compression='none')]},codec)
    path=out/'stream-paged-table.pack'; path.write_bytes(large)
    text=run(harness,path,name='one',mode='stream'); count+=1
    if 'stream: blocks=700 bytes=11200 unit_loaded=0' not in text:raise AssertionError('paged table path')
    bad=bytearray(large); table=struct.unpack_from('>I',bad,64)[0]; bad[table+600*64+32]^=1
    path=out/'stream-paged-table-crc.pack'; path.write_bytes(bad)
    run(harness,path,name='one',mode='stream',expected='section table: corrupt CRC'); count+=1
    print(f'core synthetic PASS {count} production runs; independent decoder, both sources, hostile-table cases, hashes/truncation/pools, native signed angles, room sprites and aliased mesh spheres, opaque kinds')


def real(harness,directory,out):
    report=json.loads((directory/'measurement.json').read_text()); total=0
    for source,meta in report['sources'].items():
        path=directory/f'{source}.pack'; native=view.read_pack(path.read_bytes())
        for row in meta['levels']:
            text=run(harness,path,name=row['level']); checks(text,native[row['level']]); total+=1
    if total!=42 or sum(m['source_files_read'] for m in report['sources'].values())!=43:raise AssertionError('catalog/source accounting')
    print('catalog production PASS 42 levels (21 PC,21 PSX); all43 source files read, snowcut not cookable; core+TX64, cold loader, zero retained io')


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--harness',type=Path,required=True); ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--cache',type=Path,required=True); ap.add_argument('--real',type=Path)
    ns=ap.parse_args(); ns.out.mkdir(parents=True,exist_ok=True); start=time.monotonic()
    if ns.real:real(ns.harness,ns.real,ns.out)
    else:tests(ns.harness,ns.out,ns.cache)
    print(f'core tests PASS seconds={time.monotonic()-start:.3f}')


if __name__=='__main__':main()
