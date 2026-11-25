# SPDX-License-Identifier: MIT
"""SN64 ABI1 cook. See docs/formats/n64-snd.md; source bytes stay external."""
import argparse
import hashlib
import json
import struct
from pathlib import Path
import n64_pack as pack
from n64_snd_source import FILTERS as SPU_FILTERS, decode, spu_adpcm, survey, write_wave
from n64_validate import admit, limit

KIND = 0x40000
HEADER = struct.Struct('>4s15I')
SAMPLE = struct.Struct('>8I')
MISSING = struct.Struct('>2I')
BLOCK = 4096
# Source selectors, not a claim of measured use frequency. Load in this stable
# priority order while cache capacity permits. Large ambience remains streamed.
FREQUENT = frozenset(range(0, 11)) | frozenset((27, 30, 31, 33, 37, 43, 44, 45, 48, 50))


def tool_hash():
    files = ('n64_snd.py', 'n64_snd_source.py', 'n64_snd_view.py')
    root = Path(__file__).parent
    return hashlib.sha256(b''.join((root / p).read_bytes() for p in files)).hexdigest()


def select_external_samples(level):
    """Compact the shared-bank selection without changing sound IDs or variant order."""
    data = level.data
    selected = sorted(set(data['sound_map']) - {-1})
    details = [data['sound_details'][i] for i in selected]
    slots = sorted({i for d in details for i in range(
        d['sample'], d['sample'] + max(1, (d['flags'] >> 2) & 15))})
    slot_index = {i: j for j, i in enumerate(slots)}
    detail_index = {i: j for j, i in enumerate(selected)}
    data['sound_map'] = [detail_index[i] if i != -1 else -1 for i in data['sound_map']]
    data['sound_details'] = [dict(d, sample=slot_index[d['sample']]) for d in details]
    data['sample_ordinals'] = [data['sample_ordinals'][i] for i in slots]
    level.samples = [level.samples[i] for i in slots]


def downsample_2to1(pcm):
    """Clean 3-tap [1, 2, 1]/4 anti-aliased 2:1 decimation (22050 -> 11025 Hz)."""
    n = len(pcm)
    if n < 2:
        return list(pcm)
    out = [(pcm[0] * 3 + pcm[1] + 2) >> 2]
    for i in range(2, n - 1, 2):
        out.append((pcm[i - 1] + (pcm[i] << 1) + pcm[i + 1] + 2) >> 2)
    if n % 2 == 1 and n >= 3:
        out.append((pcm[-2] + pcm[-1] * 3 + 2) >> 2)
    return out


