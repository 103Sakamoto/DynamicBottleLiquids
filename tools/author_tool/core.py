"""Authoring model. The renderer-facing adapter is confined to export_patch()."""
from __future__ import annotations

import copy
import hashlib
import json
import math
import re
import struct
import tempfile
import zipfile
from pathlib import Path, PurePosixPath

import numpy as np
import trimesh

from nif_mesh import Reader, read_nif
from nif_document import parse, save as save_nif, intern, hide


FIELDS = {
    "liquid_level": (0, 1, .9), "opacity": (0, 1, .6),
    "glass_thickness": (.01, 1, .16), "glass_opacity": (0, .95, 0),
    "bubble_density": (0, 10, 1), "shaking_bubbles": (0, 1, 1),
    "surface_foam": (0, 1, 0), "refraction_strength": (0, 20, 3),
    "glow_strength": (0, 10, 0), "wet_absorption": (0, 1, 0),
    "wet_smoothness": (0, 1, .15), "wet_refraction": (0, 1, .8),
    "wet_drying": (.1, 20, 3.5),
}
PERCENT_FIELDS = {"liquid_level", "opacity", "glass_opacity", "shaking_bubbles", "surface_foam",
                  "wet_absorption", "wet_smoothness", "wet_refraction"}
DEFAULTS = {key: spec[2] for key, spec in FIELDS.items()}
DEFAULTS.update(liquid_color="#6E3617", glass_color="#FFFFFF")
GEOMETRY_KEYS = {"id", "mesh_path", "shape", "vertices", "indices", "matrix", "rings"}
NODE_TYPES = {"NiNode", "BSFadeNode", "BSOrderedNode", "BSValueNode"}


def slug(value):
    if not isinstance(value, str) or not re.fullmatch(r"[a-z][a-z0-9-]{0,63}", value):
        raise ValueError("Use a lowercase name beginning with a letter (letters, digits and hyphens, max 64).")
    return value


def mesh_path(value):
    value = value.replace("\\", "/")
    path = PurePosixPath(value)
    if (path.is_absolute() or ".." in path.parts or ":" in value or
            len(path.parts) < 2 or path.parts[0].lower() != "meshes" or path.suffix.lower() != ".nif"):
        raise ValueError("Use a relative game path such as Meshes/props/my-bottle.nif.")
    return str(path)


def controls(values):
    if not isinstance(values, dict):
        raise ValueError("Appearance controls must be an object.")
    for key, value in values.items():
        if key in ("liquid_color", "glass_color"):
            if not isinstance(value, str) or not re.fullmatch(r"#[0-9a-fA-F]{6}", value):
                raise ValueError(f"{key}: use #RRGGBB.")
        elif key in FIELDS:
            lo, hi, _ = FIELDS[key]
            if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not lo <= value <= hi:
                raise ValueError(f"{key}: expected a finite number from {lo} to {hi}.")
        else:
            raise ValueError(f"Unknown appearance setting: {key}")
    return values


def snapped(value, key):
    lo, hi, _ = FIELDS[key]
    steps = 20 if key in PERCENT_FIELDS else 10
    return round(min(hi, max(lo, math.floor(float(value) * steps + .5) / steps)), 6)


def display_value(value, key):
    return round(float(value) * (100 if key in PERCENT_FIELDS else 1), 6)


def stored_value(value, key):
    return round(float(value) / (100 if key in PERCENT_FIELDS else 1), 6)


def new_project(pack="my-pack"):
    return {"format": "dbl-author-project", "version": 1, "pack": slug(pack),
            "geometry": {"schema_version": 1, "definitions": []},
            "appearance": {"schema_version": 1, "defaults": copy.deepcopy(DEFAULTS),
                           "groups": {name: {} for name in ("cola", "vim", "beer", "spirits")},
                           "profiles": {}}, "sources": {}}


