# SPDX-License-Identifier: MIT
"""All candidate arithmetic, bounds and margins without selecting a backend."""
from sizing import LEVEL_SECTIONS,align,resident_total
VARIANTS = ('v16_c70','v16_c48','v8_c70','v8_c48')


def candidate_summary(levels,budgets):
    """Every candidate replaces named A1 sections inside the ONE level arena."""
    result = {}
    budget = budgets['level']

    def fixed(name,pairs,replaced):
        row,sections = max(pairs,key=lambda pair:resident_total(pair[1]))
        needed = resident_total(sections)
        result[name] = dict(level=row['level'],source=row['source'],needed_bytes=needed,
            budget_bytes=budget,binding_pool='level',replacement_sections=list(replaced),
            section_bytes=sections,aligned_section_bytes={k:align(sections[k],16) for k in LEVEL_SECTIONS},
            fits=needed<=budget,
            verdict='fits, ZERO headroom' if needed==budget else 'fits level model' if needed<budget else 'does not fit level model',
            headroom_bytes=budget-needed,
            ties=[dict(level=r['level'],source=r['source']) for r,s in pairs if resident_total(s)==needed],
            failures=[dict(level=r['level'],source=r['source'],over_bytes=resident_total(s)-budget)
                      for r,s in pairs if resident_total(s)>budget])

    fixed('A2.level',[(r,r['model_a2']['sections']) for r in levels],LEVEL_SECTIONS)

    def b_sections(row,variant,policy):
        return dict(row['sections'],
            rooms=row['model_b']['rooms'][variant]['policies'][policy]['bytes'],
            meshes=row['model_b']['meshes'][variant]['policies'][policy]['bytes'])

    for variant in VARIANTS:
        pairs = [(row,max((b_sections(row,variant,p) for p in ('greedy','locality')),key=resident_total))
                 for row in levels]
        name = f'B.level.{variant}'
        fixed(name,pairs,('rooms','meshes'))
        out = result[name]
        worst = next(r for r,s in pairs if r['level']==out['level'] and r['source']==out['source'])
        lower,greedy,locality = (resident_total(b_sections(worst,variant,p))
                                for p in ('lower_bound','greedy','locality'))
        out.update(lower_bound_bytes=lower,greedy_bytes=greedy,locality_bytes=locality,
            lower_bound_headroom_bytes=budget-lower,best_known_partition_bytes=min(greedy,locality))
        lower_max = max(resident_total(b_sections(r,variant,'lower_bound')) for r in levels)
        if out['needed_bytes']>budget and lower_max<=budget:
            out['verdict'] = 'depends on batching'
            out['fits'] = None
        elif lower_max>budget:
            out['verdict'] = 'total level lower bound exceeds row'
        else:
            out['verdict'] = 'fits level with both measured batchings'

    for key in ('native_pages','union_payload_bytes','bounding_uploadable_bytes','sparse_addressable_bytes'):
        pairs = []
        for row in levels:
            value = row['model_c'][key]
            if value is not None:
                tex = value['bytes'] if isinstance(value,dict) else value
                pairs.append((row,dict(row['sections'],tex=tex)))
        if pairs:
            fixed('C.level.'+key,pairs,('tex',))
    return result


