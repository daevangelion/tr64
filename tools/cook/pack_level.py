#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Assemble a playable level and optional shared music catalog in one pack."""
import argparse
import hashlib
import json
import zlib
from pathlib import Path
import n64_core
import n64_geo
import n64_music
import n64_snd
import n64_ui_image
import n64_video
import n64_pack as pack
import n64_tex_view
import lara_graft
import src_pc
import src_psx
import src_tr2_tr3
from n64_validate import IO_BYTES, admit
from pool_rows import pool_table
from sizing import pools


def add_music_catalog(entries, path):
    """Merge validated raw tracks into this binding, returning manifest identity."""
    music = n64_music.read_music_catalog(path)
    if entries.keys() & music.keys():
        raise ValueError('music catalog collides with pack entry')
    if any(s.kind != n64_music.KIND or s.compression != 'none'
           for sections in music.values() for s in sections):
        raise ValueError('music catalog requires raw MUSIC sections')
    entries.update(music)
    return hashlib.sha256(path.read_bytes()).hexdigest()


def add_video_catalog(entries, path):
    """Merge validated VIDEO_/FILM_ streams into this binding; returns the manifest identity."""
    video = n64_video.read_video_catalog(path)
    if entries.keys() & video.keys():
        raise ValueError('video catalog collides with pack entry')
    entries.update(video)
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', choices=('pc', 'psx'), required=True)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--main-sfx', type=Path, help='PC Tomb Raider II shared MAIN.SFX bank')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--textures', type=Path, help='explicit immutable section.bin + manifest.json input')
    parser.add_argument('--cache', type=Path, help='version-keyed host compressor cache')
    parser.add_argument('--psx-encoding',choices=('pcm','spu'),default='pcm',help='native PSX sample payload encoding (same engine)')
    parser.add_argument('--music-catalog', type=Path,
                        help='immutable music manifest FILE; tracks share the level/SFX pack binding')
    parser.add_argument('--video-catalog', type=Path,
                        help='immutable video manifest FILE (n64_video.py); streams share the pack')
    parser.add_argument('--title-image', type=Path,
                        help='source title picture (PCX/BMP/PSX) cooked into UI64 section 0x50000')
    parser.add_argument('--lara-tr2-donor', type=Path,
                        help='optional PC TR2 level (e.g. WALL.TR2) to graft TR2 Lara ability clips onto TR1')
    parser.add_argument('--lara-tr3-donor', type=Path,
                        help='optional PC TR3 level (e.g. JUNGLE.TR2) to graft TR3 Lara ability clips onto TR1/TR2')
    args = parser.parse_args()
    try:
        if args.main_sfx:
            if args.source != 'pc':
                raise ValueError('--main-sfx requires PC Tomb Raider II/III data')
            bank = src_tr2_tr3.read_bank(args.main_sfx.read_bytes(), str(args.main_sfx))
            level = src_tr2_tr3.load(args.input, bank, census=True)
            game = level.reserved['game']
            if game not in (2, 3):
                raise ValueError(f'{level.name}: unsupported game version {game}')
            if game == 2 and level.reserved.get('findings'):
                raise ValueError(f'{level.name}: unexpected strict findings {level.reserved["findings"]}')
            level.name = args.input.stem.lower()
            n64_snd.select_external_samples(level)
        else:
            level = (src_pc if args.source == 'pc' else src_psx).load(args.input)
        game = level.reserved.get('game', 1)
        graft_report = None
        if args.lara_tr2_donor or args.lara_tr3_donor:
            tr2_d = lara_graft.load_donor(args.lara_tr2_donor) if (game == 1 and args.lara_tr2_donor) else None
            tr3_d = lara_graft.load_donor(args.lara_tr3_donor) if args.lara_tr3_donor else None
            graft_report = lara_graft.graft_lara_abilities(level, tr2_donor=tr2_d, tr3_donor=tr3_d)
        if args.textures:
            tx = (args.textures / 'section.bin').read_bytes()
            manifest = json.loads((args.textures / 'manifest.json').read_text())
            decoded = n64_tex_view.Section(tx)
            if decoded.odd_width != (game in (2, 3)):
                raise ValueError('explicit texture input game version / TX64 flags mismatch')
            admit(game, object_descriptors=decoded.objects, sprite_descriptors=decoded.sprites,
                  descriptor_ids=len(decoded.descriptors), clusters=len(decoded.clusters),
                  palettes=len(decoded.palettes), split_lines=len(decoded.splits))
            if manifest['source_sha256'] != level.sha256 or manifest['section_sha256'] != hashlib.sha256(tx).hexdigest():
                raise ValueError('explicit texture input source/content identity mismatch')
            for kind in ('object', 'sprite'):
                mapping = {str(d['source_index']): i for i, d in enumerate(decoded.descriptors) if d['kind'] == kind}
                if manifest[kind + '_descriptor_map'] != mapping:
                    raise ValueError('explicit texture input descriptor mapping mismatch')
        else:
            tx, manifest = n64_core.texture(level)
        sections = n64_core.encode(level, manifest)
        rg, mg, geometry = n64_geo.cook(level, manifest)
        sections += [pack.Section(0x10000, tx), pack.Section(0x20000, rg), pack.Section(0x30000, mg)]
        sound,sound_report=n64_snd.cook(level,psx_encoding=args.psx_encoding)
        sections.append(sound)
        if args.title_image:
            title_img, _ = n64_ui_image.cook(args.source, args.title_image)
            sections.append(pack.Section(0x50000, title_img))
        layout=pack.level_layout(sections,budget=pools(pool_table(game))['level'])
        codec = pack.compressor(args.cache or args.out.parent / 'compressor')
        entries = {level.name: sections}
        music_sha256 = add_music_catalog(entries, args.music_catalog) if args.music_catalog else ''
        video_sha256 = add_video_catalog(entries, args.video_catalog) if args.video_catalog else ''
        table_bytes = 32*sum(1 for section in sections for _ in pack.parts(section))
        needed = pack.align(48*len(entries)) + table_bytes + pack.BLOCK
        if needed > IO_BYTES:
            raise ValueError(f'pack {level.name} section table: needs {needed} io bytes, limit {IO_BYTES}')
        blob = pack.build(entries, codec, games={level.name: game} if game in (2, 3) else None)
        decoded = pack.inspect(blob)
        if any(decoded[name][s.kind] != s.data for name, rows in entries.items() for s in rows):
            raise ValueError('assembled pack independent decode mismatch')
        n64_core.write_changed(args.out, blob)
        report = dict(source=level.source, name=level.name, source_sha256=level.sha256,
                      texture_sha256=hashlib.sha256(tx).hexdigest(), pack_sha256=hashlib.sha256(blob).hexdigest(),
                      pack_bytes=len(blob), core_bytes=sum(pack.align(len(s.data)) for s in sections if s.kind < 0x10000),
                      texture_bytes=len(tx), rooms_bytes=len(rg), meshes_bytes=len(mg),
                      resident_layout=layout,
                      game=level.reserved.get('game', 1), texture_breakdown=manifest['breakdown'],
                      geometry_cook_sha256=geometry['cook_sha256'], compressor_sha256=codec.identity)
        report['sound_bytes']=len(sound.data)
        report['sound_metadata_bytes']=sound_report['metadata_bytes']
        report['sound_blocks']=sum(1 for _ in pack.parts(sound))
        report['sound_metadata_crc']=f'{zlib.crc32(sound.data[:sound_report["metadata_bytes"]]):08x}'
        report['sound_samples']=sound_report['sample_count']
        report['sound_unavailable']=len(sound_report['unavailable'])
        if args.music_catalog:
            tracks = {name: rows for name, rows in entries.items() if name.startswith('MUSIC_')}
            report['music_catalog_sha256'] = music_sha256
            report['music_tracks'] = len(tracks)
            report['music_bytes'] = sum(len(s.data) for rows in tracks.values() for s in rows)
        if args.video_catalog:
            report['video_catalog_sha256'] = video_sha256
            report['video_streams'] = sum(1 for n in entries if n.startswith('VIDEO_'))
        if graft_report:
            report['lara_graft'] = graft_report
        report['checksums']={n64_core.TABLES[s.kind-1].name: n64_core.checksum(s) for s in sections if s.kind<65536}
        report['texture_checksum']=n64_core.checksum(next(s for s in sections if s.kind==65536))
        report['content_key']=hashlib.sha256((n64_core.tool_hash()+geometry['cook_sha256']+sound_report['cook_sha256']+sound_report['section_sha256']+level.sha256+report['texture_sha256']+codec.identity+music_sha256+video_sha256).encode()).hexdigest()
        n64_core.write_changed(args.out.with_suffix('.json'), (json.dumps(report, indent=2) + '\n').encode())
        print(json.dumps(report, sort_keys=True))
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f'pack_level: {error}\n')


if __name__ == '__main__':
    main()
