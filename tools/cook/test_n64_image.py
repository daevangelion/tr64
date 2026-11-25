# SPDX-License-Identifier: MIT
"""Synthetic UI64 publication/bounds through the production native level loader."""
import argparse
from pathlib import Path
import re
import struct
import n64_core as core
import core_checks
import n64_core_tests as tests
import n64_core_view as independent
import n64_pack as pack

KIND=0x50000

def image(format):
    size,palette=(77344,76832) if format==1 else (153632,0)
    return struct.pack('>4s7I',b'UI64',1,size,320,240,format,32,palette)+bytes(size-32)

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--harness',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--cache',type=Path,required=True)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True);codec=pack.compressor(a.cache);count=0
    for source in ('pc','psx'):
        level=tests.fixture(source);base,_=core.cook_sections(level)
        for format in (1,2):
            raw=image(format);section=pack.Section(KIND,raw)
            sections=base+[section];layout=pack.level_layout(sections)
            blob=pack.build({'synthetic':sections},codec)
            native=independent.read_pack(blob)['synthetic'];independent.equal_ir(level,native)
            path=a.out/f'{source}-{format}.pack';path.write_bytes(blob)
            text=tests.run(a.harness,path,mode='image');tests.checks(text,native);count+=1
            if f'level-image: bytes={len(raw)} format={format} width=320 height=240 hash={core.checksum(section)}' not in text:
                raise AssertionError('image verified-content fingerprint differs')
            manifest=dict(texture_bytes=len(native.sections[65536]),image_bytes=len(raw),
                image_format=format,image_checksum=core.checksum(section))
            if core_checks.texture_extent(manifest,text)!=layout['sections']['tex']:
                raise AssertionError('scene observable omitted image tex allocation')
            try:core_checks.texture_extent(dict(manifest,image_checksum='00000000'),text)
            except ValueError:pass
            else:raise AssertionError('scene observable accepted wrong image hash')
            tex=list(re.finditer(r'^level-section: tex .* size=(\d+) used=(\d+) peak=(\d+)$',text,re.M))[-1]
            if any(int(tex[i])!=layout['sections']['tex'] for i in (1,2,3)):
                raise AssertionError('TX64/image shared-section alignment differs')
            for label,offset,value,reason in (('version',4,2,'header'),('total',8,len(raw)-1,'header'),
                    ('width',12,319,'header'),('height',16,239,'header'),('format',20,3,'format/extent'),
                    ('pixels',24,33,'header'),('palette',28,17,'format/extent')):
                bad=bytearray(raw);struct.pack_into('>I',bad,offset,value)
                path=a.out/f'{source}-{format}-{label}.pack'
                path.write_bytes(pack.build({'synthetic':base+[pack.Section(KIND,bytes(bad))]},codec))
                tests.run(a.harness,path,expected='image section: invalid UI64 '+reason);count+=1
    # Inputs may be unsorted; plan order follows the wire table, not call order.
    small=[pack.Section(KIND,bytes(32)),pack.Section(0x10000,bytes(17))]
    if pack.level_layout(small,64)['sections']['tex']!=64:raise AssertionError('inter-kind tex alignment')
    try:pack.level_layout(small,63)
    except ValueError:pass
    else:raise AssertionError('image omitted from aggregate bound')
    print(f'image native PASS {count} production loads/rejections; CI8/RGBA, both sources, TX span retained, no unit after publication')

if __name__=='__main__':main()
