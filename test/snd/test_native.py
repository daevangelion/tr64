# SPDX-License-Identifier: MIT
"""Synthetic SN64 writer/independent-reader controls; PYTHONPATH=tools/cook."""
import argparse
import io
import json
import struct
import wave
from pathlib import Path
from ir import Level
from n64_snd import encode
from n64_snd_view import Section


def fixture(source):
    level = Level(source, 'sound-synthetic', '0'*64, 0)
    level.data = dict(sound_map=[0]+[-1]*255,
                      sound_details=[dict(sample=0,volume=32767,chance=0,flags=4)],
                      sound_sources=[])
    if source == 'pc':
        out = io.BytesIO()
        with wave.open(out, 'wb') as wav:
            wav.setparams((1,1,22050,0,'NONE','not compressed'))
            wav.writeframes(bytes(range(256))*40)
        level.samples = [out.getvalue()]
        level.sample_encoding = 'pc_wave'
    else:
        level.samples = [(bytes([12,0])+bytes([0x78]*14))*399 + bytes([12,1])+bytes([0x78]*14)]
        level.sample_encoding = 'psx_spu_adpcm'
        level.data['vab_tones'] = [(0,0,127,64,84,0,0,127,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0)]
    return level


def reject(data, offset, value):
    bad = bytearray(data)
    struct.pack_into('>I', bad, offset, value)
    try:
        Section(bytes(bad))
    except ValueError:
        return
    raise AssertionError(f'invalid native sound accepted at{offset}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    for source in ('pc','psx'):
        data, report, cuts = encode(fixture(source))
        section = Section(data)
        assert len(section.samples) == 1 and not section.unavailable
        expected = [((n-128)*256) for n in bytes(range(256))*40] if source=='pc' else [-8,7]*5600
        assert section.pcm(0) == expected
        assert cuts[0] == report['metadata_bytes']
        assert report['stream_blocks'] == len(cuts) + 1
        assert all(b-a<=4096 for a,b in zip(cuts,cuts[1:]+(len(data),)))
        for offset, value in ((4,2),(8,len(data)+16),(12,64),(16,257),(20,16),
                              (24,32),(36,8192),(64,0),(68,0),(72,44100),
                              (76,3),(80,len(expected)),(84,0),(88,2),(92,1)):
            reject(data,offset,value)
        (args.out/(source+'.sn64')).write_bytes(data)
        streamed = bytearray(data)
        struct.pack_into('>I', streamed, 88, 0)
        Section(bytes(streamed))
        (args.out/(source+'-stream.sn64')).write_bytes(streamed)
    spu, _, _ = encode(fixture('psx'), psx_encoding='spu')
    assert Section(spu).pcm(0) == [-8,7]*5600
    (args.out/'psx-spu.sn64').write_bytes(spu)
    streamed = bytearray(spu)
    struct.pack_into('>I', streamed, 88, 0)
    (args.out/'psx-spu-stream.sn64').write_bytes(streamed)
    missing = fixture('psx')
    missing.data['sound_map'][1] = 1
    missing.data['sound_details'].append(dict(sample=1,volume=32767,chance=0,flags=4))
    data, _, _ = encode(missing)
    assert Section(data).unavailable == [(1,1)]
    # A present sample cannot be declared unavailable and muted.
    bad=bytearray(data);struct.pack_into('>I',bad,100,0)
    try:
        Section(bytes(bad))
    except ValueError:
        pass
    else:
        raise AssertionError('present sample was muted by unavailable table')
    for field,value in [('emitters',65),('chance',1),('flags',8),('flags',8196),('flags',16388)]:
        level=fixture('pc')
        emitter=dict(x=0,y=0,z=1024,sound=0,flags=128)
        level.data['sound_sources']=[emitter]*(value if field=='emitters' else 1)
        if field!='emitters':level.data['sound_details'][0][field]=value
        try:
            encode(level)
        except ValueError:
            pass
        else:
            raise AssertionError('unsupported ambient source contract was accepted')
    print(json.dumps(dict(sources=2, malformed_controls=38, independent_pcm=True, unavailable=True)))


if __name__ == '__main__':
    main()
