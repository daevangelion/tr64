#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Analysis only: match replacement-track waveforms to original CD-DA.

Never produces cooked media. Correlation is a candidate locator, not proof of
an entire track or permission to use the replacement as game content.
Requires NumPy/SciPy on the analysis host. Reports a known-original control.
"""
import argparse
import json
from pathlib import Path
import subprocess
import numpy as np
from scipy.signal import correlate
from n64_music_source import SECTOR, cue_tracks

RATE = 2000


def pcm(args):
    raw = subprocess.check_output(['ffmpeg', '-v', 'error', *args, '-map', '0:a:0',
                                   '-ac', '1', '-ar', str(RATE), '-f', 'f32le', '-'])
    return np.frombuffer(raw, dtype='<f4').astype(np.float64)


def best_match(source, needle):
    needle = needle - needle.mean()
    energy = float(needle @ needle)
    if energy < 1e-8 or len(source) < len(needle):
        return None
    length = len(needle)
    sums = np.concatenate(([0.0], np.cumsum(source)))
    squares = np.concatenate(([0.0], np.cumsum(source * source)))
    window_energy = squares[length:] - squares[:-length] - (sums[length:] - sums[:-length]) ** 2 / length
    numerator = correlate(source, needle, mode='valid', method='fft')
    denom = np.sqrt(np.maximum(window_energy, 1e-12) * energy)
    similarity = numerator / denom
    index = int(np.argmax(similarity))
    return {'correlation': float(similarity[index]), 'original_seconds': index / RATE}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cue', required=True, type=Path)
    parser.add_argument('--analysis-ogg', required=True, type=Path)
    parser.add_argument('--ids', nargs='+', type=int, default=[5, 57, 58, 59])
    args = parser.parse_args()
    binary, tracks = cue_tracks(args.cue)
    originals = {}
    for track in tracks:
        if track['mode'] != 'AUDIO':
            continue
        originals[track['id']] = pcm(['-f', 's16le', '-ar', '44100', '-ac', '2',
                                      '-skip_initial_bytes', str(track['start_sector'] * SECTOR),
                                      '-i', str(binary), '-t', str(track['seconds'])])
    results = []
    for track_id in args.ids:
        candidate = pcm(['-i', str(args.analysis_ogg / f'{track_id:02}.ogg')])
        for seconds in [10, len(candidate) / RATE / 2, len(candidate) / RATE - 15]:
            offset = int(seconds * RATE)
            needle = candidate[offset:offset + RATE * 5]
            matches = []
            for original_id, source in originals.items():
                match = best_match(source, needle)
                if match:
                    matches.append({'track': original_id, **match})
            matches.sort(key=lambda m: m['correlation'], reverse=True)
            results.append({'analysis_id': track_id, 'analysis_seconds': seconds,
                            'snippet_seconds': len(needle) / RATE, 'matches': matches[:3]})
    print(json.dumps(results, indent=2))


if __name__ == '__main__':
    main()
