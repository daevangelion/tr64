#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Directed, byte-for-byte schema-5 cases; use PYTHONPATH=test/game:tools/cook."""
import argparse
from itertools import zip_longest
import json
import os
from pathlib import Path
import sys

sys.dont_write_bytecode = True
from compare import first_difference, validate
from stream import Run
import directed as d


class RuleFailure(ValueError):
    pass


def check(ok, why):
    if not ok:
        raise RuleFailure(why)


def placements(tick, slot, pose):
    return ('place', tick, slot, *pose)


def input_text(masks, commands, setup):
    return 'coop 1\n' + ''.join(' '.join(map(str, row)) + '\n' for row in (*setup, *commands, *masks))


def run_pair(env, masks, commands=(), setup=(), save=None):
    path = d.scratch('coop.masks')
    path.write_text(input_text(masks, commands, setup), encoding='ascii')
    oracle = [str(env.oracle), '--level', str(env.path), '--input', str(path), '--ticks', str(len(masks)), '--trace-schema', '5']
    port = [str(env.executable), '--game-level', env.name, '--game-input', str(path), '--game-ticks', str(len(masks)),
            '--game-trace-only', '--game-trace-schema', '5']
    fds = []
    runs = []
    try:
        for cmd, prefix, out in ((oracle, '--', '--out'), (port, '--game-', '--game-trace')):
            if save:
                fd = os.memfd_create('coop-save')
                fds.append(fd)
                cmd += [prefix+'save-tick',str(save[0]),prefix+'save-out',f'/proc/self/fd/{fd}']
                if save[1] is not None:
                    cmd += [prefix+'load-tick',str(save[1])]
            runs.append(Run(cmd, out, pass_fds=(fds[-1],) if save else ()))
        trace = []
        for tick, (a, b) in enumerate(zip_longest(*(run.trace for run in runs))):
            if a is None or b is None:
                raise ValueError(f'tick {tick}: missing record')
            row = json.loads(a)
            if a != b:
                raise ValueError(f'tick {tick}: {first_difference(row, json.loads(b)) or "serialization"}')
            validate(row, a.decode('ascii'), tick, env.name, 5)
            trace.append(row)
        results = [run.finish() for run in runs]
        runs.clear()
        if len(trace) != len(masks)+1 or any(code for code, _ in results):
            raise ValueError(f'bad trace length or process status: {len(trace)} {results}')
        data = tuple(os.pread(fd, os.fstat(fd).st_size, 0) for fd in fds)
        if save and not all(data):
            raise ValueError('empty save output')
        return trace, data
    except Exception as error:
        messages = [run.finish(kill=True)[1][-500:] for run in runs]
        raise ValueError(f'{env.name}: {error}; {messages}') from error
    finally:
        path.unlink()
        for fd in fds:
            os.close(fd)


def lara(row, slot):
    return row['players'][slot]['lara']


def player_position(row, slot):
    return lara(row, slot)['pos']


def target_slot(row, index):
    target = next((it['ai']['target'] for it in row['active'] if it['index']==index and it['ai']), -1)
    return next((p['slot'] for p in row['players'] if p and p['lara']['index']==target), None)


def paired(env, name, masks, commands, rule, control, error, setup=()):
    rows, _ = run_pair(env, masks, commands, setup)
    rule(rows)
    bad_masks, bad_commands, bad_setup = control
    negative, _ = run_pair(env, bad_masks, bad_commands, bad_setup)
    try:
        rule(negative)
    except RuleFailure as exc:
        if str(exc) != error:
            raise ValueError(f'{name}: wrong negative reason: {exc}') from exc
    else:
        raise ValueError(f'{name}: negative control passed')
    print(f'PASS {name} records={len(rows)} control={error}', flush=True)
    return rows