def encode_spu(pcm):
    """Encode 11025 Hz mono signed-16 PCM into 16-byte SPU-ADPCM blocks (SN64 encoding 3)."""
    rem = len(pcm) % 28
    if rem:
        pcm = list(pcm) + [0] * (28 - rem)
    elif not pcm:
        pcm = [0] * 28
    out = bytearray(len(pcm) // 28 * 16)
    h1 = h2 = 0
    pos = 0
    for off in range(0, len(pcm), 28):
        blk = pcm[off:off + 28]
        best_err = 1 << 62
        best_hdr = 0
        best_nib = None
        best_h1 = best_h2 = 0
        for p, (a, b) in enumerate(SPU_FILTERS):
            p1, p2 = h1, h2
            max_r = 0
            for s in blk:
                pred = (p1 * a + p2 * b + 32) >> 6
                r = abs(s - pred)
                if r > max_r:
                    max_r = r
                p2, p1 = p1, s
            sh = 12
            while sh > 0 and (7 << (12 - sh)) < max_r:
                sh -= 1
            shifts = (sh, sh - 1) if sh > 0 else (0,)
            for shift in shifts:
                step = 4096 >> shift
                half = step >> 1
                t1, t2 = h1, h2
                err = 0
                nibs = [0] * 28
                for i, s in enumerate(blk):
                    pred = (t1 * a + t2 * b + 32) >> 6
                    res = s - pred
                    q = (res + half) // step if res >= 0 else -((-res + half) // step)
                    if q > 7:
                        q = 7
                    elif q < -8:
                        q = -8
                    dec = pred + q * step
                    if dec > 32767:
                        dec = 32767
                    elif dec < -32768:
                        dec = -32768
                    d = s - dec
                    err += d * d
                    if err >= best_err:
                        break
                    nibs[i] = q & 15
                    t2, t1 = t1, dec
                else:
                    best_err = err
                    best_hdr = (p << 4) | shift
                    best_nib = nibs
                    best_h1, best_h2 = t1, t2
        out[pos] = best_hdr
        out[pos + 1] = 0
        for i in range(0, 28, 2):
            out[pos + 2 + (i >> 1)] = best_nib[i] | (best_nib[i + 1] << 4)
        pos += 16
        h1, h2 = best_h1, best_h2
    out[-15] = 1  # terminal end flag required by SN64 SPU decoder
    return bytes(out), len(pcm)


def encode(level, dumps=None, psx_encoding='pcm'):
    report = survey(level)
    game = level.reserved.get('game', 1)
    sequel = game in (2, 3)
    tr3 = game == 3
    if len(level.data['sound_sources']) > limit('sound_emitters', game):
        raise ValueError('SN64 ambient emitter capacity64 exceeded')
    for emitter in (() if sequel else level.data['sound_sources']):
        index = level.data['sound_map'][emitter['sound']]
        if index < 0:
            continue
        detail = level.data['sound_details'][index]
        flags = detail['flags']
        if (flags >> 2 & 15) > 1 or flags & 0x6000 or detail['chance']:
            raise ValueError('ambient variation outside measured TR1 source contract')
    if psx_encoding not in ('pcm', 'spu'):
        raise ValueError('PSX sound encoding must be pcm or spu')
    missing = report['unavailable']
    metadata = pack.align(HEADER.size + len(level.samples) * SAMPLE.size + len(missing) * MISSING.size)
    result = bytearray(metadata)
    rows, cuts = [], [metadata]
    expected_spu_pcm = [] if tr3 else None
    for index in range(len(level.samples)):
        pcm, rate, marker = decode(level, index)
        if tr3:
            if rate == 22050:
                pcm = downsample_2to1(pcm)
                rate = 11025
            elif rate != 11025:
                raise ValueError(f'{level.name}: unsupported TR3 sample rate {rate}')
            payload, frames = encode_spu(pcm)
            encoding = 3
            expected_spu_pcm.append(spu_adpcm(payload)[0])
        elif level.source == 'psx' and psx_encoding == 'spu':
            encoding, payload, frames = 3, level.samples[index], len(pcm)
        else:
            encoding = 1 if level.source == 'pc' and all(v % 256 == 0 for v in pcm) else 2
            payload = (bytes((v >> 8) & 255 for v in pcm) if encoding == 1 else
                       struct.pack('>' + 'h' * len(pcm), *pcm))
            frames = len(pcm)
        offset = len(result)
        flags = int(not sequel and bool(FREQUENT.intersection(report['samples'][index]['sound_ids'])))
        # TR1 mode2 repeats the whole sample. SPU loop markers are kept
        # in source survey; no guessed cross-source loop-point normalisation.
        row = (offset, frames, rate, encoding, 0, len(payload), flags, 0)
        SAMPLE.pack_into(result, HEADER.size + index * SAMPLE.size, *row)
        result += payload
        result += bytes(pack.align(len(result)) - len(result))
        cuts.extend(range(offset + BLOCK, len(result), BLOCK))
        cuts.append(len(result))
        rows.append(row)
        if dumps:
            ids = report['samples'][index]['sound_ids']
            for sound in ids:
                write_wave(Path(dumps) / f'sound-{sound:03d}-sample-{index:03d}.wav', pcm, rate)
    missing_offset = HEADER.size + len(rows) * SAMPLE.size
    for index, item in enumerate(missing):
        MISSING.pack_into(result, missing_offset + index * MISSING.size, item['sound'], item['sample'])
    HEADER.pack_into(result, 0, b'SN64', 1, len(result), metadata, len(rows), SAMPLE.size,
                     HEADER.size, len(missing), missing_offset, BLOCK, *([0] * 6))
    from n64_snd_view import Section
    checked = Section(bytes(result))
    for index, row in enumerate(rows):
        expected = expected_spu_pcm[index] if tr3 else decode(level, index)[0]
        if checked.pcm(index) != expected:
            raise ValueError(f'{level.name}: sound independent decoder mismatch {index}')
    report.update(section_bytes=len(result), metadata_bytes=metadata,
                  stream_blocks=len(set(cuts)),
                  pcm_bytes=sum(row[1] * (1 if row[3] == 1 else 2) for row in rows),
                  encoded_bytes=sum(row[5] for row in rows),
                  frequent_bytes=sum(pack.align(row[5]) for row in rows if row[6]),
                  section_sha256=hashlib.sha256(result).hexdigest(), cook_sha256=tool_hash())
    return bytes(result), report, tuple(sorted(set(cuts[:-1])))


def cook(level, dumps=None, psx_encoding='pcm'):
    data, report, cuts = encode(level, dumps, psx_encoding)
    admit(level.reserved.get('game',1), sound_samples=report['sample_count'],
          sound_metadata=report['metadata_bytes'], sound_blocks=report['stream_blocks'],
          sound_missing=len(report['unavailable']))
    section = pack.Section(KIND, data, header_bytes=report['metadata_bytes'],
                           chunk_bytes=BLOCK, compression='none', boundaries=cuts)
    return section, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, choices=('pc', 'psx'))
    parser.add_argument('--input', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--dump', type=Path)
    parser.add_argument('--psx-encoding', choices=('pcm', 'spu'), default='pcm')
    args = parser.parse_args()
    import src_pc
    import src_psx
    level = (src_pc if args.source == 'pc' else src_psx).load(args.input)
    section, report = cook(level, args.dump, args.psx_encoding)
    key = hashlib.sha256((report['cook_sha256'] + level.sha256 + report['section_sha256']).encode()).hexdigest()
    root = args.out / key
    root.mkdir(parents=True, exist_ok=True)
    for name, data in [('section.bin', section.data),
                       ('manifest.json', (json.dumps(dict(report, content_key=key), indent=2) + '\n').encode())]:
        path = root / name
        if path.exists() and path.read_bytes() != data:
            raise ValueError(f'content key collision: {path}')
        if not path.exists():
            path.write_bytes(data)
    print(json.dumps(dict(path=str(root), content_key=key, metadata_bytes=report['metadata_bytes'],
                          section_bytes=report['section_bytes'], frequent_bytes=report['frequent_bytes'])))


if __name__ == '__main__':
    main()
