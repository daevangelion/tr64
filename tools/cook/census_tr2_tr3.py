# SPDX-License-Identifier: MIT
"""Measure sequel source and native-format footprints, without writing packs."""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import struct
import sys

from account_tr2_tr3 import check as account_level
from binary import FormatError
import n64_core
import n64_geo
from n64_pack import align
import n64_snd
from n64_snd_source import pcm_wave
import n64_tex
from sizing import pools, resident_total
import src_pc
import src_tr2_tr3 as source

ROOT = Path(__file__).resolve().parents[2]
RESERVE = 742*1024  # m9-coop-plat image row is 608 KiB.
SPLIT_LEVEL = 540*1024
SPLIT_STATE = 104*1024
EXPANSION = 4096*1024
SAVE_WORKSPACE = 6144  # engine/save/game.c:30 and engine/save/save.h:9.
CORE_DRAW_TABLES = {'lights', 'room_sprites', 'sprite_sequences'}
CORE_ANIMATION_TABLES = {'animations', 'states', 'ranges', 'commands', 'bones', 'models', 'frames', 'node_flags'}
CORE_NAV_TABLES = {'boxes', 'overlaps', 'zones'}
CORE_COLLISION_TABLES = {'rooms', 'sectors', 'portals', 'static_instances', 'static_meshes', 'mesh_spheres', 'floor_data'}


