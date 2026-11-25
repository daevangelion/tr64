#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Video-only TR6P pack from a cooked video catalog (player scenes and tests).

The campaign pack takes the same catalog through pack_level.py --video-catalog.
"""
import argparse
import json
from pathlib import Path
import n64_pack as pack
import n64_video


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--catalog', type=Path, required=True, help='immutable manifest.json')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    entries = n64_video.read_video_catalog(args.catalog)
    blob = pack.build(entries)
    decoded = pack.inspect(blob)
    if any(decoded[name][s.kind] != s.data for name, rows in entries.items() for s in rows):
        raise ValueError('video pack independent decode mismatch')
    args.out.write_bytes(blob)
    print(json.dumps({'pack': str(args.out), 'bytes': len(blob), 'entries': len(entries)}))


if __name__ == '__main__':
    main()
