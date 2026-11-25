# SPDX-License-Identifier: MIT
"""Independent RG64/MG64 table reader for host verification."""
import struct


def signed(v):
    return v-65536 if v & 32768 else v


def decode(data, texture_manifest):
    head = struct.unpack_from('>4s15I',data)
    magic,version,size,no,nb,nr,nf,np,oo,bo,ro,fo,vo,nt,abi,res = head
    if magic not in (b'RG64',b'MG64') or version != 1 or size != len(data) or abi != 1 or res:
        raise ValueError('invalid geometry header')
    if any(x%8 for x in (oo,bo,ro,fo,vo)) or not (64<=oo<=bo<=ro<=fo<=vo):
        raise ValueError('invalid table offsets')
    if oo+no*32>bo or bo+nb*32>ro or ro+nr*24>fo or fo+nf*8>vo or vo+np*32!=size:
        raise ValueError('invalid table extents')
    objects = [struct.unpack_from('>IIHH6h2I',data,oo+32*i) for i in range(no)]
    batches = [struct.unpack_from('>6hHHIHH2I',data,bo+32*i) for i in range(nb)]
    records = [struct.unpack_from('>HBBHH4I',data,ro+24*i) for i in range(nr)]
    descriptors = {r['descriptor']:r for r in texture_manifest['descriptors'] if r['kind']=='object'}
    result = {o[0]:[] for o in objects}
    count=0; triangles=0
    for material,tile,flags,bi,n,*words in records:
        if bi>=nb or not n or flags&0xe0 or flags&3==3:
            raise ValueError('invalid run')
        b=batches[bi]; nv,oi,offset=b[6:9]
        if oi>=no or not 1<=nv<=70 or offset<vo or offset+32*((nv+1)//2)>size:
            raise ValueError('invalid batch')
        obj=objects[oi]
        if not obj[1]<=bi<obj[1]+obj[2]: raise ValueError('batch ownership')
        flat=bool(flags&8)
        if not flat:
            d=descriptors[material]
            start=d['tiles'][tile][1]; x,y,_,_=d['rect']
        for _ in range(n):
            if count>=nf: raise ValueError('too many faces')
            v0,v1,v2,v3,mapping,source_map,source_face=struct.unpack_from('>4BBBH',data,fo+count*8)
            count+=1
            ids=(v0,v1,v2) if v3==255 else (v0,v1,v2,v3)
            corners=[]
            for j,v in enumerate(ids):
                if v>=nv: raise ValueError('invalid vertex')
                pair=offset+(v//2)*32
                xyz=struct.unpack_from('>3h',data,pair+(v%2)*8)
                rgba=tuple(data[pair+16+(v%2)*4:pair+20+(v%2)*4])
                word=words[(mapping>>(2*j))&3]
                uv=None if flat else (x+(signed(word>>16)-16)/32,y+start+(signed(word&65535)-16)/32)
                corners.append((xyz,rgba,uv))
            for tri in ((0,1,2),) if len(ids)==3 else ((0,1,2),(0,2,3)):
                result[obj[0]].append(dict(face=source_face,material=material,flat=flat,flags=flags,source_map=source_map,corners=tuple(corners[i] for i in tri)))
                triangles+=1
    if count!=nf or triangles!=nt: raise ValueError('face/triangle count mismatch')
    return result
