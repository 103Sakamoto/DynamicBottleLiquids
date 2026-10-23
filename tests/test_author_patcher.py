import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
import zipfile
import numpy as np
from author_tool import core,preparation
from author_tool.workspace import Workspace
from author_tool.qt_app import application,AuthorWindow
from PySide6.QtCore import Qt,QPoint
from PySide6.QtTest import QTest

SOURCE=Path(r'E:\Games\Modding\F4mods\mods\Vintage Nukas - Powerful New Sodas\Meshes\WhaleOilMeshes\JasmineSodaSoda.nif')

class ThicknessTests(unittest.TestCase):
    def test_close_end_rings_are_clipped_not_reversed(self):
        rings=[[0,2],[.02,2],[5,2],[9.98,1],[10,1]]
        for thickness in (.16,.4,.6):
            actual=core.appearance_rings(rings,thickness)
            self.assertTrue(np.all(np.diff(actual[:,0])>0))
            self.assertAlmostEqual(actual[0,0],thickness-.16)
            self.assertAlmostEqual(actual[-1,0],10-thickness+.16)
        with self.assertRaisesRegex(ValueError,'too thick'):core.appearance_rings([[0,1],[.2,1]],.4)
    def test_lower_thickness_keeps_runtime_ring_limit(self):
        rings=np.column_stack((np.linspace(0,10,16),np.ones(16)*2))
        self.assertEqual(len(core.appearance_rings(rings,.01)),16)

@unittest.skipUnless(SOURCE.exists(),'Local original NIF fixture unavailable')
class PatcherTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.app=application()
    def test_unselected_import_large_list_and_separate_geometry(self):
        w=AuthorWindow()
        try:
            w.load_nif(SOURCE);w.show();self.app.processEvents()
            self.assertIsNone(w.current()['liquid']);self.assertEqual(w.shapes.currentRow(),-1)
            self.assertEqual(w.shapes.selectedItems(),[]);self.assertFalse(w.calculate_button.isEnabled())
            self.assertFalse(w.geometry_window.isVisible());self.assertGreaterEqual(w.profiles.viewport().height(),w.profiles.rowHeight(0)*3)
            self.assertFalse(hasattr(w,'pack'));self.assertFalse(hasattr(w,'mesh_path'));self.assertFalse(hasattr(w,'isolate'))
            item=w.shapes.item(1);QTest.mouseClick(w.shapes.viewport(),Qt.MouseButton.LeftButton,pos=w.shapes.visualItemRect(item).center())
            self.assertEqual(w.current()['liquid'],7)
            w.open_geometry();self.assertTrue(w.geometry_window.isVisible());self.assertTrue(w.canvas.isVisible())
            w.geometry_window.close();self.assertFalse(w.canvas.isVisible())
            w.guarded(lambda:(_ for _ in ()).throw(ValueError('Test failure')))
            self.assertIn('#B10E1C',w.status.styleSheet());self.assertIn('Test failure',w.status.text())
        finally:w.document.undo.setClean();w.close()
    def test_original_mesh_export_thickness_and_manual_removal(self):
        before=SOURCE.read_bytes();doc=Workspace();key,model=doc.import_mesh(SOURCE)
        selected=next(s for s in model['meshes'] if s['block']==7)
        result=preparation.calculate(model,selected)
        doc.update(key,{'liquid':7,'selection_confirmed':True,'result':result,'needs_calculation':False,'removed_shapes':[10]})
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            for thickness in (.16,.4,.6):
                doc.state['appearance']['defaults']['glass_thickness']=thickness
                project=doc.project();out=core.export_patch(project,root/f'{thickness}.zip')
                with zipfile.ZipFile(out) as z:
                    data=json.loads(z.read('F4SE/Plugins/DynamicBottleLiquids/profiles/'+project['pack']+'.geometry.json'))
                    nominal=np.asarray(data['profiles'][0]['rings']);runtime=nominal.copy();delta=thickness-.16
                    runtime[:,1]=np.maximum(runtime[:,1]-delta,.05);runtime[0,0]+=delta;runtime[-1,0]-=delta
                    np.testing.assert_allclose(runtime,core.appearance_rings(result['rings'],thickness))
                    audit=json.loads(z.read('authoring/audit.json'))
                    self.assertIn(10,audit[0]['preparation']['hidden_blocks'])
                    self.assertNotIn('glass_thickness',data['profiles'][0])
            doc.save(root/'project');opened=Workspace();opened.open(root/'project')
            self.assertEqual(opened.item(key)['removed_shapes'],[10])
            self.assertEqual(opened.item(key)['liquid'],7)
            doc.update(key,{'removed_shapes':[result['preparation']['target_block']]})
            with self.assertRaisesRegex(ValueError,'cannot also be removed'):doc.project()
        self.assertEqual(SOURCE.read_bytes(),before)
