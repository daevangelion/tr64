# SPDX-License-Identifier: MIT
"""Myers shortest insert/delete edit script; no frequency/autojunk heuristic."""


def key(value):
    if isinstance(value,dict):
        return tuple((k,key(v)) for k,v in sorted(value.items()))
    if isinstance(value,(list,tuple)):
        return tuple(map(key,value))
    return value


def opcodes(a,b):
    n,m = len(a),len(b)
    prefix = 0
    while prefix<min(n,m) and a[prefix]==b[prefix]:
        prefix += 1
    suffix = 0
    while suffix<min(n,m)-prefix and a[n-suffix-1]==b[m-suffix-1]:
        suffix += 1
    aa = [key(x) for x in a[prefix:n-suffix if suffix else n]]
    bb = [key(x) for x in b[prefix:m-suffix if suffix else m]]
    v,trace = {1:0},[]
    done = False
    for d in range(len(aa)+len(bb)+1):
        trace.append(v.copy())
        for k in range(-d,d+1,2):
            if k==-d or (k!=d and v.get(k-1,-1)<v.get(k+1,-1)):
                x = v.get(k+1,0)
            else:
                x = v.get(k-1,0)+1
            y = x-k
            while x<len(aa) and y<len(bb) and aa[x]==bb[y]:
                x,y = x+1,y+1
            v[k] = x
            if x>=len(aa) and y>=len(bb):
                done = True
                break
        if done:
            break
    x,y = len(aa),len(bb)
    matches = []
    for d in range(len(trace)-1,-1,-1):
        old = trace[d]
        k = x-y
        previous = k+1 if k==-d or (k!=d and old.get(k-1,-1)<old.get(k+1,-1)) else k-1
        px = old.get(previous,0)
        py = px-previous
        while x>px and y>py:
            matches.append((x-1+prefix,y-1+prefix))
            x,y = x-1,y-1
        x,y = px,py
    matches = [(i,i) for i in range(prefix)]+list(reversed(matches))+[
        (n-suffix+i,m-suffix+i) for i in range(suffix)]
    result = []
    ai,bi = 0,0
    for ax,bx in matches+[(n,m)]:
        if ax>ai or bx>bi:
            result.append(('replace' if ax>ai and bx>bi else 'delete' if ax>ai else 'insert',ai,ax,bi,bx))
        if ax<n:
            if result and result[-1][0]=='equal' and result[-1][2]==ax and result[-1][4]==bx:
                tag,i,_,j,_ = result.pop()
                result.append((tag,i,ax+1,j,bx+1))
            else:
                result.append(('equal',ax,ax+1,bx,bx+1))
        ai,bi = ax+1,bx+1
    return result


def index_map(a,b):
    result = {}
    for tag,i,j,k,l in opcodes(a,b):
        if tag=='equal':
            result.update((i+offset,k+offset) for offset in range(j-i))
    return result
