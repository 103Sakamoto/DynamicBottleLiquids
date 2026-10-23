"""Document-oriented bottle editor. Qt owns undo, forms, file dialogs and state."""
import copy
import concurrent.futures
import json
from pathlib import Path
import queue
import tempfile
import threading
import zipfile
from .ui_common import application,card,note,number,VERSION,help_tip,help_note,StatusLabel,install_help
from PySide6.QtCore import Qt,QTimer,QSettings,QUrl
from PySide6.QtGui import QAction,QKeySequence,QDesktopServices
from PySide6.QtWidgets import (QMainWindow,QWidget,QVBoxLayout,QHBoxLayout,QFormLayout,QScrollArea,QFrame,
    QSplitter,QListWidgetItem,QFileDialog,QMessageBox,QDialog,QTableWidgetItem,QHeaderView,QAbstractItemView,QProgressBar,QDialogButtonBox,QPlainTextEdit,QLayout,QSizePolicy,QToolButton,QComboBox)
from qfluentwidgets import PushButton,PrimaryPushButton,LineEdit,ComboBox,CheckBox,ListWidget,TableWidget,SubtitleLabel,TitleLabel
from matplotlib.figure import Figure
from matplotlib.backends.backend_qtagg import FigureCanvasQTAgg,NavigationToolbar2QT
from . import core,preparation
from .workspace import Workspace,SETTINGS,atomic_write
from .appearance_ui import AppearanceDialog,GroupsDialog
from .color_ui import ColorDialog
from .preview import draw_preview,draw_cavity

class PreviewToolbar(NavigationToolbar2QT):
    toolitems=tuple((('Export image','Export preview image','filesave','save_figure') if item[3]=='save_figure' else item)
                   for item in NavigationToolbar2QT.toolitems if item[3] not in ('configure_subplots','edit_parameters'))

class ExportReview(QDialog):
    def __init__(self,path,project,parent=None):
        super().__init__(parent);self.setWindowTitle('Review patch export');self.resize(800,650)
        layout=QVBoxLayout(self);layout.addWidget(SubtitleLabel('Validated patch — ready to export'))
        layout.addWidget(help_note('Requires the external-JSON development/VR runtime and the original mesh mod’s materials/textures. Install the patch after both. No in-game validation has been performed.'))
        lines=[]
        for key,source in project['sources'].items():
            audit=source['audit'];prep=source.get('preparation',{})
            lines.append(f"{key}: {prep.get('target_name','glass')} · applied end trim {audit['end_trim_fraction']*100:g}% · hides blocks {prep.get('hidden_blocks',[])}")
        with zipfile.ZipFile(path) as archive:lines+=['','Files to write:']+archive.namelist()
        text=QPlainTextEdit();text.setReadOnly(True);text.setPlainText('\n'.join(lines));layout.addWidget(text,1)
        layout.addWidget(help_note('Export writes a patch ZIP; Save project keeps your editable drafts and groups.'))
        buttons=QDialogButtonBox(QDialogButtonBox.StandardButton.Save|QDialogButtonBox.StandardButton.Cancel)
        buttons.button(QDialogButtonBox.StandardButton.Save).setText('Choose ZIP location…');buttons.accepted.connect(self.accept);buttons.rejected.connect(self.reject);layout.addWidget(buttons)

