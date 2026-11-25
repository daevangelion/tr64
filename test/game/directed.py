#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Shared driver for directed oracle-differential scenarios.

Run with PYTHONPATH=tools/cook. A scenario is one `start` placement plus a
mask sequence (at most 600 ticks, no injected loadout: every item is a real
pickup of the level). Both production controllers run on the same file and
their schema-3 records are compared through independent pipes; nothing is
stored. The oracle is also used as a cheap simulator (about 40 ms per run) so a
script can be generated closed-loop from the real state: walking to a point,
picking an inventory item, waiting for an animation. Prefixes never depend on
later inputs, so re-running the prefix reproduces the state exactly.
"""
import collections
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import sys
import tempfile
import threading
sys.dont_write_bytecode = True
import src_pc
from compare import first_difference
from stream import Run

ORACLE = Path('/tmp/tr64/fixtures/oracle/m6-worldfix-6cf35c0683029590fc80bf40ac0d4b4ae8d50e229ed3835e74c9caf64b2f2109/tr1-oracle')
# Trace schema both programs are asked for: 3 (default) or 4, which adds the per-tick `world` field and
# needs the m6-worldfix oracle (pass --oracle) and a matching port.
SCHEMA = int(os.environ.get('TR64_TRACE_SCHEMA', '3'))
DATA = Path('/tmp/tr64/data/pc')

# InputState masks (test/oracle/README.md).
NONE, LEFT, RIGHT, FORWARD, BACK, JUMP, WALK, ACTION, WEAPON, LOOK, SELECT = 0, 2, 4, 8, 16, 32, 64, 128, 256, 512, 2048
ROLL = FORWARD | BACK

# Inventory slots and item types (game/common/internal.h).
SLOT = dict(leadbar=0, key1=1, key2=2, key3=3, key4=4, puzzle4=5, puzzle3=6, puzzle2=7, puzzle1=8,
            scion=9, compass=10, pistols=11, shotgun=13, magnums=15, uzis=17,
            medi_big=19, medi_small=20)
SLOT_OF_WEAPON = {1: 11, 2: 13, 3: 15, 4: 17}


def load_module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


ledges = load_module('directed_ledges', 'ledges.py')
sector_at, commands, resolve, height = ledges.sector_at, ledges.commands, ledges.resolve, ledges.height

_local = threading.local()
_tmp = Path(os.environ.get('TMPDIR', tempfile.gettempdir()))


def scratch(name):
    key = getattr(_local, 'id', None)
    if key is None:
        key = _local.id = f'{os.getpid()}-{threading.get_ident()}'
    return _tmp / f'directed-{key}-{name}'


class Env:
    def __init__(self, level, executable, oracle=ORACLE, data=DATA):
        self.name = level
        self.path = Path(data) / f'{level}.phd'
        self.level = src_pc.load(self.path)
        self.executable = Path(executable)
        self.oracle = Path(oracle)

    def sector(self, room, x, z):
        return sector_at(self.level, room, x, z)

    def floor(self, room, x, z):
        found = resolve(self.level, room, x, z)
        if found is None:
            return None
        ri, s = found
        return ri, s, height(self.level, ri, s, x, z)

    def ceiling(self, room, x, z):
        found = resolve(self.level, room, x, z)
        if found is None:
            return None
        ri, s = found
        return height(self.level, ri, s, x, z, True)

    def items(self, *types):
        return [(i, it) for i, it in enumerate(self.level.data['items']) if it['type'] in types]


def masks_text(start, masks, ticks=None, setup=()):
    """`setup` is a sequence of ('give', TYPE, COUNT) / ('item', INDEX, HEALTH) / ('activate', INDEX, TIMER): placed-state
    directives (test/oracle/README.md), not play-reached evidence."""
    masks = list(masks)
    if ticks is not None:
        masks += [0] * (ticks - len(masks))
    head = f'start {start[0]} {start[1]} {start[2]} {start[3]} {start[4]}\n' if start else ''
    head += ''.join(f'{kind} {a} {b}\n' for kind, a, b in setup)
    return head + ''.join(f'{m}\n' for m in masks)


def _oracle_cmd(env, path, ticks, save=None):
    cmd = [str(env.oracle), '--level', str(env.path), '--input', str(path), '--ticks', str(ticks),
           '--trace-schema', str(SCHEMA)]
    if save:     # (save tick, load tick): the real source gameSave/gameLoad at those boundaries
        cmd += ['--save-tick', str(save[0]), '--load-tick', str(save[1])]
    return cmd


def probe(env, start, masks, want='last', setup=()):
    """Oracle-only run of exactly these masks. Returns the final record, or all."""
    path = scratch('probe.masks')
    path.write_text(masks_text(start, masks, setup=setup))
    run = Run(_oracle_cmd(env, path, len(masks)), '--out')
    last = None
    rows = []
    for line in run.trace:
        last = line
        if want == 'all':
            rows.append(line)
    code, log = run.finish()
    if code or last is None:
        raise ValueError(f'oracle probe failed code={code}: {log[-300:]}')
    if want == 'all':
        return [json.loads(r) for r in rows]
    return json.loads(last)


class Observer:
    """Counters over records already proven equal; never part of a verdict."""
    def __init__(self):
        self.lara_states = collections.Counter()
        self.lara_anims = collections.Counter()
        self.water_states = collections.Counter()
        self.item_states = collections.Counter()
        self.weapons = set()
        self.moved = set()
        self.first = {}
        self.min_health = 1000
        self.save_max = collections.Counter()
        self.inv_states = set()
        self.last = None
        self.deaths = set()
        self.min_item_health = {}
        self.first_item_health = {}

    def __call__(self, row):
        lara = row['lara']
        if lara is None:
            return
        self.last = row
        self.lara_states[lara['state']] += 1
        self.lara_anims[lara['anim']] += 1
        self.water_states[lara['water']] += 1
        self.weapons.add((lara['weapon'], lara['weaponState']))
        self.min_health = min(self.min_health, lara['health'])
        self.inv_states.add((row['inventory']['state'], row['inventory']['page']))
        for key in ('kills', 'pickups', 'ammoUsed', 'mediUsed', 'secrets'):
            self.save_max[key] = max(self.save_max[key], row['save'][key])
        for item in row['active']:
            self.item_states[(item['type'], item['state'])] += 1
            first = self.first.setdefault(item['index'], tuple(item['pos']))
            if tuple(item['pos']) != first:
                self.moved.add(item['index'])
            self.first_item_health.setdefault(item['index'], item['health'])
            self.min_item_health[item['index']] = min(self.min_item_health.get(item['index'], 1 << 30), item['health'])

    def summary(self):
        return dict(lara_states=sorted(self.lara_states), weapons=sorted(self.weapons),
                    min_health=self.min_health, moved=sorted(self.moved), save=dict(self.save_max),
                    item_states=sorted(f'{t}:{s}' for t, s in self.item_states))

    def merge(self, other):
        self.lara_states.update(other.lara_states)
        self.lara_anims.update(other.lara_anims)
        self.water_states.update(other.water_states)
        self.item_states.update(other.item_states)
        self.weapons |= other.weapons
        self.min_health = min(self.min_health, other.min_health)
        self.inv_states |= other.inv_states
        for k, v in other.save_max.items():
            self.save_max[k] = max(self.save_max[k], v)


def compare(env, start, masks, ticks=None, observe=None, setup=(), save=None):
    """Compare both controllers on this script. Result dict has a `status`."""
    masks = list(masks)
    ticks = ticks or len(masks)
    text = masks_text(start, masks, ticks, setup)
    path = scratch('case.masks')
    path.write_text(text)
    keep = os.environ.get('DIRECTED_SAVE')
    if keep:   # keep the exact script for a later replay through a coverage build
        digest = hashlib.sha1(f'{env.name}\n{text}'.encode()).hexdigest()[:16]
        (Path(keep) / f'{env.name}-{ticks}-{digest}.masks').write_text(text)
        if os.environ.get('DIRECTED_SAVE_ONLY'):   # regenerate scripts without comparing
            return dict(status='saved', tick=ticks + 1)   # never `equal`: nothing was compared
    ref = Run(_oracle_cmd(env, path, ticks, save), '--out')
    nat = Run([str(env.executable), '--game-level', env.name, '--game-input', str(path),
               '--game-ticks', str(ticks), '--game-trace-only', '--game-trace-schema', str(SCHEMA)]
              + (['--game-save-tick', str(save[0]), '--game-load-tick', str(save[1])] if save else []), '--game-trace')
    tick = 0
    while True:
        a, b = ref.trace.readline(), nat.trace.readline()
        if not a or not b:
            break
        if a != b:
            _, rlog = ref.finish(kill=True)
            _, nlog = nat.finish(kill=True)
            if 'oracle assertion' in rlog:     # the source refused: a reference boundary, not a comparison
                return dict(status='oracle_boundary', tick=tick, reason=re.search(r'oracle assertion: ([^\n]+)', rlog).group(1)[-160:])
            try:
                diff = first_difference(json.loads(a), json.loads(b)) or ('bytes', '', '')
            except ValueError as error:
                diff = ('malformed', str(error), '')
            return dict(status='different', tick=tick, field=diff[0], expected=str(diff[1])[:120],
                        actual=str(diff[2])[:120])
        if observe is not None:
            observe(json.loads(a))
        tick += 1
    if not b:
        code, log = nat.finish()
        if code and re.search(r'tr64 fatal: game .*?(?:not ported|outside M2)', log):
            ref.finish(kill=True)
            return dict(status='loud', tick=tick, reason=re.search(r'tr64 fatal: (game [^\n]+)', log).group(1))
    else:
        code, log = None, ''
    if a:
        ref.finish(kill=True)
        if code is None:
            nat.finish(kill=True)
        return dict(status='error', tick=tick, why='native ended early', log=log[-300:])
    if b:
        nat.finish(kill=True)
        _, rlog = ref.finish()
        if 'oracle assertion' in rlog:
            return dict(status='oracle_boundary', tick=tick, reason=re.search(r'oracle assertion: ([^\n]+)', rlog).group(1)[-160:])
        return dict(status='error', tick=tick, why='oracle ended early')
    rcode, rlog = ref.finish()
    if code is None:
        code, log = nat.finish()
    if tick == 0 and rcode and code and 'start' in rlog and 'placement' in log:
        return dict(status='invalid', tick=0)
    if rcode or code or tick != ticks + 1:
        return dict(status='error', tick=tick, oracle_exit=rcode, native_exit=code, log=(rlog[-200:], log[-200:]))
    return dict(status='equal', tick=tick)


# ---- scripting -----------------------------------------------------------

class Script:
    LIMIT = 600     # ticks; a scene is 600 at most, 4096 for the four levels before a cutscene (oracle README)

    def __init__(self, env, start, setup=()):
        self.env = env
        self.start = start
        self.setup = tuple(setup)
        self.masks = []
        self._rec = None

    def hold(self, mask, ticks=1):
        self.masks.extend([mask] * ticks)
        self._rec = None
        return self

    def state(self):
        if self._rec is None or self._rec[0] != len(self.masks):
            self._rec = (len(self.masks), probe(self.env, self.start, self.masks[:self.LIMIT], setup=self.setup))  # later state is that of the last admitted tick
        return self._rec[1]

    def lara(self):
        return self.state()['lara']

    def item(self, index):
        for it in self.state()['active']:
            if it['index'] == index:
                return it
        return None

    def full(self):
        return len(self.masks) >= self.LIMIT

    def pad(self, ticks=None):
        ticks = ticks or self.LIMIT
        if len(self.masks) < ticks:
            self.masks.extend([0] * (ticks - len(self.masks)))
        del self.masks[ticks:]
        self._rec = None

    def until(self, predicate, mask, limit, chunk=1):
        """Hold `mask` until predicate(record) is true or `limit` ticks; True if met."""
        spent = 0
        while spent < limit and len(self.masks) < self.LIMIT:
            if predicate(self.state()):
                return True
            step = min(chunk, limit - spent, self.LIMIT - len(self.masks))
            self.hold(mask, step)
            spent += step
        return predicate(self.state()) if len(self.masks) <= self.LIMIT else False

    def heading(self, x, z):
        lara = self.lara()
        return heading_to(lara['pos'][0], lara['pos'][2], x, z)

    def walk_route(self, goal, limit=400, tol=350, run=True):
        """Walk to the nearest cell (by path) where goal(room, x, y, z) holds, steering through
        sector centres. True when Lara stands in such a cell."""
        lara = self.lara()
        path = route(self.env, lara['room'], lara['pos'][0], lara['pos'][1], lara['pos'][2], goal)
        if path is None:
            return False
        spent = len(self.masks)
        for px, pz in path[1:]:
            if len(self.masks) - spent > limit or not self.walk_to(px, pz, tol=tol, limit=limit, run=run, stuck=40):
                return False
        return True

    def walk_to(self, x, z, tol=300, limit=300, run=True, chunk=4, stuck=12):
        """Closed-loop steer toward (x, z). True when within `tol`."""
        spent = 0
        best, since = 1 << 30, 0
        while spent < limit and len(self.masks) + chunk <= self.LIMIT:
            lara = self.lara()
            dx, dz = x - lara['pos'][0], z - lara['pos'][2]
            dist = math.hypot(dx, dz)
            if dist <= tol:
                return True
            err = angle_diff(heading_to(0, 0, dx, dz), lara['rot'][1])
            if abs(err) > 3000:      # turning on the spot is not progress, nor a stall
                self.hold(RIGHT if err > 0 else LEFT, 2)
                spent += 2
                continue
            if dist < best - 8:
                best, since = dist, 0
            else:
                since += chunk
                if since >= stuck * chunk:
                    return False
            steer = (RIGHT if err > 600 else LEFT if err < -600 else 0)
            self.hold(FORWARD | steer | (0 if run else WALK), chunk)
            spent += chunk
        return False


def route(env, room, x, y, z, goal, limit=3000, step=800):
    """Shortest path over 1024-unit sector centres of standable ground from (room, x, z) to the
    first cell where goal(room, x, y, z) is true. Cells join when the ground is continuous
    (height step <= `step`), following portals. Returns [(x, z), ...] or None."""
    cell = lambda a: a >> 10
    first = ground(env, room, x, z, None, clear=1024)
    if first is None:
        return None
    key0 = (cell(x), cell(z))
    seen = {key0: None}
    where = {key0: (first[0], x, first[1], z)}
    queue = collections.deque([key0])
    while queue and len(seen) < limit:
        key = queue.popleft()
        r, cx, cy, cz = where[key]
        if goal(r, cx, cy, cz):
            path = []
            while key is not None:
                path.append(where[key][1::2])
                key = seen[key]
            return path[::-1]
        for dx, dz, _ in DIRS:
            nx, nz = (cell(cx) + dx) * 1024 + 512, (cell(cz) + dz) * 1024 + 512
            nkey = (cell(nx), cell(nz))
            if nkey in seen:
                continue
            g = ground(env, r, nx, nz, cy, step, clear=1024)
            if g is None:
                continue
            seen[nkey] = key
            where[nkey] = (g[0], nx, g[1], nz)
            queue.append(nkey)
    return None


def heading_to(x0, z0, x1, z1):
    dx, dz = x1 - x0, z1 - z0
    return int(round(math.atan2(dx, dz) * 32768 / math.pi))


def angle_diff(target, current):
    d = (target - current + 32768) % 65536 - 32768
    return d


def yaw(a):
    return (a + 32768) % 65536 - 32768


# ---- inventory ring ------------------------------------------------------

def open_ring(script, limit=60):
    script.hold(SELECT, 1)
    return script.until(lambda r: r['inventory']['state'] == 4, NONE, limit, chunk=2)


def ring_pick(script, slot, page_keys=None, limit=200):
    """With the ring open and ready, rotate to `slot` and press ACTION (select).

    page_keys: True moves to the keys page first, False to the main page, None stays
    on the current page (the USE page opened at a keyhole). Returns True if selected.
    """
    for _ in range(40):
        inv = script.state()['inventory']
        if inv['state'] != 4:
            script.until(lambda r: r['inventory']['state'] in (4, 0), NONE, 60, chunk=2)
            if script.state()['inventory']['state'] == 0:
                return False
            continue
        if page_keys is True and inv['page'] != 6:
            script.hold(FORWARD, 1)
            script.until(lambda r: r['inventory']['state'] == 4, NONE, 60, chunk=2)
            continue
        if page_keys is False and inv['page'] != 5:
            script.hold(BACK, 1)
            script.until(lambda r: r['inventory']['state'] == 4, NONE, 60, chunk=2)
            continue
        listed = inv['itemsList'][:inv['itemsCount']]
        if slot not in listed:
            return False
        if listed[inv['itemIndex']] == slot:
            script.hold(ACTION, 1)
            script.until(lambda r: r['inventory']['state'] in (9, 0), NONE, 40, chunk=1)
            script.until(lambda r: r['inventory']['state'] == 0, NONE, 80, chunk=2)
            return True
        n = inv['itemsCount']
        left = (listed.index(slot) - inv['itemIndex']) % n
        script.hold(LEFT if left <= n - left else RIGHT, 1)
        script.until(lambda r: r['inventory']['state'] == 4, NONE, 60, chunk=2)
    return False


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


# ---- placement helpers ---------------------------------------------------

DIRS = [(0, 1, 0), (1, 0, 16384), (0, -1, -32768), (-1, 0, -16384)]


def ground(env, room, x, z, want_y=None, tol=0, clear=1536):
    """(room, y) of standable ground at x, z (following portals), else None."""
    r = resolve(env.level, room, x, z)
    if r is None:
        return None
    ri, s = r
    if s['room_below'] != 255 or s['floor'] == -127:
        return None
    y = height(env.level, ri, s, x, z)
    ceil = height(env.level, ri, s, x, z, True)
    if y is None or ceil is None or y - ceil < clear:
        return None
    if want_y is not None and abs(y - want_y) > tol:
        return None
    return ri, y


def approach_points(env, item, distance=768, tol=0, clear=1536):
    """Placements (start tuples) `distance` from the item, facing it, per open direction."""
    out = []
    for dx, dz, angle in DIRS:
        x, z = item['x'] - dx * distance, item['z'] - dz * distance
        g = ground(env, item['room'], x, z, item['y'], tol, clear)
        if g is not None:
            out.append((g[0], x, g[1], z, angle))
    return out


def face(script, x, z, tol=1500, limit=40):
    """Turn on the spot (standing) until facing (x, z) within `tol`."""
    for _ in range(limit):
        lara = script.lara()
        if lara is None:
            raise ValueError('Lara is absent from the record (dead or gone); no facing to compute')
        err = angle_diff(heading_to(lara['pos'][0], lara['pos'][2], x, z), lara['rot'][1])
        if abs(err) <= tol:
            return True
        script.hold(RIGHT if err > 0 else LEFT, 1)
    return False


def pickup(script, item, stand=190, limit=140):
    """Walk to a pickup, stop, press ACTION until its slot count rises. True if collected."""
    before = list(script.state()['inventory']['counts'])
    lara = script.lara()
    dx, dz = item['x'] - lara['pos'][0], item['z'] - lara['pos'][2]
    dist = math.hypot(dx, dz) or 1
    # Stop short on the line from Lara to the item, then face it.
    ax, az = item['x'] - dx / dist * stand, item['z'] - dz / dist * stand
    if not script.walk_to(int(ax), int(az), tol=100, limit=200, run=False):
        return False
    script.until(lambda r: r['lara']['state'] == 2, NONE, 40)
    face(script, item['x'], item['z'], tol=800)
    script.until(lambda r: r['lara']['state'] == 2, NONE, 40)
    for _ in range(limit // 4):
        script.hold(ACTION, 4)
        if list(script.state()['inventory']['counts']) != before:
            script.hold(NONE, 2)
            return True
    return False