def markdown(report):
    lines = ['## Model A2 — internal borders only, outer clamp (assumption)', '',
        'A1 and A2 are separate assumptions. Which outer-edge treatment is correct',
        'is an RDP behaviour to measure in M1, not a sizing gate selection.',
        'Only the aligned aggregate is compared with the shared level arena.', '',
        '| Level / source | CI4 KiB | CI8 KiB | Rooms KiB | Meshes KiB | Arena KiB | Headroom B | Fits arena | Cart KiB |',
        '|---|---:|---:|---:|---:|---:|---:|---|---:|']
    for row in report['levels']:
        a = row['model_a2']
        allocation = a['pools']['level']
        ci8 = f'{a["textures_ci8"]["bytes"]/1024:.3f}' if a['textures_ci8'] else 'absent'
        lines.append(f'| {row["level"]}/{row["source"]} | {a["textures_ci4"]["bytes"]/1024:.3f} | {ci8} | '
                     f'{a["room_geometry"]["bytes"]/1024:.3f} | {a["mesh_geometry"]["bytes"]/1024:.3f} | '
                     f'{allocation["needed_bytes"]/1024:.3f} | {allocation["headroom_bytes"]} | '
                     f'{"yes" if allocation["fits"] else "NO"} | {a["cart_bytes"]/1024:.3f} |')
    lines += ['', '## Model B — bounds and two real cache partitions', '',
        '8-byte faces; 32-byte batch/object headers. Lower bound assumes one load',
        'per used vertex and ceil(unique/cache) batches per object, not a valid face',
        'partition. Both actual partitions keep each complete face in one cache.',
        'Locality grows by shared-vertex adjacency; file order is retained separately.',
        'Section byte columns below have no independent caps. Binding B scenarios',
        'replace BOTH geometry sections and retain A1 core/textures in the shared arena.', '',
        '| Level/source/section/variant | Shared before; faces | Lower KiB | File KiB; loads; dup | Locality KiB; loads; dup | A1/locality ratio | A2/locality ratio |',
        '|---|---|---:|---|---|---:|---:|']
    for row in report['levels']:
        for kind in ('rooms','meshes'):
            for variant in VARIANTS:
                ps = row['model_b'][kind][variant]['policies']
                lo,g,l = (ps[p] for p in ('lower_bound','greedy','locality'))
                ratio1 = f'{l["a_to_b_transform_ratio"]:.3f}' if l['a_to_b_transform_ratio'] is not None else 'absent'
                ratio2 = f'{l["a2_to_b_transform_ratio"]:.3f}' if l['a2_to_b_transform_ratio'] is not None else 'absent'
                lines.append(f'| {row["level"]}/{row["source"]}/{kind}/{variant} | '
                    f'{lo["shared_before_batching"]}; {lo["faces"]} | {lo["bytes"]/1024:.3f} | '
                    f'{g["bytes"]/1024:.3f}; {g["vertex_loads"]}; {g["boundary_duplication"]} | '
                    f'{l["bytes"]/1024:.3f}; {l["vertex_loads"]}; {l["boundary_duplication"]} | {ratio1} | {ratio2} |')
    lines += ['', '### All-room vertex loads and duplication', '',
        'Each variant column: file loads/dup -> locality loads/dup; A1/locality and',
        'A2/locality transform ratios. Counts, byte components and every batch are',
        'also present per room/mesh in JSON. Ratios are not measured frame speedups.', '',
        '| Level/source/room | Shared before; faces | A1 / A2 loads | B16/70 | B16/48 | B8/70 | B8/48 |',
        '|---|---|---|---|---|---|---|']
    for row in report['levels']:
        variants = row['model_b']['rooms']
        for index,base in enumerate(variants['v16_c70']['policies']['greedy']['per_object']):
            cols = []
            for variant in VARIANTS:
                g,l = (variants[variant]['policies'][p]['per_object'][index] for p in ('greedy','locality'))
                ratios = ('absent' if l['a_to_b_transform_ratio'] is None else
                          f'{l["a_to_b_transform_ratio"]:.3f}/{l["a2_to_b_transform_ratio"]:.3f}')
                cols.append(f'{g["vertex_loads"]}/{g["boundary_duplication"]} → {l["vertex_loads"]}/{l["boundary_duplication"]}; {ratios}')
            lines.append(f'| {row["level"]}/{row["source"]}/{index} | {base["shared_before_batching"]}; {base["faces"]} | '
                f'{base["model_a_vertex_loads"]}/{base["model_a2_vertex_loads"]} | '+ ' | '.join(cols)+' |')
    lines += ['', '## Model C — native pages / union coverage', '',
        'Union is a lower bound. Bounding images charge holes; sparse runs charge',
        '8-byte directories. PC cluster palette quality is unmeasured.',
        'Texture payload columns have no independent cap. Binding C scenarios',
        'retain A1 core/room/mesh bytes and replace only textures in the level arena.', '',
        '| Level/source | Native KiB | Union KiB | Bounding KiB | Sparse KiB | Clusters | Palettes / distinct source / referenced | Oversize objects |',
        '|---|---:|---:|---:|---:|---:|---|---:|']
    for row in report['levels']:
        c = row['model_c']
        native = f'{c["native_pages"]["bytes"]/1024:.3f}' if c['native_pages'] else 'absent'
        lines.append(f'| {row["level"]}/{row["source"]} | {native} | {c["union_payload_bytes"]/1024:.3f} | '
            f'{c["bounding_uploadable_bytes"]/1024:.3f} | {c["sparse_addressable_bytes"]/1024:.3f} | {c["cluster_count"]} | '
            f'{c["palette_count"]}/{c["distinct_source_palettes"]}/{c["referenced_distinct_source_palettes"]} | '
            f'{c["object_rectangles_referenced"]["over_2048_count"]} |')
    lines += ['', '### Object rectangle distribution and >2048-byte entries', '',
        'Referenced descriptors/used corners; JSON also lists all four stored corners',
        'for every source descriptor. CI4 bytes=ceil(width/2)*height, no borders.', '']
    for row in report['levels']:
        stats = row['model_c']['object_rectangles_referenced']
        lines += [f'#### {row["level"]}/{row["source"]}', '',
            ', '.join(f'{size}:{count}' for size,count in stats['size_distribution'].items()), '',
            f'Oversize count: {stats["over_2048_count"]}.']
        for entry in stats['over_2048']:
            lines.append(f'- texture {entry["texture"]}: {entry["width"]}x{entry["height"]}, {entry["ci4_bytes"]} B')
        lines.append('')
    lines += ['### Shared level arena overages / batching-sensitive cases (bytes)', '']
    for name,row in report['candidate_summary'].items():
        failures = row['failures']
        lines.append(f'- **{name}: {row["verdict"]}.** '+('No applicable level exceeds the shared arena row.' if not failures else
            '; '.join(f'{x["level"]}/{x["source"]}: +{x["over_bytes"]} B' for x in failures)))
    return '\n'.join(lines)+'\n'


def worst_rows(report):
    lines = []
    for name,row in report['candidate_summary'].items():
        needed = f'{row["needed_bytes"]/1024:.3f}'
        margin = str(row['headroom_bytes'])
        if name.startswith('B.'):
            needed = f'{row["lower_bound_bytes"]/1024:.3f} / {row["greedy_bytes"]/1024:.3f} / {row["locality_bytes"]/1024:.3f}'
            margin = f'LB {row["lower_bound_headroom_bytes"]}; file/local worst {row["headroom_bytes"]}'
        ties = f'; {len(row["ties"])}-way tie' if len(row['ties'])>1 else ''
        lines.append(f'| {name} | {row["budget_bytes"]/1024:g} | {row["level"]}/{row["source"]}{ties} | '
                     f'{needed} | {margin} | {row["verdict"]} | — |')
    return lines