def join_walk(env):
    a = [(0, d.FORWARD)]*10 + [(0,0)] + [(d.FORWARD|d.WALK, d.FORWARD|d.LEFT)]*25 + [(0,0)]*10
    b = [(p,0) for p,_ in a]
    cmd = [('join',11)]
    def rule(rows):
        check(rows[10]['present']==1 and rows[11]['present']==3,'mid-level join did not add one actor')
        check(player_position(rows[11],0)==player_position(rows[11],1),'join did not copy pose')
        check(player_position(rows[11],0)!=player_position(rows[-1],0),'primary did not walk')
        check(player_position(rows[11],1)!=player_position(rows[-1],1),'secondary did not walk')
        check(player_position(rows[-1],0)!=player_position(rows[-1],1),'inputs were not independent')
    paired(env,'join-walk',a,cmd,rule,(b,cmd,()),'secondary did not walk')


def warp(env):
    origin = (0,75264,3072,3584,0)
    pad = [(0,0)]*60
    start = [('join',0),placements(0,1,(17,16896,6656,60768,0))]
    def at_partner(row):
        return player_position(row,1)==player_position(row,0) and lara(row,1)['room']==lara(row,0)['room']
    def alive_rule(rows):
        check(not at_partner(rows[29]) and at_partner(rows[30]),'living warp did not move to partner')
        check(lara(rows[30],1)['rot'][1]==origin[4] and rows[30]['alive']==3,'living warp did not reset facing')
        check(rows[30]['inventory']['counts'][d.SLOT['medi_big']]==0,'living warp altered medipacks')
    paired(env,'warp-alive',pad,start+[('warp',30,1)],alive_rule,(pad,start,()),'living warp did not move to partner')
    dead = start+[('health',7,1,0)]
    try_warp = dead+[('warp',30,1)]
    med = (('give',94,1),)
    def revive_rule(rows):
        check(rows[29]['alive']==1 and rows[30]['alive']==3 and at_partner(rows[30]),'dead Lara was not revived at partner')
        check(lara(rows[30],1)['health']==1000 and lara(rows[30],1)['oxygen']==1800,'revival did not restore health and air')
        check(rows[29]['inventory']['counts'][d.SLOT['medi_big']]==1 and rows[30]['inventory']['counts'][d.SLOT['medi_big']]==0,
              'revival did not spend exactly one large medipack')
    paired(env,'warp-revive',pad,try_warp,revive_rule,(pad,try_warp,()),'dead Lara was not revived at partner',med)
    refused, _ = run_pair(env,pad,try_warp)
    no_command, _ = run_pair(env,pad,dead)
    check(refused==no_command and refused[-1]['alive']==1,'refused warp changed state')
    print(f'PASS warp-refused records={len(refused)} control=revive with stock changes actor and inventory')


def death(env):
    masks = [(0,0)]*20 + [(0,d.WALK|d.FORWARD)]*30 + [(0,0)]*200
    cmd = [('join',0),('health',8,0,0),('health',75,1,0)]
    def rule(rows):
        check(all(row['alive']==2 and not row['deathReady'] for row in rows[8:75]),'survivor was stopped')
        check(player_position(rows[20],1)!=player_position(rows[50],1),'survivor could not walk')
        check(rows[-1]['alive']==0 and rows[-1]['deathReady']==1,'all-dead page did not become ready')
    paired(env,'death',masks,cmd,rule,(masks,cmd[:-1],()),'all-dead page did not become ready')


def ring(env):
    join=[('join',0)]
    seq=[(0,d.SELECT)]+[(d.FORWARD,d.FORWARD)]*39+[(d.SELECT,0)]
    seq += [(0,0)]*18+[(0,d.SELECT)]+[(0,0)]*60
    def rule(rows):
        check(all(row['ringOwner']==1 for row in rows[1:60]),'slot 1 did not retain menu input')
        check(all(player_position(row,slot)==player_position(rows[0],slot)
                  for row in rows[:60] for slot in (0,1)),'world moved while ring was active')
        check(rows[-1]['ringOwner']==-1,'slot 1 did not close its ring')
    control=seq[:59]+[(0,0)]+seq[60:]
    paired(env,'ring-owner-pause',seq,join,rule,(control,join,()),'slot 1 did not close its ring')
    def priority(rows):
        check(rows[1]['ringOwner']==0,'simultaneous ring press did not prefer slot 0')
    paired(env,'ring-priority',[(d.SELECT,d.SELECT)],join,priority,
           ([(0,d.SELECT)],join,()),'simultaneous ring press did not prefer slot 0')
    def handover(rows):
        check(rows[5]['present']==1 and rows[5]['ringOwner']==0,'leave did not rebind the active ring')
    paired(env,'ring-leave',seq[:10],join+[('leave',5)],handover,
           (seq[:10],join,()),'leave did not rebind the active ring')


