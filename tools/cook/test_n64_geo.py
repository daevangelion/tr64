# SPDX-License-Identifier: MIT
"""Independent geometry expansion checks and optional retail evidence sweep."""
import argparse
from collections import Counter
from fractions import Fraction
import json
from pathlib import Path
import unittest
import struct
from unittest.mock import patch

import ir
import n64_geo
import n64_geo_view
import n64_tex
import src_pc
import src_psx
import synthetic


def nearest(x):
    x=Fraction(x); sign=-1 if x<0 else 1; x=abs(x)
    return sign*((x.numerator*2+x.denominator)//(2*x.denominator))


def shade(g, room, water):
    if room:
        t=(Fraction(1,2),Fraction(4,5),Fraction(4,5)) if water else (1,1,1)
        return tuple(min(255,max(0,nearest(256*min(g,Fraction(3,2))*v/Fraction(3,2)))) for v in t)+(255,)
    c=min(255,max(0,nearest(128*g)))
    return (c,c,c,255)


def vertices(level,obj,room):
    result=[]
    for i,v in enumerate(obj.vertices):
        if room:
            pos=tuple(v[k] for k in ('x','y','z')); light=v['lighting']; d=light['value']
            if light['encoding']=='psx_vertex_8bit': d=8191-32*d
        else:
            pos=v; d=obj.lighting[i] if obj.lighting is not None else 4096
        g=max(Fraction(0),min(Fraction(2),Fraction(8191-d,4096)))
        result.append((pos,g))
    return result


def cut(poly, bound, lower):
    if not poly: return []
    out=[]
    inside=lambda p: p[5]>=bound if lower else p[5]<=bound
    for i,cur in enumerate(poly):
        nxt=poly[(i+1)%len(poly)]
        if inside(cur): out.append(cur)
        if inside(cur)!=inside(nxt):
            ratio=(bound-cur[5])/(nxt[5]-cur[5])
            out.append(tuple(a+ratio*(b-a) for a,b in zip(cur,nxt)))
    unique=[]
    for p in out:
        if not unique or unique[-1]!=p: unique.append(p)
    if len(unique)>1 and unique[0]==unique[-1]: unique.pop()
    return unique


def nonzero(poly):
    p=poly[0]
    for j in range(1,len(poly)-1):
        a=[poly[j][i]-p[i] for i in range(3)]; b=[poly[j+1][i]-p[i] for i in range(3)]
        if (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]) != (0,0,0): return True
    return False


def canonical(face,material,flat,flags,corners):
    return (face,material,flat,flags & 7,tuple(sorted(corners,key=repr)))


def expected(level,obj,room,tm):
    vs=vertices(level,obj,room); result=Counter()
    descriptors={r['texture']:r for r in tm['descriptors'] if r['kind']=='object'}
    flats={row[0]:i for i,row in enumerate(tm['flat_colours'])}
    for fi,f in enumerate(obj.quads+obj.triangles if room else obj.faces):
        flat=f.texture is None
        material=flats[f.color] if flat else tm['object_descriptor_map'][str(f.texture)]
        flags=(0 if flat else level.object_textures[f.texture].attribute) | (4 if f.flags & 0x8000 else 0)
        uv=[None]*len(f.vertices) if flat else ir.face_uv(level,f)
        data=[(vs[i][0],vs[i][1],uv[j]) for j,i in enumerate(f.vertices)]
        water=room and bool(obj.flags&1)
        desc=None if flat else descriptors[f.texture]
        ts=((0,1,2),) if len(data)==3 else ((0,1,2),(0,2,3))
        if flat or len(desc['tiles'])==1:
            for tri in ts:
                corners=[(data[j][0],shade(data[j][1],room,water),data[j][2]) for j in tri]
                result[canonical(fi,material,flat,flags,corners)]+=1
            continue
        x,y,_,height=desc['rect']
        for tri in ts:
            p=[tuple(map(Fraction,data[j][0]))+(data[j][1],Fraction(data[j][2][0]-x),Fraction(data[j][2][1]-y)) for j in tri]
            for tile in desc['tiles']:
                start,h=tile[1],tile[3]
                clipped=cut(p,Fraction(2*start-1,2),True) if start else p
                if start+h<height: clipped=cut(clipped,Fraction(2*(start+h)-1,2),False)
                if len(clipped)<3 or not nonzero(clipped): continue
                polys=[clipped] if len(clipped)<=4 else [[clipped[0],clipped[i],clipped[i+1]] for i in range(1,len(clipped)-1)]
                for poly in polys:
                    corners=[]
                    for q in poly:
                        pos=tuple(nearest(z) for z in q[:3]); color=shade(q[3],room,water)
                        st=(x+nearest(32*q[4])/32,y+start+nearest(32*(q[5]-start))/32)
                        corners.append((pos,color,st))
                    for tt in ((0,1,2),) if len(poly)==3 else ((0,1,2),(0,2,3)):
                        result[canonical(fi,material,False,flags,[corners[i] for i in tt])]+=1
    return result

