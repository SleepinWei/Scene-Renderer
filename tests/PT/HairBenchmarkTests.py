"""Independent binary HAIR import, width preservation and PBRT export tests."""
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from pt_hair_file import read_hair
from pt_groom import Package, length, sub
from create_pt_hair_benchmarks import imported_ribbons, source_transform, read_obj, export_pbrt


def fixture(flags=31):
    header=struct.pack('<4sIIIIff3f88s',b'HAIR',2,5,flags,1,.1,.2,.3,.4,.5,b'test')
    segments=struct.pack('<2H',1,2) if flags&1 else b''
    points=struct.pack('<15f',0,0,0,0,1,0,1,0,0,1,1,0,1,2,0)
    widths=struct.pack('<5f',.1,.08,.1,.06,.02) if flags&4 else b''
    transparency=struct.pack('<5f',0,.1,.2,.3,.4) if flags&8 else b''
    colors=struct.pack('<15f',*([.1,.2,.3]*5)) if flags&16 else b''
    return header+segments+points+widths+transparency+colors


class HairBenchmarkTests(unittest.TestCase):
    def test_arrays_and_invalid_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'test.hair';p.write_bytes(fixture());h=read_hair(p)
            self.assertEqual(list(h.offsets),[0,2,5]);self.assertEqual(h.strand(1),[[1,0,0],[1,1,0],[1,2,0]])
            self.assertAlmostEqual(h.thickness[-1],.02);self.assertAlmostEqual(h.transparency[-1],.4)
            self.assertEqual(len(h.colors),15)
            bad=[fixture()[:100],fixture()[:-1],fixture()+b'x']
            for offset,value in ((0,b'BAD!'),(12,struct.pack('<I',32)),(8,struct.pack('<I',6)),(128,struct.pack('<H',9)),(128+4,struct.pack('<f',float('nan')))):
                data=bytearray(fixture());data[offset:offset+len(value)]=value;bad.append(data)
            for data in bad:
                p.write_bytes(data)
                with self.assertRaises(ValueError):read_hair(p)
            # Points-only default-segment fixture: same count in each strand.
            p.write_bytes(struct.pack('<4sIIIIff3f88s',b'HAIR',2,4,2,1,.1,.2,.3,.4,.5,b'')+struct.pack('<12f',*range(12)))
            h=read_hair(p);self.assertEqual(list(h.offsets),[0,2,4]);self.assertEqual(len(h.thickness),0)

    def test_source_width_no_taper(self):
        points=[[0,0,0],[0,1,0],[0,2,0]];widths=[.1,.07,.03]
        triangles=list(imported_ribbons([(points,widths)],[0,1,4]))
        self.assertEqual(len(triangles),4)
        for tri in triangles:
            for p,n,uv,t in tri:
                i=round(uv[0]*2)
                self.assertAlmostEqual(length(sub(p,points[i])),widths[i])
                self.assertAlmostEqual(length(n),1);self.assertAlmostEqual(length(t[:3]),1)
        with self.assertRaises(ValueError):list(imported_ribbons([(points,[.1])],[0,1,4]))

    def test_transform_and_obj(self):
        vertices=[[0,-2,-3],[4,2,7]];transform,scale,_=source_transform(vertices)
        self.assertAlmostEqual(scale,.26);self.assertAlmostEqual(transform(vertices[0])[1],.035)
        self.assertAlmostEqual(transform(vertices[1])[1],2.635)
        # The orientation-preserving axis permutation does not mirror the head.
        a=transform([1,0,0]);b=transform([0,1,0]);c=transform([0,0,1]);o=transform([0,0,0])
        from pt_groom import cross,dot
        self.assertGreater(dot(cross(sub(a,o),sub(b,o)),sub(c,o)),0)
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'head.obj';p.write_text('v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf -4 -3 -2 -1\n')
            _,faces=read_obj(p);self.assertEqual(faces,[[0,1,2],[0,2,3]])
            p.write_text('v 0 0 0\nf 0 0 0\n')
            with self.assertRaises(ValueError):read_obj(p)

    def test_reference_export(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Package(tmp,[0,1,4],[0,1,0])
            m=p.material(bsdf_model='lambert',base_color=[.5,.5,.5])
            p.quad([[0,0,0],[1,0,0],[1,1,0],[0,1,0]],[0,0,1],m)
            export_pbrt(p,[([[0,0,0],[0,1,0]],[.01,.005])],32,64)
            text=(Path(tmp)/'reference.pbrt').read_text();curves=(Path(tmp)/'curves.pbrt').read_text()
            self.assertTrue(text.startswith('Scale -1 1 1\nLookAt'))
            self.assertIn('pixelsamples" [64]',text)
            self.assertIn('0.217637641',text)  # Independent .5**2.2 conversion.
            self.assertIn('width0" [0.02]',curves);self.assertIn('width1" [0.01]',curves)
            self.assertIn('0 0.333333333 0',curves);self.assertIn('0 0.666666667 0',curves)


if __name__=='__main__':unittest.main()
