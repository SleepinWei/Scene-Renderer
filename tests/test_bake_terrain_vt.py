import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
import numpy as np

spec = importlib.util.spec_from_file_location('bake', Path(__file__).resolve().parents[1]/'tools/bake_terrain_vt.py')
bake = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bake)


class TerrainBakeTest(unittest.TestCase):
    def test_height_aprons_mips_and_endpoints(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            field = (.3+.4*np.linspace(0,1,128)[None,:]-.2*np.linspace(0,1,97)[:,None]).astype('<f4')
            field.tofile(path/'raw')
            bake.write_pack(path, 'height', bake.height_levels(path/'raw',128,97),True)
            info = json.loads((path/'height.json').read_text())
            self.assertEqual(info['extent'],128)
            tiles = np.fromfile(path/info['data'],dtype='<f4').reshape(5,68,68,4)
            # Adjacent aprons overlap four texels from the same virtual field.
            np.testing.assert_array_equal(tiles[0,:,64:68],tiles[1,:,0:4])
            np.testing.assert_array_equal(tiles[0,64:68,:],tiles[2,0:4,:])
            x=np.linspace(0,1,64)[None,:]
            y=np.linspace(0,1,64)[:,None]
            np.testing.assert_allclose(tiles[4,2:66,2:66,0],.3+.4*x-.2*y,atol=2e-7)
            self.assertAlmostEqual(info['minimum'],float(field.min()))
            self.assertAlmostEqual(info['maximum'],float(field.max()))

    def test_gamma_and_normal_mips(self):
        from PIL import Image
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)
            image=np.zeros((128,128,4),dtype=np.uint8)
            image[...,3]=255
            image[:,1::2,:3]=255
            Image.fromarray(image).save(path/'albedo.png')
            levels=list(bake.material_levels([path/'albedo.png',None,None,None,None],128))
            value=int(levels[1][0][0][10,10,0])
            self.assertTrue(185<=value<=187, 'mip averaged encoded albedo rather than linear energy')
            self.assertEqual(int(levels[1][0][2][10,10,2]),0, 'missing metallic map must produce dielectric terrain')
            normal=levels[1][0][1][10,10,:3]/255*2-1
            self.assertAlmostEqual(float(np.linalg.norm(normal)),1.,places=3)
            bake.write_pack(path,'material',iter(levels),False)
            info=json.loads((path/'material.json').read_text())
            self.assertEqual((path/info['data']).stat().st_size,5*5*68*68*4)

    def test_bad_height_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'raw'
            np.zeros(2,dtype='<f4').tofile(path)
            with self.assertRaises(ValueError):list(bake.height_levels(path,2,2))
            np.array([0,1,0,1],dtype='<f4').tofile(path)
            bake.write_pack(path.parent,'height',bake.height_levels(path,2,2),True)
            manifest=(path.parent/'height.json').read_bytes()
            np.array([0,1,np.nan,0],dtype='<f4').tofile(path)
            with self.assertRaises(ValueError):bake.write_pack(path.parent,'height',bake.height_levels(path,2,2),True)
            self.assertEqual((path.parent/'height.json').read_bytes(),manifest)
            self.assertEqual(list(path.parent.glob('*.tmp')),[])


if __name__=='__main__':
    unittest.main()
