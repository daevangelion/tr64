# SPDX-License-Identifier: MIT
"""Compact CI4-only TX64 writer; layout facts: docs/formats/n64-tex.md."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

import numpy as np

from sizing import texture_rectangle, pools
from pool_rows import pool_table
from n64_validate import admit

HEADER = struct.Struct('>4s19I')
CLUSTER = struct.Struct('>I4HI')
PALETTE = struct.Struct('>IHH')
OBJECT = struct.Struct('>HH4B8B2B3H')
SPRITE = struct.Struct('>HH4B3H4h')


def pc_palette(level):
    rgb = np.frombuffer(level.palette, np.uint8).reshape(256, 3).astype(np.uint16) >> 1
    words = (rgb[:, 0] << 11) | (rgb[:, 1] << 6) | (rgb[:, 2] << 1) | 1
    words[0] &= 0xfffe
    return words


def psx_palette(raw):
    if len(raw) != 32:
        raise ValueError('PlayStation palette must contain exactly 16 colours')
    src = np.frombuffer(raw, '<u2').astype(np.uint16)
    words = ((src & 31) << 11) | (((src >> 5) & 31) << 6) | (((src >> 10) & 31) << 1) | (src != 0)
    return words, ((src >> 15) & 1).astype(np.uint8)


def rgb555(words):
    words = np.asarray(words, dtype=np.uint16)
    return np.stack(((words >> 11) & 31, (words >> 6) & 31, (words >> 1) & 31), axis=-1)


def error(source, decoded):
    delta = rgb555(source).astype(np.int32) - rgb555(decoded).astype(np.int32)
    mse = float(np.mean(delta.astype(np.float64) ** 2))
    return dict(psnr=None if mse == 0 else 10 * math.log10(31 * 31 / mse),
                exact=mse == 0, max_error=int(np.max(np.abs(delta))), mse=mse,
                samples=int(delta.size))


def nearest(rgb, representatives):
    """Squared RGB555 distance, stable lowest-entry tie break."""
    d = np.asarray(rgb, dtype=np.int32)[:, None, :] - np.asarray(representatives, dtype=np.int32)[None, :, :]
    return np.argmin(np.sum(d*d, axis=2), axis=1)


def quantize(indices, source_palette):
    """Weighted median-cut in displayed RGB555; transparent index never merges."""
    present, counts = np.unique(indices, return_counts=True)
    transparent = bool(np.any(present == 0))
    opaque = present != 0
    colors, inverse = np.unique(rgb555(source_palette[present[opaque]]), axis=0, return_inverse=True)
    weights = np.bincount(inverse, weights=counts[opaque]).astype(np.int64)
    boxes = [np.arange(len(colors))] if len(colors) else []
    limit = 15 if transparent else 16
    while len(boxes) < limit:
        candidates = [(int(np.max(np.ptp(colors[b], axis=0))) * int(weights[b].sum()), -i, i)
                      for i, b in enumerate(boxes) if len(b) > 1]
        if not candidates:
            break
        _, _, i = max(candidates)
        b = boxes.pop(i)
        channel = int(np.argmax(np.ptp(colors[b], axis=0)))
        order = b[np.argsort(colors[b, channel], kind='stable')]
        cut = int(np.searchsorted(np.cumsum(weights[order]), (weights[order].sum() + 1) // 2)) + 1
        cut = min(max(cut, 1), len(order) - 1)
        boxes.extend((order[:cut], order[cut:]))
    reps = [(np.sum(colors[b].astype(np.int64) * weights[b, None], axis=0) + weights[b].sum() // 2)
            // weights[b].sum() for b in boxes]
    palette = [int(source_palette[0])] if transparent else []
    palette.extend((int(r) << 11) | (int(g) << 6) | (int(b) << 1) | 1 for r, g, b in reps)
    palette.extend([0] * (16 - len(palette)))
    palette = np.array(palette, dtype=np.uint16)
    lut = np.zeros(len(source_palette), dtype=np.uint8)
    if reps:
        lut[:] = nearest(rgb555(source_palette), reps) + int(transparent)
    if transparent:
        lut[0] = 0
    result = lut[indices]
    return result, palette, error(source_palette[indices], palette[result])


def quantize_argb1555(raw):
    # Transparent RGB is immaterial to the renderer; collapse it to black.
    words = np.where(raw & 0x8000, ((raw & 0x7fff) << 1) | 1, 0).astype(np.uint16)
    colors, indices = np.unique(words, return_inverse=True)
    if colors[0] != 0:
        colors = np.concatenate((np.zeros(1, dtype=np.uint16), colors))
        indices += 1
    indices = indices.reshape(raw.shape)
    q, palette, metrics = quantize(indices, colors)
    return q, palette, metrics, words


def animation_groups(level):
    words = level.data['animated_textures']
    if not words:
        return []
    groups, p = [], 1
    for _ in range(words[0]):
        if p >= len(words):
            raise ValueError('truncated animation group')
        count = words[p]+1
        if p+1+count > len(words):
            raise ValueError('truncated animation members')
        group = list(words[p+1:p+1+count])
        if any(i >= len(level.object_textures) for i in group):
            raise ValueError('animation texture out of bounds')
        groups.append(group)
        p += count+1
    if any(words[p:]):
        raise ValueError('unexplained animation words')
    return groups


def references(level):
    objects, sprites = {}, set()
    def face(f):
        if f.texture is not None:
            objects[f.texture] = max(objects.get(f.texture, 0), len(f.vertices))
    for room in level.rooms:
        for f in room.quads+room.triangles:
            face(f)
        sprites.update(s['texture'] for s in room.sprites)
    for mesh in level.meshes.values():
        for f in mesh.faces:
            face(f)
    groups = animation_groups(level)
    # Closure matters if a descriptor participates in more than one range.
    changed = True
    while changed:
        changed = False
        for group in groups:
            if any(i in objects for i in group):
                corners = max(objects[i] for i in group if i in objects)
                for i in group:
                    if objects.get(i, 0) < corners:
                        objects[i] = corners
                        changed = True
    active = [g for g in groups if any(i in objects for i in g)]
    for seq in level.data['sprite_sequences']:
        sprites.update(range(seq['first'], seq['first']+abs(seq['count'])))
    if any(i < 0 or i >= len(level.object_textures) for i in objects):
        raise ValueError('face texture out of bounds')
    if any(i < 0 or i >= len(level.sprite_textures) for i in sprites):
        raise ValueError('sprite texture out of bounds')
    return objects, sprites, active


def rectangles(level):
    objects, sprites, groups = references(level)
    rows = []
    for kind, selected, textures in (('object', sorted(objects), level.object_textures),
                                      ('sprite', sorted(sprites), level.sprite_textures)):
        for i in selected:
            corners = objects[i] if kind == 'object' else 2
            rect, _ = texture_rectangle(level, textures[i], corners)
            rows.append(dict(kind=kind, texture=i, page=textures[i].page, rect=list(rect), corners=corners))
    return rows, groups


def clusters(rows, even_origins=False):
    parent = list(range(len(rows)))
    def root(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    pages = {}
    for i, r in enumerate(rows):
        page = (r['page'], r['rect'][0] % 2) if even_origins else r['page']
        owners = pages.setdefault(page, np.full((256, 256), -1, dtype=np.int32))
        x, y, w, h = r['rect']
        if not (0 <= x < x+w <= 256 and 0 <= y < y+h <= 256):
            raise ValueError('texture rectangle outside page')
        region = owners[y:y+h, x:x+w]
        for prev in np.unique(region):
            if prev >= 0:
                parent[root(i)] = root(int(prev))
        region[:] = i
    groups = {}
    for i in range(len(rows)):
        groups.setdefault(root(i), []).append(i)
    result = []
    for members in groups.values():
        x = min(rows[i]['rect'][0] for i in members)
        y = min(rows[i]['rect'][1] for i in members)
        w = max(rows[i]['rect'][0]+rows[i]['rect'][2] for i in members)-x
        h = max(rows[i]['rect'][1]+rows[i]['rect'][3] for i in members)-y
        result.append(dict(page=rows[members[0]]['page'], x=x, y=y, width=w, height=h, members=members))
    return result


def page_indices(level, page):
    raw = np.frombuffer(level.images[page], np.uint8)
    if level.source == 'pc':
        return raw.reshape(256, 256)
    out = np.empty(raw.size*2, dtype=np.uint8)
    out[0::2], out[1::2] = raw & 15, raw >> 4
    return out.reshape(256, 256)


def tiles(x, width, height):
    # libdragon rdpq_tex.c: CI4 loads round source bounds out to even texels.
    start, end = x & ~1, (x+width+1) & ~1
    stride = (((end-start)//2)+7)//8*8
    rows = 2048//stride
    if not rows:
        raise ValueError('tile row exceeds TMEM')
    return [(0, y, width, min(rows, height-y), stride, stride*min(rows, height-y))
            for y in range(0, height, rows)]


def tool_hash():
    base = Path(__file__).parent
    h = hashlib.sha256()
    for p in sorted(base.glob('*.py')) + [pool_table()]:
        h.update(p.name.encode()+p.read_bytes())
    return h.hexdigest()


def cook(level):
    rows, animations = rectangles(level)
    game = level.reserved.get('game', 1)
    hi = game in (2, 3)
    groups = clusters(rows, even_origins=hi)
    source_palette = pc_palette(level) if level.source == 'pc' and not hi else None
    palette_values, palette_ids = [], {}
    def add_palette(words, stp=None):
        words = np.asarray(words, dtype=np.uint16)
        stp = np.zeros(16, dtype=np.uint8) if stp is None else stp
        payload = words.astype('>u2').tobytes()
        mask = sum(int(v) << i for i, v in enumerate(stp))
        key = (payload, mask)
        if key not in palette_ids:
            palette_ids[key] = len(palette_values)
            palette_values.append(key)
        return palette_ids[key]
    object_map = {str(r['texture']): i for i, r in enumerate(rows) if r['kind'] == 'object'}
    sprite_map = {str(r['texture']): i for i, r in enumerate(rows) if r['kind'] == 'sprite'}
    if game in (1, 2, 3):
        admit(game, object_descriptors=len(object_map), sprite_descriptors=len(sprite_map),
              descriptor_ids=len(rows), clusters=len(groups))
    cluster_records, images, split_lines, objects, sprites = [], [], [], [], []
    for c_id, c in enumerate(groups):
        x, y, w, h = c['x'], c['y'], c['width'], c['height']
        if hi:
            page = np.frombuffer(level.reserved['argb1555_pages'][c['page']], '<u2').reshape(256, 256)
            q, pal, c['error'], source_words = quantize_argb1555(page[y:y+h, x:x+w])
            palette_id = add_palette(pal)
        elif level.source == 'pc':
            indices = page_indices(level, c['page'])[y:y+h, x:x+w]
            q, pal, c['error'] = quantize(indices, source_palette)
            source_words = source_palette[indices]
            palette_id = add_palette(pal)
        else:
            q = page_indices(level, c['page'])[y:y+h, x:x+w]
        stride = (w+1)//2
        packed = np.zeros((h, stride), dtype=np.uint8)
        packed[:, :] = q[:, 0::2] << 4
        packed[:, :w//2] |= q[:, 1::2]
        image = packed.tobytes()
        c.update(stride=stride, image_bytes=len(image))
        images.append(image)
        cluster_records.append((0, w, h, stride, 0, 0))
        for d in c['members']:
            r = rows[d]
            t = (level.object_textures if r['kind'] == 'object' else level.sprite_textures)[r['texture']]
            if level.source == 'psx':
                pal, stp = psx_palette(level.cluts[t.palette])
                pid = add_palette(pal, stp)
            else:
                pid = palette_id
            rx, ry, rw, rh = r['rect']
            dx, dy = rx-x, ry-y
            if dx % 2 or (not hi and (dx+rw) % 2):
                raise ValueError(f'CI4 descriptor requires even cluster-local x and right end: '
                                 f'{r["kind"]} {r["texture"]}, x={dx}, right={dx+rw}')
            tile_plan = tiles(dx, rw, rh)
            first, count = len(split_lines), len(tile_plan)-1
            split_lines.extend(z[1] for z in tile_plan[1:])
            admit(game, split_lines=len(split_lines))
            if r['kind'] == 'sprite':
                sprites.append((d, (c_id, pid, dx, dy, rw-1, rh-1, r['texture'], first, count, *t.bounds)))
            else:
                if t.attribute not in (0, 1, 2):
                    raise ValueError(f'unknown texture attribute {t.attribute}')
                order = (0, 1, 3, 2) if level.source == 'psx' else (0, 1, 2, 3)
                uv = tuple((t.uv[i][0]-rx, t.uv[i][1]-ry) if i < r['corners'] else
                           (t.uv[0][0]-rx, t.uv[0][1]-ry) for i in order)
                tri_map = 0x34 if level.source == 'psx' else 0x24
                objects.append((d, (c_id, pid, dx, dy, rw-1, rh-1,
                    *(v for pair in uv for v in pair), 1 << t.attribute, tri_map, r['texture'], first, count)))
            r.update(cluster=c_id, palette=pid, tiles=[list(z) for z in tile_plan], descriptor=d)
            if level.source == 'pc':
                r['error'] = error(source_words[dy:dy+rh, dx:dx+rw], pal[q[dy:dy+rh, dx:dx+rw]])
    admit(game, palettes=len(palette_values))
    flat = []
    materials = sorted({f.color for m in level.meshes.values() for f in m.faces if f.texture is None})
    for material in materials:
        if hi:
            i = (material >> 8) * 4
            r, g, b = level.reserved['solid_colour_palette'][i:i+3]
            color, stp = ((r>>3)<<11) | ((g>>3)<<6) | ((b>>3)<<1) | 1, 0
        elif level.source == 'pc':
            color, stp = int(source_palette[material & 255]) | 1, 0
        else:
            t = level.object_textures[material & 0x7fff]
            pal, stps = psx_palette(level.cluts[t.palette])
            u, v = t.uv[0]
            index = page_indices(level, t.page)[v, u]
            color, stp = int(pal[index]), int(stps[index])
        flat.append((material, color, stp, 0))
    anim = [len(animations)]
    for group in animations:
        anim.extend([len(group)]+[object_map[str(i)] for i in group])
    blob = bytearray(HEADER.size)
    breakdown = dict(header=HEADER.size, images=0, palettes=0, cluster_records=0,
                     descriptors=0, split_lines=0, colours=0, animations=0, alignment=0)
    def append(data, category):
        pad = (-len(blob)) % 8
        blob.extend(b'\0' * pad)
        breakdown['alignment'] += pad
        offset = len(blob)
        blob.extend(data)
        breakdown[category] += len(data)
        return offset
    cluster_off = append(bytes(CLUSTER.size * len(groups)), 'cluster_records')
    palette_off = append(bytes(PALETTE.size * len(palette_values)), 'palettes')
    object_off = append(b''.join(OBJECT.pack(*r) for _, r in sorted(objects)), 'descriptors')
    sprite_off = append(b''.join(SPRITE.pack(*r) for _, r in sorted(sprites)), 'descriptors')
    split_off = append(struct.pack('>'+str(len(split_lines))+'H', *split_lines), 'split_lines')
    flat_off = append(b''.join(struct.pack('>4H', *f) for f in flat), 'colours')
    anim_off = append(struct.pack('>'+str(len(anim))+'H', *anim), 'animations')
    for i, image in enumerate(images):
        record = list(cluster_records[i])
        record[0] = append(image, 'images')
        CLUSTER.pack_into(blob, cluster_off+i*CLUSTER.size, *record)
    for i, (payload, mask) in enumerate(palette_values):
        p = append(payload, 'palettes')
        PALETTE.pack_into(blob, palette_off+i*PALETTE.size, p, mask, 0)
    append(b'', 'alignment')
    HEADER.pack_into(blob, 0, b'TX64', 1, len(blob), len(objects), len(sprites), len(groups),
        len(palette_values), len(split_lines), len(flat), len(anim),
        cluster_off, palette_off, object_off, sprite_off, split_off, flat_off, anim_off, int(hi), 0, 0)
    assert sum(breakdown.values()) == len(blob)
    tool = tool_hash()
    key = hashlib.sha256(json.dumps([tool, level.sha256, 1]).encode()).hexdigest()
    manifest = dict(version=1, source=level.source, level=level.name, source_sha256=level.sha256,
        tool_sha256=tool, content_key=key, section_sha256=hashlib.sha256(blob).hexdigest(),
        bytes=len(blob), level_budget_bytes=pools(pool_table(game if game in (1, 2, 3) else None))['level'],
        breakdown=breakdown, clusters=groups, descriptors=rows, palettes=len(palette_values),
        object_descriptor_map=object_map, sprite_descriptor_map=sprite_map,
        source_object_count=len(level.object_textures), source_sprite_count=len(level.sprite_textures),
        split_lines=len(split_lines), over_limit_rectangles=sum(len(r['tiles']) > 1 for r in rows),
        flat_colours=[list(f[:3]) for f in flat])
    return bytes(blob), manifest


def descriptor_index(manifest, kind, source_index):
    """Geometry must not silently submit an unreferenced source texture."""
    try:
        return manifest[kind+'_descriptor_map'][str(source_index)]
    except KeyError as exc:
        raise ValueError(f'unreferenced {kind} texture {source_index}') from exc


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', choices=('pc', 'psx'), required=True)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    import src_pc, src_psx
    level = (src_pc if args.source == 'pc' else src_psx).load(args.input)
    blob, manifest = cook(level)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output/'section.bin').write_bytes(blob)
    (args.output/'manifest.json').write_text(json.dumps(manifest, sort_keys=True, indent=2)+'\n')
    print(f'{level.source}/{level.name}: {len(blob)} bytes; {len(manifest["clusters"])} clusters; '
          f'{manifest["palettes"]} palettes; {len(manifest["descriptors"])} descriptors; '
          f'{manifest["over_limit_rectangles"]} split rectangles')


if __name__ == '__main__':
    main()