def effective_appearance(project, profile_id):
    appearance = project["appearance"]
    profile = appearance["profiles"][profile_id]
    group = profile["group"]
    if group is not None and group not in appearance["groups"]:
        raise ValueError(f"Missing appearance group: {group}")
    result = copy.deepcopy(appearance["defaults"])
    if group is not None:result.update(appearance["groups"][group])
    result.update(profile["overrides"])
    return controls(result)


def local_matrix(transform):
    matrix = np.eye(4)
    matrix[:3, :3] = np.asarray(transform["rotation"]).reshape(3, 3) * transform["scale"]
    matrix[:3, 3] = transform["translation"]
    return matrix


def node_matrices(parts):
    """Resolve NiNode ancestry, rejecting instancing instead of guessing a parent."""
    _, _, types, ids, _, chunks, roots = parts
    result, visiting = {}, set()

    def visit(index, parent):
        if index == -1:
            return
        if index < 0 or index >= len(chunks):
            raise ValueError("Invalid NIF child reference.")
        kind = types[ids[index]]
        if kind not in NODE_TYPES | {"BSTriShape"}:
            return
        if index in visiting or index in result:
            raise ValueError("Cyclic or multiply-parented scene graph is unsupported.")
        visiting.add(index)
        reader = Reader(chunks[index])
        reader.get("I"); reader.take(reader.get("I") * 4)
        controller, _ = reader.get("iI")
        if controller != -1:
            raise ValueError("Controlled scene nodes need a dedicated animation adapter.")
        transform = dict(translation=reader.get("3f"), rotation=reader.get("9f"), scale=reader.get("f"))
        reader.get("i")
        world = parent @ local_matrix(transform)
        result[index] = world
        if kind in NODE_TYPES:
            count = reader.get("I")
            children = struct.unpack("<" + "i" * count, reader.take(count * 4))
            for child in children:
                visit(child, world)
        visiting.remove(index)

    for root in roots:
        visit(root, np.eye(4))
    return result


def load_mesh(path):
    path = Path(path).resolve()
    if path.stat().st_size > 64 * 1024 * 1024:
        raise ValueError("NIF exceeds the tool's 64 MiB limit.")
    model = read_nif(path)
    if not model["meshes"]:
        raise ValueError("No supported static BSTriShape was found.")
    transforms = node_matrices(parse(path.read_bytes()))
    for shape in model["meshes"]:
        if shape["block"] not in transforms:
            raise ValueError(f"Cannot resolve scene transform for {shape['name']}.")
        shape["world_matrix"] = transforms[shape["block"]].tolist()
    model["path"] = str(path)
    return model


def points_world(shape):
    points = np.asarray(shape["positions"], dtype=float).reshape(-1, 3)
    matrix = np.asarray(shape["world_matrix"])
    return points @ matrix[:3, :3].T + matrix[:3, 3]


def axis_rotation(axis):
    if axis not in ("+X", "-X", "+Y", "-Y", "+Z", "-Z"):
        raise ValueError("Choose an upright model axis (+X, -X, +Y, -Y, +Z or -Z).")
    index = "XYZ".index(axis[1]); sign = 1 if axis[0] == "+" else -1
    z = np.eye(3)[index] * sign
    x = np.eye(3)[(index + 1) % 3]
    return np.stack((x, np.cross(z, x), z))


class SectionError(ValueError):
    def __init__(self, message, height):
        super().__init__(message)
        self.height=float(height)


