# SPDX-License-Identifier: MIT
"""Exercise the actual renderer scene generator's UI opt-ins."""
import argparse
import json
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    baseline = dict(level='synthetic', camera=dict(room=0, position=[0, 0, 0], target=[0, 0, 100]))
    def run(data):
        path = out/'scene.json'
        path.write_text(json.dumps(data))
        return subprocess.run(['rtk', sys.executable, str(root/'engine/render/pose.py'),
                               str(path), str(out/'pose.h')], capture_output=True, text=True)
    result = run(baseline)
    assert not result.returncode and '#define TR64_RENDER_HUD 0\n' in (out/'pose.h').read_text()
    for index, name in enumerate(('fill', 'one-cycle', 'textured')):
        result = run(dict(baseline, hud=True, hud_rectangle_path=name))
        assert not result.returncode
        assert f'#define TR64_RENDER_HUD_RECT_PATH {index}\n' in (out/'pose.h').read_text()
    for values in (dict(hud='yes'), dict(hud=True, hud_rectangle_path='bogus'),
                   dict(hud=True, hud_rectangle_path=[]), dict(hud_rectangle_path='textured')):
        result = run(dict(baseline, **values))
        assert result.returncode and 'ValueError' in result.stderr
    print('UI scene selectors PASS: default off, 3 rectangle paths, 4 rejecting opt-in controls')


if __name__ == '__main__':
    main()
