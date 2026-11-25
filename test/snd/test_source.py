# SPDX-License-Identifier: MIT
"""Run from repo root with PYTHONPATH=tools/cook; no import-path rewriting."""
import argparse
import io
import json
import struct
import subprocess
import wave
from pathlib import Path
from binary import FormatError
from n64_snd_source import pcm_wave, spu_adpcm


def rejects(fn, data):
    try:
        fn(data)
    except (FormatError, ValueError):
        return
    raise AssertionError('malformed source accepted')


def check(out):
    out.mkdir(parents=True, exist_ok=True)
    raw = io.BytesIO()
    with wave.open(raw, 'wb') as wav:
        wav.setparams((1, 1, 22050, 0, 'NONE', 'not compressed'))
        wav.writeframes(bytes([0, 128, 255, 128]))
    assert pcm_wave(raw.getvalue()) == ([-32768, 0, 32512, 0], 22050, None)
    rejects(pcm_wave, raw.getvalue()[:-1])
    rejects(pcm_wave, b'RIFF'+bytes(40))
    # Predictor0 has no history. Every packed0x78 gives low nibble-8 then+7;
    # shift12 leaves signed integers, shift0 yields -32768/+28672.
    block = bytes([12, 1]) + bytes([0x78] * 14)
    expected = [-8, 7] * 14
    assert spu_adpcm(block) == (expected, None)
    assert spu_adpcm(bytes([0, 5]) + bytes([0x78] * 14)) == ([-32768,28672]*14, 0)
    rejects(spu_adpcm, block[:-1])
    rejects(spu_adpcm, bytes([0x50,1])+bytes(14))
    rejects(spu_adpcm, bytes([13,1])+bytes(14))
    rejects(spu_adpcm, bytes([0,0])+bytes(14))
    # Predictor1: history+constant excitation, hardware +32 before >>6.
    predictor = bytes([0x1c,1])+bytes([0x11]*14)
    values, _ = spu_adpcm(predictor)
    assert values[:5] == [1,2,3,4,5]
    # Independent FFmpeg validates format/nibble/predictor conventions on a
    # synthetic predictor0 case exactly; hardware-rounding predictor1 is
    # checked above rather than pretending FFmpeg's truncation is bit-exact.
    vag = b'VAGp'+struct.pack('>IIII',0x20,0,len(block),11025)+bytes(28)+block
    encoded, decoded = out/'independent.vag', out/'independent.pcm'
    encoded.write_bytes(vag)
    subprocess.run(['rtk','ffmpeg','-v','error','-y','-i',str(encoded),'-f','s16le',str(decoded)], check=True)
    assert decoded.read_bytes() == struct.pack('<28h',*expected)
    report = dict(pcm=True, spu_predictor_zero=True, spu_predictor_history=True,
                  malformed_controls=6, independent_ffmpeg='predictor0 exact')
    (out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    check(parser.parse_args().out)


if __name__ == '__main__':
    main()
