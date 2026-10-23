"""Automatic glass selection and the existing project's static Effect-path conversion.

NIF field layout: niftools/nifxml, Fallout 4 stream 130.
Shader settings follow prepare_liquors.py's tested glass path; no asset template is shipped.
"""
from pathlib import Path
import re
import struct
import numpy as np

from nif_mesh import Reader
from nif_document import parse, object_refs, intern, hide


def bounds(shape):
    from .core import points_world
    points = points_world(shape)
    return points.min(0), points.max(0)


def detect_target(model, selected):
    # A selected glass wall is already a target. Do not replace it with an
    # enclosing label shell simply because that shell's bounds are larger.
    if (re.search(r'glass|crystal|bottle', selected['name'], re.I)
            and not re.search(r'liquid|bubble|foam|label|labl|cap|cork',selected['name'],re.I)):
        return selected
    low, high = bounds(selected); height = float((high - low).max())
    tolerance = max(height * .01, .01)
    candidates = []
    for shape in model['meshes']:
        if shape['block'] == selected['block']:
            continue
        name = shape['name'].lower()
        if re.search(r'liquid|bubble|foam|label|labl|cap|cork|frost|decal', name):
            continue
        a, b = bounds(shape)
        if np.any(a > low + tolerance) or np.any(b < high - tolerance):
            continue
        if np.min(b-a) <= 0:
            continue
        candidates.append((float(np.prod(b-a)), shape))
    candidates.sort(key=lambda item: item[0])
    if candidates:
        if len(candidates) > 1 and candidates[1][0] / candidates[0][0] < 1.02:
            raise ValueError('Two enclosing glass shapes are equally plausible. Expand Advanced and choose the glass target manually.')
        return candidates[0][1]
    if not re.search(r'liquid|bubble|foam|label|cap|cork', selected['name'], re.I):
        return selected
    raise ValueError('No enclosing bottle shape was found. Expand Advanced and choose a glass target, or select the full liquid volume instead of an isolated surface.')


def _texture_path(path):
    path = path.replace('/', '\\').strip('\x00')
    if not path:
        return ''
    return path if path.lower().startswith('textures\\') else 'textures\\' + path


def lighting_maps(model, shape, asset_root=None):
    parts = parse(Path(model['path']).read_bytes())
    chunk = parts[5][shape['block']]; end = object_refs(chunk, av=True)[0]
    shader_id = struct.unpack_from('<i', chunk, end + 20)[0]
    shader = parts[5][shader_id]
    shader_end = object_refs(shader, start=4)[0]
    uv = list(struct.unpack_from('<4f', shader, shader_end + 8))
    textures_id = struct.unpack_from('<i', shader, shader_end + 24)[0]
    embedded = []
    if textures_id >= 0 and parts[2][parts[3][textures_id]] == 'BSShaderTextureSet':
        reader = Reader(parts[5][textures_id]); embedded = [reader.string() for _ in range(reader.get('I'))]
    # BGSM texture strings are authoritative when a material file is present.
    relative = shape['material'].replace('\\', '/').removeprefix('Data/').removeprefix('data/')
    roots = ([Path(asset_root)] if asset_root else []) + list(Path(model['path']).parents)
    for root in roots:
        path = root / relative
        if relative and path.is_file():
            raw = path.read_bytes()
            if raw[:4] != b'BGSM' or struct.unpack_from('<I', raw, 4)[0] != 2:
                raise ValueError(f'Automatic material conversion currently supports BGSM version 2: {path.name}')
            reader = Reader(raw); reader.pos = 63
            textures = [reader.string().strip('\x00') for _ in range(9)]
            uv = list(struct.unpack_from('<4f', raw, 12))
            return {'diffuse': _texture_path(textures[0]), 'normal': _texture_path(textures[1]),
                    'mask': _texture_path(textures[2]), 'cube': _texture_path(textures[3]), 'uv': uv,
                    'source': str(path)}
    if embedded and embedded[0]:
        slots = embedded + [''] * 9
        return {'diffuse': _texture_path(slots[0]), 'normal': _texture_path(slots[1]),
                'cube': _texture_path(slots[4]), 'mask': _texture_path(slots[7] or slots[5]),
                'uv': uv, 'source': 'NIF texture set (external material not available)'}
    raise ValueError('The glass material is external and has no embedded diffuse texture. Keep the original Materials folder beside Meshes, or choose the mod Data folder in Advanced.')


def plan(model, selected, target=None, asset_root=None):
    target = target or detect_target(model, selected)
    low, high = bounds(target); tolerance = max(float((high-low).max()) * .01, .01)
    hidden = []
    if target['block'] != selected['block']:
        hidden.append(selected['block'])
        for shape in model['meshes']:
            if shape['block'] in (target['block'], selected['block']):
                continue
            a, b = bounds(shape)
            inside = np.all(a >= low-tolerance) and np.all(b <= high+tolerance)
            if inside and re.search(r'liquid|bubble|foam', shape['name'], re.I):
                hidden.append(shape['block'])
    result = {'mode': 'automatic', 'target_block': target['block'], 'hidden_blocks': sorted(set(hidden)),
              'target_name': target['name'], 'original_shader': target['shader'],
              'original_material': target['material'], 'maps': None}
    if target['shader'] == 'BSLightingShaderProperty':
        result['maps'] = lighting_maps(model, target, asset_root)
    elif target['shader'] != 'BSEffectShaderProperty':
        raise ValueError('This glass shader is unsupported; choose a static Lighting or Effect shape.')
    return result, target


