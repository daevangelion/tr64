# SPDX-License-Identifier: MIT
"""Run from repository root with PYTHONPATH=tools/cook; no retail inputs."""
import argparse
import struct
from pathlib import Path
from PIL import Image
import n64_ui_image as image
import n64_ui_rnc as rnc


class Writer:
    def __init__(self):
        self.data = bytearray()
        self.at = None
        self.used = 8

    def bits(self, text):
        for bit in text:
            if self.used == 8:
                self.at = len(self.data)
                self.data.append(0)
                self.used = 0
            self.data[self.at] |= int(bit) << (7-self.used)
            self.used += 1

    def byte(self, byte):
        self.data.append(byte)


def envelope(payload, raw):
    return b'RNC\2' + struct.pack('>IIHHBB', len(raw), len(payload),
                                  rnc.crc16(raw), rnc.crc16(payload), 0, 1) + payload


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    assert rnc.crc16(b'123456789') == 0xbb3d
    writer = Writer()
    writer.bits('00')
    for value in b'ABC':
        writer.bits('0')
        writer.byte(value)
    writer.bits('1000')  # four-byte overlapping match at distance three
    writer.bits('0')
    writer.byte(2)
    writer.bits('1111')
    writer.byte(0)
    writer.bits('0')
    packed = envelope(bytes(writer.data), b'ABCABCA')
    assert rnc.decode(packed) == b'ABCABCA'
    bad_reference = Writer()
    bad_reference.bits('0010000')
    bad_reference.byte(0)
    bad_reference.bits('1111')
    bad_reference.byte(0)
    bad_reference.bits('0')
    bad_ref = envelope(bytes(bad_reference.data), b'AAAA')
    mutations = [packed[:3], b'RNC\1'+packed[4:], packed[:-1],
                 packed[:18]+bytes([packed[18]^1])+packed[19:], bad_ref]
    for candidate in mutations:
        try:
            rnc.decode(candidate)
        except ValueError:
            pass
        else:
            raise AssertionError('invalid RNC2 accepted')
    pc = Image.new('P', (640, 480), 1)
    palette = [0]*768
    palette[3:6] = [248, 128, 8]
    pc.putpalette(palette)
    pc.putpixel((1, 1), 2)
    path = out/'source.pcx'
    pc.save(path)
    blob, manifest = image.cook('pc', path)
    assert struct.unpack_from('>4s7I', blob) == (b'UI64', 1, 77344, 320, 240, 1, 32, 76832)
    assert blob[32:34] == bytes([2, 1])
    assert int.from_bytes(blob[76834:76836], 'big') == (31 << 11 | 16 << 6 | 1 << 1 | 1)
    assert manifest['bytes'] == len(blob)
    for pixels, palette in [(b'', bytes(512)), (bytes(76800), b'')]:
        try:
            image.encode_ci8(pixels, palette)
        except ValueError:
            pass
        else:
            raise AssertionError('invalid CI8 accepted')
    try:
        image.encode_rgba16(b'')
    except ValueError:
        pass
    else:
        raise AssertionError('invalid RGBA16 accepted')
    print('UI image PASS: documented RNC2 literal/overlap, CRC vector, 5 rejecting RNC controls, PCX nearest+palette, 3 image-size controls')


if __name__ == '__main__':
    main()
