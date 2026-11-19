from pathlib import Path
import itertools,json,subprocess,tempfile,concurrent.futures
R=Path(__file__).resolve().parent;components=R/'installer/components';scratch=R/'catalog-test-temp';scratch.mkdir(exist_ok=True)
cache={}
for d in components.iterdir():
 root=d/'F4SE/Plugins/DynamicBottleLiquids'
 cache[d.name]={p.relative_to(root):p.read_bytes() for p in root.rglob('*.json') if p.is_file()}
optional=['Sunset','AtomicShine','Vintage','WastelandImports','ModernNuka','Fallout3Whiskey']
cases=[]
for nuka in [None,'NoveltyNuka','NukaProject','NukaColaClassic','NeoClassic','Oppressor']:
 for bits in itertools.product([False,True],repeat=len(optional)):
  for acid in [False,True]:cases.append(['desktop']+([nuka] if nuka else [])+[x for x,on in zip(optional,bits) if on]+(['acid-soaker'] if acid else []))
def run(case):
 with tempfile.TemporaryDirectory(dir=scratch) as tmp:
  root=Path(tmp)
  for component in case:
   for name,data in cache[component].items():
    p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
  (root/'UserSettings').mkdir(exist_ok=True);(root/'UserSettings/migration-v1.complete').touch()
  before={p.relative_to(root):p.read_bytes() for p in (root/'UserSettings').rglob('*') if p.is_file()}
  r=subprocess.run([str(R/'build-common/CatalogTests.exe'),str(root)],capture_output=True,text=True)
  after={p.relative_to(root):p.read_bytes() for p in (root/'UserSettings').rglob('*') if p.is_file()}
  assert before==after
  return {'components':case,'passed':r.returncode==0,'result':(r.stdout+r.stderr).strip()}
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:results=list(pool.map(run,cases))
(R/'catalog-validation.json').write_text(json.dumps(results,indent=2)+'\n')
failed=[r for r in results if not r['passed']]
print(f'{len(results)-len(failed)}/{len(results)} native startup configurations passed')
for r in failed[:10]:print(r)
assert not failed
