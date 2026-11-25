# SPDX-License-Identifier: MIT
"""Candidate B; explicit lower bound and two complete cache partitions.
Neither stock tiny3d nor Magma is claimed to implement this per-corner UV ABI.
"""
from collections import defaultdict


def source_faces(obj,rooms):
    fs = obj.quads+obj.triangles if rooms else obj.faces
    faces = [tuple(f.vertices) for f in fs]
    if rooms:
        faces += [tuple(('sprite',i,c) for c in range(4)) for i,_ in enumerate(obj.sprites)]
    return faces


def partition(faces,cache_size,locality):
    sets = [set(f) for f in faces]
    if not locality:
        result,vertices,count = [],set(),0
        for face in sets:
            if len(vertices|face)>cache_size:
                result.append((len(vertices),count))
                vertices,count = set(),0
            vertices.update(face)
            count += 1
        if count:
            result.append((len(vertices),count))
        return result
    incident = defaultdict(set)
    for i,face in enumerate(sets):
        for vertex in face:
            incident[vertex].add(i)
    remaining = set(range(len(faces)))
    result = []
    while remaining:
        vertices,selected,candidates = set(),[],set()
        while True:
            available = candidates&remaining
            fitting = [i for i in available if len(vertices|sets[i])<=cache_size]
            if fitting:
                # Max shared vertices, then minimum new vertices, then stable
                # source order. This grows along actual vertex adjacency.
                choice = min(fitting,key=lambda i:(-len(vertices&sets[i]),len(sets[i]-vertices),i))
            else:
                choice = min(remaining)
                if len(vertices|sets[choice])>cache_size:
                    break
            remaining.remove(choice)
            selected.append(choice)
            vertices.update(sets[choice])
            candidates.discard(choice)
            for vertex in sets[choice]:
                candidates.update(incident[vertex])
            if not remaining:
                break
        result.append((len(vertices),len(selected)))
    return result


def totals(obj,faces,batches,vertex_size):
    before = len(set(v for face in faces for v in face))
    after = sum(nv for nv,_ in batches)
    vb = sum(((nv+1)//2)*32 if vertex_size==16 else nv*8 for nv,_ in batches)
    fb,hb = len(faces)*8,len(batches)*32
    return dict(source_vertices=len(obj.vertices),shared_before_batching=before,
        shared_after_batching=after,boundary_duplication=after-before,vertex_loads=after,
        faces=len(faces),batches=len(batches),vertex_bytes=vb,face_bytes=fb,
        batch_header_bytes=hb,object_header_bytes=32,bytes=vb+fb+hb+32,
        batch_counts=[dict(vertices=nv,faces=nf) for nv,nf in batches])


def object_model(obj,rooms,vertex_size=16,cache_size=70,policy='greedy'):
    faces = source_faces(obj,rooms)
    return totals(obj,faces,partition(faces,cache_size,policy=='locality'),vertex_size)


def compare_a(row,a1,a2):
    for label,a in [('a',a1),('a2',a2)]:
        row[f'model_{label}_vertex_loads'] = a['vertices']
        row[f'{label}_to_b_transform_ratio'] = a['vertices']/row['vertex_loads'] if row['vertex_loads'] else None
        row[f'{label}_transform_saving_fraction'] = 1-row['vertex_loads']/a['vertices'] if a['vertices'] else None


def aggregate(rows):
    keys = ('bytes','source_vertices','shared_before_batching','shared_after_batching',
        'boundary_duplication','vertex_loads','faces','batches','vertex_bytes','face_bytes',
        'batch_header_bytes','object_header_bytes','model_a_vertex_loads','model_a2_vertex_loads')
    out = {k:sum(row[k] for row in rows) for k in keys}
    for label in ('a','a2'):
        av = out[f'model_{label}_vertex_loads']
        out[f'{label}_to_b_transform_ratio'] = av/out['vertex_loads'] if out['vertex_loads'] else None
        out[f'{label}_transform_saving_fraction'] = 1-out['vertex_loads']/av if av else None
    out['per_object'] = rows
    return out


def variants(level,rooms,model_a,model_a2):
    objects = list(level.rooms) if rooms else list(level.meshes.values())
    prepared = [(obj,source_faces(obj,rooms)) for obj in objects]
    partitions = {(i,cache,policy):partition(faces,cache,policy=='locality')
        for i,(_,faces) in enumerate(prepared) for cache in (70,48) for policy in ('greedy','locality')}
    result = {}
    for vertex_size,cache_size in ((16,70),(16,48),(8,70),(8,48)):
        policies = {}
        for policy in ('greedy','locality','lower_bound'):
            rows = []
            for i,(obj,faces) in enumerate(prepared):
                if policy=='lower_bound':
                    unique = len(set(v for f in faces for v in f))
                    minimum_batches = (unique+cache_size-1)//cache_size
                    # Optimistic packed pairs can span hypothetical batches;
                    # never present this as a legal face partition.
                    vb = ((unique+1)//2)*32 if vertex_size==16 else unique*8
                    row = dict(source_vertices=len(obj.vertices),shared_before_batching=unique,
                        shared_after_batching=unique,boundary_duplication=0,vertex_loads=unique,
                        faces=len(faces),batches=minimum_batches,vertex_bytes=vb,
                        face_bytes=len(faces)*8,batch_header_bytes=minimum_batches*32,
                        object_header_bytes=32,bytes=vb+len(faces)*8+minimum_batches*32+32,
                        batch_counts=[])
                else:
                    row = totals(obj,faces,partitions[i,cache_size,policy],vertex_size)
                row['object'] = i if rooms else obj.source_offset
                compare_a(row,model_a['per_object'][i],model_a2['per_object'][i])
                rows.append(row)
            policies[policy] = aggregate(rows)
        base = dict(policies['greedy'])
        base.update(vertex_size=vertex_size,cache_size=cache_size,face_record_bytes=8,
            partition='greedy source-file face order',policies=policies)
        result[f'v{vertex_size}_c{cache_size}'] = base
    return result