class AuthorWindow(QMainWindow):
    def __init__(self,persist=False):
        super().__init__();self.resize(1420,960);self.setMinimumSize(1040,720)
        self.document=Workspace(self);self.current_id=None;self.model=None;self.result=None;self.loading=False;self.cache={};self.preview_appearance=None;self.appearance_dialog=None;self.import_group=None
        self.future=None;self.job=None;self.cancel_event=threading.Event();self.progress_queue=queue.Queue()
        self.executor=concurrent.futures.ThreadPoolExecutor(max_workers=1);self.poll_timer=QTimer(self);self.poll_timer.timeout.connect(self.poll)
        self.settings=QSettings('DynamicBottleLiquids','AuthorTool') if persist else None
        self.last_preview_id=None;self.last_pick_event=None;self.export_temp=None;self.last_output=None
        surface=QWidget();surface.setObjectName('surface');self.setCentralWidget(surface)
        layout=QVBoxLayout(surface);layout.setContentsMargins(24,20,24,20);layout.setSpacing(16)
        heading=QHBoxLayout();layout.addLayout(heading);heading.addWidget(TitleLabel('DBL patcher'),1)
        self.document_status=note('');heading.addWidget(self.document_status)
        commands=QHBoxLayout();self.commands=commands;commands.setSpacing(8);layout.addLayout(commands)
        self.command_buttons={}
        for text,action,tip,shortcut in [
            ('New project',self.new_project,'Start a new collection of bottles.',QKeySequence.StandardKey.New),
            ('Open project…',self.open_project,'Resume a project you saved earlier.',QKeySequence.StandardKey.Open),
            ('Save project',self.save_project,'Save all bottles, groups and settings so you can continue later.',QKeySequence.StandardKey.Save),
            ('Save as…',lambda:self.save_project(save_as=True),'Save a separate copy in a new or empty folder.',QKeySequence.StandardKey.SaveAs),
            ('Import NIF…',self.open_nif,'Add an original NIF from the mod’s Meshes folder. The original file stays unchanged.',None)]:
            button=help_tip(PushButton(text),tip);
            if text!='Import NIF…':commands.addWidget(button)
            button.clicked.connect(lambda _,fn=action:fn());self.command_buttons[text]=button
            if shortcut:
                a=QAction(text,self);a.setShortcut(shortcut);a.triggered.connect(lambda _,fn=action:fn());self.addAction(a)
        self.undo_action=self.document.undo.createUndoAction(self,'Undo');self.undo_action.setShortcut(QKeySequence.StandardKey.Undo);self.addAction(self.undo_action)
        self.redo_action=self.document.undo.createRedoAction(self,'Redo');self.redo_action.setShortcut(QKeySequence.StandardKey.Redo);self.addAction(self.redo_action)
        self.history_buttons=[]
        for text,callback,signal,enabled in [('Undo',self.document.undo.undo,self.document.undo.canUndoChanged,self.document.undo.canUndo()),('Redo',self.document.undo.redo,self.document.undo.canRedoChanged,self.document.undo.canRedo())]:
            button=help_tip(PushButton(text),text+' the last change.');button.clicked.connect(callback);button.setEnabled(enabled);signal.connect(button.setEnabled);commands.addWidget(button);self.history_buttons.append(button)
        commands.addStretch()
        self.import_card,imp=card();layout.addWidget(self.import_card);imp.addWidget(SubtitleLabel('Import a bottle'))
        import_row=QHBoxLayout();imp.addLayout(import_row);import_row.addWidget(self.command_buttons['Import NIF…'])
        self.groups_button=help_tip(PushButton('Group Settings…'),'Create and edit shared appearance groups.');self.groups_button.clicked.connect(self.edit_groups);import_row.addWidget(self.groups_button)
        import_row.addSpacing(24);import_row.addWidget(note('Group'))
        self.group=help_tip(QComboBox(),'Assign the selected bottle to a group. With no selected bottle, choose the group for your next import.')
        self.group.setMinimumWidth(190);self.group.setMinimumHeight(33);self.group.setStyleSheet('QComboBox {background:white;border:1px solid #D1D1D1;border-radius:4px;padding:4px 8px;} QComboBox::drop-down {border:0;width:24px;} QComboBox:focus {border-bottom:2px solid #0067C0;}')
        self.group.activated.connect(self.group_changed);import_row.addWidget(self.group);import_row.addStretch()
        self.vertical_split=QSplitter(Qt.Orientation.Vertical);self.vertical_split.setChildrenCollapsible(False);layout.addWidget(self.vertical_split,1)
        self.splitter=QSplitter(Qt.Orientation.Horizontal);self.splitter.setChildrenCollapsible(False);self.vertical_split.addWidget(self.splitter)
        scroll=QScrollArea();scroll.setWidgetResizable(True);scroll.setFrameShape(QFrame.Shape.NoFrame);scroll.setMinimumWidth(400)
        self.workflow_scroll=scroll;self.splitter.addWidget(scroll);left=QWidget();scroll.setWidget(left);left_layout=QVBoxLayout(left);left_layout.setContentsMargins(0,0,8,0);left_layout.setSpacing(12)
        left_layout.setSizeConstraint(QLayout.SizeConstraint.SetMinAndMaxSize)
        self.editor_card,box=card();left_layout.addWidget(self.editor_card);box.setAlignment(Qt.AlignmentFlag.AlignTop)
        self.editor_card.setSizePolicy(QSizePolicy.Policy.Preferred,QSizePolicy.Policy.Maximum);box.setSpacing(8)
        title_row=QHBoxLayout();box.addLayout(title_row);self.bottle_title=SubtitleLabel('Import a bottle');title_row.addWidget(self.bottle_title,1)
        self.path_hint=help_note('');title_row.addWidget(self.path_hint)
        box.addWidget(help_tip(SubtitleLabel('Select shape to be patched'),'Choose the bottle wall or original liquid volume. Nothing is selected automatically.'))
        self.shapes=help_tip(ListWidget(),'Click the shape you want to patch. Open Advanced geometry to inspect the shapes in 3D.')
        self.shapes.setFixedHeight(225);self.shapes.currentItemChanged.connect(self.selection_changed);box.addWidget(self.shapes)
        self.detected=StatusLabel('Choose a shape.');box.addWidget(self.detected)
        self.removal_button=help_tip(PushButton('Remove unwanted shapes…'),'Optional: choose fake liquid or other shapes to hide in the exported NIF. This never edits the original.');self.removal_button.clicked.connect(self.edit_removed_shapes);box.addWidget(self.removal_button)
        self.removal_summary=note('No extra shapes selected');box.addWidget(self.removal_summary)
        self.calculate_button=help_tip(PrimaryPushButton('Prepare bottle'),'Find the glass and calculate space for the liquid using the current settings.');self.calculate_button.clicked.connect(self.calculate);box.addWidget(self.calculate_button)
        self.calculation_status=StatusLabel('');box.addWidget(self.calculation_status)
        box.removeWidget(self.removal_button);box.removeWidget(self.removal_summary);box.addWidget(self.removal_button);box.addWidget(self.removal_summary)
        left_layout.addStretch()
        self.cavity_card,cv=card();self.splitter.addWidget(self.cavity_card)
        row=QHBoxLayout();cv.addLayout(row);row.addWidget(SubtitleLabel('Cavity preview'),1)
        self.advanced_toggle=help_tip(PushButton('Advanced geometry…'),'Open the 3D preview and manual geometry settings.');self.advanced_toggle.clicked.connect(self.open_geometry);row.addWidget(self.advanced_toggle)
        self.cavity_figure=Figure(figsize=(6,4),dpi=100);self.cavity_canvas=FigureCanvasQTAgg(self.cavity_figure);self.cavity_canvas.setMinimumSize(250,230);cv.addWidget(self.cavity_canvas,1)
        self.contents_card,ct=card();self.splitter.insertWidget(1,self.contents_card);self.contents_card.setMinimumWidth(330);ct.addWidget(help_tip(SubtitleLabel('Bottles in this project'),'Select a bottle to edit it. Drag the divider to resize the list. Remove affects only that row; Undo restores it.'))
        self.profiles=TableWidget();self.profiles.setColumnCount(4);self.profiles.setHorizontalHeaderLabels(['Bottle','Group','Status','Actions'])
        self.profiles.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows);self.profiles.setSelectionMode(QAbstractItemView.SelectionMode.SingleSelection)
        self.profiles.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers);self.profiles.setMinimumHeight(170);self.profiles.verticalHeader().hide()
        self.profiles.horizontalHeader().hide();self.profiles.horizontalHeader().setSectionResizeMode(3,QHeaderView.ResizeMode.Stretch)
        for col in (0,1,2):self.profiles.hideColumn(col)
        self.profiles.horizontalHeader().setSectionResizeMode(0,QHeaderView.ResizeMode.Stretch)
        for col in (1,2):self.profiles.horizontalHeader().setSectionResizeMode(col,QHeaderView.ResizeMode.ResizeToContents)
        self.profiles.itemSelectionChanged.connect(self.table_selected);ct.addWidget(self.profiles,1)
        self.export_button=help_tip(PrimaryPushButton('Review && export'),'Check every prepared bottle and review the patch files before choosing a ZIP location.');self.export_button.clicked.connect(self.export);commands.addWidget(self.export_button)
        statusrow=QHBoxLayout();layout.addLayout(statusrow);self.status=StatusLabel('Import a NIF to begin.');statusrow.addWidget(self.status,1)
        self.progress=QProgressBar();self.progress.setRange(0,0);self.progress.setMaximumWidth(140);self.progress.hide();statusrow.addWidget(self.progress)
        self.cancel_button=help_tip(PushButton('Cancel task'),'Stop the current calculation or export check. Previously saved work stays unchanged.');self.cancel_button.clicked.connect(self.cancel_job);self.cancel_button.hide();statusrow.addWidget(self.cancel_button)
        self.open_output=help_tip(PushButton('Open output folder'),'Show the exported ZIP in its folder.');self.open_output.clicked.connect(self.open_output_folder);self.open_output.hide();statusrow.addWidget(self.open_output)
        self.make_geometry_window()
        self.setStyleSheet('QWidget#surface {background:#f3f5f7;} QFrame#card {background:white;border:1px solid #e1e5ea;border-radius:8px;}')
        self.splitter.setSizes([410,350,600]);install_help(surface);install_help(self.geometry_window);
        self.document.changed.connect(self.refresh_project);self.document.undo.cleanChanged.connect(self.update_title)
        if self.settings and self.settings.value('geometry'):self.restoreGeometry(self.settings.value('geometry'))
        self.refresh_project()

    def make_geometry_window(self):
        self.geometry_window=QDialog(self);self.geometry_window.setWindowTitle('Advanced geometry — DBL patcher');self.geometry_window.resize(1280,820)
        outer=QVBoxLayout(self.geometry_window);outer.setContentsMargins(24,24,24,24);outer.setSpacing(16)
        self.geometry_title=SubtitleLabel('Advanced geometry');outer.addWidget(self.geometry_title)
        split=QSplitter();outer.addWidget(split,1)
        scroll=QScrollArea();scroll.setWidgetResizable(True);scroll.setFrameShape(QFrame.Shape.NoFrame);scroll.setMinimumWidth(320);split.addWidget(scroll)
        self.advanced=QWidget();scroll.setWidget(self.advanced);form=QFormLayout(self.advanced);form.setSpacing(12)
        self.target=ComboBox();self.axis=ComboBox();self.axis.addItems(['Automatic','+X','-X','+Y','-Y','+Z','-Z'])
        self.clearance=number(.16,0,10,.01);self.trim=number(.5,.01,24,.5);self.offset_x=number(0,-10000,10000,.1);self.offset_y=number(0,-10000,10000,.1)
        for label,widget,tip in [
            ('Glass target',self.target,'Automatic finds the enclosing glass. Choose a different shape only if it picks the wrong one.'),
            ('Bottle up direction',self.axis,'The direction from the bottle’s base toward its opening.'),
            ('Wall inset',self.clearance,'Space left between the measured wall and liquid, in mesh units. Smaller values leave more room for liquid.'),
            ('End trim (%)',self.trim,'Percentage excluded at each end of the bottle to avoid uneven bases and rims.'),
            ('Center X',self.offset_x,'Move the liquid center along the red X axis. Whole numbers and decimals are accepted.'),
            ('Center Y',self.offset_y,'Move the liquid center along the green Y axis. Whole numbers and decimals are accepted.')]:
            form.addRow(help_tip(note(label),tip),help_tip(widget,tip))
        self.auto_trim=help_tip(CheckBox('Auto trim uneven ends'),'Allow preparation to trim up to 5% at each end if the base or rim is uneven.');self.auto_trim.setChecked(True);form.addRow(self.auto_trim)
        self.asset_root=help_tip(LineEdit(),'The original mod folder containing Materials. Only needed when material files cannot be found automatically.');self.asset_root.setPlaceholderText('Original mod folder (optional)');form.addRow(self.asset_root)
        browse=help_tip(PushButton('Choose mod folder…'),'Select the original mod folder containing Materials.');browse.clicked.connect(self.choose_assets);form.addRow(browse)
        reset=help_tip(PushButton('Reset geometry'),'Restore automatic detection, default inset and zero center offsets.');reset.clicked.connect(self.reset_geometry);form.addRow(reset)
        prepare=help_tip(PrimaryPushButton('Prepare bottle'),'Recalculate this bottle using the settings above.');prepare.clicked.connect(self.calculate);form.addRow(prepare);self.advanced_prepare=prepare
        self.geometry_status=StatusLabel('');form.addRow(self.geometry_status)
        self.preview_card,pv=card();split.addWidget(self.preview_card);pv.addWidget(help_tip(SubtitleLabel('Geometry preview'),'Approximate liquid color and fill. Lighting, foam, bubbles and refraction need in-game testing.'))
        self.figure=Figure(figsize=(9,5),dpi=100);self.canvas=FigureCanvasQTAgg(self.figure);self.canvas.setMinimumSize(400,300);pv.addWidget(self.canvas,1)
        self.toolbar=PreviewToolbar(self.canvas,self.preview_card,coordinates=False);pv.addWidget(self.toolbar);self.canvas.mpl_connect('pick_event',self.preview_picked)
        split.setSizes([320,900]);close=help_tip(PushButton('Close'),'Return to the main window. Geometry edits remain in the project.');close.clicked.connect(self.geometry_window.close);outer.addWidget(close,0,Qt.AlignmentFlag.AlignRight)
        for combo in (self.target,self.axis):combo.currentIndexChanged.connect(self.geometry_edited)
        for value in (self.clearance,self.trim,self.offset_x,self.offset_y):value.valueChanged.connect(self.geometry_edited)
        self.auto_trim.toggled.connect(self.geometry_edited);self.asset_root.editingFinished.connect(self.geometry_edited)
    def open_geometry(self):
        self.geometry_window.show();self.geometry_window.raise_();self.geometry_window.activateWindow();self.redraw()
    def group_changed(self,*_):
        if self.loading:return
        group=self.group.currentData()
        if not self.current():self.import_group=group;return
        self.document.change('Assign group',lambda s:s['appearance']['profiles'][self.current_id].update(group=group))
    def edit_removed_shapes(self):
        if not self.current() or not self.model:return
        dialog=QDialog(self);dialog.setWindowTitle('Remove unwanted shapes');dialog.resize(520,480);layout=QVBoxLayout(dialog)
        layout.addWidget(help_tip(SubtitleLabel('Shapes to remove from the patch'),'Checked shapes are hidden in the exported NIF. The original NIF stays unchanged.'))
        shapes=ListWidget();layout.addWidget(shapes,1);selected=set(self.current().get('removed_shapes',[]))
        for shape in self.model['meshes']:
            item=QListWidgetItem(shape['name']);item.setData(Qt.ItemDataRole.UserRole,shape['block']);item.setFlags(item.flags()|Qt.ItemFlag.ItemIsUserCheckable)
            item.setCheckState(Qt.CheckState.Checked if shape['block'] in selected else Qt.CheckState.Unchecked);shapes.addItem(item)
        error=StatusLabel('');layout.addWidget(error)
        buttons=QDialogButtonBox(QDialogButtonBox.StandardButton.Ok|QDialogButtonBox.StandardButton.Cancel);layout.addWidget(buttons);buttons.rejected.connect(dialog.reject)
        def apply():
            blocks=[shapes.item(i).data(Qt.ItemDataRole.UserRole) for i in range(shapes.count()) if shapes.item(i).checkState()==Qt.CheckState.Checked]
            try:
                if self.current()['liquid'] is not None:
                    target=self.current()['settings']['target']
                    if target is None:target=preparation.detect_target(self.model,self.selected())['block']
                    if target in blocks:raise ValueError('The glass being patched cannot also be removed.')
                self.document.update(self.current_id,{'removed_shapes':blocks},'Choose shapes to remove');dialog.accept()
            except ValueError as e:error.error(str(e))
        buttons.accepted.connect(apply);dialog.exec()
    @property
    def project(self):return {'appearance':self.document.state['appearance']}
    @property
    def dirty(self):return not self.document.undo.isClean()
    def update_title(self,*_):
        mark=' *' if self.dirty else '';name=self.document.folder.name if self.document.folder else 'Untitled project';self.setWindowTitle(f"{name}{mark} — DBL patcher {VERSION}")
        self.document_status.setToolTip(str(self.document.folder) if self.document.folder else 'Save project to keep your work.');self.document_status.setText(('Unsaved changes' if self.dirty else 'Saved')+' · '+(self.document.folder.name if self.document.folder else 'New project'))
    def guarded(self,action):
        try:return action()
        except (ValueError,OSError,KeyError,TypeError) as error:self.status.error(str(error));self.geometry_status.error(str(error));return None
    def current(self):return self.document.item(self.current_id)
    def refresh_project(self):
        if self.loading:return
        self.loading=True
        try:
            entries=self.document.state['items']
            if not self.current():self.current_id=entries[0]['id'] if entries else None
            self.profiles.clearContents();self.profiles.setRowCount(len(entries))
            for row,entry in enumerate(entries):
                profile=self.document.state['appearance']['profiles'][entry['id']]
                values=[entry['name'],profile['group'] or 'Ungrouped','○ To prepare' if entry['needs_calculation'] else 'Ready']
                for col,value in enumerate(values):
                    item=QTableWidgetItem(value);item.setData(Qt.ItemDataRole.UserRole,entry['id']);self.profiles.setItem(row,col,item)
                actions=QWidget();content=QVBoxLayout(actions);content.setContentsMargins(12,12,12,12);content.setSpacing(8)
                title=note(entry['name']);title.setStyleSheet('font-weight:600;');content.addWidget(title)
                content.addWidget(note(values[1]+' · '+values[2]));buttons=QHBoxLayout();buttons.setSpacing(8);content.addLayout(buttons)
                if entry['id']==self.current_id:actions.setStyleSheet('background:#EBF3FC;')
                action=PushButton('Appearance…');action.clicked.connect(lambda _,key=entry['id']:self.open_bottle_appearance(key));buttons.addWidget(action)
                remove=PushButton('Remove');remove.clicked.connect(lambda _,key=entry['id']:self.remove_row(key));remove.setEnabled(self.future is None);buttons.addWidget(remove)
                self.profiles.setCellWidget(row,3,actions);self.profiles.setRowHeight(row,132)
                if entry['id']==self.current_id:self.profiles.selectRow(row)
            entry=self.current();self.editor_card.setEnabled(entry is not None)
            if entry:
                cache_key=(entry['source_path'],entry['source_sha'])
                if cache_key not in self.cache:self.cache[cache_key]=core.load_mesh(entry['source_path'])
                self.model=self.cache[cache_key];self.result=None if entry['needs_calculation'] else entry['result']
                self.bottle_title.setText(entry['name']);self.geometry_title.setText('Advanced geometry · '+entry['name']);self.path_hint.setToolTip('Game path: '+entry['mesh_path'])
                profile=self.document.state['appearance']['profiles'][entry['id']]
                self.group.clear();self.group.addItem('Ungrouped',None)
                for group in self.document.state['appearance']['groups']:self.group.addItem(group,group)
                self.group.setCurrentIndex(self.group.findData(profile['group']))
                removed=entry.get('removed_shapes',[]);self.removal_summary.setText(f'{len(removed)} extra shapes selected' if removed else 'No extra shapes selected')

                self.shapes.clear();self.target.clear();self.target.addItem('Automatic',userData=None)
                for shape in self.model['meshes']:
                    item=QListWidgetItem(shape['name']);item.setData(Qt.ItemDataRole.UserRole,shape['block']);item.setData(Qt.ItemDataRole.AccessibleDescriptionRole,f"Block {shape['block']} · {shape['vertices']} vertices")
                    self.shapes.addItem(item);self.target.addItem(shape['name'],userData=shape['block'])
                    if entry['liquid']==shape['block']:self.shapes.setCurrentItem(item)
                if entry['liquid'] is None:self.shapes.setCurrentRow(-1);self.shapes.clearSelection()
                settings=entry['settings'];self.target.setCurrentIndex(max(0,self.target.findData(settings['target'])))
                self.axis.setCurrentText(settings['axis'] or 'Automatic');self.clearance.setValue(settings['clearance']);self.trim.setValue(settings['trim']*100)
                self.offset_x.setValue(settings['center_offset'][0]);self.offset_y.setValue(settings['center_offset'][1]);self.auto_trim.setChecked(settings['auto_trim']);self.asset_root.setText(settings['asset_root'] or '')
                self.update_detection()
                if self.result:
                    audit=self.result['audit'];p=self.result['preparation']
                    self.calculation_status.setText('Ready to export')
                    self.calculation_status.setToolTip(f"{len(self.result['rings'])} rings. Requested trim {settings['trim']*100:g}%; applied {audit['end_trim_fraction']*100:g}%. Automatically hidden shapes: {p['hidden_blocks']}.")
                else:self.calculation_status.setText('Needs recalculation' if entry['result'] else 'Choose a shape, then prepare the bottle.')
            else:
                self.model=None;self.result=None;self.shapes.clear();self.target.clear();self.bottle_title.setText('Import a bottle');self.detected.setText('');self.calculation_status.setText('No bottle selected.');self.group.clear();self.group.addItem('Ungrouped',None)
                for group in self.document.state['appearance']['groups']:self.group.addItem(group,group)
                self.group.setCurrentIndex(max(0,self.group.findData(self.import_group)));self.removal_summary.setText('');self.geometry_title.setText('No bottle selected');self.path_hint.setToolTip('Import a NIF from its original Meshes folder.')
        except (ValueError,OSError,KeyError) as error:
            self.model=None;self.result=None;self.status.error(str(error));self.calculation_status.error('Source unavailable. Reopen the project or reimport the original NIF.')
        finally:self.loading=False
        self.update_title();self.update_actions();self.redraw()
    def update_actions(self):
        active=self.future is not None;entry=self.current()
        self.calculate_button.setEnabled(bool(entry and entry['liquid'] is not None and self.model) and not active)
        self.export_button.setEnabled(bool(self.document.state['items']) and all(not i['needs_calculation'] for i in self.document.state['items']) and not active)
        self.progress.setVisible(active);self.cancel_button.setVisible(active)
        if self.appearance_dialog:
            self.editor_card.setEnabled(False);self.export_button.setEnabled(False)
            for button in self.history_buttons:button.setEnabled(False)
            self.undo_action.setEnabled(False);self.redo_action.setEnabled(False)
        self.advanced_prepare.setEnabled(self.calculate_button.isEnabled());self.advanced_toggle.setEnabled(bool(entry));self.removal_button.setEnabled(bool(entry) and not active)
    def table_selected(self):
        if self.loading:return
        row=self.profiles.currentRow()
        if row>=0:self.current_id=self.profiles.item(row,0).data(Qt.ItemDataRole.UserRole);self.refresh_project()
    def selected(self):
        entry=self.current()
        if not entry or entry['liquid'] is None or not self.model:raise ValueError('Select a shape to be patched.')
        return next(s for s in self.model['meshes'] if s['block']==entry['liquid'])
    def selection_changed(self,*_):
        if self.loading or not self.current() or not self.shapes.currentItem():return
        block=self.shapes.currentItem().data(Qt.ItemDataRole.UserRole)
        self.document.update(self.current_id,{'liquid':block,'selection_confirmed':True},'Select shape',geometry=True)
    def update_detection(self):
        try:
            entry=self.current();selected=self.selected();block=entry['settings']['target']
            target=next((s for s in self.model['meshes'] if s['block']==block),None) if block is not None else preparation.detect_target(self.model,selected)
            label='Selected shape'
            self.detected.setText(label+': '+selected['name']+'\n'+('Manual glass: ' if block is not None else 'Detected glass: ')+target['name'])
        except (ValueError,AttributeError) as e:
            if self.current() and self.current()['liquid'] is None:self.detected.setText('Choose a shape to continue.')
            else:self.detected.error(str(e))
    def geometry_edited(self,*_):
        if self.loading or not self.current():return
        settings=dict(target=self.target.currentData(),axis=None if self.axis.currentText()=='Automatic' else self.axis.currentText(),
                      clearance=self.clearance.value(),trim=self.trim.value()/100,center_offset=[self.offset_x.value(),self.offset_y.value()],asset_root=self.asset_root.text().strip() or None,auto_trim=self.auto_trim.isChecked())
        self.document.update(self.current_id,{'settings':settings},'Adjust geometry',geometry=True,merge='geometry')
        self.status.setText('Geometry changed. Prepare this bottle again.');self.geometry_status.setText('Settings changed. Prepare again to update the preview.')
    def reset_geometry(self):
        if self.current():self.document.update(self.current_id,{'settings':copy.deepcopy(SETTINGS)},'Reset geometry',geometry=True)
    def choose_assets(self):
        path=QFileDialog.getExistingDirectory(self,'Original mod Data folder')
        if path:self.asset_root.setText(path);self.geometry_edited()
    def validate_editor(self):
        core.slug(self.document.state['pack'])
        paths=set()
        for entry in self.document.state['items']:
            name=Path(core.mesh_path(entry['mesh_path'])).name.lower()
            if name in paths:raise ValueError('Two bottles use the same NIF filename. Remove the duplicate before exporting.')
            paths.add(name)
    def redraw(self):
        if not hasattr(self,'canvas'):return
        entry=self.current();selected=entry['liquid'] if entry else None;target=None;appearance=None;axis='+Z';offset=(0,0)
        if entry:
            try:
                block=entry['settings']['target'];target=block if block is not None else preparation.detect_target(self.model,self.selected())['block']
                appearance=core.effective_appearance({'appearance':self.preview_appearance or self.document.state['appearance']},entry['id'])
                shape=next(s for s in self.model['meshes'] if s['block']==target)
                axis=entry['settings']['axis'] or preparation.infer_axis(shape,self.selected());offset=entry['settings']['center_offset']
            except (ValueError,KeyError,TypeError):pass
        if entry:appearance=core.effective_appearance({'appearance':self.preview_appearance or self.document.state['appearance']},entry['id'])
        draw_cavity(self.cavity_figure,self.result,appearance);self.cavity_canvas.draw_idle()
        if not self.geometry_window.isVisible():return
        draw_preview(self.figure,self.model,selected,target,self.result,appearance,preserve=self.last_preview_id==self.current_id,isolate=False,axis=axis,center_offset=offset)
        self.last_preview_id=self.current_id;self.canvas.draw_idle()
    def preview_picked(self,event):
        if self.toolbar.mode or event.mouseevent is self.last_pick_event:return
        block=getattr(event.artist,'shape_block',None)
        if block is None:return
        self.last_pick_event=event.mouseevent
        for i in range(self.shapes.count()):
            if self.shapes.item(i).data(Qt.ItemDataRole.UserRole)==block:self.shapes.setCurrentRow(i);break
    def open_nif(self):
        if self.appearance_pending():return
        path,_=QFileDialog.getOpenFileName(self,'Import original bottle NIF','','Fallout 4 meshes (*.nif)')
        if path:self.guarded(lambda:self.load_nif(path))
    def load_nif(self,path):
        group=self.group.currentData();key,model=self.document.import_mesh(path);self.current_id=key;
        if group in self.document.state['appearance']['groups']:self.document.change('Assign imported group',lambda state:state['appearance']['profiles'][key].update(group=group))
        self.cache[(model['path'],model['sha256'])]=model;self.refresh_project()
        self.status.setText('Bottle imported. Select a shape to begin.')
    def calculate(self):
        def start():
            if self.future:raise ValueError('Wait for the current task or cancel it.')
            self.validate_editor();entry=copy.deepcopy(self.current());selected=self.selected();options=copy.deepcopy(entry['settings'])
            block=options.pop('target');target=next((s for s in self.model['meshes'] if s['block']==block),None)
            self.cancel_event=threading.Event();options['target']=target;options['cancel']=self.cancel_event.is_set
            self.job=('calculate',entry);self.future=self.executor.submit(preparation.calculate,self.model,selected,**options)
            self.status.setText('Calculating '+entry['name']+'…');self.calculation_status.setText('Calculating…');self.geometry_status.setText('Calculating…');self.poll_timer.start(100);self.update_actions()
        self.guarded(start)
    def cancel_job(self):
        if self.future:self.cancel_event.set();self.future.cancel();self.status.setText('Cancelling…')
    def poll(self):
        while not self.progress_queue.empty():self.status.setText(self.progress_queue.get_nowait())
        if self.future is None or not self.future.done():return
        self.poll_timer.stop();future=self.future;self.future=None;kind,payload=self.job;self.job=None;self.update_actions()
        try:
            result=future.result()
            if self.cancel_event.is_set():raise concurrent.futures.CancelledError()
            if kind=='calculate':
                current=self.document.item(payload['id'])
                if not current or any(current[k]!=payload[k] for k in ('settings','liquid','source_sha')):self.status.setText('Bottle settings changed; the old result was discarded.');return
                self.document.update(payload['id'],{'result':result,'needs_calculation':False,'selection_confirmed':True},'Prepare bottle')
                self.geometry_status.setText('Ready. You can close this window.');self.status.setText(payload['name']+' is prepared. Review appearance, then save or export.')
            else:self.finish_export(result,payload)
        except concurrent.futures.CancelledError:self.status.setText('Task cancelled. No result was applied.');self.refresh_project()
        except Exception as error:self.status.error(str(error));self.calculation_status.error(str(error));self.geometry_status.error(str(error))
        finally:
            if kind=='export' and self.export_temp:self.export_temp.cleanup();self.export_temp=None
    def names(self):return {i['id']:i['name'] for i in self.document.state['items']}
    def commit_appearance(self,appearance,label):
        self.document.change(label,lambda s:s.update(appearance=copy.deepcopy(appearance)))
        self.status.setText('Appearance updated. Save project to keep these changes.')
    def edit_appearance(self):
        if self.current():self.appearance_scope(('bottle',self.current_id))
    def edit_defaults(self):self.appearance_scope(('defaults',''))
    def open_bottle_appearance(self,key):
        if self.appearance_dialog:self.appearance_dialog.raise_();return
        self.current_id=key;self.refresh_project();self.appearance_scope(('bottle',key))
    def appearance_scope(self,scope):
        if self.appearance_dialog:self.appearance_dialog.raise_();return
        dialog=AppearanceDialog(self.project,self,scope,self.names());self.appearance_dialog=dialog
        dialog.setModal(False);dialog.previewChanged.connect(self.preview_changed)
        self.saved_popup_layout=(self.geometry(),self.splitter.sizes())
        area=self.screen().availableGeometry();width=min(760,max(640,int(area.width()*.48)))
        # Reserve the left side for the floating editor; keep the whole cavity on the right.
        self.resize(min(area.width(),max(self.width(),width+420)),min(self.height(),area.height()))
        self.move(max(area.left(),min(self.x(),area.right()-self.width()+1)),max(area.top(),min(self.y(),area.bottom()-self.height()+1)))
        self.splitter.setSizes([410,max(330,width-390),max(300,self.width()-width-80)])
        dialog.resize(width,min(820,area.height()-64));dialog.move(self.x()+8,self.y()+32)
        self.editor_card.setEnabled(False);self.import_card.setEnabled(False);self.contents_card.setEnabled(False)
        for button in self.command_buttons.values():button.setEnabled(False)
        self.export_button.setEnabled(False);self.undo_action.setEnabled(False);self.redo_action.setEnabled(False)
        for button in self.history_buttons:button.setEnabled(False)
        def finished(code):
            if code==QDialog.DialogCode.Accepted:self.commit_appearance(dialog.project['appearance'],'Edit appearance')
            self.appearance_dialog=None;self.preview_appearance=None
            self.import_card.setEnabled(True);self.contents_card.setEnabled(True)
            for button in self.command_buttons.values():button.setEnabled(True)
            self.undo_action.setEnabled(self.document.undo.canUndo());self.redo_action.setEnabled(self.document.undo.canRedo())
            for button,enabled in zip(self.history_buttons,(self.document.undo.canUndo(),self.document.undo.canRedo())):button.setEnabled(enabled)
            geometry,sizes=self.saved_popup_layout;self.setGeometry(geometry);self.splitter.setSizes(sizes);self.refresh_project();dialog.deleteLater()
        dialog.finished.connect(finished);dialog.show();dialog.raise_()
    def appearance_pending(self):
        if self.appearance_dialog:
            self.appearance_dialog.raise_();self.status.setText('Save or cancel the appearance popup first.');return True
        return False
    def preview_changed(self,appearance):self.preview_appearance=appearance;self.redraw()
    def edit_groups(self):
        if self.appearance_pending():return
        dialog=GroupsDialog(self.project,self,self.names())
        if dialog.exec()==QDialog.DialogCode.Accepted:self.commit_appearance(dialog.project['appearance'],'Edit appearance groups')
    def remove_row(self,key):
        if self.appearance_pending() or self.future is not None:return
        self.document.remove(key);self.status.setText("Bottle removed. Undo restores it.")
    def remove(self):
        if self.appearance_pending():return
        if self.current():self.document.remove(self.current_id);self.status.setText('Bottle removed. Undo restores its geometry and appearance.')
    def may_leave(self):
        if self.appearance_pending():return False
        if not self.dirty:return True
        answer=QMessageBox.question(self,'Unsaved project','Save your changes before continuing?',QMessageBox.StandardButton.Save|QMessageBox.StandardButton.Discard|QMessageBox.StandardButton.Cancel)
        if answer==QMessageBox.StandardButton.Cancel:return False
        if answer==QMessageBox.StandardButton.Save:return bool(self.save_project())
        return True
    def new_project(self):
        if not self.may_leave():return
        self.cancel_job();self.document.undo.clear();self.document.folder=None;self.document.state=Workspace().state
        self.current_id=None;self.cache.clear();self.refresh_project()
    def save_project(self,*_,save_as=False):
        if self.appearance_pending():return False
        def action():
            self.validate_editor();folder=None
            if save_as or self.document.folder is None:
                folder=QFileDialog.getExistingDirectory(self,'Choose a new or empty project folder')
                if not folder:return False
            result=self.document.save(folder);self.status.setText('Saved '+str(result));return True
        return self.guarded(action)
    def open_project(self):
        if not self.may_leave():return
        path=QFileDialog.getExistingDirectory(self,'Open author project folder')
        if path:self.guarded(lambda:self.load_project(path))
    def load_project(self,path):
        opened=Workspace();opened.open(path)
        self.cancel_job();self.current_id=None;self.cache.clear();self.preview_appearance=None
        self.document.state=opened.state;self.document.folder=opened.folder;self.document.undo.clear();self.document.changed.emit()
        self.status.setText('Project opened. Select a bottle to continue editing.')
    def export(self):
        def start():
            if self.future:raise ValueError('Wait for the current task or cancel it.')
            self.validate_editor();project=self.document.project();self.export_temp=tempfile.TemporaryDirectory(prefix='dbl-export-')
            path=Path(self.export_temp.name)/'patch.zip';self.cancel_event=threading.Event();self.job=('export',project)
            self.future=self.executor.submit(core.export_patch,project,path,self.progress_queue.put,self.cancel_event.is_set)
            self.status.setText('Validating cavity containment and preparing export review…');self.poll_timer.start(100);self.update_actions()
        self.guarded(start)
    def finish_export(self,path,project):
        dialog=ExportReview(path,project,self)
        if dialog.exec()!=QDialog.DialogCode.Accepted:self.status.setText('Export cancelled. Project changes are retained.');return
        output,_=QFileDialog.getSaveFileName(self,'Export patch ZIP',(self.document.folder.name if self.document.folder else 'DBL')+'-patch.zip','Patch ZIP (*.zip)')
        if not output:return
        atomic_write(output,Path(path).read_bytes());self.last_output=Path(output);self.open_output.show();self.status.setText('Exported '+output+'. Test the patch in game before distribution.')
    def open_output_folder(self):
        if self.last_output:QDesktopServices.openUrl(QUrl.fromLocalFile(str(self.last_output.parent)))
    def closeEvent(self,event):
        if not self.may_leave():event.ignore();return
        self.geometry_window.close()
        if self.settings:self.settings.setValue('geometry',self.saveGeometry())
        self.cancel_job();self.poll_timer.stop();self.executor.shutdown(wait=False,cancel_futures=True)
        if self.export_temp:
            temp=self.export_temp
            if self.future:self.future.add_done_callback(lambda _:temp.cleanup())
            else:temp.cleanup()
        event.accept()

def main():
    app=application();window=AuthorWindow(persist=True);window.show();app.exec()

