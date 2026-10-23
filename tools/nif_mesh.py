"""Read static FO4 BSTriShape data. Deliberately rejects unsupported NIF layouts.

Format reference: https://github.com/niftools/nifxml (BS stream 130).
This is an inspector, not a NIF writer; source assets are never modified.
"""
import hashlib
import struct
from pathlib import Path


class Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def take(self, n):
        if n < 0 or self.pos + n > len(self.data):
            raise ValueError("Truncated NIF")
        result = self.data[self.pos:self.pos + n]
        self.pos += n
        return result

    def get(self, fmt):
        result = struct.unpack('<' + fmt, self.take(struct.calcsize('<' + fmt)))
        return result[0] if len(result) == 1 else result

    def string(self):
        return self.take(self.get('I')).decode('utf-8')


def read_nif(path):
    data = Path(path).read_bytes()
    r = Reader(data)
    header = r.take(data.index(b'\n') + 1)
    if header != b'Gamebryo File Format, Version 20.2.0.7\n':
        raise ValueError('Unsupported NIF header')
    version, endian, user, count, stream = r.get('IBIII')
    if (version, endian, user, stream) != (0x14020007, 1, 12, 130):
        raise ValueError('Only little-endian Fallout 4 stream 130 is supported')
    for _ in range(4):
        r.take(r.get('B'))
    types = [r.string() for _ in range(r.get('H'))]
    type_ids = [r.get('H') for _ in range(count)]
    sizes = [r.get('I') for _ in range(count)]
    string_count, _ = r.get('II')
    strings = [r.string() for _ in range(string_count)]
    r.take(r.get('I') * 4)
    blocks = [(types[t], r.take(n)) for t, n in zip(type_ids, sizes)]
    roots = [r.get('i') for _ in range(r.get('I'))]
    if r.pos != len(data):
        raise ValueError('Unexpected trailing data')
    meshes = []
    for index, (kind, block) in enumerate(blocks):
        if kind != 'BSTriShape':
            continue
        s = Reader(block)
        name = strings[s.get('I')]
        s.take(s.get('I') * 4)  # extra-data references
        controller, flags = s.get('iI')
        translation = list(s.get('3f'))
        rotation = list(s.get('9f'))
        scale = s.get('f')
        collision = s.get('i')
        sphere = list(s.get('4f'))
        skin, shader, alpha, desc, nt, nv, size = s.get('iiiQIHI')
        attrs, stride = desc >> 44, (desc & 15) * 4
        if skin != -1 or controller != -1 or attrs not in (0x1b,0x3b):
            raise ValueError(f'Unsupported animated/skinned/vertex layout: {name}')
        if size != stride * nv + 6 * nt:
            raise ValueError('Vertex/index buffer size mismatch')
        positions, normals, uvs = [], [], []
        for _ in range(nv):
            vertex = s.take(stride)
            positions.extend(struct.unpack_from('<3e', vertex))
            uv_offset = ((desc >> 8) & 15) * 4
            normal_offset = ((desc >> 16) & 15) * 4
            uvs.extend(struct.unpack_from('<2e', vertex, uv_offset))
            normals.extend(v / 127.5 - 1 for v in vertex[normal_offset:normal_offset + 3])
        indices = list(s.get(str(nt * 3) + 'H'))
        if s.pos != len(block) or max(indices, default=0) >= nv:
            raise ValueError('Invalid triangle buffer')
        shader_kind, shader_data = blocks[shader]
        material_offset = 4 if shader_kind == 'BSLightingShaderProperty' else 0
        material_index = struct.unpack_from('<I', shader_data, material_offset)[0]
        material = '' if material_index == 0xffffffff else strings[material_index]
        meshes.append(dict(block=index, name=name, material=material, shader=shader_kind,
                           alpha_block=alpha, vertices=nv, triangles=nt, descriptor=desc, stride=stride,
                           positions=positions, normals=normals, uvs=uvs, indices=indices,
                           local_transform=dict(translation=translation, rotation=rotation, scale=scale)))
    return dict(source=Path(path).name, sha256=hashlib.sha256(data).hexdigest(),
                roots=roots, meshes=meshes)
