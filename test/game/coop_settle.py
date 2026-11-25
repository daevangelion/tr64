#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare authored co-op retirements to uninterrupted/pre-commit controls."""
from pathlib import Path
import argparse
import hashlib
import os
import subprocess
import sys
sys.dont_write_bytecode=True
from coop import check, paired, run_pair, lara, placements, d
import drawbridge
import items

JOIN=[('join',0)]
IDLE=(0,0)


def variants(tick, due):
    return [('leave', [('leave-result',tick,1),('leave',due+1)], ('leave',1)),
            ('warp', [('warp-result',tick,1,3),('warp-result',due+1,1,0)], ('warp-result',1,1,0)),
            ('death',[('health',tick,1,0)],('health',1,1,0))]


def unchanged_while_busy(label,rows,normal,begin,due):
    if label!='death':
        check(rows[begin:due+1]==normal[begin:due+1],f'{label} BUSY changed a simulation record')
        check(rows[due+1]['present']==(1 if label=='leave' else 3),f'{label} retry was not applied')


def active(rows, index):
    return next((row['tick'] for row in rows if any(a['index']==index for a in row['active'])),None)


def award(rows, item):
    return [row['tick'] for row in rows if [item,1] in row['pickups']]


def activate_case(env, name, idx, target, pose, masks, setup=()):
    cmd=JOIN+[placements(0,1,pose)]
    normal,_=run_pair(env,masks,cmd,setup)
    activation=active(normal,target)
    start=next(row['tick'] for row in normal if row['players'][1]['interaction']==idx and
               (not setup or lara(row,1)['state'] in (42,43)))
    check(activation is not None and activation>start+1,f'{name}: no delayed authored activation in the control')
    for label,commands,early_cmd in variants(start+1,activation):
        if label=='death':
            commands += [('save-blocked',start+1,1),('leave-result',start+1,1),
                         ('warp-result',start+1,1,3),('save-blocked',activation+1,0)]
        rows,_=run_pair(env,masks,cmd+commands,setup)
        unchanged_while_busy(label,rows,normal,start+1,activation)
        if label=='death':
            check(rows[start+1]['retiredActions'][0][:2]==[idx,lara(normal[start],1)['index']] and
                  not rows[activation]['retiredActions'],f'{name}: lost the retired actor or failed to drain')
        check(active(rows,target)==activation,f'{name}/{label}: activation not on uninterrupted tick {activation}')
        early,_=run_pair(env,[IDLE]*len(masks),cmd+[early_cmd],setup)
        check(active(early,target) is None and not any(row['retiredActions'] for row in early),
              f'{name}/{label}: pre-commit interruption fired an action')
        print(f'PASS {name}/{label}: activation at {activation}; pre-commit control inert',flush=True)


def dry_switch(env):
    case=drawbridge.lower_case(env,22)
    masks=[(0,m) for m in case['masks']]+[IDLE]*90
    activate_case(env,'dry-switch',69,22,case['start'],masks)
    cmd=JOIN+[placements(0,1,case['start'])]
    neutral=placements(100,0,case['start'])
    def bridge(rows):
        return [(row['tick'],[(a['flags'],a['state'],a['timer']) for a in row['active'] if a['index']==22])
                for row in rows]
    normal,_=run_pair(env,masks,cmd+[neutral])
    retired,_=run_pair(env,masks,cmd+[('leave-result',2,1),('join-refused',3),('leave',48),('join',49),neutral])
    check(bridge(retired)==bridge(normal),
          'the late actor re-fired the consumed switch or changed the bridge mask')
    check(retired[47]['present']==3 and retired[48]['present']==1 and retired[49]['present']==3 and
          retired[49]['players'][1]['camera']['lastIndex']==-1,'switch camera reached a replacement Lara')
    print('PASS dry-switch repeat sector entry: authored mask and timer unchanged',flush=True)


def water_switch(env):
    # Neptune's pool switch 100: approach on axis and settle in real UW tread.
    pose=(53,34304,28060,52736,-16384)
    activate_case(env,'water-switch',100,64,pose,[IDLE]*36+[(0,d.ACTION)]*210+[IDLE]*70)


