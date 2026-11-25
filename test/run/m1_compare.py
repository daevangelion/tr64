# SPDX-License-Identifier: MIT
"""Two-stage M1 gate: reference/host requires maps; host/cart compares RGB.

Limits are calibrated per named case, not inferred from an engine picture.
The CLI binds a case to its calibrated reference RGB hash. Cross-edition
calibration explicitly excludes source-domain texture IDs, never host gates.
"""
import argparse
import hashlib
import gzip
import json
from pathlib import Path
import numpy as np
from PIL import Image

MAPS = ('coverage','room','texture','depth','texel')


def grow(mask, radius=1):
    padded = np.pad(mask,radius)
    out = np.zeros_like(mask)
    for y in range(2*radius+1):
        for x in range(2*radius+1):
            out |= padded[y:y+mask.shape[0],x:x+mask.shape[1]]
    return out


def shifted(array,dy,dx,fill):
    pad = [(1,1),(1,1)]+[(0,0)]*(array.ndim-2)
    return np.pad(array,pad,constant_values=fill)[1+dy:1+dy+240,1+dx:1+dx+320]


def validate_rgb(a,b):
    if a.shape != (240,320,3) or b.shape != a.shape:
        raise ValueError('pictures must both be RGB 320x240')


def validate_maps(maps):
    if maps is None or any(key not in maps for key in MAPS):
        raise ValueError('host gate requires coverage, room, texture, depth and texel maps')
    for key in MAPS:
        expected = (240,320,2) if key == 'texel' else (240,320)
        if maps[key].shape != expected:
            raise ValueError(f'{key} map must have shape {expected}')
    covered = maps['coverage'].astype(bool)
    if np.any(~np.isfinite(maps['depth'][covered])) or np.any(maps['depth'][covered]<64-1e-4):
        raise ValueError('covered depth must be finite camera Z >= near64')
    if np.any(~np.isfinite(maps['texel'][covered])):
        raise ValueError('covered texel coordinates must be finite')
    if np.any(maps['room'][covered]<0) or np.any(maps['texture'][covered]<-2) or np.any(maps['texture'][covered]==-1):
        raise ValueError('covered label outside the source index domains')


def id_error(a,b,ca,cb):
    unmatched = 0
    for source,target,cs,ct in [(a,b,ca,cb),(b,a,cb,ca)]:
        matched = np.zeros_like(ca)
        for dy in [-1,0,1]:
            for dx in [-1,0,1]:
                matched |= (source==shifted(target,dy,dx,-99999))&shifted(ct,dy,dx,False)
        interior = cs.copy()
        interior[[0,-1],:] = False
        interior[:,[0,-1]] = False
        unmatched += int((interior&~matched).sum())
    return unmatched/max(1,int(ca.sum()+cb.sum()))


def structure_error(a,b):
    # Local SSIM is diagnostic; texel correspondence handles projection offsets.
    def block(image):
        return image.reshape(30,8,40,8).mean(axis=(1,3))
    a,b = [im.astype(float)@np.array([.2126,.7152,.0722]) for im in [a,b]]
    ma,mb = block(a),block(b)
    va,vb = np.maximum(0,block(a*a)-ma*ma),np.maximum(0,block(b*b)-mb*mb)
    cov = block(a*b)-ma*mb
    return float(1-np.mean((2*ma*mb+6.5025)*(2*cov+58.5225)/((ma*ma+mb*mb+6.5025)*(va+vb+58.5225))))


def texel_error(a,b,cross_source):
    ca,cb = a['coverage'].astype(bool),b['coverage'].astype(bool)
    uv = a['texel']
    clean = np.nan_to_num(uv)
    gx = np.linalg.norm(clean-np.roll(clean,1,axis=1),axis=2)
    gy = np.linalg.norm(clean-np.roll(clean,1,axis=0),axis=2)
    scale = np.maximum(1,gx+gy)
    best = np.full((240,320),np.inf)
    best_z = np.full((240,320),np.inf)
    for dy in [-1,0,1]:
        for dx in [-1,0,1]:
            valid = ca&shifted(cb,dy,dx,False)&(a['room']==shifted(b['room'],dy,dx,-99999))
            if not cross_source:
                valid &= a['texture']==shifted(b['texture'],dy,dx,-99999)
            error = np.linalg.norm(clean-shifted(np.nan_to_num(b['texel']),dy,dx,1e9),axis=2)/scale
            take = valid&(error<best)
            best[take] = error[take]
            best_z[take] = shifted(b['depth'],dy,dx,np.inf)[take]
    valid = ca&np.isfinite(best)
    return (float(np.minimum(best[ca],16).mean()) if ca.any() else 0.,
            float(np.minimum(1,np.abs(a['depth'][valid]-best_z[valid])/np.maximum(64,a['depth'][valid])).mean()) if valid.any() else 1.)