def section_radius(mesh, height):
    """Trimesh slices the triangles; exact segment distances bound a circular cavity."""
    lines = trimesh.intersections.mesh_plane(mesh, [0, 0, 1], [0, 0, float(height)])
    if len(lines) < 3:
        raise SectionError(f"No closed cavity section at height {height:.5g}.", height)
    lines = lines[:, :, :2]
    # Weld numerical endpoint differences and reject open/non-manifold contours.
    scale = max(float(np.max(np.abs(lines))), 1.)
    keys = np.round(lines.reshape(-1, 2) / (scale * 1e-7)).astype(np.int64)
    _, inverse, counts = np.unique(keys, axis=0, return_inverse=True, return_counts=True)
    if np.any(counts != 2):
        raise SectionError(f"Open or overlapping section at height {height:.5g}; select a closed volume or continuous wall.", height)
    a, b = lines[:, 0], lines[:, 1]
    crossing = (a[:, 1] > 0) != (b[:, 1] > 0)
    cross_x = a[crossing, 0] - a[crossing, 1] * (b[crossing, 0] - a[crossing, 0]) / (b[crossing, 1] - a[crossing, 1])
    # Glass walls can produce nested inner/outer loops. Global even/odd parity
    # describes the solid glass, incorrectly excluding the hollow cavity. Test
    # each closed loop independently, then bound radius by every wall segment.
    endpoints=inverse.reshape(-1,2); touching={}
    for segment,(u,v) in enumerate(endpoints):
        touching.setdefault(u,[]).append(segment);touching.setdefault(v,[]).append(segment)
    positive=np.zeros(len(lines),dtype=bool);positive[crossing]=cross_x>0
    unseen=set(range(len(lines)));enclosed=False
    while unseen:
        pending=[unseen.pop()];crossings=0
        while pending:
            segment=pending.pop();crossings+=int(positive[segment])
            for point in endpoints[segment]:
                for neighbor in touching[point]:
                    if neighbor in unseen:unseen.remove(neighbor);pending.append(neighbor)
        enclosed |= crossings % 2 == 1
    if not enclosed:
        raise SectionError(f"The cavity axis is outside the selected section at height {height:.5g}. Adjust center or shape.", height)
    edge = b - a
    denominator = np.einsum("ij,ij->i", edge, edge)
    if np.any(denominator < 1e-20):
        raise ValueError("Degenerate section segments; repair the mesh before authoring.")
    t = np.clip(-np.einsum("ij,ij->i", a, edge) / denominator, 0, 1)
    nearest = np.linalg.norm(a + t[:, None] * edge, axis=1)
    return float(nearest.min())


def fit_cavity(shape, target, axis="+Z", clearance=.05, trim=.005, center_offset=(0., 0.), cancel=None):
    if not math.isfinite(clearance) or clearance < 0:
        raise ValueError("Wall clearance must be finite and non-negative.")
    if not math.isfinite(trim) or not 0 < trim < .25:
        raise ValueError("End trim must be greater than 0 and less than 0.25 of the shape height.")
    if len(center_offset) != 2 or not all(math.isfinite(x) for x in center_offset):
        raise ValueError("Center offsets must be two finite numbers.")
    rotation = axis_rotation(axis)
    points = points_world(shape) @ rotation.T
    center = (points.min(0) + points.max(0)) / 2
    center[:2] += center_offset
    center[2] = points[:, 2].min()
    points -= center
    height = float(np.ptp(points[:, 2]))
    if height <= 1e-5:
        raise ValueError("Selected shape is flat; choose the liquid volume or bottle wall, not only the liquid top.")
    low, high = height * trim, height * (1 - trim)
    faces = np.asarray(shape["indices"], dtype=int).reshape(-1, 3)
    mesh = trimesh.Trimesh(vertices=points, faces=faces, process=False)
    # Probe both sides of every topology change as well as a regular grid.
    vertices_z = np.unique(points[:, 2]); eps = height * 1e-6
    levels = np.unique(np.r_[np.linspace(low, high, 129), vertices_z - eps, vertices_z + eps])
    levels = levels[(levels >= low) & (levels <= high)]
    if len(levels) > 4096:
        raise ValueError("Too many height transitions for this authoring pass (limit 4096).")
    def radius(z):
        if cancel and cancel():
            from concurrent.futures import CancelledError
            raise CancelledError()
        return section_radius(mesh,z)-clearance
    limits = np.array([radius(z) for z in levels])
    if limits.min() <= .05:
        raise ValueError("Clearance leaves too little room for liquid; review shape, axis and clearance.")
    selected = [0, len(levels) - 1]
    for _ in range(14):
        prediction = np.interp(levels, levels[selected], limits[selected])
        errors = np.abs(prediction - limits); errors[selected] = -1
        index = int(np.argmax(errors))
        if errors[index] < 1e-5:
            break
        selected.append(index); selected.sort()
    ring_z = levels[selected]
    ring_r = limits[selected].copy()
    # A single inward offset gives a conservative fit across all measured heights.
    inset = max(0., float((np.interp(levels, ring_z, ring_r) - limits).max())) + eps
    ring_r -= inset
    checks = np.unique(np.r_[levels, np.linspace(low + eps, high - eps, 513)])
    bounds = np.array([radius(z) for z in checks])
    correction = max(0., float((np.interp(checks, ring_z, ring_r) - bounds).max())) + eps
    ring_r -= correction
    if ring_r.min() <= .05:
        raise ValueError("A 16-ring cavity cannot fit this shape safely.")
    loss = float(np.max((bounds - np.interp(checks, ring_z, ring_r)) / bounds))
    if loss > .25:
        raise ValueError(f"A circular 16-ring cavity loses {loss:.0%} of radius at some heights. Manual geometry work is needed.")
    canonical = np.eye(4); canonical[:3, :3] = rotation; canonical[:3, 3] = -center
    matrix = canonical @ np.asarray(target["world_matrix"])
    axes = matrix[:3, :3]; gram = axes @ axes.T
    if not np.allclose(gram, np.eye(3) * gram[0, 0], atol=1e-5) or gram[0, 0] <= 1e-8:
        raise ValueError("Target transform has unsupported shear/nonuniform scale.")
    return {"matrix": matrix.reshape(-1).tolist(), "rings": np.column_stack((ring_z, ring_r)).tolist(),
            "audit": {"axis": axis, "clearance": clearance, "end_trim_fraction": trim,
                      "center_offset": list(center_offset), "checked_sections": len(checks),
                      "max_radius_loss_fraction": loss, "minimum_measured_clearance": float(
                          (bounds + clearance - np.interp(checks, ring_z, ring_r)).min()),
                      "reference_glass_thickness": .16,
                      "scope": "Selected shape extent only; finite section checks, not an in-game validation."},
            "canonical_matrix": canonical.tolist()}


