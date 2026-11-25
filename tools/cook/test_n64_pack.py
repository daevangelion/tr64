# SPDX-License-Identifier: MIT
"""Synthetic controls for raw streaming partitions and content-addressed storage."""
import argparse
import struct
import sys
import unittest
import zlib
import n64_pack as pack
import n64_core
import pack_merge
from n64_validate import IO_BYTES
import src_pc
import synthetic

class StreamPack(unittest.TestCase):
    def entries(self, blob):
        levels=struct.unpack_from('>I',blob,12)[0]; result=[]
        for i in range(levels):
            _,offset,count,_,_=pack.LEVEL.unpack_from(blob,32+i*48)
            result.append([pack.ENTRY.unpack_from(blob,offset+j*64) for j in range(count)])
        return result

    def test_game_version_is_per_directory_entry(self):
        payload = [pack.Section(1,b'A'*16)]
        image = bytearray(pack.build({'legacy':payload,'sequel':payload,'tr3':payload},
                                     games={'sequel':2,'tr3':3}))
        self.assertEqual(struct.unpack_from('>I', image, 76), (0,))
        self.assertEqual(struct.unpack_from('>I', image, 124), (2,))
        self.assertEqual(struct.unpack_from('>I', image, 172), (3,))
        self.assertEqual(pack.inspect(image),
                         {'legacy':{1:b'A'*16},'sequel':{1:b'A'*16},'tr3':{1:b'A'*16}})
        struct.pack_into('>I', image, 172, 4)
        struct.pack_into('>I', image, 24, zlib.crc32(image[32:176]))
        with self.assertRaisesRegex(ValueError, 'pack tr3: unsupported game version 4'):
            pack.inspect(image)

    def test_overlaps_scratch_independent_of_resident_fit(self):
        level=src_pc.read(synthetic.encode(synthetic.fixture('pc')))
        level.data['commands']=[1,-2,3,4]
        tx,manifest=n64_core.texture(level)
        # Preserve the fixture's referenced chain. The loader scans all
        # words, including this unreferenced tail, in its temporary bitset.
        level.data['overlaps'] += [0] * (8*IO_BYTES-len(level.data['overlaps']))
        sections=n64_core.encode(level,manifest)+[pack.Section(0x10000,tx)]
        self.assertGreater(pack.level_layout(sections)['margin_bytes'],0)
        level.data['overlaps'].append(0)
        with self.assertRaisesRegex(ValueError,'overlaps: validation scratch exceeds io pool'):
            n64_core.encode(level,manifest)

    def test_explicit_sample_boundaries(self):
        data=b'H'*16+b'A'*4096+b'B'*16+b'C'*16
        section=pack.Section(0x40000,data,header_bytes=16,chunk_bytes=4096,
            compression='none',boundaries=(16,4112,4128))
        self.assertEqual([(offset,len(raw)) for offset,raw in pack.parts(section)],
            [(0,16),(16,4096),(4112,16),(4128,16)])
        blob=pack.build({'one':[section]},self.codec)
        self.assertTrue(all(e[3]==e[4] and e[6]==0 for e in self.entries(blob)[0]))
        self.assertEqual(pack.inspect(blob)['one'][0x40000],data)

    def test_none_wins_shared_digest_in_either_order(self):
        data=b'A'*4096
        auto=pack.Section(1,data)
        raw=pack.Section(0x40000,data,compression='none')
        compressed=pack.build({'one':[auto]},self.codec)
        self.assertEqual(self.entries(compressed)[0][0][6],1)
        self.assertEqual(pack.inspect(compressed)['one'][1],data)
        for first,second in ((auto,raw),(raw,auto)):
            with self.subTest(first=first.compression):
                blob=pack.build({'a':[first],'b':[second]},self.codec)
                a,b=(row[0] for row in self.entries(blob))
                self.assertEqual(a[2],b[2])
                self.assertEqual((a[3],a[4],a[6]),(4096,4096,0))
                self.assertEqual((b[3],b[4],b[6]),(4096,4096,0))
                self.assertEqual(pack.inspect(blob),{'a':{first.kind:data},'b':{second.kind:data}})

    def test_invalid_boundaries(self):
        for cuts in ((0,16),(16,64),(16,16),(32,16),(4,16),(16,63),(-1,16)):
            with self.subTest(cuts=cuts):
                section=pack.Section(0x40000,bytes(64),header_bytes=16,chunk_bytes=32,
                    compression='none',boundaries=cuts)
                with self.assertRaisesRegex(ValueError,'bound'):
                    list(pack.parts(section))
        with self.assertRaisesRegex(ValueError,'split header'):
            list(pack.parts(pack.Section(1,bytes(64),header_bytes=64,boundaries=(32,))))

    def test_invalid_compression(self):
        for value in ('lz4',False,None):
            with self.subTest(value=value),self.assertRaisesRegex(ValueError,'compression'):
                pack.build({'one':[pack.Section(1,b'abc',compression=value)]},self.codec)

    def test_pack_merge_dedup_and_controls(self):
        shared = b'S' * 4096
        snd_a = b'H' * 16 + b'X' * 4096
        snd_b = b'H' * 16 + b'Y' * 4096
        p1 = pack.build(
            {'wall': [pack.Section(1, shared),
                      pack.Section(0x40000, snd_a, header_bytes=16, chunk_bytes=4096,
                                   compression='none', boundaries=(16,))]},
            self.codec, games={'wall': 2})
        p2 = pack.build(
            {'opera': [pack.Section(1, shared),
                       pack.Section(0x40000, snd_b, header_bytes=16, chunk_bytes=4096,
                                    compression='none', boundaries=(16,))]},
            self.codec, games={'opera': 2})
        merged, report = pack_merge.merge_blobs([p1, p2], self.codec, expected_game=2)
        self.assertEqual(report['levels'], ['opera', 'wall'])
        self.assertEqual(report['games'], {'opera': 2, 'wall': 2})
        self.assertGreater(report['dedup_saved_bytes'], 0)
        decoded = pack.inspect(merged)
        self.assertEqual(decoded['wall'][1], shared)
        self.assertEqual(decoded['opera'][1], shared)
        self.assertEqual(decoded['wall'][0x40000], snd_a)
        self.assertEqual(decoded['opera'][0x40000], snd_b)
        # Raw sound chunks stay uncompressed and preserve explicit 16-byte header boundary.
        for rows in self.entries(merged):
            snd_rows = [r for r in rows if r[0] == 0x40000]
            self.assertEqual([r[1] for r in snd_rows], [0, 16])
            self.assertTrue(all(r[6] == 0 and r[3] == r[4] for r in snd_rows))
        # Negative controls: duplicate level name and game mismatch.
        with self.assertRaisesRegex(ValueError, 'duplicate pack entry'):
            pack_merge.merge_blobs([p1, p1], self.codec, expected_game=2)
        with self.assertRaisesRegex(ValueError, 'game 2 != expected 3'):
            pack_merge.merge_blobs([p1], self.codec, expected_game=3)

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--cache',required=True)
    options,remaining=parser.parse_known_args()
    StreamPack.codec=staticmethod(pack.compressor(options.cache))
    unittest.main(argv=[sys.argv[0],*remaining])
