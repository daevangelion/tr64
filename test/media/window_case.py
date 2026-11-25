#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The SDK input-window case on the cart: one stream, two slice-size values.

Builds a pack whose ESCAPE and CORE videos are the same crafted stream from a
cooked catalog's CORE, differing only in the LDSZ value: ESCAPE advertises the
largest NAL plus the start code (the cook's rule), CORE the largest NAL alone
(the old rule). The stream: CORE's pictures unchanged; filler NALs (type 12, which
the SDK decoder ignores) in front of its 41st picture so that the 64 KiB input window
holds that picture and 2 bytes more when the picture begins; that picture padded
with trailing zero bytes (which the SDK's NAL extractor strips) to 5,000 bytes, so
it is the largest NAL. With the old value the window ends 2 bytes into the next start
code; with the new one the refill comes first. The SDK model in test_video_cook.py
predicts the loss; the ROM shows it.

The streams are deliberately outside the cook's checks (filler NALs, a value without
the margin): this tool bypasses `n64_video.validate`, and its pack is an experiment
never shipped. Run the pack with `test/media/video-all.route` (ESCAPE, then CORE).

  PYTHONPATH=tools/cook:test/media python3 test/media/window_case.py \\
    --catalog <cooked>/manifest.json --base <campaign+music pack> --cache <dir> --out <dir>
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import n64_video as cook
import n64_video_h264 as h264
from test_video_cook import model_window

BIG = 5000        # the padded picture: larger than any NAL of CORE (2,225)
PICTURE = 40      # which picture (0-based) is padded
ROOM = 2          # bytes of the window left after it


def filler(size):
    return b'\x00\x00\x00\x01\x0c' + b'\xff' * (size - 6) + b'\x80'


def craft(core):
    """(stream with LDSZ = BIG, stream with LDSZ = BIG + start code), both from CORE's stream."""
    spans = [(s, e, t) for s, e, t in h264.nals(core) if t != 6]
    nal = [core[s:e] for s, e, _ in spans]
    pictures = [i for i, (_, _, t) in enumerate(spans) if t in (1, 5)]
    k = pictures[PICTURE]
    if spans[k][2] != 1 or len(nal[k]) >= BIG:
        raise ValueError('picture to pad is not a smaller P picture')
    nal[k] += bytes(BIG - len(nal[k]))
    before_picture = sum(len(n) for n in nal[:k]) + len(cook.slice_sei(BIG))
    fill = h264.INPUT_WINDOW - (BIG + ROOM) - before_picture
    if fill < 0:
        raise ValueError('picture too late in the stream for the first window')
    fillers = []
    while fill:
        size = min(4000, fill)
        if fill - size and fill - size < 6:
            size -= 6
        fillers.append(filler(size))
        fill -= size

    def build(limit):
        out = bytearray()
        for i, n in enumerate(nal):
            if i == pictures[0]:
                out += cook.slice_sei(limit)
            if i == k:
                out += b''.join(fillers)
            out += n
        return bytes(out)
    before, after = build(BIG), build(BIG + h264.START_CODE_MARGIN)
    if max(e - s for s, e, _ in h264.nals(before)) != BIG:
        raise ValueError('the padded picture is not the largest NAL')
    if model_window(before, BIG)[1] or not model_window(after, BIG + h264.START_CODE_MARGIN)[1]:
        raise ValueError('the SDK model does not show the case')
    return before, after


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--catalog', type=Path, required=True, help='cooked manifest.json (needs CORE and ESCAPE)')
    parser.add_argument('--base', type=Path, required=True)
    parser.add_argument('--cache', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    manifest = json.loads(args.catalog.read_text())
    rows = {r['name']: r for r in manifest['videos']}
    before, after = craft((args.catalog.parent / rows['CORE']['video_file']).read_bytes())
    args.out.mkdir(parents=True, exist_ok=True)
    catalog = args.out / 'catalog'
    catalog.mkdir(exist_ok=True)
    out = []
    for name, stream, limit in (('ESCAPE', after, BIG + h264.START_CODE_MARGIN), ('CORE', before, BIG)):
        film = (args.catalog.parent / rows[name]['film_file']).read_bytes()
        (catalog / f'VIDEO_{name}.bin').write_bytes(stream)
        (catalog / f'FILM_{name}.bin').write_bytes(film)
        out.append({**rows[name], 'video_bytes': len(stream), 'max_slice': limit,
                    'video_sha256': hashlib.sha256(stream).hexdigest()})
    (catalog / 'manifest.json').write_text(json.dumps({'version': 1, 'content_key': 'window-case',
                                                       'identity': {}, 'videos': out}, indent=1) + '\n')
    cook.validate = lambda data: 0   # experiment only
    import pack_add_video
    sys.argv = ['pack_add_video.py', '--base', str(args.base), '--video-catalog', str(catalog / 'manifest.json'),
                '--cache', str(args.cache), '--out', str(args.out / 'window.pack')]
    pack_add_video.main()


if __name__ == '__main__':
    main()
