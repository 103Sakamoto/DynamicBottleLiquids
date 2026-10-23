"""Author document: draft bottles, Qt undo history and atomic manifest saves.

The manifest is committed last. Previous geometry/appearance revisions remain
recoverable if a write is interrupted; original meshes are never overwritten.
"""
import copy
import hashlib
import json
from pathlib import Path
import re
import uuid
from PySide6.QtCore import QObject, Signal, QSaveFile, QIODevice
from PySide6.QtGui import QUndoStack, QUndoCommand
from . import core

SETTINGS=dict(target=None,axis=None,clearance=.16,trim=.005,center_offset=[0.,0.],asset_root=None,auto_trim=True)

def identifier(text):
    value=re.sub('[^a-z0-9]+','-',text.lower()).strip('-')[:55]
    return value if value and value[0].isalpha() else 'bottle-'+value

def atomic_write(path,data):
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
    file=QSaveFile(str(path))
    if not file.open(QIODevice.OpenModeFlag.WriteOnly):raise OSError(file.errorString())
    if file.write(data)!=len(data):file.cancelWriting();raise OSError(file.errorString())
    if not file.commit():raise OSError(file.errorString())

def json_bytes(value):return (json.dumps(value,indent=2,allow_nan=False)+'\n').encode('utf-8')

class Change(QUndoCommand):
    def __init__(self,document,label,before,after,key):
        super().__init__(label);self.doc=document;self.before=before;self.after=after;self.key=key
    def id(self):return 1 if self.key else -1
    def mergeWith(self,other):
        if not isinstance(other,Change) or self.key!=other.key:return False
        self.after=other.after;return True
    def redo(self):self.doc.state=copy.deepcopy(self.after);self.doc.changed.emit()
    def undo(self):self.doc.state=copy.deepcopy(self.before);self.doc.changed.emit()

