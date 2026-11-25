# SPDX-License-Identifier: MIT
"""Focused data-cook checks; no retail data or external source checkout needed."""
import argparse
import importlib.util
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    spec = importlib.util.spec_from_file_location('ui_strings', root / 'tools/cook/n64_ui_strings.py')
    cook = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(cook)
    schema = root / 'engine/ui/en.json'
    ids, offsets, blob = cook.cook(schema, schema)
    font = cook.load_font(root / 'engine/ui/font.json')
    widths = [cook.text_width(blob[offset:blob.index(0, offset)], font) for offset in offsets]
    assert cook.text_width(b'$\x6d', font) == 21
    assert cook.text_width(b'$ ', font) == 14
    assert len(ids) == 176
    assert ids[0] == 'STR_EMPTY' and ids[169:] == [
        'STR_TR1_END2', 'STR_CANNOT_SAVE_NOW', 'STR_OPT_DETAIL_AUTO_DIST',
        'STR_OPT_CONTROLS_ABILITIES', 'STR_ABILITIES_TR1', 'STR_ABILITIES_TR2', 'STR_ABILITIES_TR3'
    ]
    assert blob[offsets[170]:].split(b'\0')[0] == b'CANNOT SAVE NOW'
    assert blob[offsets[ids.index('STR_INV_TITLE_MAIN')]:].split(b'\0')[0] == b'INVENTORY'
    assert blob[offsets[ids.index('STR_TR1_LEVEL1')]:].split(b'\0')[0] == b'Caves'
    assert len(offsets) * 2 + len(blob) < 4096
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    translated = json.loads(schema.read_text())
    translated['strings'] = dict(reversed(list(translated['strings'].items())))
    translated['strings']['STR_INV_TITLE_MAIN'] = 'ITEMS TEST'
    path = out / 'language.json'
    path.write_text(json.dumps(translated))
    translated_ids, translated_offsets, translated_blob = cook.cook(schema, path)
    assert translated_ids == ids
    assert translated_blob[translated_offsets[ids.index('STR_INV_TITLE_MAIN')]:].split(b'\0')[0] == b'ITEMS TEST'
    for mutation in ('missing', 'extra', 'nul', 'unsupported', 'overflow'):
        data = json.loads(schema.read_text())
        if mutation == 'missing':
            del data['strings']['STR_EMPTY']
        elif mutation == 'extra':
            data['strings']['STR_EXTRA'] = 'extra'
        elif mutation == 'nul':
            data['strings']['STR_EMPTY'] = '\0'
        elif mutation == 'unsupported':
            data['strings']['STR_EMPTY'] = '\xff'
        else:
            data['strings']['STR_EMPTY'] = 'A' * 65536
        path.write_text(json.dumps(data))
        try:
            cook.cook(schema, path)
        except ValueError:
            pass
        else:
            raise AssertionError(f'{mutation} accepted')
    print(f'UI language PASS: {len(ids)} IDs, {len(blob)} blob bytes, {len(offsets)*2} offset bytes; language-order and 5 rejecting controls')


if __name__ == '__main__':
    main()
