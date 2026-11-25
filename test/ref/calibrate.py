# SPDX-License-Identifier: MIT
"""Calibrate each pose on positive controls, then try to falsify its gate.

No negative or engine error enters limit selection. Every non-inert negative
must be rejected in each pose, not merely on its most favorable scene.
"""
import argparse
from collections import Counter
import hashlib
import gzip
import json
import platform
from pathlib import Path
import numpy as np
import PIL
from PIL import Image,ImageDraw,ImageFilter
from render import render,load_level,Picture
from m1_compare import compare,compare_cart,verdict,MAPS

FAULTS = {'corners':'texel','missing_room':'room','lighting':'shading_loss','fov':'texel',
          'textures':'texture','far_unculled':'far','far_culled':'coverage','no_fog':'distance_brightness','overbright_missing':'brightness'}
PHOTO_FAULTS = {'greyscale':'chroma','twice_bright':'colour','blur3':'detail',
                'block8':'detail','flat_mean':'colour'}
GATE = ('coverage','silhouette','room','texture','texel','depth','colour','chroma','brightness','shading_loss','detail','far','distance_brightness')
FLOORS = {key:1/76800 for key in ('coverage','silhouette','room','texture','far')}
FLOORS.update(brightness=1/255,colour=1/255,chroma=1/255,texel=1e-4,depth=1e-5,shading_loss=.01,detail=.01,distance_brightness=1/255)


def maps(picture,canonical=False):
    result = {key:getattr(picture,key) for key in MAPS}
    result['visible_rooms'] = np.asarray(picture.counts['visible_rooms'],dtype=int)
    if canonical:
        result['texel'] = picture.texel-picture.texel_origin
    return result


def changed_picture(picture,rgb):
    return Picture(rgb,**{key:getattr(picture,key) for key in MAPS},counts=picture.counts)


def photo_fault(picture,fault):
    rgb = picture.rgb
    if fault=='greyscale':
        gray = np.rint(rgb.astype(float)@np.array([.2126,.7152,.0722])).astype(np.uint8)
        rgb = np.repeat(gray[:,:,None],3,axis=2)
    elif fault=='twice_bright':
        rgb = np.minimum(rgb.astype(int)*2,255).astype(np.uint8)
    elif fault=='blur3':
        rgb = np.asarray(Image.fromarray(rgb).filter(ImageFilter.GaussianBlur(3)))
    elif fault=='block8':
        means = rgb.reshape(30,8,40,8,3).mean(axis=(1,3)).astype(np.uint8)
        rgb = means.repeat(8,axis=0).repeat(8,axis=1)
    elif fault=='flat_mean':
        rgb = np.zeros_like(rgb)
        rgb[picture.coverage] = np.rint(picture.rgb[picture.coverage].mean(axis=0)).astype(np.uint8)
    return changed_picture(picture,rgb)


