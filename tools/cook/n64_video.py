#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Original FMV -> content-addressed VIDEO_<NAME> H.264 + FILM_<NAME> MU64 streams.

PC: the eleven *.rpl of the release's fmv directory. PlayStation: the disc's
STR files (complete 2352-byte sectors extracted by n64_video_source.py).
Encoding follows the SDK's own videoconv64 constraints (Main profile, CABAC and
8x8 transform off, deblocking off, one slice per picture, B-frames off, BT.709
full range, the LDSZ max-slice SEI the player needs). Format: docs/formats/n64-video.md.
Results are external game data and never committed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import n64_music
import n64_pack as pack
import n64_video_h264 as h264
from n64_video_h264 import nals, START_CODE_MARGIN
from n64_music_source import SECTOR, cue_tracks, iso_files

KIND = 0x70000
FILM_KIND = n64_music.KIND
FILM_TRACK = n64_music.FILM_TRACK
FILM_RATE = 16000
WIDTH, HEIGHT, FPS = 320, 240, 15
BLOCK_BYTES = 16384
MAX_SLICE = h264.INPUT_WINDOW


def extract_str(cue, entry, destination):
    """Copy a disc file's raw 2352-byte sectors (STR with its XA audio) for FFmpeg."""
    binary, _ = cue_tracks(cue)
    sectors = (entry['iso_bytes'] + 2047) // 2048
    with binary.open('rb') as source, destination.open('wb') as out:
        source.seek(entry['sector'] * SECTOR)
        data = source.read(sectors * SECTOR)
        if len(data) != sectors * SECTOR:
            raise ValueError('truncated STR extraction')
        out.write(data)


# Release file stem -> native video name (PlayStation names differ for the logo).
PC_NAMES = {'cafe': 'CAFE', 'canyon': 'CANYON', 'core': 'CORE', 'end': 'END', 'escape': 'ESCAPE',
            'lift': 'LIFT', 'mansion': 'MANSION', 'prison': 'PRISON', 'pyramid': 'PYRAMID',
            'snow': 'SNOW', 'vision': 'VISION'}
PSX_NAMES = {'CAFE': 'CAFE', 'CANYON': 'CANYON', 'CORELOGO': 'CORE', 'END': 'END', 'LIFT': 'LIFT',
             'MANSION': 'MANSION', 'PRISON': 'PRISON', 'PYRAMID': 'PYRAMID', 'SNOW': 'SNOW',
             'VISION': 'VISION'}
X264 = 'no-deblock=1:no-info=1:slices=1:weightp=1:cabac=0:8x8dct=0'
LD_UUID = b'LIBDRAGON' + bytes(7)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def escaped(rbsp):
    out = bytearray()
    zeros = 0
    for b in rbsp:
        if zeros == 2 and b <= 3:
            out.append(3)
            zeros = 0
        out.append(b)
        zeros = zeros + 1 if b == 0 else 0
    return bytes(out)


def slice_sei(max_slice):
    payload = LD_UUID + b'LDSZ' + bytes([1]) + struct.pack('>I', max_slice)
    return b'\x00\x00\x00\x01\x06' + escaped(bytes([5, len(payload)]) + payload + b'\x80')


def embed_slice_metadata(data):
    """The SDK decoder refills its 64 KiB window by this bound; find it, embed it."""
    spans = list(nals(data))
    if not spans or spans[0][2] != 7:
        raise ValueError('H.264 stream does not start with a sequence parameter set')
    # x264's version SEI would cost nothing, but is not part of the format.
    keep = [(s, e, t) for s, e, t in spans if t != 6]
    first = next((n for n, (_, _, t) in enumerate(keep) if t in (1, 5)), None)
    if first is None:
        raise ValueError('H.264 stream has no picture')
    # The largest NAL plus the next start code (n64_video_h264.START_CODE_MARGIN): the SDK
    # refills its window when `left <= this`, and a window that ends inside a start code
    # loses the stream up to 64 KiB.
    biggest = max(e - s for s, e, _ in keep) + START_CODE_MARGIN
    if biggest > MAX_SLICE:
        raise ValueError(f'H.264 NAL of {biggest - START_CODE_MARGIN} bytes plus its start code '
                         f'exceeds the {MAX_SLICE} byte player window')
    out = bytearray()
    for n, (s, e, _) in enumerate(keep):
        if n == first:
            out += slice_sei(biggest)
        out += data[s:e]
    return bytes(out), biggest


def validate(data):
    """Everything the cart player assumes, read from the stream itself
    (n64_video_h264.check); returns the stream's picture count."""
    return h264.check(data)['pictures']


