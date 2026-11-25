# SPDX-License-Identifier: MIT
"""Source-independent TR1 level IR; layout facts: OpenLara 2333adef format.h.

No source reader code is copied. See docs/formats/ir.md for units/absence.
"""
from dataclasses import dataclass, field


@dataclass
class Face:
    vertices: tuple
    texture: int | None
    color: int | None
    flags: int
    texture_corners: tuple | None = None

    def __post_init__(self):
        if self.texture is not None and self.texture_corners is None:
            self.texture_corners = tuple(range(len(self.vertices)))


def face_uv(level, face):
    return tuple(level.object_textures[face.texture].uv[i] for i in face.texture_corners)


@dataclass
class Mesh:
    center: tuple
    radius: int
    flags: int
    vertices: list
    normals: list | None
    lighting: list | None
    faces: list
    source_offset: int
    reserved: dict = field(default_factory=dict)


@dataclass
class Room:
    info: dict
    vertices: list
    quads: list
    triangles: list
    sprites: list
    portals: list
    z_sectors: int
    x_sectors: int
    sectors: list
    ambient: dict
    lights: list
    static_instances: list
    alternate_room: int
    flags: int
    reserved: dict = field(default_factory=dict)


@dataclass
class Texture:
    page: int
    palette: int | None
    uv: tuple
    attribute: int | None
    bounds: tuple | None
    reserved: dict = field(default_factory=dict)


@dataclass
class Level:
    source: str
    name: str
    sha256: str
    file_bytes: int
    rooms: list = field(default_factory=list)
    data: dict = field(default_factory=dict)
    meshes: dict = field(default_factory=dict)
    object_textures: list = field(default_factory=list)
    sprite_textures: list = field(default_factory=list)
    images: list = field(default_factory=list)
    palette: bytes | None = None
    cluts: list | None = None
    light_map: bytes | None = None
    samples: list = field(default_factory=list)
    sample_encoding: str = ''
    sections: list = field(default_factory=list)
    reserved: dict = field(default_factory=dict)


# Ordered fields and explicit little-endian widths, not host struct layouts.
SCHEMAS = {
    'animations': ('IBBHii8H', ('frame_offset', 'frame_rate', 'frame_size',
        'state', 'speed', 'acceleration', 'frame_start', 'frame_end',
        'next_animation', 'next_frame', 'state_count', 'state_offset',
        'command_count', 'command_offset')),
    'states': ('3H', ('state', 'range_count', 'range_offset')),
    'ranges': ('4h', ('low', 'high', 'next_animation', 'next_frame')),
    'models': ('IHHIIH', ('type', 'mesh_count', 'mesh_start', 'bone_offset',
        'frame_offset', 'animation')),
    'static_meshes': ('IH12hH', ('id', 'mesh', 'visible_min_x', 'visible_max_x',
        'visible_min_y', 'visible_max_y', 'visible_min_z', 'visible_max_z',
        'collision_min_x', 'collision_max_x', 'collision_min_y', 'collision_max_y',
        'collision_min_z', 'collision_max_z', 'flags')),
    'sprite_sequences': ('IhH', ('type', 'count', 'first')),
    'cameras': ('3ihH', ('x', 'y', 'z', 'room_or_speed', 'flags')),
    'sound_sources': ('3iHH', ('x', 'y', 'z', 'sound', 'flags')),
    'boxes': ('4ihH', ('min_z', 'max_z', 'min_x', 'max_x', 'floor', 'overlap')),
    'items': ('HH3iHhH', ('type', 'room', 'x', 'y', 'z', 'angle', 'intensity', 'flags')),
    'cinematic_frames': ('8h', ('target_x', 'target_y', 'target_z', 'camera_x',
        'camera_y', 'camera_z', 'fov', 'roll')),
    'sound_details': ('4H', ('sample', 'volume', 'chance', 'flags')),
    'sectors': ('HHBbBb', ('floor_index', 'box_index', 'room_below', 'floor',
        'room_above', 'ceiling')),
    'portals': ('H15h', ('room', 'normal_x', 'normal_y', 'normal_z',
        'x0', 'y0', 'z0', 'x1', 'y1', 'z1', 'x2', 'y2', 'z2', 'x3', 'y3', 'z3')),
}
