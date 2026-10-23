import copy
from pathlib import Path
import unittest
from author_tool.qt_app import application,AuthorWindow
from author_tool.appearance_ui import AppearanceDialog
from author_tool.ui_common import HelpButton
from author_tool import core
from PySide6.QtCore import Qt,QPoint
from PySide6.QtTest import QTest

SOURCE=Path(__file__).resolve().parents[1]/'experiments/des-drink-patches-v2/input/sunset/Meshes/DesSSSS/SSSSClassic/DesSSSSClassicBottle.nif'

class ParameterGroupTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.app=application()
    def test_cavity_uses_equal_units_after_resize(self):
        from matplotlib.figure import Figure
        from matplotlib.backends.backend_agg import FigureCanvasAgg
        from author_tool.preview import draw_cavity
        figure=Figure();canvas=FigureCanvasAgg(figure)
        for size in ((4,7),(8,4)):
            figure.set_size_inches(*size)
            draw_cavity(figure,{'rings':[[0,3],[10,4],[20,1]]})
            canvas.draw();ax=figure.axes[0]
            origin,x,y=ax.transData.transform([(0,0),(1,0),(0,1)])
            self.assertAlmostEqual(x[0]-origin[0],y[1]-origin[1],places=6)
    def test_group_actions_change_only_one_parameter_and_cancel_is_transactional(self):
        p=core.new_project();p['appearance']['groups']['cola']={'opacity':.3,'liquid_level':.8}
        p['appearance']['profiles']={'one':{'group':'cola','overrides':{'opacity':.6,'liquid_level':.4}},'two':{'group':'cola','overrides':{'opacity':.9}}}
        original=copy.deepcopy(p);d=AppearanceDialog(p,scope=('bottle','one'))
        try:
            self.assertEqual(set(d.group_actions),set(core.DEFAULTS))
            d.save_to_group('opacity')
            self.assertEqual(d.project['appearance']['groups']['cola'],{'opacity':.6,'liquid_level':.8})
            self.assertEqual(d.own(),{'liquid_level':.4})
            self.assertEqual(core.effective_appearance(d.project,'two')['opacity'],.9)
            d.apply_from_group('liquid_level');self.assertEqual(d.values['liquid_level'],.8)
            d.set_value('liquid_color','#123456');d.save_to_group('liquid_color')
            self.assertEqual(d.project['appearance']['groups']['cola']['liquid_color'],'#123456')
            d.reject();self.assertEqual(p,original)
        finally:d.close()
    def test_click_help_opens_popup_without_hover_tooltip(self):
        help=HelpButton('Explain this setting');help.show()
        try:
            QTest.mouseClick(help,Qt.MouseButton.LeftButton);self.assertTrue(help.popup.isVisible());self.assertEqual(help.toolTip(),'')
            QTest.mouseClick(help,Qt.MouseButton.LeftButton);self.assertFalse(help.popup.isVisible())
            QTest.mouseClick(help,Qt.MouseButton.LeftButton);self.assertTrue(help.popup.isVisible());help.popup.close()
        finally:help.close()

@unittest.skipUnless(SOURCE.exists(),'Local NIF fixture unavailable')
class MainLayoutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.app=application()
    def test_row_remove_and_undo(self):
        from qfluentwidgets import PushButton
        w=AuthorWindow()
        try:
            w.load_nif(SOURCE);key=w.current_id
            buttons=w.profiles.cellWidget(0,3).findChildren(PushButton)
            self.assertEqual([b.text() for b in buttons],['Appearance…','Remove'])
            buttons[1].click();self.assertEqual(len(w.document.state['items']),0)
            w.document.undo.undo();self.assertEqual(w.current()['id'],key)
        finally:w.document.undo.setClean();w.close()
    def test_real_group_dropdown_select_import_reassign_undo(self):
        w=AuthorWindow();w.show();QTest.qWait(200)
        try:
            self.assertTrue(w.group.isEnabled());self.assertGreater(w.group.count(),1)
            # Drive the real popup view, not just its signal handler.
            for group in ('cola','beer'):
                QTest.mouseClick(w.group,Qt.MouseButton.LeftButton);QTest.qWait(200);index=w.group.model().index(w.group.findData(group),0)
                QTest.mouseMove(w.group.view().viewport(),QPoint(1,1));QTest.qWait(50)
                QTest.mouseMove(w.group.view().viewport(),w.group.view().visualRect(index).center());QTest.qWait(300)
                QTest.mouseClick(w.group.view().viewport(),Qt.MouseButton.LeftButton,pos=w.group.view().visualRect(index).center())
                self.assertEqual(w.group.currentData(),group)
                if group=='cola':
                    w.load_nif(SOURCE);key=w.current_id
                    self.assertEqual(w.project['appearance']['profiles'][key]['group'],'cola')
                else:self.assertEqual(w.project['appearance']['profiles'][key]['group'],'beer')
            w.document.undo.undo();self.assertEqual(w.group.currentData(),'cola')
        finally:w.document.undo.setClean();w.close()
    def test_popup_keeps_cavity_visible_and_saves_or_cancels(self):
        w=AuthorWindow();w.show();QTest.qWait(200)
        try:
            w.load_nif(SOURCE);key=w.current_id;old=copy.deepcopy(w.project['appearance'])
            w.open_bottle_appearance(key);d=w.appearance_dialog;self.app.processEvents()
            self.assertFalse(d.isModal());self.assertTrue(w.cavity_canvas.isVisible())
            self.assertGreater(w.cavity_canvas.mapToGlobal(QPoint(0,0)).x(),d.frameGeometry().right())
            d.set_value('opacity',.25);self.assertEqual(w.preview_appearance['profiles'][key]['overrides']['opacity'],.25)
            self.assertEqual(w.project['appearance'],old)
            d.reject();self.assertIsNone(w.preview_appearance);self.assertEqual(w.project['appearance'],old)
            w.open_bottle_appearance(key);w.appearance_dialog.set_value('opacity',.35);w.appearance_dialog.accept()
            self.assertEqual(w.project['appearance']['profiles'][key]['overrides']['opacity'],.35)
            w.document.undo.undo();self.assertEqual(w.project['appearance'],old)
        finally:
            if w.appearance_dialog:w.appearance_dialog.reject()
            w.document.undo.setClean();w.close()



