# SPDX-License-Identifier: MIT
"""Fast synthetic-only tests and production-reader negative controls."""
import copy
import dataclasses
import struct
import time
import unittest
from unittest.mock import patch
import account
import equivalence
import sizing
import src_pc
import src_psx
import synthetic
from binary import FormatError
from test_src_tr2_tr3 import SequelReaders


class Readers(unittest.TestCase):
    def test_round_trip_all_sections(self):
        for source,module in [('pc',src_pc),('psx',src_psx)]:
            with self.subTest(source=source):
                expected = synthetic.fixture(source)
                raw = synthetic.encode(expected)
                level = module.read(raw)
                self.assertEqual(synthetic.encode(level),raw)
                self.assertEqual(level.rooms,expected.rooms)
                self.assertEqual(level.object_textures,expected.object_textures)
                self.assertEqual(level.sprite_textures,expected.sprite_textures)
                self.assertEqual(level.images,expected.images)
                self.assertEqual(level.palette,expected.palette)
                self.assertEqual(level.cluts,expected.cluts)
                self.assertEqual(level.light_map,expected.light_map)
                self.assertEqual(level.samples,expected.samples)
                self.assertEqual(level.sample_encoding,expected.sample_encoding)
                for key,value in expected.data.items():
                    if key not in ('mesh_pointers','sample_data'):
                        self.assertEqual(level.data[key],value,key)
                for actual,wanted in zip(level.meshes.values(),expected.meshes.values()):
                    a,b = dataclasses.asdict(actual),dataclasses.asdict(wanted)
                    a.pop('source_offset'); b.pop('source_offset')
                    self.assertEqual(a,b)
                audited = account.check(level,raw)
                self.assertEqual(audited['unexplained_bytes'],0)
                self.assertEqual(audited['explained_bytes'],len(raw))

    def test_psx_cutscene_has_absent_bank(self):
        fixture = synthetic.fixture('psx')
        raw = synthetic.encode(fixture,cutscene=True)
        level = src_psx.read(raw)
        self.assertIsNone(level.reserved['vab'])
        self.assertEqual(level.samples,[])
        self.assertIsNone(level.palette)
        self.assertIsNone(level.light_map)
        self.assertIsNone(level.data['demo_data'])
        self.assertEqual(synthetic.encode(level,cutscene=True),raw)
        account.check(level,raw)

    def test_negative_controls(self):
        for source,module in [('pc',src_pc),('psx',src_psx)]:
            raw = synthetic.encode(synthetic.fixture(source))
            level = module.read(raw)
            with self.subTest(source=source,control='truncated'):
                with self.assertRaisesRegex(FormatError,'invalid count|truncated'):
                    module.read(raw[:-1])
            with self.subTest(source=source,control='trailing'):
                with self.assertRaisesRegex(FormatError,'trailing bytes: 1'):
                    module.read(raw+b'\x00')
            with self.subTest(source=source,control='off-by-one'):
                geom = next(s for s in level.sections if s['name']=='rooms/0/geometry')
                broken = bytearray(raw)
                struct.pack_into('<I',broken,geom['offset'],geom['count']+1)
                if source=='psx':
                    # PSX permits two alignment bytes. Decrease instead, so
                    # the stated negative cannot masquerade as legal padding.
                    struct.pack_into('<I',broken,geom['offset'],geom['count']-1)
                with self.assertRaisesRegex(FormatError,'geometry count mismatch'):
                    module.read(broken)
                with self.assertRaisesRegex(FormatError,'geometry count mismatch'):
                    account.sections(broken,source)

    def test_accounting_can_reject_wrong_reader_ledger(self):
        raw = synthetic.encode(synthetic.fixture('pc'))
        level = src_pc.read(raw)
        level.sections[1]['count'] += 1
        with self.assertRaisesRegex(FormatError,'independent accounting mismatch texture_pages.count'):
            account.check(level,raw)

    def test_mesh_pointer_alias_is_preserved(self):
        level = src_pc.read(synthetic.encode(synthetic.fixture('pc')))
        # A genuine duplicate pointer does not imply a second physical mesh.
        from decode import read_meshes
        level.data['mesh_pointers'].append(0)
        read_meshes(level,False)
        self.assertEqual(len(level.meshes),2)
        self.assertEqual(len(level.data['mesh_pointers']),3)


