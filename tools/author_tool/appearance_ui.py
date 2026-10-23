"""Fixed-scope appearance editor. Inheritance is status, not a checkbox."""
import copy
from pathlib import Path
import json
from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (QDialog,QVBoxLayout,QHBoxLayout,QScrollArea,QWidget,QFrame,
    QInputDialog,QFileDialog,QDialogButtonBox,QComboBox)
from qfluentwidgets import PushButton,PrimaryPushButton,ComboBox,Slider,SubtitleLabel,ListWidget
from .ui_common import note,number,help_tip,help_note,StatusLabel,install_help
from . import core
from .color_ui import ColorDialog
from .workspace import atomic_write,json_bytes,identifier

GROUP_HELP={'New':'Create a group to share appearance across bottles.', 'Duplicate':'Create a new group using the selected group’s settings.',
    'Rename':'Rename the selected group. Its bottles keep their membership.', 'Edit':'Change shared appearance for the selected group.',
    'Remove':'Remove the group while keeping each member’s current appearance.',
    'Import profile…':'Load a reusable appearance group from a JSON file.', 'Export profile…':'Save the selected group’s appearance as a reusable JSON file.'}

FIELD_HELP={
    'liquid_level':'How full the bottle is. 0% is empty; 100% fills the measured space.',
    'opacity':'How strongly the liquid hides what is behind it.',
    'glass_thickness':'Thickness of the glass wall in mesh units. Larger values leave less space for liquid.',
    'glass_opacity':'How strongly the glass hides what is behind it.',
    'bubble_density':'Amount of bubbles present in the liquid.',
    'shaking_bubbles':'How strongly shaking creates additional bubbles.',
    'surface_foam':'Amount of foam on the liquid surface.',
    'refraction_strength':'How strongly the bottle distorts the background.',
    'glow_strength':'How brightly the liquid glows. 0 turns glow off.',
    'wet_absorption':'How strongly liquid left on the inner wall darkens the view.',
    'wet_smoothness':'How glossy the wet inner wall looks.',
    'wet_refraction':'How strongly the wet inner wall distorts the view.',
    'wet_drying':'Seconds for the wet inner wall to dry.'}

