# SPDX-License-Identifier: MIT
"""M0 explicit uncompressed layout model, not a native pack writer.
See docs/formats/sizing-m0.md. tiny3d e84172f2: 32 bytes per TWO vertices,
70 vertex cache entries. No empirical inflation factors are used.
"""
import hashlib
import re
from collections import defaultdict
from fractions import Fraction
from pathlib import Path
from binary import FormatError
from pool_rows import pool_rows


def align(n, unit=8):
    return (n+unit-1)//unit*unit


def pools(path=None):
    return {name: size for name, size, kind in pool_rows(path) if kind == 'CARVE'}


def tile_rectangles(width, height, bits, outer_borders=True):
    """A1: every border; A2: internal split borders only, outer clamp assumed."""
    result = []
    side = 1 if outer_borders else 0
    row = align(((width+2*side)*bits+7)//8)
    y = 0
    while y < height:
        top = 1 if outer_borders or y else 0
        remaining = height-y
        bottom = side
        if row*(remaining+top+bottom) <= 2048:
            h = remaining
        else:
            bottom = 1
            h = 2048//row-top-bottom
        if h < 1:
            raise FormatError('texture row cannot fit TMEM with border')
        result.append((0,y,width,h,row*(h+top+bottom)))
        y += h
    return result


def texture_rectangle(level, texture, corners=None):
    uv = texture.uv if corners is None else texture.uv[:corners]
    x0,y0 = min(p[0] for p in uv),min(p[1] for p in uv)
    x1,y1 = max(p[0] for p in uv),max(p[1] for p in uv)
    page = level.images[texture.page]
    pixels = bytearray()
    for y in range(y0,y1+1):
        for x in range(x0,x1+1):
            pixel = page[y*256+x] if level.source == 'pc' else (
                page[y*128+x//2] >> (4*(x&1))) & 15
            pixels.append(pixel)
    return (x0,y0,x1-x0+1,y1-y0+1),bytes(pixels)


def referenced_textures(level):
    objects, sprites = set(),set()
    # The same source object may be used by both triangles and quads. Its
    # rectangle is the union of all USED corners, not a phantom fourth corner.
    corners = {}
    for room in level.rooms:
        for face in room.quads+room.triangles:
            objects.add(face.texture)
            corners[face.texture] = max(corners.get(face.texture,0),len(face.vertices))
        sprites.update(s['texture'] for s in room.sprites)
    for mesh in level.meshes.values():
        for face in mesh.faces:
            if face.texture is not None:
                objects.add(face.texture)
                corners[face.texture] = max(corners.get(face.texture,0),len(face.vertices))
    words = level.data['animated_textures']
    if words:
        cursor = 1
        for _ in range(words[0]):
            if cursor >= len(words):
                raise FormatError(f'{level.name}: truncated animated texture group')
            count = words[cursor]+1
            cursor += 1
            if cursor+count > len(words):
                raise FormatError(f'{level.name}: truncated animated texture members')
            for idx in words[cursor:cursor+count]:
                objects.add(idx)
                corners.setdefault(idx,4)
            cursor += count
        if any(words[cursor:]):
            raise FormatError(f'{level.name}: unexplained animated texture words')
    for seq in level.data['sprite_sequences']:
        sprites.update(range(seq['first'],seq['first']+abs(seq['count'])))
    if any(i is None or i >= len(level.object_textures) for i in objects):
        raise FormatError(f'{level.name}: referenced object texture out of bounds')
    if any(i >= len(level.sprite_textures) for i in sprites):
        raise FormatError(f'{level.name}: referenced sprite texture out of bounds')
    return [('object',i,corners[i]) for i in sorted(objects)]+[
        ('sprite',i,2) for i in sorted(sprites)]


def texture_model(level, bits, outer_borders=True):
    plans = {}
    unique_rectangles = {}
    unique_pixels, palettes = {},set()
    descriptors = 0
    logical_pixels = 0
    for kind,index,corners in referenced_textures(level):
        texture = (level.object_textures if kind == 'object' else level.sprite_textures)[index]
        rect,pixels = texture_rectangle(level,texture,corners)
        x0,y0,w,h = rect
        palette = level.cluts[texture.palette] if level.source == 'psx' else level.palette
        key = (w,h,pixels,palette)
        if key not in unique_rectangles:
            identity = len(unique_rectangles)
            tiles = []
            for tile_index,(x,y,tw,th,size) in enumerate(tile_rectangles(w,h,bits,outer_borders)):
                left = right = 1 if outer_borders else 0
                top = 1 if outer_borders or y else 0
                bottom = 1 if outer_borders or y+th<h else 0
                row = align(((tw+left+right)*bits+7)//8)
                # Native PSX CI4 data can be deduplicated byte-for-byte. PC CI4
                # is an estimate before quantisation; dedup only identical
                # SOURCE index rectangles, never assumed quantisation matches.
                padded = bytes(pixels[min(h-1,max(0,yy))*w+min(w-1,max(0,xx))]
                    for yy in range(y-top,y+th+bottom) for xx in range(x-left,x+tw+right))
                pkey = (tw+left+right,th+top+bottom,padded,row)
                unique_pixels.setdefault(pkey,size)
                tiles.append(dict(x=x0+x,y=y0+y,w=tw,h=th,
                    id=(identity,tile_index),bytes=size,row_bytes=row))
            unique_rectangles[key] = tiles
            logical_pixels += w*h
            descriptors += len(tiles)
            if bits == 4:
                # PC palettes cannot be compared before quantisation. One
                # 16-entry palette per unique rectangle is the stated model.
                palettes.add(palette if level.source == 'psx' else identity)
        tiles = unique_rectangles[key]
        # Identical pixel rectangles at different page coordinates share
        # storage but need the correct SOURCE UV bounds for geometry clipping.
        anchor_x,anchor_y = tiles[0]['x'],tiles[0]['y']
        plans[(kind,index)] = [dict(t,x=t['x']+x0-anchor_x,y=t['y']+y0-anchor_y) for t in tiles]
    palette_bytes = len(palettes)*32 if bits == 4 else (512 if unique_rectangles else 0)
    image_bytes = sum(unique_pixels.values())
    return dict(bytes=image_bytes+palette_bytes+descriptors*32,
        image_bytes=image_bytes,palette_bytes=palette_bytes,
        descriptor_bytes=descriptors*32, referenced_entries=len(plans),
        unique_rectangles=len(unique_rectangles), tiles=descriptors,
        unique_tile_images=len(unique_pixels),logical_pixels=logical_pixels),plans


def interpolate(a,b,t):
    u = a[0]+(b[0]-a[0])*t
    v = a[1]+(b[1]-a[1])*t
    weights = defaultdict(Fraction)
    for i,w in a[2]:
        weights[i] += w*(1-t)
    for i,w in b[2]:
        weights[i] += w*t
    return u,v,tuple((i,w) for i,w in sorted(weights.items()) if w)


def clip(poly, axis, bound, greater):
    if not poly:
        return []
    result = []
    prev = poly[-1]
    prev_in = prev[axis] >= bound if greater else prev[axis] <= bound
    for cur in poly:
        cur_in = cur[axis] >= bound if greater else cur[axis] <= bound
        if cur_in != prev_in:
            t = Fraction(bound-prev[axis],cur[axis]-prev[axis])
            result.append(interpolate(prev,cur,t))
        if cur_in:
            result.append(cur)
        prev,prev_in = cur,cur_in
    return result


def add_face(groups,face,level,plans):
    if face.texture is None:
        key = ('solid',)
        keys = [(index,face.color) for index in face.vertices]
        groups[key].append(tuple(keys[:3]))
        if len(keys)==4:
            groups[key].append((keys[0],keys[2],keys[3]))
        return
    texture = level.object_textures[face.texture]
    from ir import face_uv
    points = [(u,v,((index,Fraction(1)),)) for index,(u,v) in zip(face.vertices,face_uv(level,face))]
    triangles = [points[:3]] if len(points)==3 else [points[:3],[points[0],points[2],points[3]]]
    tiles = plans[('object',face.texture)]
    for tile in tiles:
        for triangle in triangles:
            if len(tiles)==1:
                poly = triangle
            else:
                # Continuous UV domains meet midway between adjacent texels;
                # borders duplicate edge samples. Outer boundaries enclose
                # all source vertex coordinates.
                poly = triangle
                for axis,lower,upper in ((0,Fraction(2*tile['x']-1,2),Fraction(2*(tile['x']+tile['w'])-1,2)),
                                         (1,Fraction(2*tile['y']-1,2),Fraction(2*(tile['y']+tile['h'])-1,2))):
                    poly = clip(poly,axis,lower,True)
                    poly = clip(poly,axis,upper,False)
            if len(poly)<3:
                continue
            # Translate UV into a tile-local coordinate. Distinct rectangles
            # sharing image storage still need distinct UV corner keys.
            keys = [(p[0]-tile['x'],p[1]-tile['y'],p[2]) for p in poly]
            material = (tile['id'],texture.attribute,face.flags & 0x8000)
            for i in range(1,len(keys)-1):
                if len(tiles)>1:
                    # Test geometric area in the original triangle's
                    # barycentric domain, NOT UV area: a real triangle may
                    # deliberately have collinear/constant texture UVs.
                    ia,ib = triangle[0][2][0][0],triangle[1][2][0][0]
                    bary = [(dict(p[2]).get(ia,0),dict(p[2]).get(ib,0))
                            for p in (poly[0],poly[i],poly[i+1])]
                    if ((bary[1][0]-bary[0][0])*(bary[2][1]-bary[0][1]) ==
                        (bary[1][1]-bary[0][1])*(bary[2][0]-bary[0][0])):
                        continue
                groups[material].append((keys[0],keys[i],keys[i+1]))


def batch_groups(groups):
    total = dict(bytes=0,vertices=0,vertex_bytes=0,triangles=0,index_bytes=0,
                 batches=0,batch_header_bytes=0,cache_duplication=0)
    for triangles in groups.values():
        vertices, count = set(),0
        unique = set(v for tri in triangles for v in tri)
        group_vertices = 0

        def flush():
            nonlocal count, group_vertices
            if not count:
                return
            nv = len(vertices)
            vb = ((nv+1)//2)*32
            ib = align(count*3)
            total['vertices'] += nv
            total['vertex_bytes'] += vb
            total['triangles'] += count
            total['index_bytes'] += ib
            total['batches'] += 1
            total['batch_header_bytes'] += 32
            total['bytes'] += vb+ib+32
            group_vertices += nv

        for triangle in triangles:
            incoming = set(triangle)
            if len(vertices|incoming)>70:
                flush()
                vertices,count = set(),0
            vertices.update(incoming)
            count += 1
        flush()
        total['cache_duplication'] += group_vertices-len(unique)
    return total


def geometry_model(level,plans,rooms):
    result = dict(bytes=0,vertices=0,vertex_bytes=0,triangles=0,index_bytes=0,
                  batches=0,batch_header_bytes=0,cache_duplication=0,
                  source_vertices=0,source_triangles=0,object_header_bytes=0,
                  tile_uv_duplication=0)
    per_object = []
    objects = level.rooms if rooms else level.meshes.values()
    for object_index,obj in enumerate(objects):
        groups = defaultdict(list)
        obj_faces = obj.quads+obj.triangles if rooms else obj.faces
        for face in obj_faces:
            add_face(groups,face,level,plans)
        if rooms:
            # Billboards need four independently UV'd vertices; the eventual
            # runtime may generate them instead. Their cost is not omitted.
            for index,sprite in enumerate(obj.sprites):
                for tile in plans[('sprite',sprite['texture'])]:
                    keys = [('sprite',index,tile['id'],corner) for corner in range(4)]
                    groups[('sprite',tile['id'])].extend([(keys[0],keys[1],keys[2]),(keys[0],keys[2],keys[3])])
        batch = batch_groups(groups)
        for key,value in batch.items():
            result[key] += value
        source_used = set(i for face in obj_faces for i in face.vertices)
        result['source_vertices'] += len(obj.vertices)
        result['source_triangles'] += sum(len(f.vertices)-2 for f in obj_faces)
        # Tile/UV splitting and clipping are measured before cache batching;
        # unused source vertices are not treated as negative duplication.
        split_vertices = batch['vertices']-batch['cache_duplication']
        result['tile_uv_duplication'] += split_vertices-len(source_used)
        result['object_header_bytes'] += 32
        result['bytes'] += 32
        per_object.append(dict(object=object_index if rooms else obj.source_offset,
            **batch,object_header_bytes=32,
            source_vertices=len(obj.vertices),
            source_triangles=sum(len(f.vertices)-2 for f in obj_faces)))
    result['per_object'] = per_object
    return result


# Proposed canonical resident strides. Counts are IR element counts, not
# PC/PSX disk sizes. Every assumption is exposed in the report/document.
CORE_STRIDES = dict(floor_data=2,mesh_pointers=4,animations=32,states=6,ranges=8,
    commands=2,bones=4,frames=2,models=20,static_meshes=32,sprite_sequences=8,
    cameras=16,sound_sources=16,boxes=20,overlaps=2,zones=2,
    animated_textures=2,items=24,cinematic_frames=16,sound_map=2,sound_details=8)
LEVEL_SECTIONS = ('core','tex','rooms','meshes')


def core_model(level):
    entries = {name:len(level.data[name])*stride for name,stride in CORE_STRIDES.items()}
    entries.update(level_header=128,room_headers=len(level.rooms)*64,
        sectors=sum(len(r.sectors)*8 for r in level.rooms),
        portals=sum(len(r.portals)*32 for r in level.rooms),
        lights=sum(len(r.lights)*20 for r in level.rooms),
        static_instances=sum(len(r.static_instances)*20 for r in level.rooms),
        room_sprites=sum(len(r.sprites)*4 for r in level.rooms),
        object_texture_table=len(level.object_textures)*24,
        sprite_texture_table=len(level.sprite_textures)*24,
        demo_data=len(level.data['demo_data'] or b''),
        sample_directory=len(level.samples)*8)
    # Each contiguous table begins on an 8-byte boundary.
    aligned = {name:align(size) for name,size in entries.items()}
    return dict(bytes=sum(aligned.values()),animation_frames=entries['frames'],
                tables=entries,aligned_tables=aligned)


def resident_total(section_bytes):
    """The level arena holds four informational sections, each 16-byte aligned."""
    return sum(align(section_bytes[name],16) for name in LEVEL_SECTIONS)


def level_allocation(section_bytes,budget):
    needed = resident_total(section_bytes)
    return dict(needed_bytes=needed,budget_bytes=budget,over_bytes=max(0,needed-budget),
                headroom_bytes=budget-needed,fits=needed<=budget)


def measure(level,budgets):
    import model_b
    import model_c
    tex,plans = texture_model(level,4)
    tex8 = texture_model(level,8)[0] if level.source == 'pc' else None
    core = core_model(level)
    room = geometry_model(level,plans,True)
    mesh = geometry_model(level,plans,False)
    tex2,plans2 = texture_model(level,4,False)
    tex82 = texture_model(level,8,False)[0] if level.source=='pc' else None
    room2,mesh2 = geometry_model(level,plans2,True),geometry_model(level,plans2,False)
    needed2 = dict(core=core['bytes'],tex=tex2['bytes'],rooms=room2['bytes'],meshes=mesh2['bytes'])
    needed = dict(core=core['bytes'],tex=tex['bytes'],rooms=room['bytes'],meshes=mesh['bytes'])
    samples = sum(len(s) for s in level.samples)
    # Effect bytes are not predicted from a codec we have not implemented.
    # Include source WAVE/SPU bytes, uncompressed, as the conservative column.
    cart = resident_total(needed)+align(samples,16)+128
    return dict(source=level.source,level=level.name,sha256=level.sha256,
        sections=needed,pools={'level':level_allocation(needed,budgets['level'])},
        core=core,textures_ci4=tex,
        textures_ci8=tex8,room_geometry=room,mesh_geometry=mesh,
        cart_bytes=cart,cart_source_effect_bytes=samples,
        effect_sample_hashes=[dict(sha256=hashlib.sha256(s).hexdigest(),bytes=len(s)) for s in level.samples],
        model_a2=dict(sections=needed2,pools={'level':level_allocation(needed2,budgets['level'])},
            textures_ci4=tex2,textures_ci8=tex82,room_geometry=room2,mesh_geometry=mesh2,
            cart_bytes=resident_total(needed2)+align(samples,16)+128),
        model_b=dict(rooms=model_b.variants(level,True,room,room2),
                     meshes=model_b.variants(level,False,mesh,mesh2)),
        model_c=model_c.measure(level),
        cart_resident_bytes=resident_total(needed))


def summary(levels,budgets):
    worst = {}
    failures = {}
    for pool in ('level',):
        top = max(levels,key=lambda row:row['pools'][pool]['needed_bytes'])
        worst[pool] = dict(level=top['level'],source=top['source'],**top['pools'][pool])
        maximum = top['pools'][pool]['needed_bytes']
        worst[pool]['ties'] = [dict(level=r['level'],source=r['source']) for r in levels if r['pools'][pool]['needed_bytes']==maximum]
        worst[pool]['headroom_bytes'] = budgets[pool]-maximum
        failures[pool] = [dict(level=row['level'],source=row['source'],
            over_bytes=row['pools'][pool]['over_bytes']) for row in levels if not row['pools'][pool]['fits']]
    carts = {}
    for source in ('pc','psx'):
        rows = [row for row in levels if row['source']==source]
        total = sum(row['cart_bytes'] for row in rows)
        source_effect_bytes = sum(align(row['cart_source_effect_bytes'],16) for row in rows)
        unique = {s['sha256']:s['bytes'] for row in rows for s in row['effect_sample_hashes']}
        unique_bytes = sum(align(n,16) for n in unique.values())
        dedup = total-source_effect_bytes+unique_bytes
        a2 = sum(row['model_a2']['cart_bytes'] for row in rows)
        dedup2 = a2-source_effect_bytes+unique_bytes
        carts[source] = dict(bytes=total,budget_bytes=24*1024*1024,fits=total<=24*1024*1024,
            over_bytes=max(0,total-24*1024*1024),sound_deduplicated_bytes=dedup,
            unique_samples=len(unique),unique_sample_aligned_bytes=unique_bytes,
            undeduplicated_sample_aligned_bytes=source_effect_bytes,
            sound_deduplicated_fits=dedup<=24*1024*1024,
            model_a2_bytes=a2,model_a2_sound_deduplicated_bytes=dedup2,
            model_a2_fits=a2<=24*1024*1024,model_a2_sound_deduplicated_fits=dedup2<=24*1024*1024)
    return dict(worst=worst,failures=failures,cart=carts)


def markdown(report):
    import report_models
    lines = ['# M0 sizing measurements', '',
        'Model: `docs/formats/sizing-m0.md`; **uncompressed estimates, not produced',
        'native packs**. Exact partition/count arithmetic under the stated model.',
        'PC CI4 quantisation quality and final audio codecs are NOT measured.', '',
        '## Model A1 — stock tiny3d, filtering border on every edge (assumption)', '',
        'Core/texture/room/mesh columns are section sizes, not separate pools.',
        'The only resident fit test is their individually 16-byte-aligned sum against CARVE level.', '',
        '| Level | Source | Core KiB | Frames KiB (in core) | CI4 KiB | CI8 KiB | Rooms KiB | Meshes KiB | Arena KiB | Headroom B | Fits arena | Cart KiB |',
        '|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|---:|']
    for row in report['levels']:
        p = row['sections']
        allocation = row['pools']['level']
        ci8 = f'{row["textures_ci8"]["bytes"]/1024:.3f}' if row['textures_ci8'] else 'absent'
        lines.append(f'| {row["level"]} | {row["source"]} | {p["core"]/1024:.3f} | '
            f'{row["core"]["animation_frames"]/1024:.3f} | {p["tex"]/1024:.3f} | '
            f'{ci8} | {p["rooms"]/1024:.3f} | {p["meshes"]/1024:.3f} | '
            f'{allocation["needed_bytes"]/1024:.3f} | {allocation["headroom_bytes"]} | '
            f'{"yes" if allocation["fits"] else "NO"} | {row["cart_bytes"]/1024:.3f} |')
    lines += ['', '## Model A1 shared level arena failures (exact bytes)', '']
    for pool, failures in report['summary']['failures'].items():
        lines.append(f'- **{pool}:** '+('every level from both sources fits.' if not failures else
            '; '.join(f'{x["level"]}/{x["source"]}: +{x["over_bytes"]} B' for x in failures)+'.'))
    lines += ['',report_models.markdown(report)]
    lines += ['', '## Worst cases and cart budget', '',
        'Cart rows sum every available source level. Undeduplicated A1 remains intact;',
        'the last column deduplicates only encoded sounds by content hash per source.',
        'A2 aggregate carts are separate rows with the same sound dedup policy.',
        'All resident candidate margins compare total level bytes with the shared level row.',
        'B needed column is total lower / file-order / locality KiB; core and A1 textures remain charged.',
        'C replaces only A1 textures; its raw image columns have no separate texture cap.',
        'Ratios are transform counts, not speed.',
        'Other cart rows reserve 40 MiB (1 image/table +20 music +16 video +3 reserve).', '',
        '| Model / binding arena or cart | Row KiB | Worst / ties / source aggregate | Needed KiB (B: total lower/file/locality) | Headroom B | Verdict | Sound-dedup cart KiB |',
        '|---|---:|---|---|---|---|---:|']
    for pool,row in report['summary']['worst'].items():
        ties = f'; {len(row["ties"])}-way tie' if len(row['ties'])>1 else ''
        verdict = 'fits, ZERO headroom' if row['headroom_bytes']==0 else 'fits model' if row['fits'] else 'does not fit model'
        lines.append(f'| A1.{pool} | {row["budget_bytes"]/1024:g} | {row["level"]}/{row["source"]}{ties} | '
                     f'{row["needed_bytes"]/1024:.3f} | {row["headroom_bytes"]} | {verdict} | — |')
    lines += report_models.worst_rows(report)
    for source,row in report['summary']['cart'].items():
        for model in ('A1','A2'):
            total = row['bytes'] if model=='A1' else sum(r['model_a2']['cart_bytes'] for r in report['levels'] if r['source']==source)
            dedup = total-row['undeduplicated_sample_aligned_bytes']+row['unique_sample_aligned_bytes']
            lines.append(f'| {model} levels cart | 24576 | all {source} levels | {total/1024:.3f} | '
                f'{24*1024*1024-total} | undedup {"fits" if total<=24*1024*1024 else "NO"}; dedup {"fits" if dedup<=24*1024*1024 else "NO"} | {dedup/1024:.3f} |')
            complete,complete_dedup = total+40*1024*1024,dedup+40*1024*1024
            lines.append(f'| {model} complete cart allocation | 65536 | {source} levels + other budget rows | '
                f'{complete/1024:.3f} | {64*1024*1024-complete} | undedup {"fits" if complete<=64*1024*1024 else "NO"}; '
                f'dedup {"fits" if complete_dedup<=64*1024*1024 else "NO"} | {complete_dedup/1024:.3f} |')
    return '\n'.join(lines)+'\n'
