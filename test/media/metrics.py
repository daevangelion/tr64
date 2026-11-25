#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Summarize actual production music/SFX telemetry, never infer a pass."""
import argparse
import json
from pathlib import Path
import re


def measure(path):
    text = Path(path).read_text()
    lines = re.findall(r'^music: (.*)$', text, re.M)
    if not lines:
        raise ValueError('missing production music telemetry')
    music = {key: int(value) for key, value in re.findall(r'(\w+)=(-?\d+)', lines[-1])}
    outputs = re.findall(r'^audio-output: (.*)$', text, re.M)
    rate = next((int(m.group(1)) for line in reversed(outputs)
                 if (m := re.search(r'\brate=(\d+)', line))), None)
    if rate is None:
        raise ValueError('missing output DAC rate')
    work = [{key: int(value) for key, value in re.findall(r'(\w+)=(-?\d+)', line)}
            for line in re.findall(r'^scene-work: (.*)$', text, re.M)]
    if not music.get('frames') or music.get('underruns'):
        raise ValueError('music never mixed or underruns were reported')
    return {'music': music, 'output_rate': rate,
            'music_pull_us_per_30hz_interval': music['mix_us'] * rate / music['frames'] / 30,
            'work_records': len(work),
            'bridge_room14_records': sum(row.get('room') == 14 for row in work),
            'mean_audio_stage_us': sum(row.get('audio_us', 0) for row in work) / len(work) if work else None,
            'evidence': str(path)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    result = measure(args.log)
    if args.out:
        args.out.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
