# SPDX-License-Identifier: MIT
"""Synthetic core emitters exercise production ambient selection and mixer."""
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
    for name in ('pc','pc-stream','psx','psx-stream','psx-spu','psx-spu-stream'):
        path=a.fixtures/(name+'.sn64');section=Section(path.read_bytes())
        pcm=section.pcm(0);rate=section.samples[0][2]
        for mode in ('loop','once','settings'):
            wav_path=a.out/f'{name}-{mode}.wav'
            run=subprocess.run(['rtk',str(a.harness),str(path),'--ambient-'+mode],check=True,
                               env=dict(os.environ,TR64_AUDIO_WAV=str(wav_path)),capture_output=True,text=True)
            (a.out/f'{name}-{mode}.log').write_text(run.stdout+run.stderr)
            with wave.open(str(wav_path),'rb') as w:
                assert w.getnframes()==22050
                actual=struct.unpack('<44100h',w.readframes(22050))
            expected=[]
            for f in range(22050):
                if mode!='once':
                    start=0 if f<3*735 else 6*735 if 6*735<=f<9*735 else None
                    index=(f-start)*rate//22050 if start is not None else None
                else:index=f*rate//22050
                value=pcm[index%len(pcm)] if index is not None and (mode!='once' or index<len(pcm)) else 0
                mastered=abs(value*7)//16*(-1 if value<0 else 1)
                expected.extend((mastered,mastered))
            assert actual==tuple(expected), f'{name} {mode}: flip/range/loop/one-shot mismatch'
            marker='sources=1 started=1 stopped=0' if mode=='once' else 'sources=1 started=2 stopped=2'
            assert 'audio-ambient: '+marker in run.stdout
            assert 'events=0 ' in run.stdout and 'underruns=0 ' in run.stdout and 'clipped=0 ' in run.stdout
            cases+=1
    print(f'ambient PASS: {cases} exact PCM cases; flip stops/restarts, range silence, non-loop not repeated')


if __name__=='__main__':main()
