#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Directed cases from the independent co-op review (PYTHONPATH=test/game:tools/cook)."""
from pathlib import Path
import argparse
import sys
sys.dont_write_bytecode=True
from coop import check, paired, run_pair, lara, placements, d
import items

JOIN=[('join',0)]
IDLE=[(0,0)]


def actor_hole(env):
    idx,itemtype,target=((30,110,29) if env.name=='level6' else (80,129,78))
    pose=items.hole_starts(env,env.level.data['items'][idx])[0][2]
    _,setup,procedure=next(v for v in items.variants(itemtype,itemtype+1) if v[0]=='action-open')
    script=d.Script(env,pose,setup=setup)
    procedure(script)
    stock=items.PICKUP_SLOT[itemtype]
    masks=[(m,m) for m in script.masks]+IDLE*45
    both=[placements(0,0,pose),*JOIN]
    def race(rows):
        check(rows[4]['ringOwner']==0 and rows[4]['players'][0]['interaction']==idx and
              rows[4]['players'][1]['interaction']==-1,'first hole reservation did not win')
        spend=next(row['tick'] for row in rows if row['inventory']['counts'][stock]==0)
        fired=next((row['tick'] for row in rows[spend:] if any(
            i['index']==target and i['flags']&0x3e00==0x3e00 for i in row['active'])),None)
        if idx!=30:
            check(fired is not None,'used key never fired the authored moving bar')
            check(lara(rows[fired],0)['weaponState']==0,'partner fired the keyhole before the owner finished')
        check(rows[-1]['players'][0]['interaction']==rows[-1]['players'][1]['interaction']==-1,
              'a finished interaction still owns the hole')
    paired(env,'puzzle-contention' if idx==30 else 'key-contention',masks,both,race,
           ([(0,m) for m in script.masks],both,setup),'first hole reservation did not win',setup)

def water(env):
    pool=(14,40448,4608,60928,0)   # Same placed underwater start as the immutable GYM fixture.
    masks=IDLE*46
    cmd=JOIN+[placements(0,0,pool)]
    def together(rows,tick):
        return all(lara(rows[tick],slot)['water']==3 for slot in (0,1)) and lara(rows[tick],0)['pos']==lara(rows[tick],1)['pos']
    def warp_rule(rows):
        check(together(rows,30),'warp did not adopt the underwater partner pose')
        check(lara(rows[30],1)['oxygen']==1799 and rows[30]['save']['distance']==rows[29]['save']['distance'],
              'warp added motion or failed to restore air')
    paired(env,'warp-underwater',masks,cmd+[('warp',30,1)],warp_rule,
           (masks,cmd,()),'warp did not adopt the underwater partner pose')
    paired(env,'revive-underwater',masks,cmd+[('health',20,1,0),('warp',30,1)],warp_rule,
           (masks,cmd+[('health',20,1,0),('warp',30,1)],()),'warp did not adopt the underwater partner pose',(('give',94,1),))
    def join_rule(rows):
        check(together(rows,0) and lara(rows[0],1)['oxygen']==1800,'join did not copy the underwater medium and pose')
    paired(env,'join-underwater',masks,[placements(0,0,pool),*JOIN],join_rule,
           (masks,cmd,()),'join did not copy the underwater medium and pose')


def airborne(env):
    masks=IDLE+[(d.JUMP,0)]*3+IDLE*35
    cmd=JOIN+[placements(0,1,(17,16896,6656,60768,0))]
    def rule(rows):
        dest=lara(rows[0],0)['pos']
        check(lara(rows[17],0)['flags']&2 and lara(rows[17],0)['pos']!=dest,'warp window did not have an airborne partner')
        check(lara(rows[18],1)['pos']==dest and not lara(rows[18],1)['flags']&2,'unsafe partner pose was not replaced by stable ground')
    paired(env,'warp-airborne-partner',masks,cmd+[('warp',18,1)],rule,
           (masks,cmd,()),'unsafe partner pose was not replaced by stable ground')


def one_shot(env):
    tile=(88,40448,-3072,93696,0)  # LEVEL3A authored camera 0, the once bit in its action word.
    masks=IDLE*18
    second=placements(8,1,tile)
    def rule(rows):
        check(rows[8]['players'][1]['camera']['lastIndex']!=0,'second player reused the shared one-shot camera')
        check(rows[1]['players'][0]['camera']['lastIndex']==0,'primary never armed the one-shot')
    paired(env,'shared-one-shot-camera',masks,JOIN+[placements(1,0,tile),second],rule,
           (masks,JOIN+[second],()),'second player reused the shared one-shot camera')


