# SPDX-License-Identifier: MIT
"""Synthetic music manifests through the production unified-pack composer."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import n64_music
import n64_pack as pack
from pack_level import add_music_catalog


class MusicComposition(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        payload = bytes([12]) + bytes(15)
        payload *= 600
        track = n64_music.HEADER.pack(b'MU64', 1, 64 + len(payload), 0, 2,
                                     1, 22050, 1, 600 * 28, 64, len(payload),
                                     16, 28, 0, 0, 0) + payload
        self.data = {'MUSIC_02': track, 'MUSIC_57': n64_music.unavailable(57)}
        rows = []
        for name, data in self.data.items():
            (self.root / (name + '.bin')).write_bytes(data)
            rows.append(dict(name=name, requested_track=int(name[-2:]),
                             file=name + '.bin', section_bytes=len(data),
                             sha256=hashlib.sha256(data).hexdigest()))
        self.manifest = self.root / 'manifest.json'
        self.manifest.write_text(json.dumps(dict(version=1, tracks=rows)))

    def test_shared_binding_lossless_raw_tracks_and_identity(self):
        resident = [pack.Section(0x10000, b'T' * 17),
                    pack.Section(0x40000, b'S' * 32, compression='none')]
        entries = {'one': resident}
        before = pack.level_layout(resident)
        identity = add_music_catalog(entries, self.manifest)
        self.assertEqual(identity, hashlib.sha256(self.manifest.read_bytes()).hexdigest())
        self.assertEqual(pack.level_layout(entries['one']), before)
        blob = pack.build(entries, self.codec)
        decoded = pack.inspect(blob)
        self.assertEqual(set(decoded), {'one', 'MUSIC_02', 'MUSIC_57'})
        for name, data in self.data.items():
            self.assertEqual(decoded[name], {n64_music.KIND: data})
        count = struct.unpack_from('>I', blob, 12)[0]
        for i in range(count):
            name, offset, parts, _, _ = pack.LEVEL.unpack_from(blob, 32 + i * 48)
            if not name.startswith(b'MUSIC_'):
                continue
            for j in range(parts):
                row = pack.ENTRY.unpack_from(blob, offset + j * 64)
                self.assertEqual((row[0], row[3], row[6]), (n64_music.KIND, row[4], 0))
                self.assertLessEqual(row[4], 4096)

    def test_collision_does_not_replace_level(self):
        original = [pack.Section(1, b'level')]
        entries = {'MUSIC_02': original}
        with self.assertRaisesRegex(ValueError, 'collides'):
            add_music_catalog(entries, self.manifest)
        self.assertEqual(entries, {'MUSIC_02': original})

    def test_manifest_bytes_are_part_of_identity(self):
        first = add_music_catalog({}, self.manifest)
        self.manifest.write_bytes(self.manifest.read_bytes() + b'\n')
        second = add_music_catalog({}, self.manifest)
        self.assertNotEqual(first, second)

    def test_bad_payload_is_rejected_before_merge(self):
        path = self.root / 'MUSIC_02.bin'
        data = bytearray(path.read_bytes())
        data[-1] ^= 1
        path.write_bytes(data)
        entries = {'one': [pack.Section(1, b'level')]}
        with self.assertRaisesRegex(ValueError, 'payload hash'):
            add_music_catalog(entries, self.manifest)
        self.assertEqual(set(entries), {'one'})

    def test_directory_is_not_a_manifest_file(self):
        with self.assertRaises(IsADirectoryError):
            add_music_catalog({}, self.root)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--cache', required=True)
    options, remaining = parser.parse_known_args()
    MusicComposition.codec = staticmethod(pack.compressor(options.cache))
    unittest.main(argv=[sys.argv[0], *remaining])
