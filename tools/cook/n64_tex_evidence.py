# SPDX-License-Identifier: MIT
"""Retail TX64 validation: independent nearest-colour, errors and source pairs."""
import argparse
from collections import defaultdict
import gzip
import hashlib
import json
import math
from pathlib import Path
import platform
import time

import numpy as np
from PIL import Image, ImageDraw

import n64_tex as tex
from n64_tex_view import Section, image, measure
from ir import face_uv
import src_pc
import src_psx


def source_palette(level, texture=None):
    """Independent source -> displayed colour conversion (not writer helpers)."""
    if level.source == 'pc':
        rgb = np.array(list(level.palette), dtype=np.uint16).reshape(-1, 3)//2
        p = rgb[:, 0]*2048+rgb[:, 1]*64+rgb[:, 2]*2+1
        p[0] -= 1
        return p, np.zeros(256, dtype=np.uint8)
    raw = np.array([int.from_bytes(level.cluts[texture.palette][i:i+2], 'little') for i in range(0, 32, 2)], dtype=np.uint16)
    p = (raw % 32)*2048 + ((raw//32) % 32)*64 + ((raw//1024) % 32)*2 + (raw != 0)
    return p, (raw//32768).astype(np.uint8)


def source_page(level, page):
    raw = np.frombuffer(level.images[page], np.uint8)
    if level.source == 'pc':
        return raw.reshape(256, 256)
    return np.stack((raw % 16, raw//16), axis=1).reshape(256, 256)


def native_palette(view, pi):
    return np.frombuffer(view.data, '>u2', count=16, offset=view.palettes[pi][0])


def independent_assignment(original, source_pal, cooked_pal):
    """Scalar RGB555 nearest entry; intentionally not the writer's quantiser."""
    lookup = np.zeros(256, dtype=np.uint8)
    def channels(word):
        return ((int(word)//2048) % 32, (int(word)//64) % 32, (int(word)//2) % 32)
    opaque = [i for i, word in enumerate(cooked_pal) if word & 1]
    for index in np.unique(original):
        if index == 0:
            matches = [i for i, word in enumerate(cooked_pal) if int(word) == int(source_pal[0])]
            assert matches, 'missing exact transparent colour'
            lookup[index] = matches[0]
        else:
            rgb = channels(source_pal[index])
            distances = [sum((a-b)**2 for a, b in zip(rgb, channels(cooked_pal[i]))) for i in opaque]
            lookup[index] = opaque[min(range(len(opaque)), key=lambda i: distances[i])]
    return cooked_pal[lookup[original]]


def verify(level, blob, manifest):
    view = Section(blob)
    assert len(view.descriptors) == len(manifest['descriptors']) > 0
    assert len(blob) <= manifest['level_budget_bytes'], f'{level.name}: texture alone exceeds level arena'
    assert sum(manifest['breakdown'].values()) == len(blob)
    cluster_metrics = []
    pages = {i: source_page(level, i) for i in range(len(level.images))}
    for ci, c in enumerate(manifest['clusters']):
        if level.source == 'pc':
            p, _ = source_palette(level)
            original = pages[c['page']][c['y']:c['y']+c['height'], c['x']:c['x']+c['width']]
            pi = view.descriptors[c['members'][0]]['palette']
            cooked_pal = native_palette(view, pi)
            decoded = cooked_pal[view.indices(ci)]
            # This fails a farthest-colour mutation, unlike quantiser-vs-itself.
            np.testing.assert_array_equal(decoded, independent_assignment(original, p, cooked_pal))
            independent = measure(p[original], decoded)
            assert independent == c['error']
            cluster_metrics.append(independent)
    pixel_count = 0
    for i, r in enumerate(manifest['descriptors']):
        textures = level.object_textures if r['kind'] == 'object' else level.sprite_textures
        t = textures[r['texture']]
        x, y, w, h = r['rect']
        p, stp = source_palette(level, t)
        indices = pages[t.page][y:y+h, x:x+w]
        decoded, decoded_stp = view.decode(i)
        assert view.descriptors[i]['source_index'] == r['texture']
        if level.source == 'psx':
            np.testing.assert_array_equal(decoded, p[indices])
            np.testing.assert_array_equal(decoded_stp, stp[indices])
            np.testing.assert_array_equal(np.array(view.image(i)), np.array(image(p[indices])))
        else:
            assert measure(p[indices], decoded) == r['error']
        # Every native tile agrees with source bounds rounded out to even texels.
        assert [list(t) for t in view.tiles(i)] == r['tiles']
        pixel_count += w*h
    return view, dict(descriptors=len(view.descriptors), pixels=pixel_count, clusters=cluster_metrics)


def distribution(metrics):
    finite = [m['psnr'] for m in metrics if m['psnr'] is not None]
    maxima = [m['max_error'] for m in metrics]
    bins = dict(exact=sum(m['exact'] for m in metrics))
    for low, high in ((0, 16), (16, 20), (20, 24), (24, 28), (28, 32), (32, 36), (36, math.inf)):
        bins[f'[{low},{high})'] = sum(low <= p < high for p in finite)
    return dict(count=len(metrics), psnr_bins=bins,
        finite_psnr_percentiles=dict(zip(('min', 'p5', 'p25', 'p50', 'p75', 'p95', 'max'),
            [float(v) for v in np.percentile(finite, (0, 5, 25, 50, 75, 95, 100))])) if finite else {},
        max_channel_error_percentiles=[float(v) for v in np.percentile(maxima, (0, 50, 95, 100))] if maxima else [])


def contact_sheet(level, view, manifest, directory):
    p, _ = source_palette(level)
    worst = sorted(enumerate(manifest['clusters']), key=lambda pair: (
        pair[1]['error']['psnr'] if pair[1]['error']['psnr'] is not None else math.inf,
        -pair[1]['error']['max_error'], pair[0]))[:40]
    cell_w, cell_h = 540, 292
    sheet = Image.new('RGB', (2*cell_w, ((len(worst)+1)//2)*cell_h), '#606060')
    draw = ImageDraw.Draw(sheet)
    entries = []
    for slot, (ci, c) in enumerate(worst):
        x, y = (slot % 2)*cell_w, (slot//2)*cell_h
        indices = source_page(level, c['page'])[c['y']:c['y']+c['height'], c['x']:c['x']+c['width']]
        pal = native_palette(view, view.descriptors[c['members'][0]]['palette'])
        source, quantized = image(p[indices]), image(pal[view.indices(ci)])
        sheet.paste(source, (x, y+32), source)
        sheet.paste(quantized, (x+270, y+32), quantized)
        draw.text((x+2, y+2), f'cluster {ci}: {c["width"]}x{c["height"]}; PSNR {c["error"]["psnr"]}; max {c["error"]["max_error"]}', fill='white')
        draw.text((x+2, y+16), 'source RGB555', fill='white')
        draw.text((x+272, y+16), 'decoded CI4 RGB555', fill='white')
        entries.append(ci)
    directory.mkdir(parents=True, exist_ok=True)
    path = directory/(level.name+'.png')
    sheet.save(path)
    return dict(path=str(path), clusters=entries)


def face_matches(level):
    """Exactly M0's identity/position multiset/relative mapped-UV predicate."""
    result = defaultdict(list)
    seen, meshes = set(), []
    for slot, offset in enumerate(level.data['mesh_pointers']):
        if offset not in seen:
            meshes.append((slot, level.meshes[offset]))
            seen.add(offset)
    for kind, objects in (('rooms', enumerate(level.rooms)), ('meshes', meshes)):
        for identity, obj in objects:
            faces = obj.quads+obj.triangles if kind == 'rooms' else obj.faces
            for f in faces:
                if f.texture is None:
                    continue
                positions = tuple(tuple(obj.vertices[i][axis] for axis in ('x', 'y', 'z')) if kind == 'rooms'
                                  else tuple(obj.vertices[i]) for i in f.vertices)
                uv = face_uv(level, f)
                x, y = min(u for u, v in uv), min(v for u, v in uv)
                rel = tuple(sorted(zip(positions, ((u-x, v-y) for u, v in uv))))
                key = (kind, identity, len(f.vertices), tuple(sorted(positions)), rel)
                bounds = (x, y, max(u for u, v in uv)-x+1, max(v for u, v in uv)-y+1)
                result[key].append((f.texture, bounds))
    return result


def crop(view, manifest, source_index, bounds):
    index = manifest['object_descriptor_map'][str(source_index)]
    words, _ = view.decode(index)
    ox, oy = manifest['descriptors'][index]['rect'][:2]
    x, y, w, h = bounds
    return words[y-oy:y-oy+h, x-ox:x-ox+w]


def original_crop(level, index, bounds):
    t = level.object_textures[index]
    p, _ = source_palette(level, t)
    x, y, w, h = bounds
    return p[source_page(level, t.page)[y:y+h, x:x+w]]


def appearance_metrics(left, right):
    """Keep raw RGB evidence, but distinguish invisible transparent RGB."""
    opaque = ((left & 1) != 0) & ((right & 1) != 0)
    transparent = ((left & 1) == 0) & ((right & 1) == 0)
    composited_left = np.where(left & 1, left, 1).astype(np.uint16)
    composited_right = np.where(right & 1, right, 1).astype(np.uint16)
    return dict(opaque_error=measure(left[opaque], right[opaque]) if np.any(opaque) else None,
        composited_error=measure(composited_left, composited_right),
        jointly_transparent_pixels=int(np.count_nonzero(transparent)),
        invisible_rgb_differences=int(np.count_nonzero(transparent & (left != right))))


def cross_source(pc, psx, pc_view, psx_view, pc_manifest, psx_manifest, output):
    a, b = face_matches(pc), face_matches(psx)
    pairs = {}
    ambiguous = 0
    for key in a.keys() & b.keys():
        if len(a[key]) > 1 or len(b[key]) > 1:
            ambiguous += 1
        for left, right in zip(a[key], b[key]):
            pairs.setdefault((left, right), key)
    assert pairs, f'{pc.name}: vacuous cross-source texture pairing'
    metrics, rows, alpha = [], [], 0
    examples = []
    for (pi, pb), (si, sb) in sorted(pairs):
        left = crop(pc_view, pc_manifest, pi, pb)
        right = crop(psx_view, psx_manifest, si, sb)
        assert left.shape == right.shape and left.size > 0
        changed = int(np.count_nonzero((left & 1) != (right & 1)))
        alpha += changed
        m = measure(left | 1, right | 1)
        metrics.append(m)
        original_pc, original_psx = original_crop(pc, pi, pb), original_crop(psx, si, sb)
        # Independent source colours prove whether disagreement predates cooking.
        np.testing.assert_array_equal(right, original_psx)
        original_error = measure(original_pc | 1, original_psx | 1)
        key = pairs[((pi, pb), (si, sb))]
        row = dict(pc=pi, psx=si, pc_bounds=pb, psx_bounds=sb, error=m,
            source_error=original_error, alpha_differences=changed,
            **appearance_metrics(left, right),
            match=dict(kind=key[0], identity=key[1], corners=key[2], positions=key[3],
                per_position_uv=key[4], distinct_pc_materials=len(set(a[key])),
                distinct_psx_materials=len(set(b[key]))))
        if m['psnr'] is not None and m['psnr'] < 16:
            # Save three deterministic examples per level, including original and
            # cooked images; no source-art explanation is inferred from PSNR alone.
            if len(examples) < 3:
                panels = [image(w) for w in (original_pc, left, original_psx, right)]
                scale = max(1, min(4, 256//max(left.shape)))
                width, height = left.shape[1]*scale, left.shape[0]*scale
                canvas = Image.new('RGB', (max(200, width)*4, height+56), '#606060')
                draw = ImageDraw.Draw(canvas)
                for j, (panel, title) in enumerate(zip(panels, ('PC source', 'PC decoded', 'PSX source', 'PSX decoded'))):
                    panel = Image.fromarray(np.repeat(np.repeat(np.array(panel), scale, axis=0), scale, axis=1))
                    canvas.paste(panel, (j*max(200, width), 48), panel)
                    draw.text((j*max(200, width)+2, 4), title, fill='white')
                draw.text((2, 20), f'{pc.name}: PC {pi}, PSX {si}; decoded {m["psnr"]:.3f}dB; source {original_error["psnr"]:.3f}dB', fill='white')
                visible = row['composited_error']
                draw.text((2, 34), f'black-composited PSNR {visible["psnr"]}; jointly transparent {row["jointly_transparent_pixels"]}', fill='white')
                directory = output/'cross-source'
                directory.mkdir(parents=True, exist_ok=True)
                path = directory/f'{pc.name}-{pi}-{si}.png'
                canvas.save(path)
                examples.append(dict(path=str(path), pc=pi, psx=si, source_psnr=original_error['psnr'], decoded_psnr=m['psnr']))
        rows.append(row)
    low = [r for r in rows if r['error']['psnr'] is not None and r['error']['psnr'] < 16]
    return dict(paired_images=len(pairs), alpha_difference_texels=alpha, distribution=distribution(metrics),
        composited_distribution=distribution([r['composited_error'] for r in rows]),
        opaque_distribution=distribution([r['opaque_error'] for r in rows if r['opaque_error'] is not None]),
        ambiguous_geometry_keys=ambiguous, pairs=rows, examples=examples,
        low_psnr=dict(count=len(low), already_low_in_source=sum(r['source_error']['psnr'] is not None and
            r['source_error']['psnr'] < 16 for r in low),
            at_least_16_when_composited=sum(r['composited_error']['exact'] or r['composited_error']['psnr'] >= 16 for r in low),
            with_invisible_rgb_differences=sum(r['invisible_rgb_differences'] > 0 for r in low)))


def markdown(report):
    out = ['# M1 native texture evidence (compact CI4)', '', f'Python {report["python"]}; tool {report["tool_sha256"]}',
           '', 'D10: CI4 only; no CI8 fallback. Texture bytes are informational; complete level fit is checked by the pack cook.', '',
           '|Source|Level|Texture bytes|Level-arena absolute bound|Clusters|Palettes|Descriptors|Split rectangles|',
           '|---|---|---:|---:|---:|---:|---:|---:|']
    for r in report['levels']:
        out.append(f'|{r["source"]}|{r["level"]}|{r["bytes"]}|{r["level_budget_bytes"]}|{r["clusters"]}|{r["palettes"]}|{r["descriptors"]}|{r["over_limit_rectangles"]}|')
    columns = ['header', 'images', 'palettes', 'cluster_records', 'descriptors', 'split_lines', 'colours', 'animations', 'alignment']
    out.extend(['', '## Exact byte breakdown', '', 'Cluster 16 B; palette directory 8 B + RGBA5551 32 B; object 24 B;',
        'sprite 22 B; split line 2 B (none for unsplit rectangles); flat colour 8 B.', '',
        '|Source/level|'+'|'.join(columns)+'|', '|---|'+'|'.join(['---:']*len(columns))+'|'])
    for r in report['levels']:
        out.append('|'+r['source']+'/'+r['level']+'|'+'|'.join(str(r['breakdown'][c]) for c in columns)+'|')
    out.extend(['', '## RGB555 error distributions', '', '```json', json.dumps(report['distributions'], indent=2), '```',
                '', '## Cross-source decoded-picture PSNR', '',
                '|Level|Paired rectangles|Alpha differences|Raw PSNR<16|Already <16 in sources|At least16 after black composite|Examples|',
                '|---|---:|---:|---:|---:|---:|---|'])
    for name, r in report['cross_source'].items():
        out.append(f'|{name}|{r["paired_images"]}|{r["alpha_difference_texels"]}|{r["low_psnr"]["count"]}|{r["low_psnr"]["already_low_in_source"]}|{r["low_psnr"]["at_least_16_when_composited"]}|'+', '.join('`'+e['path']+'`' for e in r['examples'])+'|')
    out.extend(['', 'Source-vs-source RGB555 PSNR is computed before quantisation for every pair.',
        'Each saved panel shows original PC, decoded PC, original PSX, decoded PSX;',
        'PSX source and decoded pixels are independently asserted equal. Raw RGB includes',
        'invisible colour under alpha=0: PC palette entry0 retains arbitrary RGB, while PSX',
        'transparent 0x0000 is black. This makes otherwise identical pictures score below16.',
        'The extra composite column removes that invisible-RGB difference without hiding the',
        'raw metric. JSON also gives common-opaque and black-composited distributions,',
        'geometry/UV evidence and distinct-material ambiguity counts for each pair.', '',
        '## PC contact sheets: source left, decoded CI4 right (worst up to 40 clusters)', ''])
    out.extend(f'- `{r["contact_sheet"]["path"]}`' for r in report['levels'] if 'contact_sheet' in r)
    return '\n'.join(out)+'\n'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--data', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--level', help='optional single case-folded stem for focused investigation')
    args = p.parse_args()
    start = time.monotonic()
    args.output.mkdir(parents=True, exist_ok=True)
    report = dict(python=platform.python_version(), tool_sha256=tex.tool_hash(), levels=[], cross_source={})
    pc_objects, pc_clusters, cross_metrics = [], [], []
    loaded = {}
    paths = [('pc', f) for f in sorted((args.data/'pc').glob('*')) if f.suffix.lower() == '.phd'] + [
        ('psx', f) for f in sorted((args.data/'psx/PSXDATA').glob('*')) if f.suffix.lower() == '.psx']
    paths.sort(key=lambda row: (row[1].stem.lower(), row[0]))
    if args.level:
        paths = [(s, f) for s, f in paths if f.stem.lower() == args.level.lower()]
    if not paths:
        raise ValueError('no retail levels found')
    for source, path in paths:
        level = (src_pc if source == 'pc' else src_psx).load(path)
        key = hashlib.sha256(json.dumps([report['tool_sha256'], level.sha256, 1]).encode()).hexdigest()
        directory = args.output/'sections'/source/level.name/key
        if (directory/'manifest.json').exists() and (directory/'section.bin').exists():
            manifest = json.loads((directory/'manifest.json').read_text())
            blob = (directory/'section.bin').read_bytes()
            assert manifest['content_key'] == key
            assert hashlib.sha256(blob).hexdigest() == manifest['section_sha256']
            cache = 'hit'
        else:
            blob, manifest = tex.cook(level)
            directory.mkdir(parents=True, exist_ok=True)
            (directory/'section.bin').write_bytes(blob)
            (directory/'manifest.json').write_text(json.dumps(manifest, sort_keys=True)+'\n')
            cache = 'miss'
        view, checked = verify(level, blob, manifest)
        row = dict(source=source, level=level.name, source_sha256=level.sha256,
            content_key=key, cache=cache, section=str(directory/'section.bin'), bytes=len(blob),
            level_budget_bytes=manifest['level_budget_bytes'], breakdown=manifest['breakdown'],
            clusters=len(manifest['clusters']), palettes=manifest['palettes'], descriptors=len(manifest['descriptors']),
            over_limit_rectangles=manifest['over_limit_rectangles'], checked=checked)
        if source == 'pc':
            pc_clusters.extend(c['error'] for c in manifest['clusters'])
            pc_objects.extend(r['error'] for r in manifest['descriptors'] if r['kind'] == 'object')
            row['contact_sheet'] = contact_sheet(level, view, manifest, args.output/'contact-sheets')
            loaded[level.name] = (level, view, manifest)
        elif level.name in loaded:
            pc, pv, pm = loaded.pop(level.name)
            result = cross_source(pc, level, pv, view, pm, manifest, args.output)
            report['cross_source'][level.name] = result
            cross_metrics.extend(pair['error'] for pair in result['pairs'])
        report['levels'].append(row)
        print(f'{source}/{level.name}: texture_bytes={len(blob)} level_bound={manifest["level_budget_bytes"]} '
              f'clusters={row["clusters"]} palettes={row["palettes"]} descriptors={row["descriptors"]} '
              f'split={row["over_limit_rectangles"]} checked={checked["descriptors"]}/{checked["pixels"]} cache={cache}', flush=True)
    report['unpaired_pc'] = sorted(loaded)
    paired_rows = [p for r in report['cross_source'].values() for p in r['pairs']]
    report['distributions'] = dict(pc_clusters=distribution(pc_clusters), pc_objects=distribution(pc_objects),
        cross_source=distribution(cross_metrics),
        cross_source_composited=distribution([r['composited_error'] for r in paired_rows]),
        cross_source_common_opaque=distribution([r['opaque_error'] for r in paired_rows if r['opaque_error'] is not None]))
    report['seconds'] = time.monotonic()-start
    report['worst'] = {s: max((r for r in report['levels'] if r['source'] == s), key=lambda r: r['bytes'])['level']
                       for s in ('pc', 'psx') if any(r['source'] == s for r in report['levels'])}
    with gzip.open(args.output/'report.json.gz', 'wt') as f:
        json.dump(report, f, sort_keys=True)
    (args.output/'report.md').write_text(markdown(report))
    print('LEVELS', {s: sum(r['source'] == s for r in report['levels']) for s in ('pc', 'psx')})
    print('PSX LOSSLESS DESCRIPTORS', sum(r['checked']['descriptors'] for r in report['levels'] if r['source'] == 'psx'))
    print('WORST', report['worst'])
    print('CROSS-SOURCE PAIRED IMAGES', len(cross_metrics))
    print(f'Reports: {args.output}/report.md and report.json.gz; wall seconds={report["seconds"]:.3f}')


if __name__ == '__main__':
    main()
