# SPDX-License-Identifier: MIT
"""RG64/MG64 geometry writer; see docs/formats/n64-geo.md."""
import hashlib
import json
import struct
from collections import defaultdict
from fractions import Fraction as F
from pathlib import Path

import ir
from sizing import pools
from pool_rows import pool_table


def rnd(x):
    x = F(x)
    return (1 if x >= 0 else -1) * ((2 * abs(x.numerator) + x.denominator) // (2*x.denominator))


def partition(faces):
    sets = [set(f) for f in faces]
    incident = defaultdict(set)
    for i, face in enumerate(sets):
        for v in face:
            incident[v].add(i)
    remaining = set(range(len(faces)))
    while remaining:
        vertices, selected, candidates = set(), [], set()
        while remaining:
            fitting = [i for i in candidates & remaining if len(vertices | sets[i]) <= 70]
            if fitting:
                choice = min(fitting, key=lambda i: (-len(vertices & sets[i]), len(sets[i]-vertices), i))
            else:
                choice = min(remaining)
                if len(vertices | sets[choice]) > 70:
                    break
            remaining.remove(choice)
            selected.append(choice)
            vertices.update(sets[choice])
            candidates.discard(choice)
            for v in sets[choice]:
                candidates.update(incident[v])
        yield sorted(vertices), selected


def gain(level, obj, i, room):
    if room:
        light = obj.vertices[i]['lighting']
        d = 8191 - (light['value'] << 5) if light['encoding'] == 'psx_vertex_8bit' else light['value']
    else:
        d = obj.lighting[i] if obj.lighting is not None else 4096
    return max(F(0), min(F(2), F(8191-d, 4096)))


def colour(g, room, water):
    if room:
        g = min(g, F(3, 2))
        tint = (F(1,2), F(4,5), F(4,5)) if water else (1,1,1)
        return tuple(min(255, max(0, rnd(F(512,3)*g*t))) for t in tint) + (255,)
    c = min(255, max(0, rnd(128*g)))
    return (c,c,c,255)


def tr3_rgb(value):
    return (F((value >> 10) & 31, 31), F((value >> 5) & 31, 31), F(value & 31, 31))


def colour_tr3(rgb, water):
    tint = (F(1,2), F(4,5), F(4,5)) if water else (1,1,1)
    return tuple(min(255, max(0, rnd(F(512,3)*c*t))) for c,t in zip(rgb, tint)) + (255,)


def bounds(vertices):
    if not vertices:
        return (0,)*6
    return tuple(min(v[0][i] for v in vertices) for i in range(3)) + tuple(max(v[0][i] for v in vertices) for i in range(3))


def packmap(values):
    values = list(values)
    if len(values) == 3:
        values.append(values[0])
    return sum(v << (2*i) for i,v in enumerate(values))


def st_word(u, v):
    s, t = rnd(32*u)+16, rnd(32*v)+16
    if not -32768 <= s <= 32767 or not -32768 <= t <= 32767:
        raise ValueError('geometry ST exceeds signed 16-bit range')
    return ((s & 65535) << 16) | (t & 65535)


def clip(poly, axis, edge, keep_above):
    out = []
    for a,b in zip(poly, poly[1:]+poly[:1]):
        da, db = a[axis]-edge, b[axis]-edge
        ia, ib = (da >= 0, db >= 0) if keep_above else (da <= 0, db <= 0)
        if ia:
            out.append(a)
        if ia != ib:
            t = da/(da-db)
            out.append(tuple(x+(y-x)*t for x,y in zip(a,b)))
    # Intersections at an existing vertex must not duplicate it.
    result = []
    for p in out:
        if not result or p != result[-1]:
            result.append(p)
    if len(result)>1 and result[0] == result[-1]:
        result.pop()
    return result


def area(poly):
    # Geometry can be vertical, so use its 3D cross product rather than UV area.
    a = poly[0]
    for i in range(1,len(poly)-1):
        b,c = poly[i],poly[i+1]
        u = [b[j]-a[j] for j in range(3)]
        v = [c[j]-a[j] for j in range(3)]
        if any((u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0])):
            return True
    return False


