# SPDX-License-Identifier: MIT
"""Production common mixer + stream/CRC/io, synthetic platform, real host WAV."""
import argparse
import json
import os
import struct
import subprocess
import wave
from pathlib import Path
from n64_snd_view import Section


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--harness', required=True, type=Path)
    parser.add_argument('--fixtures', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--muted', action='store_true')
    parser.add_argument('--pitch', type=int, choices=(115,128,140), default=128)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    reports=[]
    for source in ('pc','psx','psx-spu'):
        for suffix in ('','-stream'):
            name=source+suffix
            fixture=args.fixtures/(name+'.sn64')
            section=Section(fixture.read_bytes())
            pcm=section.pcm(0);rate=section.samples[0][2]
            path=args.out/(name+'.wav')
            env=dict(os.environ,TR64_AUDIO_WAV=str(path))
            command=['rtk',str(args.harness),str(fixture)]
            if args.pitch != 128: command.append(f'--pitch{args.pitch}')
            run=subprocess.run(command,env=env,capture_output=True,text=True,check=True)
            (args.out/(name+'.log')).write_text(run.stdout+run.stderr)
            with wave.open(str(path),'rb') as wav:
                assert wav.getparams()[:4] == (2,2,22050,22050)
                samples=struct.unpack('<44100h',wav.readframes(22050))
            expected=[]
            for frame in range(22050):
                index=(frame*((args.pitch*rate<<9)//22050))>>16
                value=pcm[index] if not args.muted and index<len(pcm) else 0
                # Fixed gain64 is unity, then the shared output master is1/2.
                mastered=abs(value)//2*(-1 if value<0 else 1)
                expected.extend((mastered,mastered))
            assert tuple(expected)==samples, name+' PCM/phase/unique/stereo mismatch'
            assert ('played=0 ' if args.muted else 'played=1 ') in run.stdout
            assert 'underruns=0 ' in run.stdout and 'clipped=0 ' in run.stdout
            assert 'resident_capacity=25968 ' in run.stdout
            assert 'used=107600 peak=107600' in run.stdout
            if suffix and not args.muted:
                assert 'stream_reads=0 ' not in run.stdout
            if not args.muted:
                bad=subprocess.run(['rtk',str(args.harness),str(fixture),'--corrupt'],capture_output=True,text=True)
                assert bad.returncode and 'corrupt checksum' in bad.stderr+bad.stdout
            reports.append(dict(source=name,frames=22050,exact_pcm=True,muted=args.muted,
                                corrupt_rejected=not args.muted))
    for control in ('--audio-bump','--audio-reset'):
        bad=subprocess.run(['rtk',str(args.harness),str(args.fixtures/'pc.sn64'),control],capture_output=True,text=True)
        assert bad.returncode and 'owned by library allocator' in bad.stdout+bad.stderr
    (args.out/'report.json').write_text(json.dumps(reports,indent=2)+'\n')
    print(json.dumps(reports))


if __name__ == '__main__':
    main()
