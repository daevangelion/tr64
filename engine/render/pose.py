#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Bind a posed scene to the engine's renderer; no source-data renderer."""
import argparse
from decimal import Decimal, ROUND_HALF_UP
import hashlib
import json
from pathlib import Path


def pose_macros(camera, suffix=''):
    def line(name, value):
        return f'#define TR64_RENDER_{name}{suffix} {value}\n'

    eye = [int(v) for v in camera['position']]
    if any(Decimal(v) != Decimal(q) for v, q in zip(camera['position'], eye)):
        raise ValueError('camera eye must use integer world coordinates')
    look = [int(((Decimal(v)-q)*65536).to_integral_value(rounding=ROUND_HALF_UP))
            for v, q in zip(camera['target'], eye)]
    if len(eye) != 3 or len(look) != 3 or any(not -(1<<31) <= v < (1<<31) for v in eye+look):
        raise ValueError('camera range')
    return (line('EYE', vector(eye)) + line('LOOK', vector(look)) +
            line('ROOM', str(int(camera['room']))+'u') +
            line('FLIPPED', str(int(camera.get('flipped', 0)))+'u'))


def vector(v):
    return '{'+','.join(map(str,v))+'}'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('scene', type=Path)
    p.add_argument('out', type=Path)
    mode = p.add_mutually_exclusive_group()
    mode.add_argument('--split-camera', type=Path)
    mode.add_argument('--half-view', action='store_true')
    p.add_argument('--measure', action='store_true')
    a = p.parse_args()
    raw = a.scene.read_bytes()
    scene = json.loads(raw, parse_float=Decimal)
    second = ''
    if a.split_camera:
        second_scene = json.loads(a.split_camera.read_bytes(), parse_float=Decimal)
        if second_scene['level'].lower() != scene['level'].lower():
            raise ValueError('split cameras must be from the same level')
        if second_scene['camera'].get('flipped', 0) != scene['camera'].get('flipped', 0):
            raise ValueError('split cameras must use the same flip map')
        second = pose_macros(second_scene['camera'], '_SECOND')
    rest_items = int(scene.get('rest_items', 0))
    if not 0 <= rest_items <= 65535:
        raise ValueError('rest item limit')
    texture_step = int(scene.get('texture_animation_step', 0))
    if not 0 <= texture_step <= 0xffffffff:
        raise ValueError('texture animation step')
    rest_models = scene.get('rest_models', [])
    if not isinstance(rest_models, list) or any(type(i) is not int or not 0 <= i <= 0xffffffff for i in rest_models) or len(set(rest_models)) != len(rest_models):
        raise ValueError('rest model item ids')
    live_item = int(scene.get('live_item', 0xffffffff))
    if not 0 <= live_item <= 0xffffffff:
        raise ValueError('live item id')
    posed_game_camera = int(bool(scene.get('posed_game_camera', False)))
    hud = scene.get('hud', False)
    if type(hud) is not bool:
        raise ValueError('hud must be a boolean')
    rectangle_path = scene.get('hud_rectangle_path', 'fill')
    rectangle_paths = {'fill': 0, 'one-cycle': 1, 'textured': 2}
    if not isinstance(rectangle_path, str) or rectangle_path not in rectangle_paths:
        raise ValueError('hud_rectangle_path must be fill, one-cycle or textured')
    if rectangle_path != 'fill' and not hud:
        raise ValueError('hud_rectangle_path requires hud')
    if a.half_view and posed_game_camera:
        raise ValueError('standalone half is for posed geometry scenes only')
    text = ('/* SPDX-License-Identifier: MIT */\n'
            + pose_macros(scene['camera']) + second +
            f'#define TR64_RENDER_SPLIT {int(bool(second))}\n'
            f'#define TR64_RENDER_HALF_VIEW {int(a.half_view)}\n'
            f'#define TR64_RENDER_MEASURE {int(a.measure)}\n'
            '#define TR64_RENDER_REST_ITEMS '+str(rest_items)+'u\n'
            '#define TR64_RENDER_TEXTURE_STEP '+str(texture_step)+'u\n'
            '#define TR64_RENDER_REST_MODEL_COUNT '+str(len(rest_models))+'u\n'
            '#define TR64_RENDER_REST_MODEL_IDS '+vector(rest_models or [0])+'\n'
            '#define TR64_RENDER_LIVE_ITEM '+str(live_item)+'u\n'
            '#define TR64_RENDER_POSED_GAME_CAMERA '+str(posed_game_camera)+'\n'
            '#define TR64_RENDER_HUD '+str(int(hud))+'\n'
            '#define TR64_RENDER_HUD_RECT_PATH '+str(rectangle_paths[rectangle_path])+'\n'
            '#define TR64_RENDER_LEVEL '+json.dumps(scene['level'].lower())+'\n'
            '#define TR64_RENDER_POSE '+json.dumps(a.scene.stem)+'\n'
            '#define TR64_RENDER_POSE_SHA '+json.dumps(hashlib.sha256(raw).hexdigest())+'\n')
    a.out.parent.mkdir(parents=True, exist_ok=True)
    if not a.out.exists() or a.out.read_text() != text:
        a.out.write_text(text)


if __name__ == '__main__':
    main()