def check_run_order(blob):
    run_count=struct.unpack_from('>I',blob,20)[0]
    run_offset=struct.unpack_from('>I',blob,40)[0]
    face_offset=struct.unpack_from('>I',blob,44)[0]
    face_cursor=0
    for i in range(run_count):
        count=struct.unpack_from('>H',blob,run_offset+i*24+6)[0]
        ids=[struct.unpack_from('>H',blob,face_offset+(face_cursor+j)*8+6)[0] for j in range(count)]
        if ids != sorted(ids):
            raise AssertionError(f'run {i} is not in source polygon order')
        face_cursor+=count



def compare(level,tm,rooms,meshes):
    counts=[0,0,0]
    for isroom,blob,objects in ((True,rooms,list(enumerate(level.rooms))),(False,meshes,sorted(level.meshes.items()))):
        check_run_order(blob)
        actual=n64_geo_view.decode(blob,tm)
        for source,obj in objects:
            got=Counter(canonical(t['face'],t['material'],t['flat'],t['flags'],t['corners']) for t in actual[source])
            want=expected(level,obj,isroom,tm)
            if got!=want:
                raise AssertionError(f'{level.source} {level.name} {"room" if isroom else "mesh"} {source}: missing {list((want-got).items())[:1]} extra {list((got-want).items())[:1]}')
            counts[0 if isroom else 1]+=1; counts[2]+=sum(got.values())
    return counts


class GeometryTests(unittest.TestCase):
    def test_synthetic_sources(self):
        for source in ('pc','psx'):
            level=synthetic.fixture(source); _,tm=n64_tex.cook(level)
            a,b,_=n64_geo.cook(level,tm)
            self.assertGreater(compare(level,tm,a,b)[2],0)

    def test_decoder_rejects_truncation(self):
        level=synthetic.fixture('pc'); _,tm=n64_tex.cook(level); a,_,_=n64_geo.cook(level,tm)
        with self.assertRaises(ValueError): n64_geo_view.decode(a[:-1],tm)

    def test_comparison_rejects_corrupt_vertex(self):
        level=synthetic.fixture('pc'); _,tm=n64_tex.cook(level); a,b,_=n64_geo.cook(level,tm)
        damaged=bytearray(a)
        vertex_offset=int.from_bytes(damaged[48:52],'big')
        damaged[vertex_offset+1] ^= 1
        with self.assertRaises(AssertionError): compare(level,tm,damaged,b)

    def test_pool_overflow_is_rejected(self):
        level=synthetic.fixture('pc'); _,tm=n64_tex.cook(level)
        rooms,meshes,_=n64_geo.cook(level,tm)
        needed=((len(rooms)+15)&~15)+((len(meshes)+15)&~15)
        with patch('n64_geo.pools',return_value={'level':needed}):
            n64_geo.cook(level,tm)
        with patch('n64_geo.pools',return_value={'level':needed-1}):
            with self.assertRaisesRegex(ValueError, 'geometry alone exceeds level arena'):
                n64_geo.cook(level,tm)

def sweep(data,cache,output):
    totals=[0,0,0]; rows=[]
    for source,reader in [('pc',src_pc),('psx',src_psx)]:
        base=data/'pc' if source=='pc' else data/'psx'/'PSXDATA'
        names=('title','gym','level1','level2','level3a','level3b','level4','level5','level6',
               'level7a','level7b','level8a','level8b','level8c','level10a','level10b','level10c',
               'cut1','cut2','cut3','cut4')
        for name in names:
            texdir=cache/'sections'/source/name
            path=base/(name+'.phd' if source=='pc' else name.upper()+'.PSX')
            level=reader.load(path)
            candidates=list(texdir.glob('*/manifest.json'))
            manifests=[json.loads(p.read_text()) for p in candidates]
            tm=next(m for m in manifests if m['source_sha256']==level.sha256)
            rooms,meshes,manifest=n64_geo.cook(level,tm)
            count=compare(level,tm,rooms,meshes)
            totals=[a+b for a,b in zip(totals,count)]
            row=dict(source=source,level=name,rooms=manifest['rooms'],meshes=manifest['meshes'],checked=count)
            rows.append(row)
            print(source,name,len(rooms),len(meshes),count,flush=True)
    output.mkdir(parents=True,exist_ok=True)
    (output/'report.json').write_text(json.dumps(dict(totals=totals,levels=rows),indent=2))
    print('TOTAL rooms meshes triangles',totals,flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('--sweep',action='store_true'); parser.add_argument('--data',type=Path,default=Path('/tmp/tr64/data')); parser.add_argument('--cache',type=Path,default=Path('/tmp/tr64/evidence/m1-tex2')); parser.add_argument('--output',type=Path,default=Path('/tmp/tr64/evidence/Geo')); args=parser.parse_args()
    if args.sweep: sweep(args.data,args.cache,args.output)
    else: unittest.main(argv=['test_n64_geo'])