def texel_colours(rgb,maps,cross_source=False):
    covered = maps['coverage'].astype(bool)
    uv = np.floor(np.nan_to_num(maps['texel'][covered])+.5).astype(np.int64)
    # Per-source material + exact nearest texel, not spatial 8x8 means.
    keys = np.column_stack([maps['room'][covered],np.zeros(covered.sum(),int) if cross_source else maps['texture'][covered],uv])
    unique,inverse,counts = np.unique(keys,axis=0,return_inverse=True,return_counts=True)
    sums = np.column_stack([np.bincount(inverse,weights=rgb[covered,c],minlength=len(counts)) for c in range(3)])
    return unique,sums/counts[:,None],counts


def colour_errors(a,b,am,bm,cross_source=False):
    if cross_source:
        # Different CLUT/palette domains: same geometry, compare small RGB blocks.
        aa,bb = [im.astype(float).reshape(120,2,160,2,3).mean(axis=(1,3)) for im in [a,b]]
    else:
        ka,aa,na = texel_colours(a,am)
        kb,bb,nb = texel_colours(b,bm)
        dtype = np.dtype([(str(i),np.int64) for i in range(4)])
        _,ia,ib = np.intersect1d(np.ascontiguousarray(ka).view(dtype).ravel(),np.ascontiguousarray(kb).view(dtype).ravel(),return_indices=True)
        if not len(ia):
            return 1.,1.
        aa,bb = aa[ia],bb[ib]
    delta = np.abs(aa-bb)
    chroma_a = aa-aa.mean(axis=-1,keepdims=True)
    chroma_b = bb-bb.mean(axis=-1,keepdims=True)
    return float(delta.mean()/255),float(np.abs(chroma_a-chroma_b).mean()/255)


def detail_error(a,b,ca,cb):
    def energy(rgb,mask):
        gray = rgb.astype(float)@np.array([.2126,.7152,.0722])
        lap = 4*gray-np.roll(gray,1,0)-np.roll(gray,-1,0)-np.roll(gray,1,1)-np.roll(gray,-1,1)
        valid = mask&~grow(~mask)
        return float(np.abs(lap[valid]).mean()) if valid.any() else 0.
    ea,eb = energy(a,ca),energy(b,cb)
    return float(abs(np.log((eb+.1)/(ea+.1))))


def shading_energy(rgb,maps):
    """Variation of illumination on repeated copies of the *same* texel.

    Material colour cancels within each group; constant neutral lighting
    cannot reproduce the data's spatial shading even if its mean is similar.
    """
    covered = maps['coverage'].astype(bool)
    uv = np.floor(np.nan_to_num(maps['texel'][covered])+.5).astype(np.int64)
    keys = np.column_stack([maps['room'][covered],maps['texture'][covered],uv])
    _,inverse,counts = np.unique(keys,axis=0,return_inverse=True,return_counts=True)
    gray = rgb[covered].astype(float)@np.array([.2126,.7152,.0722])
    mean = np.bincount(inverse,weights=gray)/counts
    variance = np.maximum(0,np.bincount(inverse,weights=gray*gray)/counts-mean*mean)
    valid = counts>=4
    if not valid.any():
        return 0.
    return float(np.sqrt(np.average(variance[valid],weights=counts[valid]))/(np.average(mean[valid],weights=counts[valid])+.1))