def add_profile(project, model, cavity_block, target_block, result, profile_id, relative_path, group, hide_liquid=False):
    slug(profile_id)
    if group is not None:slug(group)
    relative_path = mesh_path(relative_path)
    if profile_id in project["sources"]:
        raise ValueError("That profile ID already exists; choose a unique ID.")
    if any(PurePosixPath(d["mesh_path"]).name.lower() == PurePosixPath(relative_path).name.lower()
           for d in project["geometry"]["definitions"]):
        raise ValueError("This runtime keys appearance by filename; duplicate NIF filenames cannot share a pack.")
    target = next(m for m in model["meshes"] if m["block"] == target_block)
    if hide_liquid and cavity_block == target_block:
        raise ValueError("The render target cannot also be hidden.")
    definition = {"id": profile_id, "mesh_path": relative_path,
                  "shape": f"DynamicBottleLiquids:{project['pack']}-{profile_id}",
                  "vertices": target["vertices"], "indices": target["triangles"] * 3,
                  "matrix": result["matrix"], "rings": result["rings"]}
    project["geometry"]["definitions"].append(definition)
    if group is not None:project["appearance"]["groups"].setdefault(group, {})
    project["appearance"]["profiles"][profile_id] = {"group": group, "overrides": {}}
    project["sources"][profile_id] = {"path": model["path"], "sha256": model["sha256"],
                                     "cavity_block": cavity_block, "target_block": target_block,
                                     "hide_liquid": bool(hide_liquid), "audit": result["audit"]}
    if "preparation" in result:
        project["sources"][profile_id]["preparation"] = copy.deepcopy(result["preparation"])
    return definition


