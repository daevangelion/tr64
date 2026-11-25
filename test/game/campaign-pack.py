#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build TITLE, a source's campaign and its four cutscenes with pinned TX64, real SN64 and UI64.

PLAYABLE_LEVELS remains the sixteen-level simulation catalog. PACK_LEVELS adds
TITLE and CUT1..CUT4 (the source's in-engine cutscene levels, which the campaign
enters between LEVEL3B/LEVEL4, LEVEL7B/LEVEL8A, LEVEL10A/LEVEL10B and
LEVEL10B/LEVEL10C) through the same Core/TX64/RG64/MG64/SN64 producers and
pack.build path.
PC remains the default; --source psx uses actual PSX Core/geometry/CLUTs and
raw source SPU sound, never PC substitute sections. The UI image is an explicit
edition-matched immutable input from the pinned UI cook. Output pack and
manifest are immutable; choose a new --out when producers or inputs change.
Sound streams; resident sections share the unchanged level arena.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
import sys
sys.dont_write_bytecode = True
import n64_core as core
import n64_core_view as independent
import n64_geo
import n64_pack as pack
import n64_snd as snd
import n64_tex_view
import src_pc
import src_psx

# tr1_campaign.h:104-130, OpenLara 2333adef; GYM is deliberately separate.
PLAYABLE_LEVELS = ['gym', 'level1', 'level2', 'level3a', 'level3b', 'level4', 'level5',
          'level6', 'level7a', 'level7b', 'level8a', 'level8b', 'level8c',
          'level10a', 'level10b', 'level10c']
CUT_LEVELS = ['cut1', 'cut2', 'cut3', 'cut4']
PACK_LEVELS = ['title', *PLAYABLE_LEVELS, *CUT_LEVELS]
IMAGE_KIND = 0x50000
# UI's immutable producer snapshot; the helper binds its published bytes rather
# than adding a second PCX/RNC/image conversion path.
IMAGE_PRODUCER = dict(
    revision='06119bb88e3eba9bc12a644b95f3f9105864f13d',
    tools_sha256={
        'tools/cook/n64_ui_image.py': 'fc16dcc39e46da8b18bd6ed6a8e9c4fa805756fe8f71948540ce98204018f377',
        'tools/cook/n64_ui_rnc.py': 'e64b4e71638fa72db09b069d11ccffaaa728a94d5038bc3bab445e46123f7cbb'})
LEVEL_PACK_BUDGET = 24 * 1024 * 1024


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def validate_texture(level, blob, manifest):
    section_hash = hashlib.sha256(blob).hexdigest()
    if (manifest['source'] != level.source or manifest['level'] != level.name or
            manifest['source_sha256'] != level.sha256 or
            manifest['section_sha256'] != section_hash or manifest['bytes'] != len(blob)):
        raise ValueError(f'texture identity mismatch: {level.name}')
    decoded = n64_tex_view.Section(blob)
    for kind in ('object', 'sprite'):
        mapping = {str(d['source_index']): i for i, d in enumerate(decoded.descriptors) if d['kind'] == kind}
        if manifest[kind + '_descriptor_map'] != mapping:
            raise ValueError(f'texture descriptor mapping mismatch: {level.name}/{kind}')
    return dict(source_sha256=level.sha256, section_sha256=section_hash,
                tool_sha256=manifest['tool_sha256'], content_key=manifest['content_key'],
                bytes=len(blob))