def hole(env):
    idx,match,target=(80,129,78) if env.name=='level4' else (30,110,29)
    pose=items.hole_starts(env,env.level.data['items'][idx])[0][2]
    _,setup,step=next(v for v in items.variants(match,match+1) if v[0]=='action-open')
    script=d.Script(env,pose,setup=setup)
    step(script)
    masks=[(0,m) for m in script.masks]+[IDLE]*50
    if idx==80:
        activate_case(env,'key',idx,target,pose,masks,setup)
        return
    # Midas's three slots contribute partial masks; one does not activate the door.
    cmd=[placements(0,0,pose),*JOIN,placements(0,0,(0,50688,-512,18944,0))]
    normal,_=run_pair(env,masks,cmd,setup)
    spend=next(row['tick'] for row in normal if row['inventory']['counts'][items.PICKUP_SLOT[match]]==0)
    release=next(row['tick'] for row in normal[spend:] if lara(row,1)['weaponState']==0)
    for label,commands,early_cmd in variants(65,release):
        if label=='death':
            commands += [('save-blocked',65,1),('save-blocked',release+1,0)]
        rows,_=run_pair(env,masks,cmd+commands,setup)
        unchanged_while_busy(label,rows,normal,65,release)
        if label=='death':
            check(rows[65]['retiredActions'][0][0]==idx and rows[release-1]['retiredActions'] and
                  not rows[release]['retiredActions'],'puzzle death lost its authored release tick')
        early,_=run_pair(env,[IDLE]*len(masks),cmd+[early_cmd],setup)
        check(early[-1]['inventory']['counts'][items.PICKUP_SLOT[match]]==1 and
              rows[release]['inventory']['counts'][items.PICKUP_SLOT[match]]==0,
              f'puzzle/{label}: pre-commit control did not preserve the key stock')
        print(f'PASS puzzle/{label}: linked partial mask at {release}; pre-commit keeps stock',flush=True)


def collect(env, name, pose, masks, receipt, setup=()):
    cmd=JOIN+[placements(0,1,pose)]
    normal,_=run_pair(env,masks,cmd,setup)
    committed=next(row['tick'] for row in normal if receipt(row)==1)
    due=next(row['tick'] for row in normal[committed+1:] if row['players'][1]['interaction']==-1)
    for label,commands,early_cmd in variants(committed+1,due):
        rows,_=run_pair(env,masks,cmd+commands,setup)
        unchanged_while_busy(label,rows,normal,committed+1,due)
        check(receipt(rows[-1])==1,f'{name}/{label}: spent object was rolled back or duplicated')
        early,_=run_pair(env,[IDLE]*len(masks),cmd+[early_cmd],setup)
        check(receipt(early[-1])==0,f'{name}/{label}: pre-commit control granted the award')
        print(f'PASS {name}/{label}: exactly one award; pre-commit no award',flush=True)


def pickup(env):
    item=env.level.data['items'][29]
    pose=(item['room'],item['x'],item['y'],item['z']-160,0)
    collect(env,'pickup',pose,[(0,d.ACTION)]*8+[IDLE]*100,lambda row:row['inventory']['counts'][d.SLOT['medi_small']])


def midas(env):
    _,hand=env.items(items.MIDAS_HAND)[0]
    use=next(j for j in items.midas_jobs(env) if j['family']=='midas-use' and
             j['start'][1]==hand['x']-540 and j['yaw']==0)
    s=d.Script(env,use['start'],setup=use['give']); use['procedure'](s)
    collect(env,'Midas',use['start'],[(0,m) for m in s.masks],
            lambda row:row['inventory']['counts'][d.SLOT['puzzle1']],use['give'])


