# SPDX-License-Identifier: MIT
"""Candidate C: native pages and overlap-union texture coverage, no borders.

The union payload is a lower bound: a union may have holes. Uploadable cluster
bounding images are reported separately so the holes are not hidden savings.
"""
from collections import Counter
from sizing import referenced_textures,texture_rectangle,align


def rectangle_stats(level,references):
    distribution = Counter()
    oversized = []
    count = 0
    for kind,index,corners in references:
        if kind!='object':
            continue
        rect,_ = texture_rectangle(level,level.object_textures[index],corners)
        _,_,w,h = rect
        size = ((w+1)//2)*h
        count += 1
        distribution[f'{w}x{h}'] += 1
        if size>2048:
            oversized.append(dict(texture=index,width=w,height=h,ci4_bytes=size))
    return dict(count=count,size_distribution=dict(sorted(distribution.items())),
        over_2048_count=len(oversized),over_2048=oversized,
        byte_rule='ceil(width/2)*height, no borders and no row alignment')


def measure(level):
    refs = referenced_textures(level)
    rectangles = []
    referenced_palettes = set()
    for kind,index,corners in refs:
        texture = (level.object_textures if kind=='object' else level.sprite_textures)[index]
        rect,_ = texture_rectangle(level,texture,corners)
        rectangles.append(dict(kind=kind,index=index,page=texture.page,rect=rect))
        if texture.palette is not None:
            referenced_palettes.add(level.cluts[texture.palette])
    parent = list(range(len(rectangles)))

    def root(i):
        while parent[i]!=i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    by_page = {}
    for i,row in enumerate(rectangles):
        by_page.setdefault(row['page'],[]).append(i)
    # 256x256 pages and hundreds of rectangles: an explicit per-row overlap
    # sweep avoids quadratic pair tests while still identifying exact connected
    # components. A rectangle connects through any shared texel, not adjacency.
    for page,indices in by_page.items():
        owners = [[None]*256 for _ in range(256)]
        for i in indices:
            x,y,w,h = rectangles[i]['rect']
            for yy in range(y,y+h):
                for xx in range(x,x+w):
                    previous = owners[yy][xx]
                    if previous is not None:
                        ri,rp = root(i),root(previous)
                        if ri!=rp:
                            parent[ri] = rp
                    owners[yy][xx] = i
    members = {}
    for i in range(len(rectangles)):
        members.setdefault(root(i),[]).append(i)
    clusters = []
    for group in members.values():
        mask = [0]*256
        for i in group:
            x,y,w,h = rectangles[i]['rect']
            row_mask = ((1<<w)-1)<<x
            for yy in range(y,y+h):
                mask[yy] |= row_mask
        area = sum(row.bit_count() for row in mask)
        x0 = min(rectangles[i]['rect'][0] for i in group)
        y0 = min(rectangles[i]['rect'][1] for i in group)
        x1 = max(rectangles[i]['rect'][0]+rectangles[i]['rect'][2] for i in group)
        y1 = max(rectangles[i]['rect'][1]+rectangles[i]['rect'][3] for i in group)
        # Row runs demonstrate an addressable sparse layout, rather than
        # calling an arbitrary union a rectangular DMA image.
        runs = []
        sparse_bytes = 0
        for yy in range(y0,y1):
            xx = x0
            while xx<x1:
                if not ((mask[yy]>>xx)&1):
                    xx += 1
                    continue
                start = xx
                while xx<x1 and ((mask[yy]>>xx)&1):
                    xx += 1
                length = xx-start
                runs.append(dict(x=start,y=yy,width=length))
                sparse_bytes += (length+1)//2
        w,h = x1-x0,y1-y0
        clusters.append(dict(page=rectangles[group[0]]['page'],x=x0,y=y0,width=w,height=h,
            references=[dict(kind=rectangles[i]['kind'],texture=rectangles[i]['index']) for i in group],
            union_texels=area,union_tightly_packed_bytes=(area+1)//2,
            bounding_image_bytes=((w+1)//2)*h,
            sparse_row_image_bytes=sparse_bytes,row_runs=len(runs),runs=runs))
    native = None
    if level.source=='psx':
        page_bytes = sum(len(page) for page in level.images)
        clut_bytes = sum(len(clut) for clut in level.cluts)
        native = dict(page_bytes=page_bytes,clut_bytes=clut_bytes,bytes=page_bytes+clut_bytes,
                      pages=len(level.images),source_cluts=len(level.cluts))
    palette_count = len(level.cluts) if level.source=='psx' else len(clusters)
    palette_bytes = palette_count*32
    union = sum(c['union_tightly_packed_bytes'] for c in clusters)
    bounding = sum(c['bounding_image_bytes'] for c in clusters)
    sparse = sum(c['sparse_row_image_bytes'] for c in clusters)
    descriptors = 16*len(clusters)
    row_directory = 8*sum(c['row_runs'] for c in clusters)
    return dict(native_pages=native,clusters=clusters,cluster_count=len(clusters),
        referenced_rectangles=len(refs),union_texels=sum(c['union_texels'] for c in clusters),
        union_ci4_image_bytes=union,palette_count=palette_count,palette_bytes=palette_bytes,
        union_payload_bytes=union+palette_bytes,
        bounding_ci4_image_bytes=bounding,bounding_payload_bytes=bounding+palette_bytes,
        cluster_descriptor_bytes=descriptors,bounding_uploadable_bytes=bounding+palette_bytes+descriptors,
        sparse_row_image_bytes=sparse,sparse_directory_bytes=row_directory,
        sparse_addressable_bytes=sparse+palette_bytes+descriptors+row_directory,
        distinct_source_palettes=len(set(level.cluts)) if level.source=='psx' else (1 if level.palette is not None else 0),
        referenced_distinct_source_palettes=len(referenced_palettes) if level.source=='psx' else (1 if refs else 0),
        object_rectangles_referenced=rectangle_stats(level,refs),
        object_rectangles_all_four_corners=rectangle_stats(level,[('object',i,4) for i in range(len(level.object_textures))]))
