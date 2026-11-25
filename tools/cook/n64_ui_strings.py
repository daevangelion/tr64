# SPDX-License-Identifier: MIT
"""Pack UI language data as one byte blob and u16 offsets, preserving StringID."""
import argparse
import json
import re
from pathlib import Path


def load_font(path):
    font = json.loads(Path(path).read_text())
    for key, size, limit in (('advances', 110, 255), ('remap', 102, 109)):
        values = font[key]
        if len(values) != size or any(type(v) is not int or not 0 <= v <= limit for v in values):
            raise ValueError(f'invalid font {key}')
    return font


def text_width(value, font):
    advances, remap = font['advances'], font['remap']
    def glyph(byte):
        if 0 < byte < 11:
            return byte + 81
        if 11 <= byte < 16:
            return byte + 91
        if 32 <= byte < 134:
            return remap[byte - 32]
        raise ValueError('unsupported font byte')
    index = 0
    while index < len(value):
        byte = value[index]
        if byte == 36:
            index += 1
            if index == len(value) or not 0 < value[index] < 110:
                raise ValueError('invalid escaped glyph')
        else:
            glyph(byte)
        index += 1
    width = sum(6 if byte == 32 else advances[glyph(byte)] + 1 for byte in value)
    if width > 65535:
        raise ValueError('text width exceeds u16 domain')
    return width


def cook(schema_path, language_path):
    schema = json.loads(Path(schema_path).read_text())
    language = json.loads(Path(language_path).read_text())
    ids = list(schema['strings'])
    values = language['strings']
    if set(values) != set(ids):
        raise ValueError('language StringID set differs from schema')
    blob = bytearray()
    offsets = []
    for key in ids:
        if not re.fullmatch(r'STR_[A-Z0-9_]+', key):
            raise ValueError('invalid StringID')
        value = values[key].encode('latin1')
        if b'\0' in value or any(c >= 134 or 16 <= c < 32 for c in value):
            raise ValueError(f'{key}: unsupported font byte')
        if len(blob) + len(value) + 1 > 65535:
            raise ValueError('language exceeds u16 offset domain')
        offsets.append(len(blob))
        blob.extend(value + b'\0')
    return ids, offsets, bytes(blob)


def write_if_changed(path, content):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text() != content:
        path.write_text(content)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--schema', required=True)
    parser.add_argument('--language', required=True)
    parser.add_argument('--ids', required=True)
    parser.add_argument('--data', required=True)
    parser.add_argument('--font', required=True)
    parser.add_argument('--font-header', required=True)
    args = parser.parse_args()
    ids, offsets, blob = cook(args.schema, args.language)
    font = load_font(args.font)
    widths = [text_width(blob[offset:blob.index(0, offset)], font) for offset in offsets]
    header = '/* SPDX-License-Identifier: MIT */\n/* Generated StringID layout facts; do not edit. */\n'
    header += '#ifndef TR64_UI_STRING_IDS_H\n#define TR64_UI_STRING_IDS_H\nenum {\n'
    header += ''.join(f' TR64_{key}={i},\n' for i, key in enumerate(ids))
    header += f' TR64_UI_STRING_COUNT={len(ids)}\n}};\n#endif\n'
    data = '/* SPDX-License-Identifier: BSD-2-Clause */\n'
    data += '/* Language data derived from OpenLara (c) 2018 Timur Gagiev; see game/LICENSE-OPENLARA. */\n'
    data += 'static const uint16_t string_offsets[TR64_UI_STRING_COUNT]={\n'
    data += ','.join(str(o) for o in offsets) + '\n};\n'
    data += 'static const uint16_t string_widths[TR64_UI_STRING_COUNT]={\n'
    data += ','.join(str(w) for w in widths) + '\n};\n'
    data += f'static const unsigned char string_bytes[{len(blob)}]={{\n'
    data += '\n'.join(','.join(str(b) for b in blob[i:i+32]) + ',' for i in range(0, len(blob), 32))
    data += '\n};\n'
    write_if_changed(args.ids, header)
    write_if_changed(args.data, data)
    font_header = '/* SPDX-License-Identifier: MIT */\n/* Generated glyph layout facts; do not edit. */\n'
    font_header += '#ifndef TR64_UI_FONT_DATA_H\n#define TR64_UI_FONT_DATA_H\n#include <stdint.h>\n'
    for name in ('advances', 'remap'):
        values = font[name]
        font_header += f'static const uint8_t {name}[{len(values)}]={{'+','.join(map(str, values))+'};\n'
    font_header += '#endif\n'
    write_if_changed(args.font_header, font_header)
    print(f'UI strings: count={len(ids)} blob={len(blob)} offsets={len(offsets)*2} widths={len(widths)*2} total={len(blob)+len(offsets)*2+len(widths)*2} language={args.language}')


if __name__ == '__main__':
    main()