def scion(env):
    holder=env.name=='level10b'
    pose=(50,52736,-18944,45018,0) if holder else (62,36352,3072,59629,0)
    start=JOIN+[placements(0,1,pose)]
    def complete(row):
        return row['cinematic'] is not None if holder else row['inventory']['page']==3  # statistics
    tick=2 if holder else 46
    masks=[(0,d.ACTION)]*(1 if holder else 45)+[IDLE]*220
    normal,_=run_pair(env,masks,start)
    due=next(row['tick'] for row in normal if complete(row))
    check(due>tick,'Scion unexpectedly completed before interruption')
    for label,commands,early in variants(tick,due):
        if holder and label!='death':commands=commands[:1] # completion changes the level; no same-level retry
        def rule(rows):
            check(sum(complete(row) and not complete(prev) for prev,row in zip(rows,rows[1:]))==1,
                  'committed Scion did not cause exactly one level completion')
            if not holder:check(award(rows,143)==[45],'Scion was not awarded exactly once')
            check(complete(rows[due]) and not complete(rows[due-1]),'Scion changed its authored completion tick')
        control=([IDLE]*len(masks),start+[early],())
        paired(env,f'scion-{env.name}/{label}',masks,start+commands,rule,control,
               'committed Scion did not cause exactly one level completion')
    if holder:
        masks=[(0,d.ACTION),IDLE,(d.SELECT,0)]+[IDLE]*40+[(d.SELECT,0)]+[IDLE]*240
        rows,_=run_pair(env,masks,start+[('health',4,1,0),('leave-result',4,1),('save-blocked',4,1)])
        check(rows[4]['completionOwed'] and rows[4]['ringOwner']==0 and
              all(row['completionOwed'] and not complete(row) for row in rows[4:43]),
              'open ring lost or prematurely settled Scion completion')
        check(sum(complete(row) and not complete(prev) for prev,row in zip(rows,rows[1:]))==1 and
              not rows[-1]['completionOwed'],'Scion did not settle exactly once after ring close')
        control,_=run_pair(env,masks[:43]+[IDLE]*(len(masks)-43),start+[('health',4,1,0)])
        check(control[-1]['completionOwed'] and not complete(control[-1]),'withholding ring close failed to keep Scion pending')
        print('PASS Scion holder while paused: completion waits for ring close',flush=True)
        same_tick=masks[:2]+[(d.SELECT,0)]+masks[3:]
        rows,_=run_pair(env,same_tick,start+[('health',3,1,0)])
        check(rows[3]['ringOwner']==0 and rows[3]['completionOwed'] and not complete(rows[3]) and
              complete(rows[-1]),'a same-tick ring open did not delay Scion completion')
        print('PASS Scion holder same-tick ring opening: pending then completed',flush=True)



def other_actor_free(env):
    case=drawbridge.lower_case(env,22)
    masks=[(0,m) for m in case['masks']]+[IDLE]*90
    starts=JOIN+[placements(0,1,case['start'])]
    rows,_=run_pair(env,masks,starts+[('warp-result',2,0,0)])
    check(lara(rows[2],0)['pos']==lara(rows[2],1)['pos'] and active(rows,22)==47,
          'a living Lara could not warp to the busy partner')
    # A dead owner cannot leave or spend a medipack until the switch action fires.
    cmds=starts+[('health',2,1,0),('warp-result',3,1,3),('leave-result',3,1),
                ('save-blocked',3,1),('save-blocked',48,0),('warp-result',48,1,0)]
    rows,_=run_pair(env,masks,cmds,(('give',94,1),))
    stock=d.SLOT['medi_big']
    check(rows[3]['inventory']['counts'][stock]==1 and lara(rows[47],1)['health']==0 and
          lara(rows[48],1)['health']==1000 and rows[48]['inventory']['counts'][stock]==0,
          'a busy warp spent medicine or the later retry did not revive')
    print('PASS partner warp allowed; busy dead actor pays only after settling',flush=True)


def block(env):
    idx=0; pusher=(0,60928,1024,28584,0); destination=(19,60928,1024,30208,0)
    cmd=JOIN+[placements(0,0,destination),placements(0,1,pusher)]
    masks=[(0,d.ACTION)]*45+[(0,d.ACTION|d.FORWARD)]*25+[IDLE]*165
    normal,_=run_pair(env,masks,cmd)
    def state(row):return next(b for b in row['blocks'] if b[0]==idx)
    dest=state(normal[-1]); check(dest[2]==[60928,1024,30208] and dest[4]==0,'block control did not finish a full cell with floor raised')
    due=next(row['tick'] for row in normal[80:] if state(row)==dest)
    for label,commands,early_cmd in variants(80,due):
        rows,_=run_pair(env,masks,cmd+commands)
        unchanged_while_busy(label,rows,normal,80,due)
        check(state(rows[80])==state(normal[80]) and state(rows[-1])==dest and rows[-1]['world']==normal[-1]['world'],
              f'block/{label}: interrupted push changed landing/floor/timing')
        check(lara(rows[-1],0)['health']>0 and lara(rows[-1],0)['pos'][1]<=dest[4],
              f'block/{label}: partner embedded in the landed block')
        early,_=run_pair(env,[IDLE]*len(masks),cmd+[early_cmd])
        check(state(early[-1])==state(normal[0]),f'block/{label}: pre-push control moved the block')
        print(f'PASS block/{label}: full-cell landing {dest}, pre-push stationary',flush=True)


