#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Measure H.264 payloads from original RPLs in external scratch.

No shipping claim: these FFmpeg streams test baseline-compatible settings;
final playback uses the pinned SDK converter and checks its slice limits.
Source 320x120 RPL is line-doubled to a4:3 display, not cropped or stretched
as a2.67:1 movie. Sound is counted separately, never silently omitted.
"""
import argparse
import json
from pathlib import Path
import subprocess
from n64_music_source import probe


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rpl', type=Path, required=True)
    parser.add_argument('--scratch', type=Path, required=True)
    parser.add_argument('--crf', type=int, default=32)
    parser.add_argument('--widths', nargs='+', type=int, default=[320, 256, 160])
    args = parser.parse_args()
    args.scratch.mkdir(parents=True, exist_ok=True)
    result = []
    for source in sorted(args.rpl.glob('*.rpl')):
        info = probe(source)
        video = next(s for s in info['streams'] if s['codec_type'] == 'video')
        seconds = float(video['duration'])
        for width in args.widths:
            height = width * 3 // 4
            dest = args.scratch / f'{source.stem}-{width}.h264'
            # Single reference/no B frames bounds DPB memory. No CABAC/8x8 DCT.
            subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(source), '-an',
                '-vf', f'scale={width}:{height}:flags=lanczos,setsar=1,fps=15',
                '-c:v', 'libx264', '-profile:v', 'baseline', '-pix_fmt', 'yuv420p',
                '-preset', 'slow', '-threads', '2', '-crf', str(args.crf), '-refs', '1',
                '-bf', '0', '-g', '45', '-x264-params', 'no-deblock=1:slices=1:weightp=0:cabac=0:8x8dct=0',
                '-f', 'h264', str(dest)], check=True)
            encoded_info = probe(dest)
            row = {'source': source.name, 'width': width, 'height': height, 'fps': 15,
                   'crf': args.crf, 'source_header_seconds': seconds,
                   'video_bytes': dest.stat().st_size,
                   'video_bitrate_from_header_seconds': dest.stat().st_size * 8 / seconds,
                   'audio_ima16k_mono_projected_bytes': int((seconds * 16000 + 1016) // 1017) * 512,
                   'encoded_stream': encoded_info['streams'][0]}
            # Exact framecount used by PSNR; compares to the same source scaling.
            quality = subprocess.run(['ffmpeg', '-v', 'info', '-i', str(source), '-i', str(dest),
                '-filter_complex', f'[0:v]scale={width}:{height}:flags=lanczos,setsar=1,fps=15[ref];[ref][1:v]psnr',
                '-an', '-f', 'null', '-'], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, check=True)
            row['psnr_summary'] = next((line for line in reversed(quality.stderr.splitlines()) if 'PSNR y:' in line), None)
            result.append(row)
    if not result:
        raise ValueError('no source RPLs')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
