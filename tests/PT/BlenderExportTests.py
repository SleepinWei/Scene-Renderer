"""Optional integration test, run with Blender --background --python this_file.

No source assets required. Exercises actual Cycles EMIT baking and the exporter,
checking raw PNG scalar bytes independently of Blender's color management.
"""
import importlib.util
import json
from pathlib import Path
import struct
import sys
import zlib
import bpy
import numpy as np

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('pt_export',ROOT/'tools/export_blender_pt.py')
export=importlib.util.module_from_spec(spec);spec.loader.exec_module(export)
output=Path(sys.argv[sys.argv.index('--')+1]).resolve();output.mkdir(parents=True,exist_ok=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
scene=bpy.context.scene
material=bpy.data.materials.new('Scalar fixture');material.use_nodes=True
tree=material.node_tree;principal=next(n for n in tree.nodes if n.type=='BSDF_PRINCIPLED')
principal.inputs['Base Color'].default_value=(.25,.5,.75,1)
for name,value in (('Roughness',.25),('Metallic',.75),('Alpha',.5)):
    node=tree.nodes.new('ShaderNodeValue');node.outputs[0].default_value=value;tree.links.new(node.outputs[0],principal.inputs[name])
bpy.ops.mesh.primitive_plane_add(size=2);bpy.context.object.data.materials.append(material)
# A source water sheet with an explicit normal and homogeneous absorption.
water=bpy.data.materials.new('water');water.use_nodes=True;t=water.node_tree;t.nodes.clear()
out=t.nodes.new('ShaderNodeOutputMaterial');glass=t.nodes.new('ShaderNodeBsdfGlass');glass.inputs['IOR'].default_value=1.1;glass.inputs['Roughness'].default_value=.08
normal=t.nodes.new('ShaderNodeNormalMap');normal.inputs['Color'].default_value=(.65,.7,.95,1);t.links.new(normal.outputs[0],glass.inputs['Normal']);t.links.new(glass.outputs[0],out.inputs['Surface'])
volume=t.nodes.new('ShaderNodeVolumeAbsorption');volume.inputs['Color'].default_value=(.8,.9,.95,1);volume.inputs['Density'].default_value=2;t.links.new(volume.outputs[0],out.inputs['Volume'])
assert np.allclose(export.describe(water)[0]['absorption'],[.4,.2,.1]),'Blender volume coefficient conversion changed'
# A stochastic Mix volume is not equivalent to adding both coefficients.
mix_volume=t.nodes.new('ShaderNodeMixShader');t.links.new(volume.outputs[0],mix_volume.inputs[1]);t.links.new(volume.outputs[0],mix_volume.inputs[2]);t.links.new(mix_volume.outputs[0],out.inputs['Volume'])
assert 'absorption' not in export.describe(water)[0] and any('volume graph omitted' in issue for issue in export.describe(water)[2]),'Unsupported volume mixture was silently summed'
t.links.new(volume.outputs[0],out.inputs['Volume'])
bpy.ops.mesh.primitive_plane_add(size=2,location=(4,0,0));bpy.context.object.data.materials.append(water)
# Asymmetric source UV texture detects accidental top-down PNG / bottom-up UV mismatch.
orientation=bpy.data.materials.new('UV orientation');orientation.use_nodes=True
principal=next(n for n in orientation.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
image=bpy.data.images.new('Asymmetric UV',width=4,height=4,float_buffer=True);image.colorspace_settings.name='Non-Color'
pixels=np.zeros((4,4,4),dtype=np.float32);pixels[:2,:,1]=.5;pixels[2:,:,0]=.5;pixels[:,:,3]=1;image.pixels.foreach_set(pixels.ravel())
node=orientation.node_tree.nodes.new('ShaderNodeTexImage');node.image=image;node.interpolation='Closest';orientation.node_tree.links.new(node.outputs['Color'],principal.inputs['Base Color'])
bpy.ops.mesh.primitive_plane_add(size=2,location=(8,0,0));bpy.context.object.data.materials.append(orientation)
# Actual procedural Bump baking must survive independently of Normal Map nodes.
bump_material=bpy.data.materials.new('Bump fixture');bump_material.use_nodes=True;t=bump_material.node_tree
principal=next(n for n in t.nodes if n.type=='BSDF_PRINCIPLED');bump=t.nodes.new('ShaderNodeBump');bump.inputs['Strength'].default_value=1;bump.inputs['Distance'].default_value=.2
noise=t.nodes.new('ShaderNodeTexNoise');noise.inputs['Scale'].default_value=3;t.links.new(noise.outputs['Fac'],bump.inputs['Height']);t.links.new(bump.outputs['Normal'],principal.inputs['Normal'])
bpy.ops.mesh.primitive_plane_add(size=2,location=(16,0,0));bpy.context.object.data.materials.append(bump_material)
leaf=bpy.data.materials.new('Thin leaf fixture');leaf.use_nodes=True;t=leaf.node_tree;t.nodes.clear();out=t.nodes.new('ShaderNodeOutputMaterial');add=t.nodes.new('ShaderNodeAddShader');reflection=t.nodes.new('ShaderNodeBsdfDiffuse');transmission=t.nodes.new('ShaderNodeBsdfTranslucent');reflection.inputs['Color'].default_value=(.2,.3,.4,1);transmission.inputs['Color'].default_value=(.6,.2,.1,1);t.links.new(reflection.outputs[0],add.inputs[0]);t.links.new(transmission.outputs[0],add.inputs[1]);t.links.new(add.outputs[0],out.inputs['Surface'])
bpy.ops.mesh.primitive_plane_add(size=2,location=(20,0,0));bpy.context.object.data.materials.append(leaf)
# Vertex-instanced emitter hidden for render must still retain its children.
bpy.ops.mesh.primitive_cube_add(size=.2,location=(12,0,0));emitter=bpy.context.object;emitter.name='Hidden instancer';emitter.instance_type='VERTS';emitter.show_instancer_for_render=False
bpy.ops.mesh.primitive_plane_add(size=.1);child=bpy.context.object;child.parent=emitter;child.name='Instanced child'
bpy.ops.object.camera_add(location=(0,0,3));scene.camera=bpy.context.object
scene.render.resolution_x=scene.render.resolution_y=128
bpy.ops.wm.save_as_mainfile(filepath=str(output/'fixture.blend'))
sys.argv=['blender','--','--output',str(output),'--atlas-size','128','--particle-limit','-1','--water-profile','pool','--pool-depth','.25']
export.main()

def png(path):
    data=path.read_bytes();offset=8;compressed=b''
    while offset<len(data):
        count=struct.unpack('>I',data[offset:offset+4])[0];kind=data[offset+4:offset+8];chunk=data[offset+8:offset+8+count];offset+=count+12
        if kind==b'IHDR':w,h=struct.unpack('>II',chunk[:8])
        if kind==b'IDAT':compressed+=chunk
    rows=np.frombuffer(zlib.decompress(compressed),dtype=np.uint8).reshape(h,w*4+1)
    assert (rows[:,0]==0).all(),'Unexpected PNG filter'
    return rows[:,1:].reshape(h,w,4)

package=json.loads((output/'scene.json').read_text());mat=package['materials'][0]
base=png(output/mat['base_texture']);orm=png(output/mat['orm_texture'])
covered=base[:,:,0]>100
assert covered.sum()>1000,'Fixture atlas has insufficient coverage'
assert np.abs(base[covered,0].astype(int)-round(.25**(1/2.2)*255)).max()<=1,'Base color encoding changed'
assert np.abs(base[covered,3].astype(int)-128).max()<=1,'Alpha was gamma transformed or lost'
assert np.abs(orm[covered,1].astype(int)-64).max()<=1,'Roughness was gamma transformed or lost'
assert np.abs(orm[covered,2].astype(int)-191).max()<=1,'Metallic was gamma transformed or lost'
assert mat['opacity']==1,'Baked coverage would be multiplied twice'
assert package['export']['native_mask_atlases']==1,'Native mask UVs were fragmented by smart project'
opaque=png(output/'color-opaque-0000.png');assert (opaque[:,:,3]==255).all(),'Opaque atlas gaps became holes'
leaf=next(m for m in package['materials'] if m['name']=='Thin leaf fixture');assert leaf['bsdf_model']=='thin_diffuse' and leaf['ior']==0,'Translucent leaf became opaque or a dielectric volume'
r=png(output/leaf['base_texture'])[:,:,:3]/255;t=png(output/leaf['transmission_texture'])[:,:,:3]/255;mask=r[:,:,0]>.1
assert np.max(np.abs(r[mask]**2.2-[.2,.3,.4]))<.01 and np.max(np.abs(t[mask]**2.2-[.6,.2,.1]))<.01,'Reflection/transmission colors were merged, gamma transformed twice or lost'
assert (r[mask]**2.2+t[mask]**2.2<=1.01).all(),'Leaf R+T gained energy'
tangent_bytes=(output/package['tangents']).read_bytes();assert tangent_bytes[:8]==b'PTTANG01','Missing corner tangent sidecar'
for draw in package['draws']:
    if 'tangents_offset' not in draw:continue
    frames=np.frombuffer(tangent_bytes,dtype='<f4',count=draw['vertices']*4,offset=draw['tangents_offset']).reshape(-1,4)
    assert np.isfinite(frames).all() and np.isin(frames[:,3],[-1,0,1]).all(),'Invalid corner tangent frame'

# A masked glossy object must not be mistaken for an architectural glass sheet.
mask=bpy.data.materials.new('Mask fixture');mask.use_nodes=True;t=mask.node_tree;t.nodes.clear()
out=t.nodes.new('ShaderNodeOutputMaterial');mix=t.nodes.new('ShaderNodeMixShader');mix.inputs[0].default_value=.25
transparent=t.nodes.new('ShaderNodeBsdfTransparent');glossy=t.nodes.new('ShaderNodeBsdfGlossy')
t.links.new(transparent.outputs[0],mix.inputs[1]);t.links.new(glossy.outputs[0],mix.inputs[2]);t.links.new(mix.outputs[0],out.inputs['Surface'])
assert not export.describe(mask)[0]['thin_dielectric'],'Glossy alpha mask became glass'
fresnel=t.nodes.new('ShaderNodeFresnel');assert not export.describe(mask)[0]['thin_dielectric'],'Unused Fresnel node changed mask classification';t.links.new(fresnel.outputs[0],mix.inputs[0])
assert export.describe(mask)[0]['thin_dielectric'],'Angular transparent/glossy graph lost sheet classification'
assert not any(d['name']=='Hidden instancer' for d in package['draws']),'Hidden emitter geometry covered its instances'
assert sum(d['name']=='Instanced child' for d in package['draws'])>=8,'Hidden emitter lost visible instances'
pool=next(m for m in package['materials'] if m['name']=='water')
assert pool['ior']==1.333 and pool['bounded_volume'] and pool['absorption']==[.12,.035,.015],'Explicit pool profile lost'
assert 'normal_texture' in pool,'Source normal was omitted'
normal=png(output/pool['normal_texture']);covered=normal[:,:,2]>200
assert covered.sum()>1000 and np.ptp(normal[covered,:2])>20,'Normal bake is empty or gamma encoded'
bump=next(m for m in package['materials'] if m['name']=='Bump fixture');pixels=png(output/bump['normal_texture']);covered=pixels[:,:,2]>150
assert np.ptp(pixels[covered,:2])>20,'Procedural Bump node became a flat normal'
# Closedness and outward winding are checked independently of the exporter.
buffer=np.frombuffer((output/'geometry.bin').read_bytes()[8:],dtype='<f4').reshape(-1,8)
draw=next(d for d in package['draws'] if package['materials'][d['material']]['name']=='water')
points=buffer[(draw['offset']-8)//32:(draw['offset']-8)//32+draw['vertices'],:3].reshape(-1,3,3)
from collections import Counter
edges=Counter(tuple(sorted((tuple(a),tuple(b)))) for tri in points for a,b in zip(tri,np.roll(tri,-1,axis=0)))
assert set(edges.values())=={2},'Pool volume has open boundaries'
pool_rows=buffer[(draw['offset']-8)//32:(draw['offset']-8)//32+draw['vertices']].reshape(-1,3,8)
expected=np.array([.3,.4,.9]);expected/=np.linalg.norm(expected)
for triangle in pool_rows:
    n=triangle[:,3:6].mean(axis=0)
    if n[2]<.9:continue
    e1,e2=triangle[1,:3]-triangle[0,:3],triangle[2,:3]-triangle[0,:3];a,b=triangle[1,6:]-triangle[0,6:],triangle[2,6:]-triangle[0,6:];det=a[0]*b[1]-a[1]*b[0]
    tangent=(e1*b[1]-e2*a[1])/det;bitangent=(-e1*b[0]+e2*a[0])/det;tangent/=np.linalg.norm(tangent);bitangent/=np.linalg.norm(bitangent)
    uv=triangle[:,6:].mean(axis=0)%1;encoded=normal[min(int(uv[1]*128),127),min(int(uv[0]*128),127),:3]/255*2-1
    decoded=tangent*encoded[0]+bitangent*encoded[1]+n*encoded[2];decoded/=np.linalg.norm(decoded)
    assert np.linalg.norm(decoded-expected)<.03,'Reflected UV / normal green channel changed the physical normal'
assert abs(sum(np.dot(t[0],np.cross(t[1],t[2]))/6 for t in points)-1)<1e-5,'Pool volume depth/winding is incorrect'
draw=next(d for d in package['draws'] if package['materials'][d['material']]['name']=='UV orientation')
rows=buffer[(draw['offset']-8)//32:(draw['offset']-8)//32+draw['vertices']].reshape(-1,3,8)
color=png(output/package['materials'][draw['material']]['base_texture'])
for triangle in rows:
    point=triangle[:,:3].mean(axis=0);uv=triangle[:,6:].mean(axis=0)%1
    sample=color[min(int(uv[1]*128),127),min(int(uv[0]*128),127)]
    assert (sample[0]>sample[1])==(point[1]>0),'Source texture V was reflected against exported geometry'
print('Blender PT normal/volume/UV orientation and color/alpha/roughness/metallic bake and glass/mask classification passed',flush=True)
