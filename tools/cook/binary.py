# SPDX-License-Identifier: MIT
"""Checked input cursor and named physical byte ledger."""
import struct
from contextlib import contextmanager
from ir import SCHEMAS


class FormatError(ValueError):
    pass


class Reader:
    def __init__(self, data, label, origin=0):
        self.data, self.label, self.origin = memoryview(data), label, origin
        self.pos = 0
        self.sections = []

    def take(self, size):
        if size < 0 or size > len(self.data) - self.pos:
            raise FormatError(f'{self.label}: truncated at {self.origin+self.pos}: '
                              f'need {size}, remaining {len(self.data)-self.pos}')
        start = self.pos
        self.pos += size
        return bytes(self.data[start:self.pos])

    def unpack(self, fmt):
        return struct.unpack('<' + fmt, self.take(struct.calcsize('<' + fmt)))

    def scalar(self, fmt):
        return self.unpack(fmt)[0]

    def count(self, fmt='I', minimum=1):
        n = self.scalar(fmt)
        if n < 0 or n * minimum > len(self.data) - self.pos:
            raise FormatError(f'{self.label}: invalid count {n} at '
                              f'{self.origin+self.pos-struct.calcsize(fmt)}')
        return n

    def rows(self, fmt, n):
        size = struct.calcsize('<' + fmt)
        return list(struct.iter_unpack('<' + fmt, self.take(n * size)))

    def records(self, name, n):
        fmt, fields = SCHEMAS[name]
        return [dict(zip(fields, row)) for row in self.rows(fmt, n)]

    @contextmanager
    def section(self, name, count=None, prefix=0):
        start = self.pos
        meta = {'name': name, 'offset': self.origin + start, 'count': count,
                'prefix_bytes': prefix}
        yield meta
        meta['bytes'] = self.pos - start
        meta['payload_bytes'] = meta['bytes'] - prefix
        self.sections.append(meta)

    def finish(self):
        if self.pos != len(self.data):
            raise FormatError(f'{self.label}: trailing bytes: {len(self.data)-self.pos} '
                              f'at {self.origin+self.pos}')


def array(r, level, name, fmt=None, count_fmt='I', count=None):
    with r.section(name, count, struct.calcsize(count_fmt) if count is None else 0) as meta:
        if count is None:
            count = r.count(count_fmt, struct.calcsize('<'+(fmt or SCHEMAS[name][0])))
        meta['count'] = count
        if fmt:
            rows = r.rows(fmt, count)
            value = [row[0] for row in rows] if len(rows[0] if rows else ()) <= 1 else rows
        else:
            value = r.records(name, count)
        level.data[name] = value
    return value
