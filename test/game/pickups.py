#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""A collected pickup that has a pickup trigger under it, then a door or a map flip.

The reference source's world validator (worldBuildRoomPairs, object.h) used to refuse every door
update and map flip once such a pickup had been collected: Pickup::collide unlinks the item and
makes it INVISIBLE, the trigger (usePickup -> Pickup::use) turns INVISIBLE into INACTIVE while the
item stays unlinked, and an unlinked item that is not INVISIBLE failed the check. The oracle's hunk
14 and the port's `game_world_items_linked` now exempt exactly that item (test/oracle/README.md).

scan      the six affected PC levels from the floor data (pickup triggers on pickup-type items),
          and, for LEVEL3B, from the room-connection graph: rooms 10 and 11 (Larson's trigger) are
          reachable only after flip 1, which the Scion's trigger requests.
cases     per affected pickup: Lara placed in front of it (no `give`, no `item`), collects it with
          ACTION, the trigger fires, and the following door opening or map flip is run; oracle and
          port schema-4 traces are compared record for record and the oracle's own record is
          checked (validator verdict, world hash change, `flipped`, Lara through the door).
          Collected by play: LEVEL3A 33/40/53, LEVEL3B 20, LEVEL4 71, LEVEL8C 10/54. Followed by a door or flip
          that fires in the same trigger: LEVEL3B (flip 1 and door 32) and LEVEL8C item 54 (door 56). The
          other door/flip triggers are 5000 to 70000 units away or beyond a path this driver does not
          walk (reported as `reached: false`). LEVEL7A 46/61/67 and LEVEL8B 15-18 are listed as not
          exercised, with the reason (NOT_EXERCISED); none of that is shown by play.
negative  every case again against a port built with the old refusal (`--old`): it must differ.
relink    a save and load after the pickup was collected and its trigger made it INACTIVE: the source's
          `ItemObj::load` (item.h) links every saved item into its room again, and the collision loop
          skips only INVISIBLE items (lara.h:363), so a collected pickup whose trigger turned it
          INACTIVE is linked, drawn and collectable again. Each affected pickup (and, as a control, an
          ordinary pickup that has no trigger under it) is collected by play, saved and loaded 60 ticks
          later, and Lara presses ACTION where she stood; oracle and port are compared in schema 4 and the
          oracle's own record says whether the pickup counter rose again. `--no-save` runs the same
          script without the save and load.

Run with PYTHONPATH=tools/cook:
  pickups.py --executable HOST --oracle ORACLE --old OLD_HOST --out DIR [scan|cases|negative|all]
"""
import argparse
import collections
import json
import math
from pathlib import Path
import sys
sys.dont_write_bytecode = True
import directed as d
from directed import ACTION, FORWARD, NONE

PICKUP_TYPES = set(range(84, 95)) | set(range(110, 114)) | {126} | set(range(129, 133)) | {143, 144}
DOOR_TYPES = range(57, 65)
ACT_ACTIVATE, ACT_FLIP, ACT_FLIP_ON, ACT_FLIP_OFF = 0, 3, 4, 5
# From the floor-data scan of the PC levels (reproduced by `scan`, which fails if this differs).
AFFECTED = {'level3a': [33, 40, 53], 'level3b': [20], 'level4': [71], 'level7a': [46, 61, 67],
            'level8b': [15, 16, 17, 18], 'level8c': [10, 54]}
PC_LEVELS = ['level1', 'level2', 'level3a', 'level3b', 'level4', 'level5', 'level6', 'level7a', 'level7b',
             'level8a', 'level8b', 'level8c', 'level10a', 'level10b', 'level10c']


def trigger_sectors(level):
    """Yield (room, sector index, trigger type, [(action, argument)], x, z) for every trigger sector."""
    words = level.data['floor_data']
    for ri, room in enumerate(level.rooms):
        for index, sector in enumerate(room.sectors):
            i = sector['floor_index']
            if not i:
                continue
            while True:
                cmd = words[i]
                kind = cmd & 31
                if kind in (1, 2, 3):
                    i += 2
                    if cmd & 0x8000:
                        break
                    continue
                if kind == 4:
                    j, acts = i + 2, []
                    while True:
                        w = words[j]
                        act = (w >> 10) & 15
                        acts.append((act, w & 0x3ff))
                        if act in (1, 12):
                            j += 1          # camera / flyby carry a second word
                        if w & 0x8000:
                            break
                        j += 1
                    x = room.info['x'] + (index // room.z_sectors) * 1024 + 512
                    z = room.info['z'] + (index % room.z_sectors) * 1024 + 512
                    yield ri, index, (cmd >> 8) & 0x7f, acts, x, z
                break


def pickup_triggers(level):
    """Pickup-type triggers (type 4) whose first action names a pickup-class item."""
    items = level.data['items']
    for ri, index, ttype, acts, x, z in trigger_sectors(level):
        if ttype == 4 and acts and acts[0][0] == ACT_ACTIVATE and acts[0][1] < len(items) \
                and items[acts[0][1]]['type'] in PICKUP_TYPES:
            yield dict(room=ri, sector=index, item=acts[0][1], acts=acts[1:], x=x, z=z)


def room_graph(level, flipped):
    """Room-connection graph: portals and floor/ceiling openings of the rooms in the given flip state."""
    rooms = level.rooms
    mate = {}
    for i, r in enumerate(rooms):
        if r.alternate_room not in (-1, 255, 65535) and r.alternate_room < len(rooms):
            mate[i], mate[r.alternate_room] = r.alternate_room, i
    graph = collections.defaultdict(set)
    for r in range(len(rooms)):
        source = mate.get(r, r) if flipped else r
        graph[r].update(p['room'] for p in rooms[source].portals)
        for s in rooms[source].sectors:
            for key in ('room_below', 'room_above'):
                if s[key] != 255:
                    graph[r].add(s[key])
    return graph


def reachable(graph, start):
    seen, todo = {start}, [start]
    while todo:
        for nxt in graph[todo.pop()]:
            if nxt not in seen:
                seen.add(nxt)
                todo.append(nxt)
    return seen


def scan(out):
    import src_pc
    found, details = {}, {}
    for name in PC_LEVELS:
        level = src_pc.load(d.DATA / f'{name}.phd')
        hits = sorted({t['item'] for t in pickup_triggers(level)})
        if hits:
            found[name] = hits
            items = level.data['items']
            details[name] = [dict(item=t['item'], type=items[t['item']]['type'], room=t['room'],
                                  doors=[b for a, b in t['acts'] if a == ACT_ACTIVATE and b < len(items) and items[b]['type'] in DOOR_TYPES],
                                  flips=[(a, b) for a, b in t['acts'] if a in (ACT_FLIP, ACT_FLIP_ON, ACT_FLIP_OFF)])
                             for t in pickup_triggers(level)]
    if found != AFFECTED:
        raise ValueError(f'pickup triggers differ from the recorded set: {found}')
    level = src_pc.load(d.DATA / 'level3b.phd')
    rows = {}
    for flipped in (False, True):
        graph = room_graph(level, flipped)
        for start in (5, 19):
            seen = reachable(graph, start)
            rows[f'flipped={int(flipped)} from room {start}'] = dict(room10=10 in seen, room11=11 in seen, rooms=len(seen))
    if any(r['room10'] or r['room11'] for k, r in rows.items() if k.startswith('flipped=0')) \
            or not all(r['room10'] and r['room11'] for k, r in rows.items() if k.startswith('flipped=1')):
        raise ValueError(f'LEVEL3B room graph does not show rooms 10/11 only after the flip: {rows}')
    for name, rows_ in details.items():
        for row in rows_:
            print(f'scan {name} item {row["item"]} type {row["type"]} room {row["room"]} doors {row["doors"]} flips {row["flips"]}')
    for k, v in rows.items():
        print(f'graph level3b {k}: rooms 10 reachable={v["room10"]} 11 reachable={v["room11"]} ({v["rooms"]} rooms)')
    (out / 'scan.json').write_text(json.dumps(dict(found=found, details=details, graph=rows), indent=1) + '\n')


# ---- placement and scripts -------------------------------------------------

def column(level, item):
    """The item's room and the rooms below it at its position (the floor lookup that reads the trigger)."""
    rooms, room = {item['room']}, item['room']
    while True:
        sector = d.sector_at(level, room, item['x'], item['z'])
        if sector is None or sector['room_below'] == 255 or sector['room_below'] in rooms:
            return rooms
        room = sector['room_below']
        rooms.add(room)


def starts_for(env, item):
    """Placements facing the item, a few distances and the four open sides; floor within the pickup
    window first, then up to 800 units below it (the Scion hovers 640 over its pedestal)."""
    out = []
    for tol in (300, 800):
        for dist in (512, 640, 768, 1024, 320, 420):
            for start in d.approach_points(env, item, distance=dist, tol=tol, clear=1024):
                if start not in out:
                    out.append(start)
    return out


def grab(script, item, limit=400):
    """Walk to the item, face it, creep to 120..230 units in front of it, press ACTION until its
    inventory slot count rises (the pickup window is -256..100 units along Lara's heading,
    object.h LIMIT_PICKUP). True if collected."""
    before = list(script.state()['inventory']['counts'])
    lara = script.lara()
    if lara['water'] == 3:       # underwater item: tread facing it, ACTION inside the +-512 window
        script.until(lambda r: r['lara']['state'] == 13, NONE, 80, chunk=2)
        for _ in range(25):
            script.hold(ACTION, 4)
            if list(script.state()['inventory']['counts']) != before:
                script.hold(NONE, 2)
                return True
        return False
    dx, dz = item['x'] - lara['pos'][0], item['z'] - lara['pos'][2]
    dist = math.hypot(dx, dz) or 1
    if dist > 330 and not script.walk_to(int(item['x'] - dx / dist * 300), int(item['z'] - dz / dist * 300),
                                         tol=100, limit=limit, run=False):
        return False
    script.until(lambda r: r['lara']['state'] == 2, NONE, 40)
    if not d.face(script, item['x'], item['z'], tol=500):
        return False
    script.until(lambda r: r['lara']['state'] == 2, NONE, 40)
    for _ in range(40):
        lara = script.lara()
        if math.hypot(item['x'] - lara['pos'][0], item['z'] - lara['pos'][2]) <= 230:
            break
        script.hold(FORWARD | d.WALK, 1)
    script.until(lambda r: r['lara']['state'] == 2, NONE, 40)
    for _ in range(25):
        script.hold(ACTION, 4)
        if list(script.state()['inventory']['counts']) != before:
            script.hold(NONE, 2)
            return True
    return False


def collect(env, index):
    """Closed-loop from the oracle: (script, start) with the item collected, else None."""
    item = dict(env.level.data['items'][index])
    for start in starts_for(env, item):
        try:
            script = d.Script(env, start)
            if grab(script, item):
                return script, start
        except ValueError:
            continue
    return None


def settle(script, ticks):
    script.hold(NONE, ticks)


def go(script, x, z, tol=250, limit=260):
    return script.walk_to(int(x), int(z), tol=tol, limit=limit, run=False)


def through(script, door):
    """Walk along the door's axis through its two cells (the cell in front of it and its own) and one
    cell beyond, from whichever side Lara is on (Door::action closes both, object.h worldBuildDoorRefs)."""
    ax, az = {0: (0, -1), 16384: (-1, 0), -32768: (0, 1), -16384: (1, 0)}[d.yaw(door['angle'])]
    lara = script.lara()
    t = ((lara['pos'][0] - door['x']) * ax + (lara['pos'][2] - door['z']) * az) / 1024
    # t >= 0.5: Lara is on the front side, so go to the cell behind the door; otherwise the other way.
    steps = (-1, -2) if t >= 0.5 else (2, 3)
    return all(go(script, door['x'] + k * ax * 1024, door['z'] + k * az * 1024, tol=300, limit=260) for k in steps)


# Pickups no scripted window of 600 ticks collects, with the reason `cases` checks where it can.
NOT_EXERCISED = {
    ('level7a', 46): 'door 45 closes the key room at the start; its switch (item 51) is 6000 units away',
    ('level7a', 61): 'the key lies in the front cell of door 106, which a pad 3584 units higher opens first',
    ('level8b', 15): 'the artifacts hover 3328 over a pit; ITEM_DRAWBRIDGE has no controller in the fixed source',
    ('level8b', 16): 'as item 15', ('level8b', 17): 'as item 15', ('level8b', 18): 'as item 15',
}

# What follows the collection, per item: wait for the trigger/animation, step on a pad, or pass a door.
FOLLOW = {
    ('level3a', 33): ('wait', 120), ('level3a', 40): ('wait', 120), ('level3a', 53): ('wait', 120),
    ('level3b', 20): ('wait', 520),
    ('level4', 71): ('pad', (15872, 40448)),    # best effort: room 51's pad opens doors 102 and 103, 5500 units away
    ('level7a', 46): ('wait', 120), ('level7a', 61): ('door', 106), ('level7a', 67): ('door', 104),
    ('level8b', 15): ('wait', 120), ('level8b', 16): ('wait', 120), ('level8b', 17): ('wait', 120), ('level8b', 18): ('wait', 120),
    ('level8c', 10): ('pad', (44544, 49664)),   # best effort: the pad opens door 1, 5200 units away
    ('level8c', 54): ('door', 56),
}


def build(env, index):
    """The full mask list for one pickup, or (None, reason)."""
    found = collect(env, index)
    if found is None:
        return None, 'no placement collects it'
    script, start = found
    kind, arg = FOLLOW[(env.name, index)]
    settle(script, 20)
    reached = True
    if kind == 'wait':
        settle(script, arg)
    elif kind == 'pad':
        reached = go(script, *arg, tol=300, limit=500)
        settle(script, 150)
    elif kind == 'door':
        settle(script, 120)
        reached = through(script, dict(env.level.data['items'][arg]))
        settle(script, 30)
    script.pad(min(600, len(script.masks)))
    return dict(masks=list(script.masks), start=start, reached=reached, follow=(kind, arg)), ''


# ---- checks on the oracle's own records ------------------------------------

class Facts:
    """Collected from records already proven equal between the programs."""
    def __init__(self):
        self.rows = []

    def __call__(self, row):
        self.rows.append(dict(pickups=row['save']['pickups'], flipped=row['save']['flipped'], world=row['world'],
                              pos=row['lara']['pos'] if row['lara'] else None))

    def summary(self):
        rows = self.rows
        base = rows[0]['pickups']
        got = next((i for i, r in enumerate(rows) if r['pickups'] > base), None)
        out = dict(collected_tick=got, records=len(rows), validator_refused=sum(1 for r in rows if r['world'][3] != 1),
                   flipped=rows[-1]['flipped'])
        if got is not None:
            # Changes from the tick before the collection on: the trigger fires on the collection tick.
            after = [r['world'] for r in rows[max(got - 1, 0):]]
            out['sector_hash_changes'] = len({w[0] for w in after}) - 1
            out['flip_maps_changes'] = len({w[2] for w in after}) - 1
            out['box_flag_changes'] = len({w[1] for w in after}) - 1
        return out


def run_case(env, index, build_result, executable):
    facts = Facts()
    result = d.compare(env, build_result['start'], build_result['masks'], observe=facts)
    result['facts'] = facts.summary() if facts.rows else {}
    return result


def verify(name, index, case, facts):
    problems = []
    if facts.get('collected_tick') is None:
        problems.append('the pickup was not collected')
    if facts.get('validator_refused'):
        problems.append(f"the oracle's validator refused the world on {facts['validator_refused']} ticks")
    kind, arg = FOLLOW[(name, index)]
    if kind == 'door' and not facts.get('sector_hash_changes'):
        problems.append('no door changed the live sectors after the collection')
    if name == 'level3b' and (facts.get('flipped') != 1 or not facts.get('sector_hash_changes')):
        problems.append('flip 1 and door 32 did not change the world')
    return problems


def cases(args, only_negative=False):
    report = {}
    failed = False
    old = args.old
    for name, items in AFFECTED.items():
        if args.levels and name not in args.levels.split(','):
            continue
        env = d.Env(name, args.executable, args.oracle)
        old_env = d.Env(name, old, args.oracle) if old else None
        for index in items:
            key = f'{name}/{index}'
            item = env.level.data['items'][index]
            if not any(t['room'] in column(env.level, item) for t in pickup_triggers(env.level) if t['item'] == index):
                # The trigger lives only in the alternate room: it exists while the map is flipped.
                print(f'{key}: not exercised: its pickup trigger is in no room of the item\'s floor column {sorted(column(env.level, item))}', flush=True)
                report[key] = dict(status='not-exercised', reason='pickup trigger only in an alternate room')
                continue
            built, why = build(env, index)
            if built is None and (name, index) in NOT_EXERCISED:
                print(f'{key}: not exercised: {NOT_EXERCISED[(name, index)]} ({why})', flush=True)
                report[key] = dict(status='not-exercised', reason=NOT_EXERCISED[(name, index)], tried=why)
                continue
            if built is None:
                print(f'{key}: FAIL {why}', flush=True)
                report[key] = dict(status='fail', why=why)
                failed = True
                continue
            row = dict(start=built['start'], follow=built['follow'], reached=built['reached'], ticks=len(built['masks']))
            if args.save_masks:
                args.save_masks.mkdir(parents=True, exist_ok=True)
                (args.save_masks / f'{name}-{index}.masks').write_text(d.masks_text(built['start'], built['masks']))
            if not only_negative:
                result = run_case(env, index, built, args.executable)
                row.update(status=result['status'], facts=result['facts'])
                if result['status'] != 'equal':
                    row['result'] = {k: v for k, v in result.items() if k != 'facts'}
                    failed = True
                else:
                    problems = verify(name, index, built, result['facts'])
                    row['problems'] = problems
                    failed |= bool(problems)
            if old_env is not None:
                neg = d.compare(old_env, built['start'], built['masks'])
                row['negative'] = {k: v for k, v in neg.items() if k != 'facts'}
                # The old behaviour must be told apart: at the refused flip (save.flipped, LEVEL3B) or,
                # when no door or flip follows, at the validator verdict (world[3]).
                if neg['status'] != 'different' or not neg.get('field', '').startswith(('world', 'save.flipped')):
                    failed = True
                    row['negative_problem'] = 'the old refusal was not detected'
            report[key] = row
            print(key, json.dumps(row, sort_keys=True), flush=True)
    return report, failed


class Rel:
    """Pickup counter and the inventory slot totals per tick, from records already proven equal."""
    def __init__(self):
        self.rows = []

    def __call__(self, row):
        self.rows.append((row['save']['pickups'], sum(row['inventory']['counts']), row['world'][3]))


def relink_cases(args):
    report, failed = {}, False
    cases_ = [(n, i, True) for n, items in AFFECTED.items() for i in items if (n, i) not in NOT_EXERCISED]
    for name in ('level1', 'level2', 'level3a', 'level4'):
        env0 = d.Env(name, args.executable, args.oracle)
        trig = {t['item'] for t in pickup_triggers(env0.level)}
        plain = [i for i, it in enumerate(env0.level.data['items']) if it['type'] in PICKUP_TYPES and i not in trig]
        cases_ += [(name, i, False) for i in plain[:2]]
    for name, index, triggered in cases_:
        if args.levels and name not in args.levels.split(','):
            continue
        env = d.Env(name, args.executable, args.oracle)
        key = f'{name}/{index}'
        if triggered and not any(t['room'] in column(env.level, env.level.data['items'][index])
                                 for t in pickup_triggers(env.level) if t['item'] == index):
            continue
        found = collect(env, index)
        if found is None:
            print(f'{key}: not exercised: no placement collects it', flush=True)
            report[key] = dict(status='not-exercised', triggered=triggered)
            continue
        script, start = found
        settle(script, 80)                       # the trigger turned it INACTIVE long before the save
        first = list(script.masks)
        second = [ACTION] * 60 + [NONE] * 10 + [FORWARD] * 6 + [NONE] * 20 + [ACTION] * 60 + [NONE] * 10
        masks = first + second
        masks = masks[:600]
        save = None if args.no_save else (len(first), len(first) + 1)
        rel = Rel()
        result = d.compare(env, start, masks, ticks=len(masks), observe=rel, save=save)
        row = dict(start=start, triggered=triggered, save_tick=save[0] if save else None, ticks=len(masks), status=result['status'])
        if result['status'] != 'equal':
            row['result'] = result
            failed = True
        elif rel.rows:
            before = rel.rows[min(len(first), len(rel.rows) - 1)]
            after = rel.rows[-1]
            row.update(pickups_at_start=rel.rows[0][0], pickups_at_save=before[0], pickups_end=after[0],
                       counts_at_save=before[1], counts_end=after[1],
                       collected_again=after[0] > before[0], validator_refused=sum(1 for r in rel.rows if r[2] != 1))
            # Positive control: the script really collected it before the save. Since oracle hunk 20 a collected pickup
            # is not linked again by the load, so ACTION beside it a second time changes nothing.
            problems = []
            if before[0] <= rel.rows[0][0]:
                problems.append('the script did not collect the pickup before the save')
            if after[0] != before[0] or after[1] != before[1]:
                problems.append('collected again after the load')
            if row['validator_refused']:
                problems.append('the world validator refused')
            if problems:
                row['problems'] = problems
                failed = True
        # Negative control: the previous oracle links every item on load, so the same script differs after the load.
        if save and triggered and result['status'] == 'equal':
            old = d.compare(d.Env(name, args.executable, args.old_oracle), start, masks, ticks=len(masks), save=save)
            row['negative'] = {k: v for k, v in old.items() if k != 'facts'}
            if old['status'] != 'different' or old['tick'] <= save[0]:
                row['negative_problem'] = 'the previous oracle was not told apart after the load'
                failed = True
        report[key] = row
        print(key, json.dumps(row, sort_keys=True), flush=True)
    return report, failed


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--executable', type=Path)
    p.add_argument('--oracle', type=Path, default=d.ORACLE)
    p.add_argument('--old-oracle', type=Path, default=Path('/tmp/tr64/fixtures/oracle/m6-gaps1-72fab60c27a6f2007db13f81c4d31c8ef7bd5d5d9728cde31c7b74fce282613f/tr1-oracle'),
                   help='relink negative control: the oracle whose load links every item again')
    p.add_argument('--old', type=Path, help='port built with the old refusal (negative control)')
    p.add_argument('--levels')
    p.add_argument('--save-masks', type=Path, help='also write each case as an input file (for identity.py different)')
    p.add_argument('--no-save', action='store_true', help='relink: the same script without the save and load (control)')
    p.add_argument('--out', required=True, type=Path)
    p.add_argument('action', choices=['scan', 'cases', 'negative', 'relink', 'all'], nargs='?', default='all')
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    d.SCHEMA = 4
    failed = False
    if args.action in ('scan', 'all'):
        scan(args.out)
    if args.action == 'relink':
        if not args.executable:
            p.error('relink needs --executable')
        report, failed = relink_cases(args)
        (args.out / ('relink-nosave.json' if args.no_save else 'relink.json')).write_text(json.dumps(report, indent=1, default=str) + '\n')
    if args.action in ('cases', 'negative', 'all'):
        if not args.executable or (args.action in ('negative', 'all') and not args.old):
            p.error('cases need --executable; negative and all also need --old')
        report, failed = cases(args, only_negative=args.action == 'negative')
        (args.out / 'pickups.json').write_text(json.dumps(report, indent=1, default=str) + '\n')
    print('pickups', 'FAIL' if failed else 'PASS')
    return int(failed)


if __name__ == '__main__':
    raise SystemExit(main())
