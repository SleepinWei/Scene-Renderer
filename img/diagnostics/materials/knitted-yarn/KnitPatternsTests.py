"""Dataset-independent tests for the YarnSim importer and periodic geometry."""
import math
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'tools'))
from create_pt_knitted import read_yarns, bspline, clip_segment, tiled_yarns, join_yarns, plies, nap


class KnitPatterns(unittest.TestCase):
    def test_import_rejects_corrupt_controls(self):
        text = 'Num Yarns: 1\n4\n0 0 0\n1 0 0\n2 0 0\n3 0 0\n'
        self.assertEqual(len(read_yarns(text)[0]), 4)
        for bad in [text.replace('Num Yarns', 'Curves'), text.replace('3 0 0', 'nan 0 0'),
                    text.replace('2 0 0', '2 0'), text+'surplus\n', text.replace('\n4\n', '\n3\n')]:
            with self.assertRaises(ValueError): read_yarns(bad)

    def test_uniform_bspline_linear_precision_and_join(self):
        controls = [[i, i*2, 4] for i in range(6)]
        sampled = bspline(controls, 8)
        self.assertEqual(len(sampled), 25)
        for i, p in enumerate(sampled):
            self.assertAlmostEqual(p[0], 1+i/8)
            self.assertAlmostEqual(p[1], 2*p[0]); self.assertAlmostEqual(p[2], 4)

    def test_periodic_crop_no_ghost_duplicates(self):
        curve = [[-7.5,0,0],[-2.5,0,0],[2.5,0,0],[7.5,0,0]]
        cells = tiled_yarns([curve,curve],2,1,8)
        segments = [(a,b) for c in cells for a,b in zip(c,c[1:])]
        keys = [tuple(sorted(tuple(round(v,5) for v in p) for p in pair)) for pair in segments]
        self.assertEqual(len(keys), len(set(keys)))
        for c in cells:
            for x,y,_ in c: self.assertTrue(-7.5<=x<=22.5); self.assertLessEqual(abs(y),10)
        self.assertEqual(len(cells),1)
        self.assertAlmostEqual(cells[0][0][0],-7.5);self.assertAlmostEqual(cells[0][-1][0],22.5)
        a,b = clip_segment([-10,0,-2],[10,0,2])
        self.assertEqual(a,[-7.5,0,-1.5]); self.assertEqual(b,[7.5,0,1.5])
        self.assertIsNone(clip_segment([0,12,0],[4,12,0]))

    def test_plies_and_nap_are_finite_deterministic(self):
        points = [[0,i*.01,0] for i in range(40)]
        result = plies(points,.01)
        self.assertEqual(len(result),3)
        for ply in result:
            for p,q in zip(ply,points):
                self.assertAlmostEqual(math.dist(p,q),.0048)
        strands = nap([points],.01,100)
        self.assertEqual(strands,nap([points],.01,100))
        for strand in strands:
            for p in strand['points']: self.assertTrue(all(math.isfinite(v) for v in p))

    def test_yarn_boundary_join_retains_disconnected_strands(self):
        fragments = [[[0,0,0],[1,0,0]], [[2,0,0],[1.00001,0,0]],
                     [[2,0,0],[3,0,0]], [[10,0,0],[11,0,0]]]
        joined = join_yarns(fragments)
        self.assertEqual(len(joined),2)
        self.assertEqual(joined[0],[[0,0,0],[1,0,0],[2,0,0],[3,0,0]])
        self.assertEqual(joined[1],fragments[3])
        overlap = join_yarns([[[0,0,0],[1,0,0],[2,0,0]],[[1,0,0],[2,0,0],[3,0,0]]],max_overlap=2)
        self.assertEqual(overlap,[[[0,0,0],[1,0,0],[2,0,0],[3,0,0]]])


if __name__ == '__main__': unittest.main()
