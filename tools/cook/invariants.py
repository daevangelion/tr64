# SPDX-License-Identifier: MIT
"""Consumer-independent format invariants, exercised by the retail gate.
SPU ADPCM headers: psx-spx; corner order: permitted OpenLara 2333adef layout
facts plus cross-edition geometry/UV congruence, never byte-ledger agreement.
"""
from collections import Counter,defaultdict
from binary import FormatError
from ir import face_uv


def samples(level):
    if level.source!='psx':
        return dict(applicable=False)
    blocks,invalid,endings = 0,0,0
    for sample in level.samples:
        if not sample or len(sample)%16:
            raise FormatError(f'{level.name}: ADPCM sample is not whole 16-byte blocks')
        for offset in range(0,len(sample),16):
            header,flags = sample[offset:offset+2]
            blocks += 1
            invalid += int(header>>4>4 or header&15>12 or flags>7)
        endings += bool(sample[-15]&1)
    if invalid or endings!=len(level.samples):
        raise FormatError(f'{level.name}: ADPCM invariant: invalid_headers={invalid}, '
                          f'end_flags={endings}/{len(level.samples)}')
    return dict(applicable=True,samples=len(level.samples),blocks=blocks,
                invalid_headers=invalid,last_block_end_flags=endings)


def convex(uv):
    signs = set()
    for i in range(4):
        a,b,c = uv[i],uv[(i+1)%4],uv[(i+2)%4]
        cross = (b[0]-a[0])*(c[1]-b[1])-(b[1]-a[1])*(c[0]-b[0])
        if cross:
            signs.add(1 if cross>0 else -1)
    return len(signs)<=1  # Point/line textures are valid degenerate convex sets.


def material_counts(level):
    return [dict(pointer=slot,mesh_offset=offset,
        counts={f'{material}_{corners}':sum(len(f.vertices)==corners and
            (f.texture is None)==(material=='flat') for f in level.meshes[offset].faces)
            for material in ('flat','textured') for corners in (3,4)})
        for slot,offset in enumerate(level.data['mesh_pointers'])]


def local(level):
    checked,degenerate = 0,0
    for obj in list(level.rooms)+list(level.meshes.values()):
        faces = obj.quads if hasattr(obj,'quads') else obj.faces
        for face in faces:
            if face.texture is None or len(face.vertices)!=4:
                continue
            uv = face_uv(level,face)
            checked += 1
            if not convex(uv):
                raise FormatError(f'{level.name}: nonconvex quad texture {face.texture}')
            degenerate += not any((uv[i][0]-uv[0][0])*(uv[i+1][1]-uv[0][1]) !=
                (uv[i][1]-uv[0][1])*(uv[i+1][0]-uv[0][0]) for i in (1,2))
    return dict(adpcm=samples(level),quad_textures=dict(checked=checked,
        nonconvex=0,degenerate_convex=degenerate),mesh_material_counts=material_counts(level))


def geometry(level,kind,corners,relative=True):
    result = defaultdict(Counter)
    if kind=='rooms':
        objects = enumerate(level.rooms)
    else:
        # Source mesh byte offsets differ. Mesh pointer slots are common IDs;
        # aliases must not make one physical mesh appear multiple times.
        seen = set()
        objects = []
        for slot,offset in enumerate(level.data['mesh_pointers']):
            if offset not in seen:
                objects.append((slot,level.meshes[offset]))
                seen.add(offset)
    for identity,obj in objects:
        faces = obj.quads+obj.triangles if kind=='rooms' else obj.faces
        for face in faces:
            if len(face.vertices)!=corners:
                continue
            if kind=='rooms':
                positions = tuple(tuple(obj.vertices[i][axis] for axis in ('x','y','z')) for i in face.vertices)
            else:
                positions = tuple(obj.vertices[i] for i in face.vertices)
            # Match geometry by its positions, not a preferred starting
            # corner/winding: authored PC/PSX entity faces may rotate their
            # corner lists. Compare each UV at its ACTUAL position.
            if face.texture is not None:
                coords = face_uv(level,face)
                if relative:
                    u0,v0 = min(u for u,v in coords),min(v for u,v in coords)
                    coords = tuple((u-u0,v-v0) for u,v in coords)
                uv = tuple(sorted(zip(positions,coords)))
            else:
                uv = None
            result[(identity,tuple(sorted(positions)))][uv] += 1
    return result


def paired(pc,psx):
    rows = {}
    for kind in ('rooms','meshes'):
        for corners in (3,4):
            a,b = geometry(pc,kind,corners),geometry(psx,kind,corners)
            absolute_a,absolute_b = geometry(pc,kind,corners,False),geometry(psx,kind,corners,False)
            absolute_matches = sum(sum((absolute_a[k]&absolute_b[k]).values()) for k in absolute_a.keys()&absolute_b.keys())
            na = sum(sum(v.values()) for v in a.values())
            nb = sum(sum(v.values()) for v in b.values())
            matching,matching_uv,checked_quad_uv = 0,0,0
            mismatches = []
            for key in a.keys() & b.keys():
                count = min(sum(a[key].values()),sum(b[key].values()))
                same = sum((a[key] & b[key]).values())
                matching += count
                matching_uv += same
                # Flat faces carry no UV. Compare textured common geometry;
                # changed flat/textured materials are gameplay/art differences.
                ta,tb = sum(v for k,v in a[key].items() if k is not None),sum(v for k,v in b[key].items() if k is not None)
                common_textured = min(ta,tb)
                same_textured = sum(v for k,v in (a[key]&b[key]).items() if k is not None)
                checked_quad_uv += common_textured
                if same_textured<common_textured:
                    mismatches.append(dict(object=key[0],positions=key[1],
                        pc_uvs=[dict(uv=uv,count=n) for uv,n in a[key].items()],
                        psx_uvs=[dict(uv=uv,count=n) for uv,n in b[key].items()]))
            if mismatches:
                raise FormatError(f'{pc.name}: {kind} {corners}-corner corner-to-UV invariant: '
                                  f'{len(mismatches)} congruent geometry mismatches; {mismatches[:1]}')
            possible_textured = min(sum(n for counter in a.values() for uv,n in counter.items() if uv is not None),
                                    sum(n for counter in b.values() for uv,n in counter.items() if uv is not None))
            if possible_textured and not checked_quad_uv:
                raise FormatError(f'{pc.name}: {kind} {corners}-corner invariant has no common textured geometry')
            rows[f'{kind}_{corners}'] = dict(pc_faces=na,psx_faces=nb,
                congruent_geometry=matching,congruent_geometry_and_uv=matching_uv,
                absolute_uv_placement_matches=absolute_matches,
                atlas_placement_differences=matching_uv-absolute_matches,
                uv_layout_rule='per-position UV minus used-rectangle origin; absolute atlas offsets reported separately',
                pc_unmatched_geometry=na-matching,psx_unmatched_geometry=nb-matching,
                checked_matching_textured_quads=checked_quad_uv if corners==4 else 0,
                checked_matching_textured_triangles=checked_quad_uv if corners==3 else 0,
                quad_uv_mismatches=0,triangle_uv_mismatches=0)
    aa,bb = material_counts(pc),material_counts(psx)
    differences = [dict(pointer=i,pc=a['counts'],psx=b['counts']) for i,(a,b) in enumerate(zip(aa,bb))
                   if a['counts']!=b['counts']]
    rows['mesh_material_evidence'] = dict(compared_pointer_slots=min(len(aa),len(bb)),
        same_counts=min(len(aa),len(bb))-len(differences),different_counts=len(differences),examples=differences[:5])
    return rows