def encode_video(source, destination):
    # Source lines are doubled/stretched to 4:3 here; the decoder outputs 320x240.
    vf = (f'scale={WIDTH}:{HEIGHT}:flags=lanczos:out_color_matrix=bt709:out_range=pc,'
          f'fps={FPS}:eof_action=pass,format=yuv420p')
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(source), '-an', '-vf', vf,
                    '-c:v', 'libx264', '-profile:v', 'main', '-pix_fmt', 'yuv420p',
                    '-colorspace', 'bt709', '-color_range', 'pc', '-color_primaries', 'bt709',
                    '-color_trc', 'bt709', '-crf', str(encode_video.crf),
                    '-maxrate', '480k', '-bufsize', '240k', '-bf', '0', '-refs', '1', '-preset', 'slower',
                    '-x264-params', X264, '-f', 'h264', str(destination)], check=True)


encode_video.crf = 32


def encode_audio(source, spu_encoder):
    pcm = subprocess.check_output(['ffmpeg', '-v', 'error', '-i', str(source), '-vn', '-ac', '1',
                                   '-ar', str(FILM_RATE), '-f', 's16le', '-'])
    if not pcm:
        raise ValueError(f'{source}: no audio')
    return n64_music.encode(pcm, FILM_RATE, FILM_TRACK, spu_encoder), len(pcm) // 2


def cook_one(source, name, spu_encoder, scratch):
    video = scratch / f'{name}.h264'
    encode_video(source, video)
    stream, biggest = embed_slice_metadata(video.read_bytes())
    pictures = validate(stream)
    audio, audio_frames = encode_audio(source, spu_encoder)
    return stream, audio, {'name': name, 'pictures': pictures, 'max_slice': biggest,
                           'video_bytes': len(stream), 'audio_frames': audio_frames,
                           'audio_bytes': len(audio), 'audio_rate': FILM_RATE,
                           'video_seconds': pictures / FPS, 'audio_seconds': audio_frames / FILM_RATE}


def video_section(data):
    validate(data)
    return pack.Section(KIND, data, chunk_bytes=BLOCK_BYTES, compression='none')


def film_section(data):
    return n64_music.section(data, FILM_TRACK)


def read_video_catalog(path):
    """Strict manifest FILE -> named Sections for the pack builder."""
    path = Path(path)
    manifest = json.loads(path.read_text())
    if manifest.get('version') != 1 or not isinstance(manifest.get('videos'), list):
        raise ValueError('invalid video catalog version/videos')
    result = {}
    for row in manifest['videos']:
        name = row['name']
        if name not in set(PC_NAMES.values()) or row['video_file'] != f'VIDEO_{name}.bin' \
                or row['film_file'] != f'FILM_{name}.bin' or f'VIDEO_{name}' in result:
            raise ValueError('video catalog identity/duplicate')
        for key, kind in (('video', 'VIDEO_'), ('film', 'FILM_')):
            data = (path.parent / row[key + '_file']).read_bytes()
            if len(data) != row[key + '_bytes'] or digest(data) != row[key + '_sha256']:
                raise ValueError('video catalog payload hash')
            result[kind + name] = [video_section(data) if key == 'video' else film_section(data)]
    if not result:
        raise ValueError('empty video catalog')
    return result


TOOL_FILES = ('n64_video.py', 'n64_video_h264.py', 'n64_video_source.py', 'n64_music.py',
              'n64_music_spu.c', 'n64_pack.py')


def cook_sha():
    root = Path(__file__).parent
    return digest(b''.join((root / n).read_bytes() for n in TOOL_FILES))


def content_key(identity):
    return digest(json.dumps(identity, sort_keys=True, separators=(',', ':')).encode())


