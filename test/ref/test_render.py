# SPDX-License-Identifier: MIT
"""Synthetic-only analytic tests: PYTHONPATH=tools/cook:test/ref python3 -m unittest discover -s test/ref."""
import unittest
import numpy as np
from ir import Level,Room,Face,Texture,Mesh
from render import Camera,Materials,render,visible_rooms,selected_rooms,portal_clip,NEAR,FAR,darkness,fog_darkness,fog_factor,modulation


def room(vertices,faces=(),portals=(),flags=0):
    return Room({'x':0,'z':0,'y_top':-1024,'y_bottom':1024},
                [dict(zip('xyz',v),lighting={'encoding':'tr1_darkness_13bit','value':4095}) for v in vertices],
                [f for f in faces if len(f.vertices)==4],[f for f in faces if len(f.vertices)==3],
                [],list(portals),1,1,[],{'value':0},[],[],-1,flags)


def fixture():
    level = Level('pc','synthetic','synthetic',0)
    palette = np.zeros((256,3),np.uint8)
    for i in range(1,17):
        palette[i] = [i*3,63-i*2,i]
    level.palette = palette.tobytes()
    level.light_map = np.tile(np.arange(256,dtype=np.uint8),(32,1)).tobytes()
    page = np.zeros((256,256),np.uint8)
    page[:4,:4] = np.arange(1,17).reshape(4,4)
    level.images = [page.tobytes()]
    level.object_textures = [Texture(0,None,((0,0),(3,0),(3,3),(0,3)),0,None)]
    face = Face((0,1,2,3),0,None,0)
    level.rooms = [room([(-128,-128,512),(128,-128,512),(128,128,512),(-128,128,512)],[face])]
    return level,{'room':0,'position':[0,0,0],'target':[0,0,1]}


def portal(dest,z,normal=-1,left=-100,right=100):
    p = {'room':dest,'normal_x':0,'normal_y':0,'normal_z':normal}
    for i,(x,y) in enumerate([(left,-100),(right,-100),(right,100),(left,100)]):
        p.update({f'x{i}':x,f'y{i}':y,f'z{i}':z})
    return p