def pit(env):
    pusher=(0,60928,1024,28584,0)
    cmd=JOIN+[placements(0,1,pusher)]
    masks=[(0,d.ACTION)]*45+[(0,d.ACTION|d.FORWARD)]*25+[IDLE]*185
    # Both reference obstacle tests forbid pushing onto a tile already lower.
    # Open a test-only pit after a legal push commits, before the authored
    # terminal translation. All settling and gravity are production code.
    drop=('pit',120,19,60928,30208,4)
    def state(row):return next(b for b in row['blocks'] if b[0]==0)
    flat,_=run_pair(env,masks,cmd)
    normal,_=run_pair(env,masks,cmd+[drop])
    final=state(normal[-1])
    check(final[2]==[60928,2048,30208] and final[4]==1024 and final!=state(flat[-1]),
          'pit control did not land on the lower floor')
    check(any(state(row)[2][2]==30208 and 1024<state(row)[2][1]<2048 for row in normal),
          'pit translated but never fell')
    due=next(row['tick'] for row in normal[80:] if state(row)==final)
    for label,commands,_ in variants(80,due):
        rows,_=run_pair(env,masks,cmd+sorted([*commands,drop],key=lambda c:c[1]))
        unchanged_while_busy(label,rows,normal,80,due)
        check(state(rows[-1])==final and normal[-1]['world']==rows[-1]['world'] and
              lara(rows[-1],0)['health']==1000, f'pit/{label}: unsafe or divergent landing')
        no_drop,_=run_pair(env,masks,cmd+commands)
        check(state(no_drop[-1])==state(flat[-1]) and state(no_drop[-1])!=final,
              f'pit/{label}: no-drop control entered the pit')
        print(f'PASS pit/{label}: fell and landed at {final}; no-drop landed at original height',flush=True)


def flipped_warp(env):
    # The query runs between the real level flip and the next actor tick.
    pose=(22,29184,7486,42496,0)
    cmd=JOIN+[placements(0,0,pose)]
    masks=[IDLE]*62
    def rule(rows):
        check(rows[61]['save']['flipped']==1,'authored rooms were not flipped')
        check(lara(rows[60],0)['water']==3 and lara(rows[61],1)['water']!=3 and
              lara(rows[61],1)['pos']!=lara(rows[60],0)['pos'],'warped to the obsolete underwater position')
    paired(env,'warp-after-wet-room-flip',masks,cmd+[('flip',61),('warp',61,1)],rule,
           (masks,cmd+[('warp',61,1)],()),'authored rooms were not flipped')


def shaft_join(env):
    def rule(rows):
        check(rows[0]['present']==3,'slot 1 did not join at the authored shaft spawn')
        p=lara(rows[0],1)
        check(lara(rows[0],0)['room']==19 and p['room']==20 and p['pos']==[57856,200,1536] and p['water']==3,
              'shaft join did not use the reachable swim pose beneath the spawn')
    paired(env,'join-at-authored-shaft',[],JOIN,rule,
           ([],[],()),'slot 1 did not join at the authored shaft spawn')


def loaded_pose(env):
    saved=(14,40448,4608,60928,0)
    masks=[IDLE]*35
    cmd=[placements(0,0,saved),*JOIN]
    rows,blobs=run_pair(env,masks,cmd+[('join',32)],save=(30,31))
    control,_=run_pair(env,masks,cmd,save=(30,31))
    check(all(blobs) and rows[0]['present']==3 and rows[31]['present']==control[32]['present']==1 and rows[32]['present']==3,
          'the load did not discard slot 1 or join on the next tick')
    p=lara(rows[32],1)
    check(p['pos']==lara(rows[31],0)['pos']==[40448,4711,60929] and p['water']==3 and p['room']==14,
          'a post-load join used the dry authored spawn')
    print('PASS join on the first tick after load copies the restored underwater actor',flush=True)