def pickup(env):
    idx = 29  # Small medipack, room 17 in LEVEL1.
    item = env.level.data['items'][idx]
    check(item['type']==93,'pickup fixture does not name a small medipack')
    pose = (item['room'],item['x'],item['y'],item['z']-160,0)
    cmd = [('join',0),placements(0,0,pose),placements(0,1,pose)]
    masks = [(d.ACTION,d.ACTION)]*50
    def rule(rows):
        check(rows[1]['players'][0]['interaction']==idx and rows[1]['players'][1]['interaction']==-1,
              'simultaneous pickup did not reserve for slot 0')
        check([event for row in rows for event in row['pickups']]==[[93,0]],'pickup was not awarded once to slot 0')
        check(rows[-1]['inventory']['counts'][d.SLOT['medi_small']]==1,'shared count did not rise by one')
    paired(env,'pickup-race',masks,cmd,rule,([(0,d.ACTION)]*50,cmd,()),'simultaneous pickup did not reserve for slot 0')


def trigger(env):
    pad = (6,75264,3072,22016,0)  # Authored dart pad for item 5.
    cmd = [('join',0),placements(0,1,pad)]
    masks = [(0,0)]*8
    def rule(rows):
        check(lara(rows[0],0)['room']!=pad[0] and not any(it['index']==5 for it in rows[0]['active']),
              'dart emitter was already active or primary stood on pad')
        check(any(it['index']==5 for it in rows[1]['active']),'slot-1 floor trigger did not activate dart emitter')
    paired(env,'secondary-trigger',masks,cmd,rule,(masks,cmd[:1],()),'slot-1 floor trigger did not activate dart emitter')


def fixed_camera(env):
    pad = (9,50688,0,46592,0)  # GYM floor camera 0.
    cmd = [('join',0),placements(0,1,pad)]
    masks = [(0,0)]*6
    def rule(rows):
        check(any(row['players'][1]['camera']['lastIndex']==0 for row in rows[1:]),'slot-1 fixed camera did not trigger')
        check(all(row['players'][0]['camera']['lastIndex']==-1 for row in rows),'fixed camera changed the primary view')
    paired(env,'secondary-fixed-camera',masks,cmd,rule,(masks,cmd[:1],()),'slot-1 fixed camera did not trigger')


def level_end(env):
    from cuts import end_triggers
    pad = (*end_triggers(env.level)[0],0)
    cmd = [('join',0),placements(0,1,pad)]
    masks = [(0,0)]*40 + [(d.SELECT,0)] + [(0,0)]*40
    def rule(rows):
        check(rows[1]['ringOwner']==0 and rows[1]['inventory']['statsNextLevel']!=rows[1]['pendingLevel'],
              'slot-1 level end did not open primary statistics')
        check(rows[-1]['level']!=rows[0]['level'] and rows[-1]['present']==1,'level end did not advance and drop secondary')
    paired(env,'secondary-level-end',masks,cmd,rule,(masks,cmd[:1],()),'slot-1 level end did not open primary statistics')


