# SPDX-License-Identifier: MIT
"""Synthetic tests; PYTHONPATH=test/run python3 -m unittest discover -s test/run -p test_m1_compare.py."""
import unittest
import numpy as np
from m1_compare import compare,compare_cart,verdict


class ComparisonTests(unittest.TestCase):
    def setUp(self):
        y,x = np.mgrid[:240,:320]
        self.image = np.stack([x%256,y%256,(x+y)%256],axis=-1).astype(np.uint8)
        self.maps = {'coverage':np.ones((240,320),bool),'room':np.zeros((240,320),int),
                     'texture':np.zeros((240,320),int),'depth':np.full((240,320),512.),
                     'texel':np.stack([x,y],axis=-1).astype(float)}
        self.maps['visible_rooms'] = np.array([0],dtype=int)

    def test_identity_zero(self):
        for value in compare(self.image,self.image,self.maps,self.maps).values():
            self.assertAlmostEqual(value,0)

    def test_maps_are_mandatory(self):
        with self.assertRaisesRegex(ValueError,'requires'):
            compare(self.image,self.image)
        with self.assertRaisesRegex(ValueError,'requires'):
            compare(self.image,self.image,self.maps,{k:v for k,v in self.maps.items() if k!='texel'})

    def test_bad_map_shape_or_domain_is_error(self):
        with self.assertRaisesRegex(ValueError,'shape'):
            compare(self.image,self.image,self.maps,{**self.maps,'texel':np.zeros((240,320))})
        with self.assertRaisesRegex(ValueError,'depth'):
            compare(self.image,self.image,self.maps,{**self.maps,'depth':np.full((240,320),np.nan)})

    def test_wrong_texture_ids_even_identical_rgb(self):
        wrong = {**self.maps,'texture':np.ones((240,320),int)}
        self.assertGreater(compare(self.image,self.image,self.maps,wrong)['texture'],.98)

    def test_wrong_uv_even_identical_rgb(self):
        wrong = {**self.maps,'texel':self.maps['texel']+50}
        self.assertGreater(compare(self.image,self.image,self.maps,wrong)['texel'],1)

    def test_missing_room_and_coverage(self):
        wrong = {**self.maps,'coverage':np.zeros((240,320),bool)}
        result = compare(self.image,self.image,self.maps,wrong)
        self.assertEqual(result['coverage'],1)
        self.assertEqual(result['silhouette'],1)
        self.assertGreater(result['room'],.98)

    def test_gray_and_bright_are_measured(self):
        gray = np.repeat(self.image.mean(axis=2,keepdims=True),3,axis=2).astype(np.uint8)
        result = compare(self.image,gray,self.maps,self.maps)
        self.assertGreater(result['chroma'],.1)
        result = compare(self.image,np.minimum(self.image.astype(int)*2,255).astype(np.uint8),self.maps,self.maps)
        self.assertGreater(result['colour'],.1)

    def test_neutral_light_loses_repeated_texel_shading(self):
        repeated = {**self.maps,'texel':np.zeros((240,320,2))}
        y = np.mgrid[:240,:320][0]
        shaded = np.repeat((40+y//2)[:,:,None],3,axis=2).astype(np.uint8)
        neutral = np.full_like(shaded,int(shaded.mean()))
        result = compare(shaded,neutral,repeated,repeated)
        self.assertGreater(result['shading_loss'],3)

    def test_cart_reports_metrics_without_inventing_limits(self):
        quantized = ((self.image.astype(int)>>3)<<3).astype(np.uint8)
        self.assertLess(compare_cart(self.image,quantized)['pixel_mae'],8/255)
        self.assertGreater(compare_cart(self.image,np.zeros_like(self.image))['pixel_mae'],.1)

    def test_missing_metric_cannot_pass(self):
        with self.assertRaisesRegex(ValueError,'missing'):
            verdict({'colour':0},{'colour':.1,'texel':.1})

    def test_candidate_far_depth_cannot_hide_reference_geometry(self):
        candidate = self.image.copy()
        candidate[60:180,80:240] = 0
        depth = self.maps['depth'].copy()
        depth[60:180,80:240] = 20000
        wrong = {**self.maps,'depth':depth}
        errors = compare(self.image,candidate,self.maps,wrong)
        for key in ['coverage','room','texel']:
            self.assertGreater(errors[key],.01)
        # Only genuine far depth in the reference creates don't-care pixels,
        # including its one-pixel dilation for legitimate edge conventions.
        candidate[59:181,79:241] = 0
        errors = compare(self.image,candidate,wrong,self.maps)
        for key in ['coverage','room','texel','colour']:
            self.assertAlmostEqual(errors[key],0)


if __name__=='__main__':
    unittest.main()
