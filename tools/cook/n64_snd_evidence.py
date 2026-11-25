# SPDX-License-Identifier: MIT
"""Measure native sound banks or rendered WAVs; all outputs stay external."""
import argparse
import hashlib
import json
import math
import struct
import re
import wave
from pathlib import Path
import n64_snd


def wav_measure(path):
    with wave.open(str(path), 'rb') as wav:
        if wav.getsampwidth() != 2 or wav.getnchannels() != 2:
            raise ValueError('expected signed16 stereo WAV')
        frames, rate = wav.getnframes(), wav.getframerate()
        raw = wav.readframes(frames)
    values = struct.unpack('<' + 'h' * (len(raw) // 2), raw)
    energy = sum(n*n for n in values)
    rms = math.sqrt(energy / max(1, len(values)))
    peak = max(map(abs, values), default=0)
    return dict(path=str(path), sha256=hashlib.sha256(raw).hexdigest(), frames=frames,
                rate=rate, seconds=frames/rate, nonzero=sum(n != 0 for n in values),
                peak=peak, rms=rms, rms_dbfs=20*math.log10(rms/32768) if rms else None,
                saturated=sum(n in (-32768,32767) for n in values))


def scene_measure(path):
    text = path.read_text()
    def fields(line):
        return {k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)}
    def stats(values):
        values=sorted(values)
        return dict(count=len(values),mean=sum(values)/len(values),
                    p95=values[math.ceil(len(values)*.95)-1],maximum=values[-1]) if values else None
    work, groups, room, audio, output, rate, length = [], {}, None, None, None, 0, 0
    for line in text.splitlines():
        if line.startswith('game-render: '):
            room=fields(line)['room']
        elif line.startswith('scene-work: '):
            row=fields(line)
            if row['total_us']+row['pacing_us']+row['diagnostics_us']!=row['wall_us']:
                raise ValueError('invalid scene-work interval identity')
            work.append(dict(row,room=row.get('room',room)))
        elif line.startswith('audio-buffer: '):
            row=fields(line);groups[row['voices']]=row
        elif line.startswith('audio: '):
            audio=fields(line)
        elif line.startswith('audio-output: cart '):
            row=fields(line)
            if 'frames_per_buffer' in row:
                rate,length=row['rate'],row['frames_per_buffer']
            if 'underruns' in row:
                output=row
    if not work or not audio or not output or not rate or not length:
        raise ValueError('incomplete native audio/work evidence')
    if audio['ticks']!=len(work) or any(audio[k] for k in ('dropped','underruns','clipped')) or output['underruns']:
        raise ValueError('incomplete ticks or nonclean native audio')
    if not groups or sum(row['buffers'] for row in groups.values())*length!=output['frames']:
        raise ValueError('incomplete native buffer profile')
    omitted=len(re.findall(r'^game-tick: .*hash=omitted/perf-only ',text,re.M))
    if omitted and omitted!=len(work)+1:
        raise ValueError('partial performance-only hash omission')
    for row in groups.values():
        n=row['buffers']
        if not n: raise ValueError('empty native buffer profile')
        row['mean_mix_us']=row['mix_total_us']/n;row['mean_copy_us']=row['copy_total_us']/n
        row['cpu_us_per_30hz']=(row['mean_mix_us']+row['mean_copy_us'])*rate/(30*length)
        row['cpu_us_per_observed_tick']=(row['mix_total_us']+row['copy_total_us'])/len(work)
    return dict(path=str(path),log_sha256=hashlib.sha256(text.encode()).hexdigest(),
                perf_only=bool(omitted),ticks=len(work),rate=rate,buffer_frames=length,
                audio=audio,output=output,voice_profiles=groups,
                work=stats([r['total_us'] for r in work]),
                audio_stage=stats([r['audio_us'] for r in work]),
                diagnostics=stats([r['diagnostics_us'] for r in work]),
                rooms={str(room):stats([r['total_us'] for r in work if r['room']==room])
                       for room in sorted({r['room'] for r in work})})


def inventory(pc, psx, out):
    import src_pc
    import src_psx
    rows, unique = [], {}
    for source, root, suffix, loader in [('pc', pc, '.phd', src_pc.load),
                                          ('psx', psx, '.PSX', src_psx.load)]:
        for path in sorted(root.glob('*' + suffix)):
            level = loader(path)
            data, report, cuts = n64_snd.encode(level)
            # Match the production arena's allocation order/32-bit ABI. The
            # actual cache capacity is additionally reported by the runtime;
            # --resident-capacity is deliberately not an invented row change.
            rows.append(report)
            starts = (0,) + cuts
            ends = cuts + (len(data),)
            for start, end in zip(starts, ends):
                block = data[start:end]
                unique.setdefault(source, {}).setdefault(hashlib.sha256(block).hexdigest(), len(block))
            print(f'{source} {level.name}: metadata={report["metadata_bytes"]} '
                  f'blocks={len(starts)} PCM={report["pcm_bytes"]} frequent={report["frequent_bytes"]}', flush=True)
    result = dict(levels=rows, max_metadata_bytes=max(r['metadata_bytes'] for r in rows),
                  max_blocks=max(r['stream_blocks'] for r in rows),
                  deduplicated_bytes={s:sum(d.values()) for s,d in unique.items()})
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(result, indent=2)+'\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inventory', action='store_true')
    parser.add_argument('--pc', type=Path, default=Path('/tmp/tr64/data/pc'))
    parser.add_argument('--psx', type=Path, default=Path('/tmp/tr64/data/psx/PSXDATA'))
    parser.add_argument('--wav', type=Path, action='append', default=[])
    parser.add_argument('--scene-log', type=Path, action='append', default=[])
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    if args.inventory:
        result = inventory(args.pc, args.psx, args.out)
        print(json.dumps({k:v for k,v in result.items() if k!='levels'}))
    else:
        result = [scene_measure(path) for path in args.scene_log] if args.scene_log else [wav_measure(path) for path in args.wav]
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(result, indent=2)+'\n')
        print(json.dumps(result))
        if not args.scene_log and any(not r['nonzero'] or r['saturated'] for r in result):
            raise SystemExit('silent or saturated WAV')


if __name__ == '__main__':
    main()