def save(env):
    masks = [(0,0)]*60
    rows, a = run_pair(env,masks,[('join',0)],save=(30,40))
    solo, b = run_pair(env,masks,save=(30,40))
    check(rows[39]['present']==3 and rows[40]['present']==1,'checkpoint load did not drop secondary')
    check(a==b and rows[40]==solo[40],'checkpoint bytes or reconstructed state changed with second player')
    control, c = run_pair(env,masks,[('join',0)],setup=(('give',94,1),),save=(30,40))
    check(c[0]!=b[0] and c[1]!=b[1] and control[40]['inventory']['counts'][d.SLOT['medi_big']]==1,
          'save control did not see changed shared inventory')
    print(f'PASS save-solo-bytes records={len(rows)} source={len(a[0])} native={len(a[1])} control=shared stock changes both byte streams')


def enemy(env):
    idx=26  # LEVEL1 wolf in room 14.
    setup=(('activate',idx,0),)
    near=(14,22016,6656,51200,-32768)
    origin=(0,75264,3072,3584,0)
    masks=[(0,0)]*44
    tagged=[('join',0),placements(0,1,near)]
    cmd=tagged+[('leave',25)]
    def leave_rule(rows):
        check(target_slot(rows[4],idx)==1,'wolf did not target the secondary')
        check(rows[25]['present']==1 and target_slot(rows[25],idx)==0,'leave did not clear secondary target')
    paired(env,'leave-retarget',masks,cmd,leave_rule,(masks,tagged,setup),'leave did not clear secondary target',setup)
    paused=masks[:20]+[(d.SELECT,0)]+masks[21:]
    def immediate_rule(rows):
        check(rows[24]['ringOwner']==0 and target_slot(rows[24],idx)==1,
              'ring did not pause an already targeted wolf')
        ai=next(i['ai'] for i in rows[25]['active'] if i['index']==idx)
        check(rows[25]['present']==1 and ai['target']==-1 and ai['contact'][1]==0,
              'leave retained a stale target/contact while the world was paused')
    paired(env,'leave-cleanup-while-paused',paused,cmd,immediate_rule,
           (paused,tagged,setup),'leave retained a stale target/contact while the world was paused',setup)
    masks=[(0,0)]*160
    cmd=tagged+[placements(80,0,near),placements(80,1,origin)]
    def fight_rule(rows):
        check(target_slot(rows[4],idx)==1,'wolf did not target the secondary')
        check(rows[80]['present']==3 and target_slot(rows[80],idx)==0,'nearest actor changed without retarget')
        check(lara(rows[-1],0)['health']<lara(rows[80],0)['health'] and
              lara(rows[-1],1)['health']==lara(rows[80],1)['health'],
              'wolf did not land a hit on its new target')
    paired(env,'fight-retarget',masks,cmd,fight_rule,(masks,tagged,setup),'nearest actor changed without retarget',setup)


def secondary_placed(env,name,pose,masks,predicate,reason,setup=()):
    cmd=[('join',0),placements(0,1,pose)]
    pads=[(0,m) for m in masks]
    paired(env,name,pads,cmd,lambda rows: check(predicate(rows),reason),(pads,cmd[:1],setup),reason,setup)


def midas(env):
    import items
    jobs=items.midas_jobs(env)
    death=next(j for j in jobs if j['family']=='midas-death')
    secondary_placed(env,'midas-death',death['start'],[0]*120,
                     lambda r: any(lara(row,1)['state']==51 and row['players'][1]['camera']['mode']==6 and
                                   row['players'][0]['camera']['mode']!=6 for row in r) and r[-1]['alive']==1,
                     'Midas did not take over only the eligible Lara')
    _,hand=env.items(items.MIDAS_HAND)[0]
    # The west approach does not slide Lara onto the hand's lethal square.
    use=next(j for j in jobs if j['family']=='midas-use' and j['start'][1]==hand['x']-540 and j['yaw']==0)
    solo=d.Script(env,use['start'],setup=use['give'])
    use['procedure'](solo)
    secondary_placed(env,'midas-use',use['start'],solo.masks,
                     lambda r: any(row['ringOwner']==1 for row in r) and
                     any(lara(row,1)['state']==50 and lara(row,0)['state']!=50 for row in r) and
                     r[-1]['inventory']['counts'][d.SLOT['puzzle1']]==1 and
                     r[-1]['inventory']['counts'][d.SLOT['leadbar']]==1,
                     'Midas did not consume the secondary ring choice',use['give'])