class RendererTests(unittest.TestCase):
    def test_known_quad_analytic_every_pixel(self):
        level,scene = fixture()
        picture = render(level,scene)
        self.assertEqual(int(picture.coverage.sum()),128*128)
        self.assertTrue(np.all(picture.depth[56:184,96:224]==512))
        y,x = np.mgrid[56:184,96:224]
        u = np.floor((x+.5-96)*3/128+.5).astype(int)
        v = np.floor((y+.5-56)*3/128+.5).astype(int)
        palette = np.frombuffer(level.palette,np.uint8).reshape(256,3).astype(int)*4
        np.testing.assert_array_equal(picture.rgb[56:184,96:224],palette[1+v*4+u])
        self.assertEqual(picture.counts['faces_drawn'],1)

    def test_corner_map_is_not_raw_texture_slot_order(self):
        level,scene = fixture()
        original = render(level,scene)
        level.rooms[0].quads[0].texture_corners = (1,2,3,0)
        rotated = render(level,scene)
        self.assertFalse(np.array_equal(original.rgb,rotated.rgb))
        np.testing.assert_array_equal(rotated.rgb[64,104],original.rgb[64,215])
        self.assertTrue(np.array_equal(original.coverage,rotated.coverage))

    def test_near_clip_matches_analytic_intersection(self):
        level,scene = fixture()
        level.rooms[0] = room([(-80,-80,32),(80,-80,96),(0,80,96)],
                              [Face((0,1,2),None,5,0x8000)])
        picture = render(level,scene)
        self.assertGreater(picture.coverage.sum(),0)
        self.assertTrue(np.all(picture.depth[picture.coverage]>=NEAR))
        # Intersections at z64 are (0,-80,64),(-40,0,64).
        expected = room([(0,-80,64),(80,-80,96),(0,80,96),(-40,0,64)],
                        [Face((0,1,2,3),None,5,0x8000)])
        level.rooms[0] = expected
        other = render(level,scene)
        np.testing.assert_array_equal(picture.coverage,other.coverage)
        np.testing.assert_allclose(picture.depth[picture.coverage],other.depth[other.coverage])

    def test_key_texel_writes_no_depth(self):
        level,scene = fixture()
        level.images = [bytes(65536)]
        picture = render(level,scene)
        self.assertFalse(picture.coverage.any())
        self.assertTrue(np.isinf(picture.depth).all())

    def test_winding_and_double_sided(self):
        level,scene = fixture()
        face = level.rooms[0].quads[0]
        face.vertices = tuple(reversed(face.vertices))
        self.assertFalse(render(level,scene).coverage.any())
        face.flags = 0x8000
        self.assertEqual(int(render(level,scene).coverage.sum()),128*128)

    def test_portal_visibility_far_and_normal(self):
        level,scene = fixture()
        level.rooms += [room([],portals=[portal(0,1024,1)])]
        level.rooms[0].portals = [portal(1,1024)]
        self.assertEqual(set(visible_rooms(level.rooms,Camera(scene))),{0,1})
        level.rooms[0].portals = [portal(1,11000)]
        self.assertEqual(set(visible_rooms(level.rooms,Camera(scene))),{0})
        level.rooms[0].portals = [portal(1,1024,1)]
        self.assertEqual(set(visible_rooms(level.rooms,Camera(scene))),{0})

    def test_flip_selects_mates_at_logical_ids(self):
        level,scene = fixture()
        level.rooms.append(room([]))
        level.rooms[0].alternate_room = 1
        selected = selected_rooms(level,True)
        self.assertIs(selected[0],level.rooms[1])
        self.assertIs(selected[1],level.rooms[0])
        level.rooms[1].alternate_room = 0
        self.assertIs(selected_rooms(level,True)[0],level.rooms[1])

    def test_static_mesh_instance_uses_alias_and_transform(self):
        level,scene = fixture()
        mesh = Mesh((0,0,0),512,0,[(-128,-128,0),(128,-128,0),(128,128,0),(-128,128,0)],
                    None,[4096]*4,[Face((0,1,2,3),0,None,0)],12)
        level.meshes = {12:mesh}
        level.data.update(mesh_pointers=[12,12],static_meshes=[{'id':7,'mesh':1,'flags':2}])
        level.rooms[0] = room([])
        level.rooms[0].static_instances = [{'mesh':7,'x':0,'y':0,'z':512,'angle':0,'intensity':4095}]
        actual = render(level,scene)
        expected = render(fixture()[0],scene)
        np.testing.assert_array_equal(actual.rgb,expected.rgb)
        self.assertEqual(actual.counts['static_instances'],1)

    def test_perspective_correct_not_affine_uv(self):
        level,scene = fixture()
        # Screen corners (96,56),(224,56),(96,184), but z differs.
        level.rooms[0] = room([(-128,-128,512),(256,-256,1024),(-128,128,512)],
                              [Face((0,1,2),0,None,0)])
        level.object_textures[0].uv = ((0,0),(3,0),(0,3),(0,3))
        picture = render(level,scene)
        x,y = 166,80
        weights = np.array([1-(x+.5-96)/128-(y+.5-56)/128,
                            (x+.5-96)/128,(y+.5-56)/128])
        invz = weights/np.array([512,1024,512])
        uv = (invz @ np.array([[0,0],[3,0],[0,3]]))/invz.sum()
        u,v = np.floor(uv+.5).astype(int)
        expected = np.frombuffer(level.palette,np.uint8).reshape(256,3)[1+v*4+u].astype(int)*4
        np.testing.assert_array_equal(picture.rgb[y,x],expected)
        self.assertAlmostEqual(picture.depth[y,x],1/invz.sum())
        self.assertNotEqual(int(u),int(np.floor((weights @ np.array([0,3,0]))+.5)))

    def test_psx_native_clut_key_and_flat_descriptor(self):
        level,_ = fixture()
        level.source = 'psx'
        level.palette = None
        level.images = [bytes([0x21])+bytes(32767)]
        level.object_textures[0] = Texture(0,1,((1,0),(1,0),(1,0),(1,0)),None,None)
        words = np.zeros(16,dtype='<u2')
        words[1],words[2] = 31,31<<10
        level.cluts = [bytes(32),words.tobytes()]
        materials = Materials(level)
        np.testing.assert_array_equal(materials.flat(0),[0,0,255])
        rgb,visible = materials.image(0)
        np.testing.assert_array_equal(rgb[0,0],[255,0,0])
        self.assertTrue(visible[0,0])
        self.assertFalse(visible[0,2])

    def test_darkness_fog_and_psx_neutral_are_analytic(self):
        self.assertEqual(darkness({'encoding':'tr1_darkness_13bit','value':4096}),4096)
        self.assertEqual(darkness({'encoding':'psx_vertex_8bit','value':128}),4095)
        self.assertEqual(darkness({'encoding':'psx_vertex_8bit','value':32}),7167)
        np.testing.assert_allclose(fog_factor([8192,8320,9000,12000]),[0,128/2048,808/2048,1])
        np.testing.assert_array_equal(fog_darkness([4096]*4,[8192,8320,9000,12000]),[4096,4352,5712,8191])
        self.assertEqual(fog_factor(FAR),1)
        np.testing.assert_allclose(modulation([4095,8191,31]),[1,0,1.5])

    def test_pc_source_lightmap_row_not_linear_multiplier(self):
        level,_ = fixture()
        table = np.tile(np.arange(256,dtype=np.uint8),(32,1))
        table[16,:],table[17,:] = 5,9
        level.light_map = table.tobytes()
        material = Materials(level,'pc_lut')
        rgb,_ = material.sample(0,np.array([[0,0],[0,0]],float),dark=np.array([4096,4352]))
        palette = np.frombuffer(level.palette,np.uint8).reshape(256,3).astype(int)*4
        np.testing.assert_array_equal(rgb,palette[[5,9]])

    def test_static_quarter_turn_sign(self):
        level,scene = fixture()
        expected = render(level,scene)
        mesh = Mesh((0,0,0),512,0,[(0,-128,-128),(0,-128,128),(0,128,128),(0,128,-128)],
                    None,[4096]*4,[Face((0,1,2,3),0,None,0)],12)
        level.meshes = {12:mesh}
        level.data.update(mesh_pointers=[12],static_meshes=[{'id':7,'mesh':0,'flags':2}])
        level.rooms[0] = room([])
        level.rooms[0].static_instances = [{'mesh':7,'x':0,'y':0,'z':512,'angle':16385,'intensity':4095}]
        np.testing.assert_array_equal(render(level,scene).rgb,expected.rgb)

    def test_near_portal_expansion_and_parent_intersection(self):
        r = room([])
        scene = {'room':0,'position':[0,0,0],'target':[0,0,1]}
        p = {'room':1,'normal_x':-1,'normal_y':0,'normal_z':0}
        for i,(y,z) in enumerate([(-10,-100),(-10,100),(10,100),(10,-100)]):
            p.update({f'x{i}':10,f'y{i}':y,f'z{i}':z})
        expanded = portal_clip(r,p,np.array([0,0,320,240]),Camera(scene))
        self.assertIsNotNone(expanded)
        np.testing.assert_allclose(expanded,[185.6,0,320,240])
        np.testing.assert_allclose(portal_clip(r,p,np.array([190,100,210,140]),Camera(scene)),[190,100,210,140])

    def test_far_rejection_keeps_crossing_triangle(self):
        level,scene = fixture()
        level.rooms[0] = room([(-4000,-4000,9000),(4000,-4000,11000),(0,4000,11000)],
                              [Face((0,1,2),None,5,0x8000)])
        picture = render(level,scene)
        self.assertTrue(picture.coverage.any())
        self.assertGreater(picture.depth[picture.coverage].max(),FAR)
        for vertex in level.rooms[0].vertices:
            vertex['z'] = 11000
        self.assertFalse(render(level,scene).coverage.any())

    def test_static_fog_at_origin_is_not_omitted(self):
        level,scene = fixture()
        level.palette = np.repeat((np.arange(256,dtype=np.uint8)%64)[:,None],3,axis=1).tobytes()
        level.light_map = np.repeat(np.arange(32,dtype=np.uint8)[:,None],256,axis=1).tobytes()
        mesh = Mesh((0,0,0),4000,0,[(-2000,-2000,0),(2000,-2000,0),(2000,2000,0),(-2000,2000,0)],
                    None,[4096]*4,[Face((0,1,2,3),0,None,0)],12)
        level.meshes = {12:mesh}
        level.data.update(mesh_pointers=[12],static_meshes=[{'id':7,'mesh':0,'flags':2}])
        level.rooms[0] = room([])
        level.rooms[0].static_instances = [{'mesh':7,'x':0,'y':0,'z':9000,'angle':0,'intensity':4096}]
        np.testing.assert_array_equal(render(level,scene).rgb[120,160],[27,27,27])

    def test_clip_growth_is_repropagated(self):
        level,scene = fixture()
        level.rooms = [room([],portals=[portal(1,512,left=-256,right=-128),portal(2,512,left=128,right=256)]),
                       room([],portals=[portal(3,2048,left=-2048,right=2048)]),
                       room([],portals=[portal(1,1024,left=256,right=512)]),room([])]
        clips = visible_rooms(level.rooms,Camera(scene))
        self.assertAlmostEqual(clips[3][0],32)
        self.assertAlmostEqual(clips[3][2],288)

    def test_texel_map_carries_continuous_face_local_coordinates(self):
        level,scene = fixture()
        picture = render(level,scene)
        np.testing.assert_allclose(picture.texel[80,166],[(166.5-96)*3/128,(80.5-56)*3/128])
        self.assertTrue(np.isnan(picture.texel[0,0]).all())

    def test_water_tint_is_applied_to_source_colour(self):
        level,scene = fixture()
        plain = render(level,scene)
        level.rooms[0].flags = 1
        water = render(level,scene)
        np.testing.assert_array_equal(water.rgb[120,160],np.rint(plain.rgb[120,160]*[.5,.8,.8]))
        np.testing.assert_array_equal(water.coverage,plain.coverage)

    def test_fog_blends_after_overbright_colour_is_clamped(self):
        level,scene = fixture()
        level.palette = np.full((256,3),50,dtype=np.uint8).tobytes()
        level.rooms[0] = room([(-2000,-2000,9216),(2000,-2000,9216),(2000,2000,9216),(-2000,2000,9216)],
                             [Face((0,1,2,3),None,1,0)])
        for vertex in level.rooms[0].vertices:
            vertex['lighting']['value'] = 0
        np.testing.assert_array_equal(render(level,scene).rgb[120,160],[128,128,128])

    def test_gain_is_screen_affine_not_perspective_correct(self):
        level,scene = fixture()
        level.palette = np.full((256,3),50,dtype=np.uint8).tobytes()
        level.rooms[0] = room([(-128,-128,512),(256,-256,1024),(-128,128,512)],[Face((0,1,2),None,1,0)])
        gains = np.array([.25,.75,.5])
        for vertex,gain in zip(level.rooms[0].vertices,gains):
            vertex['lighting']['value'] = 8191-gain*4096
        x,y = 166,80
        weights = np.array([1-(x+.5-96)/128-(y+.5-56)/128,(x+.5-96)/128,(y+.5-56)/128])
        np.testing.assert_array_equal(render(level,scene).rgb[y,x],np.repeat(np.rint(200*(weights@gains)),3))

    def test_fog_is_per_vertex_and_screen_affine(self):
        level,scene = fixture()
        level.palette = np.full((256,3),50,dtype=np.uint8).tobytes()
        level.rooms[0] = room([(-2000,-2000,8000),(2500,-2500,10000),(-2000,2000,8000)],[Face((0,1,2),None,1,0)])
        x,y = 166,80
        weight = (x+.5-96)/128
        expected = np.rint(200*(1-weight*((10000-8192)/2048)))
        np.testing.assert_array_equal(render(level,scene).rgb[y,x],np.repeat(expected,3))

    def test_source_page_texels_and_canonical_face_origin(self):
        level,scene = fixture()
        page = np.zeros((256,256),np.uint8)
        page[40:44,80:84] = np.arange(1,17).reshape(4,4)
        level.images = [page.tobytes()]
        level.object_textures[0].uv = tuple((u+80,v+40) for u,v in level.object_textures[0].uv)
        picture = render(level,scene)
        np.testing.assert_allclose(picture.texel[80,166],[80+(166.5-96)*3/128,40+(80.5-56)*3/128])
        np.testing.assert_allclose(picture.texel_origin[80,166],[80,40])


if __name__ == '__main__':
    unittest.main()
