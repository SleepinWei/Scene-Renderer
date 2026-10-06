"""Generate a small normal-map / thin-leaf comparison package inside Blender.

Run Blender --background --factory-startup --python this_file -- OUTPUT_DIR,
then prepare_pt_comparison.py --profile lambert-leaves --normals and render both
engines. No downloaded assets; fixed camera, smooth normals and mirrored scale.
"""
import importlib.util
from pathlib import Path
import sys
import bpy
from mathutils import Vector

root=Path(__file__).resolve().parents[2]
output=Path(sys.argv[sys.argv.index('--')+1]).resolve()
output.mkdir(parents=True,exist_ok=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
scene=bpy.context.scene

def material(name,translucent=False):
    m=bpy.data.materials.new(name);m.use_nodes=True;t=m.node_tree;t.nodes.clear()
    out=t.nodes.new('ShaderNodeOutputMaterial');r=t.nodes.new('ShaderNodeBsdfDiffuse');r.inputs['Color'].default_value=(.2,.3,.4,1)
    n=t.nodes.new('ShaderNodeNormalMap');n.inputs['Color'].default_value=(.65,.6,.96,1);t.links.new(n.outputs[0],r.inputs['Normal'])
    if translucent:
        tr=t.nodes.new('ShaderNodeBsdfTranslucent');tr.inputs['Color'].default_value=(.6,.2,.1,1);t.links.new(n.outputs[0],tr.inputs['Normal'])
        add=t.nodes.new('ShaderNodeAddShader');t.links.new(r.outputs[0],add.inputs[0]);t.links.new(tr.outputs[0],add.inputs[1]);t.links.new(add.outputs[0],out.inputs['Surface'])
    else:t.links.new(r.outputs[0],out.inputs['Surface'])
    return m

opaque=material('Smooth normal fixture');leaf=material('Thin diffuse normal fixture',True)
for i,x in enumerate((-1.6,0,1.6)):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32,ring_count=16,radius=.7,location=(x,0,0))
    obj=bpy.context.object;obj.name=f'Normal sphere {i}'
    obj.scale=((-1 if i==1 else 1)*.85,1.2,.75);obj.rotation_euler=(.12,.2*i,.1)
    obj.data.materials.append(leaf if i==2 else opaque)
    for p in obj.data.polygons:p.use_smooth=True
floor=bpy.data.materials.new('Floor');floor.use_nodes=True
principal=next(n for n in floor.node_tree.nodes if n.type=='BSDF_PRINCIPLED');principal.inputs['Base Color'].default_value=(.3,.3,.3,1)
bpy.ops.mesh.primitive_plane_add(size=20,location=(0,0,-.8));bpy.context.object.data.materials.append(floor)
world=bpy.data.worlds.new('Constant world');world.use_nodes=True
background=next(n for n in world.node_tree.nodes if n.type=='BACKGROUND');background.inputs['Color'].default_value=(.15,.2,.3,1);scene.world=world
bpy.ops.object.light_add(type='POINT',location=(1,1.5,2));bpy.context.object.data.energy=180;bpy.context.object.data.shadow_soft_size=0
bpy.ops.object.camera_add(location=(0,-7,2.2));camera=bpy.context.object
camera.rotation_euler=(Vector((0,0,-.1))-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.lens=45;scene.camera=camera
scene.render.resolution_x=192;scene.render.resolution_y=96
bpy.ops.wm.save_as_mainfile(filepath=str(output/'fixture.blend'))
spec=importlib.util.spec_from_file_location('pt_export',root/'tools/export_blender_pt.py')
export=importlib.util.module_from_spec(spec);spec.loader.exec_module(export)
sys.argv=['blender','--','--output',str(output),'--source-name','appearance-fixture','--atlas-size','256','--particle-limit','-1','--water-profile','source']
export.main()