def doppelganger(env):
    idx,item=env.items(6)[0]
    start=d.approach_points(env,item,distance=512,tol=768,clear=762)[0]
    cmd=[('join',0),placements(0,1,start),('warp',2,1),('hit',3,0,idx,20)]
    masks=[(0,0)]*15
    setup=(('activate',idx,0),)
    def rule(rows):
        check(target_slot(rows[1],idx)==1 and target_slot(rows[3],idx)==1,
              'doppelganger did not retain its first mirror target')
        check(lara(rows[2],0)['health']-lara(rows[3],0)['health']==200 and
              lara(rows[2],1)['health']==lara(rows[3],1)['health'],
              'doppelganger returned the hit to someone other than its shooter')
    paired(env,'doppelganger-shooter',masks,cmd,rule,
           (masks,cmd[:-1]+[('hit',3,1,idx,20)],setup),
           'doppelganger returned the hit to someone other than its shooter',setup)


def hammer(env):
    _,item=env.items(44)[0]
    x,z=item['x'],item['z']-3072
    room,y=d.ground(env,item['room'],x,z,clear=512)
    secondary_placed(env,'thor-impact',(room,x,y,z,-32768),[0]*150,
                     lambda r: any(row['alive']==1 and lara(row,1)['hazardCause']==5 for row in r),
                     'Thor did not kill the second actor in the footprint')


def lightning(env):
    idx,item=env.items(46)[0]
    # Place inside the sphere before the first strike. Other actors can
    # change the shared lightning recharge RNG, so do not assume a solo jump phase.
    pose=(item['room'],item['x']-512,item['y']+640,item['z'],16384)
    secondary_placed(env,'lightning',pose,[0]*24,
                     lambda r: any(lara(a,1)['health']-lara(b,1)['health']==400 and
                                   lara(a,0)['health']==lara(b,0)['health'] for a,b in zip(r,r[1:])),
                     'lightning did not strike only the exposed actor',(('activate',idx,0),))


def slam(env):
    from burn import pad_starts
    _,pose,_=next(pad_starts(env,[32]))
    secondary_placed(env,'slam',pose,[0]*60+[d.FORWARD]*60+[0]*120,
                     lambda r: any(lara(a,1)['health']-lara(b,1)['health']==400 and
                                   lara(a,0)['health']==lara(b,0)['health'] for a,b in zip(r,r[1:])),
                     'slam did not bite the overlapping second Lara')


def sword(env):
    idx,item=env.items(43)[0]
    x,z=item['x'],item['z']
    room,y=d.ground(env,item['room'],x,z,clear=1024)
    secondary_placed(env,'damocles-sword',(room,x,y,z,-32768),[0]*60+[d.FORWARD]*60+[0]*120,
                     lambda r: any(lara(a,1)['health']-lara(b,1)['health']==100 for a,b in zip(r,r[1:])) and
                     any(it['index']==idx and it['state']==2 for row in r for it in row['active']),
                     'sword did not fall and wound the second actor')


def flame(env):
    idx,item=env.items(179)[0]
    x,z=item['x'],item['z']
    room,y=d.ground(env,item['room'],x,z,clear=1024)
    secondary_placed(env,'flame',(room,x,y,z,0),[0]*50,
                     lambda r: any(row['players'][1]['burn'][3] and not row['players'][0]['burn'][3] for row in r) and
                     any(lara(a,1)['health']-lara(b,1)['health']==5 for a,b in zip(r,r[1:])),
                     'flame did not bind its burn to the exposed Lara',(('activate',idx,0),))


def trap_floor(env):
    idx,item=env.items(35)[0]
    pose=(item['room'],item['x'],item['y']-512,item['z'],0)
    secondary_placed(env,'falling-floor',pose,[0]*70,
                     lambda r: any(it['index']==idx and it['state']==2 for row in r for it in row['active']),
                     'falling floor did not test the nearer actor height',(('activate',idx,0),))


