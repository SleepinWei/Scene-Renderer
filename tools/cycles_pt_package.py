"""Reconstruct controlled PT package closures in Cycles, without the source .blend.

Shared object-space meshes retain their transforms, split normals and UVs.
The cyclic XYZ permutation maps engine equirectangular coordinates to Cycles.
"""
import hashlib
import json
import math
from pathlib import Path
import bpy
import numpy as np
from mathutils import Matrix, Vector

WORLD=np.array(((0,0,1),(1,0,0),(0,1,0)),dtype=np.float32)


def matrix(values):return np.asarray(values,dtype=np.float32).reshape(4,4).T.copy()


def build(path,width,height):
    path=Path(path).resolve();root=path.parent;j=json.loads(path.read_text())
    if j['format']!='SceneRenderer.PT.v1':raise ValueError('Unsupported PT package')
    if not j.get('comparison'):raise ValueError('Prepare a controlled comparison package first')
    def asset(name):
        p=(root/name).resolve()
        if not p.is_relative_to(root):raise ValueError('Asset escapes package')
        expected=j['comparison']['assets_sha256'].get(name)
        if expected and hashlib.sha256(p.read_bytes()).hexdigest()!=expected:raise ValueError('Frozen asset checksum mismatch')
        return p
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    scene=bpy.context.scene;scene.name='PT matched package';scene.world=bpy.data.worlds.new('PT matched environment')
    textures={}
    def texture(tree,name):
        if name not in textures:
            source=bpy.data.images.load(str(asset(name)),check_existing=False);source.colorspace_settings.name='Non-Color'
            w,h=source.size;pixels=np.empty(w*h*4,dtype=np.float32);source.pixels.foreach_get(pixels)
            # Engine UV V=0 reads the first PNG scanline. Blender buffer V=0 is bottom-up.
            pixels=pixels.reshape(h,w,4)[::-1].copy()
            image=bpy.data.images.new('PT '+name,width=w,height=h,float_buffer=True);image.colorspace_settings.name='Non-Color';image.pixels.foreach_set(pixels.ravel())
            textures[name]=image;bpy.data.images.remove(source)
        node=tree.nodes.new('ShaderNodeTexImage');node.image=textures[name];node.interpolation='Linear';node.extension='REPEAT'
        return node
    def material(data,index):
        m=bpy.data.materials.new(f"PT {index}: {data['name']}");m.use_nodes=True;t=m.node_tree;t.nodes.clear();out=t.nodes.new('ShaderNodeOutputMaterial');geo=t.nodes.new('ShaderNodeNewGeometry')
        def bind(value,socket):
            if hasattr(value,'is_output'):t.links.new(value,socket)
            else:socket.default_value=value
        def op(operation,*values):
            n=t.nodes.new('ShaderNodeMath');n.operation=operation
            for value,target in zip(values,n.inputs):bind(value,target)
            return n.outputs[0]
        def multiply(color,other):
            n=t.nodes.new('ShaderNodeMixRGB');n.blend_type='MULTIPLY';n.inputs[0].default_value=1;bind(color,n.inputs[1]);bind(other,n.inputs[2]);return n.outputs[0]
        def mix(factor,a,b):
            n=t.nodes.new('ShaderNodeMixRGB');bind(factor,n.inputs[0]);bind(a,n.inputs[1]);bind(b,n.inputs[2]);return n.outputs[0]
        color=(*data['base_color'],1);alpha=data.get('opacity',1)
        if 'base_texture' in data:
            base=texture(t,data['base_texture']);color=multiply(base.outputs['Color'],color);alpha=op('MULTIPLY',base.outputs['Alpha'],alpha)
        gamma=t.nodes.new('ShaderNodeGamma');bind(color,gamma.inputs['Color']);gamma.inputs['Gamma'].default_value=2.2;color=gamma.outputs[0]
        metal=data.get('metallic',0);rough=data.get('roughness',.8);ior=data.get('ior',0);thin=data.get('thin_dielectric',False)
        if 'orm_texture' in data:
            orm=texture(t,data['orm_texture']);channels=t.nodes.new('ShaderNodeSeparateColor');t.links.new(orm.outputs['Color'],channels.inputs[0]);metal=channels.outputs['Blue'];rough=channels.outputs['Green']
        rough=op('MINIMUM',op('MAXIMUM',rough,0 if ior and not thin else .045),1)
        normal=None
        if 'normal_texture' in data:
            n=t.nodes.new('ShaderNodeNormalMap');n.uv_map='PT UV';n.inputs['Strength'].default_value=data.get('normal_strength',1);t.links.new(texture(t,data['normal_texture']).outputs['Color'],n.inputs['Color']);normal=n.outputs[0]
        def bsdf(kind,color,rough=None,normal=normal):
            n=t.nodes.new(kind);bind(color,n.inputs['Color'])
            if hasattr(n,'distribution'):n.distribution='GGX'
            if rough is not None:bind(rough,n.inputs['Roughness'])
            if normal is not None and 'Normal' in n.inputs:bind(normal,n.inputs['Normal'])
            return n
        if thin:
            # Parallel plate R=2F/(1+F), air on both sides; exact graph, not a single Glass boundary.
            dot=t.nodes.new('ShaderNodeVectorMath');dot.operation='DOT_PRODUCT';t.links.new(geo.outputs['Incoming'],dot.inputs[0]);t.links.new(geo.outputs['True Normal'],dot.inputs[1]);c=op('MINIMUM',op('ABSOLUTE',dot.outputs['Value']),1)
            ct=op('SQRT',op('MAXIMUM',op('SUBTRACT',1,op('DIVIDE',op('SUBTRACT',1,op('MULTIPLY',c,c)),ior*ior)),0))
            a=op('MULTIPLY',ior,c);b=op('MULTIPLY',ior,ct);rp=op('DIVIDE',op('SUBTRACT',a,ct),op('ADD',a,ct));rs=op('DIVIDE',op('SUBTRACT',c,b),op('ADD',c,b));f=op('MULTIPLY',.5,op('ADD',op('MULTIPLY',rp,rp),op('MULTIPLY',rs,rs)));r=op('DIVIDE',op('MULTIPLY',2,f),op('ADD',1,f))
            transparent=bsdf('ShaderNodeBsdfTransparent',color).outputs[0];glossy=bsdf('ShaderNodeBsdfGlossy',(1,1,1,1),0,geo.outputs['True Normal']).outputs[0]
            node=t.nodes.new('ShaderNodeMixShader');bind(r,node.inputs[0]);t.links.new(transparent,node.inputs[1]);t.links.new(glossy,node.inputs[2]);surface=node.outputs[0]
        elif ior:
            # Engine tints transmission only. Separate reflection/refraction GGX with exact Fresnel
            # is unavailable as a stock Glass input; require untinted glass in the matched profile.
            if data['base_color']!=[1,1,1]:raise ValueError('Matched solid dielectric requires white interface tint')
            if 'base_texture' in data:
                image=textures[data['base_texture']];pixels=np.empty(image.size[0]*image.size[1]*4,dtype=np.float32);image.pixels.foreach_get(pixels)
                if not np.all(pixels.reshape(-1,4)[:,:3]>=1-1e-6):raise ValueError('Matched solid dielectric requires a white interface texture')
            node=bsdf('ShaderNodeBsdfGlass',(1,1,1,1),rough);node.inputs['IOR'].default_value=ior;surface=node.outputs[0]
            absorption=np.asarray(data.get('absorption',[0,0,0]));scattering=np.asarray(data.get('scattering',[0,0,0]));volume=[]
            for kind,sigma in (('ShaderNodeVolumeAbsorption',absorption),('ShaderNodeVolumeScatter',scattering)):
                density=float(sigma.max())
                if density:
                    n=t.nodes.new(kind);n.inputs['Density'].default_value=density;n.inputs['Color'].default_value=(*((1-sigma/density) if kind.endswith('Absorption') else sigma/density),1)
                    if kind.endswith('Scatter'):n.inputs['Anisotropy'].default_value=data.get('anisotropy',0)
                    volume.append(n.outputs[0])
            if len(volume)==2:
                n=t.nodes.new('ShaderNodeAddShader');t.links.new(volume[0],n.inputs[0]);t.links.new(volume[1],n.inputs[1]);volume=[n.outputs[0]]
            if volume:t.links.new(volume[0],out.inputs['Volume'])
        elif data['bsdf_model']=='thin_diffuse':
            transmission=tuple(data.get('diffuse_transmission',[0,0,0]))+(1,)
            if 'transmission_texture' in data:
                gamma=t.nodes.new('ShaderNodeGamma');t.links.new(texture(t,data['transmission_texture']).outputs['Color'],gamma.inputs['Color']);gamma.inputs['Gamma'].default_value=2.2;transmission=multiply(gamma.outputs[0],transmission)
            r=bsdf('ShaderNodeBsdfDiffuse',color,0);tr=bsdf('ShaderNodeBsdfTranslucent',transmission);n=t.nodes.new('ShaderNodeAddShader');t.links.new(r.outputs[0],n.inputs[0]);t.links.new(tr.outputs[0],n.inputs[1]);surface=n.outputs[0]
        elif data['bsdf_model']=='lambert':surface=bsdf('ShaderNodeBsdfDiffuse',color,0).outputs[0]
        elif data['bsdf_model']=='ggx_add':
            diffuse=bsdf('ShaderNodeBsdfDiffuse',multiply(color,op('MULTIPLY',.96,op('SUBTRACT',1,metal))),0)
            glossy=bsdf('ShaderNodeBsdfGlossy',mix(metal,(.04,.04,.04,1),color),rough)
            n=t.nodes.new('ShaderNodeAddShader');t.links.new(diffuse.outputs[0],n.inputs[0]);t.links.new(glossy.outputs[0],n.inputs[1]);surface=n.outputs[0]
        else:raise ValueError('Engine Schlick PBR has no exact stock Cycles closure; use a controlled model')
        if any(data.get('emission',[0,0,0])):
            emit=t.nodes.new('ShaderNodeEmission');radiance=(*data['emission'],1)
            if not data.get('two_sided',False):radiance=multiply(radiance,op('SUBTRACT',1,geo.outputs['Backfacing']))
            bind(radiance,emit.inputs['Color']);n=t.nodes.new('ShaderNodeAddShader');t.links.new(surface,n.inputs[0]);t.links.new(emit.outputs[0],n.inputs[1]);surface=n.outputs[0]
        cutoff=data.get('alpha_cutoff',0)
        if cutoff:alpha=op('SUBTRACT',1,op('LESS_THAN',alpha,cutoff))
        if 'base_texture' in data or alpha!=1:
            null=bsdf('ShaderNodeBsdfTransparent',(1,1,1,1));n=t.nodes.new('ShaderNodeMixShader');bind(alpha,n.inputs[0]);t.links.new(null.outputs[0],n.inputs[1]);t.links.new(surface,n.inputs[2]);surface=n.outputs[0]
        t.links.new(surface,out.inputs['Surface']);return m
    materials=[material(m,i) for i,m in enumerate(j['materials'])]
    geometry=asset(j['geometry'])
    with geometry.open('rb') as f:
        if f.read(8)!=b'PTMESH01':raise ValueError('Invalid PT mesh header')
    raw=np.memmap(geometry,dtype='<f4',mode='r',offset=8).reshape(-1,8);triangles=0
    meshes={}
    for draw in j['draws']:
        key=(draw['offset'],draw['vertices'],draw['material'],draw.get('tangents_offset'))
        mesh=meshes.get(key)
        if mesh is None:
            start=(draw['offset']-8)//32;rows=np.array(raw[start:start+draw['vertices']],copy=True)
            positions=rows[:,:3];normals=rows[:,3:6].copy();normals/=np.maximum(np.linalg.norm(normals,axis=1,keepdims=True),1e-20)
            faces=np.arange(len(rows),dtype=np.int32).reshape(-1,3)
            cross=np.cross(positions[faces[:,1]]-positions[faces[:,0]],positions[faces[:,2]]-positions[faces[:,0]]);valid=np.einsum('ij,ij->i',cross,cross)>1e-24;faces=faces[valid];cross=cross[valid]
            if not len(faces):continue
            disagree=np.einsum('ij,ij->i',cross,normals[faces].sum(axis=1))<0
            if j['materials'][draw['material']].get('bounded_volume',False):normals[faces[disagree].ravel()]*=-1
            else:faces[disagree]=faces[disagree][:,[0,2,1]]
            loops=faces.ravel();mesh=bpy.data.meshes.new('PT '+draw['name']);mesh.vertices.add(len(rows));mesh.vertices.foreach_set('co',positions.ravel());mesh.loops.add(len(loops));mesh.loops.foreach_set('vertex_index',loops)
            mesh.polygons.add(len(faces));mesh.polygons.foreach_set('loop_start',np.arange(len(faces),dtype=np.int32)*3);mesh.polygons.foreach_set('loop_total',np.full(len(faces),3,dtype=np.int32));mesh.polygons.foreach_set('use_smooth',np.ones(len(faces),dtype=bool));mesh.update()
            uv=mesh.uv_layers.new(name='PT UV');uv.data.foreach_set('uv',rows[loops,6:8].ravel());mesh.normals_split_custom_set(normals[loops].tolist());mesh.materials.append(materials[draw['material']]);meshes[key]=mesh
        obj=bpy.data.objects.new(draw['name'],mesh);model=matrix(draw['model']);model[:3,:]=WORLD@model[:3,:];obj.matrix_world=Matrix(model.tolist());scene.collection.objects.link(obj);triangles+=len(mesh.polygons)
    scene.render.resolution_x=width;scene.render.resolution_y=height;scene.render.resolution_percentage=100
    world=matrix(j['camera']['world']);world[:3,:]=WORLD@world[:3,:];projection=matrix(j['camera']['projection']);aspect=width/height
    camera=bpy.data.cameras.new('PT matched camera');camera.sensor_fit='VERTICAL';camera.sensor_height=24;camera.lens=12*float(projection[1,1]);camera.shift_x=float(projection[0,2])*aspect/2;camera.shift_y=float(projection[1,2])/2;camera.clip_start=1e-6;camera.clip_end=1e6
    obj=bpy.data.objects.new('PT matched camera',camera);obj.matrix_world=Matrix(world.tolist());scene.collection.objects.link(obj);scene.camera=obj
    bpy.context.view_layer.update();actual=np.asarray(obj.calc_matrix_camera(bpy.context.evaluated_depsgraph_get(),x=width,y=height));expected=projection.copy();expected[0,0]*=j['camera']['aspect']/aspect
    camera_error=float(np.abs(actual[:2,:]-expected[:2,:]).max())
    if camera_error>1e-5:raise ValueError(f'Camera projection does not match: {camera_error}')
    t=scene.world.node_tree;scene.world.use_nodes=True;t=scene.world.node_tree;t.nodes.clear();out=t.nodes.new('ShaderNodeOutputWorld');background=t.nodes.new('ShaderNodeBackground');background.inputs['Color'].default_value=(0,0,0,1);t.links.new(background.outputs[0],out.inputs['Surface'])
    if 'environment' in j:
        env=j['environment'];w,h=env['width'],env['height'];pixels=np.fromfile(asset(env['file']),dtype='<f4').reshape(h,w,3);rgba=np.ones((h,w,4),dtype=np.float32);rgba[:,:,:3]=pixels[::-1]
        image=bpy.data.images.new('PT linear environment',width=w,height=h,float_buffer=True);image.colorspace_settings.name='Non-Color';image.pixels.foreach_set(rgba.ravel());node=t.nodes.new('ShaderNodeTexEnvironment');node.image=image;node.interpolation='Linear';t.links.new(node.outputs['Color'],background.inputs['Color'])
    def light(kind,position,direction,color,energy,angle=0):
        data=bpy.data.lights.new('PT '+kind,kind);maximum=max(color);data.energy=energy*maximum;data.color=[v/maximum for v in color] if maximum else [1,1,1];data.use_shadow=True
        if kind=='SUN':data.angle=angle
        else:data.shadow_soft_size=0
        obj=bpy.data.objects.new('PT '+kind,data);scene.collection.objects.link(obj);obj.location=WORLD@np.asarray(position);d=WORLD@np.asarray(direction);obj.rotation_euler=Vector(d).to_track_quat('-Z','Y').to_euler();return data
    if 'sun' in j:
        sun=j['sun'];light('SUN',[0,0,0],-np.asarray(sun['direction']),sun['irradiance'],1,2*sun['radius'])
    for l in j.get('lights',[]):
        if l['type']==0:raise ValueError('Delta directional lights require a separate controlled fixture')
        data=light('SPOT' if l['type']==2 else 'POINT',l['position'],l['direction'],l['color'],4*math.pi)
        if l['type']==2:raise ValueError('Spot falloff matching is not implemented')
    exported=j.get('export',{});retained=exported.get('particle_instances_retained');total=exported.get('particle_instances_source')
    budget=retained<total if retained is not None and total is not None else None
    return {'manifest':str(path),'manifest_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'comparison':j['comparison'],'particle_budget':budget,'draws':len(j['draws']),'unique_meshes':len(meshes),'triangles':triangles,'camera_projection_max_error':camera_error,'limits':['controlled closures rather than original Cycles source graphs','single-scatter GGX + Lambert exact constant-color closure model; no Schlick Fresnel in ggx_add','thin_diffuse is Lambert reflection + transmission, without glossy/Layer Weight layers','solid Glass requires untinted interface; volumetric absorption retained','native Cycles sampling, roulette and ray offsets differ']}
