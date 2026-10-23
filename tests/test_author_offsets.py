import unittest
from pathlib import Path
import numpy as np
import trimesh
from author_tool import core, preparation
from author_tool.qt_app import application, AuthorWindow
from PySide6.QtCore import Qt
from PySide6.QtTest import QTest


class OffsetInputTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.app=application()

    def test_visible_editor_accepts_typed_decimal_and_arrow_steps(self):
        window=AuthorWindow()
        try:
            fixture=Path(__file__).resolve().parents[1]/'experiments/des-drink-patches-v2/input/sunset/Meshes/DesSSSS/SSSSClassic/DesSSSSClassicBottle.nif'
            if not fixture.exists():self.skipTest('Local bottle fixture unavailable')
            window.load_nif(fixture)
            window.show();window.open_geometry();self.app.processEvents()
            for width in (1040,1360):
                window.geometry_window.resize(width,940);self.app.processEvents()
                for box in (window.clearance,window.trim,window.offset_x,window.offset_y):
                    self.assertGreaterEqual(box.lineEdit().width(),60,'Numeric editor has no usable typing area')
                    editor=box.lineEdit();QTest.mouseClick(editor,Qt.MouseButton.LeftButton)
                    QTest.keyClick(editor,Qt.Key.Key_A,Qt.KeyboardModifier.ControlModifier)
                    QTest.keyClicks(editor,'0.25');QTest.keyClick(editor,Qt.Key.Key_Return)
                    self.assertAlmostEqual(box.value(),.25)
                    QTest.keyClick(editor,Qt.Key.Key_Up);self.assertAlmostEqual(box.value(),.25+box.singleStep())
                editor=window.offset_x.lineEdit();editor.selectAll();QTest.keyClicks(editor,'-0.125')
                QTest.keyClick(editor,Qt.Key.Key_Return);self.assertAlmostEqual(window.offset_x.value(),-.125)
        finally:window.document.undo.setClean();window.close()


class HollowWallTests(unittest.TestCase):
    def test_small_liquid_bound_difference_does_not_invert_bottle(self):
        mesh=trimesh.creation.cylinder(radius=2,height=10,sections=32)
        glass=dict(block=1,name='Glass',shader='BSEffectShaderProperty',material='',
                   positions=mesh.vertices.reshape(-1).tolist(),indices=mesh.faces.reshape(-1).tolist(),
                   world_matrix=np.eye(4).tolist())
        liquid=dict(glass,block=2,name='Liquid',positions=(mesh.vertices+[0,0,.6]).reshape(-1).tolist())
        result=preparation.calculate({'meshes':[glass,liquid]},liquid,target=glass)
        self.assertEqual(result['audit']['axis'],'+Z')

    def broken_wall(self, broken_ring):
        angles=np.arange(32)*2*np.pi/32
        vertices=np.array([[2*np.cos(a),2*np.sin(a),z] for z in (0,4,6,9.8,10) for a in angles])
        faces=[]
        for ring in range(4):
            for i in range(32):
                a=ring*32+i;b=ring*32+(i+1)%32;c=b+32;d=a+32
                if (ring,i)!=(broken_ring,0):faces.extend([[a,b,c],[a,c,d]])
        return dict(block=1,name='Glass',shader='BSEffectShaderProperty',material='',
                    positions=vertices.reshape(-1).tolist(),indices=np.array(faces).reshape(-1).tolist(),
                    world_matrix=np.eye(4).tolist())

    def test_only_small_end_seams_can_be_trimmed_automatically(self):
        shape=self.broken_wall(3);model={'meshes':[shape]}
        result=preparation.calculate(model,shape)
        self.assertTrue(result['audit']['automatic_end_trim'])
        self.assertGreaterEqual(result['audit']['end_trim_fraction'],.02)
        self.assertLessEqual(result['audit']['end_trim_fraction'],.05)
        with self.assertRaises(core.SectionError):preparation.calculate(model,shape,auto_trim=False)
        shape=self.broken_wall(1)
        with self.assertRaises(core.SectionError):preparation.calculate({'meshes':[shape]},shape)

    def test_nested_wall_sections_contain_cavity_axis(self):
        mesh=trimesh.util.concatenate([trimesh.creation.cylinder(radius=3,height=10,sections=32),
                                       trimesh.creation.cylinder(radius=2.5,height=10,sections=32)])
        self.assertAlmostEqual(core.section_radius(mesh,0),2.5*np.cos(np.pi/32),places=6)

    def test_offset_outside_all_contours_is_still_rejected(self):
        mesh=trimesh.creation.cylinder(radius=2,height=10,sections=32);mesh.apply_translation([5,0,0])
        with self.assertRaisesRegex(ValueError,'outside'):core.section_radius(mesh,0)

    def test_selected_glass_does_not_switch_to_enclosing_label(self):
        mesh=trimesh.creation.cylinder(radius=2,height=10,sections=32)
        def shape(block,name,scale):
            return dict(block=block,name=name,shader='BSEffectShaderProperty',positions=(mesh.vertices*scale).reshape(-1).tolist(),world_matrix=np.eye(4).tolist())
        glass=shape(1,'Glass',1);label=shape(2,'lablee',1.01)
        self.assertEqual(preparation.detect_target({'meshes':[glass,label]},glass)['block'],1)


if __name__=='__main__':unittest.main()