def block(env):
    idx,item=env.items(48)[0]
    pose=d.approach_points(env,item,distance=600,clear=762)[0]
    cmd=[('join',0),placements(0,0,pose),placements(0,1,pose)]
    seq=[d.ACTION]*45+[d.ACTION|d.FORWARD]*50+[0]*50
    def rule(r):
        check(any(row['players'][0]['interaction']==idx for row in r) and
              all(row['players'][1]['interaction']!=idx for row in r),'block reservation was not awarded to slot 0')
        check(any(row['world'][0]!=r[0]['world'][0] for row in r),'reserved block never changed the floor')
    paired(env,'push-block',[(m,m) for m in seq],cmd,rule,
           ([(0,m) for m in seq],cmd,()),'block reservation was not awarded to slot 0')


def switch_bridge(env):
    from drawbridge import lower_case
    case=lower_case(env,22)
    secondary_placed(env,'switch-drawbridge',case['start'],case['masks'],
                     lambda r: any(row['players'][1]['interaction']==69 for row in r) and
                     any(it['index']==22 and it['state']==1 for row in r for it in row['active']),
                     'second Lara did not pull the switch and lower the bridge')


def keyhole(env):
    import items
    idx,item=env.items(137)[0]
    start=items.hole_starts(env,item)[0][2]
    _,setup,procedure=next(v for v in items.variants(129,130) if v[0]=='action-open')
    solo=d.Script(env,start,setup=setup)
    procedure(solo)
    secondary_placed(env,'keyhole',start,solo.masks,
                     lambda r: any(row['ringOwner']==1 for row in r) and
                     any(lara(row,1)['state']==42 for row in r) and
                     r[-1]['inventory']['counts'][d.SLOT['key1']]==0,
                     f'second Lara did not use her reserved keyhole {idx}',setup)


def scion_holder(env):
    # 550 units south of the authored holder, in its interaction height window.
    start=(50,52736,-18944,45018,0)
    secondary_placed(env,'scion-holder-finish',start,[d.ACTION]*180,
                     lambda r: any(row['present']==3 and row['players'][1]['camera']['mode']==6 and
                                   row['players'][0]['camera']['mode']!=6 for row in r) and
                     r[-1]['present']==0 and r[-1]['cinematic'] is not None,
                     'Scion holder did not take over the secondary camera and enter the cutscene')


def scion_pickup(env):
    # Same room-5 pedestal and y offset as the solo pickup (cuts.pickup_placements).
    from cuts import pickup_placements
    start=next(pickup_placements(env.level,143))
    secondary_placed(env,'scion-pickup',start,[d.ACTION]*140,
                     lambda r: any([143,1] in row['pickups'] for row in r) and
                     any(row['players'][1]['camera']['mode']==6 and
                         row['players'][0]['camera']['mode']!=6 for row in r),
                     'Scion pickup did not award to the owner of the cinematic')


def boulder(env):
    idx,item=env.items(38)[0]
    x,z=item['x'],item['z']
    room,y=d.ground(env,item['room'],x,z,clear=762)
    secondary_placed(env,'boulder',(room,x,y,z,0),[0]*35,
                     lambda r: any(row['alive']==1 and lara(row,1)['hazardCause']==2 for row in r),
                     'boulder did not hit the secondary collision actor',(('activate',idx,0),))


def floor_spikes(env):
    item=env.level.data['items'][11]
    check(item['type']==37,'spike fixture does not name spikes')
    x,z=item['x'],item['z']
    room,y=d.ground(env,item['room'],x,z,clear=1024)
    secondary_placed(env,'spikes',(room,x,y,z,-32768),[0]*60+[d.FORWARD]*60+[0]*120,
                     lambda r: any(row['alive']==1 and lara(row,1)['hazardCause']==1 for row in r),
                     'spikes did not kill the secondary collision actor')