def colour_rounding(picture,mode):
    rgb = picture.rgb.astype(int)
    if mode=='round5':
        rgb = np.minimum(31,(rgb+4)//8)*8
    elif mode=='floor5':
        rgb = (rgb//8)*8
    else:
        dither = np.array([[-4,0,-3,1],[2,-2,3,-1],[-3,1,-4,0],[3,-1,2,-2]])
        rgb = (np.clip(rgb+np.tile(dither,(60,80))[:,:,None],0,255)//8)*8
    return changed_picture(picture,rgb.astype(np.uint8))


def difference(a,b):
    care = ~((a.coverage&(a.depth>10240))|(b.coverage&(b.depth>10240)))
    rgb = int((np.any(a.rgb!=b.rgb,axis=2)&care).sum())
    common = a.coverage&b.coverage&care
    geom = a.coverage!=b.coverage
    geom |= common&((a.room!=b.room)|(a.texture!=b.texture))
    geom |= common&(np.abs(np.nan_to_num(a.texel)-np.nan_to_num(b.texel)).max(axis=2)>1e-6)
    geom |= common&(np.abs(np.nan_to_num(a.depth)-np.nan_to_num(b.depth))>1e-5)
    visible_change = len(set(a.counts['visible_rooms'])^set(b.counts['visible_rooms']))
    return rgb,int((geom&care).sum())+visible_change


def lighting_evidence(levels):
    result = {}
    for name in sorted({name for source,name in levels}):
        pc,psx = levels['pc',name],levels['psx',name]
        palette = np.frombuffer(pc.palette,np.uint8).reshape(256,3).astype(float)*4
        lut = np.frombuffer(pc.light_map,np.uint8).reshape(32,256)
        luminance = palette@np.array([.2126,.7152,.0722])
        rows = []
        for row in lut:
            mapped = palette[row]
            rows.append({'luminance_sum_ratio':float((mapped@np.array([.2126,.7152,.0722])).sum()/luminance.sum()),
                         'mean_luminance_ratio':float(np.mean((mapped@np.array([.2126,.7152,.0722]))[luminance>0]/luminance[luminance>0])),
                         'identity_entries':int(np.all(mapped==palette,axis=1).sum()),
                         'saturated_entries':int(np.any(mapped>=252,axis=1).sum())})
        residuals = Counter(a['lighting']['value']-(8191-(b['lighting']['value']<<5))
                            for ra,rb in zip(pc.rooms,psx.rooms) for a,b in zip(ra.vertices,rb.vertices)
                            if all(a[k]==b[k] for k in 'xyz'))
        result[name] = {'pc_lightmap_rows':rows,'matched_coordinates':sum(residuals.values()),
                        'formula':'pc_raw - (8191 - (psx_raw << 5))','residual_histogram':dict(sorted(residuals.items()))}
    return result


def cooked_textures(directory,level):
    from n64_tex_view import Section
    matches = []
    for path in (directory/level.source/level.name).glob('*/manifest.json'):
        manifest = json.loads(path.read_text())
        if manifest['source_sha256']==level.sha256:
            matches.append((path,manifest))
    if len(matches)!=1:
        raise ValueError(f'expected one cached native texture section for {level.source}/{level.name}')
    path,manifest = matches[0]
    blob = path.with_name('section.bin').read_bytes()
    if hashlib.sha256(blob).hexdigest()!=manifest['section_sha256']:
        raise ValueError('cached texture section hash mismatch')
    view = Section(blob)
    return {int(source):(np.asarray(view.image(index)),np.asarray(manifest['descriptors'][index]['rect'][:2]))
            for source,index in manifest['object_descriptor_map'].items()},manifest['section_sha256']


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--data',required=True)
    p.add_argument('--output',required=True,type=Path)
    p.add_argument('--textures',type=Path,default=Path('/tmp/tr64/evidence/m1-tex2/sections'))
    args = p.parse_args()
    root = Path(__file__).resolve().parents[2]
    paths = sorted((root/'test/scenes').glob('m1-*.scene.json'))
    scenes = [(path.stem.replace('.scene',''),json.loads(path.read_text())) for path in paths]
    out = args.output
    out.mkdir(parents=True,exist_ok=True)
    levels = {(source,name):load_level(args.data,source,name) for source in ['pc','psx'] for name in sorted({s['level'] for _,s in scenes})}
    native = {key:cooked_textures(args.textures,level) for key,level in levels.items()}
    reference = {}
    for source in ['pc','psx']:
        for name,scene in scenes:
            picture = render(levels[source,scene['level']],scene['camera'])
            reference[source,name] = picture
            picture.save(out/source/name)
            counts = Counter(picture.room[picture.coverage].tolist())
            print(source,name,'rooms',picture.counts['visible_rooms'],'faces',picture.counts['faces_drawn'],
                  'pixels',picture.counts['covered_pixels'],'minZ',float(picture.depth[picture.coverage].min()),'room pixels',dict(counts),flush=True)
    pc_lut_difference = {}
    for name,scene in scenes:
        scalar = reference['pc',name]
        lut = render(levels['pc',scene['level']],scene['camera'],lighting_model='pc_lut')
        lut.save(out/'pc-lut'/name)
        delta = scalar.rgb.astype(float)-lut.rgb.astype(float)
        pc_lut_difference[name] = {'rgb_mae':float(np.abs(delta).mean()/255),
                                   'rgb_rmse':float(np.sqrt((delta*delta).mean())/255),
                                   'changed_pixels':int(np.any(delta!=0,axis=2).sum())}
    sheet = Image.new('RGB',(640,264*len(scenes)),(30,30,30))
    draw = ImageDraw.Draw(sheet)
    for row,(name,scene) in enumerate(scenes):
        for column,source in enumerate(['pc','psx']):
            sheet.paste(Image.fromarray(reference[source,name].rgb),(column*320,row*264+24))
            draw.text((column*320+4,row*264+5),source+' '+name,fill='white')
    sheet.save(out/'contact.png')
    records,cases = [],{}
    total_failures = 0
    for source in ['pc','psx']:
        for name,scene in scenes:
            baseline = reference[source,name]
            level = levels[source,scene['level']]
            positives = [('other_source',reference['psx' if source=='pc' else 'pc',name]),
                         ('bilinear',render(level,scene['camera'],bilinear=True)),
                         ('half_pixel',render(level,scene['camera'],fault='half_pixel')),
                         ('round_vertices',render(level,scene['camera'],fault='round_vertices')),
                         ('quarter_vertices',render(level,scene['camera'],fault='quarter_vertices')),
                         ('shade_floor',render(level,scene['camera'],fault='shade_floor')),
                         ('shade_round',render(level,scene['camera'],fault='shade_round')),
                         ('cooked_ci4',render(level,scene['camera'],cooked=native[source,scene['level']][0])),
                         ('framebuffer5',colour_rounding(baseline,'floor5')),
                         ('combined',colour_rounding(render(level,scene['camera'],fault='combined',cooked=native[source,scene['level']][0]),'floor5'))]
            measurements = [(control,pic,compare(baseline.rgb,pic.rgb,maps(baseline,control=='other_source'),maps(pic,control=='other_source'),cross_source=control=='other_source')) for control,pic in positives]
            # Limits depend ONLY on positives, with a declared 5% guard and
            # one-pixel image-area quantum. Negatives cannot raise any limit.
            limits = {key:max(FLOORS[key],max(errors.get(key,0) for _,_,errors in measurements)*1.05) for key in GATE}
            cart_positive = [(mode,colour_rounding(baseline,mode)) for mode in ['round5','floor5','dither5']]
            cart_metrics = [(mode,pic,compare_cart(baseline.rgb,pic.rgb)) for mode,pic in cart_positive]
            cases[source+'/'+name] = {'host':limits,
                                     'reference_rgb_sha256':hashlib.sha256(baseline.rgb.tobytes()).hexdigest()}
            populations = Counter(baseline.room[baseline.coverage].tolist())
            neighbors = [(count,index) for index,count in populations.items() if index!=scene['camera']['room']]
            if not neighbors:
                print('SCENE ERROR no non-camera occupied room',source,name,flush=True)
                total_failures += 1
            omit = max(neighbors)[1] if neighbors else -99999
            negatives = [(fault,render(level,{**scene['camera'],'omit_room':omit},fault=fault)) for fault in FAULTS]
            smallest = min(neighbors)[1] if neighbors else -99999
            negatives.append(('missing_smallest_room',render(level,{**scene['camera'],'omit_room':smallest},fault='missing_room')))
            negatives += [(fault,photo_fault(baseline,fault)) for fault in PHOTO_FAULTS]
            measurements += [(control,pic,compare(baseline.rgb,pic.rgb,maps(baseline),maps(pic))) for control,pic in negatives]
            for control,pic,errors in measurements:
                positive = control in {c for c,_ in positives}
                rgb_changed,map_changed = difference(baseline,pic)
                active = positive or rgb_changed>0 or map_changed>0
                assessed = {key:limit for key,limit in limits.items() if key in errors}
                decision = verdict(errors,assessed)
                accepted = decision['pass'] if positive else (not active or not decision['pass'])
                intended = (FAULTS|PHOTO_FAULTS).get(control)
                margins = {key:errors[key]-assessed[key] for key in assessed}
                record = {'case':source+'/'+name,'stage':'host','control':control,'positive':positive,
                          'rgb_changed_pixels':rgb_changed,'map_changed_pixels':map_changed,'active':active,
                          'intended_metric':intended,'intended_margin':None if intended is None else margins[intended],
                          'caught_by':[key for key,value in margins.items() if value>0],
                          'accepted_control':accepted,'errors':errors,'margins':margins}
                records.append(record)
                if not accepted:
                    total_failures += 1
                    print('CONTROL FAIL',record,flush=True)
                directory = out/'controls'/source
                directory.mkdir(parents=True,exist_ok=True)
                Image.fromarray(pic.rgb).save(directory/(name+'-'+control+'.png'))
            for control,pic,errors in cart_metrics+[('bilinear',positives[1][1],compare_cart(baseline.rgb,positives[1][1].rgb))]:
                positive = control!='bilinear'
                active = int(np.any(baseline.rgb!=pic.rgb,axis=2).sum())>0
                records.append({'case':source+'/'+name,'stage':'cart_diagnostic','control':control,'positive':positive,'active':active,
                                'accepted_control':None,'errors':errors,'margins':{}})
    metadata = {'policy':'per pose/source; host positive maximum *1.05 with declared precision floors; every active negative must fail; cart uncalibrated/refuses gate',
                'host_metric_floors':FLOORS,
                'native_texture_sha256':{source+'/'+name:value[1] for (source,name),value in native.items()},
                'activity':'RGB: at least one pixel with any changed RGB8 channel; map-only: coverage/labels change or covered depth>1e-5 / texel>1e-6',
                'cases':cases,'records':records,'failures':total_failures,'lighting':lighting_evidence(levels),
                'reference_lighting':'scalar gain, post-clamp vertex fog (Main D11)',
                'pc_lut_difference_informational_only':pc_lut_difference,
                'scene_sha256':{path.name:hashlib.sha256(path.read_bytes()).hexdigest() for path in paths},
                'inputs':{source+'/'+name:level.sha256 for (source,name),level in levels.items()},
                'runtime':{'python':platform.python_version(),'numpy':np.__version__,'Pillow':PIL.__version__},
                'tool_sha256':{str(path.relative_to(root)):hashlib.sha256(path.read_bytes()).hexdigest() for path in [root/'test/ref/render.py',Path(__file__),root/'test/run/m1_compare.py']}}
    (out/'calibration.json.gz').write_bytes(gzip.compress((json.dumps(metadata,indent=2)+'\n').encode(),mtime=0))
    lines = ['# Per-pose calibration (limits from positives only)', '',
             '| Case | Stage | Control | Positive | RGB changed | Map changed | Active | Accepted | Intended margin | Caught by |',
             '|---|---|---|---|---:|---:|---|---|---:|---|']
    for r in records:
        lines.append('| '+r['case']+' | '+r['stage']+' | '+r['control']+' | '+str(r['positive'])+' | '+str(r.get('rgb_changed_pixels',''))+' | '+str(r.get('map_changed_pixels',''))+' | '+str(r['active'])+' | '+str(r['accepted_control'])+' | '+str(r.get('intended_margin',''))+' | '+','.join(r.get('caught_by',[]))+' |')
    lines += ['','## PC lightmap row evidence','', '| Level | Darkness row | Luminance ratio | Identity entries | Saturated entries |','|---|---:|---:|---:|---:|']
    for name,evidence in metadata['lighting'].items():
        for row,values in enumerate(evidence['pc_lightmap_rows']):
            lines.append(f'| {name} | {row} | {values["luminance_sum_ratio"]:.8f} | {values["identity_entries"]} | {values["saturated_entries"]} |')
    lines += ['','## PC-LUT vs scalar reference (informational, not a gate)','',
              '| Pose | RGB MAE /255 | RGB RMSE /255 | Changed pixels |','|---|---:|---:|---:|']
    for name,values in pc_lut_difference.items():
        lines.append(f'| {name} | {values["rgb_mae"]:.8f} | {values["rgb_rmse"]:.8f} | {values["changed_pixels"]} |')
    (out/'calibration.md.gz').write_bytes(gzip.compress(('\n'.join(lines)+'\n').encode(),mtime=0))
    print(f'CALIBRATION {"PASS" if total_failures==0 else "FAIL"}: {len(cases)} cases, {len(records)} per-pose controls, {total_failures} failures',flush=True)
    raise SystemExit(0 if total_failures==0 else 1)


if __name__=='__main__':
    main()
