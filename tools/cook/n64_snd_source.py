# SPDX-License-Identifier: MIT
"""Independent source audio decoding and inventory; no retail bytes committed.

PCM RIFF and Sony SPU ADPCM facts: Microsoft RIFF WAVE specification and
https://psx-spx.consoledev.net/soundprocessingunitspu/#4bit-adpcm-samples.
"""
import argparse
import hashlib
import io
import json
import struct
import wave
from pathlib import Path
from binary import FormatError

FILTERS = ((0, 0), (60, 0), (115, -52), (98, -55), (122, -60))


def pcm_wave(data):
    """Read PCM chunks strictly, including RIFF alignment and unknown chunks."""
    if len(data) < 12 or data[:4] != b'RIFF' or data[8:12] != b'WAVE':
        raise FormatError('sample is not RIFF WAVE')
    end = struct.unpack_from('<I', data, 4)[0] + 8
    if end != len(data):
        raise FormatError('RIFF sample length mismatch')
    pos, fmt, pcm = 12, None, None
    while pos < end:
        if pos + 8 > end:
            raise FormatError('truncated RIFF chunk')
        kind, size = struct.unpack_from('<4sI', data, pos)
        pos += 8
        if size > end - pos:
            raise FormatError('RIFF chunk exceeds sample')
        chunk = data[pos:pos + size]
        if kind == b'fmt ':
            if fmt is not None or size < 16:
                raise FormatError('invalid RIFF format chunk')
            fmt = struct.unpack_from('<HHIIHH', chunk)
        elif kind == b'data':
            if pcm is not None:
                raise FormatError('duplicate PCM chunk')
            pcm = chunk
        pos += size + (size & 1)
    if fmt is None or pcm is None or pos != end:
        raise FormatError('incomplete RIFF sample')
    code, channels, rate, byte_rate, align, bits = fmt
    if code != 1 or channels != 1 or bits not in (8, 16) or not rate:
        raise FormatError(f'unsupported PCM format {fmt}')
    if align != bits // 8 or byte_rate != rate * align or len(pcm) % align:
        raise FormatError('inconsistent PCM format')
    values = ([((n - 128) << 8) for n in pcm] if bits == 8 else
              list(struct.unpack('<' + 'h' * (len(pcm) // 2), pcm)))
    return values, rate, None


def spu_adpcm(data):
    """Decode each little-nibble-first SPU block with signed predictor history.

    Return source loop marker as well as all samples, including the terminal
    block; caller chooses loop policy from gameplay mode, not SPU marker alone.
    """
    if not data or len(data) % 16:
        raise FormatError('SPU sample must contain whole blocks')
    pcm, h1, h2, loop = [], 0, 0, None
    for offset in range(0, len(data), 16):
        header, flags = data[offset:offset + 2]
        predictor, shift = header >> 4, header & 15
        if predictor > 4 or shift > 12 or flags > 7:
            raise FormatError('invalid SPU block parameters')
        if flags & 4:
            loop = len(pcm)
        a, b = FILTERS[predictor]
        for packed in data[offset + 2:offset + 16]:
            for nibble in (packed & 15, packed >> 4):
                signed = nibble if nibble < 8 else nibble - 16
                value = ((signed << 12) >> shift) + ((h1 * a + h2 * b + 32) >> 6)
                value = min(32767, max(-32768, value))
                pcm.append(value)
                h2, h1 = h1, value
    if not data[-15] & 1:
        raise FormatError('SPU sample has no terminal end flag')
    return pcm, loop


def decode(level, index):
    if level.sample_encoding == 'pc_wave':
        return pcm_wave(level.samples[index])
    if level.sample_encoding != 'psx_spu_adpcm':
        raise FormatError('unsupported source audio encoding')
    # OpenLara 2333adef src/sound.h:500 explicitly decodes VAG at11025 Hz.
    # The source VAB centre84/fine0 and PC length correspondence agree.
    tones = level.data['vab_tones']
    if index * 16 >= len(tones):
        raise FormatError('VAG lacks corresponding program')
    tone = tones[index * 16]
    if tone[18] != index or tone[19] != index + 1 or tone[4:6] != (84, 0):
        raise FormatError('unsupported nonidentity VAB tone mapping/tuning')
    values, loop = spu_adpcm(level.samples[index])
    return values, 11025, loop


def write_wave(path, values, rate):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), 'wb') as out:
        out.setparams((1, 2, rate, 0, 'NONE', 'not compressed'))
        out.writeframes(struct.pack('<' + 'h' * len(values), *values))


def survey(level, dumps=None):
    sample_rows = []
    details = level.data['sound_details']
    sound_map = level.data['sound_map']
    for i, encoded in enumerate(level.samples):
        values, rate, marker = decode(level, i)
        ids = [sound for sound, detail in enumerate(sound_map) if detail >= 0 and
               details[detail]['sample'] <= i < details[detail]['sample'] +
               max(1, (details[detail]['flags'] >> 2) & 15)]
        modes = sorted({details[sound_map[n]]['flags'] & 3 for n in ids})
        sample_rows.append(dict(sample=i, source_bytes=len(encoded), frames=len(values),
                                rate=rate, seconds=len(values)/rate, modes=modes,
                                sound_ids=ids, spu_loop_start=marker,
                                decoded_sha256=hashlib.sha256(struct.pack('<' + 'h'*len(values), *values)).hexdigest()))
        if dumps is not None:
            write_wave(Path(dumps)/f'{i:03d}.wav', values, rate)
    unavailable = []
    for sound, detail in enumerate(sound_map):
        if detail < 0:
            continue
        d = details[detail]
        for sample in range(d['sample'], d['sample'] + max(1, (d['flags'] >> 2) & 15)):
            if sample >= len(level.samples):
                unavailable.append(dict(sound=sound, detail=detail, sample=sample))
    return dict(source=level.source, level=level.name, sha256=level.sha256,
                sample_count=len(sample_rows), source_bytes=sum(len(s) for s in level.samples),
                seconds=sum(s['seconds'] for s in sample_rows),
                rates=sorted({s['rate'] for s in sample_rows}),
                samples=sample_rows, unavailable=unavailable,
                ambient_sources=level.data['sound_sources'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pc', type=Path, default=Path('/tmp/tr64/data/pc'))
    parser.add_argument('--psx', type=Path, default=Path('/tmp/tr64/data/psx/PSXDATA'))
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--dump', type=Path)
    args = parser.parse_args()
    import src_pc
    import src_psx
    rows = []
    for source, root, suffix, loader in [('pc', args.pc, '.phd', src_pc.load),
                                          ('psx', args.psx, '.PSX', src_psx.load)]:
        for path in sorted(root.glob('*' + suffix)):
            level = loader(path)
            rows.append(survey(level, args.dump/source/level.name if args.dump else None))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(rows, indent=2) + '\n')
    for row in rows:
        print(f"{row['source']:3} {row['level']:8} {row['sample_count']:3} "
              f"{row['source_bytes']:8} {row['seconds']:8.3f}s {row['rates']} "
              f"missing={row['unavailable']}")


if __name__ == '__main__':
    main()
