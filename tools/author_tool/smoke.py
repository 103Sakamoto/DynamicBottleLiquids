"""Exercise the actual document UI and export in source and frozen builds."""
import hashlib
import json
from pathlib import Path
import tempfile
import time
import sys
from PySide6.QtCore import Qt,QPoint,QRect
from PySide6.QtGui import QImage,QColor
from PySide6.QtTest import QTest
from .qt_app import application,AuthorWindow,ExportReview
from .appearance_ui import AppearanceDialog
from .workspace import Workspace
from .color_ui import ColorDialog
from .screen_color import ScreenOverlay,pixel_color
from .ui_common import VERSION
from . import core

def run(nif,report,liquid=None):
    report=Path(report).resolve();report.parent.mkdir(parents=True,exist_ok=True)
    app=application();window=AuthorWindow();color=None
    try:
        if getattr(sys,'frozen',False):
            import tzdata
            from zoneinfo import ZoneInfo
            from importlib.resources import files
            assert 'author-resources.zip' in tzdata.__file__
            assert not (Path(sys._MEIPASS)/'tzdata').exists()
            with files('tzdata.zoneinfo').joinpath('America/New_York').open('rb') as resource:
                assert ZoneInfo.from_file(resource).key is None
            assert ZoneInfo('Atlantic/Reykjavik') is not None
        before=hashlib.sha256(Path(nif).read_bytes()).hexdigest();window.load_nif(nif);key=window.current_id
        if liquid is not None:
            row=next(i for i,s in enumerate(window.model['meshes']) if s['block']==liquid);window.shapes.setCurrentRow(row)
        assert window.current()['liquid'] is not None,'Select a liquid block using --liquid'
        window.show();assert window.shapes.currentRow()>=0;assert not window.geometry_window.isVisible();window.open_geometry();app.processEvents()
        for box in (window.offset_x,window.offset_y):
            editor=box.lineEdit();assert editor.width()>=60;QTest.mouseClick(editor,Qt.MouseButton.LeftButton);editor.selectAll()
            QTest.keyClicks(editor,'-0.125');QTest.keyClick(editor,Qt.Key.Key_Return);assert box.value()==-.125
            editor.selectAll();QTest.keyClicks(editor,'0.0');QTest.keyClick(editor,Qt.Key.Key_Return);assert box.value()==0
        window.calculate()
        for _ in range(600):
            if window.future is None:break
            app.processEvents();time.sleep(.05)
        assert window.result is not None,window.status.text()
        assert window.trim.value()==.5,'Automatic trim must not replace the requested value'
        assert not window.current()['needs_calculation']
        dialog=AppearanceDialog(window.project,window,('bottle',key),window.names())
        dialog.set_value('opacity',.4);assert 'this bottle' in dialog.rows['opacity'][2].text()
        dialog.reset_value('opacity');assert dialog.rows['opacity'][1].value()==60
        dialog.set_value('opacity',.4);window.commit_appearance(dialog.project['appearance'],'Edit bottle appearance');dialog.accept()
        assert core.effective_appearance(window.project,key)['opacity']==.4
        window.geometry_window.close();window.open_bottle_appearance(key);app.processEvents()
        popup=window.appearance_dialog
        assert not popup.isModal() and window.cavity_canvas.isVisible()
        assert window.cavity_canvas.mapToGlobal(QPoint(0,0)).x()>popup.frameGeometry().right()
        assert set(popup.group_actions)==set(core.DEFAULTS)
        popup.set_value('opacity',.25);popup.reject()
        assert core.effective_appearance(window.project,key)['opacity']==.4
        with tempfile.TemporaryDirectory(dir=report.parent,prefix='workflow-smoke-') as temp:
            folder=Path(temp)/'project';window.document.save(folder);window.document.update(key,{'removed_shapes':[next(s['block'] for s in window.model['meshes'] if s['block'] not in (window.current()['liquid'],window.result['preparation']['target_block']))]},'Choose removal');window.document.save()
            restored=Workspace();restored.open(folder);assert len(restored.item(key)['removed_shapes'])==1
            window.document.remove(key);window.document.undo.undo();assert window.current()['id']==key
            project=restored.project();zip_path=core.export_patch(project,Path(temp)/'patch.zip')
            review=ExportReview(zip_path,project,window);review.show();app.processEvents();review.grab().save(str(report.with_name(report.stem+'-export.png')));review.reject()
            color=ColorDialog('#123456',window);color.show();app.processEvents();color.hex.setText('12AB34');assert color.color.name()=='#12ab34'
            assert '#12ab34' in color.swatch.styleSheet();assert not hasattr(color,'loadTexture')
            image=QImage(400,200,QImage.Format.Format_RGB32);image.fill(QColor('#FF8000'))
            assert pixel_color(image,QRect(-200,0,200,100),QPoint(-100,50)).name()=='#ff8000'
            overlay=ScreenOverlay(QRect(20,20,200,100),image);overlay.picked.connect(color.setColor);overlay.show();app.processEvents()
            QTest.mouseClick(overlay,Qt.MouseButton.LeftButton,pos=QPoint(50,50));assert color.color.name()=='#ff8000';overlay.close()
            color.grab().save(str(report.with_name(report.stem+'-color.png')));color.reject();color=None
            appearance=AppearanceDialog(window.project,window,('bottle',key),window.names());appearance.show();app.processEvents()
            appearance.grab().save(str(report.with_name(report.stem+'-appearance.png')));appearance.reject()
        window.geometry_window.grab().save(str(report.with_name(report.stem+'-geometry.png')));window.geometry_window.close();app.processEvents();QTest.qWait(150);window.grab().save(str(report.with_suffix('.png')))
        assert hashlib.sha256(Path(nif).read_bytes()).hexdigest()==before
        result={'passed':True,'version':VERSION,'source_sha256':before,'rings':len(window.result['rings']),
                'checks':['draft import','typed decimal input','async preparation','requested vs applied trim','bottle-only appearance',
                          'immediate inheritance reset','normal repeated save','reopen editable bottle','manual shape removal','separate geometry window','remove/undo','export review',
                          'hex preview','desktop overlay click','scaled/negative screen coordinates','source preservation'],
                'desktop_capture_note':'Overlay and coordinate paths tested; real QScreen capture checked separately in the source UI.'}
    except Exception as error:
        result={'passed':False,'version':VERSION,'error':repr(error)};raise
    finally:
        if color:color.reject()
        window.document.undo.setClean();window.close()
        if 'result' in locals():report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