class Reports(unittest.TestCase):
    def test_equivalence_reports_not_normalises(self):
        a = src_pc.read(synthetic.encode(synthetic.fixture('pc')))
        b = src_psx.read(synthetic.encode(synthetic.fixture('psx')))
        self.assertTrue(equivalence.compare(a,b)['equal'])
        b.rooms[0].sectors[0]['floor'] += 2
        b.data['items'][0]['x'] += 100
        b.data['frames'][0] += 7
        b.data['floor_data'].append(42)
        report = equivalence.compare(a,b)
        self.assertFalse(report['equal'])
        self.assertEqual(report['classes']['level.rooms[].sectors[].floor']['numeric_deltas'],{'2':1})
        self.assertEqual(report['classes']['level.items[].x']['classification'],'single observation')
        self.assertIn('level.frames[]',report['classes'])
        self.assertEqual(report['sequence_edits']['level.floor_data']['psx_count'],3)
        self.assertIn('level.floor_data[].inserted',report['classes'])

    def test_vertex_cache_partition_exact(self):
        # 72 distinct vertices: 23 triangles / 69 entries, then 1 triangle.
        groups = {0:[tuple(range(i,i+3)) for i in range(0,72,3)]}
        result = sizing.batch_groups(groups)
        self.assertEqual(result['batches'],2)
        self.assertEqual(result['vertices'],72)
        self.assertEqual(result['vertex_bytes'],35*32+2*32)
        self.assertEqual(result['index_bytes'],sizing.align(69)+sizing.align(3))
        self.assertEqual(result['bytes'],result['vertex_bytes']+result['index_bytes']+64)

    def test_uv_seams_and_tmem_splits(self):
        level = synthetic.fixture('pc')
        texture = level.object_textures[256]
        texture.uv = ((0,0),(63,0),(63,63),(0,63))
        tex,plans = sizing.texture_model(level,4)
        self.assertGreater(len(plans[('object',256)]),1)
        for tiles in plans.values():
            self.assertTrue(all(tile['bytes']<=2048 for tile in tiles))
        result = sizing.geometry_model(level,plans,True)
        self.assertGreater(result['triangles'],3)
        self.assertGreater(result['vertices'],len(level.rooms[0].vertices))
        self.assertGreater(result['tile_uv_duplication'],0)
        self.assertGreater(tex['palette_bytes'],0)

    def test_texture_and_clut_dedup(self):
        level = synthetic.fixture('psx')
        before,plans = sizing.texture_model(level,4)
        level.object_textures.append(copy.deepcopy(level.object_textures[256]))
        face = copy.deepcopy(level.rooms[0].quads[0])
        face.texture = len(level.object_textures)-1
        face.flags = face.texture
        level.rooms[0].quads.append(face)
        after,newplans = sizing.texture_model(level,4)
        self.assertEqual(before['bytes'],after['bytes'])
        self.assertEqual(before['unique_rectangles'],after['unique_rectangles'])
        self.assertEqual(newplans[('object',256)],newplans[('object',face.texture)])
        level.cluts[1] = b'\xff'*32
        level.object_textures[face.texture].palette = 1
        changed,_ = sizing.texture_model(level,4)
        self.assertEqual(changed['palette_bytes'],after['palette_bytes']+32)
        self.assertEqual(changed['image_bytes'],after['image_bytes'])

    def test_uv_degenerate_geometry_is_not_discarded(self):
        from collections import defaultdict
        from ir import Face
        level = synthetic.fixture('pc')
        level.object_textures[256].uv = ((0,0),(63,63),(31,31),(0,63))
        level.rooms[0].quads = []
        level.rooms[0].triangles = [Face((0,1,2),256,None,256)]
        _,plans = sizing.texture_model(level,4)
        self.assertGreater(len(plans[('object',256)]),1)
        groups = defaultdict(list)
        sizing.add_face(groups,level.rooms[0].triangles[0],level,plans)
        self.assertGreater(sum(len(tris) for tris in groups.values()),0)

    def test_model_b_four_real_cache_partitions(self):
        import model_b
        from ir import Face
        obj = synthetic.fixture('pc').meshes[0]
        obj.vertices = [(i,0,0) for i in range(101)]
        obj.faces = [Face((i,i+1,i+2),256,None,256) for i in range(0,99,2)]
        results = {}
        for vertex_size,cache_size in ((16,70),(16,48),(8,70),(8,48)):
            row = model_b.object_model(obj,False,vertex_size,cache_size)
            self.assertTrue(all(b['vertices']<=cache_size for b in row['batch_counts']))
            self.assertEqual(row['faces'],50)
            self.assertEqual(row['shared_before_batching'],101)
            self.assertGreater(row['boundary_duplication'],0)
            self.assertEqual(row['face_bytes'],400)
            results[(vertex_size,cache_size)] = row
        for cache_size in (70,48):
            self.assertEqual(results[16,cache_size]['vertex_loads'],results[8,cache_size]['vertex_loads'])
            self.assertGreater(results[16,cache_size]['bytes'],results[8,cache_size]['bytes'])
        for cache_size in (70,48):
            local = model_b.object_model(obj,False,8,cache_size,'locality')
            self.assertTrue(all(b['vertices']<=cache_size for b in local['batch_counts']))
            self.assertEqual(local['faces'],50)
            self.assertGreaterEqual(local['vertex_loads'],local['shared_before_batching'])

    def test_a1_a2_border_assumptions(self):
        a1 = sizing.tile_rectangles(64,64,4,True)
        a2 = sizing.tile_rectangles(64,64,4,False)
        self.assertEqual([t[3] for t in a1],[49,15])
        self.assertEqual(sum(t[4] for t in a1),2720)
        self.assertEqual(a2,[(0,0,64,64,2048)])
        split = sizing.tile_rectangles(64,128,4,False)
        self.assertTrue(all(t[4]<=2048 for t in split))
        self.assertEqual(sum(t[3] for t in split),128)

    def test_edit_script_not_index_cascade(self):
        import alignment
        a = list(range(100))
        b = a[:33]+[1000]+a[33:]
        self.assertEqual([op for op in alignment.opcodes(a,b) if op[0]!='equal'],
                         [('insert',33,33,33,34)])
        pc = src_pc.read(synthetic.encode(synthetic.fixture('pc')))
        psx = src_psx.read(synthetic.encode(synthetic.fixture('psx')))
        pc.data['items'] = [dict(type=i,room=0,x=i,y=0,z=0,angle=0,intensity=-1,flags=0) for i in range(50)]
        psx.data['items'] = copy.deepcopy(pc.data['items'])
        psx.data['items'].insert(33,dict(type=999,room=0,x=1,y=0,z=0,angle=0,intensity=-1,flags=0))
        pc.data['floor_data'] = [0,0x8004,0,0x8000|40]
        psx.data['floor_data'] = [0,0x8004,0,0x8000|41]
        report = equivalence.compare(pc,psx)
        self.assertEqual(report['classes']['level.items[].inserted']['differences'],1)
        self.assertNotIn('level.items[].x',report['classes'])
        refs = report['item_reference_renumbering']
        self.assertTrue(refs['consistent'])
        self.assertEqual(refs['consistently_renumbered_references'],1)
        self.assertEqual(report['classes']['level.floor_data[].derived_renumbering']['differences'],1)
        self.assertNotIn('level.floor_data[]',report['classes'])

    def test_deleted_state_derives_empty_end_offsets(self):
        pc = src_pc.read(synthetic.encode(synthetic.fixture('pc')))
        psx = src_psx.read(synthetic.encode(synthetic.fixture('psx')))
        pc.data['states'].append(dict(state=99,range_count=0,range_offset=1))
        pc.data['animations'][0].update(state_count=0,state_offset=2)
        psx.data['animations'][0].update(state_count=0,state_offset=1)
        report = equivalence.compare(pc,psx)
        derived = report['classes']['level.animations[].state_offset.derived_renumbering']
        self.assertEqual(derived['derived_from'],'states edit script')
        self.assertEqual(derived['classification'],'single observation')
        self.assertEqual(report['classes']['level.states[].deleted']['differences'],1)

    def test_one_sided_and_absent_item_references(self):
        pc = src_pc.read(synthetic.encode(synthetic.fixture('pc')))
        psx = src_psx.read(synthetic.encode(synthetic.fixture('psx')))
        empty = equivalence.compare(pc,psx)['item_reference_renumbering']
        self.assertIsNone(empty['consistent'])
        self.assertEqual(empty['status'],'no item references')
        for level in (pc,psx):
            level.data['floor_data'] = [0,0x8404,0,0x8000]
            level.rooms[0].x_sectors = 2
        pc.rooms[0].sectors = [dict(pc.rooms[0].sectors[0],floor_index=1),dict(pc.rooms[0].sectors[0],floor_index=0)]
        psx.rooms[0].sectors = [dict(psx.rooms[0].sectors[0],floor_index=0),dict(psx.rooms[0].sectors[0],floor_index=1)]
        refs = equivalence.compare(pc,psx)['item_reference_renumbering']
        self.assertFalse(refs['consistent'])
        self.assertEqual(refs['added_reference_count'],1)
        self.assertEqual(refs['removed_reference_count'],1)
        self.assertEqual(refs['pc_total_item_references'],1)
        self.assertEqual(refs['psx_total_item_references'],1)
        self.assertEqual(refs['added_item_references'][0]['sector'],1)
        self.assertEqual(refs['removed_item_references'][0]['sector'],0)
        self.assertEqual(refs['added_item_references'][0]['action'],'activator')

    def test_dependent_state_and_overlap_alignment(self):
        pc = src_pc.read(synthetic.encode(synthetic.fixture('pc')))
        psx = src_psx.read(synthetic.encode(synthetic.fixture('psx')))
        pc.data['ranges'] = [dict(low=i,high=i,next_animation=0,next_frame=0) for i in range(3)]
        psx.data['ranges'] = [pc.data['ranges'][0],pc.data['ranges'][2]]
        pc.data['states'] = [dict(state=i*10,range_count=1,range_offset=i) for i in range(3)]
        psx.data['states'] = [dict(state=0,range_count=1,range_offset=0),dict(state=20,range_count=1,range_offset=1)]
        pc.data['animations'][0].update(state_count=1,state_offset=2)
        psx.data['animations'][0].update(state_count=1,state_offset=1)
        pc.data['overlaps'] = [0x8000,0x8001,0x8002]
        psx.data['overlaps'] = [0x8000,7,8,0x8001,0x8002]
        pc.data['boxes'][0]['overlap'] = 0x8002
        psx.data['boxes'][0]['overlap'] = 0x8004
        report = equivalence.compare(pc,psx)
        classes = report['classes']
        self.assertIn('level.boxes[].overlap.derived_renumbering',classes)
        self.assertNotIn('level.boxes[].overlap',classes)
        self.assertIn('level.animations[].state_offset.derived_renumbering',classes)
        self.assertIn('level.states[].range_offset.derived_renumbering',classes)
        self.assertNotIn('level.states[].state',classes)
        self.assertEqual(classes['level.states[].deleted']['differences'],1)
        self.assertEqual(report['difference_count'],report['primary_difference_count']+report['derived_difference_count'])
        psx.data['boxes'][0]['overlap'] = 4  # Changed flag is NOT an index renumbering.
        self.assertIn('level.boxes[].overlap',equivalence.compare(pc,psx)['classes'])

    def test_layout_invariants_independent_of_round_trip(self):
        import invariants
        from ir import face_uv
        a = src_pc.read(synthetic.encode(synthetic.fixture('pc')))
        raw = synthetic.encode(synthetic.fixture('psx'))
        # Independent reference constants, not source-reader section offsets:
        # samples at SOUND_DATA_BASE 2600 + 1 program * 512.
        self.assertEqual(raw[3108:3112],struct.pack('<I',16))
        self.assertEqual(raw[3112:3128],bytes(range(16)))
        fixture = synthetic.fixture('psx')
        mesh = synthetic.encode_mesh(fixture.meshes[0],True)
        # 12-byte header + 4*8 positions + 4*8 normals + quad count.
        self.assertEqual(mesh[78:86],struct.pack('<4H',0,1,2,3))
        b = src_psx.read(raw)
        self.assertEqual(invariants.local(b)['adpcm']['last_block_end_flags'],1)
        self.assertEqual(face_uv(a,a.rooms[0].quads[0]),((2,2),(9,2),(9,9),(2,9)))
        self.assertEqual(face_uv(b,b.rooms[0].quads[0]),((2,2),(9,2),(9,9),(2,9)))
        self.assertEqual(invariants.paired(a,b)['rooms_4']['congruent_geometry_and_uv'],1)
        self.assertEqual(invariants.paired(a,b)['meshes_4']['checked_matching_textured_quads'],1)
        b.meshes[next(iter(b.meshes))].faces[1].texture_corners = (1,0,2)
        with self.assertRaisesRegex(FormatError,'corner-to-UV invariant'):
            invariants.paired(a,b)
        b = src_psx.read(raw)
        b.meshes[next(iter(b.meshes))].faces[0].vertices = (0,1,3,2)
        with self.assertRaisesRegex(FormatError,'corner-to-UV invariant'):
            invariants.paired(a,b)
        b = src_psx.read(raw)
        b.rooms[0].quads[0].texture_corners = (0,1,2,3)
        with self.assertRaisesRegex(FormatError,'nonconvex quad'):
            invariants.local(b)
        with self.assertRaisesRegex(FormatError,'corner-to-UV invariant'):
            invariants.paired(a,b)
        b = src_psx.read(raw)
        b.samples[0] = bytes([255])+b.samples[0][1:]
        with self.assertRaisesRegex(FormatError,'ADPCM invariant'):
            invariants.local(b)
        b = src_psx.read(raw)
        b.samples[0] = b.samples[0][:1]+bytes([0])+b.samples[0][2:]
        with self.assertRaisesRegex(FormatError,'end_flags=0/1'):
            invariants.local(b)
        broken = bytearray(raw)
        struct.pack_into('<I',broken,3108,15)
        with self.assertRaisesRegex(FormatError,'body-size mismatch'):
            src_psx.read(broken)

    def test_encoder_field_order_is_independent(self):
        for source in ('pc','psx'):
            fixture = synthetic.fixture(source)
            expected = synthetic.encode(fixture)
            for name,rows in fixture.data.items():
                if isinstance(rows,list) and rows and isinstance(rows[0],dict):
                    fixture.data[name] = [dict(reversed(list(row.items()))) for row in rows]
            for room in fixture.rooms:
                room.portals = [dict(reversed(list(row.items()))) for row in room.portals]
            self.assertEqual(synthetic.encode(fixture),expected)

    def test_edit_script_exhaustive_small_sequences(self):
        import alignment
        import itertools
        sequences = [list(s) for n in range(4) for s in itertools.product((0,1),repeat=n)]
        for a in sequences:
            for b in sequences:
                actual,cost = [],0
                for tag,i,j,k,l in alignment.opcodes(a,b):
                    if tag=='equal':
                        self.assertEqual(a[i:j],b[k:l])
                        actual.extend(a[i:j])
                    else:
                        actual.extend(b[k:l])
                        cost += j-i+l-k
                self.assertEqual(actual,b)
                dp = list(range(len(b)+1))
                for av in a:
                    new = [dp[0]+1]
                    for j,bv in enumerate(b):
                        new.append(dp[j] if av==bv else min(dp[j+1],new[-1])+1)
                    dp = new
                self.assertEqual(cost,dp[-1])

    def test_model_c_exact_overlap_not_bounding_box(self):
        import model_c
        from ir import Face,Texture
        level = synthetic.fixture('pc')
        level.object_textures = [
            Texture(0,None,((0,0),(3,0),(3,3),(0,3)),0,None),
            Texture(0,None,((2,2),(5,2),(5,5),(2,5)),0,None)]
        level.rooms[0].quads = [Face((0,1,2,3),0,None,0)]
        level.rooms[0].triangles = [Face((0,1,2),1,None,1)]
        level.rooms[0].sprites = []
        level.meshes = {}
        level.data['animated_textures'] = [0]
        level.data['sprite_sequences'] = []
        row = model_c.measure(level)
        self.assertEqual(row['cluster_count'],1)
        self.assertEqual(row['union_texels'],28)
        self.assertEqual(row['union_ci4_image_bytes'],14)
        self.assertEqual(row['bounding_ci4_image_bytes'],18)
        self.assertEqual(row['palette_count'],1)
        self.assertEqual(row['union_payload_bytes'],46)
        self.assertEqual(row['bounding_uploadable_bytes'],66)
        native = model_c.measure(synthetic.fixture('psx'))['native_pages']
        self.assertEqual(native['bytes'],13*32768+1024*32)

    def test_pool_helper_exposes_every_carve_row(self):
        definition = ('TR64_POOL(image, 448, IMAGE)\n'
                      'TR64_POOL(fb, 300, FIXED)\n'
                      'TR64_POOL(gpu, 232, CARVE)\n'
                      'TR64_POOL(level, 1472, CARVE)\n'
                      'TR64_POOL(state, 192, CARVE)\n')
        with patch('sizing.Path.read_text',return_value=definition):
            self.assertEqual(sizing.pools('synthetic.def'),
                dict(gpu=232*1024,level=1472*1024,state=192*1024))

    def test_level_arena_aligns_each_section(self):
        sections = dict(core=1,tex=17,rooms=33,meshes=49)
        self.assertEqual(sizing.resident_total(sections),160)
        exact = sizing.level_allocation(sections,160)
        self.assertTrue(exact['fits'])
        self.assertEqual(exact['headroom_bytes'],0)
        short = sizing.level_allocation(sections,159)
        self.assertFalse(short['fits'])
        self.assertEqual(short['over_bytes'],1)
        self.assertEqual(short['headroom_bytes'],-1)

    def test_budget_negative_is_not_hidden(self):
        level = src_pc.read(synthetic.encode(synthetic.fixture('pc')))
        row = sizing.measure(level,dict(level=1,gpu=0,state=0))
        self.assertEqual(set(row['sections']),set(sizing.LEVEL_SECTIONS))
        self.assertEqual(set(row['pools']),{'level'})
        self.assertEqual(set(row['model_a2']['pools']),{'level'})
        allocation = row['pools']['level']
        self.assertFalse(allocation['fits'])
        self.assertEqual(allocation['needed_bytes'],sizing.resident_total(row['sections']))
        self.assertEqual(allocation['over_bytes'],allocation['needed_bytes']-1)
        self.assertEqual(row['cart_resident_bytes'],allocation['needed_bytes'])

    def test_candidates_keep_other_sections_in_level_arena(self):
        import report_models
        baseline = dict(core=1,tex=17,rooms=33,meshes=49)
        def variants(greedy):
            return {v:dict(policies=dict(lower_bound=dict(bytes=1),
                greedy=dict(bytes=greedy),locality=dict(bytes=17)))
                for v in report_models.VARIANTS}
        row = dict(level='synthetic',source='psx',sections=baseline,
            model_a2=dict(sections=dict.fromkeys(sizing.LEVEL_SECTIONS,1)),
            model_b=dict(rooms=variants(33),meshes=variants(49)),
            model_c=dict(native_pages=dict(bytes=1),union_payload_bytes=1,
                         bounding_uploadable_bytes=17,sparse_addressable_bytes=33))
        candidates = report_models.candidate_summary([row],dict(level=128,gpu=1,state=1))
        self.assertEqual(candidates['A2.level']['needed_bytes'],64)
        self.assertTrue(candidates['A2.level']['fits'])
        b = candidates['B.level.v16_c70']
        self.assertEqual(b['replacement_sections'],['rooms','meshes'])
        self.assertEqual(b['lower_bound_bytes'],80)
        self.assertEqual(b['greedy_bytes'],160)
        self.assertEqual(b['locality_bytes'],112)
        self.assertIsNone(b['fits'])
        self.assertEqual(b['verdict'],'depends on batching')
        c = candidates['C.level.native_pages']
        self.assertEqual(c['needed_bytes'],144)
        self.assertEqual(c['headroom_bytes'],-16)
        self.assertFalse(c['fits'])  # A tiny texture does not make the whole level fit.
        self.assertEqual(c['section_bytes'],dict(baseline,tex=1))
        self.assertTrue(all(v['budget_bytes']==128 for v in candidates.values()))


if __name__=='__main__':
    start = time.monotonic()
    suite = unittest.defaultTestLoader.loadTestsFromModule(__import__(__name__))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if result.wasSuccessful():
        print(f'PASS cook-tests: {result.testsRun} tests; synthetic pc+psx; '
              f'truncated/trailing/count/accounting/budget negative controls; '
              f'elapsed_seconds={time.monotonic()-start:.3f}')
    raise SystemExit(0 if result.wasSuccessful() else 1)
