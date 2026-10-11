"""Dense standalone hair demo: guide determinism, bounds and view packaging."""
import json
import math
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'tools'))
from create_pt_hair_demo import create_demo, dense_groom, VIEWS


class HairDemoTests(unittest.TestCase):
    def test_guides(self):
        strands = dense_groom(256)
        self.assertEqual(strands, dense_groom(256))
        self.assertNotEqual(strands, dense_groom(256, seed=20))
        self.assertGreater(len({s['lock'] for s in strands}), 40)
        radii = [s['radius'] for s in strands]
        self.assertGreaterEqual(min(radii), .0015*.8)
        self.assertLessEqual(max(radii), .0015*1.2)
        for strand in strands:
            self.assertEqual(len(strand['points']), 35)
            root = strand['points'][0]
            self.assertGreater(root[1], 2.5)
            self.assertLess(strand['points'][-1][1], 1.3)
            for x, y, z in strand['points']:
                self.assertTrue(all(math.isfinite(v) for v in (x, y, z)))
                if y > 2.25:
                    self.assertGreater((x/.56)**2+((y-2.25)/.8)**2+(z/.48)**2, 1)
        for count, radius in ((0, .0015), (100001, .0015), (1, float('nan')), (1, .1)):
            with self.assertRaises(ValueError):
                dense_groom(count, radius=radius)

    def test_views(self):
        with tempfile.TemporaryDirectory() as tmp:
            for view, (eye, _) in VIEWS.items():
                path = create_demo(Path(tmp)/view, count=12, view=view)
                scene = json.loads(path.read_text())
                groom = json.loads((path.parent/'groom.json').read_text())
                self.assertEqual(groom['reference_camera'], eye)
                self.assertEqual(scene['source']['hair_triangles'], 12*68)
                self.assertEqual(len(groom['strands']), 12)
                hair_draws = [d for d in scene['draws'] if scene['materials'][d['material']].get('bsdf_model') == 'hair']
                self.assertEqual(sum(d['vertices'] for d in hair_draws), 12*68*3)
                self.assertEqual(scene['camera']['world'][12:15], eye)


if __name__ == '__main__':
    unittest.main()
