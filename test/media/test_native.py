#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
import argparse
import json
import math
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import n64_music as music
import n64_pack as pack


class NativeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.out.mkdir(parents=True, exist_ok=True)
        cls.encoder = cls.out / 'spu-encoder'
        cls.decoder = cls.out / 'music-decoder'
        subprocess.run(['rtk', 'cc', '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                        'tools/cook/n64_music_spu.c', '-o', str(cls.encoder)], check=True)
        subprocess.run(['rtk', 'cc', '-std=c99', '-m32', '-O2', '-Wall', '-Wextra', '-Werror', '-I.', '-Iinclude',
                        'test/media/decode.c', 'engine/snd/music_decode.c', 'engine/snd/music_format.c',
                        '-o', str(cls.decoder)], check=True)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=self.out)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_spu_source_recurrence_and_padding_trim(self):
        # Independent hand-authored predictor0 control, not fitted encoder data.
        raw = bytes([0, 0]) + bytes([0x71, 0xf8]) * 7
        data = music.HEADER.pack(b'MU64', 1, 80, 0, 5, 1, 16000, 1, 27, 64, 16, 16, 28, 0, 0, 0) + raw
        path = self.root / 'spu.mu64';path.write_bytes(data)
        actual = subprocess.check_output([str(self.decoder), str(path)])
        expected = struct.pack('<' + 'h' * 28, *([4096, 28672, -32768, -4096] * 7))[:54]
        self.assertEqual(actual, expected)
        pcm = struct.pack('<' + 'h' * 1057, *[int(14000 * math.sin(i * .031)) for i in range(1057)])
        cooked = music.encode(pcm, 22050, 5, self.encoder)
        path.write_bytes(cooked)
        self.assertEqual(len(subprocess.check_output([str(self.decoder), str(path)])), len(pcm))
        blob = pack.build({'MUSIC_05': [music.section(cooked, 5)]})
        self.assertEqual(pack.inspect(blob)['MUSIC_05'][music.KIND], cooked)
        self.assertTrue(all(len(data) <= 4096 for _, data in pack.parts(music.section(cooked, 5))))

    def test_correctly_pack_hashed_bad_native_headers_reject(self):
        good = music.encode(bytes(2200), 22050, 13, self.encoder)
        for offset, value in [(4, 2), (12, 2), (20, 2), (24, 44100), (28, 2),
                              (32, 0), (44, 15), (52, 1), (60, 1)]:
            bad = bytearray(good);struct.pack_into('>I', bad, offset, value)
            blob = pack.build({'MUSIC_13': [pack.Section(music.KIND, bytes(bad), compression='none')]})
            path = self.root / 'bad.mu64';path.write_bytes(pack.inspect(blob)['MUSIC_13'][music.KIND])
            result = subprocess.run([str(self.decoder), str(path)], capture_output=True)
            self.assertEqual(result.returncode, 3, (offset, result.stderr))
        bad = bytearray(good);bad[64] = 0xf0;path.write_bytes(bad)
        self.assertEqual(subprocess.run([str(self.decoder), str(path)], capture_output=True).returncode, 3)

    def test_unavailable_has_no_invented_samples_and_catalog_hashes(self):
        data = music.unavailable(57)
        self.assertFalse(music.validate(data, 57)['available'])
        path = self.root / 'MUSIC_57.bin';path.write_bytes(data)
        manifest = self.root / 'manifest.json'
        row = {'name': 'MUSIC_57', 'requested_track': 57, 'file': path.name,
               'section_bytes': len(data), 'sha256': music.digest(data)}
        manifest.write_text(json.dumps({'version': 1, 'tracks': [row]}))
        self.assertEqual(music.read_music_catalog(manifest)['MUSIC_57'][0].data, data)
        path.write_bytes(data[:-1] + b'\1')
        with self.assertRaisesRegex(ValueError, 'payload hash'):
            music.read_music_catalog(manifest)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', type=Path, required=True)
    args, rest = parser.parse_known_args()
    NativeTests.out = args.out
    unittest.main(argv=['test_native.py', *rest])
