#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Original CD-DA -> content-addressed MU64 tracks; no replacement audio.

A PC CD requires an explicit request-id to CD-track mapping JSON. The supplied
PC data directory alone is not a music source. Results are external game data.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import n64_pack as pack
from n64_music_source import SECTOR, cue_tracks

FILM_TRACK = 60  # film sound of a video (n64-video.md): not a game music request
TRACKS = (*range(2, 60), FILM_TRACK)

KIND = 0x60000
HEADER = struct.Struct('>4s15I')
SPU = (1, 16, 28)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def file_digest(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as source:
        for data in iter(lambda: source.read(1024 * 1024), b''):
            value.update(data)
    return value.hexdigest()




def validate(data, track):
    if len(data) < 64:
        raise ValueError('short MU64')
    magic, version, total, flags, actual, codec, rate, channels, frames, start, size, ub, uf, cb, cbsize, reserved = HEADER.unpack_from(data)
    if magic != b'MU64' or version != 1 or total != len(data) or actual != track or track not in TRACKS or reserved:
        raise ValueError('invalid MU64 identity/extent')
    if flags == 1:
        if len(data) != 64 or any(HEADER.unpack_from(data)[5:]):
            raise ValueError('unavailable track has payload')
        return {'available': False, 'codec': 0, 'rate': 0, 'frames': 0, 'metadata_bytes': 64}
    if flags or channels != 1 or rate not in (16000, 22050) or not frames:
        raise ValueError('invalid MU64 flags/rate/channels/frames')
    if codec != SPU[0] or (ub, uf) != SPU[1:] or size != ((frames - 1) // uf + 1) * ub:
        raise ValueError('invalid MU64 codec framing')
    if (start, cb, cbsize) != (64, 0, 0):
        raise ValueError('unexpected MU64 codebook')
    if start + size != len(data):
        raise ValueError('invalid MU64 data extent')
    payload = data[start:]
    for offset in range(0, len(payload), ub):
        block = payload[offset:offset + ub]
        if codec == 1 and (block[0] >> 4 > 4 or block[0] & 15 > 12 or block[1]):
            raise ValueError('invalid SPU block')
    return {'available': True, 'codec': codec, 'rate': rate, 'frames': frames, 'metadata_bytes': start}


def section(data, track):
    view = validate(data, track)
    prefix = view['metadata_bytes']
    cuts = tuple(range(prefix, len(data), 4096)) if len(data) > prefix else ()
    return pack.Section(KIND, data, header_bytes=prefix, chunk_bytes=4096,
                        compression='none', boundaries=cuts)


def encode(pcm, rate, track, spu_encoder):
    if not pcm or len(pcm) % 2 or rate not in (16000, 22050) or track not in TRACKS:
        raise ValueError('music PCM extent/rate/id')
    code, unit_bytes, unit_frames = SPU
    payload = subprocess.check_output([str(spu_encoder)], input=pcm)
    data = HEADER.pack(b'MU64', 1, 64 + len(payload), 0, track, code, rate, 1,
                       len(pcm) // 2, 64, len(payload), unit_bytes, unit_frames, 0, 0, 0) + payload
    validate(data, track)
    return data


def unavailable(track):
    return HEADER.pack(b'MU64', 1, 64, 1, track, *([0] * 11))


def read_music_catalog(path):
    """Strict manifest FILE -> named Sections for the existing pack builder."""
    path = Path(path)
    manifest = json.loads(path.read_text())
    if manifest.get('version') != 1 or not isinstance(manifest.get('tracks'), list):
        raise ValueError('invalid music catalog version/tracks')
    result = {}
    for row in manifest['tracks']:
        track = row['requested_track']
        name = f'MUSIC_{track:02}'
        if not 2 <= track <= 57 or row['name'] != name or row['file'] != name + '.bin' or name in result:
            raise ValueError('music catalog identity/duplicate')
        data = (path.parent / row['file']).read_bytes()
        if len(data) != row['section_bytes'] or digest(data) != row['sha256']:
            raise ValueError('music catalog payload hash')
        result[name] = [section(data, track)]
    if not result:
        raise ValueError('empty music catalog')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, choices=['pc', 'psx'])
    parser.add_argument('--cue', required=True, type=Path)
    parser.add_argument('--mapping', type=Path, help='request-id -> original CD track or null (required for PC)')
    parser.add_argument('--rate', type=int, choices=[16000, 22050], default=22050)
    parser.add_argument('--tracks', nargs='+', type=int, default=list(range(2, 58)))
    parser.add_argument('--spu-encoder', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if args.source == 'pc' and not args.mapping:
        parser.error('PC CD music requires --mapping; the data directory has no music')
    mapping = ({int(k): v for k, v in json.loads(args.mapping.read_text()).items()}
               if args.mapping else {n: n for n in range(2, 57)})
    if len(set(args.tracks)) != len(args.tracks) or any(not 2 <= t <= 57 for t in args.tracks):
        parser.error('tracks must be unique request ids2..57')
    binary, tracks = cue_tracks(args.cue)
    originals = {t['id']: t for t in tracks if t['mode'] == 'AUDIO'}
    args.out.mkdir(parents=True, exist_ok=True)
    tool_root = Path(__file__).parent
    tool_bytes = b''.join((tool_root / name).read_bytes() for name in
                          ('n64_music.py', 'n64_music_source.py', 'n64_music_spu.c', 'n64_pack.py'))
    manifest = {'version': 1, 'source_release': args.source, 'cue_sha256': digest(args.cue.read_bytes()),
                'cook_sha256': digest(tool_bytes), 'codec': 'spu', 'rate': args.rate,
                'ffmpeg': subprocess.check_output(['ffmpeg', '-version']).decode().splitlines()[0],
                'spu_encoder_sha256': digest(args.spu_encoder.read_bytes()) if args.spu_encoder else None,
                'unavailable_requests': [n for n in (57, 58, 59) if n not in mapping or mapping[n] is None],
                'tracks': []}
    identity = {k: v for k, v in manifest.items() if k != 'tracks'}
    identity.update(bin_sha256=file_digest(binary),
                    mapping={str(k): v for k, v in mapping.items()},
                    selected_tracks=sorted(args.tracks))
    key = digest(json.dumps(identity, sort_keys=True, separators=(',', ':')).encode())
    root = args.out / key
    if (root / 'manifest.json').exists():
        cached = json.loads((root / 'manifest.json').read_text())
        read_music_catalog(root / 'manifest.json')
        if cached.get('content_key') != key or cached.get('identity') != identity:
            raise ValueError('music cached identity mismatch')
        print(json.dumps({'catalog': str(root / 'manifest.json'), 'content_key': key,
                          'section_bytes': sum(r['section_bytes'] for r in cached['tracks']),
                          'cache_hit': True}))
        return
    manifest['identity'] = identity
    payloads = {}
    for track in sorted(args.tracks):
        original = mapping.get(track)
        if original is None:
            data = unavailable(track)
            source_hash = None
        else:
            if original not in originals:
                raise ValueError(f'request{track} maps to absent CD-DA track{original}')
            row = originals[original]
            with binary.open('rb') as source:
                source.seek(row['start_sector'] * SECTOR)
                raw = source.read(row['sectors'] * SECTOR)
            if len(raw) != row['sectors'] * SECTOR:
                raise ValueError('truncated original CD-DA')
            source_hash = digest(raw)
            pcm = subprocess.check_output(['ffmpeg', '-v', 'error', '-f', 's16le',
                '-ar', '44100', '-ac', '2', '-i', '-', '-ac', '1', '-ar', str(args.rate), '-f', 's16le', '-'], input=raw)
            data = encode(pcm, args.rate, track, args.spu_encoder)
        name = f'MUSIC_{track:02}'
        payloads[name + '.bin'] = data
        manifest['tracks'].append({'requested_track': track, 'source_track': original,
            'source_sha256': source_hash, 'name': name, 'file': name + '.bin',
            'section_bytes': len(data), 'sha256': digest(data), **validate(data, track)})
    root.mkdir(exist_ok=True)
    manifest['content_key'] = key
    payloads['manifest.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    for name, data in payloads.items():
        output = root / name
        if output.exists() and output.read_bytes() != data:
            raise ValueError('music content key collision: ' + str(output))
        if not output.exists():
            output.write_bytes(data)
    print(json.dumps({'catalog': str(root / 'manifest.json'), 'content_key': key,
                      'section_bytes': sum(r['section_bytes'] for r in manifest['tracks'])}))


if __name__ == '__main__':
    main()