def validate_project(project):
    if project.get("format") != "dbl-author-project" or project.get("version") != 1:
        raise ValueError("Unsupported author project.")
    slug(project["pack"])
    appearance = project["appearance"]
    controls(appearance["defaults"])
    if set(appearance["defaults"]) != set(DEFAULTS):
        raise ValueError("Appearance defaults must supply all supported controls.")
    for name, values in appearance["groups"].items():
        slug(name); controls(values)
    ids, names = set(), set()
    for definition in project["geometry"]["definitions"]:
        if set(definition) != GEOMETRY_KEYS:
            raise ValueError("Geometry definitions contain only identity, transform and cavity data; appearance fields are forbidden.")
        profile_id = slug(definition["id"])
        name = PurePosixPath(mesh_path(definition["mesh_path"])).name.lower()
        if profile_id in ids or name in names:
            raise ValueError("Duplicate profile ID or mesh filename.")
        ids.add(profile_id); names.add(name)
        if definition["shape"] != f"DynamicBottleLiquids:{project['pack']}-{profile_id}":
            raise ValueError("Profile shape tag does not match its pack and ID.")
        for key, limit in (("vertices", 65535), ("indices", 3000000)):
            value = definition[key]
            if type(value) is not int or not 0 < value <= limit:
                raise ValueError(f"Invalid {key} count.")
        if definition["indices"] % 3:
            raise ValueError("Index count must describe complete triangles.")
        matrix = np.asarray(definition["matrix"], dtype=float)
        if matrix.shape != (16,) or not np.all(np.isfinite(matrix)):
            raise ValueError("Invalid geometry transform.")
        matrix = matrix.reshape(4, 4); axes = matrix[:3, :3]; gram = axes @ axes.T
        scale = math.sqrt(float(gram[0, 0]))
        if (not .0001 <= scale <= 1000 or np.max(np.abs(matrix)) > 100000 or
                not np.array_equal(matrix[3], [0, 0, 0, 1]) or
                not np.allclose(gram, np.eye(3) * gram[0, 0], rtol=.001, atol=1e-7)):
            raise ValueError("Geometry transforms must be affine with uniform scale.")
        rings = np.asarray(definition["rings"], dtype=float)
        if (rings.ndim != 2 or rings.shape[1] != 2 or not 2 <= len(rings) <= 16 or
                not np.all(np.isfinite(rings)) or np.any(np.diff(rings[:, 0]) <= 0) or
                np.any(rings[:, 1] <= 0) or np.max(np.abs(rings)) > 10000):
            raise ValueError("Invalid cavity rings.")
        profile = appearance["profiles"][profile_id]
        if set(profile) != {"group", "overrides"}:
            raise ValueError("An appearance profile contains group and overrides only.")
        effective_appearance(project, profile_id)
    if ids != set(appearance["profiles"]) or ids != set(project["sources"]):
        raise ValueError("Geometry, appearance and source profile IDs must agree.")


