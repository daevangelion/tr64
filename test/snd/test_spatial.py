# SPDX-License-Identifier: MIT
"""Independent float expression for OpenLara's listener rule, not game output fits."""
import argparse
import math
import random
import subprocess


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--harness',required=True)
    a=p.parse_args()
    points=[(0,0,0),(1,1,1),(0,0,1024),(1024,0,0),(-1024,0,0),
            (0,0,-1024),(8191,0,0),(8192,0,0),(8191,8191,8191),
            (2147483647,-2147483648,2147483647)]
    cases=[(point,eye,turn,attenuate,gain) for point in points
           for eye in [(0,0,0),(-2147483648,2147483647,-2147483648)]
           for turn in range(4) for attenuate in (0,1) for gain in (0,64,4096)]
    rng=random.Random(0x534e3634)
    cases += [(tuple(rng.randrange(-20000,20001) for _ in range(3)),(0,0,0),rng.randrange(4),rng.randrange(2),rng.randrange(4097)) for _ in range(500)]
    text=''.join(' '.join(map(str,(*pos,*eye,turn,attenuate,gain)))+'\n' for pos,eye,turn,attenuate,gain in cases)
    run=subprocess.run(['rtk',a.harness],input=text,capture_output=True,text=True,check=True)
    rows=[tuple(map(int,line.split())) for line in run.stdout.splitlines()]
    assert len(rows)==len(cases)
    axes=[(1,0),(0,1),(-1,0),(0,-1)]
    maximum=0
    for actual,(pos,eye,turn,attenuate,gain) in zip(rows,cases):
        d=[x-y for x,y in zip(pos,eye)];length=math.sqrt(sum(x*x for x in d))
        n=[x/length if length else 0 for x in d];c,s=axes[turn]
        pan=c*n[0]-s*n[2];forward=s*n[0]+c*n[2]
        facing=.85+.15*forward
        distance=max(0,1-length/8192) if attenuate else 1
        expected=[gain*(min(1,1-pan)*.7+.3)*facing*distance,
                  gain*(min(1,1+pan)*.7+.3)*facing*distance]
        error=max(abs(x-y) for x,y in zip(actual,expected));maximum=max(maximum,error)
        assert error<=2, (actual,expected,pos,eye,turn,attenuate,gain)
    print(f'spatial PASS: {len(cases)} independent rules/extreme positions; max Q12 error={maximum:.4f}')


if __name__=='__main__':
    main()
