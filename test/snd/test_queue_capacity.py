# SPDX-License-Identifier: MIT
"""Directed TR2 sound-queue overflow equivalence test + negative controls."""
import argparse
import io
import json
import math
import os
import re
import struct
import subprocess
import wave
from pathlib import Path
from ir import Level
from n64_snd import encode


def make_sn64(path):
    """Build a synthetic TR2 SN64 bank with 16 distinct non-zero waveforms."""
    level = Level('pc', 'queue-synthetic', '0' * 64, 0)
    level.reserved = { 'game': 2 }
    sound_map = list(range(16)) + [-1] * (370 - 16)
    details = [dict(sample=i, volume=32767, chance=0, flags=4) for i in range(16)]
    level.data = dict(sound_map=sound_map, sound_details=details, sound_sources=[])
    samples = []
    # Distinct frequencies & phases so any voice/gain/pitch/pan/age mismatch changes PCM.
    for i in range(16):
        freq = 180.0 + i * 73.0
        amp = 9000 + (i % 5) * 2000
        frames = 4096
        pcm = bytearray()
        for n in range(frames):
            val = int(amp * math.sin(2.0 * math.pi * freq * n / 22050.0 + i * 0.37))
            # 8-bit unsigned PC WAVE (keeps SN64 small and fast).
            u8 = max(0, min(255, 128 + (val >> 8)))
            pcm.append(u8)
        buf = io.BytesIO()
        with wave.open(buf, 'wb') as wav:
            wav.setparams((1, 1, 22050, 0, 'NONE', 'not compressed'))
            wav.writeframes(bytes(pcm))
        samples.append(buf.getvalue())
    level.samples = samples
    level.sample_encoding = 'pc_wave'
    data, _, _ = encode(level)
    path.write_bytes(data)


def run_case(harness, sn64, scenario, mode, wav_path):
    env = dict(os.environ, TR64_AUDIO_WAV=str(wav_path))
    res = subprocess.run(['rtk', str(harness), str(sn64), scenario, mode],
                         env=env, capture_output=True, text=True, check=True)
    m = re.search(r'queue-test: scenario=(\S+) mode=(\S+) rng_draw=([0-9a-f]+) rng_logic=([0-9a-f]+) sound_calls=(\d+)',
                  res.stdout)
    assert m, f'missing queue-test summary: {res.stdout}\n{res.stderr}'
    with wave.open(str(wav_path), 'rb') as wav:
        frames = wav.getnframes()
        raw = wav.readframes(frames)
        pcm = struct.unpack(f'<{frames * 2}h', raw)
    return dict(rng_draw=m.group(3), rng_logic=m.group(4), sound_calls=int(m.group(5)),
                frames=frames, pcm=pcm, log=res.stdout)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--harness', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    sn64 = args.out / 'tr2-queue.sn64'
    make_sn64(sn64)

    scenarios = (
        'flames-100',
        'plain-100',
        'mixed-stop-150',
        'full-uniques-plus-plain-70',
        'adv-unique-after-stop-6-plains',
        'adv-plain-after-stop-6-plains',
        'adv-tail-dup-with-5-plains',
        'adv-stop-at-64',
        'mutant-m1-multivariant',
        'mutant-m3-m6-sat-dup-clear-pos',
        'mutant-m5-stop-dedup',
    )
    results = []
    refs = {}
    for sc in scenarios:
        ref = run_case(args.harness, sn64, sc, '--unlimited', args.out / f'{sc}-unlimited.wav')
        prod = run_case(args.harness, sn64, sc, '--prod', args.out / f'{sc}-prod.wav')
        assert any(ref['pcm']), f'{sc}: silent reference WAV'
        assert prod['rng_draw'] == ref['rng_draw'], f'{sc}: rng_draw mismatch'
        assert prod['rng_logic'] == ref['rng_logic'], f'{sc}: rng_logic mismatch'
        assert prod['sound_calls'] == ref['sound_calls'], f'{sc}: sound_calls mismatch'
        assert prod['pcm'] == ref['pcm'], f'{sc}: PCM mismatch vs unlimited reference'
        assert 'underruns=0 ' in prod['log'], f'{sc}: mixer underrun'
        refs[sc] = ref
        results.append(dict(scenario=sc, frames=prod['frames'], sound_calls=prod['sound_calls'],
                            rng_draw=prod['rng_draw'], exact_pcm=True))
        print(f'PASS {sc}: frames={prod["frames"]} calls={prod["sound_calls"]} rng={prod["rng_draw"]} exact_pcm=True')

    # Negative controls: each wrong strategy MUST diverge in audible PCM from unlimited!
    negatives = (
        ('flames-100', '--neg-loudest'),
        ('plain-100', '--neg-drop-newest'),
        ('mixed-stop-150', '--neg-fold-stop'),
        ('adv-unique-after-stop-6-plains', '--neg-drop-stop-64'),
        ('adv-stop-at-64', '--neg-drop-stop-64'),
        ('mutant-m3-m6-sat-dup-clear-pos', '--neg-drop-stop-64'),
    )
    neg_results = []
    for sc, mode in negatives:
        neg = run_case(args.harness, sn64, sc, mode, args.out / f'{sc}-{mode[2:]}.wav')
        diff_samples = sum(1 for a, b in zip(neg['pcm'], refs[sc]['pcm']) if a != b)
        assert diff_samples > 0, f'negative control {sc} {mode} unexpectedly matched unlimited PCM'
        neg_results.append(dict(scenario=sc, control=mode, diff_samples=diff_samples, rejected=True))
        print(f'PASS negative {sc} {mode}: rejected ({diff_samples} differing stereo samples)')

    summary = dict(positive=results, negative=neg_results, pass_all=True)
    (args.out / 'report.json').write_text(json.dumps(summary, indent=2) + '\n')


if __name__ == '__main__':
    main()
