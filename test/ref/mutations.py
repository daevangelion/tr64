# SPDX-License-Identifier: MIT
"""Prove the analytic tests reject nine named renderer defects (synthetic only).

Each generated mutant runs in its own scratch import directory through
PYTHONPATH; repository sources and the interpreter's sys.path are never edited.
"""
import argparse
import ast
import copy
import os
from pathlib import Path
import subprocess
import sys
import tempfile

EXPECTED = {
    'fog_removed':'test_darkness_fog_and_psx_neutral_are_analytic',
    'pc_darkness_doubled':'test_darkness_fog_and_psx_neutral_are_analytic',
    'psx_intensity_inverted':'test_darkness_fog_and_psx_neutral_are_analytic',
    'static_rotation_sign':'test_static_quarter_turn_sign',
    'near_portal_expansion_removed':'test_near_portal_expansion_and_parent_intersection',
    'parent_intersection_removed':'test_near_portal_expansion_and_parent_intersection',
    'static_fog_removed':'test_static_fog_at_origin_is_not_omitted',
    'far_clip_removed':'test_far_rejection_keeps_crossing_triangle',
    'clip_growth_not_propagated':'test_clip_growth_is_repropagated',
}


class Mutate(ast.NodeTransformer):
    def __init__(self,name):
        self.name,self.function,self.changed = name,'',0

    def visit_FunctionDef(self,node):
        previous = self.function
        self.function = node.name
        self.generic_visit(node)
        self.function = previous
        return node

    def visit_Return(self,node):
        if self.name=='fog_removed' and self.function=='fog_factor':
            node.value = ast.parse('np.zeros_like(z)').body[0].value
            self.changed += 1
        if self.function=='darkness':
            if self.name=='pc_darkness_doubled' and isinstance(node.value,ast.Subscript):
                node.value = ast.BinOp(node.value,ast.Mult(),ast.Constant(2))
                self.changed += 1
            if self.name=='psx_intensity_inverted' and isinstance(node.value,ast.BinOp):
                node.value = node.value.right
                self.changed += 1
        return node

    def visit_Assign(self,node):
        text = ast.unparse(node)
        if self.name=='static_rotation_sign' and self.function=='render' and text.startswith('c, s ='):
            node.value.elts[1] = ast.UnaryOp(ast.USub(),node.value.elts[1])
            self.changed += 1
        elif self.name=='parent_intersection_removed' and self.function=='portal_clip' and (text.startswith('bounds[:2] = np.maximum') or text.startswith('bounds[2:] = np.minimum')):
            node.value = copy.deepcopy(node.targets[0])
            node.value.ctx = ast.Load()
            self.changed += 1
        elif self.name=='static_fog_removed' and self.function=='draw_faces' and text.startswith('factors = fog_factor'):
            node.value = ast.parse('np.zeros(len(view))').body[0].value
            self.changed += 1
        return node

    def visit_If(self,node):
        test = ast.unparse(node.test)
        if self.name=='far_clip_removed' and self.function=='draw_faces' and test.startswith('np.all(tri[:, 2] > far)'):
            node.test = ast.Constant(False)
            self.changed += 1
        if self.name=='near_portal_expansion_removed' and self.function=='portal_clip' and 'a[2] <= 0' in test and 'b[2] <= 0' in test:
            node.test = ast.Constant(False)
            self.changed += 1
        elif self.name=='clip_growth_not_propagated' and self.function=='visible_rooms' and test.startswith('old is None or'):
            node.test = ast.Compare(ast.Name('old',ast.Load()),[ast.Is()],[ast.Constant(None)])
            self.changed += 1
        self.generic_visit(node)
        return node


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--scratch',required=True,type=Path)
    args = p.parse_args()
    args.scratch.mkdir(parents=True,exist_ok=True)
    root = Path(__file__).resolve().parents[2]
    original = ast.parse((root/'test/ref/render.py').read_text())
    failures = []
    for name,test in EXPECTED.items():
        transform = Mutate(name)
        tree = transform.visit(copy.deepcopy(original))
        if not transform.changed:
            raise RuntimeError('mutation target disappeared: '+name)
        ast.fix_missing_locations(tree)
        with tempfile.TemporaryDirectory(prefix=name+'-',dir=args.scratch) as temp:
            Path(temp,'render.py').write_text(ast.unparse(tree)+'\n')
            environment = {**os.environ,'PYTHONDONTWRITEBYTECODE':'1',
                           'PYTHONPATH':os.pathsep.join([temp,str(root/'tools/cook'),str(root/'test/ref')])}
            result = subprocess.run(['rtk',sys.executable,'-m','unittest','test_render.RendererTests.'+test,'-v'],
                                    cwd=args.scratch,env=environment,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            named_failure = any(line.startswith(test+' ') and line.endswith('... FAIL') for line in result.stdout.splitlines())
            rejected = result.returncode!=0 and named_failure
            print(name+': '+('REJECTED by '+test if rejected else 'SURVIVED OR WRONG FAILURE'),flush=True)
            if not rejected:
                failures.append(name)
                print(result.stdout)
    print(f'MUTATIONS: {len(EXPECTED)-len(failures)}/{len(EXPECTED)} rejected by intended analytic tests')
    raise SystemExit(bool(failures))


if __name__=='__main__':
    main()