def save_project(project, folder):
    validate_project(project)
    folder = Path(folder)
    if folder.exists() and any(folder.iterdir()):
        raise ValueError("Choose a new or empty project folder; existing projects are never overwritten.")
    folder.mkdir(parents=True, exist_ok=True)
    pack = project["pack"]
    staged = copy.deepcopy(project)
    for profile_id, source in staged["sources"].items():
        raw = Path(source["path"]).read_bytes()
        if hashlib.sha256(raw).hexdigest() != source["sha256"]:
            raise ValueError("Source NIF changed since calculation; reopen and recalculate.")
        path = Path("sources") / f"{profile_id}.nif"
        (folder / path).parent.mkdir(exist_ok=True)
        (folder / path).write_bytes(raw)
        source["path"] = path.as_posix()
    files = {f"geometry/{pack}.geometry.json": staged.pop("geometry"),
             f"appearance/{pack}.appearance.json": staged.pop("appearance")}
    staged["geometry_file"], staged["appearance_file"] = files.keys()
    files["project.json"] = staged
    for filename, content in files.items():
        dest = folder / filename; dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(json.dumps(content, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    return folder / "project.json"


def load_project(folder):
    folder = Path(folder).resolve()
    def child(name):
        result = (folder / name).resolve()
        if not result.is_relative_to(folder):
            raise ValueError("Project file points outside the project folder.")
        return result
    project = json.loads((folder / "project.json").read_text(encoding="utf-8"))
    for key in ("geometry", "appearance"):
        project[key] = json.loads(child(project.pop(key + "_file")).read_text(encoding="utf-8"))
    for source in project["sources"].values():
        source["path"] = str(child(source["path"]))
    validate_project(project)
    return project


def appearance_rings(rings, thickness):
    """Clip the nominal polyline at the runtime's inset ends before shrinking it.

    End rings can be arbitrarily close to a topology transition. Moving just
    the end vertex across that transition reverses a segment, not the cavity.
    Interpolate at the new ends and omit the now-outside interior vertices.
    """
    rings=np.asarray(rings,dtype=float);delta=thickness-.16
    low,high=rings[0,0]+delta,rings[-1,0]-delta
    if low>=high:raise ValueError('Glass is too thick for this bottle. Lower Glass thickness in Appearance.')
    interior=rings[1:-1]
    heights=np.r_[low,interior[(interior[:,0]>low)&(interior[:,0]<high),0],high]
    radii=np.maximum(np.interp(heights,rings[:,0],rings[:,1])-delta,.05)
    return np.column_stack((heights,radii))


def export_patch(project, destination, progress=None, cancel=None):
    """Compile groups to current runtime's mesh overrides; never alter parent assets."""
    validate_project(project)
    if not project["geometry"]["definitions"]:
        raise ValueError("Add at least one calculated bottle before exporting.")
    destination = Path(destination)
    if destination.exists():
        raise ValueError("Choose a new ZIP filename; existing packages are never overwritten.")
    profiles, appearances, audit = [], {}, []
    with tempfile.TemporaryDirectory(prefix="dbl-author-") as temp:
        stage = Path(temp)
        for definition in project["geometry"]["definitions"]:
            if cancel and cancel():
                from concurrent.futures import CancelledError
                raise CancelledError()
            if progress:progress('Checking and preparing '+definition['id'])
            profile_id = definition["id"]; source = project["sources"][profile_id]
            source_path = Path(source["path"])
            raw = source_path.read_bytes()
            if hashlib.sha256(raw).hexdigest() != source["sha256"]:
                raise ValueError("Source NIF changed since calculation; reopen and recalculate.")
            model = load_mesh(source_path)
            target = next(m for m in model["meshes"] if m["block"] == source["target_block"])
            preparation = source.get("preparation")
            if not preparation and (target["shader"] != "BSEffectShaderProperty" or target["descriptor"] != 0x1b00000430205):
                raise ValueError("Patch export requires a prepared Effect-shader glass target with layout 0x1b00000430205. Geometry projects can still be saved.")
            if (target["vertices"], target["triangles"] * 3) != (definition["vertices"], definition["indices"]):
                raise ValueError("Geometry signature does not match source.")
            # The selected measurement volume must also fit the actual draw target.
            matrix = np.asarray(definition["matrix"]).reshape(4, 4)
            vertices = np.asarray(target["positions"]).reshape(-1, 3)
            vertices = vertices @ matrix[:3, :3].T + matrix[:3, 3]
            target_mesh = trimesh.Trimesh(vertices=vertices, faces=np.asarray(target["indices"]).reshape(-1, 3), process=False)
            rings = np.asarray(definition["rings"])
            appearance = effective_appearance(project, profile_id)
            # The current runtime shrinks the nominal cavity when thickness exceeds .16.
            runtime_rings = appearance_rings(rings,appearance['glass_thickness'])
            delta=appearance['glass_thickness']-.16
            export_rings=runtime_rings.copy();export_rings[:,1]+=delta
            export_rings[0,0]-=delta;export_rings[-1,0]+=delta
            for z in np.unique(np.r_[runtime_rings[:, 0], np.linspace(runtime_rings[0, 0], runtime_rings[-1, 0], 257)]):
                if cancel and cancel():
                    from concurrent.futures import CancelledError
                    raise CancelledError()
                allowed = section_radius(target_mesh, z)
                if np.interp(z, runtime_rings[:, 0], runtime_rings[:, 1]) > allowed + 1e-7:
                    raise ValueError("Calculated cavity escapes the glass target with this appearance thickness. Review target, clearance and thickness.")
            parts = list(parse(raw)); original = list(parts[5]); target_id = target["block"]
            if preparation:
                from .preparation import apply
                valid_blocks = {m['block'] for m in model['meshes']}
                if preparation['target_block'] != target_id or any(i not in valid_blocks for i in preparation['hidden_blocks']):
                    raise ValueError('Automatic preparation contains invalid shape references.')
                changed = apply(parts, target, preparation, definition['shape'])
            else:
                chunk = bytearray(parts[5][target_id])
                struct.pack_into("<I", chunk, 0, intern(parts[4], definition["shape"]))
                parts[5][target_id] = bytes(chunk)
                changed = {target_id}
            if not preparation and source["hide_liquid"]:
                liquid_id = source["cavity_block"]
                if liquid_id == target_id:
                    raise ValueError("Cannot hide the glass render target.")
                if not any(m["block"] == liquid_id for m in model["meshes"]):
                    raise ValueError("Liquid block is not a supported shape.")
                parts[5][liquid_id] = hide(parts[5][liquid_id]); changed.add(liquid_id)
            output = stage / definition["mesh_path"]
            save_nif(parts, output)
            verified = load_mesh(output)
            for old, new in zip(model["meshes"], verified["meshes"]):
                for key in ("positions", "indices", "normals", "uvs", "material", "descriptor", "local_transform"):
                    if preparation and old['block'] == target_id and key in ('material', 'descriptor'):
                        continue
                    if old[key] != new[key]:
                        raise ValueError(f"Export changed {key}; refusing the patch.")
            if any(old != new for i, (old, new) in enumerate(zip(original, parts[5])) if i not in changed):
                raise ValueError("Export changed unrelated NIF blocks.")
            row = {key: definition[key] for key in ("shape", "vertices", "indices", "matrix", "rings")}
            row['rings']=export_rings.tolist()
            row.update(mesh=PurePosixPath(definition["mesh_path"]).name, empty=False)
            profiles.append(row)
            appearances[row["mesh"]] = appearance
            audit.append({"id": profile_id, "source_sha256": source["sha256"],
                          "output_sha256": verified["sha256"], "preparation": preparation,
                          "hidden_liquid": bool(preparation['hidden_blocks']) if preparation else source["hide_liquid"],
                          **source["audit"]})
        root = stage / "F4SE/Plugins/DynamicBottleLiquids"
        files = {
            root / f"profiles/{project['pack']}.geometry.json": {"schema_version": 1, "profiles": profiles},
            root / f"drinks.d/{project['pack']}.appearance.json": {"schema_version": 1, "meshes": appearances},
            stage / "authoring" / f"{project['pack']}.appearance.json": project["appearance"],
            stage / "authoring" / "audit.json": audit,
        }
        for path, value in files.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n", encoding="utf-8")
        (stage / "README-author-patch.txt").write_text(
            "Requires the external-JSON development/VR runtime, NOT the fixed-profile 1.0 release.\n"
            "Install after DynamicBottleLiquids and the original mesh mod. Original materials/textures remain required.\n"
            "No DLL or shaders are supplied. Geometry and appearance are separate.\n"
            "Author groups are resolved to per-mesh appearance for the existing runtime; edit groups in the author project and re-export.\n"
            "Existing user tuning may override pack appearance. Restart the game after installation.\n"
            "Cavity fitting checks the selected shape, not bottle capacity or rendered material composition.\n"
            "Verify no opaque body/static-liquid layer obscures the effect. Test upright, tilted, inverted and shaking.\n"
            "Glass thickness is nominally 0.16 during measurement; changing it changes the runtime cavity.\n",
            encoding="utf-8")
        destination.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(destination, "x", zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(stage.rglob("*")):
                if path.is_file():
                    archive.write(path, path.relative_to(stage).as_posix())
    return destination
