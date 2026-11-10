from pathlib import Path
import zipfile,json,hashlib,xml.etree.ElementTree as ET,sys
from whiskey_installer import add_whiskey_option
R=Path(__file__).resolve().parent;root=R.parents[1];n=root/'release/next'
sha=lambda b:hashlib.sha256(b).hexdigest()
base=root/'release/2.0/DynamicBottleLiquids-2.0-FOMOD-fixed.zip'
assert sha(base.read_bytes())=='46a622bde36e56026a9379ae31063d9468410bdf3f6582367ce7a346fb427041'
with zipfile.ZipFile(base) as z:items={k:z.read(k) for k in z.namelist() if not k.endswith('/')}
items={k:v for k,v in items.items() if not k.startswith('components/vr/') and k not in ('manifest.json','VALIDATION.json')}
# Accepted components only; no experimental directories are assembly inputs.
for component in ['AtomicShine','NoveltyNuka','Vintage','WastelandImports','Fallout3Whiskey']:
 for p in (n/'components'/component).rglob('*'):
  if p.is_file() and p.suffix.lower() not in ('.dll','.cso'):
   items['components/'+p.relative_to(n/'components').as_posix()]=p.read_bytes()
for p in (n/'ice-condensation/components').rglob('*'):
 if p.is_file():items['components/'+p.relative_to(n/'ice-condensation/components').as_posix()]=p.read_bytes()
# Verify every repaired material, substituting the later accepted Vintage layer fix.
materials=json.loads((n/'material-repair-test/validation.json').read_text())
for row in materials['meshes']:
 if row['component']=='Vintage':continue
 key=row['file'].replace('components/NoveltyIAF/','components/NoveltyNuka/')
 assert sha(items[key])==row['sha256'],key
vintage=json.loads((n/'vintage-inner-layer-fix-accepted.json').read_text())
for row in vintage['meshes']:assert sha(items['components/Vintage/'+row['file']])==row['sha256']
whiskey=json.loads((n/'fallout3-whiskey-accepted.json').read_text())
for key,h in whiskey['files'].items():assert sha(items['components/Fallout3Whiskey/'+key])==h
# Source snapshot equals accepted production except version and log-label cleanup.
source=root/'release/2.0/source/desktop/native/src';rc=R/'source/desktop/native/src'
for p in source.iterdir():
 if p.is_file():
  expected=p.read_text().replace('Native-only test:','Native optics:') if p.suffix=='.cpp' else None
  if p.name=='Plugin.cpp':expected=expected.replace('0x02000000','0x02010000').replace('v.PluginVersion({2,0,0,0})','v.PluginVersion({2,1,0,0})').replace('DynamicBottleLiquids 2.0 loaded.','DynamicBottleLiquids 2.1 RC1 loaded.')
  if expected is not None:assert (rc/p.name).read_text()==expected,p.name
  else:assert (rc/p.name).read_bytes()==p.read_bytes(),p.name
for ui in ['fusion','mf','dmui','none']:
 p=R/f'build-{ui}/DynamicBottleLiquids.dll';data=p.read_bytes()
 assert p.stat().st_mtime>(rc/'Plugin.cpp').stat().st_mtime
 assert b'[DEBUG-ssr' not in data and b'2.1 RC1 loaded' in data
 items[f'components/ui-{ui}/F4SE/Plugins/DynamicBottleLiquids.dll']=data
ET.register_namespace('xsi','http://www.w3.org/2001/XMLSchema-instance')
x=ET.fromstring(items['fomod/ModuleConfig.xml']);x.find('moduleName').text='Dynamic Bottle Liquids 2.1 RC1 (OG / NG / AE)'
for plugins in x.iter('plugins'):
 for plugin in list(plugins):
  if any(f.get('name')=='Runtime' and f.text=='vr' for f in plugin.findall('./conditionFlags/flag')):plugins.remove(plugin)
add_whiskey_option(x)
items['fomod/images/Fallout3Whiskey.jpg']=(R/'assets/Fallout3Whiskey.jpg').read_bytes()
ET.indent(x);items['fomod/ModuleConfig.xml']=b'<?xml version="1.0" encoding="utf-8"?>\n'+ET.tostring(x,encoding='utf-8')
info=ET.fromstring(items['fomod/info.xml']);info.find('Version').text='2.1 RC1';ET.indent(info);items['fomod/info.xml']=b'<?xml version="1.0" encoding="utf-8"?>\n'+ET.tostring(info,encoding='utf-8')
for node in x.iter():
 if node.tag in ('file','folder'):
  path=node.get('source').replace('\\','/');assert path in items or any(k.startswith(path+'/') for k in items),path
 if node.tag=='image':assert node.get('path').replace('\\','/') in items
