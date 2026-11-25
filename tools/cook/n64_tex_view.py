# SPDX-License-Identifier: MIT
"""Independent host/test-only compact TX64 decoder; no writer imports."""
import math
import struct

import numpy as np
from PIL import Image


class Section:
    def __init__(self, data):
        self.data = data
        if len(data) < 80:
            raise ValueError('truncated TX64 header')
        h = struct.unpack_from('>4s19I', data)
        if h[0] != b'TX64' or h[1] != 1 or h[2] != len(data) or h[17] not in (0, 1) or any(h[18:]):
            raise ValueError('TX64 magic/version/length/reserved')
        self.odd_width = bool(h[17])
        self.objects, self.sprites, nc, npal, ns, nf, na = h[3:10]
        co, po, oo, so, lo, fo, ao = h[10:17]
        self.clusters = self.table(co, nc, '>I4HI')
        self.palettes = self.table(po, npal, '>IHH')
        object_rows = self.table(oo, self.objects, '>HH4B8B2B3H')
        sprite_rows = self.table(so, self.sprites, '>HH4B3H4h')
        self.splits = [r[0] for r in self.table(lo, ns, '>H')]
        self.flat = self.table(fo, nf, '>4H')
        self.animations = [r[0] for r in self.table(ao, na, '>H')]
        self.descriptors = []
        for i, row in enumerate(object_rows):
            ci, pi, x, y, wm, hm = row[:6]
            self.descriptors.append(dict(cluster=ci, palette=pi, x=x, y=y, width=wm+1, height=hm+1,
                uv=tuple(zip(row[6:14:2], row[7:14:2])), flags=row[14], triangle_map=row[15],
                source_index=row[16], first_split=row[17], split_count=row[18], kind='object'))
        for row in sprite_rows:
            ci, pi, x, y, wm, hm, source, first, count, *bounds = row
            self.descriptors.append(dict(cluster=ci, palette=pi, x=x, y=y, width=wm+1, height=hm+1,
                uv=((0, 0), (wm, 0), (wm, hm), (0, hm)), flags=2, triangle_map=0x24,
                source_index=source, first_split=first, split_count=count, kind='sprite', bounds=bounds))
        for off, w, height, row, border, zero in self.clusters:
            if not w or not height or w > 256 or height > 256 or border or zero or row != (w+1)//2:
                raise ValueError('invalid cluster extent/stride/border')
            self.region(off, row*height)
        for poff, stp, zero in self.palettes:
            if zero:
                raise ValueError('invalid palette reserved')
            self.region(poff, 32)
        for index, d in enumerate(self.descriptors):
            ci, pi = d['cluster'], d['palette']
            if ci >= nc or pi >= npal or d['first_split']+d['split_count'] > ns:
                raise ValueError('invalid descriptor reference')
            c = self.clusters[ci]
            if d['x']+d['width'] > c[1] or d['y']+d['height'] > c[2]:
                raise ValueError('descriptor extent')
            if d['x'] % 2 or (not h[17] and d['width'] % 2):
                raise ValueError('CI4 descriptor requires even cluster-local x and right end')
            if d['flags'] not in (1, 2, 4) or d['triangle_map'] not in (0x24, 0x34):
                raise ValueError('descriptor flags/map')
            plan = self.tiles(index)
            if any(t[5] > 2048 for t in plan):
                raise ValueError('tile TMEM limit')
        for kind in ('object', 'sprite'):
            sources = [d['source_index'] for d in self.descriptors if d['kind'] == kind]
            if sources != sorted(set(sources)):
                raise ValueError('source ids not strictly increasing')
        p = 1
        if not self.animations:
            raise ValueError('missing animation count')
        for _ in range(self.animations[0]):
            if p >= len(self.animations):
                raise ValueError('truncated animation group')
            count = self.animations[p]
            if count < 1 or p+1+count > len(self.animations):
                raise ValueError('truncated animation members')
            if any(i >= self.objects for i in self.animations[p+1:p+1+count]):
                raise ValueError('animation descriptor reference')
            p += 1+count
        if p != len(self.animations):
            raise ValueError('unexplained animation words')

    def region(self, offset, size):
        if offset < 80 or offset % 8 or offset+size > len(self.data):
            raise ValueError('TX64 offset/alignment/length')
        return memoryview(self.data)[offset:offset+size]

    def table(self, offset, count, fmt):
        s = struct.Struct(fmt)
        return list(s.iter_unpack(self.region(offset, count*s.size)))

    def tiles(self, index):
        d = self.descriptors[index]
        first, count = d['first_split'], d['split_count']
        boundaries = [0]+self.splits[first:first+count]+[d['height']]
        if boundaries != sorted(set(boundaries)):
            raise ValueError('invalid tile split lines')
        # Independently round both CI4 load endpoints to an even texel.
        x0 = d['x']-d['x'] % 2
        x1 = d['x']+d['width']
        x1 += x1 % 2
        row = ((x1-x0)//2+7)//8*8
        return [(0, y0, d['width'], y1-y0, row, row*(y1-y0)) for y0, y1 in zip(boundaries, boundaries[1:])]

    def indices(self, cluster):
        off, w, h, stride, *_ = self.clusters[cluster]
        raw = np.frombuffer(self.region(off, stride*h), np.uint8).reshape(h, stride)
        pixels = np.empty((h, stride*2), dtype=np.uint8)
        pixels[:, ::2] = raw >> 4
        pixels[:, 1::2] = raw & 15
        return pixels[:, :w]

    def decode(self, descriptor):
        d = self.descriptors[descriptor]
        po, mask, _ = self.palettes[d['palette']]
        words = np.frombuffer(self.region(po, 32), '>u2')
        stp = np.array([(mask >> i) & 1 for i in range(16)], dtype=np.uint8)
        indices = self.indices(d['cluster'])[d['y']:d['y']+d['height'], d['x']:d['x']+d['width']]
        return words[indices].astype(np.uint16), stp[indices]

    def image(self, descriptor):
        words, _ = self.decode(descriptor)
        return image(words)


def image(words):
    words = np.asarray(words, dtype=np.uint16)
    rgb = np.stack(((words >> 11) & 31, (words >> 6) & 31, (words >> 1) & 31), axis=-1)
    rgb = ((rgb << 3) | (rgb >> 2)).astype(np.uint8)
    alpha = ((words & 1)*255).astype(np.uint8)
    return Image.fromarray(np.dstack((rgb, alpha)))


def measure(source, decoded):
    """Independent metric arithmetic; RGB555 units, alpha checked separately."""
    a = np.asarray(source, dtype=np.uint16).reshape(-1)
    b = np.asarray(decoded, dtype=np.uint16).reshape(-1)
    if a.size != b.size or not a.size:
        raise ValueError('metric image dimensions')
    if not np.array_equal(a & 1, b & 1):
        raise AssertionError('alpha differs')
    total, maximum = 0, 0
    for shift in (11, 6, 1):
        delta = ((a >> shift) & 31).astype(np.int64) - ((b >> shift) & 31).astype(np.int64)
        total += int(np.dot(delta, delta))
        maximum = max(maximum, int(np.abs(delta).max()))
    mse = total/(3*a.size)
    return dict(psnr=None if total == 0 else 10*math.log10(961/mse), exact=total == 0,
                max_error=maximum, mse=mse, samples=3*a.size)
