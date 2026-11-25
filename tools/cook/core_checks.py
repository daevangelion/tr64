# SPDX-License-Identifier: MIT
"""External scene observable for the single production level loader."""
import json
from pathlib import Path
import re
from pool_rows import pool_rows


def texture_extent(manifest,text):
    """Exact shared tex use, including an optional independently verified image."""
    texture=manifest['texture_bytes']
    if 'image_bytes' not in manifest:
        if re.search(r'^level-image:',text,re.M):raise ValueError('image absent from cook manifest')
        return texture
    size,format=manifest['image_bytes'],manifest['image_format']
    if (format,size) not in ((1,77344),(2,153632)):raise ValueError('invalid cooked image format/extent')
    expected=f'level-image: bytes={size} format={format} width=320 height=240 hash={manifest["image_checksum"]}'
    if expected not in text.splitlines():raise ValueError('image verified-content fingerprint differs')
    return ((texture+15)&~15)+size

def check(ctx,args):
    if len(args)!=1 or args[0] not in ('pc','psx'):raise ValueError('level-load requires pc or psx')
    header=(ctx['build']/'generated/level_pack.h').read_text()
    match=re.search(r'^#define TR64_LEVEL_PACK_PATH (".*")$',header,re.M)
    if not match:raise ValueError('missing production pack binding')
    path=Path(json.loads(match[1])); manifest=json.loads(path.with_suffix('.json').read_text())
    if manifest['source']!=args[0]:raise ValueError('wrong source pack')
    text=ctx['log']
    limits={name:size for name,size,_ in pool_rows(game=None if ctx['cart'] else manifest.get('game',1))}
    code_pages=limits['code']//4096
    if f'level-pack: sha256={manifest["pack_sha256"]}' not in text:raise ValueError('pack identity differs from cook')
    tables={m[1]:m[2] for m in re.finditer(r'^core-table: (\w+) count=\d+ bytes=\d+ hash=([0-9a-f]{8})$',text,re.M)}
    if tables!=manifest['checksums']:raise ValueError('core verified-content checksums differ')
    if f'level-textures: bytes={manifest["texture_bytes"]} hash={manifest["texture_checksum"]}' not in text:raise ValueError('texture verified-content checksum differs')
    m=re.search(r'^level-load: level=(\w+) bytes_read=(\d+) total_us=(\d+) bus_us=(\d+) decode_us=(\d+) hash_us=(\d+) validation_us=(\d+) unit_load_us=(\d+) staging_peak=(\d+) ready=(core\+textures(?:\+sound)?) sound_refs=(validated|unvalidated) placement=code-unit$',text,re.M)
    if not m or m[1]!=manifest['name'] or any(int(m[i])<=0 for i in range(2,8)) or int(m[8])!=0 or not 0<int(m[9])<=16384:
        raise ValueError('missing/invalid level-load timings and honest readiness')
    sound='sound_bytes' in manifest
    expected_ready=('core+textures+sound','validated') if sound else ('core+textures','unvalidated')
    if (m[10],m[11])!=expected_ready:
        raise ValueError('sound readiness differs from selected pack')
    if sound:
        expected=f'level-sound: metadata={manifest["sound_metadata_bytes"]} blocks={manifest["sound_blocks"]} total={manifest["sound_bytes"]} metadata_crc={manifest["sound_metadata_crc"]} samples={manifest["sound_samples"]} unavailable={manifest["sound_unavailable"]} refs=validated'
        if expected not in text:raise ValueError('sound metadata/index differs from cook')
    pools={p[1]:(int(p[2]),int(p[3])) for p in re.finditer(r'^pool: (\w+) base=[0-9a-f]+ size=\d+ used=(\d+) peak=(\d+) kind=\d+$',text,re.M)}
    if pools.get('io',(1,0))[0]!=0 or not 0<pools.get('io',(0,0))[1]<=limits['io']:raise ValueError('loading io lease not released/bounded')
    sections={s[1]:(int(s[2],16),int(s[3]),int(s[4]),int(s[5])) for s in re.finditer(r'^level-section: (\w+) base=([0-9a-f]+) size=(\d+) used=(\d+) peak=(\d+)$',text,re.M)}
    layout=manifest['resident_layout']; order=('core','tex','rooms','meshes')
    parents=list(re.finditer(r'^pool: level base=([0-9a-f]+) size=(\d+) used=(\d+) peak=(\d+) kind=\d+$',text,re.M))
    parent=parents[-1] if parents else None
    if not parent or any(name in pools for name in order) or set(sections)!=set(order):
        raise ValueError('missing shared arena or obsolete resident partitions')
    base=int(parent[1],16); reserved=0
    for name in order:
        address,budget,used,peak=sections[name]
        if address!=base+reserved or budget!=layout['sections'][name] or not 0<=used<=peak<=budget:
            raise ValueError('level section layout/bound mismatch: '+name)
        reserved+=(budget+15)&~15
    if reserved!=layout['bytes'] or int(parent[2])!=layout['budget_bytes'] or int(parent[2])!=limits['level'] or int(parent[3])!=reserved or int(parent[4])!=reserved:
        raise ValueError('level arena reserved total differs from checked directory')
    if sections['core'][2]<=0 or sections['tex'][2]!=texture_extent(manifest,text):
        raise ValueError('Core/texture section allocation mismatch')
    if 'borrow: boot-card' in text:raise ValueError('boot card/level coexistence')
    loads=re.findall(r'^code: load load pages=(\d+) ',text,re.M)
    releases=re.findall(r'^code: unload load released=(\d+)$',text,re.M)
    scopes=list(re.finditer(r'^load-scope: unit_load_us=(\d+) total_us=(\d+) unload_us=(\d+) released=1$',text,re.M))
    if len(loads)!=1 or releases!=loads or len(scopes)!=1:
        raise ValueError('load unit did not have exactly one balanced outer lease')
    pages=int(loads[0]); scope=scopes[0]
    if not 0<pages<=code_pages or int(scope[2])<int(m[3]) or int(scope[2])<int(scope[1]):
        raise ValueError('invalid load-unit pages or complete transaction timing')
    if ctx['cart'] and int(scope[1])<=0:
        raise ValueError('missing emulated load-unit acquisition time')
    returned=re.search(r'^code: frames used=(\d+) free=(\d+) ',text[scope.end():],re.M)
    if not returned or returned[1]!='0' or int(returned[2])!=code_pages:
        raise ValueError('load-unit frames not returned before play')
    if pools.get('code',(0,0))[1]<pages*4096:
        raise ValueError('load-unit frames were not charged to the code pool')
    ctx['results'].update(tables=len(tables),bytes_read=int(m[2]),total_us=int(m[3]),bus_us=int(m[4]),
        decode_us=int(m[5]),hash_us=int(m[6]),validation_us=int(m[7]),core_unit_load_us=int(m[8]),
        unit_load_us=int(scope[1]),loading_total_us=int(scope[2]),unit_unload_us=int(scope[3]),
        staging_peak=int(m[9]),unit_pages=pages,frames_after_load=0,level_peak=reserved,
        core_peak=sections['core'][3],tex_peak=sections['tex'][3],rooms_peak=sections['rooms'][3],meshes_peak=sections['meshes'][3],
        io_peak=pools['io'][1],image=pools['image'][0],image_added=pools['image'][0]-192664,content_key=manifest['content_key'])
    if sound:ctx['results'].update(sound_metadata=manifest['sound_metadata_bytes'],sound_blocks=manifest['sound_blocks'],
        sound_bytes=manifest['sound_bytes'],sound_samples=manifest['sound_samples'],sound_unavailable=manifest['sound_unavailable'])


CHECKS={'level-load':check}
