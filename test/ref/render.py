# SPDX-License-Identifier: MIT
"""Independent IR rasterizer; rules/citations: docs/specs/camera-and-projection.md.

Run from root with PYTHONPATH=tools/cook:test/ref. No native pack reader lives
here; only the CLI imports source readers, which return the common IR.
"""
import argparse
import json
from collections import deque
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from PIL import Image
from ir import face_uv

WIDTH, HEIGHT, FOCAL, NEAR, FAR = 320, 240, 256.0, 64.0, 10240.0


@dataclass
class Picture:
    rgb: np.ndarray
    depth: np.ndarray
    room: np.ndarray
    texture: np.ndarray
    coverage: np.ndarray
    texel: np.ndarray
    counts: dict
    texel_origin: np.ndarray | None = None

    def save(self, stem):
        stem = Path(stem)
        stem.parent.mkdir(parents=True, exist_ok=True)
        Image.fromarray(self.rgb).save(str(stem) + '.png')
        np.savez_compressed(str(stem) + '.npz', depth=self.depth,
                            room=self.room, texture=self.texture,
                            coverage=self.coverage, texel=self.texel,
                            visible_rooms=np.asarray(self.counts['visible_rooms'],dtype=np.int32))
        Path(str(stem) + '.json').write_text(json.dumps(self.counts, indent=2)+'\n')


class Camera:
    def __init__(self, scene, focal=FOCAL):
        self.position = np.asarray(scene['position'], dtype=float)
        f = np.asarray(scene['target'], dtype=float) - self.position
        if np.linalg.norm(f) == 0:
            raise ValueError('camera position equals target')
        f /= np.linalg.norm(f)
        r = np.cross([0, 1, 0], f)
        if np.linalg.norm(r) < 1e-10:
            raise ValueError('vertical camera has undefined right vector')
        r /= np.linalg.norm(r)
        self.matrix = np.array([r, np.cross(f, r), f])
        self.room = int(scene['room'])
        self.focal = focal
        self.offset = np.zeros(2)
        self.round_vertices = False
        self.quarter_vertices = False

    def transform(self, points):
        return (np.asarray(points) - self.position) @ self.matrix.T

    def project(self, points):
        xy = points[:, :2] * (self.focal / points[:, 2:3]) + [WIDTH/2, HEIGHT/2] + self.offset
        return np.rint(xy*4)/4 if self.quarter_vertices else (np.rint(xy) if self.round_vertices else xy)


def selected_rooms(level, flipped=False):
    indices = list(range(len(level.rooms)))
    if flipped:
        for i, r in enumerate(level.rooms):
            if r.alternate_room >= 0 and indices[i] == i:
                j = r.alternate_room
                indices[i], indices[j] = j, i
    return [level.rooms[i] for i in indices]


def portal_clip(room, portal, parent, camera, far=FAR):
    world = np.array([[portal[f'{a}{i}'] for a in 'xyz'] for i in range(4)], float)
    world += [room.info['x'], 0, room.info['z']]
    normal = [portal['normal_x'], portal['normal_y'], portal['normal_z']]
    if np.dot(normal, world[0] - camera.position) >= 0:
        return None
    p = camera.transform(world)
    if np.all(p[:, 2] <= 0) or np.all(p[:, 2] > far):
        return None
    bounds = [parent[2]-160, parent[3]-120, parent[0]-160, parent[1]-120]
    for x, y, z in p:
        if z > 0:
            v = np.array([x, y]) * camera.focal / min(z, far)
            bounds[0] = min(bounds[0], v[0])
            bounds[1] = min(bounds[1], v[1])
            bounds[2] = max(bounds[2], v[0])
            bounds[3] = max(bounds[3], v[1])
    bounds = np.array(bounds) + [160,120,160,120]
    for a, b in zip(p, np.roll(p, 1, axis=0)):
        if (a[2] <= 0) != (b[2] <= 0):
            for axis, size in [(0, WIDTH), (1, HEIGHT)]:
                if not (a[axis] > 0 and b[axis] > 0):
                    bounds[axis] = 0
                if not (a[axis] < 0 and b[axis] < 0):
                    bounds[axis+2] = size
    bounds[:2] = np.maximum(bounds[:2], parent[:2])
    bounds[2:] = np.minimum(bounds[2:], parent[2:])
    if np.any(bounds[:2] >= bounds[2:]):
        return None
    return bounds