def compare(reference,candidate,ref_maps=None,candidate_maps=None,*,cross_source=False):
    validate_rgb(reference,candidate)
    validate_maps(ref_maps)
    validate_maps(candidate_maps)
    ca,cb = ref_maps['coverage'].astype(bool),candidate_maps['coverage'].astype(bool)
    dont_care = grow(ca&(ref_maps['depth']>10240))
    reference,candidate = reference.copy(),candidate.copy()
    reference[dont_care],candidate[dont_care] = 0,0
    ref_maps,candidate_maps = dict(ref_maps),dict(candidate_maps)
    ca,cb = ca&~dont_care,cb&~dont_care
    cb &= ~(ca&(candidate_maps['depth']>10240))
    ref_maps['coverage'],candidate_maps['coverage'] = ca,cb
    if 'visible_rooms' not in ref_maps or 'visible_rooms' not in candidate_maps:
        raise ValueError('host gate requires visible_rooms logical-index metadata')
    colour,chroma = colour_errors(reference,candidate,ref_maps,candidate_maps,cross_source)
    texel,depth = texel_error(ref_maps,candidate_maps,cross_source)
    distance_brightness = 0.
    for low in range(0,10240,1024):
        ma = ca&(ref_maps['depth']>=low)&(ref_maps['depth']<low+1024)
        mb = cb&(candidate_maps['depth']>=low)&(candidate_maps['depth']<low+1024)
        if ma.sum()>=16 and mb.sum()>=16:
            distance_brightness = max(distance_brightness,abs(float((reference[ma]@np.array([.2126,.7152,.0722])).mean()-(candidate[mb]@np.array([.2126,.7152,.0722])).mean()))/255)
    result = {'coverage':float(abs(int(ca.sum())-int(cb.sum()))/ca.size),
              'silhouette':float(((ca&~grow(cb))|(cb&~grow(ca))).sum()/max(1,(ca|cb).sum())),
              'room':id_error(ref_maps['room'],candidate_maps['room'],ca,cb),
              'texel':texel,'depth':depth,'colour':colour,'chroma':chroma,
              'distance_brightness':distance_brightness,
              'brightness':float(abs((reference[ca].astype(float)@np.array([.2126,.7152,.0722])).mean()-(candidate[cb].astype(float)@np.array([.2126,.7152,.0722])).mean())/255) if ca.any() and cb.any() else 1.,
              'detail':detail_error(reference,candidate,ca,cb),
              'shading_loss':float(max(0,np.log((shading_energy(reference,ref_maps)+.001)/(shading_energy(candidate,candidate_maps)+.001)))),
              'far':len(set(ref_maps['visible_rooms'])^set(candidate_maps['visible_rooms']))/max(1,len(set(ref_maps['visible_rooms'])|set(candidate_maps['visible_rooms']))),
              'structure':structure_error(reference,candidate)}
    if not cross_source:
        result['texture'] = id_error(ref_maps['texture'],candidate_maps['texture'],ca,cb)
    return result


def compare_cart(host,cart):
    validate_rgb(host,cart)
    delta = np.abs(host.astype(float)-cart.astype(float))
    return {'pixel_mae':float(delta.mean()/255),
            'changed_pixels':float(np.any(delta!=0,axis=2).mean())}


def verdict(metrics,thresholds):
    missing = set(thresholds)-set(metrics)
    if missing:
        raise ValueError('missing required metrics: '+','.join(sorted(missing)))
    decisions = {key:bool(np.isfinite(metrics[key]) and metrics[key]<=limit) for key,limit in thresholds.items()}
    return {'pass':bool(decisions) and all(decisions.values()),'metrics':decisions}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('reference',type=Path)
    p.add_argument('candidate',type=Path)
    p.add_argument('--reference-maps',type=Path)
    p.add_argument('--candidate-maps',type=Path)
    p.add_argument('--thresholds',required=True,type=Path)
    p.add_argument('--case',required=True,help='source/scene e.g. pc/m1-level1-start')
    p.add_argument('--stage',choices=['host','cart'],default='host')
    args = p.parse_args()
    a,b = [np.asarray(Image.open(path).convert('RGB')) for path in [args.reference,args.candidate]]
    raw = args.thresholds.read_bytes()
    calibration = json.loads(gzip.decompress(raw) if args.thresholds.suffix=='.gz' else raw)
    if args.stage=='cart':
        provenance = calibration.get('cart_calibration',{})
        if provenance.get('kind')!='measured_host_cart' or not provenance.get('pairs'):
            raise ValueError('cart gate unavailable: requires calibration from real host-engine/cart picture pairs')
    limits = calibration['cases'][args.case]
    if args.stage=='host' and hashlib.sha256(a.tobytes()).hexdigest()!=limits['reference_rgb_sha256']:
        raise ValueError('reference RGB does not match calibrated case')
    if args.stage=='host':
        maps = [np.load(path) if path else None for path in [args.reference_maps,args.candidate_maps]]
        metrics = compare(a,b,*maps)
    else:
        metrics = compare_cart(a,b)
    decision = verdict(metrics,limits[args.stage])
    print(json.dumps({'errors':metrics,**decision},indent=2))
    raise SystemExit(0 if decision['pass'] else 1)


if __name__=='__main__':
    main()
