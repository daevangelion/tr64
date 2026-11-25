# SPDX-License-Identifier: MIT
"""Exact channel PCM for canonical listener directions; no distance refade."""
import argparse
import os
import struct
import subprocess
import wave
from pathlib import Path
from n64_snd_view import Section


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('harness','fixtures','out'):p.add_argument('--'+name,required=True,type=Path)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    cases=0
    for source in ('pc','pc-stream','psx','psx-stream','psx-spu','psx-spu-stream'):
        path=a.fixtures/(source+'.sn64');section=Section(path.read_bytes());pcm=section.pcm(0);rate=section.samples[0][2]
        for direction,gains in [('right',(1044,3481)),('left',(3481,1044)),('front',(4096,4096)),('back',(2867,2867))]:
            wav_path=a.out/f'{source}-{direction}.wav'
            r=subprocess.run(['rtk',str(a.harness),str(path),'--pan-'+direction],check=True,
                             env=dict(os.environ,TR64_AUDIO_WAV=str(wav_path)),capture_output=True,text=True)
            (a.out/f'{source}-{direction}.log').write_text(r.stdout+r.stderr)
            with wave.open(str(wav_path),'rb') as w:actual=struct.unpack('<44100h',w.readframes(22050))
            expected=[]
            for frame in range(22050):
                index=frame*rate//22050;value=pcm[index] if index<len(pcm) else 0
                expected.extend(abs(value*g)//8192*(-1 if value<0 else 1) for g in gains)
            assert actual==tuple(expected), f'{source}/{direction}: channel gain or duplicate attenuation'
            assert 'underruns=0 ' in r.stdout and 'clipped=0 ' in r.stdout
            cases+=1
    print(f'stereo PASS: {cases} exact channel PCM cases; pan signs, facing, no double distance')


if __name__=='__main__':main()