def visible_rooms(rooms, camera, far=FAR):
    clips = {camera.room: np.array([0., 0., WIDTH, HEIGHT])}
    pending = deque([camera.room])
    while pending:
        index = pending.popleft()
        room = rooms[index]
        for portal in room.portals:
            clip = portal_clip(room, portal, clips[index], camera, far)
            if clip is None:
                continue
            dest = portal['room']
            old = clips.get(dest)
            merged = clip if old is None else np.r_[np.minimum(old[:2], clip[:2]),
                                                   np.maximum(old[2:], clip[2:])]
            if old is None or not np.array_equal(old, merged):
                clips[dest] = merged
                pending.append(dest)
    return clips


def clip_polygon(vertices, axis, plane, greater):
    """Sutherland-Hodgman in view space; carries UV and lighting attributes."""
    result = []
    for a, b in zip(vertices, np.roll(vertices, -1, axis=0)):
        ia = a[axis] >= plane if greater else a[axis] <= plane
        ib = b[axis] >= plane if greater else b[axis] <= plane
        if ia:
            result.append(a)
        if ia != ib:
            result.append(a + (b-a) * ((plane-a[axis])/(b[axis]-a[axis])))
    return np.asarray(result)


class Materials:
    def __init__(self, level, lighting_model='scalar', cooked=None):
        self.level = level
        self.lighting_model = lighting_model
        self.cooked = cooked
        self.cache = {}
        self.indices = {}
        if level.source == 'pc':
            self.palette = np.frombuffer(level.palette,np.uint8).reshape(256,3).astype(float)*4
            self.lightmap = np.frombuffer(level.light_map,np.uint8).reshape(32,256)

    def clut(self, index):
        words = np.frombuffer(self.level.cluts[index],'<u2')
        channels = np.stack([words&31,(words>>5)&31,(words>>10)&31],axis=1)
        return ((channels<<3)|(channels>>2)).astype(float),words != 0

    def image(self, index):
        if self.cooked is not None:
            rgba,_ = self.cooked[index]
            return rgba[:,:,:3].astype(float),rgba[:,:,3]!=0
        if index not in self.cache:
            tex = self.level.object_textures[index]
            raw = np.frombuffer(self.level.images[tex.page],np.uint8)
            if self.level.source == 'pc':
                idx = raw.reshape(256,256)
                rgb,alpha = self.palette[idx],idx != 0
            else:
                idx = np.empty(65536,np.uint8)
                idx[0::2],idx[1::2] = raw&15,raw>>4
                idx = idx.reshape(256,256)
                palette,visible = self.clut(tex.palette)
                rgb,alpha = palette[idx],visible[idx]
            self.indices[index] = idx
            self.cache[index] = (rgb,alpha)
        return self.cache[index]

    def flat(self, index, dark=None):
        if self.level.source == 'pc':
            if dark is None:
                return self.palette[index&255]
            if self.lighting_model=='pc_lut':
                return self.palette[self.lightmap[np.clip(np.asarray(dark,dtype=int)//256,0,31),index&255]]
            return self.palette[index&255]*modulation(dark)[:,None]
        tex = self.level.object_textures[index&0x7fff]
        u,v = tex.uv[0]
        rgb = self.image(index&0x7fff)[0][v,u]
        return rgb if dark is None else rgb*modulation(dark)[:,None]

    def sample(self, index, uv, bilinear=False, dark=None):
        image,alpha = self.image(index)
        bounds = np.asarray(self.level.object_textures[index].uv)
        uv = np.clip(uv,bounds.min(axis=0),bounds.max(axis=0))
        offset = np.asarray(self.cooked[index][1]) if self.cooked is not None else np.zeros(2,dtype=int)
        def fetch(x,y):
            x,y = x-offset[0],y-offset[1]
            if dark is not None and self.level.source=='pc' and self.lighting_model=='pc_lut':
                row = np.clip(np.asarray(dark,dtype=int)//256,0,31)
                return self.palette[self.lightmap[row,self.indices[index][y,x]]]
            rgb = image[y,x]
            return rgb if dark is None else rgb*modulation(dark)[:,None]
        point = np.floor(uv+.5).astype(int)
        if not bilinear:
            return fetch(point[:,0],point[:,1]),alpha[point[:,1]-offset[1],point[:,0]-offset[0]]
        lo = np.floor(uv).astype(int)
        hi = np.minimum(lo+1,bounds.max(axis=0))
        f = uv-lo
        rgb = np.zeros((len(uv),3))
        opacity = np.zeros(len(uv))
        for x in [0,1]:
            for y in [0,1]:
                px,py = (hi[:,0] if x else lo[:,0]),(hi[:,1] if y else lo[:,1])
                w = (f[:,0] if x else 1-f[:,0])*(f[:,1] if y else 1-f[:,1])
                rgb += fetch(px,py)*w[:,None]
                opacity += alpha[py-offset[1],px-offset[0]]*w
        return rgb,opacity >= .5


def darkness(lighting):
    if lighting['encoding'] == 'psx_vertex_8bit':
        return 8191-(lighting['value']<<5)
    return lighting['value']


def modulation(dark):
    """PSX-SPX GPU modulation: vertex colour 128 is neutral, not 255."""
    return np.clip((8191-np.asarray(dark))/4096,0,1.5)


def fog_factor(z):
    return np.clip((np.asarray(z)-8192)/2048,0,1)


def fog_darkness(dark, z, fog=True):
    """Fixed software renderer's additive cue; secondary PC-LUT output only."""
    dark = np.asarray(dark,float)
    if fog:
        dark = dark+np.maximum(0,np.asarray(z)-8192)*2
    return np.clip(dark,0,8191)


def raster_triangle(out, triangle, camera, clip, materials, texture, flat,
                    room_id, double_sided=False, bilinear=False, water=False, uv_origin=(0,0)):
    xy = camera.project(triangle)
    a,b,c = xy
    area = (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
    if abs(area) < 1e-10 or (area < 0 and not double_sided):
        return False
    left,top = np.maximum(np.floor(xy.min(axis=0)),np.ceil(clip[:2]-.5)).astype(int)
    right,bottom = np.minimum(np.ceil(xy.max(axis=0)),np.ceil(clip[2:]-.5)).astype(int)
    left,top,right,bottom = max(left,0),max(top,0),min(right,WIDTH),min(bottom,HEIGHT)
    if left>=right or top>=bottom:
        return False
    y,x = np.mgrid[top:bottom,left:right]
    px,py = x+.5,y+.5
    w0 = ((b[0]-px)*(c[1]-py)-(b[1]-py)*(c[0]-px))/area
    w1 = ((c[0]-px)*(a[1]-py)-(c[1]-py)*(a[0]-px))/area
    weights = np.stack([w0,w1,1-w0-w1],axis=-1)
    inside = np.all(weights >= -1e-9,axis=-1)
    invz = weights @ (1/triangle[:,2])
    z = 1/np.maximum(invz,1e-30)
    good = inside & (z < out.depth[top:bottom,left:right])
    if not good.any():
        return False
    ys,xs = y[good],x[good]
    interp = (weights[good] @ (triangle[:,3:]/triangle[:,2:3])) / invz[good,None]
    interp[:,2:] = weights[good] @ triangle[:,5:]
    if texture is None:
        rgb = np.broadcast_to(materials.flat(flat,interp[:,2]),(len(xs),3))
        keep = np.ones(len(xs),bool)
    else:
        rgb,keep = materials.sample(texture,interp[:,:2],bilinear,interp[:,2])
    ys,xs,rgb,interp = ys[keep],xs[keep],rgb[keep],interp[keep]
    if not len(xs):
        return False
    tint = np.array([.5,.8,.8]) if water else np.ones(3)
    out.rgb[ys,xs] = np.rint(np.clip(rgb*tint,0,255)*(1-interp[:,3:4])).astype(np.uint8)
    out.depth[ys,xs] = z[good][keep]
    out.room[ys,xs] = room_id
    out.texture[ys,xs] = texture if texture is not None else -2
    out.texel[ys,xs] = interp[:,:2]
    out.texel_origin[ys,xs] = uv_origin
    out.coverage[ys,xs] = True
    return True


def render(level, scene, *, bilinear=False, fault=None, flipped=False, lighting_model='scalar', cooked=None):
    """fault options deliberately perturb the real path for calibration only."""
    focal = FOCAL / 1.05 if fault == 'fov' else FOCAL
    camera = Camera(scene, focal)
    camera.offset[:] = .5 if fault == 'half_pixel' else 0
    camera.round_vertices = fault == 'round_vertices'
    camera.quarter_vertices = fault in ('quarter_vertices','combined')
    rooms = selected_rooms(level,bool(scene.get('flipped',flipped)))
    far = FAR*100 if fault == 'far_unculled' else (FAR*.5 if fault == 'far_culled' else FAR)
    clips = visible_rooms(rooms,camera,far)
    out = Picture(np.zeros((HEIGHT,WIDTH,3),np.uint8),
                  np.full((HEIGHT,WIDTH),np.inf),np.full((HEIGHT,WIDTH),-1,np.int32),
                  np.full((HEIGHT,WIDTH),-1,np.int32),np.zeros((HEIGHT,WIDTH),bool),
                  np.full((HEIGHT,WIDTH,2),np.nan),{})
    out.texel_origin = np.zeros((HEIGHT,WIDTH,2))
    materials = Materials(level,lighting_model,cooked)
    face_count = 0
    static_count = 0
    missing = scene.get('omit_room')
    if fault == 'missing_room' and missing is None:
        missing = next((i for i in clips if i != camera.room),None)

    def draw_faces(vertices, light, faces, room_id, clip, water):
        nonlocal face_count
        view = camera.transform(vertices)
        factors = fog_factor(view[:,2]) if lighting_model=='scalar' else np.zeros(len(view))
        if fault == 'no_fog':
            factors[:] = 0
        if fault == 'overbright_missing':
            light = 8191-np.minimum(modulation(light),1)*4096
        if fault in ('shade_floor','shade_round','combined'):
            gain = modulation(light)
            gain = np.minimum(255,np.floor(gain*170) if fault in ('shade_floor','combined') else np.rint(gain*170))/170
            light = 8191-gain*4096
            factors = (np.floor(factors*255) if fault in ('shade_floor','combined') else np.rint(factors*255))/255
        for face in faces:
            indices = list(face.vertices)
            uv = np.zeros((len(indices),2)) if face.texture is None else np.asarray(face_uv(level,face),float)
            if fault == 'corners' and len(indices)==4:
                uv = np.roll(uv,1,axis=0)
            polygon = np.column_stack([view[indices],uv,light[indices],factors[indices]])
            drawn = False
            texture = face.texture
            if fault == 'textures' and texture is not None:
                texture = (texture+len(level.object_textures)//2+1)%len(level.object_textures)
            for i in range(1,len(polygon)-1):
                tri = clip_polygon(polygon[[0,i,i+1]],2,NEAR,True)
                if len(tri)<3:
                    continue
                if np.all(tri[:,2]>far):
                    continue
                for j in range(1,len(tri)-1):
                    drawn |= raster_triangle(out,tri[[0,j,j+1]],camera,clip,materials,
                                             texture,face.color,room_id,bool(face.flags&0x8000),bilinear,water,uv.min(axis=0))
            face_count += int(drawn)

    defs = {d['id']:d for d in level.data.get('static_meshes',[])}
    for index,clip in clips.items():
        if fault == 'missing_room' and index == missing:
            continue
        room = rooms[index]
        vertices = np.array([[v[a] for a in 'xyz'] for v in room.vertices],float)
        if len(vertices):
            vertices += [room.info['x'],0,room.info['z']]
            light = fog_darkness([darkness(v['lighting']) for v in room.vertices],camera.transform(vertices)[:,2],fog=lighting_model=='pc_lut')
            if fault == 'lighting':
                light[:] = 4096 if level.source=='pc' else 4095
            draw_faces(vertices,light,room.quads+room.triangles,index,clip,bool(room.flags&1))
        for instance in room.static_instances:
            definition = defs[instance['mesh']]
            if not definition['flags']&2:
                continue
            mesh = level.meshes[level.data['mesh_pointers'][definition['mesh']]]
            vertices = np.array(mesh.vertices,float)
            angle = (instance['angle']//16384)*np.pi/2
            c,s = np.cos(angle),np.sin(angle)
            vertices = vertices @ np.array([[c,0,-s],[0,1,0],[s,0,c]])
            origin = np.array([instance[a] for a in 'xyz'])
            vertices += origin
            dark = np.array(mesh.lighting if mesh.lighting is not None else [4096]*len(vertices),float)
            dark += instance['intensity']-4096
            light = fog_darkness(dark,camera.transform([origin])[0,2],fog=lighting_model=='pc_lut')
            if fault == 'lighting':
                light[:] = 4096 if level.source=='pc' else 4095
            draw_faces(vertices,light,mesh.faces,index,clip,bool(room.flags&1))
            static_count += 1
    out.counts = {'visible_rooms':list(clips),'visible_room_count':len(clips),
                  'faces_drawn':face_count,'static_instances':static_count,
                  'covered_pixels':int(out.coverage.sum()),'source':level.source,
                  'source_sha256':level.sha256,'scene':scene,'fault':fault}
    return out


def load_level(data, source, name):
    import src_pc
    import src_psx
    data = Path(data)
    return (src_pc.load(data/'pc'/f'{name.lower()}.phd') if source == 'pc'
            else src_psx.load(data/'psx/PSXDATA'/f'{name.upper()}.PSX'))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--data',required=True)
    p.add_argument('--source',choices=['pc','psx'],required=True)
    p.add_argument('--scene',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--bilinear',action='store_true')
    p.add_argument('--lighting-model',choices=['scalar','pc_lut'],default='scalar')
    args = p.parse_args()
    scene = json.loads(args.scene.read_text())
    picture = render(load_level(args.data,args.source,scene['level']),scene['camera'],bilinear=args.bilinear,lighting_model=args.lighting_model)
    picture.save(args.output)
    print(json.dumps(picture.counts))


if __name__ == '__main__':
    main()
