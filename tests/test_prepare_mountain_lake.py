import sys
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from prepare_mountain_lake import grid_from_vertices


class MountainLakeConversionTests(unittest.TestCase):
    def setUp(self):
        # Affine ground with a non-square grid and one duplicated mesh seam.
        x, y = np.meshgrid([-4., 0., 4.], [-2., 2.])
        self.positions = np.column_stack((x.ravel(), y.ravel(), (2*x+3*y+20).ravel())).astype(np.float32)
        self.uv = np.column_stack(((x.ravel()+4)/8, (y.ravel()+2)/4))

    def test_geometry_and_uv_survive_mesh_splits(self):
        order = [5, 0, 2, 4, 1, 3, 2]
        field, xs, ys = grid_from_vertices(self.positions[order], self.uv[order])
        np.testing.assert_array_equal(field, [[6,14,22], [18,26,34]])
        np.testing.assert_array_equal(xs, [-4,0,4])
        np.testing.assert_array_equal(ys, [-2,2])

    def test_missing_sample_rejected(self):
        with self.assertRaisesRegex(ValueError, 'missing'):
            grid_from_vertices(self.positions[[0,1,2,3,5]], self.uv[[0,1,2,3,5]])

    def test_overhang_and_mirrored_uv_rejected(self):
        conflict = self.positions[2].copy(); conflict[2] += 1
        with self.assertRaisesRegex(ValueError, 'disagree'):
            grid_from_vertices(np.vstack((self.positions,conflict)),np.vstack((self.uv,self.uv[2])))
        with self.assertRaisesRegex(ValueError, 'UV'):
            grid_from_vertices(self.positions, 1-self.uv)


if __name__ == '__main__':
    unittest.main()
