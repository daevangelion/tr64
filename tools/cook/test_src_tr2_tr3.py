# SPDX-License-Identifier: MIT
"""Synthetic, independently laid-out sequel files; no retail fixture bytes."""
import struct
import unittest

from account_tr2_tr3 import check
from binary import FormatError
from src_tr2_tr3 import TextureOnly, read, read_bank


def pack(fmt, *values):
    return struct.pack('<'+fmt,*values)


def table(fmt, *rows):
    return pack('I',len(rows))+b''.join(pack(fmt,*row) for row in rows)


def wave(value):
    body = b'WAVEfmt '+pack('IHHIIHH',16,1,1,11025,22050,2,16)+b'data'+pack('Ih',2,value)
    return b'RIFF'+pack('I',len(body))+body


def fixture(game):
    vertex_tail = 0x7c1f if game == 3 else 4000
    geometry = pack('H',3)+b''.join(pack('3h3H',x,0,z,3999,0x8000,vertex_tail)
                                    for x,z in ((0,0),(100,0),(100,100)))
    geometry += pack('HH4HH2H',0,1,0,1,2,0x8000,1,0,0)
    room = pack('4iI',0,0,256,0,len(geometry)//2)+geometry
    room += pack('3H2HBbBb',0,1,1,1,0x0003 if game==3 else 0,255,1,255,0)
    room += pack('2H',1024,1028)+(pack('H',1) if game==2 else b'')+pack('H',1)
    room += (pack('3i3BB4h',1,2,3,10,20,30,0,10,20,30,40) if game == 3 else
             pack('3i2H2I',1,2,3,10,20,1024,2048))
    room += pack('H3i4HhH',1,1,2,3,0,100,200,1,-1,0)
    if game == 3:
        room += pack('3B',2,3,4)
    floor = [0,0x8004,0,0x0400,0,0x8000]  # camera payload overrides its action end bit
    if game == 3:
        floor[1:1] = [0x0067,0x1234,0x0013]  # split floor, monkey-swing
    else:
        floor[1:1] = [0x0106]  # +Z climb
    mesh = pack('3h3H',0,0,0,128,0,0)+pack('5H',0,0,0,0,0)+b'\0\0'
    objects = table('2H16B',(2 if game==3 else 1,0,1,0,1,0, 1,15,1,0, 1,15,1,15, 1,0,1,15))
    sprites = table('HBBHH4h',(0,20,20,15*256+255,15*256+255,-16,-16,16,16))
    raw = pack('I',0xff180038 if game==3 else 0x2d)+bytes(768)+bytes(1024)
    raw += pack('I',1)+bytes(65536)+bytes(131072)+pack('IH',0,1)+room
    raw += table('H',*((x,) for x in floor))+pack('I',len(mesh)//2)+mesh+table('I',(0,),(0,))
    raw += table('IBBHii8H',(0,1,10,0,0,0,0,0,0,0,1,0,0,0))+table('3H',(0,1,0))+table('4h',(0,0,0,0))
    raw += pack('II',0,0)+table('H',*((0,) for _ in range(9)),(0x4000,))
    raw += table('IHHIIH',(7,1,1,0,0,0))+table('IH12hH',(1,0,*([0]*12),0))
    raw += (objects if game == 2 else b'')+sprites+table('IhH',(100,-1,0))
    raw += table('3ihH',(6,7,8,0,0))+table('3iHH',(1,2,3,0,0xc0))
    raw += table('4BhH',(1,2,3,4,0,65535))+table('H')+pack('10h',*range(10))
    raw += table('H',(0,))+(objects if game==3 else b'')
    raw += table('HH3iH2hH',(7,0,1,2,3,4,-1,12,0))
    raw += bytes(8192)+pack('H8hH',1,*range(8),3)+b'\x01\0\x02'
    raw += pack('370h',0,*([-1]*369))
    raw += table('H4BH',(0,1,2,3,4,4)) if game==3 else table('4H',(0,100,200,4))
    return raw+table('I',(1,))


class SequelReaders(unittest.TestCase):
    def test_edition_sections_and_extensions(self):
        bank = read_bank(wave(0)+wave(9))
        for game in (2,3):
            with self.subTest(game=game):
                raw = fixture(game)
                level = read(raw,bank=bank)
                self.assertEqual(check(level,raw),len(raw))
                self.assertEqual(level.reserved['game'],game)
                self.assertEqual(level.images,[bytes(65536)])
                self.assertEqual(level.reserved['argb1555_pages'],[bytes(131072)])
                self.assertEqual(level.rooms[0].vertices[0]['attributes'],0x8000)
                self.assertEqual(level.rooms[0].vertices[0]['legacy_lighting'],3999)
                self.assertEqual(level.rooms[0].triangles[0].flags,0x8000)
                self.assertEqual(level.rooms[0].sectors[0]['box_index'],0)
                self.assertEqual(level.rooms[0].static_instances[0]['intensity2'],200)
                self.assertEqual(level.data['mesh_pointers'],[0,0])
                self.assertEqual(len(level.meshes),1)
                self.assertEqual(level.data['boxes'][0]['min_x'],3072)
                self.assertEqual(level.data['zones'],[0,1,4,5,6,9])
                self.assertEqual(level.data['extra_ground_zones'],[2,3,7,8])
                self.assertEqual(level.data['items'][0]['intensity2'],12)
                self.assertEqual(level.data['demo_data'],b'\x01\0\x02')
                self.assertEqual(level.samples,[wave(9)])
                self.assertEqual(level.reserved['floor_codes']['functions'],{4:1,7:1,19:1} if game==3 else {4:1,6:1})
                self.assertEqual(level.reserved['floor_codes']['actions'],{0:1,1:1})
                self.assertEqual(level.reserved['floor_codes']['unreferenced_words'],1)
                self.assertEqual(level.reserved['findings'],[])
                pos = 0
                for section in level.sections:
                    self.assertEqual(section['offset'],pos)
                    self.assertEqual(section['bytes'],section['prefix_bytes']+section['payload_bytes'])
                    pos += section['bytes']
                self.assertEqual(pos,len(raw))
                where = {r['name']:r['offset'] for r in level.sections}
                self.assertEqual(where['object_textures']>where['animated_textures'],game==3)
                if game == 3:
                    self.assertEqual(level.rooms[0].lights[0]['sun_or_point'],pack('4h',10,20,30,40))
                    self.assertEqual(level.rooms[0].vertices[0]['lighting']['value'],0x7c1f)
                    self.assertEqual(level.rooms[0].sectors[0]['material'],3)
                    self.assertEqual(level.rooms[0].reserved['filter'],4)
                    self.assertEqual(level.data['sound_details'][0]['pitch'],4)
                else:
                    self.assertEqual(level.rooms[0].lights[0]['radius2'],2048)
                    self.assertEqual(level.rooms[0].reserved['light_mode'],1)
    def test_independent_account_detects_ledger_mutation(self):
        raw = fixture(2)
        level = read(raw)
        level.sections[3]['count'] += 1
        with self.assertRaisesRegex(FormatError,'independent account disagrees.*texture_pages'):
            check(level,raw)


    def test_rejects_truncated_and_corrupted_count_by_name(self):
        for game in (2,3):
            with self.subTest(game=game):
                raw = fixture(game)
                label = f'fixture{game}.TR2'
                with self.assertRaisesRegex(FormatError,r'sample_ordinals: '+label+r': invalid count .* at \d+'):
                    read(raw[:-1],label)
                damaged = bytearray(raw)
                struct.pack_into('<I',damaged,1796,0xffffffff)
                with self.assertRaisesRegex(FormatError,r'texture_pages: '+label+r': invalid count .* at 1796'):
                    read(damaged,label)
                with self.assertRaisesRegex(FormatError,r'trailing bytes: 1 at'):
                    read(raw+b'\0',label)

    def test_strict_dispatch_error_and_sized_census_finding(self):
        raw = bytearray(fixture(3))
        section = next(x for x in read(raw).sections if x['name']=='ranges')
        field = section['offset']+8
        struct.pack_into('<h',raw,field,99)
        with self.assertRaisesRegex(FormatError,rf'ranges at {field}: dispatch 0 targets animation 99'):
            read(raw)
        level = read(raw,census=True)
        self.assertEqual(level.reserved['findings'][0]['offset'],field)
        self.assertEqual(level.data['ranges'][0]['next_animation'],99)

    def test_rejects_unknown_opcodes_even_for_census(self):
        raw = bytearray(fixture(3))
        section = next(x for x in read(raw).sections if x['name']=='floor_data')
        at = section['offset']+6
        struct.pack_into('<H',raw,at,0x801f)
        with self.assertRaisesRegex(FormatError,rf'floor_data at {at}.*unsupported floor function 0x1f'):
            read(raw,census=True)

    def test_known_signature_with_no_body_still_rejected_as_level(self):
        header = pack('I',0xff180034)+bytes(768+1024+4)
        with self.assertRaisesRegex(TextureOnly,'level_reserved at 1800: EOF'):
            read(header)

    def test_external_riff_boundaries_and_ordinals(self):
        with self.assertRaisesRegex(FormatError,'sample/1: MAIN.SFX: RIFF WAVE at 46 exceeds'):
            read_bank(wave(0)+wave(1)[:-1])
        broken = wave(1).replace(b'data',b'JUNK')
        with self.assertRaisesRegex(FormatError,"unsupported/duplicate WAVE chunk b'JUNK' at 36"):
            read_bank(broken)
        with self.assertRaisesRegex(FormatError,r'sample_ordinals at \d+: reference 1\+1 outside 1'):
            read(fixture(2),bank=[wave(0)])


if __name__ == '__main__':
    unittest.main()
