# SPDX-License-Identifier: MIT
"""Synthetic music integration only; never a real-codec acceptance claim."""
import argparse
import os
import struct
import subprocess
import wave
from pathlib import Path


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--harness',type=Path,required=True)
    p.add_argument('--fixture',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    path=a.out/'shared-bus.wav'
    r=subprocess.run(['rtk',str(a.harness),str(a.fixture)],check=True,
                     env=dict(os.environ,TR64_AUDIO_WAV=str(path)),capture_output=True,text=True)
    (a.out/'hooks.log').write_text(r.stdout+r.stderr)
    assert 'music-hooks PASS boot=1 ticks=30 pumps=30 pulls=60 frames=22050' in r.stdout
    assert 'played=0 ' in r.stdout and 'clipped=0 ' in r.stdout
    assert 'used=108624 peak=108624' in r.stdout
    with wave.open(str(path),'rb') as w:
        assert w.getparams()[:4]==(2,2,22050,22050)
        actual=struct.unpack('<44100h',w.readframes(22050))
    expected=tuple(v for i in range(22050) for v in ((-256 if i&1 else 256),)*2)
    assert actual==expected, 'music-only mono input did not reach both stereo buses'
    print('music hooks PASS: compound boot allocation, lifecycle, mono-before-clip stereo bus')


if __name__=='__main__':
    main()
