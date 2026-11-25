# SPDX-License-Identifier: MIT
"""Reuse one build tree for six cart probes; preserve only compact evidence."""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path
from n64_snd_evidence import scene_measure

ROOT=Path(__file__).resolve().parents[2]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('build','out','pc-pack','psx-pack','pc-reference','psx-reference'):
        parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    references={source:hashlib.sha256(path.read_bytes()).hexdigest()
                for source,path in [('pc',args.pc_reference),('psx',args.psx_reference)]}
    cases=[('pc-one','pc',args.pc_pack,'one',1),('pc-six','pc',args.pc_pack,'six',6),
           ('psx-one','psx',args.psx_pack,'one',1),('psx-six','psx',args.psx_pack,'six',6),
           ('pc-on','pc',args.pc_pack,'on',None),('pc-off','pc',args.pc_pack,'off',0)]
    results=[]
    for name,source,pack,probe,voices in cases:
        destination=args.out/name
        if destination.exists():
            raise ValueError(f'evidence output already exists: {destination}')
        env=dict(os.environ,BUILD_DIR=str(args.build.resolve()),TR64_TEST='1',
                 TR64_LEVEL_PACK=str(pack.resolve()),TR64_SCENE_TEST_SOURCES=f'test/snd/{probe}.c',
                 TR64_RENDER_SCENE='test/snd/bridge-perf.scene.json')
        subprocess.run(['rtk',str(ROOT/'test/run/scene.sh'),'--ares','test/snd/bridge-perf.route'],
                       cwd=ROOT,env=env,check=True)
        produced=args.build/'scenes/ares/snd-bridge-perf'
        shutil.copytree(produced,destination)
        shots=list(destination.glob('ares-test-*.ppm'))
        if len(shots)!=1 or hashlib.sha256(shots[0].read_bytes()).hexdigest()!=references[source]:
            raise ValueError(f'{name}: rendering changed from ordinary bridge reference')
        result=scene_measure(destination/'engine.log')
        if not result['perf_only'] or result['ticks']!=180:
            raise ValueError(f'{name}: incomplete performance-only replay')
        if voices and voices not in result['voice_profiles']:
            raise ValueError(f'{name}: requested voice workload never mixed')
        if voices==0 and any(n for n in result['voice_profiles']):
            raise ValueError('sound-off control mixed active voices')
        result.update(case=name,source=source,pack_sha256=hashlib.sha256(pack.read_bytes()).hexdigest(),
                      reference_sha256=references[source])
        results.append(result)
        temporary=args.out/'summary.tmp'
        temporary.write_text(json.dumps(results,indent=2)+'\n')
        temporary.replace(args.out/'summary.json')
        print(f"audio benchmark {name}: work_mean_us={result['work']['mean']:.2f}",flush=True)


if __name__=='__main__':
    main()
