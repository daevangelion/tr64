#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""LEVEL10A's cabin (item 84), oracle against port, schema 4 (oracle host.patch hunk 19; docs/specs/wall/objects-missing.md 18).

The reference never built a controller for the cabin, so the three fuses (puzzle holes 26, 27, 28 in room 30, each
a key trigger of mask 31 that lists the cabin) did nothing. Cases:

fuses     Lara is placed in front of hole 26 (yaw -32768, which the source's int16 `abs` lets through; later holes: she
          stands at +327 on the item's +z side facing +z, within 30 degrees of its angle) with three fuses (`give 110 3`, placed state) and inserts all three from
          the inventory ring, walking between the holes (1,024 units apart, the keyhole limit box is +-200 across and
          312..512 deep, +-30 degrees: the walk is closed loop and the script refuses a placement outside it).
          Each insertion advances the cabin one stage: 0 -> 1 -> 2 -> 3 (animations 304, 305, 306), then state 4: the
          flip is requested and applied (`save.flipped` 0 -> 1, the world hash changes) and the cabin item is gone
          (invisible, not collidable). One script of about 1,500 ticks (LEVEL10A allows 4096).
flip      what the flip opens (cooked data, `rooms`): the six sectors of room 30 over the shaft (cells x 3..4, z 4..6)
          have `roomBelow` 94 (a hole: the floor below is room 94's, 1,280 units lower); in the alternate room 29
          the same cells have no room below, so they are floor at room 30's level. The portal 30 <-> 94 is the only edge of the portal graph the flip removes; reachability of the
          98 rooms the level start sees is the same either way, so the flip changes where Lara can stand, not which
          rooms she can reach.
          Room 30 is a pit room (nearly every sector has a room below); the six sectors are an island in it, two sectors
          (2,048 units) beyond the ledge Lara stands on, reached only by a jump the closed-loop driver does not make,
          so what the flip opens is shown from the data and from the trace of the flip (the sector hash changes), not
          by walking onto it.
negative  the fuse script against the oracle with no cabin (`--old-oracle`, m6-worldfix): the cabin never moves, so the
          traces must differ at the first stage.

Placed-state evidence: the fuses are given and Lara is placed at hole 26, not walked there from the level start
(the pickups are 11k to 100k units away).

Run with PYTHONPATH=tools/cook:test/game:
  cabin.py --executable HOST --oracle ORACLE [--old-oracle OLD] --out DIR [rooms|fuses|all]
"""
import argparse
import itertools
import json
from pathlib import Path
import sys
sys.dont_write_bytecode = True
import directed as d
import items
import src_pc
from directed import NONE, WALK

CABIN = 84
HOLES = (26, 27, 28)
FUSE_SLOT = 8
GIVE = [('give', 110, 3)]
SHAFT = dict(room=30, below=94, cells=[(x, z) for x in (3, 4) for z in (4, 5, 6)])


def cabin(row):
    for it in row['active']:
        if it['index'] == CABIN:
            return it
    return None


def hole_start(env, index):
    item = env.level.data['items'][index]
    return item, items.hole_starts(env, item)[0][2]


def insert(s, item, nth):
    """Put one fuse into the hole in front of Lara: the ring, the USE animation, back to standing."""
    left = 3 - nth - 1
    s.hold(NONE, 3)
    d.open_ring(s)
    if not d.ring_pick(s, FUSE_SLOT, page_keys=True):
        raise ValueError(f'fuse {nth}: the ring did not select the fuse')
    ok = s.until(lambda r: r['lara']['state'] == 2 and r['inventory']['state'] == 0
                 and r['inventory']['counts'][FUSE_SLOT] == left, NONE, 260, chunk=4)
    if not ok:
        raise ValueError(f'fuse {nth}: the insertion did not finish (limit box?)')
    return len(s.masks)


def place(s, item, start):
    """Walk to the hole's front position (start = (room, x, y, z, yaw)) and face the hole."""
    if not s.walk_to(start[1], start[3], tol=45, limit=400, run=False, chunk=2, stuck=30):
        raise ValueError('walk to the next hole failed')
    s.hold(NONE, 12)
    # LIMIT_HOLE accepts Lara within 30 degrees of the item's angle (0 here: +z); the source's `abs` of the int16
    # difference also lets exactly -32768 through (hole 26's placed start), which turning by hand cannot hit.
    if not d.face(s, item['x'], item['z'] + 3000, tol=600, limit=60):
        raise ValueError('turn to the hole failed')
    s.hold(NONE, 8)
    lara = s.lara()
    if lara['state'] != 2:
        raise ValueError(f'Lara is not standing: state {lara["state"]}')
    return lara


def build_fuses(env, cache=None):
    d.Script.LIMIT = 4096
    item0, start = hole_start(env, HOLES[0])
    s = d.Script(env, start, setup=GIVE)
    log = []
    if cache and cache.exists():     # development aid: the fuse part is 1,000 ticks of closed-loop driving
        saved = json.loads(cache.read_text())
        s.masks = saved['masks']
        log, flip_tick = saved['log'], saved['flip_tick']
        s.hold(NONE, 40)
        return start, list(s.masks), log, flip_tick
    for nth, index in enumerate(HOLES):
        item, st = hole_start(env, index)
        if nth:
            # the cabin finishes the stage it is in before the next completion counts
            if not s.until(lambda r: (cabin(r) or {}).get('state') == nth and (cabin(r) or {}).get('goal') == nth,
                           NONE, 400, chunk=10):
                raise ValueError(f'the cabin did not reach stage {nth}')
            lara = place(s, item, st)
        else:
            lara = s.lara()
        log.append(dict(hole=index, lara_pos=lara['pos'], lara_yaw=lara['rot'][1], tick=len(s.masks)))
        insert(s, item, nth)
    ok = s.until(lambda r: r['save']['flipped'] == 1, NONE, 600, chunk=10)
    if not ok:
        raise ValueError('the map was not flipped')
    s.hold(NONE, 20)
    flip_tick = len(s.masks)
    if cache:
        cache.write_text(json.dumps(dict(masks=list(s.masks), log=log, flip_tick=flip_tick)))
    s.hold(NONE, 40)
    return start, list(s.masks), log, flip_tick


def room_graph(level):
    rooms = level.rooms
    alt = {i: r.alternate_room for i, r in enumerate(rooms) if r.alternate_room not in (-1, 0xFFFF)}
    hidden = set(alt.values())

    def edges(flipped):
        out = set()
        for slot in range(len(rooms)):
            if slot in hidden:
                continue
            data = alt.get(slot, slot) if flipped else slot
            out |= {(slot, p['room']) for p in rooms[data].portals if p['room'] not in hidden}
        return out
    e0, e1 = edges(False), edges(True)

    def reach(e, start):
        seen, stack = {start}, [start]
        while stack:
            a = stack.pop()
            for x, y in e:
                if x == a and y not in seen:
                    seen.add(y)
                    stack.append(y)
        return seen
    start = next(i for i in level.data['items'] if i['type'] == 0)['room']
    shaft = SHAFT
    a, b = rooms[shaft['room']], rooms[alt[shaft['room']]]
    cells = []
    for x, z in shaft['cells']:
        i = x * a.z_sectors + z
        cells.append(dict(cell=(x, z), normal=dict(floor=a.sectors[i]['floor'], room_below=a.sectors[i]['room_below']),
                          flipped=dict(floor=b.sectors[i]['floor'], room_below=b.sectors[i]['room_below'])))
    return dict(rooms=len(rooms), alternate_pairs=len(alt), edges_removed=sorted(e0 - e1), edges_added=sorted(e1 - e0),
                reachable_before=len(reach(e0, start)), reachable_after=len(reach(e1, start)),
                reachable_same=reach(e0, start) == reach(e1, start), shaft_cells=cells)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--executable', type=Path, required=True)
    p.add_argument('--oracle', type=Path, required=True)
    p.add_argument('--old-oracle', type=Path, default=d.ORACLE)
    p.add_argument('--out', required=True, type=Path)
    p.add_argument('--cache', action='store_true', help='reuse the closed-loop fuse prefix of an earlier run in --out')
    p.add_argument('action', choices=['rooms', 'fuses', 'all'], nargs='?', default='all')
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    d.SCHEMA = 4
    env = d.Env('level10a', args.executable, oracle=args.oracle)
    old = d.Env('level10a', args.executable, oracle=args.old_oracle)
    report, ok = {}, True
    graph = room_graph(env.level)
    report['rooms'] = graph
    print('rooms', json.dumps(graph), flush=True)
    for c in graph['shaft_cells']:
        ok &= c['normal']['room_below'] == SHAFT['below'] and c['flipped']['room_below'] == 255 \
            and c['normal']['floor'] == c['flipped']['floor']
    ok &= graph['edges_removed'] == [(30, 94), (94, 30)] and not graph['edges_added'] and graph['reachable_same']
    if args.action in ('fuses', 'all'):
        start, masks, log, flip_tick = build_fuses(env, args.out / 'fuses-prefix.json' if args.cache else None)
        seq, flips, finals = [], [], {}

        def watch(row):
            it = cabin(row)
            if it is not None:
                key = (it['state'], it['goal'], it['anim'], it['flags'] & 0x3e00, it['flags'] & 0x18)
                if not seq or seq[-1][1:] != key:
                    seq.append((row['tick'],) + key)
            if not flips or flips[-1][1] != row['save']['flipped'] or flips[-1][2] != row['world'][2]:
                flips.append((row['tick'], row['save']['flipped'], row['world'][2], row['world'][0]))
            finals['lara'] = (row['lara']['room'], row['lara']['pos'])
            finals['cabin_active'] = it is not None
        result = d.compare(env, start, masks, ticks=len(masks), observe=watch, setup=GIVE)
        neg = d.compare(old, start, masks, ticks=len(masks), setup=GIVE)
        # states 0..4 are animations 303..307 (the stage-3 animation reads state 0 for its first frames)
        order = [k for k, _ in itertools.groupby(s[3] for s in seq)]
        staged = order == [303, 304, 305, 306, 307]
        flipped = len(flips) > 1 and flips[-1][1] == 1
        # after state 4 the item is deactivated (it leaves the active list) and is invisible, not collidable
        gone = bool(seq) and seq[-1][3] == 307 and not finals.get('cabin_active')
        ok &= result.get('status') == 'equal' and staged and flipped and gone and neg.get('status') == 'different'
        report['fuses'] = dict(start=start, ticks=len(masks), insertions=log, compare=result, cabin_sequence=seq,
                               flip_events=flips, flip_tick=flip_tick, cabin_gone=gone, final=finals.get('lara'),
                               negative=neg)
        print('fuses', result.get('status'), 'ticks', len(masks), 'cabin', seq, 'flips', flips, 'final', finals.get('lara'),
              'negative', neg.get('status'), neg.get('tick'), flush=True)
    (args.out / 'cabin.json').write_text(json.dumps(report, indent=1, default=str) + '\n')
    print('cabin', 'PASS' if ok else 'FAIL')
    return int(not ok)


if __name__ == '__main__':
    raise SystemExit(main())
