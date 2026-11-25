# SPDX-License-Identifier: MIT
"""Read every source level, independently account, compare, and size M0."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import time
import account
import equivalence
import sizing
import report_models
import invariants
import src_pc
import src_psx
from binary import FormatError
from pool_rows import pool_table


def write_json(path, value):
    path.write_text(json.dumps(value,indent=2,sort_keys=True)+'\n')


def run(data_root, output, pools_path):
    started = time.monotonic()
    output.mkdir(parents=True,exist_ok=True)
    pc_root,psx_root = data_root/'pc',data_root/'psx'/'PSXDATA'
    paths = {}
    for source,root,suffix in [('pc',pc_root,'.phd'),('psx',psx_root,'.psx')]:
        paths[source] = sorted(p for p in root.iterdir() if p.is_file() and p.suffix.lower()==suffix)
        if not paths[source]:
            raise FormatError(f'no {source} levels in {root}')
        stems = [p.stem.lower() for p in paths[source]]
        if len(stems)!=len(set(stems)):
            raise FormatError(f'duplicate case-insensitive {source} level names')
    tool = hashlib.sha256()
    for path in sorted(Path(__file__).parent.glob('*.py')):
        tool.update(path.name.encode()+b'\0'+path.read_bytes())
    tool.update(pools_path.read_bytes())
    manifest = dict(tool_sha256=tool.hexdigest(),model='m0-level-arena-v2',inputs=[])
    budgets = sizing.pools(pools_path)
    if 'level' not in budgets:
        raise FormatError('pool table requires a CARVE level row; source sections are not independent pools')
    counts,measurements,decoded = [],[],{}
    for source,reader in [('pc',src_pc),('psx',src_psx)]:
        decoded[source] = {}
        for path in paths[source]:
            data = path.read_bytes()
            level = reader.read(data,path.stem.lower())
            counts.append(account.check(level,data))
            counts[-1]['layout_invariants'] = invariants.local(level)
            decoded[source][level.name] = level
            manifest['inputs'].append(dict(source=source,name=level.name,sha256=level.sha256))
            print(f'READ {source}/{level.name}: {len(data)} bytes, '
                  f'{len(level.sections)} sections, unexplained=0',flush=True)
    pc,psx = decoded['pc'],decoded['psx']
    congruence = {name:invariants.paired(pc[name],psx[name]) for name in sorted(pc.keys()&psx.keys())}
    comparison = dict(unpaired_pc=sorted(pc.keys()-psx.keys()),
        unpaired_psx=sorted(psx.keys()-pc.keys()),
        levels=[equivalence.compare(pc[name],psx[name]) for name in sorted(pc.keys() & psx.keys())])
    for source,levels in decoded.items():
        for level in levels.values():
            measurements.append(sizing.measure(level,budgets))
            print(f'SIZE {source}/{level.name}: A1+A2, B four variants/three policies, C',flush=True)
    key = hashlib.sha256(json.dumps(manifest,sort_keys=True).encode()).hexdigest()
    metadata = dict(content_key=key,manifest=manifest,
        executable=str(Path(__file__).resolve()),python=sys.version,
        scenes=[],artifact_directory=str(output.resolve()))
    count_report = dict(metadata=metadata,levels=counts,
        source_counts={source:len(items) for source,items in paths.items()},
        paired_geometry_invariants=congruence,
        unexplained_bytes=sum(row['unexplained_bytes'] for row in counts))
    size_report = dict(metadata=metadata,budgets=budgets,levels=measurements,
        binding_pool='level',section_alignment_bytes=16,section_names=list(sizing.LEVEL_SECTIONS),
        summary=sizing.summary(measurements,budgets),
        candidate_summary=report_models.candidate_summary(measurements,budgets),
        model_names=dict(A1='stock tiny3d, borders on every tile edge (assumption)',
                         A2='stock tiny3d, internal borders only and outer clamp (assumption)',
                         B='shared vertices, per-corner UV; four stride/cache variants',
                         C='native pages / union coverage; candidate, not implemented backend'))
    comparison['metadata'] = metadata
    write_json(output/'counts.json',count_report)
    write_json(output/'equivalence.json',comparison)
    (output/'equivalence.md').write_text(equivalence.markdown(comparison))
    write_json(output/'sizing.json',size_report)
    (output/'sizing.md').write_text(sizing.markdown(size_report))
    if 'level2' in pc:
        sections = pc['level2'].sections
        def payload(names):
            return sum(s['payload_bytes'] for s in sections if s['name'] in names)
        animation = ('animations','states','ranges','commands','bones','frames')
        cross = dict(texture_pages=payload(('texture_pages',)),
            room_geometry=sum(s['payload_bytes'] for s in sections if s['name'].endswith('/geometry')),
            room_triangles=sum(s['triangle_equivalents'] for s in sections if s['name'].endswith('/geometry')),
            rooms=len(pc['level2'].rooms),floor_data=payload(('floor_data',)),
            mesh_data_and_pointers=payload(('mesh_data','mesh_pointers')),
            animation_block=payload(animation),frames=payload(('frames',)),samples=payload(('samples',)))
        print('PC LEVEL2 payload cross-check: '+json.dumps(cross,sort_keys=True))
    elapsed = time.monotonic()-started
    print(f'PASS cook-m0: pc={len(pc)} psx={len(psx)} paired={len(comparison["levels"])} '
          f'unexplained_bytes=0 reports=5 elapsed_seconds={elapsed:.3f}')
    print('CONTENT_KEY '+key)
    print('REPORTS '+str(output.resolve()))
    print(sizing.markdown(size_report).split('## Worst cases and cart budget',1)[1])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--pools',type=Path,help='override the build-selected memory rows')
    args = parser.parse_args()
    try:
        run(args.data,args.output,args.pools or pool_table())
    except (FormatError,OSError,ValueError,IndexError) as error:
        print(f'FAIL cook-m0: {error}',file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
