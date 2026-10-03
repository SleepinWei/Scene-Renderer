import sys
import unittest
from pathlib import Path
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from prepare_beach_material import mip_atlas, shoreline_mask


class BeachMaterialTests(unittest.TestCase):
    def test_albedo_mips_preserve_linear_energy(self):
        checker=np.zeros((4,4,3));checker[::2,::2]=1;checker[1::2,1::2]=1
        atlas=mip_atlas(checker,'albedo')
        # A black/white checker should average to linear 0.5, not encoded 0.5.
        np.testing.assert_allclose((atlas[4:6,:2,:3]/255)**2.2,.5,atol=.004)
        np.testing.assert_allclose(atlas[6,0,:3],186,atol=1)

    def test_normal_mips_remain_unit_length(self):
        normal=np.full((4,4,3),[.8,.5,.9]);normal[::2,:,0]=.2
        atlas=mip_atlas(normal,'normal')
        for row,size in [(0,4),(4,2),(6,1)]:
            vectors=atlas[row:row+size,:size,:3]/255*2-1
            np.testing.assert_allclose(np.linalg.norm(vectors,axis=-1),1,atol=.01)

    def test_shore_distance_is_local_and_monotonic(self):
        height=np.full((21,21),10.);height[:,0]=0
        mask=shoreline_mask(height,1,200,full_width=20,outer_width=60)
        np.testing.assert_allclose(mask[:,:3],1)
        np.testing.assert_allclose(mask[:,6:],0)
        self.assertTrue(np.all(np.diff(mask[10])<=0))
        self.assertGreater(mask[10,4],0)
        self.assertLess(mask[10,4],1)
        empty=shoreline_mask(np.ones((5,5)),0,40,1,10)
        np.testing.assert_array_equal(empty,0)


if __name__ == '__main__':
    unittest.main()