def falling_ceiling(env):
    idx=24
    it=env.level.data['items'][idx]
    x,z=it['x'],it['z']
    room,y=d.ground(env,it['room'],x,z,clear=1024)
    secondary_placed(env,'falling-ceiling',(room,x,y,z,0),[0]*100,
                     lambda r: [lara(a,1)['health']-lara(b,1)['health'] for a,b in zip(r,r[1:])
                                if lara(a,1)['health']!=lara(b,1)['health']][:3]==[300,300,300],
                     'falling ceiling did not apply the fixed damage to the secondary',(('activate',idx,0),))


def rolling_block(env):
    from movers import slide_case
    case=slide_case(env,49)
    secondary_placed(env,'rolling-block',case['start'],case['masks'],
                     lambda r: any(it['index']==49 and it['goal']==1 for row in r for it in row['active']) and
                     any(row['world'][0]!=r[0]['world'][0] for row in r),
                     'secondary switch did not start the rolling block')


def lava_particles(env):
    # LEVEL10C emitter 35: the cell from the existing burn.py particle check.
    secondary_placed(env,'lava-particle',(21,47616,-10752,32256,0),[0]*160,
                     lambda r: any(it['type']==176 for row in r for it in row['active']) and
                     any(lara(a,1)['health']-lara(b,1)['health']==10 for a,b in zip(r,r[1:])),
                     'lava particle did not hit the overlapping secondary')


def lava_floor(env):
    import lava
    case=lava.cases(env,lava.lava_cells(env),limit=1)[0]
    masks=[(0,d.FORWARD)]*80
    cmd=[('join',0),placements(0,1,case['lava'])]
    def rule(r):
        check(any(row['alive']==1 and lara(row,1)['hazardCause']==9 for row in r),
              'lava floor did not burn the second Lara')
    paired(env,'lava-floor',masks,cmd,rule,(masks,cmd[:1],()),'lava floor did not burn the second Lara')


def scion_target(env):
    idx,_=env.items(145)[0]
    setup=(('activate',idx,0),)
    masks=[(0,0)]*8
    cmd=[('join',0),('hit',1,1,idx,1000)]
    def rule(r):
        check(any(all(p['camera']['bounce'] for p in row['players']) for row in r[1:]),
              'Scion destruction did not shake both cameras')
    paired(env,'scion-target',masks,cmd,rule,(masks,cmd[:1],setup),'Scion destruction did not shake both cameras',setup)


def encounter(env):
    kind=163 if env.name=='level10b' else 161
    idx,item=env.items(kind)[0]
    pose=d.approach_points(env,item,distance=1024,tol=512,clear=762)[0]
    secondary_placed(env,'small-egg' if kind==163 else 'centaur-statue',pose,[0]*90,
                     lambda r: any(it['index']==next(a['headOffset'] for a in r[0]['active'] if a['index']==idx)
                                   for row in r[1:] for it in row['active']),
                     'triggered encounter did not hatch near the secondary',(('activate',idx,0),))


def pierre(env):
    idx=3  # LEVEL4 unflagged Pierre starts retreating at 40 HP.
    secondary_placed(env,'pierre-second-camera',(0,29184,0,39936,0),[0]*120,
                     lambda r: any(it['index']==idx and it['health']==40 and it['ai']
                                   for it in r[-1]['active']),
                     'Pierre retired despite the second camera seeing him',
                     (('item',idx,40),('activate',idx,0)))


def centaur_bolt(env):
    idx=51  # LEVEL8C centaur in room 40.
    secondary_placed(env,'centaur-projectile',(40,27136,-2048,32256,-16384),[0]*220,
                     lambda r: any(it['type']==173 for row in r for it in row['active']) and
                     any(lara(a,1)['health']-lara(b,1)['health']==100 and
                         lara(a,0)['health']==lara(b,0)['health'] for a,b in zip(r,r[1:])),
                     'centaur missile missed the secondary contact sphere',(('activate',idx,0),))