def solo_puzzle_saves(env):
    # Frozen pre-change 67bfedcd7e native and m9-coop-review fixed snapshots;
    # the LEVEL6 route is ordinary slot-0 input, not the co-op frame parser.
    pose=items.hole_starts(env,env.level.data['items'][30])[0][2]
    _,setup,step=next(v for v in items.variants(110,111) if v[0]=='action-open')
    s=d.Script(env,pose,setup=setup)
    step(s)
    path=d.scratch('solo-puzzle.masks')
    path.write_text('\n'.join(('start '+' '.join(map(str,pose)), 'give 110 1', *map(str,s.masks)))+'\n')
    engines=[
        ([env.executable,'--game-level','level6','--game-input',path,'--game-ticks',str(len(s.masks)),
          '--game-trace-only'],'--game-','game test save failed',4250,
         '92945a4748abd1fa3d5af89f399aa33fbe9bb1ca1b0a366f9b54fdb0a2a5f783'),
        ([env.oracle,'--level',env.path,'--input',path,'--ticks',str(len(s.masks)),'--out','/dev/null'],
         '--','source gameSave failed',4220,'151404fd207833296e1c549cc0bd89a99aa6b365ca08a42a3c2312a4e02431ac')]
    try:
        for base,prefix,reason,size,digest in engines:
            for tick in (134,162,164,170):
                fd=os.memfd_create('solo-puzzle-save')
                try:
                    run=subprocess.run([*base,prefix+'save-tick',str(tick),
                                        prefix+'save-out',f'/proc/self/fd/{fd}'],pass_fds=(fd,),capture_output=True)
                    data=os.pread(fd,os.fstat(fd).st_size,0)
                finally:
                    os.close(fd)
                if tick==134:
                    check(not run.returncode and len(data)==size and hashlib.sha256(data).hexdigest()==digest,
                          f'{prefix} altered save at {tick}: exit={run.returncode} bytes={len(data)} '
                          f'sha256={hashlib.sha256(data).hexdigest()} error={run.stderr[-240:]!r}')
                else:
                    check(run.returncode and not data and reason.encode() in run.stderr,
                          f'{prefix} changed the puzzle save rejection at tick {tick}')
    finally:
        path.unlink()
    print('PASS solo LEVEL6 checkpoint 134 byte-identical; 162/164/170 refused in both engines',flush=True)
    masks=[(m,0) for m in s.masks]
    cmd=[placements(0,0,pose)]
    def rule(rows):
        check(rows[65]['present']==3,'could not join while the primary had committed a puzzle')
        check(rows[65]['players'][0]['interaction']==30 and rows[65]['inventory']['counts'][8]==0,
              'joining the busy primary changed its committed use')
    paired(env,'join-with-busy-primary',masks,cmd+[('join',65)],rule,
           (masks,cmd,setup),'could not join while the primary had committed a puzzle',setup)


