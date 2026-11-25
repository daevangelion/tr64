#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import wave
from n64_music_source import SECTOR, cue_tracks, extract_cdda, frames, iso_files
from n64_video_source import survey


def record(name, extent, size, flags=0):
    length = 33 + len(name) + (not (len(name) & 1))
    raw = bytearray(length)
    raw[0] = length
    struct.pack_into('<I', raw, 2, extent)
    struct.pack_into('>I', raw, 6, extent)
    struct.pack_into('<I', raw, 10, size)
    struct.pack_into('>I', raw, 14, size)
    raw[25] = flags
    raw[28:32] = b'\x01\x00\x00\x01'
    raw[32] = len(name)
    raw[33:33 + len(name)] = name
    return raw


class SourceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.bin = self.root / 'disc.bin'
        self.cue = self.root / 'disc.cue'
        self.cue.write_text('FILE "disc.bin" BINARY\n TRACK 01 MODE2/2352\n INDEX 01 00:00:00\n'
                            ' TRACK 02 AUDIO\n INDEX 00 00:00:30\n INDEX 01 00:00:32\n')
        self.raw = bytearray(34 * SECTOR)
        for i in range(30):
            self.raw[i * SECTOR + 15] = 2
            self.raw[i * SECTOR + 16:i * SECTOR + 24] = bytes([1, 1, 8, 0]) * 2
        pvd = 16 * SECTOR + 24
        self.raw[pvd:pvd + 7] = b'\x01CD001\x01'
        self.raw[pvd + 156:pvd + 190] = record(b'\0', 20, 2048, 2)
        root = 20 * SECTOR + 24
        directory = record(b'\0', 20, 2048, 2) + record(b'\1', 20, 2048, 2) + record(b'CLIP.FMV;1', 21, 4096)
        self.raw[root:root + len(directory)] = directory
        struct.pack_into('<IHHIIHH', self.raw, 21 * SECTOR + 24,
                         0x80010160, 0, 1, 1, 40, 320, 208)
        self.raw[22 * SECTOR + 16:22 * SECTOR + 24] = bytes([1, 1, 0x64, 1]) * 2
        self.pcm = struct.pack('<4h', -32768, -1, 0, 32767) * (2 * SECTOR // 8)
        self.raw[32 * SECTOR:] = self.pcm
        self.bin.write_bytes(self.raw)

    def test_cdda_excludes_pregap_and_preserves_pcm(self):
        binary, tracks = cue_tracks(self.cue)
        self.assertEqual(binary, self.bin)
        self.assertEqual(tracks[0]['sectors'], 30)
        self.assertEqual(tracks[1]['frames'], 1176)
        output = self.root / 'track.wav'
        info = extract_cdda(self.cue, 2, output)
        with wave.open(str(output), 'rb') as audio:
            self.assertEqual((audio.getnchannels(), audio.getsampwidth(), audio.getframerate()), (2, 2, 44100))
            self.assertEqual(audio.readframes(1176), self.pcm)
        self.assertEqual(info['source_sha256'], hashlib.sha256(self.pcm).hexdigest())
        with self.assertRaises(ValueError):
            extract_cdda(self.cue, 1, self.root / 'wrong.wav')

    def test_iso_and_str_xa_are_distinct_from_cdda(self):
        self.assertEqual(iso_files(self.cue), [{'path': '/CLIP.FMV', 'sector': 21, 'iso_bytes': 4096}])
        output = self.root / 'clip.str'
        result = survey(self.cue, 'CLIP.FMV', output)
        self.assertEqual(output.read_bytes(), self.raw[21 * SECTOR:23 * SECTOR])
        self.assertEqual(result[0]['frames'], 1)
        self.assertEqual(result[0]['dimensions'], [(320, 208)])
        xa = result[0]['xa_audio'][0]
        self.assertEqual((xa['file'], xa['channel'], xa['rate'], xa['channels'], xa['bits']), (1, 1, 37800, 2, 4))
        self.assertAlmostEqual(xa['decoded_seconds'], 2016 / 37800)

    def test_bad_cue_span_and_truncation_reject(self):
        with self.assertRaises(ValueError):
            frames('00:60:00')
        self.cue.write_text(self.cue.read_text().replace('00:00:32', '00:00:34'))
        with self.assertRaises(ValueError):
            cue_tracks(self.cue)
        self.bin.write_bytes(self.raw[:-1])
        with self.assertRaises(ValueError):
            cue_tracks(self.cue)

    def test_corrupt_subheader_and_incomplete_video_reject(self):
        self.raw[21 * SECTOR + 20] ^= 1
        self.bin.write_bytes(self.raw)
        with self.assertRaisesRegex(ValueError, 'invalid video sector'):
            survey(self.cue)
        self.raw[21 * SECTOR + 20] ^= 1
        struct.pack_into('<H', self.raw, 21 * SECTOR + 30, 2)
        self.bin.write_bytes(self.raw)
        with self.assertRaisesRegex(ValueError, 'incomplete STR frame'):
            survey(self.cue)

    def test_pc_explicit_mapping_and_content_cache(self):
        mapping = self.root / 'mapping.json'
        mapping.write_text(json.dumps({'5': 2, '57': None}))
        encoder = self.root / 'spu-encoder'
        subprocess.run(['rtk', 'cc', '-O2', 'tools/cook/n64_music_spu.c', '-o', str(encoder)], check=True)
        command = ['python3', 'tools/cook/n64_music.py', '--source', 'pc',
                   '--cue', str(self.cue), '--mapping', str(mapping),
                   '--tracks', '5', '57', '--spu-encoder', str(encoder),
                   '--out', str(self.root / 'content')]
        first = json.loads(subprocess.check_output(command))
        cached = json.loads(subprocess.check_output(command))
        self.assertEqual(first['content_key'], cached['content_key'])
        self.assertTrue(cached['cache_hit'])
        self.raw[32 * SECTOR] ^= 1
        self.bin.write_bytes(self.raw)
        changed = json.loads(subprocess.check_output(command))
        self.assertNotEqual(first['content_key'], changed['content_key'])
        missing_map = subprocess.run(command[:6] + command[8:], capture_output=True)
        self.assertNotEqual(missing_map.returncode, 0)


if __name__ == '__main__':
    unittest.main()