def section(level, tm, room, split_enabled=True):
    objects = list(enumerate(level.rooms)) if room else sorted(level.meshes.items())
    desc = {r['texture']:r for r in tm['descriptors'] if r['kind']=='object'}
    flat = {row[0]:i for i,row in enumerate(tm['flat_colours'])}
    batches, runs, faces, objects_out = [], [], [], []
    split_count = 0
    triangles = 0
    for oi,(source,obj) in enumerate(objects):
        fs = obj.quads + obj.triangles if room else obj.faces
        positions = [tuple(v[k] for k in ('x','y','z')) for v in obj.vertices] if room else obj.vertices
        tr3_room = room and level.reserved.get('game') == 3 and bool(obj.vertices) and obj.vertices[0]['lighting']['encoding'] == 'tr3_rgb555'
        water = room and bool(obj.flags & 1)
        if tr3_room:
            rgbs = [tr3_rgb(v['lighting']['value']) for v in obj.vertices]
            gs = None
            source_vertices = [(tuple(p), colour_tr3(rgbs[i], water)) for i,p in enumerate(positions)]
        else:
            rgbs = None
            gs = [gain(level,obj,i,room) for i in range(len(positions))]
            source_vertices = [(tuple(p), colour(gs[i],room,water)) for i,p in enumerate(positions)]
        first = len(batches)
        first_run, first_face, first_triangles = len(runs), len(faces), triangles
        deferred = []
        object_vertices = []
        def emit_batch(vertices, pieces):
            nonlocal triangles
            bi = len(batches)
            object_vertices.extend(vertices)
            grouped = defaultdict(list)
            for key, face in pieces:
                grouped[key[:-1]].append((face,key[-1]))
            batch_runs = []
            for key in sorted(grouped):
                cls,palette,isflat,material,tile,side,st = key
                ff = sorted((f for f,_ in grouped[key]), key=lambda f:f[6])
                eligible = int(all(e for _,e in grouped[key]))
                flags = cls | (side << 2) | (isflat << 3) | (eligible << 4)
                batch_runs.append(dict(material=material,tile=tile,flags=flags,batch=bi,st=st,faces=len(ff),triangles=sum(1 if f[3] == 255 else 2 for f in ff),palette=palette))
                faces.extend(ff)
                triangles += sum(1 if f[3] == 255 else 2 for f in ff)
            runs.extend(batch_runs)
            batches.append(dict(vertices=vertices, object=oi, runs=len(batch_runs), run_records=batch_runs))
        for indices,selected in partition([f.vertices for f in fs]):
            local = {v:i for i,v in enumerate(indices)}
            pieces = []
            for fi in selected:
                f = fs[fi]
                side = int(bool(f.flags & 0x8000))
                eligible = 1 if tr3_room else int(all(gs[i] <= 1 for i in f.vertices))
                if f.texture is None:
                    material = flat[f.color]
                    key = (0,65535,1,material,0,side,(0,0,0,0),eligible)
                    vi = [local[i] for i in f.vertices]
                    pieces.append((key,tuple(vi + ([255] if len(vi)==3 else []) + [0,0,fi])))
                    continue
                r = desc[f.texture]
                material = tm['object_descriptor_map'][str(f.texture)]
                x,y,w,h = r['rect']
                dx = x-tm['clusters'][r['cluster']]['x']
                if dx & 1 or ((dx+w) & 1 and level.reserved.get('game') not in (2, 3)):
                    raise ValueError(f'{level.name} {source} face {fi}: odd texture endpoint')
                uv = [(u-x,v-y) for u,v in ir.face_uv(level,f)]
                order = (0,1,3,2) if level.source == 'psx' else (0,1,2,3)
                inverse = {slot:i for i,slot in enumerate(order)}
                srcmap = [inverse[i] for i in f.texture_corners]
                if split_enabled and len(r['tiles']) > 1:
                    deferred.append((fi,f,r,uv,srcmap,side,material))
                    continue
                raw = level.object_textures[f.texture].uv
                native_uv = [raw[i] if i < r['corners'] else raw[0] for i in order]
                st = tuple(st_word(u-x,v-y) for u,v in native_uv)
                key = (level.object_textures[f.texture].attribute,r['palette'],0,material,0,side,st,eligible)
                vi = [local[i] for i in f.vertices]
                for a in range(len(vi)):
                    for b in range(a):
                        if vi[a] == vi[b] and st[srcmap[a]] != st[srcmap[b]]:
                            raise ValueError(f'{level.name} {source} face {fi}: repeated vertex with distinct ST')
                pieces.append((key,tuple(vi + ([255] if len(vi)==3 else []) + [packmap(srcmap),packmap(srcmap),fi])))
            emit_batch([source_vertices[i] for i in indices],pieces)
        if deferred:
            generated, vertex_ids, pieces = [], {}, []
            for fi,f,r,uv,srcmap,side,material in deferred:
                split_count += 1
                for triangle in ((0,1,2),(0,2,3)) if len(f.vertices)==4 else ((0,1,2),):
                    if tr3_room:
                        original = [tuple(map(F, positions[f.vertices[i]])) + rgbs[f.vertices[i]] + (F(uv[i][0]),F(uv[i][1])) for i in triangle]
                        v_axis = 7
                    else:
                        original = [tuple(map(F, positions[f.vertices[i]])) + (gs[f.vertices[i]], F(uv[i][0]),F(uv[i][1])) for i in triangle]
                        v_axis = 5
                    for ti,tile in enumerate(r['tiles']):
                        start,height = tile[1],tile[3]
                        p = clip(original,v_axis,F(2*start-1,2),True) if start else original
                        if start+height < r['rect'][3]:
                            p = clip(p,v_axis,F(2*(start+height)-1,2),False) if p else []
                        if len(p)<3 or not area(p):
                            continue
                        polygons = [p] if len(p)<=4 else [[p[0],p[j],p[j+1]] for j in range(1,len(p)-1)]
                        for poly in polygons:
                            vi=[]
                            for q in poly:
                                vert = (tuple(rnd(z) for z in q[:3]),colour_tr3(q[3:6],water) if tr3_room else colour(q[3],room,water))
                                if vert not in vertex_ids:
                                    vertex_ids[vert]=len(generated)
                                    generated.append(vert)
                                vi.append(vertex_ids[vert])
                            st = tuple(st_word(q[6],q[7]-start) if tr3_room else st_word(q[4],q[5]-start) for q in poly)
                            if len(st)==3: st += (st[0],)
                            for tri in ((0,1,2),) if len(vi)==3 else ((0,1,2),(0,2,3)):
                                for a in tri:
                                    for b in tri:
                                        if a < b and vi[a]==vi[b] and st[a]!=st[b]:
                                            raise ValueError(f'{level.name} {source} face {fi}: repeated split vertex with distinct ST')
                            eligible = 1 if tr3_room else int(all(q[3]<=1 for q in poly))
                            key=(level.object_textures[f.texture].attribute,r['palette'],0,material,ti,side,st,eligible)
                            # Source corners identify the unsplit polygon, not intersections.
                            sm = packmap(srcmap)
                            pieces.append((key,tuple(vi+([255] if len(vi)==3 else [])+[packmap(range(len(vi))),sm,fi])))
            if pieces:
                for used, selection in partition([p[1][:3] if p[1][3]==255 else p[1][:4] for p in pieces]):
                    remap={v:i for i,v in enumerate(used)}
                    pp=[]
                    for j in selection:
                        key,face=pieces[j]
                        pp.append((key,tuple([remap[v] if v != 255 else 255 for v in face[:4]])+face[4:]))
                    emit_batch([generated[i] for i in used],pp)
        subset = batches[first:]
        objects_out.append(dict(source=source,first=first,count=len(subset),bounds=bounds(object_vertices),
            vertices=sum(len(b['vertices']) for b in subset),runs=len(runs)-first_run,
            faces=len(faces)-first_face,triangles=triangles-first_triangles,
            bytes=32+32*len(subset)+24*(len(runs)-first_run)+8*(len(faces)-first_face)
                +sum(32*((len(b['vertices'])+1)//2) for b in subset),split_sources=len(deferred)))
    if max(len(objects),len(batches),len(runs))>65535:
        raise ValueError('geometry table exceeds u16')
    align=lambda n:(n+7)&~7
    oo=64; bo=align(oo+32*len(objects)); ro=align(bo+32*len(batches)); fo=align(ro+24*len(runs)); vo=align(fo+8*len(faces))
    blob=bytearray(vo)
    for i,o in enumerate(objects_out):
        struct.pack_into('>IIHH6h2I',blob,oo+32*i,o['source'],o['first'],o['count'],0,*o['bounds'],0,0)
    payload=bytearray(); pair_count=0
    for i,b in enumerate(batches):
        verts=b['vertices']; offset=vo+len(payload)
        for j in range(0,len(verts),2):
            a=verts[j]; c=verts[j+1] if j+1<len(verts) else ((0,0,0),(0,0,0,0))
            payload.extend(struct.pack('>3hH3hH4B4B4H',*a[0],0,*c[0],0,*a[1],*c[1],0,0,0,0))
            pair_count+=1
        struct.pack_into('>6hHHIHH2I',blob,bo+32*i,*bounds(verts),len(verts),b['object'],offset,b['runs'],0,0,0)
    for i,r in enumerate(runs):
        struct.pack_into('>HBBHH4I',blob,ro+24*i,r['material'],r['tile'],r['flags'],r['batch'],r['faces'],*r['st'])
    for i,f in enumerate(faces): struct.pack_into('>4BBBH',blob,fo+8*i,*f)
    blob.extend(payload)
    struct.pack_into('>4s15I',blob,0,b'RG64' if room else b'MG64',1,len(blob),len(objects),len(batches),len(runs),len(faces),pair_count,oo,bo,ro,fo,vo,triangles,1,0)
    info=dict(bytes=len(blob),objects=len(objects),batches=len(batches),runs=len(runs),faces=len(faces),vertices=sum(len(b['vertices']) for b in batches),pairs=pair_count,triangles=triangles,split_sources=split_count,object_records=objects_out,batch_records=[dict(object=b['object'],vertices=len(b['vertices']),runs=b['runs'],faces=sum(r['faces'] for r in b['run_records']),triangles=sum(r['triangles'] for r in b['run_records']),upload_keys=[[r[k] for k in ('material','tile','flags','palette')] for r in b['run_records']]) for b in batches], breakdown=dict(header=64,objects=32*len(objects),batches=32*len(batches),runs=24*len(runs),faces=8*len(faces),vertices=len(payload),alignment=vo-(64+32*len(objects)+32*len(batches)+24*len(runs)+8*len(faces))))
    return bytes(blob), info


def cook(level, tex_manifest):
    rooms,rm = section(level,tex_manifest,True)
    meshes,mm = section(level,tex_manifest,False)
    for is_room, blob, info in ((True,rooms,rm),(False,meshes,mm)):
        before = len(section(level,tex_manifest,is_room,False)[0]) if info['split_sources'] else len(blob)
        info['pre_split_bytes'] = before
        info['split_bytes'] = len(blob)-before
    base = Path(__file__).resolve().parent
    game = level.reserved.get('game', 1)
    pool_path = pool_table(game if game in (1, 2, 3) else None)
    limits = pools(pool_path)
    # These are only two components; complete-level assembly owns the fit gate.
    if ((len(rooms)+15)&~15)+((len(meshes)+15)&~15)>limits['level']:
        raise ValueError(f'{level.source} {level.name}: geometry alone exceeds level arena {limits["level"]} bytes')
    digest = hashlib.sha256()
    for path in [base/n for n in ('n64_geo.py','ir.py','binary.py','decode.py','src_pc.py','src_psx.py','sizing.py','pool_rows.py')] + [pool_path]:
        digest.update(path.name.encode()+path.read_bytes())
    return rooms,meshes,dict(source=level.source,level=level.name,source_sha256=level.sha256,texture_sha256=tex_manifest['section_sha256'],cook_sha256=digest.hexdigest(),rooms=rm,meshes=mm,level_budget_bytes=limits['level'])
