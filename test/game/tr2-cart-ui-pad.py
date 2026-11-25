#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run the shipping controller path at 4 MiB and retain ring/HUD frames."""
import argparse
from pathlib import Path
import re
import shutil

from drive import Drive, IDLE, word
from guest_identity import expected_identity, require_identity


def run(build, output):
    root = Path(__file__).resolve().parents[2]
    driver = Drive(root, build/'tr64.z64', output, timeout=150, two_pads=True)
    shot_pattern = r'^\[ares\] screenshot (\S+\.ppm) '
    drawn = r'^game-render:[^\n]*\n(?:(?!game-render:)[^\n]*\n)*'
    shots = {}

    def screenshot(name, after):
        end = driver.wait(shot_pattern, after=after)
        shots[name] = Path(re.search(shot_pattern, driver.text()[after:end], re.M)[1]).name

    def release():
        driver.wait_sample(1, driver.inject_idle_reads('release pad before next edge'))

    ready = r'^ui: menu frame=\d+ state=4 page=5 passport=0 item={}$'
    try:
        live = driver.wait(r'^play-tr2: ready=wall hz=30$')
        require_identity(driver.text(), expected_identity(build))
        if 'hardware_rdram=4194304 sdk_rdram=4194304' not in driver.text():
            raise ValueError('live cart did not run at 4 MiB')
        screen = driver.press_until_effect(word('START'), 'open main ring', ready.format(0))
        driver.wait(r'^pool: code .* used=\d+ peak=\d+ kind=2$', after=live)
        release()
        screenshot('ring', driver.wait(r'^game-render:', after=screen))
        closed = driver.press_until_effect(word('START'), 'cancel inventory',
                                           r'^play-tr2: ring closed type=0 clock=[1-9]\d*$')
        driver.wait(r'^code: unload front released=\d+$', after=closed)
        release()
        driver.press_until_effect(word('START'), 'reopen main ring', ready.format(0))
        release()
        for i in (1, 2):
            spin = driver.press_until_effect(word('LEFT'), f'rotate to ordinal {i}',
                                             rf'^ui: menu frame=\d+ state=6 page=5 passport=0 item={i-1}$')
            release()
            driver.wait(ready.format(i), after=spin)
        selecting = driver.press_until_effect(word('B'), 'request shotgun',
            r'^ui: menu frame=\d+ state=7 page=5 passport=0 item=2$')
        release()
        selected = driver.wait(r'^play-tr2: ring closed type=158 clock=[1-9]\d*$', after=selecting)
        driver.wait(f'(?:{drawn}){{50}}', after=selected)
        driver.wait(r'^code: load front pages=\d+ pairs=\d+ .*', after=selected)
        screenshot('hud', len(driver.text()))
    finally:
        driver.stop()
    require_identity(driver.text(), expected_identity(build))
    for name, shot in shots.items():
        image = driver.vi / shot
        if not image.read_bytes().startswith(b'P6\n320 240\n255\n'):
            raise ValueError(f'invalid 320x240 capture: {image}')
        shutil.copyfile(image, driver.out/f'{name}.ppm')
    print('Controller opened, cancelled and reopened the ring; shotgun choice and ammo HUD observed.')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('build', type=Path)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    run(a.build.resolve(), a.output.resolve())