def rederive_catalog(manifest, out):
    """A catalog made by an earlier version of this tool, finished by this one without its
    sources. The video streams differ from a fresh cook only in what `embed_slice_metadata`
    and `validate` decide (it drops every SEI of the stream and writes its own, so applying it
    to an earlier cook's output gives exactly what it would write for the encoder's output); the
    film sound and every other field are carried over. The identity keeps the encoder's
    (source, ffmpeg, x264 and SPU encoder digests) with this tool's `cook_sha256`; the old
    content key is recorded beside it, outside the identity."""
    manifest = Path(manifest)
    old = json.loads(manifest.read_text())
    if old.get('version') != 1 or not isinstance(old.get('videos'), list):
        raise ValueError('invalid video catalog version/videos')
    identity = {**old['identity'], 'cook_sha256': cook_sha()}
    key = content_key(identity)
    root = Path(out) / key
    payloads, rows = {}, []
    for row in old['videos']:
        name = row['name']
        data = (manifest.parent / row['video_file']).read_bytes()
        film = (manifest.parent / row['film_file']).read_bytes()
        if digest(data) != row['video_sha256'] or digest(film) != row['film_sha256']:
            raise ValueError(f'{name}: catalog payload hash')
        stream, biggest = embed_slice_metadata(data)
        summary = h264.check(stream)
        if summary['pictures'] != row['pictures'] or summary['max_slice'] != biggest:
            raise ValueError(f'{name}: picture count or slice size changed')
        payloads[row['video_file']] = stream
        payloads[row['film_file']] = film
        rows.append({**row, 'max_slice': biggest, 'video_bytes': len(stream), 'video_sha256': digest(stream)})
    payload = {'version': 1, 'content_key': key, 'derived_from': old['content_key'],
               'identity': identity, 'videos': rows}
    payloads['manifest.json'] = (json.dumps(payload, indent=2) + '\n').encode()
    root.mkdir(parents=True, exist_ok=True)
    for file, data in payloads.items():
        (root / file).write_bytes(data)
    read_video_catalog(root / 'manifest.json')
    return {'catalog': str(root / 'manifest.json'), 'content_key': key, 'derived_from': old['content_key'],
            'video_bytes': sum(r['video_bytes'] for r in rows), 'film_bytes': sum(r['film_bytes'] for r in rows)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', choices=['pc', 'psx'])
    parser.add_argument('--input', type=Path, help='PC: directory of *.rpl')
    parser.add_argument('--cue', type=Path, help='PlayStation: single-file BIN/CUE')
    parser.add_argument('--names', nargs='+', help='native names to cook (default: all present)')
    parser.add_argument('--crf', type=int, default=32)
    parser.add_argument('--spu-encoder', type=Path)
    parser.add_argument('--from-catalog', type=Path, metavar='MANIFEST',
                        help='re-derive a catalog cooked by an earlier version of this tool without its sources: '
                             'the same stream finishing (SEI, checks), the same film sound')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if args.from_catalog:
        print(json.dumps(rederive_catalog(args.from_catalog, args.out)))
        return
    if not args.source or not args.spu_encoder:
        parser.error('--source and --spu-encoder are required to cook')
    encode_video.crf = args.crf
    sources = {}
    if args.source == 'pc':
        if not args.input:
            parser.error('PC release needs --input DIR of *.rpl')
        for path in sorted(args.input.glob('*.rpl')):
            if path.stem in PC_NAMES:
                sources[PC_NAMES[path.stem]] = path
    else:
        if not args.cue:
            parser.error('PlayStation release needs --cue')
        listing = {Path(r['path']).stem: r for r in iso_files(args.cue) if r['path'].endswith('.FMV')}
        for stem, row in listing.items():
            if stem in PSX_NAMES:
                sources[PSX_NAMES[stem]] = row
    if args.names:
        sources = {n: sources[n] for n in args.names}
    if not sources:
        raise ValueError('no videos found')
    source_digest = {}
    args.out.mkdir(parents=True, exist_ok=True)
    scratch = Path(tempfile.mkdtemp(prefix='n64video-', dir=args.out))
    try:
        files = {}
        for name, source in sorted(sources.items()):
            if args.source == 'psx':
                extracted = scratch / f'{name}.str'
                extract_str(args.cue, source, extracted)
                source = extracted
            files[name] = source
            h = hashlib.sha256()
            with source.open('rb') as f:
                for chunk in iter(lambda: f.read(1 << 20), b''):
                    h.update(chunk)
            source_digest[name] = h.hexdigest()
        identity = {'source_release': args.source, 'crf': args.crf, 'x264': X264, 'fps': FPS,
                    'size': [WIDTH, HEIGHT], 'film_rate': FILM_RATE, 'block_bytes': BLOCK_BYTES,
                    'cook_sha256': cook_sha(), 'source_sha256': source_digest,
                    'spu_encoder_sha256': digest(args.spu_encoder.read_bytes()),
                    'ffmpeg': subprocess.check_output(['ffmpeg', '-version']).decode().splitlines()[0]}
        key = content_key(identity)
        root = args.out / key
        if (root / 'manifest.json').exists():
            read_video_catalog(root / 'manifest.json')
            print(json.dumps({'catalog': str(root / 'manifest.json'), 'content_key': key, 'cache_hit': True}))
            return
        payloads, rows = {}, []
        for name, source in sorted(files.items()):
            stream, film, info = cook_one(source, name, args.spu_encoder, scratch)
            payloads[f'VIDEO_{name}.bin'] = stream
            payloads[f'FILM_{name}.bin'] = film
            rows.append({**info, 'video_file': f'VIDEO_{name}.bin', 'film_file': f'FILM_{name}.bin',
                         'video_sha256': digest(stream), 'film_sha256': digest(film),
                         'film_bytes': len(film), 'source_sha256': source_digest[name]})
        manifest = {'version': 1, 'content_key': key, 'identity': identity, 'videos': rows}
        payloads['manifest.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
        root.mkdir(parents=True, exist_ok=True)
        for name, data in payloads.items():
            (root / name).write_bytes(data)
        read_video_catalog(root / 'manifest.json')
        print(json.dumps({'catalog': str(root / 'manifest.json'), 'content_key': key,
                          'video_bytes': sum(r['video_bytes'] for r in rows),
                          'film_bytes': sum(r['film_bytes'] for r in rows)}))
    finally:
        for p in scratch.iterdir():
            p.unlink()
        scratch.rmdir()


if __name__ == '__main__':
    main()