def textures(level):
    try:
        blob, manifest = n64_tex.cook(level)
    except ValueError as error:
        if not str(error).startswith('CI4 descriptor requires even cluster-local'):
            raise
        rejected = str(error)
    else:
        return manifest, dict(method='writer', bytes=len(blob), breakdown=manifest['breakdown'])

    rows, animations = n64_tex.rectangles(level)
    groups = n64_tex.clusters(rows)
    maps = {'object_descriptor_map': {}, 'sprite_descriptor_map': {}}
    split_lines = 0
    for i, cluster in enumerate(groups):
        x, w, h = (cluster[k] for k in ('x','width','height'))
        left, right = x & ~1, align(x+w, 2)
        cluster.update(x=left, width=right-left, image_bytes=(right-left)//2*h)
        for d in cluster['members']:
            row = rows[d]
            x,y,w,h = row['rect']
            left, right = x & ~1, align(x+w, 2)
            row.update(rect=(left,y,right-left,h), cluster=i, palette=i,
                       tiles=n64_tex.tiles(left-cluster['x'],right-left,h))
            split_lines += len(row['tiles'])-1
            maps[row['kind']+'_descriptor_map'][str(row['texture'])] = d
    flat = sorted({f.color for m in level.meshes.values() for f in m.faces if f.color is not None})
    words = 1+sum(1+len(group) for group in animations)
    count = len(groups)  # Conservative: one new palette per padded cluster, no dedup.
    parts = dict(header=n64_tex.HEADER.size, cluster_records=count*n64_tex.CLUSTER.size,
                 palettes=40*count, descriptors=sum(len(m)*s for m,s in
                     zip(maps.values(),(n64_tex.OBJECT.size,n64_tex.SPRITE.size))),
                 split_lines=2*split_lines, colours=8*len(flat), animations=2*words,
                 images=sum(c['image_bytes'] for c in groups))
    lengths = [parts['header'], parts['cluster_records'], count*n64_tex.PALETTE.size,
               len(maps['object_descriptor_map'])*n64_tex.OBJECT.size,
               len(maps['sprite_descriptor_map'])*n64_tex.SPRITE.size,
               parts['split_lines'], parts['colours'], parts['animations']]
    total = sum(align(n,8) for n in lengths)+sum(align(c['image_bytes'],8) for c in groups)+32*count
    parts['alignment'] = total-sum(parts.values())
    manifest = dict(clusters=groups, descriptors=rows, flat_colours=[(f,) for f in flat], **maps)
    return manifest, dict(method='even-ci4-estimate', bytes=total, breakdown=parts, writer_rejection=rejected)


def geometry(level, manifest, room, texture_method):
    blob, info = n64_geo.section(level, manifest, room)
    unchanged = texture_method == 'writer' and (not room or level.reserved['game'] != 3)
    return dict(bytes=len(blob), method='writer' if unchanged else 'writer-projection',
                triangles=info['triangles'], objects=info['object_records'])


def core(level, manifest, game):
    data = level.data
    try:
        sections = n64_core.encode(level, manifest)
    except (ValueError, struct.error) as error:
        counts = {t.name:len(data.get(t.name) or ()) for t in n64_core.TABLES}
        counts.update(rooms=len(level.rooms), node_flags=len(data['bones'])//4,
                      mesh_spheres=len(data['mesh_pointers']))
        for key in ('portals','sectors','lights','static_instances'):
            counts[key] = sum(len(getattr(room,key)) for room in level.rooms)
        counts['room_sprites'] = sum(len(room.sprites) for room in level.rooms)
        counts['zones'] += len(data['extra_ground_zones'])
        # Wider room records carry sequel ambient/mode or ambient + the III tail.
        # Repack full 24 B source lamps, 20 B instances and 24 B items in existing strides.
        tables = {t.name:align(32+counts[t.name]*(struct.calcsize('>'+t.fmt)+
                     ((4 if game == 2 else 8) if t.name=='rooms' else 0))) for t in n64_core.TABLES}
        return dict(method='core-table-estimate', bytes=sum(tables.values()), tables=tables,
                    writer_rejection=str(error))
    tables = {t.name:align(len(section.data)) for t,section in zip(n64_core.TABLES,sections,strict=True)}
    return dict(method='writer', bytes=sum(tables.values()), tables=tables)


def game_state(level, game):
    data = level.data
    rooms, boxes, items = len(level.rooms), len(data['boxes']), len(data['items'])
    sectors = sum(len(room.sectors) for room in level.rooms)
    nmodel = (191,265,374)[game-1]
    item_stride, model_stride = (76,8) if game == 1 else (80,10)
    # Target-sized allocation terms from the 32-bit ABI and game/game.c:235–238.
    tables = dict(fixed_game=8392, room_bindings=16*rooms, mutable_sectors=8*sectors,
                  items=align(max(256,items)*item_stride,4), models=align(nmodel*model_stride,4),
                  navigation=9*8*boxes, camera_flags=align(2*len(data['cameras']),4),
                  box_flags=align(2*boxes,4))
    objects, _, groups = n64_tex.references(level)
    anim = 2*len(objects)+4*sum(1+len(a) for a in groups) if groups else 0
    renderer = align(align(13*rooms,8)+12*(rooms+len(level.meshes))+2*items+anim)
    # The live scene allocates four separate 16-byte-aligned chunks in the state arena.
    terms = [sum(tables.values()), renderer, 4352, SAVE_WORKSPACE]
    return dict(method='state-estimate', bytes=sum(map(align,terms)), tables=tables,
                renderer=renderer, drawables=terms[2], save=terms[3], item_slots=max(256,items))


def bank_measure(raw, label):
    bank = source.read_bank(raw,label)
    sizes, rates = [], Counter()
    for record in bank:
        pcm, rate, _ = pcm_wave(record)
        sizes.append(len(pcm)*(1 if all(v % 256 == 0 for v in pcm) else 2))
        rates[rate] += 1
    return bank, sizes, dict(sha256=hashlib.sha256(raw).hexdigest(), bytes=len(raw), samples=len(bank),
                             sample_rates=dict(rates), native_pcm_bytes=sum(sizes))


def audio(level, bank, bank_sizes):
    data = level.data
    ordinals = data['sample_ordinals']
    slots = set()
    for index in set(data['sound_map']) - {-1}:
        detail = data['sound_details'][index]
        start = detail['sample']
        slots.update(range(start,start+max(1,(detail['flags']>>2)&15)))
    used = {ordinals[i] for i in slots}
    metadata = align(n64_snd.HEADER.size+len(ordinals)*n64_snd.SAMPLE.size)
    try:
        blob, report, _ = n64_snd.encode(level)
    except ValueError as error:
        if str(error) not in ('ambient variation outside measured TR1 source contract',
                              'SN64 ambient emitter capacity64 exceeded'):
            raise
        packed = [align(bank_sizes[i]) for i in ordinals]
        effects = dict(method='sn64-byte-estimate', writer_rejection=str(error),
                       section_bytes=metadata+sum(packed), metadata_bytes=metadata,
                       stream_blocks=1+sum((n+4095)//4096 for n in packed),
                       encoded_bytes=sum(bank_sizes[i] for i in ordinals))
    else:
        effects = {k:report[k] for k in ('metadata_bytes','stream_blocks','encoded_bytes','frequent_bytes')}
        effects.update(method='writer', section_bytes=len(blob))
    emitters = len(data['sound_sources'])
    # Runtime gains/heard arrays take five bytes per emitter; preserve all existing cache/voice space.
    delta = (max(0,effects['metadata_bytes']-4096)+16*max(0,effects['stream_blocks']-1024)+
             align(5*max(0,emitters-64)))
    return dict(**effects, row_method='audio-fixed-plus-subcapacity-estimate', row_bytes=127584+delta,
                runtime_subcaps_fit=effects['metadata_bytes']<=4096 and effects['stream_blocks']<=1024 and
                                    emitters<=64 and len(ordinals)<=256,
                slot_count=len(ordinals), directory_unique=len(set(ordinals)),
                referenced_slots=len(slots), referenced_ordinals=sorted(used), referenced_unique=len(used),
                referenced_wave_bytes=sum(len(bank[i]) for i in used), listed_wave_bytes=sum(map(len,level.samples)))


def model_meshes(level):
    data = level.data
    ptr = data['mesh_pointers']
    return ({m['type']:ptr[m['mesh_start']:m['mesh_start']+m['mesh_count']] for m in data['models']},
            {s['id']:ptr[s['mesh']] for s in data['static_meshes']})


def triangle_count(faces):
    return sum(len(f.vertices)-2 for f in faces)


def portal_sets(level, *, raw=False):
    """Unclipped reachability on projected base IDs or renderer raw IDs."""
    alt = {r.alternate_room:i for i,r in enumerate(level.rooms) if r.alternate_room != -1}
    starts = [i for i in range(len(level.rooms)) if raw or i not in alt]
    for flipped in (False,True):
        physical = {i:i for i in starts}
        if flipped:
            for other, base in alt.items():
                physical[base] = other
                if raw:
                    physical[other] = base
        edges = {i:{p['room'] if raw else alt.get(p['room'],p['room'])
                    for p in level.rooms[j].portals} for i,j in physical.items()}
        for start in starts:
            seen, pending = {start}, [start]
            while pending:
                for dest in edges[pending.pop()] - seen:
                    pending.append(dest)
                    seen.add(dest)
            yield start, flipped, tuple(sorted(physical[i] for i in seen))


def raw_portal_bound(level, triangles):
    size = tris = None
    seen_sets = set()
    for start, flipped, rooms in portal_sets(level, raw=True):
        if rooms in seen_sets:
            continue
        seen_sets.add(rooms)
        total = sum(triangles[i] for i in rooms)
        witness = dict(start=start, flipped=flipped, rooms=len(rooms),
                       room_indices=rooms, triangles=total)
        if size is None or len(rooms) > size['rooms']:
            size = witness
        if tris is None or total > tris['triangles']:
            tris = witness
    return dict(rooms=size, triangles=tris)


def animated_shape_findings(level, manifest):
    """Projected descriptor differences under the renderer's PC shape predicate."""
    active = n64_tex.references(level)[2]
    signatures = {}
    for descriptor in manifest['descriptors']:
        if descriptor['kind'] != 'object':
            continue
        i = descriptor['texture']
        x,y,w,h = descriptor['rect']
        texture = level.object_textures[i]
        uv = tuple((u-x,v-y) for u,v in texture.uv[:descriptor['corners']])
        uv += (uv[0],)*(4-len(uv))
        # PC corner-map is constant 0x24; offsets, cluster and palette are not shape.
        signatures[i] = (w,h,uv,texture.attribute,tuple(t[1] for t in descriptor['tiles'][1:]))
    return [{'group':group, 'mismatched_members':[i for i in group[1:] if signatures[i] != signatures[group[0]]]}
            for group in active if any(signatures[i] != signatures[group[0]] for i in group[1:])]


def streaming(level, manifest, rg, mg, co):
    data = level.data
    descs = {(x['kind'], x['texture']):x['cluster'] for x in manifest['descriptors']}
    groups = n64_tex.animation_groups(level)
    active = defaultdict(set)
    for group in groups:
        for i in group:
            active[i].update(group)

    def clusters(faces, sprites=()):
        ids = {f.texture for f in faces if f.texture is not None}
        pending = list(ids)
        while pending:
            more = active.get(pending.pop(),set()) - ids
            ids.update(more)
            pending.extend(more)
        return {descs['object',i] for i in ids} | {descs['sprite',i] for i in sprites}

    def image_bytes(ids):
        return sum(manifest['clusters'][i]['image_bytes'] for i in ids)

    model, static = model_meshes(level)
    mesh_info = {x['source']:x for x in mg['objects']}
    mesh_clusters = {i:clusters(m.faces) for i,m in level.meshes.items()}
    per_room_items = defaultdict(list)
    for item in data['items']:
        per_room_items[item['room']].append(item['type'])
    sprite_models = {i['type']:range(i['first'],i['first']-i['count']) for i in data['sprite_sequences']}

    def model_record(pointers):
        meshes = [level.meshes[p] for p in pointers]
        unique = set(pointers)
        return dict(mesh_slots=len(pointers), unique_meshes=len(unique),
                    vertices=sum(len(x.vertices) for x in meshes), faces=sum(len(x.faces) for x in meshes),
                    triangles=sum(triangle_count(x.faces) for x in meshes),
                    mesh_bytes=sum(mesh_info[i]['bytes'] for i in unique),
                    texture_images=image_bytes(set().union(*(mesh_clusters[i] for i in unique))))

    movable = {i:model_record(p) for i,p in model.items()}
    statics = {i:model_record([p]) for i,p in static.items()}
    rooms, room_clusters, drawable_clusters, drawable_meshes = [], [], [], []
    for i,r in enumerate(level.rooms):
        fs = r.quads+r.triangles
        image_ids = clusters(fs, (s['texture'] for s in r.sprites))
        room_clusters.append(image_ids)
        placed = [static[s['mesh']] for s in r.static_instances]
        placed += [p for t in per_room_items[i] for p in model.get(t,())]
        unique = set(placed)
        obj_images = set().union(*(mesh_clusters[p] for p in unique))
        for t in per_room_items[i]:
            obj_images.update(descs['sprite',s] for s in sprite_models.get(t,()))
        drawable_clusters.append(image_ids | obj_images)
        drawable_meshes.append(unique)
        rooms.append(dict(room=i, vertices=len(r.vertices), triangles=triangle_count(fs),
                          native_triangles=rg['objects'][i]['triangles'], portals=len(r.portals),
                          geometry_bytes=rg['objects'][i]['bytes'], texture_images=image_bytes(image_ids),
                          placed_mesh_bytes=sum(mesh_info[p]['bytes'] for p in unique),
                          static_mesh_triangles=sum(mesh_info[static[s['mesh']]]['triangles'] for s in r.static_instances),
                          placed_mesh_triangles=sum(mesh_info[p]['triangles'] for p in placed)))

    def workspace(start, flipped, seen):
        totals = {k:sum(rooms[i][k] for i in seen) for k in
                  ('triangles','native_triangles','geometry_bytes','static_mesh_triangles','placed_mesh_triangles')}
        image_ids = set().union(*(room_clusters[i] for i in seen))
        all_images = set().union(*(drawable_clusters[i] for i in seen))
        meshes = set().union(*(drawable_meshes[i] for i in seen))
        mesh_bytes = sum(mesh_info[p]['bytes'] for p in meshes)
        totals['native_with_placed_triangles'] = totals['native_triangles']+totals['placed_mesh_triangles']
        return dict(start=start, flipped=flipped, rooms=len(seen), room_indices=seen, **totals,
                    room_texture_images=image_bytes(image_ids), placed_mesh_bytes=mesh_bytes,
                    draw_image_union=image_bytes(all_images),
                    draw_content_bytes=totals['geometry_bytes']+mesh_bytes+image_bytes(all_images))

    # A portal-reachable component is often the same from every start; sum it once per flip state.
    windows = {}
    for start,flipped,seen in portal_sets(level):
        if (flipped,seen) not in windows:
            windows[flipped,seen] = workspace(start,flipped,seen)
    maxima = {f:max(windows.values(),key=lambda w:w[f],default=None) for f in
              ('rooms','triangles','native_triangles','native_with_placed_triangles','draw_content_bytes')}
    raw_bound = raw_portal_bound(level, [r['triangles'] for r in rooms])
    resident = {k:sum(co['tables'][i] for i in keys) for k,keys in
                [('collision_world',CORE_COLLISION_TABLES),('navigation',CORE_NAV_TABLES),
                 ('animation',CORE_ANIMATION_TABLES),('draw_only_core',CORE_DRAW_TABLES)]}
    resident.update(items=co['tables']['items'], core_keep=co['bytes']-resident['draw_only_core'])
    return dict(**resident, visible_bound=maxima, raw_visible_bound=raw_bound,
                rooms=rooms, movable_models=movable, static_models=statics)


def extensions(level):
    d = level.data
    game = level.reserved['game']
    nr = len(level.rooms)
    vertex = sum(len(r.vertices) for r in level.rooms)
    lights = sum(len(r.lights) for r in level.rooms)
    statics = sum(len(r.static_instances) for r in level.rooms)
    faces = [f for r in level.rooms for f in r.quads+r.triangles] + [f for m in level.meshes.values() for f in m.faces]
    # These source bytes are not extra native allocation. Some are complete retained records.
    return dict(solid_colour_palette=1024, argb1555_pages=131072*len(level.images),
                vertex_legacy_lighting=2*vertex, vertex_attributes=2*vertex,
                vertex_rgb555=2*vertex if game==3 else 0, room_lighting_mode=2*nr if game==2 else 0,
                room_ambient2=2*nr, tr2_light_secondary=6*lights if game==2 else 0,
                tr3_light_colour_type_union=12*lights if game==3 else 0, static_instance_secondary=2*statics,
                tr3_static_rgb555=2*statics if game==3 else 0, tr3_water_reverb_filter=3*nr if game==3 else 0,
                tr3_sector_material_box_words=2*sum(len(r.sectors) for r in level.rooms) if game==3 else 0,
                textured_face_flags=2*sum(bool(f.flags&0x8000) for f in faces if f.texture is not None),
                flat_two_palette_words=2*sum(f.color is not None for f in faces),
                additive_attributes=2*sum(t.attribute==2 for t in level.object_textures),
                tile_flag_words=2*sum(bool(t.reserved['tile_flags']) for t in level.object_textures+level.sprite_textures),
                floor_program=2*len(d['floor_data']), extra_zone_words=2*len(d['extra_ground_zones']),
                packed_frames=2*len(d['frames']), item_secondary=2*len(d['items']),
                cinematic_frames=16*len(d['cinematic_frames']), demo_bytes=len(d['demo_data']),
                extra_sound_map_entries=2*(len(d['sound_map'])-256),
                tr3_sound_range_pitch=2*len(d['sound_details']) if game==3 else 0,
                external_sample_ordinals=4*len(d['sample_ordinals']))


def measure(level, game, bank, sizes, budgets):
    d = level.data
    manifest, tx = textures(level)
    rg = geometry(level, manifest, True, tx['method'])
    mg = geometry(level, manifest, False, tx['method'])
    co = core(level, manifest, game)
    state = game_state(level, game)
    sn = audio(level, bank, sizes)
    stream = streaming(level, manifest, rg, mg, co)
    image = tx['breakdown']['images']
    native = dict(core=co['bytes'],tex=tx['bytes'],rooms=rg['bytes'],meshes=mg['bytes'])
    half = resident_total(dict(native,tex=native['tex']-image+(image+3)//4))
    full = resident_total(native)
    audio_fits = sn['row_bytes']<=budgets['audio']
    base_fit = audio_fits and state['bytes']<=budgets['state']
    split_fit = audio_fits and state['bytes']<=budgets['state']+SPLIT_STATE
    extra = sum(max(0,x-budgets[k]) for k,x in (('level',full),('state',state['bytes']),('audio',sn['row_bytes'])))
    tx_rows, _ = n64_tex.rectangles(level)
    stream.update(draw_only_total=full-stream['core_keep'])
    mesh_data = dict(vertices=sum(len(x.vertices) for x in level.meshes.values()),
                     faces=sum(len(x.faces) for x in level.meshes.values()),
                     triangles=sum(triangle_count(x.faces) for x in level.meshes.values()))
    return dict(input_bytes=level.file_bytes, sha256=level.sha256, version=f'{level.reserved["version"]:#x}',
                sections=len(level.sections), validation_findings=level.reserved['findings'],
                extensions=extensions(level),
                animated_texture_shape_findings=animated_shape_findings(level, manifest),
                counts=dict(rooms=len(level.rooms), room_vertices=sum(len(r.vertices) for r in level.rooms),
                    room_triangles=sum(2*len(r.quads)+len(r.triangles) for r in level.rooms),
                    sectors=sum(len(r.sectors) for r in level.rooms), portals=sum(len(r.portals) for r in level.rooms),
                    lights=sum(len(r.lights) for r in level.rooms),
                    static_instances=sum(len(r.static_instances) for r in level.rooms),
                    static_models=len(d['static_meshes']), movable_models=len(d['models']),
                    physical_meshes=len(level.meshes), mesh_pointers=len(d['mesh_pointers']),
                    mesh_vertices=mesh_data['vertices'], mesh_faces=mesh_data['faces'], mesh_triangles=mesh_data['triangles'],
                    animations=len(d['animations']), frame_bytes=2*len(d['frames']), items=len(d['items']),
                    pages=len(level.images), object_textures=len(level.object_textures), sprite_textures=len(level.sprite_textures),
                    distinct_used_rectangles=len({(r['page'],*r['rect']) for r in tx_rows}),
                    room_sprites=sum(len(r.sprites) for r in level.rooms), sprite_sequences=len(d['sprite_sequences']),
                    floor_words=len(d['floor_data']), boxes=len(d['boxes']), overlaps=len(d['overlaps']),
                    cameras=len(d['cameras']), sound_sources=len(d['sound_sources'])),
                items_by_type=dict(Counter(i['type'] for i in d['items'])), floor=level.reserved['floor_codes'],
                largest_room=max(stream['rooms'],key=lambda r:r['triangles'],default=None),
                largest_room_by_vertices=max(stream['rooms'],key=lambda r:r['vertices'],default=None),
                core=co, textures=tx, rooms={k:v for k,v in rg.items() if k!='objects'},
                meshes={k:v for k,v in mg.items() if k!='objects'}, state=state, sound=sn,
                streaming=stream, native_full=full, native_half=half, expansion_deficit=extra,
                fit=dict(stock=base_fit and full<=budgets['level'],
                         full_reserve=base_fit and full<=budgets['level']+RESERVE,
                         half=base_fit and half<=budgets['level'],
                         half_reserve=base_fit and half<=budgets['level']+RESERVE,
                         split_full=split_fit and full<=budgets['level']+SPLIT_LEVEL,
                         split_half=split_fit and half<=budgets['level']+SPLIT_LEVEL,
                         expansion=extra<=EXPANSION))


def run(root, budgets, games):
    result = dict(format=2, bytes_budget=dict(level=budgets['level'],state=budgets['state'],audio=budgets['audio'],
                    add_reserve=RESERVE,add_expansion=EXPANSION,
                    split_level=SPLIT_LEVEL,split_state=SPLIT_STATE,
                    split_unassigned=RESERVE-SPLIT_LEVEL-SPLIT_STATE), games={})
    for game in games:
        folder = root/f'pc-tr{game}'
        sound_path = folder/'MAIN.SFX'
        bank,sizes,sound = bank_measure(sound_path.read_bytes(),str(sound_path))
        levels, rejected = [], []
        for p in sorted(folder.rglob('*'), key=lambda p:str(p).lower()):
            if p.suffix.lower()!='.tr2':
                continue
            rel = str(p.relative_to(folder))
            raw = p.read_bytes()
            try:
                level = source.read(raw,str(p),bank,census=True)
                if level.reserved['game'] != game:
                    raise FormatError(f'{p}: version at 0 belongs to TR{level.reserved["game"]}')
            except source.TextureOnly as error:
                prefix = error.level
                rejected.append(dict(level=rel, reason=str(error), accounted_bytes=account_level(prefix,raw),
                                     version=f'{prefix.reserved["version"]:#x}', pages=len(prefix.images), sha256=prefix.sha256))
                continue
            except FormatError as error:
                rejected.append(dict(level=rel, reason=str(error)))
                continue
            account_level(level,raw)
            del raw
            row = measure(level,game,bank,sizes,budgets)
            row.update(level=rel, kind='title' if p.stem.lower()=='title' else
                       'cutscene' if p.stem.lower().startswith('cut') else 'map')
            levels.append(row)
            print(f'TR{game} {rel}: {row["input_bytes"]} B; level {row["native_full"]} B; '
                  f'{len(row["validation_findings"])} strict findings',file=sys.stderr,flush=True)
        used = set().union(*(set(r['sound']['referenced_ordinals']) for r in levels))
        counts, ext, items = Counter(), Counter(), Counter()
        codes = {k:Counter() for k in ('functions','actions','trigger_types','opaque_actions')}
        for row in levels:
            counts.update(row['counts'])
            ext.update(row['extensions'])
            items.update(row['items_by_type'])
            for k,h in codes.items():
                h.update(row['floor'][k])
        result['games'][game] = dict(levels=levels, rejected_files=rejected, bank=sound,
                                     totals=dict(counts), extension_totals=dict(ext), items_by_type=dict(items),
                                     floor={k:dict(h) for k,h in codes.items()},
                                     unique_referenced_samples=len(used), unique_referenced_wave_bytes=sum(len(bank[i]) for i in used))
    pc = root/'pc'
    if pc.is_dir():
        bounds, raw_bounds, states = {}, {}, {}
        for p in sorted(pc.iterdir()):
            if p.suffix.lower() != '.phd':
                continue
            level = src_pc.load(p)
            triangles = [2*len(r.quads)+len(r.triangles) for r in level.rooms]
            bounds[level.name] = max((sum(triangles[i] for i in seen)
                                      for _,_,seen in portal_sets(level)),default=0)
            raw_bounds[level.name] = raw_portal_bound(level,triangles)
            if level.name=='gym' or level.name.startswith('level'):
                states[level.name] = game_state(level,1)
        result['tr1_pc_graph_bounds'] = bounds
        result['tr1_pc_raw_graph_bounds'] = raw_bounds
        result['tr1_pc_state_model'] = states
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='small JSON report; never a pack')
    parser.add_argument('--game', type=int, choices=(2,3), action='append')
    args = parser.parse_args()
    # This comparison deliberately takes the pre-D17 stock baseline, not the target's selected rows.
    report = run(args.data,pools(ROOT/'include/tr64/pools.def'),args.game or (2,3))
    args.output.write_text(json.dumps(report,separators=(',',':'))+'\n')


if __name__ == '__main__':
    main()