def look_at(env):
    tile=(4,77312,3072,32256,0) # LEVEL1 look-at target 4.
    masks=IDLE*18
    def rule(rows):
        check(rows[1]['players'][1]['camera']['lastItem']==4,'secondary look-at never selected its target')
        check(rows[1]['players'][0]['camera']['lastItem']==-1,'secondary look-at stole primary camera')
    paired(env,'secondary-look-at',masks,JOIN+[placements(1,1,tile)],rule,
           (masks,JOIN,()),'secondary look-at never selected its target')


def heavy(env):
    masks=IDLE*7
    cmd=JOIN+[('heavy',1,0,31232,256,36352)]  # LEVEL4 room 0, authored heavy camera 1.
    def rule(rows):
        check(rows[1]['players'][0]['camera']['lastIndex']==1,'heavy camera did not target the primary camera')
        check(all(row['players'][1]['camera']['lastIndex']==-1 for row in rows),'heavy camera reached the secondary camera')
    paired(env,'heavy-camera-owner',masks,cmd,rule,(masks,JOIN,()),'heavy camera did not target the primary camera')


def ammo(env):
    stock=(('give',85,1),)
    script=d.Script(env,None,setup=stock)
    d.open_ring(script)
    check(d.ring_pick(script,d.SLOT['shotgun'],page_keys=False),'could not select shotgun from the shared ring')
    use=script.masks
    masks=[(m,0) for m in use]+[(0,m) for m in use]+[(d.WEAPON,d.WEAPON)]+IDLE*60
    first,_=run_pair(env,masks+[(d.ACTION,0)]*20,JOIN,stock)
    shell=6  # Fixed shotgun consumes six pellets per shell.
    ammo_slot=d.SLOT['shotgun']+1
    spent=next((row['tick'] for row in first[len(masks):] if row['inventory']['counts'][ammo_slot]==shell),None)
    check(spent is not None,'the arms did not fire the first of two shells')
    shot_anim=lara(first[spent],0)['armR']['anim']
    masks += [(d.ACTION,0)]*(spent-len(masks))+IDLE*55
    race_tick=len(masks)
    def rule(rows):
        ready=rows[race_tick]
        check(all(lara(ready,i)['weapon']==3 and lara(ready,i)['weaponState']==4 for i in (0,1)) and
              ready['inventory']['counts'][ammo_slot]==shell,'last-round setup did not arm both Laras')
        last=next(row for row in rows[race_tick+1:] if row['inventory']['counts'][ammo_slot]==0)
        check(lara(last,0)['armR']['anim']==shot_anim and lara(last,1)['armR']['anim']!=shot_anim and
              last['save']['ammoUsed']-ready['save']['ammoUsed']==shell,'last shell was not spent exactly once by slot 0')
    paired(env,'last-shared-shotgun-shell',masks+[(d.ACTION,d.ACTION)]*30,JOIN,rule,
           (masks+[(0,d.ACTION)]*30,JOIN,stock),'last shell was not spent exactly once by slot 0',stock)


def lightning(env):
    p=(50,53248,-19840,11776,16384); q=(50,53760,-19840,11776,0)
    masks=IDLE*4; setup=(('activate',107,0),)
    def rule(rows):
        check([lara(rows[1],i)['health'] for i in (0,1)]==[1000,600],
              'one bolt did not select only the closest exposed Lara')
    cmd=JOIN+[placements(0,0,p),placements(0,1,q)]
    paired(env,'lightning-one-nearest-victim',masks,cmd,rule,
           (masks,JOIN+[placements(0,0,q),placements(0,1,p)],setup),
           'one bolt did not select only the closest exposed Lara',setup)


CASES={'puzzle':('level6',actor_hole),'key':('level4',actor_hole),'water':('gym',water),
       'air':('level1',airborne),'once':('level3a',one_shot),'look':('level1',look_at),
       'heavy':('level4',heavy),'ammo':('level1',ammo),'lightning':('level10c',lightning)}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable',required=True,type=Path)
    p.add_argument('--oracle',required=True,type=Path)
    p.add_argument('--case',choices=CASES,action='append')
    args=p.parse_args()
    for case in args.case or CASES:
        level,run=CASES[case]
        run(d.Env(level,args.executable,args.oracle))


if __name__=='__main__':
    main()
