import copy
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zipfile

import numpy as np
import trimesh

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from author_tool import core
from nif_document import parse, save, object_refs


def shape(mesh, transform=None):
    return {"positions": mesh.vertices.reshape(-1).tolist(), "indices": mesh.faces.reshape(-1).tolist(),
            "world_matrix": (np.eye(4) if transform is None else transform).tolist()}


class CavityTests(unittest.TestCase):
    def test_cylinder_fit_is_inside_polygon_not_circumscribed_radius(self):
        mesh = trimesh.creation.cylinder(radius=2, height=10, sections=32)
        source = shape(mesh)
        fit = core.fit_cavity(source, source, clearance=.1)
        expected = 2 * np.cos(np.pi / 32) - .1
        for _, radius in fit["rings"]:
            self.assertLessEqual(radius, expected)
            self.assertAlmostEqual(radius, expected, places=3)
        self.assertGreater(fit["audit"]["checked_sections"], 500)

    def test_relative_transform_maps_target_into_selected_cavity_frame(self):
        mesh = trimesh.creation.cylinder(radius=2, height=10, sections=24)
        source_transform = np.eye(4); source_transform[:3, 3] = [6, -4, 8]
        target_transform = np.eye(4); target_transform[:3, :3] *= 2
        target_transform[:3, 3] = [-2, 1, 3]
        result = core.fit_cavity(shape(mesh, source_transform), shape(mesh, target_transform))
        mapping = np.array(result["matrix"]).reshape(4, 4)
        center_world = np.r_[source_transform[:3, 3], 1]
        target_point = np.linalg.inv(target_transform) @ center_world
        np.testing.assert_allclose(mapping @ target_point, [0, 0, 5, 1], atol=1e-7)

    def test_open_side_and_off_center_shape_are_rejected(self):
        mesh = trimesh.creation.cylinder(radius=2, height=10, sections=24)
        open_mesh = trimesh.Trimesh(vertices=mesh.vertices, faces=mesh.faces[np.arange(len(mesh.faces)) != 1], process=False)
        with self.assertRaises(ValueError):
            core.fit_cavity(shape(open_mesh), shape(open_mesh))
        with self.assertRaisesRegex(ValueError, "outside"):
            core.fit_cavity(shape(mesh), shape(mesh), center_offset=(4., 0.))

    def test_nonuniform_target_transform_is_rejected(self):
        mesh = trimesh.creation.cylinder(radius=2, height=10, sections=24)
        transform = np.eye(4); transform[0, 0] = 2
        with self.assertRaisesRegex(ValueError, "nonuniform"):
            core.fit_cavity(shape(mesh), shape(mesh, transform))

    def test_neck_transition_fit_remains_conservative(self):
        # Ring mesh with an abrupt shoulder; no scene or external asset dependency.
        angles = np.arange(32) * 2 * np.pi / 32
        levels = [(0, 2), (5, 2), (6, 1), (10, 1)]
        vertices = np.array([[r*np.cos(a), r*np.sin(a), z] for z, r in levels for a in angles])
        faces = []
        for ring in range(3):
            for i in range(32):
                a = ring*32+i; b = ring*32+(i+1)%32; c = b+32; d = a+32
                faces.extend([[a,b,c],[a,c,d]])
        mesh = trimesh.Trimesh(vertices=vertices, faces=faces, process=False)
        fit = core.fit_cavity(shape(mesh), shape(mesh), clearance=.05)
        rings = np.array(fit["rings"])
        for z in np.linspace(.05, 9.95, 100):
            allowed = core.section_radius(mesh, z) - .05
            self.assertLessEqual(np.interp(z, rings[:,0], rings[:,1]), allowed + 1e-7)


class ProjectTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Existing fixture source is a real, prepared glass bottle from the pending test package.
        matches = list((ROOT / "experiments/vr-community-integration/package/Meshes").rglob("BottleRum01.nif"))
        if not matches:
            raise unittest.SkipTest("Local prepared rum fixture unavailable")
        cls.source = matches[0]
        cls.model = core.load_mesh(cls.source)
        cls.target = next(m for m in cls.model["meshes"] if m["name"].startswith("DynamicBottleLiquids:"))
        cls.fit = core.fit_cavity(cls.target, cls.target)

    def project(self):
        project = core.new_project("author-test")
        core.add_profile(project, self.model, self.target["block"], self.target["block"], self.fit,
                         "rum", "Meshes/props/BottleRum01.nif", "custom-spirits")
        return project

    def test_group_inheritance_and_per_bottle_reset(self):
        project = self.project(); appearance = project["appearance"]
        appearance["groups"]["custom-spirits"] = {"opacity": .3, "liquid_color": "#112233"}
        appearance["profiles"]["rum"]["overrides"] = {"opacity": .8}
        self.assertEqual(core.effective_appearance(project, "rum")["opacity"], .8)
        self.assertEqual(core.effective_appearance(project, "rum")["liquid_color"], "#112233")
        appearance["profiles"]["rum"]["overrides"] = {}
        self.assertEqual(core.effective_appearance(project, "rum")["opacity"], .3)
        self.assertEqual(core.effective_appearance(project, "rum")["glass_thickness"], .16)

    def test_portable_project_roundtrip_and_geometry_rejects_appearance(self):
        project = self.project()
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp) / "project"
            core.save_project(project, folder)
            loaded = core.load_project(folder)
            self.assertEqual(loaded["geometry"], project["geometry"])
            self.assertEqual(loaded["appearance"], project["appearance"])
            self.assertTrue(Path(loaded["sources"]["rum"]["path"]).is_relative_to(folder))
            loaded["geometry"]["definitions"][0]["controls"] = {"opacity": .4}
            with self.assertRaisesRegex(ValueError, "forbidden"):
                core.validate_project(loaded)

    def test_patch_export_keeps_source_and_non_target_blocks_exact(self):
        before = self.source.read_bytes(); project = self.project()
        project["appearance"]["groups"]["custom-spirits"] = {"opacity": .3}
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "patch.zip"
            core.export_patch(project, output)
            with zipfile.ZipFile(output) as archive:
                root = "F4SE/Plugins/DynamicBottleLiquids/"
                geometry = json.loads(archive.read(root + "profiles/author-test.geometry.json"))
                appearance = json.loads(archive.read(root + "drinks.d/author-test.appearance.json"))
                self.assertNotIn("controls", geometry["profiles"][0])
                self.assertEqual(appearance["meshes"]["BottleRum01.nif"]["opacity"], .3)
                self.assertFalse(any(n.endswith((".dll", ".cso")) for n in archive.namelist()))
                after = archive.read("Meshes/props/BottleRum01.nif")
            old = parse(before); new = parse(after)
            for i, (a,b) in enumerate(zip(old[5], new[5])):
                self.assertEqual(a[4:] if i == self.target["block"] else a,
                                 b[4:] if i == self.target["block"] else b)
            self.assertEqual(before, self.source.read_bytes())
            with self.assertRaisesRegex(ValueError, "never overwritten"):
                core.export_patch(project, output)

    def test_stale_source_and_path_escape_rejected(self):
        project = self.project()
        project["sources"]["rum"]["sha256"] = "0" * 64
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "patch.zip"
            with self.assertRaisesRegex(ValueError, "changed"):
                core.export_patch(project, output)
            self.assertFalse(output.exists())
        for path in ("../bad.nif", "Meshes/../../bad.nif", "C:/bad.nif", "Meshes/../bad.nif"):
            with self.assertRaises(ValueError):
                core.mesh_path(path)

    def test_nif_shared_helpers_roundtrip(self):
        for filename in ("NukaColaBottleFull.nif", "NukaColaBottleQuantum.nif"):
            raw = (ROOT / filename).read_bytes()
            with tempfile.TemporaryDirectory() as temp:
                output = Path(temp) / filename
                save(parse(raw), output)
                self.assertEqual(raw, output.read_bytes())


class ControlsTests(unittest.TestCase):
    def test_step_and_color_validation(self):
        self.assertEqual(core.snapped(.46, "opacity"), .45)
        self.assertEqual(core.snapped(.04, "opacity"), .05)
        self.assertEqual(core.display_value(.45, "opacity"), 45)
        self.assertEqual(core.stored_value(45, "opacity"), .45)
        self.assertEqual(core.display_value(.16, "glass_thickness"), .16)
        self.assertEqual(core.snapped(100, "glass_opacity"), .95)
        for value in (float("nan"), float("inf"), True, -1, 2):
            with self.assertRaises(ValueError):
                core.controls({"opacity": value})
        for value in ("112233", "#GGGGGG", "#123"):
            with self.assertRaises(ValueError):
                core.controls({"liquid_color": value})


if __name__ == "__main__":
    unittest.main()
