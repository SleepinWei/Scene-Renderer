"""Independent groom sampling, frame and package geometry checks (stdlib only)."""
import math
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from pt_groom import surface_roots, ribbons, dot, length, sub, cross, read_ascii_ply
from create_pt_groom_fixtures import hairstyle, sweater, yarn_swatch


class GroomTests(unittest.TestCase):
    def test_area_sampling(self):
        vertices=[[0,0,0],[1,0,0],[0,1,0],[3,0,0],[5,0,0],[3,2,0]]
        faces=[[0,1,2],[3,4,5]];roots=surface_roots(vertices,faces,10000,41)
        fraction=sum(r['triangle']==0 for r in roots)/len(roots)
        self.assertAlmostEqual(fraction,.2,delta=.02)
        means=[sum(r['barycentric'][i] for r in roots)/len(roots) for i in range(3)]
        for mean in means:self.assertAlmostEqual(mean,1/3,delta=.01)
        for r in roots:
            self.assertAlmostEqual(sum(r['barycentric']),1)
            self.assertAlmostEqual(length(r['normal']),1)
            self.assertGreaterEqual(min(r['barycentric']),0)
        self.assertEqual(roots[:40],surface_roots(vertices,faces,40,41))
        masked=surface_roots(vertices,faces,100,2,lambda p,n:p[0]>2)
        self.assertTrue(all(r['triangle']==1 for r in masked))

    def test_ribbon_frame_and_taper(self):
        curve=dict(points=[[0,0,0],[0,.2,.01],[.03,.5,.07],[.04,.8,.12]],radius=.02)
        triangles=list(ribbons([curve],[1,1,3]))
        self.assertEqual(len(triangles),6)
        for tri in triangles:
            self.assertGreater(length(cross(sub(tri[1][0],tri[0][0]),sub(tri[2][0],tri[0][0]))),1e-8)
            for p,n,uv,t in tri:
                self.assertAlmostEqual(length(n),1);self.assertAlmostEqual(length(t[:3]),1)
                self.assertAlmostEqual(dot(n,t[:3]),0)
                self.assertIn(uv[1],(0,1));self.assertGreaterEqual(uv[0],0);self.assertLessEqual(uv[0],1)
        start=[v for tri in triangles for v in tri if v[2][0]==0]
        end=[v for tri in triangles for v in tri if v[2][0]==1]
        self.assertAlmostEqual(length(sub(start[0][0],curve['points'][0])),.02)
        self.assertAlmostEqual(length(sub(end[0][0],curve['points'][-1])),.02*.04)
        for bad in [dict(points=[[0,0,0]],radius=.01),dict(points=[[0,0,0],[0,1,0]],radius=-1),dict(points=[[0,0,0],[0,0,0]],radius=.01)]:
            with self.assertRaises(ValueError):list(ribbons([bad],[0,0,3]))

    def test_renderable_models(self):
        import json
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            for name,create in [('sweater',sweater),('hair',lambda p:hairstyle(p,32)),('yarn',yarn_swatch)]:
                path=create(root/name);scene=json.loads(path.read_text());geometry=(path.parent/'geometry.bin').read_bytes();tangents=(path.parent/'tangents.bin').read_bytes()
                self.assertEqual(geometry[:8],b'PTMESH01');self.assertEqual(tangents[:8],b'PTTANG01')
                for draw in scene['draws']:
                    self.assertEqual(draw['vertices']%3,0)
                    self.assertLessEqual(draw['offset']+draw['vertices']*32,len(geometry))
                    self.assertLessEqual(draw['tangents_offset']+draw['vertices']*16,len(tangents))
                if name=='hair':
                    groom=json.loads((path.parent/'groom.json').read_text());self.assertEqual(len(groom['strands']),32)
                    for strand in groom['strands']:
                        self.assertEqual(len(strand['points']),29)
                        for x,y,z in strand['points']:
                            if y>2.25:self.assertGreaterEqual((x/.56)**2+((y-2.25)/.8)**2+(z/.48)**2,.999)
                if name=='sweater':self.assertEqual(scene['materials'][0]['bsdf_model'],'knit')

    def test_invalid_ply(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'bad.ply'
            path.write_text('ply\nformat ascii 1.0\nelement vertex 3\nelement face 1\nend_header\n0 0 0\n1 0 0\n0 1 0\n3 0 1 9\n')
            with self.assertRaises(ValueError):read_ascii_ply(path)


if __name__=='__main__':unittest.main()
