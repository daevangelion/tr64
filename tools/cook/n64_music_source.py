#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Inventory/extract original CD-DA without assuming a replacement soundtrack.

CUE positions are raw-sector positions, not ISO offsets. ISO9660 directories
are read as layout facts only; no retail executable is opened or interpreted.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import wave

SECTOR = 2352


def frames(msf):
    m, s, f = map(int, msf.split(':'))
    if s >= 60 or f >= 75:
        raise ValueError('invalid CUE MSF')
    return (m * 60 + s) * 75 + f


def cue_tracks(path):
    path = Path(path)
    tracks = []
    binary = None
    current = None
    for line in path.read_text().splitlines():
        line = line.strip()
        m = re.fullmatch(r'FILE "([^"]+)" BINARY', line)
        if m:
            if binary is not None:
                raise ValueError('a single-file BIN/CUE is required')
            binary = path.parent / m[1]
        m = re.fullmatch(r'TRACK (\d+) (\S+)', line)
        if m:
            current = {'id': int(m[1]), 'mode': m[2], 'indices': {}}
            tracks.append(current)
        m = re.fullmatch(r'INDEX (\d+) (\d+:\d+:\d+)', line)
        if m:
            if current is None or int(m[1]) in current['indices']:
                raise ValueError('invalid or duplicate CUE index')
            current['indices'][int(m[1])] = frames(m[2])
        if line.startswith(('PREGAP ', 'POSTGAP ')):
            raise ValueError('synthetic CUE gaps unsupported; use indexed BIN')
    if binary is None or not tracks or binary.stat().st_size % SECTOR:
        raise ValueError('invalid raw BIN size or missing CUE tracks')
    sectors = binary.stat().st_size // SECTOR
    previous = -1
    for i, track in enumerate(tracks):
        start = track['indices'].get(1)
        stop = min(tracks[i + 1]['indices'].values()) if i + 1 < len(tracks) else sectors
        if start is None or not previous < start < stop <= sectors:
            raise ValueError('invalid CUE track span')
        previous = start
        track['start_sector'] = start
        track['sectors'] = stop - start
        if track['mode'] == 'AUDIO':
            track.update(rate=44100, channels=2, bits=16,
                         frames=track['sectors'] * 588,
                         seconds=track['sectors'] / 75)
    if len({t['id'] for t in tracks}) != len(tracks):
        raise ValueError('duplicate track id')
    return binary, tracks


def extract_cdda(cue, track_id, output):
    binary, tracks = cue_tracks(cue)
    track = next((t for t in tracks if t['id'] == track_id), None)
    if track is None or track['mode'] != 'AUDIO':
        raise ValueError('requested CD-DA track absent')
    digest = hashlib.sha256()
    with binary.open('rb') as source, wave.open(str(output), 'wb') as dest:
        dest.setparams((2, 2, 44100, 0, 'NONE', 'not compressed'))
        source.seek(track['start_sector'] * SECTOR)
        left = track['sectors'] * SECTOR
        while left:
            data = source.read(min(left, SECTOR * 75))
            if not data:
                raise ValueError('truncated CD-DA track')
            digest.update(data)
            dest.writeframesraw(data)
            left -= len(data)
    return {**track, 'source_sha256': digest.hexdigest()}


def iso_files(cue):
    binary, tracks = cue_tracks(cue)
    if tracks[0]['mode'] != 'MODE2/2352' or tracks[0]['start_sector'] != 0:
        raise ValueError('expected first MODE2/2352 data track at sector zero')
    limit = tracks[0]['sectors']
    with binary.open('rb') as source:
        def sector(lba):
            if not 0 <= lba < limit:
                raise ValueError('ISO extent outside data track')
            source.seek(lba * SECTOR)
            raw = source.read(SECTOR)
            if len(raw) != SECTOR or raw[15] != 2 or raw[16:20] != raw[20:24]:
                raise ValueError('invalid MODE2 sector')
            return raw
        pvd = sector(16)[24:2072]
        if pvd[:7] != b'\x01CD001\x01':
            raise ValueError('missing ISO primary volume descriptor')
        visited = set()
        result = []
        def directory(lba, size, parent):
            if lba in visited:
                raise ValueError('cyclic ISO directory')
            visited.add(lba)
            raw = b''.join(sector(lba + i)[24:2072] for i in range((size + 2047) // 2048))[:size]
            offset = 0
            while offset < size:
                length = raw[offset]
                if not length:
                    offset = (offset // 2048 + 1) * 2048
                    continue
                if length < 34 or offset + length > size:
                    raise ValueError('invalid ISO directory record')
                record = raw[offset:offset + length]
                extent, count = struct.unpack_from('<I', record, 2)[0], struct.unpack_from('<I', record, 10)[0]
                name = record[33:33 + record[32]]
                offset += length
                if name in (b'\x00', b'\x01'):
                    continue
                name = name.decode('ascii').split(';')[0]
                full = parent + '/' + name
                if record[25] & 2:
                    directory(extent, count, full)
                else:
                    result.append({'path': full, 'sector': extent, 'iso_bytes': count})
        directory(struct.unpack_from('<I', pvd, 158)[0], struct.unpack_from('<I', pvd, 166)[0], '')
    return result


def probe(path):
    return json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-show_format',
                                              '-show_streams', '-of', 'json', str(path)]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cue', type=Path)
    parser.add_argument('--iso', action='store_true')
    parser.add_argument('--extract', type=int, metavar='CDDA_TRACK')
    parser.add_argument('--wav', type=Path)
    parser.add_argument('--ogg', type=Path)
    parser.add_argument('--rpl', type=Path)
    args = parser.parse_args()
    result = {}
    if args.cue:
        binary, tracks = cue_tracks(args.cue)
        result['disc'] = {'bin': str(binary), 'tracks': tracks}
        if args.iso:
            result['disc']['files'] = iso_files(args.cue)
        if args.extract is not None:
            if not args.wav:
                parser.error('--extract requires --wav')
            result['extracted'] = extract_cdda(args.cue, args.extract, args.wav)
    for label, directory, suffix in [('ogg', args.ogg, '*.ogg'), ('rpl', args.rpl, '*.rpl')]:
        if directory:
            result[label] = {p.name: probe(p) for p in sorted(directory.glob(suffix))}
            if not result[label]:
                raise ValueError('no input media in ' + str(directory))
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
