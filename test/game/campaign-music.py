#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compose the shipping pack: the 21-level campaign pack plus the 55 raw music tracks.

Inputs are two immutable packs: --campaign (campaign-pack.py output with its
manifest: TITLE, sixteen gameplay levels, CUT1..CUT4, each with its SN64 effects
bank) and --music-pack (an earlier composed pack whose MUSIC_nn entries are the
validated SPU-ADPCM tracks). Every campaign block and every music block is
re-emitted with its own wire alignment, compression and block boundaries, then
the pack is rebuilt and each section is compared against its producer. The
music tracks are not re-encoded.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
sys.dont_write_bytecode = True
import n64_music
import n64_pack as pack
import n64_snd
from n64_snd_view import Section as SoundView

CART_LIMIT = 64 * 1024 * 1024
VIDEO_RESERVE = 16 * 1024 * 1024


def sha(data):
    return hashlib.sha256(data).hexdigest()


def entries_of(blob, names=None):
    """Recover each level's sections with their wire alignment/compression/boundaries."""
    decoded = pack.inspect(blob)
    out = {}
    count = struct.unpack_from('>I', blob, 12)[0]
    for i in range(count):
        name, offset, size, _, _ = pack.LEVEL.unpack_from(blob, 32 + i * 48)
        name = name.split(b'\0', 1)[0].decode()
        if names is not None and name not in names:
            continue
        groups = {}
        for j in range(size):
            row = pack.ENTRY.unpack_from(blob, offset + 64 * j)
            groups.setdefault(row[0], []).append(row)
        rows = []
        for kind, wire in sorted(groups.items()):
            wire.sort(key=lambda row: row[1])
            assert [row[1] for row in wire] == [sum(x[4] for x in wire[:j]) for j in range(len(wire))]
            assert sum(row[4] for row in wire) == len(decoded[name][kind])
            raw = kind in (n64_snd.KIND, n64_music.KIND)
            if raw:
                assert all(row[6] == 0 and row[3] == row[4] for row in wire)
            rows.append(pack.Section(kind, decoded[name][kind], alignment=wire[0][5],
                                     compression='none' if raw else 'auto',
                                     boundaries=tuple(row[1] for row in wire[1:])))
        out[name] = rows
    return decoded, out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--campaign', type=Path, required=True)
    parser.add_argument('--campaign-report', type=Path, required=True)
    parser.add_argument('--music-pack', type=Path, required=True)
    parser.add_argument('--cache', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    original = args.campaign.read_bytes()
    report_bytes = args.campaign_report.read_bytes()
    campaign = json.loads(report_bytes)
    assert (len(original), sha(original)) == (campaign['pack_bytes'], campaign['pack_sha256'])
    expected = set(campaign['pack_levels'])
    codec = pack.compressor(args.cache)
    assert codec.identity == campaign['compressor_sha256']
    decoded, entries = entries_of(original)
    assert set(decoded) == set(entries) == expected
    assert pack.build(entries, codec) == original, 'canonical pack reproduction mismatch'
    music_blob = args.music_pack.read_bytes()
    _, music = entries_of(music_blob)
    music = {name: rows for name, rows in music.items() if name.startswith('MUSIC_')}
    assert set(music) == {f'MUSIC_{n:02}' for n in range(2, 58)}
    assert not music.keys() & entries.keys()
    entries.update(music)
    blob = pack.build(entries, codec)
    independent = pack.inspect(blob)
    for name, rows in entries.items():
        assert all(independent[name][row.kind] == row.data for row in rows), name
    for name, kinds in decoded.items():
        assert independent[name] == kinds, 'campaign native payload changed: ' + name
    # PC banks are lossless PCM8/PCM16 (1, 2); PSX banks keep the disc's raw SPU ADPCM (3).
    source = campaign.get('source', 'pc')
    allowed = {3} if source == 'psx' else {1, 2}
    assert source in ('pc', 'psx')
    sound, native_hashes = {}, {}
    for name in sorted(expected):
        raw = independent[name][n64_snd.KIND]
        header = n64_snd.HEADER.unpack_from(raw)
        SoundView(raw)
        encodings = sorted({n64_snd.SAMPLE.unpack_from(raw, header[6] + i * n64_snd.SAMPLE.size)[3]
                            for i in range(header[4])})
        assert set(encodings) <= allowed, f'sound bank encodings {encodings} not {sorted(allowed)}: ' + name
        sound[name] = dict(section_sha256=sha(raw), bytes=len(raw), sample_count=header[4],
                           metadata_bytes=header[3], encodings=encodings)
        native_hashes[name] = {str(kind): sha(data) for kind, data in independent[name].items()}
    available = sum(n64_music.validate(rows[0].data, int(name[-2:]))['available']
                    for name, rows in entries.items() if name.startswith('MUSIC_'))
    assert available == 55
    assert len(blob) + VIDEO_RESERVE < CART_LIMIT
    music_report = args.music_pack.with_suffix('.json')
    music_source = json.loads(music_report.read_text()).get('music_source') if music_report.exists() else None
    report = dict(pack_bytes=len(blob), pack_sha256=sha(blob), source=source,
                  campaign_pack=str(args.campaign), campaign_pack_sha256=sha(original),
                  campaign_report_sha256=sha(report_bytes), campaign_content_key=campaign['content_key'],
                  music_pack=str(args.music_pack), music_pack_sha256=sha(music_blob),
                  campaign_levels=sorted(expected), campaign_level_count=len(expected),
                  music_real_tracks=available, music_source=music_source,
                  music_raw_bytes=sum(len(rows[0].data) for name, rows in entries.items()
                                      if name.startswith('MUSIC_')),
                  sound_sections=sound, native_section_sha256=native_hashes,
                  total_directory_entries=len(entries), compressor_sha256=codec.identity,
                  recipe_sha256=sha(Path(__file__).read_bytes()),
                  cart_limit_bytes=CART_LIMIT, video_reserved_bytes=VIDEO_RESERVE,
                  remaining_for_image_and_linker_bytes=CART_LIMIT - VIDEO_RESERVE - len(blob))
    report['content_key'] = sha((report['campaign_pack_sha256'] + report['music_pack_sha256'] +
                                 codec.identity + report['recipe_sha256']).encode())
    for path, data in ((args.out, blob),
                       (args.out.with_suffix('.json'), (json.dumps(report, indent=2, sort_keys=True) + '\n').encode())):
        if path.exists() and path.read_bytes() != data:
            raise ValueError(f'immutable output differs: {path}')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(blob)
    args.out.with_suffix('.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
    print(json.dumps({k: report[k] for k in ('pack_bytes', 'pack_sha256', 'campaign_level_count',
          'music_real_tracks', 'music_raw_bytes', 'total_directory_entries',
          'remaining_for_image_and_linker_bytes', 'content_key')}, sort_keys=True))


if __name__ == '__main__':
    main()
