# SPDX-License-Identifier: MIT
"""Cook a source title picture into the edition-independent UI64 image section."""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from PIL import Image
from n64_ui_rnc import decode as rnc2_decode

WIDTH, HEIGHT = 320, 240
CI8, RGBA16 = 1, 2


def encode_ci8(indices, palette):
    if len(indices) != WIDTH*HEIGHT or len(palette) != 512:
        raise ValueError('UI64 CI8 dimensions/palette')
    return struct.pack('>4s7I', b'UI64', 1, 32+len(indices)+512,
                       WIDTH, HEIGHT, CI8, 32, 32+len(indices)) + indices + palette


def encode_rgba16(pixels):
    if len(pixels) != WIDTH*HEIGHT*2:
        raise ValueError('UI64 RGBA16 dimensions')
    return struct.pack('>4s7I', b'UI64', 1, 32+len(pixels),
                       WIDTH, HEIGHT, RGBA16, 32, 0) + pixels


def cook(source, path):
    path = Path(path)
    data = path.read_bytes()
    if source == 'pc':
        with Image.open(path) as image:
            if image.size != (640, 480) or image.mode not in ('P', 'RGB'):
                raise ValueError('expected PC 640x480 indexed (PCX) or RGB (BMP) title image')
            if image.mode == 'P':
                indices = image.resize((WIDTH, HEIGHT), Image.Resampling.NEAREST).tobytes()
                colours = image.getpalette()
                if colours is None or len(colours) != 768:
                    raise ValueError('PC title lacks 256-entry RGB palette')
                palette = bytearray()
                for i in range(256):
                    r, g, b = (c >> 3 for c in colours[3*i:3*i+3])
                    palette.extend(struct.pack('>H', r << 11 | g << 6 | b << 1 | 1))
                output = encode_ci8(indices, palette)
                width, height, encoding, fmt = 640, 480, 'PCX indexed RGB8', 'CI8'
            else:
                rgb = image.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS).tobytes()
                pixels = bytearray(WIDTH * HEIGHT * 2)
                for i in range(WIDTH * HEIGHT):
                    r = rgb[3*i] >> 3
                    g = rgb[3*i+1] >> 3
                    b = rgb[3*i+2] >> 3
                    struct.pack_into('>H', pixels, 2*i, (r << 11) | (g << 6) | (b << 1) | 1)
                output = encode_rgba16(bytes(pixels))
                width, height, encoding, fmt = 640, 480, 'PC RGB24', 'RGBA5551'
    elif source == 'psx':
        raw = rnc2_decode(data)
        # Measured source raster: 384x256 little-endian BGR555 words. The
        # decompressed byte count and complete arranged picture are recorded
        # with the input identity; no retail executable is inspected.
        width, height, encoding, fmt = 384, 256, 'RNC2 BGR555 little-endian', 'RGBA5551'
        if len(raw) != width*height*2:
            raise ValueError('PlayStation title raster is not 384x256 BGR555')
        pixels = bytearray()
        for y in range(HEIGHT):
            sy = (2*y+1)*height//(2*HEIGHT)
            for x in range(WIDTH):
                sx = (2*x+1)*width//(2*WIDTH)
                at = (sy*width+sx)*2
                word = raw[at] | raw[at+1] << 8
                native = (word & 31) << 11 | ((word >> 5) & 31) << 6 | ((word >> 10) & 31) << 1 | 1
                pixels.extend(struct.pack('>H', native))
        output = encode_rgba16(pixels)
    else:
        raise ValueError('source must be pc or psx')
    return output, dict(source=source, source_sha256=hashlib.sha256(data).hexdigest(),
                        source_bytes=len(data), source_width=width, source_height=height,
                        source_encoding=encoding, width=WIDTH, height=HEIGHT,
                        format=fmt,
                        bytes=len(output), resize='nearest pixel centres; no dither/quantisation',
                        output_sha256=hashlib.sha256(output).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', choices=('pc', 'psx'), required=True)
    parser.add_argument('--input', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    data, manifest = cook(args.source, args.input)
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(data)
    output.with_suffix(output.suffix+'.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print(json.dumps(manifest, sort_keys=True))


if __name__ == '__main__':
    main()
