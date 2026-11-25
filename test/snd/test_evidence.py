# SPDX-License-Identifier: MIT
"""Synthetic positive/negative controls for native audio measurement parsing."""
import argparse
from pathlib import Path
from n64_snd_evidence import scene_measure

TEXT='''audio-output: cart rate=22047 buffers=6 frames_per_buffer=448 arena=98304
game-tick: tick=0 hash=omitted/perf-only tick_us=0
game-tick: tick=1 hash=omitted/perf-only tick_us=10
game-render: room=14
scene-work: tick=1 total_us=100 wall_us=180 diagnostics_us=20 pacing_us=60 game_us=10 audio_us=5
audio: ticks=1 events=6 played=6 dropped=0 unavailable=0 underruns=0 clipped=0 frames=448
audio-output: cart rate=22047 frames=448 underruns=0 pumps=1 mixer_total_us=110 mixer_peak_pump_us=110
audio-buffer: voices=6 buffers=1 mix_total_us=100 copy_total_us=10 mix_peak_us=100 copy_peak_us=10
'''


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True)
    out=parser.parse_args().out;out.mkdir(parents=True,exist_ok=True)
    path=out/'positive.log';path.write_text(TEXT)
    result=scene_measure(path)
    assert result['perf_only'] and result['ticks']==1
    assert result['work']['mean']==100 and result['voice_profiles'][6]['mean_copy_us']==10
    negatives=[TEXT.replace('total_us=100 wall','total_us=101 wall'),
               TEXT.replace('dropped=0','dropped=1'),TEXT.replace('underruns=0','underruns=1'),
               TEXT.replace('clipped=0','clipped=1'),TEXT.replace('audio: ticks=1','audio: ticks=2'),
               TEXT.replace('voices=6 buffers=1','voices=6 buffers=2'),
               TEXT.replace('game-tick: tick=0 hash=omitted/perf-only tick_us=0\n',''),
               TEXT.replace('scene-work: tick=1','not-a-work-record: tick=1')]
    for i,text in enumerate(negatives):
        path=out/f'negative-{i}.log';path.write_text(text)
        try:
            scene_measure(path)
        except (ValueError,KeyError):
            continue
        raise AssertionError(f'bad measurement accepted: {i}')
    print(f'snd evidence PASS: positive +{len(negatives)} rejection controls')


if __name__=='__main__':
    main()
