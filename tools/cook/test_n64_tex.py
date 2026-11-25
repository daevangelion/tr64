# SPDX-License-Identifier: MIT
"""Synthetic writer/independent decoder controls; no retail data."""
import math
import struct
import unittest
from unittest.mock import patch

import numpy as np

from ir import Face, Texture, face_uv
import synthetic
import n64_tex as tex
from n64_tex_view import Section, measure


def descriptor(view, manifest, source, kind='object'):
    return tex.descriptor_index(manifest, kind, source)


class TextureTests(unittest.TestCase):
    def test_roundtrip_and_palette_overlap(self):
        level = synthetic.fixture('psx')
        level.cluts[1] = struct.pack('<16H', 0, 0x8000, 0x801f, *range(3, 16))
        level.object_textures[257].palette = 1
        blob, manifest = tex.cook(level)
        view = Section(blob)
        a = view.descriptors[descriptor(view, manifest, 256)]
        b = view.descriptors[descriptor(view, manifest, 257)]
        self.assertEqual(a['cluster'], b['cluster'])
        self.assertNotEqual(a['palette'], b['palette'], 'overlap must retain distinct palettes')
        for i, r in enumerate(manifest['descriptors']):
            t = (level.object_textures if r['kind'] == 'object' else level.sprite_textures)[r['texture']]
            _, raw = tex.texture_rectangle(level, t, r['corners'])
            p, stp = tex.psx_palette(level.cluts[t.palette])
            indices = np.frombuffer(raw, np.uint8).reshape(r['rect'][3], r['rect'][2])
            words, decoded_stp = view.decode(i)
            np.testing.assert_array_equal(words, p[indices])
            np.testing.assert_array_equal(decoded_stp, stp[indices])
        self.assertGreater(len(view.descriptors), 0)

    def test_palette_deduplication_exact_count(self):
        level = synthetic.fixture('psx')
        level.object_textures[257].palette = 1
        level.cluts[1] = level.cluts[0]
        blob, m = tex.cook(level)
        v = Section(blob)
        self.assertEqual(len(v.palettes), 1)
        self.assertEqual(m['palettes'], 1)
        self.assertEqual(len({d['palette'] for d in v.descriptors}), 1)

    def test_touching_does_not_cluster_but_overlap_does(self):
        def row(x):
            return dict(page=0, rect=[x, 0, 4, 4])
        self.assertEqual(len(tex.clusters([row(0), row(4)])), 2)
        self.assertEqual(len(tex.clusters([row(0), row(3)])), 1)

    def test_transparent_and_stp_reversible(self):
        level = synthetic.fixture('psx')
        level.images = [bytes([0x10])*32768]
        level.cluts = [struct.pack('<16H', 0, 0x8000, *range(2, 16))]
        blob, m = tex.cook(level)
        words, stp = Section(blob).decode(descriptor(None, m, 256))
        self.assertEqual(int(words[0, 0]), 0)
        self.assertEqual(int(words[0, 1]), 1)
        self.assertEqual(int(stp[0, 1]), 1)

    def test_tmem_split_and_corruption_negative(self):
        level = synthetic.fixture('pc')
        level.object_textures[256].uv = ((0, 0), (71, 0), (71, 95), (0, 95))
        blob, m = tex.cook(level)
        v = Section(blob)
        plan = v.tiles(descriptor(v, m, 256))
        self.assertEqual([t[3] for t in plan], [51, 45])
        self.assertTrue(all(t[5] <= 2048 for t in plan))
        bad = bytearray(blob)
        split_off = struct.unpack_from('>I', bad, 56)[0]
        struct.pack_into('>H', bad, split_off, 52)
        with self.assertRaisesRegex(ValueError, 'TMEM'):
            Section(bad)

    def test_ci4_descriptor_endpoints_require_even_texels(self):
        # The low-level pitch arithmetic still models outward load rounding.
        self.assertEqual(tex.tiles(1, 16, 200)[0][4], 16)
        self.assertEqual(tex.tiles(0, 16, 200)[0][4], 8)
        for x, width in ((1, 16), (1, 15), (0, 17)):
            with self.subTest(x=x, width=width):
                level = synthetic.fixture('pc')
                level.object_textures[256].uv = ((0, 0), (17, 0), (17, 199), (0, 199))
                level.object_textures[257].uv = ((x, 0), (x+width-1, 0), (x+width-1, 199), (x, 199))
                with self.assertRaisesRegex(ValueError, 'even cluster-local x and right end'):
                    tex.cook(level)
        level = synthetic.fixture('pc')
        level.sprite_textures[0].uv = ((12, 12), (18, 19))
        with self.assertRaisesRegex(ValueError, 'even cluster-local x and right end'):
            tex.cook(level)

    def test_decoder_rejects_unaligned_descriptor_endpoints(self):
        blob, _ = tex.cook(synthetic.fixture('pc'))
        object_offset = struct.unpack_from('>I', blob, 48)[0]
        for x, width in ((1, 7), (0, 7)):
            with self.subTest(x=x, width=width):
                bad = bytearray(blob)
                bad[object_offset+4] = x
                bad[object_offset+6] = width-1
                with self.assertRaisesRegex(ValueError, 'even cluster-local x and right end'):
                    Section(bad)

    def test_tr2_odd_end_keeps_texels_and_clamp(self):
        level = synthetic.fixture('pc')
        page = np.full((256,256), 0xfc00, dtype='<u2')  # opaque red
        page[:,2] = 0x001f                           # transparent blue
        page[:,3] = 0x801f                           # opaque blue
        level.reserved.update(game=2, argb1555_pages=[page.tobytes()],
                              solid_colour_palette=bytes(1024))
        level.object_textures[256].uv = ((0,0),(15,0),(15,7),(0,7))
        level.object_textures[257].uv = ((1,0),(3,0),(3,7),(1,7))
        blob, m = tex.cook(level)
        view = Section(blob)
        a, b = (descriptor(view, m, n) for n in (256,257))
        self.assertNotEqual(view.descriptors[a]['cluster'], view.descriptors[b]['cluster'])
        d = view.descriptors[b]
        self.assertEqual((d['x'],d['width'],d['uv']), (0,3,((0,0),(2,0),(2,7),(0,0))))
        self.assertEqual(view.tiles(b), [(0,0,3,8,8,64)])
        np.testing.assert_array_equal(view.decode(b)[0], [[0xf801,0,0x003f]]*8)
        offset = view.clusters[d['cluster']][0]
        self.assertEqual(blob[offset:offset+2], bytes.fromhex('2010'))
        bad = bytearray(blob)
        struct.pack_into('>I',bad,68,0)
        with self.assertRaisesRegex(ValueError, 'even cluster-local x and right end'):
            Section(bad)

    def test_psx_more_than_sixteen_colours_rejected(self):
        level = synthetic.fixture('psx')
        level.cluts[0] += b'\0\0'
        with self.assertRaisesRegex(ValueError, 'exactly 16'):
            tex.cook(level)

    def test_fifteen_opaque_colours_plus_transparency_exact(self):
        level = synthetic.fixture('pc')
        level.palette = bytes(v for i in range(256) for v in ((i*2) % 64, (i*4) % 64, (i*6) % 64))
        level.images = [bytes(i % 16 for i in range(65536))]
        blob, m = tex.cook(level)
        v = Section(blob)
        for i, r in enumerate(m['descriptors']):
            x, y, w, h = r['rect']
            indices = np.array(list(level.images[r['page']]), dtype=np.uint8).reshape(256, 256)[y:y+h, x:x+w]
            rgb = np.array(list(level.palette), dtype=np.uint16).reshape(256, 3)//2
            pal = rgb[:, 0]*2048+rgb[:, 1]*64+rgb[:, 2]*2+1
            pal[0] -= 1
            np.testing.assert_array_equal(v.decode(i)[0], pal[indices])
        # Deliberately replacing nearest with farthest must fail exactness.
        with patch.object(tex, 'nearest', lambda a, b: np.argmax(np.sum((np.asarray(a, dtype=np.int32)[:, None, :]-np.asarray(b, dtype=np.int32)[None, :, :])**2, axis=2), axis=1)):
            wrong, wm = tex.cook(level)
            self.assertFalse(np.array_equal(Section(wrong).decode(0)[0], v.decode(0)[0]))

    def test_hand_computed_nearest_colour_and_metrics(self):
        # Distances to black / red=10: x=4 ->16/36, x=6 ->36/16, tie x=5 ->25/25.
        actual = tex.nearest(np.array([[4, 0, 0], [6, 0, 0], [5, 0, 0]]), [[0, 0, 0], [10, 0, 0]])
        np.testing.assert_array_equal(actual, [0, 1, 0])
        a = np.array([[1, (10 << 11) | 1]], dtype=np.uint16)
        b = np.array([[(3 << 11) | 1, (6 << 11) | 1]], dtype=np.uint16)
        expected_mse = (9+16)/6
        expected_psnr = 10*math.log10(961/expected_mse)
        measured = measure(a, b)
        self.assertEqual(measured['max_error'], 4)
        self.assertEqual(measured['mse'], expected_mse)
        self.assertEqual(measured['psnr'], expected_psnr)
        self.assertEqual(tex.error(a, b), measured)

    def test_transparency_reserves_one_of_sixteen_entries(self):
        level = synthetic.fixture('pc')
        level.palette = bytes(v for i in range(256) for v in (2*(i % 32), 0, 0))
        source_pal = tex.pc_palette(level)
        indices = np.arange(17, dtype=np.uint8).reshape(1, 17)
        q, palette, _ = tex.quantize(indices, source_pal)
        self.assertEqual(len(palette), 16, 'transparent + 16 opaque must quantise to 16 TOTAL')
        self.assertLess(int(q.max()), 16)
        self.assertEqual(int(palette[q[0, 0]]), int(source_pal[0]))
        np.testing.assert_array_equal(palette[q] & 1, indices != 0)
        level.images = [bytes(i % 17 for i in range(65536))]
        level.object_textures[256].uv = ((0, 0), (17, 0), (17, 16), (0, 16))
        blob, m = tex.cook(level)
        decoded, _ = Section(blob).decode(m['object_descriptor_map']['256'])
        source = np.frombuffer(level.images[0], np.uint8).reshape(256, 256)[:17, :18]
        np.testing.assert_array_equal(decoded & 1, source != 0)

    def test_isolated_room_sprite_reference(self):
        level = synthetic.fixture('pc')
        level.rooms[0].quads = []
        level.rooms[0].triangles = []
        level.meshes = {}
        level.data['sprite_sequences'] = []
        level.data['animated_textures'] = []
        blob, m = tex.cook(level)
        self.assertEqual(m['object_descriptor_map'], {})
        self.assertEqual(m['sprite_descriptor_map'], {'0': 0})
        self.assertEqual(Section(blob).sprites, 1)

    def test_isolated_mesh_face_reference(self):
        level = synthetic.fixture('pc')
        level.rooms = []
        level.meshes = {0: level.meshes[0]}
        level.meshes[0].faces = [Face((0, 1, 2, 3), 256, None, 256)]
        level.data['sprite_sequences'] = []
        level.data['animated_textures'] = []
        blob, m = tex.cook(level)
        self.assertEqual(m['object_descriptor_map'], {'256': 0})
        self.assertEqual(m['sprite_descriptor_map'], {})
        self.assertEqual(Section(blob).objects, 1)

    def test_isolated_sprite_sequence_reference(self):
        level = synthetic.fixture('pc')
        level.rooms = []
        level.meshes = {}
        level.data['animated_textures'] = []
        blob, m = tex.cook(level)
        self.assertEqual(m['object_descriptor_map'], {})
        self.assertEqual(m['sprite_descriptor_map'], {'0': 0})
        self.assertEqual(Section(blob).sprites, 1)

    def test_source_alpha_attributes_become_flags(self):
        for attribute in (0, 1, 2):
            level = synthetic.fixture('pc')
            level.object_textures[256].attribute = attribute
            blob, m = tex.cook(level)
            d = Section(blob).descriptors[m['object_descriptor_map']['256']]
            self.assertEqual(d['flags'], 1 << attribute)

    def test_reference_maps_animation_activation_and_unreferenced_error(self):
        level = synthetic.fixture('pc')
        level.data['animated_textures'] = [2, 1, 256, 255, 0, 1]
        blob, m = tex.cook(level)
        self.assertNotIn('1', m['object_descriptor_map'])
        self.assertIn('255', m['object_descriptor_map'])
        v = Section(blob)
        self.assertEqual(v.animations, [1, 2, m['object_descriptor_map']['256'], m['object_descriptor_map']['255']])
        self.assertEqual([d['source_index'] for d in v.descriptors if d['kind'] == 'object'], [255, 256, 257])
        with self.assertRaisesRegex(ValueError, 'unreferenced'):
            tex.descriptor_index(m, 'object', 1)
        level.rooms[0].quads[0].texture = len(level.object_textures)
        with self.assertRaisesRegex(ValueError, 'bounds'):
            tex.cook(level)

    def test_face_maps_unused_corner_sprite_inclusive_and_flat_zero(self):
        for source in ('pc', 'psx'):
            level = synthetic.fixture(source)
            # Triangle's unused slot at (0,0) must neither expand bounds nor leak.
            level.object_textures[257].uv = ((9, 9), (4, 9), (4, 4), (0, 0))
            level.meshes[1].faces[0].color = 0
            blob, m = tex.cook(level)
            v = Section(blob)
            ti = descriptor(v, m, 257)
            tr = m['descriptors'][ti]
            td = v.descriptors[ti]
            self.assertEqual(tr['rect'], [4, 4, 6, 6])
            unused = 2 if source == 'psx' else 3
            self.assertEqual(td['uv'][unused], td['uv'][0])
            for obj in [*level.rooms, *level.meshes.values()]:
                faces = obj.quads+obj.triangles if hasattr(obj, 'quads') else obj.faces
                for f in faces:
                    if f.texture is None:
                        continue
                    di = descriptor(v, m, f.texture)
                    d, r = v.descriptors[di], m['descriptors'][di]['rect']
                    mapping = d['triangle_map'] if len(f.vertices) == 3 else 0xe4
                    native = tuple(tuple(d['uv'][(mapping >> (2*j)) & 3][k]+r[k] for k in (0, 1)) for j in range(len(f.vertices)))
                    self.assertEqual(native, face_uv(level, f))
            si = descriptor(v, m, 0, 'sprite')
            self.assertEqual(v.decode(si)[0].shape, (8, 8))
            self.assertEqual(v.descriptors[si]['uv'], ((0, 0), (7, 0), (7, 7), (0, 7)))
            if source == 'pc':
                self.assertEqual(v.flat[0][1] & 1, 1, 'flat colour zero must remain opaque')
            else:
                t = level.object_textures[0]
                p, _ = tex.psx_palette(level.cluts[t.palette])
                u, y = t.uv[0]
                self.assertEqual(v.flat[0][1], int(p[tex.page_indices(level, t.page)[y, u]]))
            self.assertEqual(sum(m['breakdown'].values()), len(blob))
            self.assertEqual(m['breakdown']['descriptors'], v.objects*24+v.sprites*22)

    def test_invisible_rgb_is_separate_from_picture_error(self):
        from n64_tex_evidence import appearance_metrics
        left = np.array([[0xf83e, 0xffff]], dtype=np.uint16)
        right = np.array([[0, 0xffff]], dtype=np.uint16)
        self.assertFalse(measure(left | 1, right | 1)['exact'])
        result = appearance_metrics(left, right)
        self.assertTrue(result['opaque_error']['exact'])
        self.assertTrue(result['composited_error']['exact'])
        self.assertEqual(result['invisible_rgb_differences'], 1)
        right[0, 1] = 1
        self.assertFalse(appearance_metrics(left, right)['composited_error']['exact'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
