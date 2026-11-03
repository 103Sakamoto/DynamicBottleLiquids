"""Raised-bottom regressions; reporter NIFs remain optional, local-only fixtures."""
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

import numpy as np
import trimesh

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from author_tool import core, preparation


def shape(mesh):
    return dict(block=1, name='Glass', shader='BSEffectShaderProperty', material='',
                positions=mesh.vertices.reshape(-1).tolist(),
                indices=mesh.faces.reshape(-1).tolist(), world_matrix=np.eye(4).tolist())


def punt(apex=.4):
    return trimesh.creation.revolve(np.array([[0., apex], [2., 0.], [2., 10.], [0., 10.]]), sections=32)


def calculate(mesh, **kwargs):
    s = shape(mesh)
    return preparation.calculate({'meshes': [s]}, s, axis='+Z', **kwargs)


class BottomTests(unittest.TestCase):
    def test_closed_punt_lifts_only_floor_and_preserves_clearance(self):
        mesh = punt()
        self.assertTrue(mesh.is_watertight)
        result = calculate(mesh)
        rings = np.asarray(result['rings'])
        self.assertGreater(rings[0, 0], .4)
        self.assertLessEqual(rings[0, 0], .5)
        self.assertAlmostEqual(rings[-1, 0], 9.95)
        self.assertTrue(result['audit']['automatic_bottom_trim'])
        self.assertLessEqual(result['audit']['bottom_trim_fraction'], .05)
        self.assertTrue(np.all(rings[:, 1] > .05))
        for z in np.linspace(rings[0, 0], rings[-1, 0], 1001):
            self.assertLessEqual(np.interp(z, rings[:, 0], rings[:, 1]) + .16,
                                 core.section_radius(mesh, z) + 1e-8)

    def test_auto_trim_disabled_retains_clearance_failure(self):
        with self.assertRaisesRegex(ValueError, 'Clearance'):
            calculate(punt(), auto_trim=False)

    def test_deep_punt_is_not_silently_removed(self):
        with self.assertRaisesRegex(ValueError, 'Clearance'):
            calculate(punt(.8))

    def test_punt_at_budget_boundary_requires_apex_margin(self):
        with self.assertRaisesRegex(ValueError, 'Clearance'):
            calculate(punt(.5))

    def test_flat_bottom_is_unchanged(self):
        result = calculate(punt(0))
        self.assertAlmostEqual(result['rings'][0][0], .05)
        self.assertAlmostEqual(result['rings'][-1][0], 9.95)

    def test_open_side_still_fails_even_with_punt(self):
        mesh = punt()
        # Remove a triangle in the main wall, not a cap outside the fitted range.
        index = next(i for i, tri in enumerate(mesh.triangles)
                     if np.ptp(tri[:, 2]) > 9)
        mesh.update_faces(np.arange(len(mesh.faces)) != index)
        with self.assertRaises(core.SectionError):
            calculate(mesh)

    def test_overlapping_side_still_fails_even_with_punt(self):
        mesh = punt()
        index = next(i for i, tri in enumerate(mesh.triangles)
                     if np.ptp(tri[:, 2]) > 9)
        duplicated = trimesh.Trimesh(mesh.vertices, np.vstack([mesh.faces, mesh.faces[index]]), process=False)
        with self.assertRaises(core.SectionError):
            calculate(duplicated)

    def test_narrow_entire_bottle_is_not_fixed_by_trimming(self):
        mesh = trimesh.creation.cylinder(radius=.20, height=10, sections=32)
        with self.assertRaisesRegex(ValueError, 'Clearance'):
            calculate(mesh)

    def test_narrow_top_is_not_treated_as_bottom(self):
        mesh = punt()
        mesh.vertices[:, 2] = 10 - mesh.vertices[:, 2]
        mesh.invert()
        with self.assertRaisesRegex(ValueError, 'Clearance'):
            calculate(mesh)

    def test_narrow_middle_is_not_trimmed(self):
        mesh = trimesh.creation.revolve(np.array([
            [0., .4], [2., 0.], [2., 4.], [.20, 5.],
            [2., 6.], [2., 10.], [0., 10.]]), sections=32)
        with self.assertRaisesRegex(ValueError, 'Clearance'):
            calculate(mesh)

    def test_bottom_trim_validation(self):
        s = shape(punt(0))
        for value in (-.01, .001, .25, float('nan'), float('inf')):
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, 'Bottom trim'):
                core.fit_cavity(s, s, bottom_trim=value)


FIXTURES = ROOT / 'experiments/issue-1-bottom/input'


@unittest.skipUnless((FIXTURES/'item_Kvass.nif').exists(), 'Reporter fixtures not installed')
class ReporterBottomTests(unittest.TestCase):
    def fit(self, filename, **kwargs):
        model = core.load_mesh(FIXTURES/filename)
        target = model['meshes'][0]
        # The supplied archive has no external BGSM. Isolate geometry only;
        # do not bypass production material validation or export any asset.
        with patch.object(preparation, 'plan', return_value=({}, target)):
            return preparation.calculate(model, target, target=target, axis='+Z', **kwargs)

    def test_kvass_punt(self):
        with self.assertRaisesRegex(ValueError, 'Clearance'):
            self.fit('item_Kvass.nif', auto_trim=False)
        result = self.fit('item_Kvass.nif')
        self.assertGreater(result['rings'][0][0], .9485)
        self.assertLessEqual(result['audit']['bottom_trim_fraction'], .05)
        self.assertAlmostEqual(result['rings'][-1][0], 23.22509765625*.995)
        self.assertGreaterEqual(result['audit']['minimum_measured_clearance'], .16)
        target = core.load_mesh(FIXTURES/'item_Kvass.nif')['meshes'][0]
        world = core.points_world(target)
        transform = np.asarray(result['canonical_matrix'])
        vertices = world @ transform[:3, :3].T + transform[:3, 3]
        mesh = trimesh.Trimesh(vertices, np.asarray(target['indices']).reshape(-1, 3), process=False)
        rings = np.asarray(result['rings'])
        for z in np.linspace(rings[0, 0], rings[-1, 0], 1001):
            self.assertLessEqual(np.interp(z, rings[:, 0], rings[:, 1]) + .16,
                                 core.section_radius(mesh, z) + 1e-8)

    def test_other_reporter_bodies_still_reject_open_sections(self):
        for filename in ('item_vodka.nif', 'item_Whiskey.nif', 'item_moonshine.nif'):
            with self.subTest(filename=filename), self.assertRaises(core.SectionError):
                self.fit(filename)


if __name__ == '__main__':
    unittest.main()
