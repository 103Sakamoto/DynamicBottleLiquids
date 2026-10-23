import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from author_tool.ui_common import application
from author_tool import core,preparation
from author_tool.workspace import Workspace
from author_tool.appearance_ui import AppearanceDialog,GroupsDialog
from author_tool.qt_app import AuthorWindow
from author_tool.screen_color import pixel_color,ScreenOverlay,DesktopSampler
from author_tool.color_ui import ColorDialog
from PySide6.QtCore import QPoint,QRect,Qt
from PySide6.QtGui import QImage,QColor
from PySide6.QtTest import QTest

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'experiments/des-drink-patches-v2/input/sunset/Meshes/DesSSSS/SSSSClassic/DesSSSSClassicBottle.nif'

class AppearanceWorkflowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.app=application()
    def project(self):
        p=core.new_project();p['appearance']['groups']['cola']={'opacity':.3}
        p['appearance']['profiles']={'one':{'group':'cola','overrides':{'opacity':.8}},'two':{'group':'cola','overrides':{}}};return p
    def test_bottle_scope_is_default_and_reset_immediately_inherits(self):
        original=self.project();dialog=AppearanceDialog(original)
        try:
            self.assertEqual(dialog.current_scope,('bottle','one'))
            self.assertFalse(hasattr(dialog,'scope'))
            dialog.reset_value('opacity');slider,value,source,_=dialog.rows['opacity']
            self.assertEqual(value.value(),30);self.assertEqual(slider.value(),3000);self.assertIn('cola',source.text())
            dialog.set_value('opacity',.55)
            self.assertIn('this bottle',source.text());self.assertEqual(dialog.project['appearance']['groups']['cola']['opacity'],.3)
            self.assertEqual(original['appearance']['profiles']['one']['overrides']['opacity'],.8)
            self.assertEqual(dialog.rows['glass_thickness'][0].value(),1600)
            self.assertEqual(dialog.rows['glass_thickness'][1].value(),.16)
        finally:dialog.reject()
    def test_membership_change_keeps_overrides_and_refreshes_inheritance(self):
        dialog=AppearanceDialog(self.project(),scope=('bottle','two'))
        try:
            dialog.membership.setCurrentIndex(0)
            self.assertEqual(dialog.values['opacity'],core.DEFAULTS['opacity'])
            self.assertIn('default appearance',dialog.rows['opacity'][2].text())
        finally:dialog.reject()
    def test_group_edit_reports_members_and_keeps_bottle_override(self):
        dialog=AppearanceDialog(self.project(),scope=('group','cola'))
        try:
            self.assertIn('2 bottles',dialog.impact.text());self.assertIn('opacity',dialog.impact.text())
            dialog.set_value('opacity',.4)
            self.assertEqual(core.effective_appearance(dialog.project,'one')['opacity'],.8)
            self.assertEqual(core.effective_appearance(dialog.project,'two')['opacity'],.4)
        finally:dialog.reject()
    def test_remove_group_preserves_members_appearance(self):
        dialog=GroupsDialog(self.project())
        try:
            dialog.list.setCurrentRow(list(dialog.project['appearance']['groups']).index('cola'));dialog.remove()
            self.assertIsNone(dialog.project['appearance']['profiles']['one']['group'])
            self.assertEqual(core.effective_appearance(dialog.project,'one')['opacity'],.8)
            self.assertEqual(core.effective_appearance(dialog.project,'two')['opacity'],.3)
        finally:dialog.reject()