def infer_axis(target,selected):
    low,high=bounds(target);sl,sh=bounds(selected);index=int(np.argmax(high-low))
    lower_gap=sl[index]-low[index];upper_gap=high[index]-sh[index];threshold=float(high[index]-low[index])*.1
    return ('-' if lower_gap>threshold and lower_gap-upper_gap>threshold else '+')+'XYZ'[index]

def calculate(model, selected, *, target=None, axis=None, clearance=.16, trim=.005, center_offset=(0., 0.), asset_root=None, auto_trim=True, cancel=None):
    from .core import fit_cavity, SectionError, points_world, axis_rotation
    preparation, target = plan(model, selected, target, asset_root)
    if axis is None:
        axis=infer_axis(target,selected)
    # Fit the whole bottle, not the arbitrary fill height of its original static liquid.
    requested_trim=trim
    height=float(np.ptp((points_world(target) @ axis_rotation(axis).T)[:,2]))
    while True:
        try:
            fit = fit_cavity(target, target, axis, clearance, trim, center_offset,cancel=cancel)
            break
        except SectionError as error:
            # Only omit irregular end regions, never repair a seam through the
            # body or conceal an off-center axis. The full fitter runs again.
            distance=min(error.height,height-error.height)/height
            if not auto_trim or not 0 <= distance < .05 or trim >= .05:
                raise
            adjusted=max(trim+.005,(np.floor(distance/.005)+1)*.005)
            trim=min(.05,round(float(adjusted),6))
    fit['audit']['requested_end_trim_fraction']=requested_trim
    fit['audit']['automatic_end_trim']=trim!=requested_trim
    fit['preparation'] = preparation
    fit['audit']['scope'] = 'Full enclosing bottle, measured at nominal 0.16 thickness; original liquid only identifies the bottle.'
    fit['audit']['selected_liquid'] = selected['name']
    fit['audit']['automatic_target'] = target['name']
    return fit


def _sized(value):
    raw = value.encode('utf-8'); return struct.pack('<I', len(raw)) + raw


def effect_shader(maps):
    # NiObjectNET; flags; UV; Effect shader fields in nifxml stream 130 order.
    out = struct.pack('<IIiII4f', 0xFFFFFFFF, 0, -1, 0x800000C0, 0x40000001, *maps['uv'])
    out += _sized(maps['diffuse']) + struct.pack('<4B4f4f2f', 3, 250, 0, 0,
                                              1., 0., .85, .95, 1., 1., 1., 1., .35, 100.)
    for texture in ('', maps['cube'], maps['normal'], maps['mask']):
        out += _sized(texture)
    return out + struct.pack('<f', .5)


def apply(parts, target, preparation, tag):
    old_types = list(parts[2]); chunks = parts[5]; target_id = target['block']
    end = object_refs(chunks[target_id], av=True)[0]
    original = chunks[target_id]
    desc, nt, nv, size = struct.unpack_from('<QIHI', original, end + 28)
    stride = (desc & 15) * 4; start = end + 46
    uv = ((desc >> 8) & 15)*4; normal = ((desc >> 16) & 15)*4; tangent = ((desc >> 20) & 15)*4
    if min(uv, normal, tangent) < 8 or max(uv, normal, tangent)+4 > stride:
        raise ValueError('This vertex layout lacks the UV/normal/tangent data needed for glass conversion.')
    vertices = []
    for i in range(nv):
        vertex = original[start+i*stride:start+(i+1)*stride]
        vertices.append(vertex[:8] + vertex[uv:uv+4] + vertex[normal:normal+4] + vertex[tangent:tangent+4])
    header = bytearray(original[:end+28]); struct.pack_into('<I', header, 0, intern(parts[4], tag))

    def append(kind, chunk):
        if kind not in parts[2]: parts[2].append(kind)
        parts[3].append(parts[2].index(kind)); chunks.append(chunk)
        return len(chunks)-1

    if preparation['maps'] is not None:
        shader_id = append('BSEffectShaderProperty', effect_shader(preparation['maps']))
        alpha_id = append('NiAlphaProperty', struct.pack('<IIiHB', 0xFFFFFFFF, 0, -1, 0x10ED, 128))
        struct.pack_into('<ii', header, end+20, shader_id, alpha_id)
    indices = original[start+nv*stride:start+nv*stride+nt*6]
    chunks[target_id] = bytes(header) + struct.pack('<QIHI', 0x1b00000430205, nt, nv, nv*20+nt*6) + b''.join(vertices) + indices
    changed = {target_id}
    for block in preparation['hidden_blocks']:
        if block == target_id:
            raise ValueError('Cannot hide the glass target.')
        chunks[block] = hide(chunks[block]); changed.add(block)
    if parts[2] != old_types:
        old_size = 2 + sum(4+len(t.encode('utf-8')) for t in old_types)
        parts[1] = parts[1][:-old_size] + struct.pack('<H', len(parts[2])) + b''.join(_sized(t) for t in parts[2])
    return changed
