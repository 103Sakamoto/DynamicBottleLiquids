import copy
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile
from author_tool import core, preparation
from nif_document import parse, save, object_refs
from nif_mesh import Reader

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'experiments/des-drink-patches-v2/input/sunset/Meshes/DesSSSS/SSSSClassic/DesSSSSClassicBottle.nif'


@unittest.skipUnless(SOURCE.exists(),'Local original bottle fixture unavailable')
class PreparationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.model=core.load_mesh(SOURCE);cls.liquid=next(m for m in cls.model['meshes'] if m['block']==9)
        cls.result=preparation.calculate(cls.model,cls.liquid)

    def test_detects_full_bottle_and_replaces_liquid_layers(self):
        p=self.result['preparation'];self.assertEqual(p['target_block'],2);self.assertEqual(p['hidden_blocks'],[9,13])
        self.assertGreater(self.result['rings'][-1][0],20)
        ambiguous=copy.deepcopy(self.model);duplicate=copy.deepcopy(ambiguous['meshes'][0]);duplicate['block']=999
        ambiguous['meshes'].append(duplicate)
        with self.assertRaisesRegex(ValueError,'equally plausible'):preparation.detect_target(ambiguous,self.liquid)

    def test_original_nif_export_preserves_other_blocks(self):
        project=core.new_project('automatic')
        core.add_profile(project,self.model,9,2,self.result,'sunset','Meshes/test/Bottle.nif','cola')
        with tempfile.TemporaryDirectory() as folder:
            output=core.export_patch(project,Path(folder)/'patch.zip')
            with zipfile.ZipFile(output) as archive:after=parse(archive.read('Meshes/test/Bottle.nif'))
        before=parse(SOURCE.read_bytes())
        for i,chunk in enumerate(before[5]):
            if i not in (2,9,13):self.assertEqual(chunk,after[5][i])
        for i in (9,13):
            end=object_refs(after[5][i],av=True)[0]
            self.assertEqual(struct.unpack_from('<I',after[5][i],end-60)[0]&1,1)

    def test_lighting_conversion_adds_effect_and_preserves_geometry(self):
        # Use a real Lighting shader/texture set from the same NIF on its full glass geometry.
        parts=list(parse(SOURCE.read_bytes()));glass=bytearray(parts[5][2]);liquid=parts[5][9]
        end=object_refs(glass,av=True)[0];le=object_refs(liquid,av=True)[0]
        glass[end+20:end+24]=liquid[le+20:le+24];parts[5][2]=bytes(glass)
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'lighting.nif';save(parts,path);model=core.load_mesh(path)
            target=next(m for m in model['meshes'] if m['block']==2)
            plan,_=preparation.plan(model,next(m for m in model['meshes'] if m['block']==9),target,
                                    SOURCE.parents[3])
            original=list(parts[5]);preparation.apply(parts,target,plan,'DynamicBottleLiquids:test')
            output=Path(folder)/'output.nif';save(parts,output);updated=core.load_mesh(output)
            actual=next(m for m in updated['meshes'] if m['block']==2)
            self.assertEqual(actual['shader'],'BSEffectShaderProperty')
            for key in ('positions','indices','normals','uvs'):self.assertEqual(target[key],actual[key])
            self.assertGreater(len(parts[5]),len(original))
            for i,chunk in enumerate(original):
                if i not in (2,9,13):self.assertEqual(chunk,parts[5][i])


if __name__=='__main__':unittest.main()