class AppearanceDialog(QDialog):
    previewChanged=Signal(dict)
    def __init__(self,project,parent=None,scope=None,names=None):
        super().__init__(parent);self.project=copy.deepcopy(project);self.names=names or {};self.loading=False
        profiles=self.project['appearance']['profiles']
        self.current_scope=scope or (('bottle',next(iter(profiles))) if profiles else ('defaults',''))
        self.setWindowTitle('Appearance — '+self.current_scope[0]);self.resize(760,820)
        outer=QVBoxLayout(self);outer.setContentsMargins(24,24,24,24);outer.setSpacing(12)
        kind,key=self.current_scope
        title='Default appearance' if kind=='defaults' else ('Group: '+key if kind=='group' else self.names.get(key,key))
        outer.addWidget(SubtitleLabel('Editing '+title));self.impact=note('');outer.addWidget(self.impact)
        self.membership=help_tip(QComboBox(),'Use shared settings from this group. Existing bottle overrides are kept.');self.membership.addItem('Ungrouped',userData=None);self.membership.setStyleSheet('QComboBox {background:white;border:1px solid #D1D1D1;border-radius:4px;padding:6px;} QComboBox::drop-down {border:0;width:24px;}')
        for group in self.project['appearance']['groups']:self.membership.addItem(group,userData=group)
        if kind=='bottle':
            row=QHBoxLayout();row.addWidget(note('Appearance group'));row.addWidget(self.membership,1);outer.addLayout(row)
            self.membership.setCurrentIndex(self.membership.findData(profiles[key]['group']))
            self.membership.currentIndexChanged.connect(self.member_changed)
        else:self.membership.hide()
        scroll=QScrollArea();scroll.setWidgetResizable(True);scroll.setFrameShape(QFrame.Shape.NoFrame);outer.addWidget(scroll,1)
        content=QWidget();scroll.setWidget(content);form=QVBoxLayout(content);form.setSpacing(16)
        self.rows={};self.colors={};self.group_actions={}
        for key,(lo,hi,default) in core.FIELDS.items():
            row=QVBoxLayout();form.addLayout(row);line=QHBoxLayout();row.addLayout(line)
            suffix=' (%)' if key in core.PERCENT_FIELDS else (' (units)' if key=='glass_thickness' else (' (seconds)' if key=='wet_drying' else ''))
            line.addWidget(help_tip(note(key.replace('_',' ').capitalize()+suffix),FIELD_HELP[key]),1)
            value=number(core.display_value(default,key),core.display_value(lo,key),core.display_value(hi,key),5 if key in core.PERCENT_FIELDS else .1)
            value.setDecimals(4);value.setSuffix("%" if key in core.PERCENT_FIELDS else "");value.setAccessibleDescription(FIELD_HELP[key]);line.addWidget(value)
            reset=PushButton('Reset');reset.clicked.connect(lambda _,k=key:self.reset_value(k));line.addWidget(reset)
            slider=Slider(Qt.Orientation.Horizontal);slider.setRange(round(lo*10000),round(hi*10000));slider.setSingleStep(500 if key in core.PERCENT_FIELDS else 1000)
            slider.setAccessibleDescription(FIELD_HELP[key]);line.insertWidget(1,slider,2);source=note('');row.addWidget(source)
            self.rows[key]=(slider,value,source,reset)
            if kind=='bottle':
                reset.hide();row.removeWidget(source);self.add_group_actions(row,source,key)
            slider.valueChanged.connect(lambda v,k=key:self.slider_changed(k,v))
            value.valueChanged.connect(lambda v,k=key:self.set_value(k,core.stored_value(v,k)))
        for key in ('liquid_color','glass_color'):
            line=QHBoxLayout();form.addLayout(line);line.addWidget(note(key.replace('_',' ').capitalize()),1)
            swatch=QFrame();swatch.setFixedSize(48,32);line.addWidget(swatch)
            choose=help_tip(PushButton('Choose color…'),'Choose a color, paste a hex value or pick from your desktop.');line.addWidget(choose);choose.clicked.connect(lambda _,k=key:self.choose_color(k))
            reset=PushButton('Reset');line.addWidget(reset);reset.clicked.connect(lambda _,k=key:self.reset_value(k))
            help_tip(reset,'Remove this override and use the inherited color.');source=note('');self.colors[key]=(swatch,choose,source,reset)
            if kind=='bottle':reset.hide();self.add_group_actions(form,source,key)
            else:form.addWidget(source)
        form.addWidget(help_note('The geometry preview shows approximate color and fill. Test lighting, refraction, foam and bubbles in game.'))
        self.error=StatusLabel('');outer.addWidget(self.error)
        outer.addWidget(help_note('Save keeps these changes in the project. Save project writes them to disk. Cancel discards changes made in this window.'))
        buttons=QHBoxLayout();buttons.addStretch();outer.addLayout(buttons)
        cancel=help_tip(PushButton('Cancel'),'Discard changes made in this window.');cancel.clicked.connect(self.reject);buttons.addWidget(cancel)
        done=help_tip(PrimaryPushButton('Save'),'Keep bottle edits and any per-parameter group edits in the project. Save project writes them to disk.');done.clicked.connect(self.accept);buttons.addWidget(done)
        self.load_values();install_help(self)
    def add_group_actions(self,layout,source,key):
        row=QHBoxLayout();layout.addLayout(row);row.addWidget(source,1)
        save=PushButton('Save to group');save.clicked.connect(lambda:self.save_to_group(key));row.addWidget(save)
        apply=PushButton('Apply from group');apply.clicked.connect(lambda:self.apply_from_group(key));row.addWidget(apply)
        row.addWidget(help_note('Save to group copies only this parameter to the assigned group and restores inheritance for this bottle. Other bottle overrides stay unchanged. Apply from group uses the group value for only this parameter. Save at the bottom keeps these edits; Cancel discards them. Assign a group to enable these actions.'))
        self.group_actions[key]=(save,apply)
    def save_to_group(self,key):
        group=self.project['appearance']['profiles'][self.current_scope[1]]['group']
        if group is None:return
        self.project['appearance']['groups'][group][key]=self.values[key];self.own().pop(key,None);self.load_values()
    def apply_from_group(self,key):
        group=self.project['appearance']['profiles'][self.current_scope[1]]['group']
        if group is None:return
        self.own().pop(key,None);self.load_values()
    def own(self):
        a=self.project['appearance'];kind,key=self.current_scope
        return a['defaults'] if kind=='defaults' else a['groups'][key] if kind=='group' else a['profiles'][key]['overrides']
    def effective(self):
        a=self.project['appearance'];kind,key=self.current_scope
        if kind=='bottle':return core.effective_appearance(self.project,key)
        values=dict(a['defaults'])
        if kind=='group':values.update(a['groups'][key])
        return values
    def source(self,key):
        a=self.project['appearance'];kind,name=self.current_scope
        if kind=='defaults':return 'Default appearance'
        if key in self.own():return 'Customized for this bottle' if kind=='bottle' else 'Set by this group'
        group=a['profiles'][name]['group'] if kind=='bottle' else None
        return 'From '+group+' group' if group and key in a['groups'][group] else 'From default appearance'
    def load_values(self):
        self.loading=True;values=self.effective();self.values=values;kind,name=self.current_scope
        for key,(slider,value,source,reset) in self.rows.items():
            slider.setValue(round(values[key]*10000));value.setValue(core.display_value(values[key],key))
            source.setText(self.source(key));reset.setEnabled(kind=='defaults' and values[key]!=core.DEFAULTS[key] or kind!='defaults' and key in self.own())
            reset.setToolTip('Restore factory value' if kind=='defaults' else 'Use inherited value immediately') if kind!='bottle' else None
        for key,(swatch,button,source,reset) in self.colors.items():
            swatch.setStyleSheet(f'background:{values[key]};border:1px solid #9ba6b0;border-radius:5px;')
            button.setText(values[key]+' · Choose…');source.setText(self.source(key))
            reset.setEnabled(kind=='defaults' and values[key]!=core.DEFAULTS[key] or kind!='defaults' and key in self.own())
        if kind=='bottle':self.impact.setText('Bottle appearance' if self.project['appearance']['profiles'][name]['group'] is not None else 'Choose a group to enable Save to group / Apply from group');self.impact.setToolTip('Edits affect this bottle. Save to group shares only that parameter with the group. Save commits all pending edits; Cancel discards them.')
        else:
            members=[k for k,p in self.project['appearance']['profiles'].items() if kind=='defaults' or p['group']==name]
            exceptions=[self.names.get(k,k)+': '+', '.join(self.project['appearance']['profiles'][k]['overrides']) for k in members if self.project['appearance']['profiles'][k]['overrides']]
            self.impact.setText(f"Shared settings · {len(members)} bottles: "+(', '.join(self.names.get(k,k) for k in members) or 'none yet')+'. Bottle overrides remain unchanged.'+
                                ('\nExceptions: '+'; '.join(exceptions) if exceptions else '')+
                                (' Group overrides also take precedence.' if kind=='defaults' else ''))
        for save,apply in self.group_actions.values():
            enabled=self.project['appearance']['profiles'][name]['group'] is not None;save.setEnabled(enabled);apply.setEnabled(enabled)
        self.loading=False;self.previewChanged.emit(self.project['appearance'])
    def set_value(self,key,value):
        if self.loading:return
        core.controls({key:value});self.own()[key]=value;self.load_values()
    def slider_changed(self,key,value):
        if not self.loading:self.set_value(key,core.snapped(value/10000,key))
    def reset_value(self,key):
        if self.current_scope[0]=='defaults':self.own()[key]=core.DEFAULTS[key]
        else:self.own().pop(key,None)
        self.load_values()
    def member_changed(self,*_):
        if self.loading:return
        self.project['appearance']['profiles'][self.current_scope[1]]['group']=self.membership.currentData();self.load_values()
    def choose_color(self,key):
        dialog=ColorDialog(self.values[key],self)
        if dialog.exec()==QDialog.DialogCode.Accepted:self.set_value(key,dialog.color.name().upper())