# Historical embedded core release notes are replaced with the candidate instructions.
for key in list(items):
 if key.startswith('components/desktop/docs/DynamicBottleLiquids/') and 'README' in key.upper():del items[key]
for name in ['README.txt','CHANGELOG.txt','AUDIT.txt']:items[name]=(R/name).read_bytes()
for key in list(items):
 if key.endswith('THIRD_PARTY.txt'):items[key]=items[key].replace(b'Dynamic Bottle Liquids 1.3:',b'Dynamic Bottle Liquids 2.1 RC1:')
 if key=='components/ModernNuka/docs/DBL-Modern-Nuka/README.txt':items[key]=items[key].replace(b'DBL 1.3',b'DBL 2.1 RC1')
items['components/desktop/docs/DynamicBottleLiquids/README-2.1-RC1.txt']=items['README.txt']
items['components/desktop/docs/DynamicBottleLiquids/CHANGELOG-2.1-RC1.txt']=items['CHANGELOG.txt']
assert 'Mode=Basic' in items['components/desktop/F4SE/Plugins/DynamicBottleLiquids.ini'].decode()
assert len(items)==len({k.lower() for k in items}), 'Case-insensitive archive collision'
assert not any('/UserSettings/' in k or k.endswith(('/tuning.json','/ui.json','.esp','.pdb','.obj')) or 'SSRFix' in k for k in items)
for ui in ['fusion','mf','dmui','none']:
 assert sum(k.endswith('/DynamicBottleLiquids.dll') and (k.startswith('components/desktop/') or k.startswith(f'components/ui-{ui}/')) for k in items)==1
report=dict(version='2.1 RC1',published=False,installed=False,runtimes=['1.10.163','1.10.984','1.11.240'],ui_variants=['fusion','mf','dmui','none'],vr_included=False,accepted_fixes=['ice condensation and shell removal','Supa Yum and 39 material repairs','Vintage inner layer','Fallout 3 Style Whiskey cork/collar','Visible Favorites','F4R preview hook order','general inspection fallback','ESP-free mixed drinks'],pending=['permanent SSR Fix compatibility','original public 2.0/DLSS report','user RC validation across runtime/UI combinations'],logging='Basic',diagnostic_payload=False,source_changes='version metadata and historical log labels only',files=len(items),base_sha256=sha(base.read_bytes()),dll_sha256={ui:sha(items[f'components/ui-{ui}/F4SE/Plugins/DynamicBottleLiquids.dll']) for ui in ['fusion','mf','dmui','none']})
# Stage first for native catalog and XML schema validation; finalize only afterward.
for k,v in items.items():
 p=R/'installer'/k;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(v)
(R/'assembly-validation.json').write_text(json.dumps(report,indent=2)+'\n')
if '--finalize' in sys.argv:
 cases=json.loads((R/'catalog-validation.json').read_text());assert all(c['passed'] for c in cases)
 report['native_startup_configurations_passed']=len(cases)
 report['offline_tests']='inspection helper/adapter, Visible Favorites, OG/NG/AE address audit, mixed-drink model selection, FOMOD schema and encoding'
 items['VALIDATION.json']=(json.dumps(report,indent=2)+'\n').encode()
 items['manifest.json']=json.dumps({k:sha(v) for k,v in sorted(items.items())},indent=2).encode()
 archive=R/'DynamicBottleLiquids-2.1-RC1-OG-NG-AE-FOMOD.zip'
 with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
  for k,v in sorted(items.items()):z.writestr(k,v)
 with zipfile.ZipFile(archive) as z:
  assert z.testzip() is None
  for k,h in json.loads(z.read('manifest.json')).items():assert sha(z.read(k))==h
 (R/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
 (R/'SHA256SUMS.txt').write_text(sha(archive.read_bytes())+'  '+archive.name+'\n')
 print(archive)
else:print('PASS: accepted asset hashes, source diff allowlist, DLL diagnostic exclusion, installer references; staged for catalog validation.')
