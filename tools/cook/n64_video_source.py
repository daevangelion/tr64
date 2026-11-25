#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Measure retail STR sectors and optionally extract a raw stream for FFmpeg.

Layout facts: psx-spx CDROM XA subheaders and MDEC STR chunk headers.
The ISO9660 length counts 2048-byte logical sectors, not XA audio payloads.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import struct
from n64_music_source import SECTOR, cue_tracks, iso_files


def survey(cue, extract=None, output=None):
    binary, tracks = cue_tracks(cue)
    result = []
    with binary.open('rb') as source:
        for entry in iso_files(cue):
            if not entry['path'].endswith(('.FMV', '.STR')):
                continue
            sectors = (entry['iso_bytes'] + 2047) // 2048
            if entry['sector'] + sectors > tracks[0]['sectors']:
                raise ValueError('video outside data track')
            source.seek(entry['sector'] * SECTOR)
            chunks = Counter()
            frames = {}
            xa = Counter()
            dimensions = set()
            dest = None
            if extract == Path(entry['path']).name:
                if output is None:
                    raise ValueError('video extraction needs --output')
                dest = Path(output).open('wb')
            try:
                for i in range(sectors):
                    raw = source.read(SECTOR)
                    if len(raw) != SECTOR or raw[15] != 2 or raw[16:20] != raw[20:24]:
                        raise ValueError('invalid video sector')
                    if dest:
                        dest.write(raw)
                    file_no, channel, submode, coding = raw[16:20]
                    if submode & 4:
                        xa[(file_no, channel, coding)] += 1
                    if raw[24:28] != b'\x60\x01\x01\x80':
                        continue
                    _, chunk, count, frame, size, width, height = struct.unpack_from('<IHHIIHH', raw, 24)
                    if not count or chunk >= count or not width or not height:
                        raise ValueError('invalid STR chunk')
                    key = (frame, chunk)
                    if chunks[key]:
                        raise ValueError('duplicate STR chunk')
                    chunks[key] += 1
                    frame_meta = (count, size, width, height)
                    if frame in frames and frames[frame] != frame_meta:
                        raise ValueError('inconsistent STR frame header')
                    frames[frame] = frame_meta
                    dimensions.add((width, height))
            finally:
                if dest:
                    dest.close()
            for frame, (count, _, _, _) in frames.items():
                if any((frame, chunk) not in chunks for chunk in range(count)):
                    raise ValueError('incomplete STR frame')
            if not frames:
                raise ValueError('video contains no STR frames')
            audio = []
            for (file_no, channel, coding), count in sorted(xa.items()):
                audio.append({'file': file_no, 'channel': channel, 'coding': coding,
                              'sectors': count, 'channels': 2 if coding & 1 else 1,
                              'rate': 18900 if coding & 4 else 37800,
                              'bits': 8 if coding & 16 else 4,
                              'decoded_seconds': count * 18 * 28 *
                              (4 if coding & 16 else 8) /
                              (2 if coding & 1 else 1) /
                              (18900 if coding & 4 else 37800)})
            result.append({**entry, 'raw_bytes': sectors * SECTOR,
                           'read_seconds_at_150_sectors_per_second': sectors / 150,
                           'frames': len(frames),
                           'first_frame': min(frames), 'last_frame': max(frames),
                           'dimensions': sorted(dimensions), 'xa_audio': audio})
    if extract and not any(Path(e['path']).name == extract for e in result):
        raise ValueError('requested video not present')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cue', required=True, type=Path)
    parser.add_argument('--extract', metavar='NAME.FMV')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    print(json.dumps(survey(args.cue, args.extract, args.output), indent=2))


if __name__ == '__main__':
    main()
