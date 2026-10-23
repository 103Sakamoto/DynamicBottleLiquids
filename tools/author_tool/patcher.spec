"""One-folder build: zip-compatible resources stay compressed at runtime."""
from pathlib import Path
import json
import zipfile
import importlib.util
from PyInstaller.utils.hooks import collect_data_files

HERE=Path(SPECPATH)
ROOT=HERE.parents[1]
archive=ROOT/'build/author-resources.zip'
tzroot=Path(importlib.util.find_spec('tzdata').origin).parent
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
    for p in sorted(tzroot.rglob('*')):
        if p.is_file() and '__pycache__' not in p.parts and p.suffix!='.pyc':
            z.write(p, 'tzdata/'+p.relative_to(tzroot).as_posix())
a=Analysis([str(ROOT/'tools/author_tool_entry.py')],
    pathex=[str(ROOT/'tools'),str(ROOT/'build/author-tool-deps')],
    binaries=[],datas=[(str(HERE/'app-icon.ico'),'author_tool'),(str(archive),'.')]+collect_data_files('trimesh')+collect_data_files('qfluentwidgets'),
    hiddenimports=['matplotlib.backends.backend_qtagg'],
    runtime_hooks=[str(HERE/'runtime_resources.py')],
    excludes=['tzdata','tkinter','PySide6.QtWebEngineCore','PySide6.QtWebEngineWidgets','PySide6.QtPdf','PySide6.QtPdfWidgets',
              'PySide6.QtQml','PySide6.QtQuick','pandas','scipy','lxml','openpyxl','IPython','pytest','networkx','shapely','sympy'],
    noarchive=False)
# English UI never installs QTranslator. Matplotlib examples are not application data.
def unused_data(name):
    name=name.replace('\\','/')
    return name.startswith(('PySide6/translations/','matplotlib/mpl-data/sample_data/','matplotlib/mpl-data/plot_directive/'))
# These Qt plugins provide PDF image decoding and Qt's separate QML virtual keyboard,
# neither used by this QWidget desktop editor. Windows IME/input remains available.
unused_binaries={'qpdf.dll','qtvirtualkeyboardplugin.dll','Qt6Pdf.dll','Qt6VirtualKeyboard.dll',
                 'Qt6Quick.dll','Qt6Qml.dll','Qt6QmlMeta.dll','Qt6QmlModels.dll','Qt6QmlWorkerScript.dll'}
removed=[n for n,_,_ in a.datas if unused_data(n)]+[n for n,_,_ in a.binaries if Path(n).name in unused_binaries]
a.datas=[entry for entry in a.datas if not unused_data(entry[0])]
a.binaries=[entry for entry in a.binaries if Path(entry[0]).name not in unused_binaries]
(ROOT/'build/author-bundle-trim.json').write_text(json.dumps({'removed':removed,'resource_archive':'author-resources.zip'},indent=2))
pyz=PYZ(a.pure)
exe=EXE(pyz,a.scripts,[],exclude_binaries=True,name='DBL-patcher',console=False,icon=str(HERE/'app-icon.ico'))
coll=COLLECT(exe,a.binaries,a.datas,name='DBL-patcher')