class GroupsDialog(QDialog):
    def __init__(self,project,parent=None,names=None):
        super().__init__(parent);self.project=copy.deepcopy(project);self.names=names or {};self.resize(680,600);self.setWindowTitle('Appearance groups')
        layout=QVBoxLayout(self);layout.addWidget(SubtitleLabel('Appearance groups'))
        layout.addWidget(help_note('Groups share appearance across bottles. Individual bottle changes take priority. Removing a group keeps each bottle’s current appearance.'))
        self.list=ListWidget();layout.addWidget(self.list,1);row=QHBoxLayout();layout.addLayout(row)
        for text,action in [('New',self.new),('Duplicate',self.duplicate),('Rename',self.rename),('Edit',self.edit),('Remove',self.remove)]:
            b=help_tip(PushButton(text),GROUP_HELP[text]);b.clicked.connect(lambda _,a=action:self.guarded(a));row.addWidget(b)
        defaults=help_tip(PushButton('Default appearance…'),'Edit starting values used by bottles and groups that have no overrides.');defaults.clicked.connect(self.edit_defaults);layout.addWidget(defaults)
        row=QHBoxLayout();layout.addLayout(row)
        for text,action in [('Import profile…',self.import_profile),('Export profile…',self.export_profile)]:
            b=help_tip(PushButton(text),GROUP_HELP[text]);b.clicked.connect(lambda _,a=action:self.guarded(a));row.addWidget(b)
        self.error=StatusLabel('');layout.addWidget(self.error);buttons=QDialogButtonBox(QDialogButtonBox.StandardButton.Ok|QDialogButtonBox.StandardButton.Cancel)
        buttons.button(QDialogButtonBox.StandardButton.Ok).setText('Done');buttons.accepted.connect(self.accept);buttons.rejected.connect(self.reject);layout.addWidget(buttons);self.refresh();install_help(self)
    def guarded(self,action):
        try:action();self.error.setText('')
        except (ValueError,OSError,KeyError,TypeError) as e:self.error.error(str(e))
    def refresh(self):
        self.list.clear()
        for name in self.project['appearance']['groups']:self.list.addItem(name)
        if self.list.count():self.list.setCurrentRow(0)
    def selected(self):
        if not self.list.currentItem():raise ValueError('Select a group first.')
        return self.list.currentItem().text()
    def ask_name(self,title):
        name,ok=QInputDialog.getText(self,title,'Group name (an ID will be generated):')
        if not ok or not name.strip():return None
        key=identifier(name);core.slug(key)
        if key in self.project['appearance']['groups']:raise ValueError('That group already exists.')
        return key
    def new(self):
        key=self.ask_name('New group')
        if key:self.project['appearance']['groups'][key]={};self.refresh()
    def duplicate(self):
        old=self.selected();key=self.ask_name('Duplicate group')
        if key:self.project['appearance']['groups'][key]=dict(self.project['appearance']['groups'][old]);self.refresh()
    def rename(self):
        old=self.selected();key=self.ask_name('Rename group')
        if not key:return
        a=self.project['appearance'];a['groups'][key]=a['groups'].pop(old)
        for p in a['profiles'].values():
            if p['group']==old:p['group']=key
        self.refresh()
    def edit(self):
        dialog=AppearanceDialog(self.project,self,('group',self.selected()),self.names)
        if dialog.exec()==QDialog.DialogCode.Accepted:self.project=dialog.project
    def edit_defaults(self):
        dialog=AppearanceDialog(self.project,self,('defaults',''),self.names)
        if dialog.exec()==QDialog.DialogCode.Accepted:self.project=dialog.project
    def remove(self):
        name=self.selected();a=self.project['appearance']
        for key,p in a['profiles'].items():
            if p['group']==name:p['overrides']=core.effective_appearance(self.project,key);p['group']=None
        del a['groups'][name];self.refresh()
    def import_profile(self):
        path,_=QFileDialog.getOpenFileName(self,'Import group profile','','Group profiles (*.json)')
        if not path:return
        data=json.loads(Path(path).read_text(encoding='utf-8'))
        if data.get('format')!='dbl-appearance-group' or data.get('version')!=1:raise ValueError('Unsupported group profile.')
        name=core.slug(data['name']);values=core.controls(data['values'])
        if name in self.project['appearance']['groups']:
            name=self.ask_name('Choose a new name for imported group')
        if name:self.project['appearance']['groups'][name]=values;self.refresh()
    def export_profile(self):
        name=self.selected();path,_=QFileDialog.getSaveFileName(self,'Export reusable group',name+'.appearance-group.json','JSON (*.json)')
        if path:atomic_write(path,json_bytes({'format':'dbl-appearance-group','version':1,'name':name,'values':self.project['appearance']['groups'][name]}))

