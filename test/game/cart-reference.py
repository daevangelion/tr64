#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Verify a full host/oracle scene in pipes, then check cart hashes via a memfd."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
sys.dont_write_bytecode = True
from compare import first_difference, validate
from stream import Run


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--executable', required=True, type=Path)
    ap.add_argument('--oracle', required=True, type=Path)
    ap.add_argument('--level', required=True, type=Path)
    ap.add_argument('--masks', required=True, type=Path)
    ap.add_argument('--pack', required=True, type=Path)
    ap.add_argument('--build-dir', required=True, type=Path)
    ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--scene-meta', type=Path)
    ap.add_argument('--schema',type=int,choices=(2,3),default=2)
    ap.add_argument('--save-tick',type=int)
    ap.add_argument('--load-tick',type=int)
    args = ap.parse_args()
    words = args.masks.read_text().split()
    ticks = len(words) - (6 if words and words[0] == 'start' else 0)
    if not 1 <= ticks <= 600: ap.error('scene must contain 1..600 ticks')
    if (args.save_tick is None)!=(args.load_tick is None):
        ap.error('save/load scene requires both boundary ticks')
    if args.save_tick is not None and not 0<=args.save_tick<args.load_tick<=ticks:
        ap.error('save must precede load, both inside the scene')
    if args.save_tick is not None and args.schema!=3:
        ap.error('M5 save/load scenes require schema 3')
    args.out.mkdir(parents=True, exist_ok=True)
    identity = dict(executable_sha256=sha(args.executable), oracle_sha256=sha(args.oracle),
                    level_sha256=sha(args.level), masks_sha256=sha(args.masks), pack_sha256=sha(args.pack),
                    schema=args.schema,save_tick=args.save_tick,load_tick=args.load_tick)
    source_command=['rtk',str(args.oracle),'--level',str(args.level),'--input',str(args.masks),'--ticks',str(ticks)]
    native_command=['rtk',str(args.executable),'--game-level',args.level.stem.lower(),
                    '--game-input',str(args.masks),'--game-ticks',str(ticks),'--game-trace-only']
    if args.schema==3:
        source_command+=['--trace-schema','3']
        native_command+=['--game-trace-schema','3']
    if args.save_tick is not None:
        source_command+=['--save-tick',str(args.save_tick),'--load-tick',str(args.load_tick)]
        native_command+=['--game-save-tick',str(args.save_tick),'--game-load-tick',str(args.load_tick)]
    source=Run(source_command,'--out')
    try:
        native=Run(native_command,'--game-trace')
    except BaseException:
        source.finish(kill=True)
        raise
    source_done = native_done = False
    fd = os.memfd_create('tr64-verified-cart-reference', os.MFD_CLOEXEC)
    reference = Path(f'/proc/{os.getpid()}/fd/{fd}')
    count = 0
    digest = hashlib.sha256()
    transitions=[]
    previous_level=None
    coverage=dict(first_ring_open=None,ring_closed_after=None,min_health=None,count_changes=[])
    previous_counts=None
    try:
        with os.fdopen(os.dup(fd), 'wb') as destination:
            while True:
                expected, actual = source.trace.readline(), native.trace.readline()
                if not expected or not actual:
                    if expected != actual: raise ValueError(f'tick={count}: unequal scene termination')
                    break
                if expected != actual:
                    difference = first_difference(json.loads(expected), json.loads(actual))
                    raise ValueError(f'tick={count}: {difference}')
                row=json.loads(actual)
                validate(row, actual.decode('ascii'), count, 'verified cart host reference',schema=args.schema)
                if args.schema==3:
                    if previous_level is not None and row['level']!=previous_level:
                        transitions.append(dict(tick=count,previous=previous_level,next=row['level']))
                    previous_level=row['level']
                    inventory=row['inventory']
                    health=row['lara']['health'] if row['lara'] else None   # a cutscene level has no Lara
                    if inventory['state'] and coverage['first_ring_open'] is None:
                        coverage['first_ring_open']=count
                    if not inventory['state'] and coverage['first_ring_open'] is not None \
                            and coverage['ring_closed_after'] is None:
                        coverage['ring_closed_after']=count
                    if health is not None and (coverage['min_health'] is None or health<coverage['min_health']):
                        coverage['min_health']=health
                    counts=inventory['counts']
                    if previous_counts is not None and counts!=previous_counts and len(coverage['count_changes'])<32:
                        coverage['count_changes'].append(dict(tick=count,
                            slots={str(n):[a,b] for n,(a,b) in enumerate(zip(previous_counts,counts)) if a!=b}))
                    previous_counts=counts
                    coverage.update(final_health=health,medi_used=row['save']['mediUsed'],
                                    ammo_used=row['save']['ammoUsed'],pickups=row['save']['pickups'])
                destination.write(actual)
                digest.update(actual)
                count += 1
        source_code, source_log = source.finish()
        source_done = True
        native_code, native_log = native.finish()
        native_done = True
        if source_code or native_code or count != ticks + 1:
            raise ValueError(f'full reference failed: records={count} source={source_code} native={native_code}\n{source_log[-400:]}\n{native_log[-400:]}')
        os.lseek(fd, 0, os.SEEK_SET)
        spec = importlib.util.spec_from_file_location('cart_scene_routes', Path(__file__).with_name('routes.py'))
        routes = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(routes)
        route = args.out / 'scene.route'
        route_text=routes.route(args.level.stem.lower(), args.masks, reference,args.schema)
        # A route admits one `observe log`: require every fixture service in tick order.
        services=[]
        if args.save_tick is not None:
            services+=[f'^game-service: tick={args.save_tick} save bytes=[1-9][0-9]*$',
                       f'^game-service: tick={args.load_tick} load bytes=[1-9][0-9]*$']
        for change in transitions:
            services.append(f'^game-service: tick={change["tick"]} transition '
                            f'from={change["previous"]} to={change["next"]} carry_bytes=[1-9][0-9]* reason=[0-5]$')
        if services:
            route_text+='observe log "(?s)'+'.*'.join(services)+'"\n'
        route.write_text(route_text)
        identity.update(records=count, trace_sha256=digest.hexdigest(), reference=str(reference),
                        transitions=transitions,coverage=coverage,storage='memory-only memfd, live through checker completion')
        (args.out / 'reference.json').write_text(json.dumps(identity, indent=2) + '\n')
        environment = dict(os.environ, BUILD_DIR=str(args.build_dir.resolve()),
                           TR64_LEVEL_PACK=str(args.pack.resolve()), ARES_EXPANSION_PAK='false')
        meta = args.scene_meta
        if meta is None:
            # The bind-time posed frame only names the level; the live follow camera replaces it.
            if not words or words[0] != 'start':
                raise ValueError('a scene without --scene-meta needs a start placement to pose the bind frame')
            room, x, y, z = map(int, words[1:5])
            meta = args.out / 'scene-meta.json'
            meta.write_text(json.dumps(dict(level=args.level.stem.upper(), camera=dict(room=room,
                position=[x, y - 768, z], target=[x, y - 768, z + 100]))) + '\n')
        environment['TR64_RENDER_SCENE'] = str(meta.resolve())
        if args.schema==3:
            sources=environment.get('TR64_SCENE_TEST_SOURCES','').split()
            for source_file in ('test/game/cart_actions.c','test/game/save-io.c'):
                if source_file not in sources:sources.append(source_file)
            environment['TR64_SCENE_TEST_SOURCES']=' '.join(sources)
            scene_name=next(line.split()[1] for line in route_text.splitlines() if line.startswith('scene '))
            generated=args.build_dir.resolve()/'scene-builds'/scene_name/'generated'
            generated.mkdir(parents=True,exist_ok=True)
            never='UINT32_MAX'
            header=generated/'cart_scene.h'
            header.write_text('/* Generated by test/game/cart-reference.py. */\n'
                f'#define TR64_CART_SAVE_TICK {never if args.save_tick is None else str(args.save_tick)+"u"}\n'
                f'#define TR64_CART_LOAD_TICK {never if args.load_tick is None else str(args.load_tick)+"u"}\n')
        result = subprocess.run(['rtk', 'test/run/scene.sh', '--ares', '--checks', 'test/game/scene-checks.py', str(route)],
                                env=environment, pass_fds=(fd,), timeout=1800)
        if result.returncode: raise ValueError(f'cart scene failed: exit={result.returncode}')
        if identity['executable_sha256'] != sha(args.executable) or identity['oracle_sha256'] != sha(args.oracle):
            raise ValueError('verified producer changed during cart gate')
        print(f'cart reference PASS records={count} sha256={digest.hexdigest()} metadata={args.out / "reference.json"}', flush=True)
    except Exception:
        if not source_done: source.finish(kill=True)
        if not native_done: native.finish(kill=True)
        raise
    finally:
        os.close(fd)


if __name__ == '__main__':
    main()
