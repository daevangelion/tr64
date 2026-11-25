# SPDX-License-Identifier: MIT
"""Three event ticks then87 empty ticks, through the production mixer/IO."""
import argparse
import json
import os
import struct
import subprocess
import wave
from pathlib import Path
from n64_snd import encode
from n64_snd_view import Section
from test_native import fixture


def clip(value):
    return max(-32768,min(32767,value))


def master(value):
    return abs(value)//2*(-1 if value<0 else 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--harness', required=True, type=Path)
    parser.add_argument('--fixtures', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    results = []
    for name in ('pc','pc-stream','psx','psx-stream','psx-spu','psx-spu-stream'):
        path = args.fixtures/(name+'.sn64')
        section = Section(path.read_bytes())
        pcm, rate = section.pcm(0), section.samples[0][2]
        for mode in range(4):
            wav_path = args.out/f'{name}-mode{mode}.wav'
            run = subprocess.run(['rtk',str(args.harness),str(path),f'--mode{mode}'],
                                 env=dict(os.environ,TR64_AUDIO_WAV=str(wav_path)),
                                 capture_output=True,text=True,check=True)
            (args.out/f'{name}-mode{mode}.log').write_text(run.stdout+run.stderr)
            with wave.open(str(wav_path),'rb') as wav:
                assert wav.getnframes() == 90*735
                actual = struct.unpack('<'+str(90*735*2)+'h',wav.readframes(90*735))
            expected, clipped = [], 0
            for frame in range(90*735):
                if mode == 2:
                    value = pcm[(frame*rate//22050)%len(pcm)] if frame < 3*735 else 0
                else:
                    starts = [0] if mode == 0 else ([min(frame//735,2)*735] if mode == 1 else [0,735,1470])
                    value = 0
                    for start in starts:
                        pos = (frame-start)*rate//22050
                        if frame >= start and pos < len(pcm):
                            value += pcm[pos]
                value=master(value)
                clipped += value < -32768 or value > 32767
                expected.extend((clip(value),)*2)
            assert actual == tuple(expected), f'{name} mode{mode}: lifetime/replay/overlap mismatch'
            assert f'played={3 if mode in (1,3) else 1} ' in run.stdout
            assert 'underruns=0 ' in run.stdout and f'clipped={clipped} ' in run.stdout
            if mode == 2:
                assert not any(actual[3*735*2:]), 'unrefreshed loop remained audible'
            results.append(dict(source=name,mode=mode,exact=True))
    # A predictive one-block loop must carry its decoder history across wraps.
    level = fixture('psx')
    level.samples = [bytes([0x1c,3])+bytes([0x11]*14)]
    data, _, _ = encode(level,psx_encoding='spu')
    encoded = args.out/'predictive-loop.sn64';encoded.write_bytes(data)
    wav_path = args.out/'predictive-loop.wav'
    run = subprocess.run(['rtk',str(args.harness),str(encoded),'--mode2'],
                         env=dict(os.environ,TR64_AUDIO_WAV=str(wav_path)),
                         capture_output=True,text=True,check=True)
    with wave.open(str(wav_path),'rb') as wav:
        actual = struct.unpack('<'+str(90*735*2)+'h',wav.readframes(90*735))
    history, samples = 0, []
    for _ in range((3*735+1)//2):
        history = 1+(history*60+32)//64
        samples.append(history)
    expected = []
    for frame in range(90*735):
        value = master(samples[frame//2]) if frame < 3*735 else 0
        expected.extend((value,value))
    assert actual == tuple(expected), 'SPU history reset at a loop wrap'
    results.append(dict(source='predictive-spu',mode=2,exact=True))
    (args.out/'report.json').write_text(json.dumps(results,indent=2)+'\n')
    print(json.dumps(dict(mode_cases=len(results),refresh_ticks=3,empty_ticks=87,pass_all=True)))


if __name__ == '__main__':
    main()