def multi_link(env):
    # The selector's own sector is not necessarily the room that owns the item.
    if env.name=='level2':
        pose,idx,targets,other,cut,due=(75,20572,-768,30208,16384),60,(61,62),(),2,47
    elif env.name=='level7a':
        pose,idx,targets,other,cut,due=(4,45148,-6656,71168,16384),5,(6,9,10,108,109),(),2,47
    else:
        pose,idx,targets,other,cut,due=(49,42496,0,48970,-32768),75,(73,74),(4,),65,189
    setup=()
    masks=[(0,d.ACTION)]*9+[IDLE]*115
    if idx==75:
        _,setup,step=next(v for v in items.variants(129,130) if v[0]=='action-open')
        s=d.Script(env,pose,setup=setup)
        step(s)
        masks=[(0,m) for m in s.masks]+[IDLE]*60
    cmd=JOIN+[placements(0,1,pose)]
    normal,_=run_pair(env,masks,cmd,setup)
    def state(row):
        return [(a['index'],a['flags'],a['timer'],a['state']) for a in row['active'] if a['index'] in targets]
    def rule(rows):
        check(all(active(rows,i)==due for i in targets),'the authored sector link did not activate its targets')
        check(all(active(rows,i)==active(normal,i) for i in other) and
              state(rows[due])==state(normal[due]),'activation resolved to a different authored sector branch')
        check(rows[cut]['retiredActions'][0][:2]==[idx,lara(normal[cut],1)['index']] and
              lara(rows[due],1)['pos']==lara(rows[cut-1],1)['pos'],
              'the real owner or its death sector changed')
        if idx==5:
            check(rows[due]['players'][1]['camera']['lastIndex']==0 and
                  rows[due]['players'][0]['camera']['lastIndex']==-1,
                  'authored camera was not applied to the dead Lara')
    paired(env,f'{env.name}/dead-owner-multi-link-{idx}',masks,cmd+[('health',cut,1,0)],rule,
           ([IDLE]*len(masks),cmd+[('health',1,1,0)],setup),'the authored sector link did not activate its targets',setup)


def paused_switch(env):
    case=drawbridge.lower_case(env,22)
    starts=JOIN+[placements(0,1,case['start'])]
    full=[(0,d.ACTION)]+[IDLE]*8+[(d.SELECT,0)]+[IDLE]*44+[(d.SELECT,0)]+[IDLE]*118
    cmd=starts+[('health',2,1,0),('save-blocked',3,1),('save-blocked',52,1)]
    rows,_=run_pair(env,full,cmd)
    owed=rows[9]['retiredActions']
    check(owed and all(row['retiredActions']==owed for row in rows[10:54]),
          'the open ring advanced the corpse settlement clock')
    due=active(rows,22)
    check(due>55 and rows[due]['save']['time']==47,'switch did not settle at the same simulation tick after unpause')
    control,_=run_pair(env,full[:54]+[IDLE]*(len(full)-54),cmd)
    check(not active(control,22) and control[-1]['retiredActions']==owed,'paused negative control settled anyway')
    print('PASS owed switch pauses in ring and resumes at simulation tick 47',flush=True)

    # The other Lara leaving cannot orphan a primary corpse's obligation.
    other=JOIN+[placements(0,0,case['start']),('health',2,0,0),('leave',2),('save-blocked',3,1)]
    rows,_=run_pair(env,[(d.ACTION,0)]+[IDLE]*95,other)
    check(rows[2]['present']==1 and active(rows,22)==47 and not rows[47]['retiredActions'],
          'leaving the primary alone orphaned its committed switch')
    print('PASS primary corpse settles after the uncommitted partner leaves',flush=True)

    both=starts+[('health',2,1,0),('health',2,0,0)]
    rows,_=run_pair(env,[(0,d.ACTION)]+[(d.SELECT,0)]+[IDLE]*400,both)
    check(active(rows,22)==47 and not any(row['deathReady'] for row in rows[:48]) and rows[-1]['deathReady'],
          'death page opened before owed action or never opened afterwards')
    print('PASS the death page waits for the owed switch to settle',flush=True)

CASES={'dry':('level8b',dry_switch),'wet':('level4',water_switch),'key':('level4',hole),
       'puzzle':('level6',hole),'pickup':('level1',pickup),'midas':('level6',midas),
       'holder':('level10b',scion),'scion':('level8c',scion),'block':('level2',block),'flip':('level8c',flipped_warp),
       'pit':('level2',pit),'shaft':('level7b',shaft_join),'load':('gym',loaded_pose),
       'solo-puzzle-saves':('level6',solo_puzzle_saves),'multi-2':('level2',multi_link),
       'multi-7a':('level7a',multi_link),'multi-8a':('level8a',multi_link),'pause':('level8b',paused_switch),
       'other-actor':('level8b',other_actor_free)}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable',required=True,type=Path)
    p.add_argument('--oracle',required=True,type=Path)
    p.add_argument('--case',choices=CASES,action='append')
    a=p.parse_args()
    for case in a.case or CASES:
        level,run=CASES[case]
        run(d.Env(level,a.executable,a.oracle))


if __name__=='__main__': main()