def bind_image(image_path, manifest_path, source_path, source='pc'):
    metadata = json.loads(manifest_path.read_text())
    blob = image_path.read_bytes()
    if (metadata['source'] != source or metadata['source_sha256'] != digest(source_path) or
            metadata['output_sha256'] != hashlib.sha256(blob).hexdigest() or
            metadata['bytes'] != len(blob) or (metadata['width'], metadata['height']) != (320, 240)):
        raise ValueError('title image source/output identity mismatch')
    format_id = {'CI8': 1, 'RGBA5551': 2}[metadata['format']]
    size, palette = (77344, 76832) if format_id == 1 else (153632, 0)
    expected = (b'UI64', 1, size, 320, 240, format_id, 32, palette)
    if len(blob) != size or struct.unpack_from('>4s7I', blob) != expected:
        raise ValueError('title image UI64 header/layout mismatch')
    section = pack.Section(IMAGE_KIND, blob)
    binding = dict(source=source, path=str(image_path.resolve()), manifest_path=str(manifest_path.resolve()),
        source_path=str(source_path.resolve()), source_sha256=metadata['source_sha256'],
        source_bytes=source_path.stat().st_size, manifest_sha256=digest(manifest_path),
        section_sha256=metadata['output_sha256'], producer=IMAGE_PRODUCER, metadata=metadata,
        image_bytes=len(blob), image_format=format_id, image_checksum=core.checksum(section))
    return section, binding


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source', choices=('pc', 'psx'), default='pc')
    ap.add_argument('--data', type=Path, help='level folder; defaults to data/pc or data/psx/PSXDATA')
    ap.add_argument('--textures', type=Path, required=True)
    ap.add_argument('--title-image', type=Path, required=True, help='immutable UI64 published by the UI cook')
    ap.add_argument('--title-image-manifest', type=Path, required=True)
    ap.add_argument('--title-image-source', type=Path, help='defaults to PC titleh.pcx or PSX sibling DELDATA/AMERTIT.RAW')
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--cache', type=Path, required=True)
    args = ap.parse_args()
    data = args.data if args.data is not None else Path('/tmp/tr64/data/pc' if args.source == 'pc'
                                                       else '/tmp/tr64/data/psx/PSXDATA')
    reader = src_pc if args.source == 'pc' else src_psx
    source_paths = {name: data / (name + '.phd' if args.source == 'pc' else name.upper() + '.PSX')
                    for name in PACK_LEVELS}
    picture = data / 'titleh.pcx' if args.source == 'pc' else data.parent / 'DELDATA' / 'AMERTIT.RAW'
    sound_encoding = 'spu' if args.source == 'psx' else 'pcm'
    image, image_binding = bind_image(args.title_image, args.title_image_manifest,
        args.title_image_source if args.title_image_source is not None else picture, args.source)
    bindings = {}
    original_texture = core.texture

    def texture(level):
        if level.name == 'title':
            # Exactly the original source/tool/version-keyed producer. In
            # particular, do not recook the sixteen pinned campaign TX64s.
            blob, manifest = original_texture(level)
            binding = validate_texture(level, blob, manifest)
            binding['producer'] = 'n64_core.texture'
            binding['path'] = str(Path('/tmp/tr64/content') / manifest['content_key'])
        else:
            choices = list((args.textures / args.source / level.name).glob('*/manifest.json'))
            if len(choices) != 1:
                raise ValueError(f'texture binding not unique: {level.name}')
            meta = choices[0]
            manifest = json.loads(meta.read_text())
            blob = (meta.parent / 'section.bin').read_bytes()
            binding = validate_texture(level, blob, manifest)
            binding.update(path=str(meta.parent.resolve()), manifest_sha256=digest(meta),
                           producer='immutable TX64 binding')
        bindings[level.name] = binding
        return blob, manifest

    codec = pack.compressor(args.cache)
    sections = {}
    levels = {}
    core.texture = texture
    try:
        for name in PACK_LEVELS:
            source_path = source_paths[name]
            level = reader.load(source_path)
            native, tm = core.cook_sections(level)
            rg, mg, geometry = n64_geo.cook(level, tm)
            sound, sound_report = snd.cook(level, psx_encoding=sound_encoding)
            native += [pack.Section(0x20000, rg), pack.Section(0x30000, mg), sound]
            if name == 'title':
                native.append(image)
            layout = pack.level_layout(native)
            identities = {}
            for section in native:
                record = dict(bytes=len(section.data), sha256=hashlib.sha256(section.data).hexdigest(),
                              checksum=core.checksum(section))
                if section.kind < 0x10000:
                    header = core.PREFIX.unpack_from(section.data)
                    record.update(table=core.TABLES[section.kind - 1].name, count=header[2],
                        stride=header[3], present=header[4],
                        wire_body_sha256=hashlib.sha256(section.data[header[5]:]).hexdigest())
                identities[str(section.kind)] = record
            levels[name] = dict(source=args.source, source_path=str(source_path.resolve()), source_sha256=level.sha256,
                geometry_cook_sha256=geometry['cook_sha256'], resident_layout=layout, sound=sound_report,
                source_rooms=len(level.rooms), source_items=len(level.data['items']),
                source_models=[dict(model, reserved=model['reserved'].hex() if model['reserved'] is not None else None)
                               for model in level.data['models']], source_models_reserved_encoding='hex',
                source_mesh_pointers=len(level.data['mesh_pointers']),
                decoded_meshes=len(level.meshes), sections=identities)
            if name == 'title':
                levels[name].update(image_bytes=image_binding['image_bytes'],
                    image_format=image_binding['image_format'], image_checksum=image_binding['image_checksum'])
                if layout['sections']['tex'] != pack.align(len(next(s.data for s in native if s.kind == 0x10000))) + len(image.data):
                    raise ValueError('title image is not charged to the combined TX64/image reservation')
            sections[name] = native
            print(f'campaign cook {args.source}/{name} resident={layout["bytes"]}/{layout["budget_bytes"]} '
                  f'sound={len(sound.data)} metadata={sound_report["metadata_bytes"]} '
                  f'sound_parts={sound_report["stream_blocks"]} image={levels[name].get("image_bytes", 0)}', flush=True)
    finally:
        core.texture = original_texture
    blob = pack.build(sections, codec)
    if len(blob) > LEVEL_PACK_BUDGET:
        raise ValueError(f'campaign pack {len(blob)} exceeds level-pack cart budget {LEVEL_PACK_BUDGET}')
    decoded = independent.read_pack(blob)
    if set(decoded) != set(PACK_LEVELS):
        raise ValueError('assembled campaign directory does not contain exactly the pack levels')
    for name in PACK_LEVELS:
        independent.equal_ir(reader.load(source_paths[name]), decoded[name])
        if set(decoded[name].sections) != {s.kind for s in sections[name]}:
            raise ValueError(f'{name}: assembled section catalog differs from its producers')
        if any(decoded[name].sections[s.kind] != s.data for s in sections[name]):
            raise ValueError(f'{name}: assembled section differs from its producer')
    root = Path(__file__).resolve().parents[2]
    identity = dict(source=args.source, sound_encoding=sound_encoding,
        core_abi=core.CORE_ABI, pack_sha256=hashlib.sha256(blob).hexdigest(),
        core_tool_sha256=core.tool_hash(), sound_tool_sha256=snd.tool_hash(),
        compressor_sha256=codec.identity, producer_sha256=digest(__file__),
        pack_tool_sha256=digest(pack.__file__), source_reader_sha256=digest(reader.__file__),
        independent_verifier_sha256=digest(independent.__file__),
        texture_verifier_sha256=digest(n64_tex_view.__file__),
        pools_sha256=digest(root / 'include/tr64/pools.def'),
        playable_levels=PLAYABLE_LEVELS, pack_levels=PACK_LEVELS,
        source_sha256={name: row['source_sha256'] for name, row in levels.items()},
        texture_sections={name: row['section_sha256'] for name, row in bindings.items()},
        texture_tools={name: row['tool_sha256'] for name, row in bindings.items()},
        sound_sections={name: row['sound']['section_sha256'] for name, row in levels.items()},
        geometry_tools={name: row['geometry_cook_sha256'] for name, row in levels.items()},
        image_sections={'title': image_binding})
    key = hashlib.sha256(json.dumps(identity, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    manifest = dict(identity, content_key=key, pack_bytes=len(blob),
        level_pack_budget_bytes=LEVEL_PACK_BUDGET, bindings=bindings, levels=levels)
    artifacts = [(args.out, blob), (args.out.with_suffix('.json'),
                 (json.dumps(manifest, indent=2) + '\n').encode())]
    # Check both destinations before publishing either; never replace frozen evidence.
    for path, data in artifacts:
        if path.exists() and path.read_bytes() != data:
            raise ValueError(f'immutable campaign output differs: {path}')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    for path, data in artifacts:
        if not path.exists():
            path.write_bytes(data)
    print(f'campaign pack PASS source={args.source} levels={len(sections)} playable={len(PLAYABLE_LEVELS)} bytes={len(blob)} '
          f'key={key} sha256={identity["pack_sha256"]} path={args.out}', flush=True)


if __name__ == '__main__':
    main()
