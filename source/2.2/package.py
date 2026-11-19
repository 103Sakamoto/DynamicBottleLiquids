from pathlib import Path
import hashlib, json, zipfile
from xml.etree import ElementTree as ET
R=Path(__file__).resolve().parent
base=R.parent/'2.1-rc1/DynamicBottleLiquids-2.1-RC1-OG-NG-AE-FOMOD.zip'
sha=lambda b:hashlib.sha256(b).hexdigest()
assert sha(base.read_bytes())=='524135988d395e010d337d3c5d3782d2141f61ed426648012acfbed72f7766d0'
with zipfile.ZipFile(base) as z:
    items={p:z.read(p) for p in z.namelist() if not p.endswith('/')}
for key,digest in json.loads(items.pop('manifest.json')).items():
    assert sha(items[key])==digest,key
for key in list(items):
    if key in ('VALIDATION.json','AUDIT.txt') or key.endswith(('README-2.1-RC1.txt','CHANGELOG-2.1-RC1.txt')):
        del items[key]
    elif key.endswith(('.txt','.xml')):
        items[key]=items[key].replace(b'2.1 RC1',b'2.2')
items['fomod/info.xml']=items['fomod/info.xml'].replace(b'Desktop and VR bottle liquids',b'Desktop bottle liquids')
for key in ('README.txt','CHANGELOG.txt'):
    items[key]=(R/key).read_bytes()
    items['components/desktop/docs/DynamicBottleLiquids/'+key]=items[key]
for ui in ('fusion','mf','dmui','none'):
    binary=(R/f'build-{ui}/DynamicBottleLiquids.dll').read_bytes()
    assert b'DynamicBottleLiquids 2.2 loaded' in binary and b'profiling LOCKED' not in binary
    items[f'components/ui-{ui}/F4SE/Plugins/DynamicBottleLiquids.dll']=binary
approved=R.parent/'next/ssr-routing-defaults/components/shared/F4SE/Plugins/DynamicBottleLiquids/shaders'
assert len(list(approved.glob('*.cso')))==6
for shader in approved.glob('*.cso'):
    data=(R/'build-common'/shader.name).read_bytes()
    assert data==shader.read_bytes(),shader
    items['components/desktop/F4SE/Plugins/DynamicBottleLiquids/shaders/'+shader.name]=data
for elem in ET.fromstring(items['fomod/ModuleConfig.xml']).iter():
    if elem.tag in ('folder','file','image'):
        path=(elem.get('source') or elem.get('path')).replace(chr(92),'/')
        assert path in items or any(k.startswith(path+'/') for k in items),path
assert len(items)==len({p.lower() for p in items})
assert not any('/usersettings/' in p.lower() for p in items)
items['components/desktop/F4SE/Plugins/DynamicBottleLiquids.ini']=b'[Logging]\r\nMode=Basic\r\n'
assert b'Mode=Basic' in items['components/desktop/F4SE/Plugins/DynamicBottleLiquids.ini']
items['manifest.json']=json.dumps({p:sha(v) for p,v in sorted(items.items())},indent=2).encode()
out=R/'DynamicBottleLiquids-2.2-OG-NG-AE-FOMOD.zip'
with zipfile.ZipFile(out,'w',zipfile.ZIP_DEFLATED) as z:
    for key,data in sorted(items.items()):
        z.writestr(key,data)
with zipfile.ZipFile(out) as z:
    assert z.testzip() is None
    for key,digest in json.loads(z.read('manifest.json')).items():
        assert sha(z.read(key))==digest
    z.extractall(R/'installer')
print(out,len(items),'verified entries')

