# SPDX-License-Identifier: MIT
"""Resident current-track lookup through real mounted catalogs and engine/io."""
import argparse
from pathlib import Path
import struct
import subprocess
import zlib
import n64_pack as pack

MUSIC=0x60000

def directory(blob,name):
    count=struct.unpack_from('>I',blob,12)[0]
    for i in range(count):
        pos=32+i*48
        raw,offset,n,crc,zero=pack.LEVEL.unpack_from(blob,pos)
        if raw.split(b'\0',1)[0].decode()==name:return pos,offset,n
    raise AssertionError('missing test directory')

def repair(blob,pos,offset,count):
    struct.pack_into('>I',blob,pos+40,zlib.crc32(blob[offset:offset+64*count]))
    levels=struct.unpack_from('>I',blob,12)[0]
    struct.pack_into('>I',blob,24,zlib.crc32(blob[32:32+48*levels]))

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--harness',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--cache',type=Path,required=True)
    a=parser.parse_args();a.out.mkdir(parents=True,exist_ok=True);codec=pack.compressor(a.cache);runs=0
    def run(label,blob,mode='catalog',reason=None):
        nonlocal runs
        path=a.out/(label+'.pack');path.write_bytes(blob)
        result=subprocess.run(['rtk',str(a.harness),str(path),'one',mode],capture_output=True,text=True,timeout=12)
        text=result.stdout+result.stderr;(a.out/(label+'.log')).write_text(text);runs+=1
        if reason:
            if not result.returncode or reason not in text:raise AssertionError(f'{label}: expected {reason}\n{text}')
            print('catalog negative PASS',label,text.splitlines()[-1])
        elif result.returncode or 'catalog: blocks=' not in text or 'unit_loaded=0 io_used=0' not in text:
            raise AssertionError(label+'\n'+text)
    music=pack.Section(MUSIC,b'H'*64+bytes(600*16)+b'odd',header_bytes=64,chunk_bytes=16,compression='none')
    base={'MUSIC_02':[music],'MUSIC_57':[pack.Section(MUSIC,b'U'*64,compression='none')],
          'one':[pack.Section(0x10000,b'T'*17),pack.Section(0x20000,b'R'*32)]}
    blob=pack.build(base,codec)
    for mode in ('catalog','catalog-busy','catalog-drain','catalog-busy-drain'):
        run(mode,blob,mode)
    run('index-too-small',blob,'catalog-small','pack stream table: index capacity')
    run('scratch-too-small',blob,'catalog-workspace','pack stream table: invalid workspace')
    run('active-query',blob,'catalog-active','pack stream table: query active')
    pos,offset,count=directory(blob,'MUSIC_02')
    # Corrupt only discarded SHA bytes in the last page: full wire CRC still rejects.
    bad=bytearray(blob);bad[offset+count*64-1]^=1
    run('last-page-crc',bad,reason='pack stream table: corrupt CRC')
    for label,word,value,reason in (
        ('wrong-kind',0,0x70000,'kind mismatch'),
        ('logical-gap',4,1,'logical destination bounds'),
        ('raw-too-large',16,16385,'compressed block'),
        ('rom-outside',8,len(blob)+16,'descriptor bounds'),
        ('compressed',24,1,'compressed block')):
        bad=bytearray(blob);struct.pack_into('>I',bad,offset+64+word,value);repair(bad,pos,offset,count)
        run(label,bad,reason='pack stream table: '+reason)
    # Directory reference must be rejected before reading an out-of-ROM table.
    bad=bytearray(blob);struct.pack_into('>I',bad,pos+32,len(blob)+16)
    levels=struct.unpack_from('>I',bad,12)[0];struct.pack_into('>I',bad,24,zlib.crc32(bad[32:32+48*levels]))
    run('catalog-table-outside',bad,reason='pack catalog: directory table bounds')
    # Compression is forbidden even if its physical lengths happen to match.
    automatic=dict(base);automatic['MUSIC_02']=[pack.Section(MUSIC,b'A'*8192,chunk_bytes=4096)]
    run('auto-compressed',pack.build(automatic,codec),reason='pack stream table: compressed block')
    # Bootstrap has no resident directory entry and does not call mem_level_begin.
    video=pack.Section(0x70000,music.data,header_bytes=64,chunk_bytes=16,compression='none')
    bootstrap=pack.build({'VIDEO_00':[video]},codec)
    for mode in ('catalog-only','catalog-only-busy','catalog-only-drain','catalog-only-busy-drain'):
        run(mode,bootstrap,mode)
    run('bootstrap-no-scope',bootstrap,'catalog-only-no-scope','load scope required')
    run('bootstrap-kind',bootstrap,'catalog-only-kind','invalid load/pool')
    bad=bytearray(bootstrap);bad[0]^=1
    run('bootstrap-header',bad,'catalog-only','invalid header')
    bad=bytearray(bootstrap);bad[32]^=1
    run('bootstrap-directory-crc',bad,'catalog-only','pack directory: corrupt CRC')
    bad=bytearray(bootstrap);bad[32:64]=b'A'*32
    struct.pack_into('>I',bad,24,zlib.crc32(bad[32:80]))
    run('bootstrap-directory-name',bad,'catalog-only','invalid/unsorted level name')
    for label,mode,reason in (
        ('bind-live-io','bind-busy','level pack stream bind: I/O pending'),
        ('bind-io-lease','bind-lease','level pack stream bind: exclusive io lease active'),
        ('bind-load-scope','bind-scope','level pack stream bind: load scope active')):
        run(label,bootstrap,mode,reason)
    print(f'catalog native PASS {runs} production runs; late CRC/bounds/kind/codec/capacity, queue BUSY, drain without resubmit, post-unload payload CRC, VIDEO-only boot without level planning, live-bind guards')

if __name__=='__main__':main()