@unittest.skipUnless(SOURCE.exists(),'Local bottle fixture unavailable')
class DocumentWorkflowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.app=application()
    def test_draft_save_normal_save_reopen_and_atomic_failure(self):
        doc=Workspace();key,_=doc.import_mesh(SOURCE);doc.update(key,{'name':'First'},'Rename')
        with tempfile.TemporaryDirectory() as folder:
            target=Path(folder)/'project';doc.save(target);self.assertTrue(doc.undo.isClean())
            self.assertTrue(Path(doc.item(key)['source_path']).is_relative_to(target))
            reopened=Workspace();reopened.open(target);self.assertTrue(reopened.item(key)['needs_calculation'])
            self.assertEqual(reopened.item(key)['name'],'First')
            doc.update(key,{'name':'Second'},'Rename');doc.save();reopened.open(target);self.assertEqual(reopened.item(key)['name'],'Second')
            old=(target/'project.json').read_bytes();doc.update(key,{'name':'Not committed'},'Rename')
            from author_tool.workspace import atomic_write
            def fail_manifest(path,data):
                if Path(path).name=='project.json':raise OSError('simulated disk failure')
                return atomic_write(path,data)
            with patch('author_tool.workspace.atomic_write',side_effect=fail_manifest):
                with self.assertRaises(OSError):doc.save()
            self.assertEqual((target/'project.json').read_bytes(),old);reopened.open(target);self.assertEqual(reopened.item(key)['name'],'Second')
    def test_remove_undo_restores_appearance_and_new_bottle_uses_defaults(self):
        doc=Workspace();key,_=doc.import_mesh(SOURCE)
        doc.change('Color',lambda s:s['appearance']['profiles'][key]['overrides'].update(liquid_color='#123456'))
        doc.remove(key);self.assertIsNone(doc.item(key));doc.undo.undo()
        self.assertEqual(doc.state['appearance']['profiles'][key]['overrides']['liquid_color'],'#123456')
        settings=copy.deepcopy(doc.item(key)['settings']);settings['trim']=.03;doc.update(key,{'settings':settings})
        other,_=doc.import_mesh(SOURCE);self.assertEqual(doc.item(other)['settings']['trim'],.005)
        self.assertIsNone(doc.state['appearance']['profiles'][other]['group'])
    def test_preparation_update_preserves_profile_and_blocks_stale_export(self):
        doc=Workspace();key,model=doc.import_mesh(SOURCE);doc.update(key,{'liquid':9,'selection_confirmed':True});entry=doc.item(key)
        selected=next(s for s in model['meshes'] if s['block']==entry['liquid']);result=preparation.calculate(model,selected)
        doc.update(key,{'result':result,'needs_calculation':False});doc.change('Opacity',lambda s:s['appearance']['profiles'][key]['overrides'].update(opacity=.4))
        doc.update(key,{'result':result,'needs_calculation':False},'Reprepare')
        self.assertEqual(core.effective_appearance(doc.project(),key)['opacity'],.4)
        settings=copy.deepcopy(entry['settings']);settings['center_offset']=[.1,0];doc.update(key,{'settings':settings},geometry=True)
        with self.assertRaisesRegex(ValueError,'prepare'):doc.project()
    def test_selected_row_restores_its_settings_and_manual_target_feedback(self):
        window=AuthorWindow()
        try:
            window.load_nif(SOURCE);window.shapes.setCurrentRow(next(i for i,s in enumerate(window.model['meshes']) if s['block']==9));first=window.current_id;window.offset_x.setValue(.25)
            window.load_nif(SOURCE);self.assertEqual(window.offset_x.value(),0)
            window.profiles.selectRow(0);self.assertEqual(window.current_id,first);self.assertEqual(window.offset_x.value(),.25)
            index=window.target.findData(9);window.target.setCurrentIndex(index)
            self.assertIn('Manual glass',window.detected.text());self.assertIn('Liquid',window.detected.text())
            self.assertFalse(window.export_button.isEnabled())
        finally:window.document.undo.setClean();window.close()
    def test_cancelled_calculation_does_not_install_result(self):
        window=AuthorWindow()
        try:
            window.load_nif(SOURCE);window.shapes.setCurrentRow(next(i for i,s in enumerate(window.model['meshes']) if s['block']==9));window.calculate();window.cancel_job()
            for _ in range(100):
                if window.future is None:break
                QTest.qWait(50)
            self.assertIsNone(window.future);self.assertTrue(window.current()['needs_calculation']);self.assertIsNone(window.result)
        finally:window.document.undo.setClean();window.close()

class DesktopColorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.app=application()
    def test_scaled_screen_and_negative_desktop_coordinates(self):
        image=QImage(400,200,QImage.Format.Format_RGB32);image.fill(QColor('#112233'));image.setPixelColor(200,100,QColor('#ABCDEF'))
        self.assertEqual(pixel_color(image,QRect(-200,40,200,100),QPoint(-100,90)).name(),'#abcdef')
        with self.assertRaises(ValueError):pixel_color(image,QRect(-200,40,200,100),QPoint(0,40))
    def test_overlay_click_samples_rgb_and_escape_cancels(self):
        image=QImage(200,100,QImage.Format.Format_RGB32);image.fill(QColor('#2468AC'))
        overlay=ScreenOverlay(QRect(20,20,200,100),image);picked=[];cancelled=[]
        overlay.picked.connect(lambda c:picked.append(c.name()));overlay.cancelled.connect(lambda:cancelled.append(True))
        try:
            overlay.show();self.app.processEvents();QTest.mouseClick(overlay,Qt.MouseButton.LeftButton,pos=QPoint(50,50))
            self.assertEqual(picked,['#2468ac']);QTest.keyClick(overlay,Qt.Key.Key_Escape);self.assertEqual(cancelled,[True])
        finally:overlay.close()
    def test_picker_has_no_texture_loader_and_cancel_preserves_color(self):
        dialog=ColorDialog('#123456')
        try:
            self.assertFalse(hasattr(dialog,'loadTexture'));dialog.hex.setText('ABCDEF');self.assertEqual(dialog.color.name(),'#abcdef')
            self.assertEqual(dialog.original.name(),'#123456');dialog.sampler.active=True;dialog.sampler.cancel()
            self.assertEqual(dialog.color.name(),'#abcdef');self.assertFalse(dialog.sampler.overlays)
        finally:dialog.reject()

if __name__=='__main__':unittest.main()
