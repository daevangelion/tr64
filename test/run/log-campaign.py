# SPDX-License-Identifier: MIT
"""One actual host image, sixteen real startups/frame0; not gameplay parity."""
import argparse
import ast
import importlib.util
import os
from pathlib import Path
import re
import sys
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('scene',ROOT/'test/run/scene.py')
scene=importlib.util.module_from_spec(spec);spec.loader.exec_module(scene)
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--executable',required=True,type=Path)
a=p.parse_args()
levels=None
for node in ast.parse((ROOT/'test/game/campaign-pack.py').read_text()).body:
    if isinstance(node,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='LEVELS' for t in node.targets):
        levels=ast.literal_eval(node.value)
if not levels:raise RuntimeError('canonical level list missing')
out=Path(os.environ['BUILD_DIR'])/'log-campaign'
out.mkdir(parents=True,exist_ok=True)
inputs=out/'empty.inputs';inputs.write_text('')
maximum=0
for level in levels:
    log=out/(level+'.log')
    scene.command([a.executable,'--game-input',inputs,'--game-level',level,'--game-ticks','0','--game-live'],log,timeout=60)
    text=log.read_text()
    stats=re.findall(r'^log-ring: bytes=(\d+) peak_pending=(\d+) dropped=(\d+)$',text,re.M)
    if not stats or any(int(loss) or int(peak)>4096 for _,peak,loss in stats):
        raise AssertionError('4KiB campaign burst proof failed: '+level+' '+str(stats))
    peak=max(int(peak) for _,peak,_ in stats);maximum=max(maximum,peak)
    print(f'log campaign startup PASS {level}: frame0, peak={peak}, loss0')
print(f'log campaign PASS levels={len(levels)} maximum={maximum}; one existing executable, no per-level builds')