class Workspace(QObject):
    changed=Signal()
    def __init__(self,parent=None):
        super().__init__(parent);self.undo=QUndoStack(self);self.folder=None
        self.state={'pack':'project-'+uuid.uuid4().hex[:12],'appearance':core.new_project()['appearance'],'items':[]}
    def change(self,label,edit,key=None):
        before=copy.deepcopy(self.state);after=copy.deepcopy(before);edit(after)
        if after!=before:self.undo.push(Change(self,label,before,after,key))
    def item(self,key):return next((i for i in self.state['items'] if i['id']==key),None)
    def import_mesh(self,path):
        model=core.load_mesh(path);key=identifier(Path(path).stem);base=key;n=2
        while self.item(key):key=f'{base}-{n}';n+=1
        parts=Path(path).parts;index=next((i for i,p in enumerate(parts) if p.lower()=='meshes'),None)
        if index is None:raise ValueError('Import the NIF from its original Meshes folder so its game path can be detected.')
        suggested='/'.join(parts[index:])
        candidates=[s['block'] for s in model['meshes'] if re.search('liquid',s['name'],re.I) and not re.search('bubble|foam|top',s['name'],re.I)]
        liquid=None
        entry=dict(id=key,name=Path(path).stem,source_path=model['path'],source_sha=model['sha256'],mesh_path=suggested,
                   liquid=liquid,selection_confirmed=False,removed_shapes=[],settings=copy.deepcopy(SETTINGS),result=None,needs_calculation=True)
        def edit(state):
            state['items'].append(entry);state['appearance']['profiles'][key]={'group':None,'overrides':{}}
        self.change('Import bottle',edit);return key,model
    def update(self,key,changes,label='Edit bottle',geometry=False,merge=None):
        def edit(state):
            entry=next(i for i in state['items'] if i['id']==key);entry.update(copy.deepcopy(changes))
            if geometry:entry['needs_calculation']=True
        self.change(label,edit,(key,merge) if merge else None)
    def remove(self,key):
        def edit(state):
            state['items']=[i for i in state['items'] if i['id']!=key];state['appearance']['profiles'].pop(key,None)
        self.change('Remove bottle',edit)
    def project(self,require_ready=True):
        project=core.new_project(core.slug(self.state['pack']))
        project['appearance']=copy.deepcopy(self.state['appearance']);project['appearance']['profiles']={}
        for entry in self.state['items']:
            if entry['needs_calculation'] or not entry['result']:
                if require_ready:raise ValueError(f"{entry['name']}: prepare this bottle before export.")
                continue
            model=core.load_mesh(entry['source_path'])
            if model['sha256']!=entry['source_sha']:raise ValueError(f"{entry['name']}: source changed; reimport it before export.")
            result=copy.deepcopy(entry['result']);profile=self.state['appearance']['profiles'][entry['id']]
            target=result['preparation']['target_block']
            removed=entry.get('removed_shapes',[])
            if target in removed:raise ValueError(entry['name']+': the patched glass cannot also be removed.')
            result['preparation']['hidden_blocks']=sorted(set(result['preparation']['hidden_blocks'])|set(removed))
            core.add_profile(project,model,entry['liquid'],target,result,entry['id'],entry['mesh_path'],profile['group'])
            project['appearance']['profiles'][entry['id']]=copy.deepcopy(profile)
        return project
    def save(self,folder=None):
        if folder is None and self.folder is None:raise ValueError('Choose a project folder first.')
        folder=Path(folder or self.folder or '').resolve()
        core.slug(self.state['pack'])
        if self.folder is None or folder!=self.folder:
            if folder.exists() and any(folder.iterdir()):raise ValueError('Save As needs a new or empty folder to avoid replacing another project.')
        staged=copy.deepcopy(self.state);revision=uuid.uuid4().hex
        for entry in staged['items']:
            raw=Path(entry['source_path']).read_bytes()
            if hashlib.sha256(raw).hexdigest()!=entry['source_sha']:raise ValueError(f"{entry['name']}: source changed since import.")
            relative=f"sources/{entry['id']}-{entry['source_sha'][:16]}.nif"
            if not (folder/relative).exists():atomic_write(folder/relative,raw)
            entry['source_path']=relative
        pack=staged['pack'];appearance=staged.pop('appearance')
        geometry=[]
        for entry in staged['items']:
            result=entry.pop('result')
            if result:
                geometry.append({'id':entry['id'],'result':{k:result[k] for k in ('matrix','rings','canonical_matrix')}})
                entry['calculation']={k:result[k] for k in ('audit','preparation')}
        geometry_file=f'geometry/{pack}.{revision}.geometry.json';appearance_file=f'appearance/{pack}.{revision}.appearance.json'
        atomic_write(folder/geometry_file,json_bytes(geometry));atomic_write(folder/appearance_file,json_bytes(appearance))
        staged.update(format='dbl-author-workspace',version=2,geometry_file=geometry_file,appearance_file=appearance_file)
        atomic_write(folder/'project.json',json_bytes(staged))
        self.folder=folder
        def rebase(state):
            for item in state['items']:
                saved=folder/f"sources/{item['id']}-{item['source_sha'][:16]}.nif"
                if saved.exists():item['source_path']=str(saved)
        rebase(self.state)
        for index in range(self.undo.count()):
            command=self.undo.command(index);rebase(command.before);rebase(command.after)
        self.undo.setClean();self.changed.emit();return folder/'project.json'
    def open(self,folder):
        folder=Path(folder).resolve();manifest=json.loads((folder/'project.json').read_text(encoding='utf-8'))
        def child(name):
            path=(folder/name).resolve()
            if not path.is_relative_to(folder):raise ValueError('Project path escapes its folder.')
            return path
        if manifest.get('format')=='dbl-author-workspace' and manifest.get('version')==2:
            appearance=json.loads(child(manifest['appearance_file']).read_text(encoding='utf-8'))
            results={r['id']:r['result'] for r in json.loads(child(manifest['geometry_file']).read_text(encoding='utf-8'))}
            state={'pack':manifest['pack'],'appearance':appearance,'items':manifest['items']}
            core.slug(state['pack']);core.controls(appearance['defaults'])
            if set(appearance['defaults'])!=set(core.DEFAULTS):raise ValueError('Incomplete pack defaults.')
            for name,values in appearance['groups'].items():core.slug(name);core.controls(values)
            ids=set()
            for entry in state['items']:
                key=core.slug(entry['id'])
                if key in ids:raise ValueError('Duplicate bottle ID.')
                ids.add(key);entry['source_path']=str(child(entry['source_path']));entry['result']=results.get(key)
                if entry['result']:entry['result'].update(entry.pop('calculation',{}))
                if not entry['result']:entry['needs_calculation']=True
                core.effective_appearance({'appearance':appearance},key)
            if ids!=set(appearance['profiles']):raise ValueError('Bottle and appearance IDs differ.')
        else:
            legacy=core.load_project(folder);state={'pack':legacy['pack'],'appearance':legacy['appearance'],'items':[]}
            import numpy as np
            from . import preparation
            for definition in legacy['geometry']['definitions']:
                key=definition['id'];source=legacy['sources'][key];model=core.load_mesh(source['path'])
                selected=next(m for m in model['meshes'] if m['block']==source['cavity_block'])
                target=next(m for m in model['meshes'] if m['block']==source['target_block'])
                plan=source.get('preparation') or preparation.plan(model,selected,target)[0]
                result=dict(matrix=definition['matrix'],rings=definition['rings'],audit=source['audit'],preparation=plan,
                            canonical_matrix=(np.array(definition['matrix']).reshape(4,4)@np.linalg.inv(target['world_matrix'])).tolist())
                settings=copy.deepcopy(SETTINGS)
                settings.update(axis=source['audit']['axis'],trim=source['audit']['end_trim_fraction'],
                                clearance=source['audit']['clearance'],center_offset=source['audit']['center_offset'],target=target['block'])
                state['items'].append(dict(id=key,name=key,source_path=source['path'],source_sha=source['sha256'],mesh_path=definition['mesh_path'],
                                           liquid=selected['block'],selection_confirmed=True,settings=settings,result=result,needs_calculation=False))
        self.state=state;self.folder=folder;self.undo.clear();self.changed.emit()
