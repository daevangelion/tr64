#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise a blocked save through the real passport, then save and load normally."""
import argparse
from pathlib import Path
import sys
sys.dont_write_bytecode=True
from coop import check, run_pair, lara, placements, d
from coop_settle import active, award, IDLE, JOIN
import drawbridge
import items

MENU=[(d.SELECT,0)]+[IDLE]*20+[(d.BACK,0)]+[IDLE]*32+[(d.ACTION,0)]+[IDLE]*40
CONFIRM=[(d.ACTION,0)]
BLOCKED=[(0,0),(5,170),(0,0),(1,19)]


def options(row):
    inv=row['inventory']
    return [tuple(entry[:2]) for entry in inv['options'][:inv['optionsCount']]]


def passport(env, name, pose, seq, committed, outcome, setup=(), kill=False):
    # Match the solo room so unrelated triggers near the authored spawn do
    # not influence the world fingerprint.
    commands=JOIN+[placements(0,0,pose),placements(0,1,pose)]
    if kill:
        commands += [('health',committed+1,1,0)]
    prefix=[(0,m) for m in seq[:committed]]+MENU
    confirm=prefix+CONFIRM
    waiting,old=run_pair(env,prefix+[IDLE],commands,setup,save=(0,None))
    refused,unchanged=run_pair(env,confirm,commands,setup,save=(0,None))
    row=refused[-1]
    check(options(row)==BLOCKED and row['inventory']['optionIndex']==3,
          f'{name}: passport did not show the distinct refusal and OK button')
    check(row['players'][1]['interaction']>=0 and bool(row['retiredActions'])==kill,
          f'{name}: actor had no committed work at the blocked request')
    check(options(waiting[-1])!=BLOCKED and old==unchanged and row['save']==waiting[-1]['save'] and
          row['retiredActions']==waiting[-1]['retiredActions'],
          f'{name}: refusing a save changed its previous bytes or pending action')

    # Dismiss, wait out the action on world ticks, then save via the passport.
    resume=confirm+[IDLE]*3+CONFIRM+[IDLE]*240+MENU
    finish=len(resume)+1
    load=finish+48
    steps=resume+CONFIRM+[IDLE]*50
    success,updated=run_pair(env,steps,commands,setup,save=(0,load))
    check(success[load-1]['inventory']['state']==0 and not success[load-1]['retiredActions'] and
          all(a!=b for a,b in zip(unchanged,updated)) and success[load]['present']==1,
          f'{name}: the post-settlement passport did not save and load')
    solo,_=run_pair(env,[(m,0) for m in seq[:committed]]+[IDLE]*(600-committed),
                    [placements(0,0,pose)],setup)
    target_time=success[load]['save']['time']
    reference=next(r for r in solo if r['save']['time']==target_time and r['inventory']['state']==0)
    check(outcome(success[load])==outcome(reference),f'{name}: load did not preserve the solo world outcome')
    check(success[load]['world']==success[finish]['world'],f'{name}: loading changed the committed world')

    # Same real save controls with slot 1 idle: no refusal, closes without OK.
    idle,_=run_pair(env,[IDLE]*committed+MENU+CONFIRM+[IDLE]*48,
                    JOIN,setup,save=(0,None))
    check(all(options(r)!=BLOCKED for r in idle) and idle[-1]['inventory']['state']==0,
          f'{name}: the idle partner control was blocked')
    print(f'PASS passport {name}: blocked with original bytes; save/load at world tick {target_time} matches solo; idle control accepted',flush=True)


def controller(row, index):
    it=next((a for a in row['active'] if a['index']==index),None)
    return row['world'],None if it is None else (it['type'],it['state'],it['pos'],it['flags'],it['timer'])


def bridge(env, kill):
    case=drawbridge.lower_case(env,22)
    def observe(row):
        world,value=controller(row,22)
        check(value is not None and value[1]==1,'bridge checkpoint did not contain the lowered bridge')
        return world,value
    passport(env,'dead switch' if kill else 'living switch',case['start'],case['masks'],1,observe,kill=kill)


def key(env):
    pose=items.hole_starts(env,env.level.data['items'][80])[0][2]
    _,setup,step=next(v for v in items.variants(129,130) if v[0]=='action-open')
    s=d.Script(env,pose,setup=setup); step(s)
    rows,_=run_pair(env,[(0,m) for m in s.masks],JOIN+[placements(0,1,pose)],setup)
    committed=next(row['tick'] for row in rows if lara(row,1)['state']==42)
    def observe(row):
        world,value=controller(row,78)
        check(row['inventory']['counts'][d.SLOT['key1']]==0 and value is not None,'key was not committed on load')
        return world,value
    passport(env,'living key',pose,s.masks,committed,observe,setup)


def pickup(env):
    item=env.level.data['items'][29]
    pose=(item['room'],item['x'],item['y'],item['z']-160,0)
    seq=[d.ACTION]*8+[0]*100
    rows,_=run_pair(env,[(0,m) for m in seq],JOIN+[placements(0,1,pose)])
    committed=award(rows,93)[0]
    def observe(row):
        value=(row['inventory']['counts'][d.SLOT['medi_small']],row['save']['pickups'])
        check(value==(1,1),'the loaded pickup was not awarded once')
        return row['world'],value
    passport(env,'living pickup',pose,seq,committed,observe)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable',required=True,type=Path)
    p.add_argument('--oracle',required=True,type=Path)
    p.add_argument('--case',choices=('dead','live','key','pickup'),action='append')
    a=p.parse_args()
    for kind in a.case or ('dead','live','key','pickup'):
        level={'dead':'level8b','live':'level8b','key':'level4','pickup':'level1'}[kind]
        env=d.Env(level,a.executable,a.oracle)
        if kind in ('dead','live'):
            bridge(env,kind=='dead')
        elif kind=='key':
            key(env)
        else:
            pickup(env)


if __name__=='__main__': main()