def rex_fatal(env):
    idx=55
    secondary_placed(env,'rex-fatal',(60,48088,3584,15872,16384),[0]*260,
                     lambda r: any(it['index']==idx and it['state']==8
                                   for row in r for it in row['active']) and
                     any(lara(row,1)['health']==0 and lara(row,0)['health']==1000 for row in r),
                     'Rex fatal bite did not take over its secondary target',(('activate',idx,0),))


def dart_contact(env):
    idx=5
    secondary_placed(env,'dart-contact',(6,76288,3072,22016,0),[0]*130,
                     lambda r: any(it['type']==39 for row in r for it in row['active']) and
                     any(lara(a,1)['health']-lara(b,1)['health']==50 and
                         lara(a,0)['health']==lara(b,0)['health'] for a,b in zip(r,r[1:])),
                     'dart did not hit the secondary collision actor',(('activate',idx,0),))


def lava_wedge(env):
    idx=15
    secondary_placed(env,'lava-wedge',(19,39424,-512,37376,0),[0]*90,
                     lambda r: any(row['alive']==1 and lara(row,1)['hazardCause']==9 for row in r),
                     'lava wedge did not kill the secondary collision actor',(('activate',idx,0),))


def adam_grab(env):
    setup=(('activate',103,0),)  # LEVEL10C big egg whose prepared child is Adam.
    masks=[(0,0)]*220
    cmd=[('join',0),placements(0,1,(36,64000,-15616,12000,0)),('health',0,1,500)]
    def rule(r):
        check(any(it['type']==34 and it['state']==11 for row in r for it in row['active']) and
              any(lara(row,1)['health']==0 and lara(row,0)['health']==1000 for row in r),
              'Adam did not grab and kill the secondary target')
    paired(env,'adam-grab',masks,cmd,rule,(masks,[cmd[0],cmd[-1]],setup),
           'Adam did not grab and kill the secondary target',setup)


def current(env):
    secondary_placed(env,'swim-current',(2,36352,640,77312,0),[0]*40,
                     lambda r: all(row['players'][0]['flow']==[-1,0] for row in r) and
                     any(row['players'][1]['flow']==[1,1] and lara(row,1)['nav']['cellsCount']>0
                         for row in r[2:]),
                     'swimming current did not populate the second actor path only')


CASES = {name:(level, routine) for name, level, routine in (
    ('join','level1',join_walk), ('warp','level1',warp), ('death','level1',death), ('pickup','level1',pickup),
    ('trigger','level1',trigger), ('camera','gym',fixed_camera), ('end','level1',level_end), ('save','gym',save),
    ('ring','gym',ring),
    ('enemy','level1',enemy),
    ('midas','level6',midas), ('doppelganger','level10b',doppelganger), ('hammer','level4',hammer),
    ('lightning','level10c',lightning), ('slam','level7b',slam), ('sword','level4',sword),
    ('flame','level6',flame), ('falling-floor','level1',trap_floor), ('block','level2',block),
    ('switch','level8b',switch_bridge), ('keyhole','level4',keyhole), ('scion-target','level10c',scion_target),
    ('small-egg','level10b',encounter), ('centaur-statue','level7b',encounter),
    ('scion-holder','level10b',scion_holder), ('scion-pickup','level3b',scion_pickup),
    ('boulder','level3b',boulder), ('spikes','level3b',floor_spikes), ('falling-ceiling','level3b',falling_ceiling),
    ('rolling-block','level3b',rolling_block), ('lava-floor','level10a',lava_floor),
    ('lava-particles','level10c',lava_particles),
    ('pierre','level4',pierre), ('centaur-bolt','level8c',centaur_bolt),
    ('rex','level3a',rex_fatal), ('dart','level1',dart_contact), ('lava-wedge','level10c',lava_wedge),
    ('adam','level10c',adam_grab), ('current','level3a',current),
)}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable',required=True,type=Path)
    p.add_argument('--oracle',required=True,type=Path)
    p.add_argument('--case',action='append',choices=CASES)
    args=p.parse_args()
    for case in args.case or CASES:
        level, routine=CASES[case]
        routine(d.Env(level,args.executable,args.oracle))


if __name__=='__main__':
    main()
