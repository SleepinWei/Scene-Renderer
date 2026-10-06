#!/usr/bin/env python3
"""Run with Blender --background --disable-autoexec --python ... -- --output DIR.
Exports evaluated triangles, original camera/lights and a baked world. Legacy
shader graphs are explicitly reduced to supported PT lobes; color graphs are
baked into UV atlases, never represented as complete Cycles compatibility.
"""
import argparse
import collections
import hashlib
import json
import math
from pathlib import Path
import sys
import time
import bpy
from mathutils import Matrix, Vector
import numpy as np

ROT = Matrix(((1, 0, 0, 0), (0, 0, 1, 0), (0, -1, 0, 0), (0, 0, 0, 1)))

def packed_matrix(m):
    return [float(m[r][c]) for c in range(4) for r in range(4)]

def default(socket, fallback):
    if socket is None:
        return fallback
    v = socket.default_value
    return list(v) if hasattr(v, '__len__') else float(v)

def shaders(tree, output_socket, seen=None):
    """Walk only the connected surface graph, including nested groups."""
    seen = set() if seen is None else seen
    if not output_socket or not output_socket.is_linked:
        return []
    found = []
    for link in output_socket.links:
        n = link.from_node
        key = (tree.as_pointer(), n.as_pointer())
        if key in seen:
            continue
        seen.add(key)
        if n.type == 'GROUP' and n.node_tree:
            out = next((o for o in n.node_tree.nodes if o.type == 'GROUP_OUTPUT' and o.is_active_output), None)
            if out:
                i = next((x for x in out.inputs if x.name == link.from_socket.name), None)
                found += shaders(n.node_tree, i, seen)
        elif n.type in ('MIX_SHADER', 'ADD_SHADER'):
            for i in n.inputs:
                if i.type == 'SHADER':
                    found += shaders(tree, i, seen)
        else:
            found.append(n)
    return found

def homogeneous_volume_nodes(socket):
    if not socket or not socket.is_linked:return []
    node=socket.links[0].from_node
    if node.type=='ADD_SHADER':
        children=[homogeneous_volume_nodes(i) for i in node.inputs if i.type=='SHADER']
        return None if any(c is None for c in children) else [n for c in children for n in c]
    if node.type in ('VOLUME_ABSORPTION','VOLUME_SCATTER') and not any(i.is_linked for i in node.inputs):return [node]
    return None

def surface_nodes(material):
    if not material or not material.use_nodes:
        return []
    out = next((n for n in material.node_tree.nodes if n.type == 'OUTPUT_MATERIAL' and n.is_active_output), None)
    return shaders(material.node_tree, out.inputs.get('Surface') if out else None)

def describe(material):
    nodes = surface_nodes(material)
    types = {n.type for n in nodes}
    chosen = next((n for n in nodes if n.type == 'BSDF_PRINCIPLED'), None)
    chosen = chosen or next((n for n in nodes if n.type == 'BSDF_DIFFUSE'), None)
    chosen = chosen or next((n for n in nodes if n.type == 'BSDF_GLASS'), None)
    chosen = chosen or next((n for n in nodes if n.type in ('BSDF_GLOSSY', 'BSDF_TRANSLUCENT', 'EMISSION')), None)
    color = chosen.inputs.get('Base Color' if chosen and chosen.type == 'BSDF_PRINCIPLED' else 'Color') if chosen else None
    linear = default(color, list(material.diffuse_color) if material else [.8,.8,.8,1])[:3]
    rough_node = next((n for n in nodes if n.type == 'BSDF_GLOSSY'), chosen)
    roughness = default(rough_node.inputs.get('Roughness') if rough_node else None, .8)
    metallic = default(chosen.inputs.get('Metallic'), 0) if chosen and chosen.type == 'BSDF_PRINCIPLED' else (1 if types == {'BSDF_GLOSSY'} else 0)
    thin = 'BSDF_TRANSPARENT' in types and types <= {'BSDF_GLOSSY','BSDF_TRANSPARENT'} and material and any(n.type in ('FRESNEL','LAYER_WEIGHT') and any(o.is_linked for o in n.outputs) for n in material.node_tree.nodes)
    if thin:
        chosen = next(n for n in nodes if n.type == 'BSDF_TRANSPARENT')
        color = chosen.inputs.get('Color');linear = default(color,[1,1,1,1])[:3]
    glass = chosen and (chosen.type == 'BSDF_GLASS' or (chosen.type == 'BSDF_PRINCIPLED' and default(chosen.inputs.get('Transmission Weight'), 0) >= .99))
    ior = default(chosen.inputs.get('IOR'), 1.5) if glass else (1.5 if thin else 0)
    if types <= {'BSDF_GLOSSY','BSDF_TRANSPARENT'} and not thin:metallic=1
    emission = next((n for n in nodes if n.type == 'EMISSION'), None)
    emit_color = default(emission.inputs.get('Color'), [1,1,1,1])[:3] if emission else [0,0,0]
    strength = default(emission.inputs.get('Strength'), 1) if emission else 0
    result = dict(name=material.name if material else 'default',base_color=[max(0,min(1,c))**(1/2.2) for c in linear],metallic=metallic,roughness=max(.045,min(1,roughness)),ior=ior,dielectric_roughness=max(0,min(1,roughness)) if glass else 0,emission=[c*strength for c in emit_color],two_sided=bool(emission))
    result['thin_dielectric']=bool(thin)
    if 'BSDF_TRANSLUCENT' in types and not glass and not thin:
        result['bsdf_model']='thin_diffuse';result['diffuse_transmission']=[1,1,1]
        if 'BSDF_DIFFUSE' not in types:color=None;result['base_color']=[0,0,0]
    if thin:result.update(dielectric_roughness=0,metallic=0)
    issues = []
    if chosen and chosen.type=='BSDF_PRINCIPLED' and chosen.inputs.get('Alpha'):
        alpha=chosen.inputs['Alpha']
        if alpha.is_linked:issues.append('Principled alpha coverage baked into base atlas')
        else:result['opacity']=max(0,min(1,default(alpha,1.0)))
    if len(nodes) > 1:
        issues.append('shader mixture reduced to dominant diffuse/glass/metal lobe; Fresnel/layer-weight mixing not retained')
    if 'BSDF_TRANSLUCENT' in types:
        issues.append('thin diffuse Lambert reflection/transmission baked separately; RGB R+T normalized above one; glossy and angular layer mixtures omitted')
    if thin:
        issues.append('angle-dependent transparent/glossy graph converted to smooth parallel thin dielectric; IOR=1.5 approximation, thin-sheet bump/rough layers omitted')
    elif 'BSDF_TRANSPARENT' in types:
        issues.append('transparent shader coverage baked separately from color; thin translucency remains approximate')
    if material and material.use_nodes and any(n.type == 'NEW_GEOMETRY' and n.outputs.get('Backfacing') and n.outputs['Backfacing'].is_linked for n in material.node_tree.nodes) and 'BSDF_TRANSPARENT' in types and emission:
        result['opacity']=.02
        issues.append('view-dependent emissive portal retained as diagnosed 2% coverage approximation; directional closure unsupported')
    normal_sockets=[n.inputs['Normal'] for n in nodes if n.inputs.get('Normal') and n.inputs['Normal'].is_linked]
    if any(socket.id_data==material.node_tree and socket.links[0].from_node.type in ('BUMP','NORMAL_MAP') for socket in normal_sockets):
        issues.append('selected surface normal/bump baked into tangent atlas; shader displacement remains unsupported')
    elif normal_sockets:issues.append('group-internal or arbitrary normal-vector graph omitted; only top-level Bump/NormalMap outputs bake')
    if rough_node and rough_node.inputs.get('Roughness') and rough_node.inputs['Roughness'].is_linked:
        issues.append('roughness graph baked into linear ORM atlas' if rough_node.inputs['Roughness'].id_data == material.node_tree else 'group-internal roughness uses diagnosed constant fallback')
    if color and color.is_linked:
        issues.append('selected lobe color graph baked into UV atlas')
        if material and color.id_data != material.node_tree:
            issues.append('group-internal color socket uses constant fallback; cross-tree baking needs group flattening')
    alpha=chosen.inputs.get('Alpha') if chosen and chosen.type=='BSDF_PRINCIPLED' else None
    result['bake_alpha']=bool(not thin and ('BSDF_TRANSPARENT' in types or (alpha and (alpha.is_linked or default(alpha,1.0)<1))))
    if any('emissive portal' in issue for issue in issues):result['bake_alpha']=False
    if material and material.use_nodes:
        out=next((n for n in material.node_tree.nodes if n.type=='OUTPUT_MATERIAL' and n.is_active_output),None)
        volume=homogeneous_volume_nodes(out.inputs.get('Volume') if out else None)
        if volume is None:issues.append('unsupported linked/mixed volume graph omitted')
        elif volume:
            supported=sum(n.type=='VOLUME_SCATTER' for n in volume)<=1
            if supported and glass:
                absorption=np.zeros(3);scattering=np.zeros(3);anisotropy=0
                for n in volume:
                    c=np.asarray(default(n.inputs.get('Color'),[1,1,1,1])[:3]);density=default(n.inputs.get('Density'),0)
                    if n.type=='VOLUME_ABSORPTION':absorption+=(1-c)*density
                    else:scattering+=c*density;anisotropy=default(n.inputs.get('Anisotropy'),0)
                result.update(absorption=absorption.tolist(),scattering=scattering.tolist(),anisotropy=anisotropy,bounded_volume=True)
                issues.append('constant homogeneous volume coefficients retained; geometry must be closed')
            else:issues.append('unsupported linked/mixed volume graph omitted')
    return result, color, issues

def coverage_socket(tree, socket, seen=None):
    """Convert direction-independent transparent mixtures to a scalar graph.

    Other lobe mixtures are fully covered, so their view-dependent factors do
    not need evaluation. Node groups stay diagnosed fallbacks until flattened.
    """
    seen=set() if seen is None else seen
    if not socket or not socket.is_linked:return 1.0
    node=socket.links[0].from_node
    if node.as_pointer() in seen:return 1.0
    seen=seen|{node.as_pointer()}
    if node.type=='BSDF_TRANSPARENT':return 0.0
    if node.type=='BSDF_PRINCIPLED':
        alpha=node.inputs.get('Alpha')
        return alpha.links[0].from_socket if alpha and alpha.is_linked else default(alpha,1.0)
    if node.type not in ('MIX_SHADER','ADD_SHADER'):return 1.0
    inputs=[i for i in node.inputs if i.type=='SHADER'];a,b=[coverage_socket(tree,i,seen) for i in inputs]
    if isinstance(a,float) and isinstance(b,float) and a==b:return a
    def math_node(operation,left,right):
        n=tree.nodes.new('ShaderNodeMath');n.operation=operation
        for target,value in zip(n.inputs,(left,right)):
            if isinstance(value,(float,int)):target.default_value=value
            else:tree.links.new(value,target)
        return n.outputs[0]
    if node.type=='ADD_SHADER':return math_node('ADD',a,b)
    factor=node.inputs[0];f=factor.links[0].from_socket if factor.is_linked else default(factor,.5)
    return math_node('ADD',a,math_node('MULTIPLY',math_node('SUBTRACT',b,a),f))

def scalar_channels(material, tree):
    nodes=surface_nodes(material)
    chosen=next((n for n in nodes if n.type=='BSDF_PRINCIPLED'),None)
    chosen=chosen or next((n for n in nodes if n.type=='BSDF_GLOSSY'),None)
    chosen=chosen or next((n for n in nodes if n.type in ('BSDF_DIFFUSE','BSDF_GLASS')),None)
    def value(name,fallback):
        socket=chosen.inputs.get(name) if chosen else None
        return socket.links[0].from_socket if socket and socket.is_linked and socket.id_data==tree else default(socket,fallback)
    desc,_,_=describe(material)
    output=next((n for n in tree.nodes if n.type=='OUTPUT_MATERIAL' and n.is_active_output),None)
    coverage=1.0 if desc['thin_dielectric'] or desc.get('opacity')==.02 else coverage_socket(tree,output.inputs.get('Surface') if output else None)
    return coverage,value('Roughness',desc['roughness']),value('Metallic',desc['metallic'])

def reusable_mask_uv(material):
    if not material or not describe(material)[0]['bake_alpha']:return False
    # Repeated leaf UVs are useful only for UV-driven graphs. Generated/object
    # space procedural fields need the non-overlapping atlas instead.
    for node in material.node_tree.nodes:
        if node.type=='TEX_COORD' and any(node.outputs[name].is_linked for name in ('Generated','Object','Normal','Reflection')):return False
        if node.type=='NEW_GEOMETRY':return False
        if node.type.startswith('TEX_') and node.type!='TEX_IMAGE' and node.inputs.get('Vector') and not node.inputs['Vector'].is_linked:return False
    return True

def bake_color(source_mesh, source_materials, object_name, matrix, bake_scene, size, directory, index, preserve_uv=False):
    mesh = source_mesh.copy()
    originals = [m.copy() if m else bpy.data.materials.new('PT default') for m in source_materials]
    if not originals:
        originals = [bpy.data.materials.new('PT default')]
    old_uv = mesh.uv_layers.active.name if mesh.uv_layers.active else None
    mesh.materials.clear()
    emission_nodes=[];channels=[];normal_sources=[];transmission_sources=[]
    for m in originals:
        m.use_nodes = True
        tree = m.node_tree
        # Pin source UV reads before making the atlas UV the active bake target.
        if old_uv:
            uv = tree.nodes.new('ShaderNodeUVMap'); uv.uv_map = old_uv
            for n in list(tree.nodes):
                if n.type == 'TEX_IMAGE' and not n.inputs['Vector'].is_linked:
                    tree.links.new(uv.outputs['UV'], n.inputs['Vector'])
                if n.type == 'NORMAL_MAP' and n.space=='TANGENT' and not n.uv_map:n.uv_map=old_uv
                if n.type == 'TEX_COORD':
                    for link in list(n.outputs['UV'].links):
                        tree.links.new(uv.outputs['UV'], link.to_socket)
        description, color, _ = describe(m)
        translucent=next((n for n in surface_nodes(m) if n.type=='BSDF_TRANSLUCENT'),None)
        transmission_sources.append(translucent.inputs.get('Color') if translucent else None)
        channels.append(scalar_channels(m,tree))
        candidates=surface_nodes(m)
        normal=next((n.inputs.get('Normal') for n in candidates if n.inputs.get('Normal') and n.inputs['Normal'].is_linked and n.inputs['Normal'].id_data==tree and n.inputs['Normal'].links[0].from_node.type in ('BUMP','NORMAL_MAP')),None)
        normal_sources.append(normal.links[0].from_socket if normal else None)
        out = next((n for n in tree.nodes if n.type == 'OUTPUT_MATERIAL' and n.is_active_output), None)
        if out is None:
            out = tree.nodes.new('ShaderNodeOutputMaterial')
        emit = tree.nodes.new('ShaderNodeEmission')
        emission_nodes.append(emit)
        emit.inputs['Color'].default_value = [0,0,0,1] if description.get('bsdf_model')=='thin_diffuse' and color is None else default(color, list(m.diffuse_color))
        if color and color.is_linked and color.id_data == tree:
            tree.links.new(color.links[0].from_socket, emit.inputs['Color'])
        elif color and color.is_linked:
            # Group-internal sockets require flattening/baking the complete group;
            # keep a diagnosed constant fallback instead of wiring across trees.
            pass
        tree.links.new(emit.outputs[0], out.inputs['Surface'])
        mesh.materials.append(m)
    obj = bpy.data.objects.new(object_name, mesh); bake_scene.collection.objects.link(obj);obj.matrix_world=matrix
    bpy.context.window.scene = bake_scene
    bpy.ops.object.select_all(action='DESELECT');obj.select_set(True);bpy.context.view_layer.objects.active=obj
    source_uv=np.empty(len(mesh.loops)*2,dtype=np.float32)
    if preserve_uv:mesh.uv_layers.active.data.foreach_get('uv',source_uv)
    atlas = mesh.uv_layers.new(name='PT_atlas');mesh.uv_layers.active=atlas;atlas.active_render=True
    if preserve_uv:atlas.data.foreach_set('uv',source_uv)
    else:
        bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT');bpy.ops.uv.smart_project(angle_limit=math.radians(66),island_margin=.01);bpy.ops.object.mode_set(mode='OBJECT')
    image = bpy.data.images.new('PT atlas',width=size,height=size,float_buffer=True)
    for m in originals:
        node=m.node_tree.nodes.new('ShaderNodeTexImage');node.image=image;m.node_tree.nodes.active=node
    bpy.ops.object.bake(type='EMIT',margin=4,use_clear=True)
    pixels=np.empty(size*size*4,dtype=np.float32);image.pixels.foreach_get(pixels)
    pixels=pixels.reshape(size,size,4)[::-1].copy()
    # Bake alpha / roughness / metallic as linear scalar channels on the same UV.
    for material,emit,values in zip(originals,emission_nodes,channels):
        tree=material.node_tree;combine=tree.nodes.new('ShaderNodeCombineXYZ')
        for target,value in zip(combine.inputs,values):
            if isinstance(value,(float,int)):target.default_value=value
            else:tree.links.new(value,target)
        tree.links.new(combine.outputs[0],emit.inputs['Color'])
    bpy.ops.object.bake(type='EMIT',margin=4,use_clear=True)
    scalar=np.empty(size*size*4,dtype=np.float32);image.pixels.foreach_get(scalar)
    scalar=scalar.reshape(size,size,4)[::-1].copy()
    # Encode for the renderer's established pow(2.2) albedo decode.
    coverage=np.clip(scalar[:,:,0],0,1).copy()
    rgba=np.ones_like(pixels);rgba[:,:,:3]=np.clip(pixels[:,:,:3],0,1)**(1/2.2);rgba[:,:,3]=np.clip(scalar[:,:,0],0,1)
    # Blender pixels are bottom-up; PNG writer below produces top-down rows.
    import zlib,struct
    def chunk(kind, data):
        return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    def save_png(filename,rgba):
        raw=(np.clip(rgba,0,1)*255+.5).astype(np.uint8)
        png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',size,size,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(b''.join(b'\0'+row.tobytes() for row in raw)))+chunk(b'IEND',b'')
        (directory/filename).write_bytes(png)
    filename=f'color-{index:04d}.png';save_png(filename,rgba)
    # Atlas gaps are not transparent geometry. Opaque lobes must remain opaque
    # even when bilinear filtering reaches unbaked padding / tiny UV islands.
    opaque=f'color-opaque-{index:04d}.png';rgba[:,:,3]=1;save_png(opaque,rgba)
    orm=f'orm-{index:04d}.png';scalar[:,:,0]=1;scalar[:,:,3]=1;save_png(orm,scalar)
    normal_filename=None
    if any(normal_sources):
        for material,source in zip(originals,normal_sources):
            tree=material.node_tree;diffuse=tree.nodes.new('ShaderNodeBsdfDiffuse')
            if source:tree.links.new(source,diffuse.inputs['Normal'])
            out=next(n for n in tree.nodes if n.type=='OUTPUT_MATERIAL' and n.is_active_output)
            tree.links.new(diffuse.outputs[0],out.inputs['Surface'])
            for link in list(out.inputs['Volume'].links):tree.links.remove(link)
        bpy.ops.object.bake(type='NORMAL',normal_space='TANGENT',normal_r='POS_X',normal_g='POS_Y',normal_b='POS_Z',margin=4,use_clear=True)
        normal_pixels=np.empty(size*size*4,dtype=np.float32);image.pixels.foreach_get(normal_pixels)
        normal_pixels=normal_pixels.reshape(size,size,4)[::-1].copy();normal_pixels[:,:,1]=1-normal_pixels[:,:,1];normal_pixels[:,:,3]=1
        normal_filename=f'normal-{index:04d}.png';save_png(normal_filename,normal_pixels)
    transmission_filename=None
    if any(transmission_sources):
        for material,emit,source in zip(originals,emission_nodes,transmission_sources):
            tree=material.node_tree
            for link in list(emit.inputs['Color'].links):tree.links.remove(link)
            emit.inputs['Color'].default_value=default(source,[0,0,0,1])
            if source and source.is_linked and source.id_data==tree:tree.links.new(source.links[0].from_socket,emit.inputs['Color'])
            out=next(n for n in tree.nodes if n.type=='OUTPUT_MATERIAL' and n.is_active_output);tree.links.new(emit.outputs[0],out.inputs['Surface'])
        bpy.ops.object.bake(type='EMIT',margin=4,use_clear=True)
        transmitted=np.empty(size*size*4,dtype=np.float32);image.pixels.foreach_get(transmitted);transmitted=transmitted.reshape(size,size,4)[::-1].copy()
        r=np.clip(pixels[:,:,:3],0,1);t=np.clip(transmitted[:,:,:3],0,1);total=np.maximum(1,r+t)
        rgba[:,:,:3]=(r/total)**(1/2.2);rgba[:,:,3]=coverage;save_png(filename,rgba);rgba[:,:,3]=1;save_png(opaque,rgba)
        transmitted[:,:,:3]=(t/total)**(1/2.2);transmitted[:,:,3]=1;transmission_filename=f'transmission-{index:04d}.png';save_png(transmission_filename,transmitted)
    uv=np.empty(len(mesh.loops)*2,dtype=np.float32);mesh.uv_layers.active.data.foreach_get('uv',uv)
    tangents=None
    if normal_filename:
        mesh.calc_tangents(uvmap=mesh.uv_layers.active.name);t=np.empty(len(mesh.loops)*3,dtype=np.float32);w=np.empty(len(mesh.loops),dtype=np.float32);mesh.loops.foreach_get('tangent',t);mesh.loops.foreach_get('bitangent_sign',w);tangents=np.column_stack((t.reshape(-1,3),-w));mesh.free_tangents()
    bpy.data.objects.remove(obj,do_unlink=True);bpy.data.meshes.remove(mesh);bpy.data.images.remove(image)
    for m in originals:bpy.data.materials.remove(m)
    return uv.reshape(-1,2),filename,orm,opaque,normal_filename,tangents,transmission_filename

def close_pool_sheet(raw, transform, depth):
    """Close a planar single-material pool sheet in Blender world units."""
    if len(set(raw['mats']))!=1:raise ValueError('Pool sheet must have one material')
    edges=collections.Counter(tuple(sorted((int(a),int(b)))) for t in raw['tri'] for a,b in zip(t,np.roll(t,-1)))
    if all(c==2 for c in edges.values()):return False
    if any(c not in (1,2) for c in edges.values()):raise ValueError('Non-manifold pool sheet')
    points=np.asarray([transform@Vector(v) for v in raw['verts']])
    if np.ptp(points[:,2])>1e-4:raise ValueError('Pool profile requires a horizontal planar sheet')
    count=len(raw['verts']);shift=np.asarray(transform.to_3x3().inverted()@Vector((0,0,-depth)))
    verts=np.concatenate((raw['verts'],raw['verts']+shift));tri=list(raw['tri']);tri+=list(raw['tri'][:,::-1]+count)
    for t in raw['tri']:
        for a,b in zip(t,np.roll(t,-1)):
            if edges[tuple(sorted((int(a),int(b))))]==1:tri.extend(((b,a,a+count),(b,a+count,b+count)))
    tri=np.asarray(tri,dtype=np.int32);normals=[]
    for t in tri:
        normal=np.cross(verts[t[1]]-verts[t[0]],verts[t[2]]-verts[t[0]]);normal/=np.linalg.norm(normal);normals.extend((normal,normal,normal))
    source_uv=raw['uv'].reshape(-1,3,2);uv=np.concatenate((source_uv,source_uv[:,::-1],np.tile([[0,0],[1,0],[1,1]],(len(tri)-2*len(source_uv),1)).reshape(-1,3,2)))
    raw.update(verts=verts.astype(np.float32),tri=tri,normals=np.asarray(normals,dtype=np.float32),uv=uv.reshape(-1,2).astype(np.float32),mats=np.full(len(tri),raw['mats'][0]),has_uv=False)
    return True

def light_color(data):
    color=list(data.color);energy=float(data.energy)
    if data.use_nodes:
        emit=next((n for n in data.node_tree.nodes if n.type=='EMISSION'),None)
        if emit:
            color=default(emit.inputs.get('Color'),[1,1,1,1])[:3]
            c=emit.inputs.get('Color')
            if c and c.is_linked and c.links[0].from_node.type=='BLACKBODY':
                # Documented approximation; exact blackbody spectrum is deferred.
                color=[1,.92,.82]
            strength=emit.inputs.get('Strength')
            if strength and strength.is_linked and strength.links[0].from_node.type=='LIGHT_FALLOFF':
                energy=default(strength.links[0].from_node.inputs.get('Strength'),energy)
            elif strength:energy=default(strength,energy)
    return np.array(color,dtype=np.float64)*energy

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',required=True);parser.add_argument('--scene');parser.add_argument('--atlas-size',type=int,default=256);parser.add_argument('--source-name',default='custom');parser.add_argument('--particle-limit',type=int,default=512,help='preview particle instance budget; -1 exports all');parser.add_argument('--water-profile',choices=['source','pool'],default='source');parser.add_argument('--pool-depth',type=float,default=.5);args=parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    start=time.monotonic();directory=Path(args.output).resolve();directory.mkdir(parents=True,exist_ok=True)
    if args.atlas_size not in (128,256,512,1024) or args.particle_limit < -1:raise ValueError('Invalid atlas size or particle budget')
    # A failed rebuild must not leave a stale manifest pointing to new partial buffers.
    (directory/'scene.json').unlink(missing_ok=True)
    original=bpy.context.scene
    if args.scene:original=bpy.data.scenes[args.scene];bpy.context.window.scene=original
    if not original.camera or original.camera.data.type!='PERSP':raise RuntimeError('A perspective camera is required')
    for obj in original.objects:
        for mod in obj.modifiers:
            if mod.type=='SUBSURF':mod.levels=mod.render_levels
    original.render.engine='CYCLES';bpy.context.view_layer.update();graph=bpy.context.evaluated_depsgraph_get()
    instances=[(i.object.original,i.matrix_world.copy(),i.is_instance,bool(i.particle_system)) for i in graph.object_instances if (not i.parent.original.hide_render if i.is_instance else not i.object.original.hide_render and i.object.original.show_instancer_for_render) and i.show_self]
    particles=[k for k,i in enumerate(instances) if i[3]];particle_total=len(particles)
    if args.particle_limit>=0 and particle_total>args.particle_limit:
        selected={particles[int(k*particle_total/args.particle_limit)] for k in range(args.particle_limit)} if args.particle_limit else set()
        instances=[i for k,i in enumerate(instances) if not i[3] or k in selected]
    particle_retained=sum(bool(i[3]) for i in instances)
    if not math.isfinite(args.pool_depth) or args.pool_depth<=0:raise ValueError('Pool depth must be positive and finite')
    # Evaluated objects belong to the depsgraph. Switching scenes during a bake
    # may invalidate them; detach every mesh/light before any context changes.
    detached={};geometry_instances=[];light_instances=[]
    # Reading a depsgraph must not create/remove Main datablocks or clear another
    # object's evaluated mesh. Copy numeric buffers first; construct bake meshes
    # only after the entire capture has finished.
    for original_obj,transform,is_instance,parent in instances:
        obj=original_obj.evaluated_get(graph)
        if obj.type in ('MESH','CURVE','SURFACE','FONT','META'):
            key=obj.original.as_pointer()
            if key not in detached:
                mesh=obj.to_mesh(preserve_all_data_layers=True,depsgraph=graph)
                if not mesh or not len(mesh.polygons):continue
                mesh.calc_loop_triangles()
                verts=np.empty(len(mesh.vertices)*3,dtype=np.float32);mesh.vertices.foreach_get('co',verts);verts=verts.reshape(-1,3)
                loops=np.empty(len(mesh.loops),dtype=np.int32);mesh.loops.foreach_get('vertex_index',loops)
                normals=np.empty(len(mesh.loops)*3,dtype=np.float32);mesh.corner_normals.foreach_get('vector',normals);normals=normals.reshape(-1,3)
                tri=np.empty(len(mesh.loop_triangles)*3,dtype=np.int32);mesh.loop_triangles.foreach_get('loops',tri);tri=tri.reshape(-1,3)
                mats=np.empty(len(mesh.loop_triangles),dtype=np.int32);mesh.loop_triangles.foreach_get('material_index',mats)
                uv=np.zeros((len(mesh.loops),2),dtype=np.float32)
                if mesh.uv_layers.active:mesh.uv_layers.active.data.foreach_get('uv',uv.ravel())
                detached[key]=dict(name=obj.original.name,verts=verts,tri=loops[tri],normals=normals[tri.ravel()],uv=uv[tri.ravel()],mats=mats,materials=list(mesh.materials),uv_name=mesh.uv_layers.active.name if mesh.uv_layers.active else 'UVMap',has_uv=bool(mesh.uv_layers.active))
            geometry_instances.append((key,transform,is_instance))
        elif obj.type=='LIGHT':light_instances.append((obj.name,obj.original.data,transform))
    camera=original.camera.evaluated_get(graph);world=ROT@camera.matrix_world
    projection=camera.calc_matrix_camera(graph,x=original.render.resolution_x,y=original.render.resolution_y,scale_x=original.render.pixel_aspect_x,scale_y=original.render.pixel_aspect_y)
    result=dict(format='SceneRenderer.PT.v1',geometry='geometry.bin',camera=dict(world=packed_matrix(world),projection=packed_matrix(projection),aspect=original.render.resolution_x*original.render.pixel_aspect_x/(original.render.resolution_y*original.render.pixel_aspect_y)),exposure=1,materials=[],draws=[],lights=[],source=dict(name=args.source_name,blend=Path(bpy.data.filepath).name,sha256=hashlib.sha256(Path(bpy.data.filepath).read_bytes()).hexdigest(),scene=original.name,camera=original.camera.name,frame=original.frame_current),export={})
    warnings=[];diagnostics={};mesh_cache={};inst_count=0;triangles=0;unique_triangles=0;atlases=0;native_mask_atlases=0;normal_atlases=set()
    bake_scene=bpy.data.scenes.new('PT color bake');bake_scene.render.engine='CYCLES';bake_scene.cycles.samples=1;bake_scene.cycles.device='CPU';bake_scene.render.threads_mode='FIXED';bake_scene.render.threads=8
    with (directory/'geometry.bin').open('wb') as output,(directory/'tangents.bin').open('wb') as tangent_output:
        tangent_output.write(b'PTTANG01');result['tangents']='tangents.bin'
        output.write(b'PTMESH01')
        for key,transform,is_instance in geometry_instances:
            raw=detached[key];object_name=raw['name']
            if key not in mesh_cache:
                materials=raw['materials'];desc=[describe(m) for m in materials] or [describe(None)]
                for material,(_,_,issues) in zip(materials,desc):diagnostics[material.name if material else 'default']=issues
                pool=args.water_profile=='pool' and any(m and m.name=='water' and describe(m)[0]['ior']>1 for m in materials)
                if pool:
                    if sum(k==key for k,_,_ in geometry_instances)!=1:raise ValueError('Pool profile requires a single water object instance; shared transformed pool boundaries are unsupported')
                    closed=close_pool_sheet(raw,transform,args.pool_depth)
                    for d,_,issues in desc:
                        if d['name']!='water':continue
                        d.update(ior=1.333,absorption=[.12,.035,.015],bounded_volume=True)
                        issues.append(f'explicit pool profile: IOR 1.333, RGB absorption [0.12,0.035,0.015] per world unit, depth {args.pool_depth}; source IOR differs')
                    if closed:warnings.append(f'{object_name}: open source water sheet closed with explicit pool depth {args.pool_depth}')
                if not pool and any(m and m.name=='water' for m in materials):warnings.append(f'{object_name}: source water IOR retained; open sheet has no finite pool volume, use --water-profile pool for bounded absorption')
                uv=raw['uv'];texture=None;orm=None;opaque=None;normal_texture=None;tangents=None;transmission_texture=None
                if any(d.get('bsdf_model')=='thin_diffuse' for d,_,_ in desc) or any(any('normal/bump baked' in issue for issue in issues) for _,_,issues in desc) or any(color and color.is_linked for _,color,_ in desc) or any(any('coverage baked' in issue or 'roughness graph baked' in issue for issue in issues) for _,_,issues in desc) or any(m and m.use_nodes and any(n.type=='BSDF_PRINCIPLED' and n.inputs.get('Metallic') and n.inputs['Metallic'].is_linked for n in surface_nodes(m)) for m in materials):
                    mesh=bpy.data.meshes.new('PT detached geometry')
                    mesh.from_pydata(raw['verts'].tolist(),[],raw['tri'].tolist());mesh.update()
                    layer=mesh.uv_layers.new(name=raw['uv_name']);layer.data.foreach_set('uv',uv.ravel())
                    mesh.polygons.foreach_set('material_index',raw['mats']);mesh.polygons.foreach_set('use_smooth',np.ones(len(raw['tri']),dtype=bool));mesh.normals_split_custom_set(raw['normals'].tolist())
                    uv,texture,orm,opaque,normal_texture,tangents,transmission_texture=bake_color(mesh,materials,object_name,transform,bake_scene,args.atlas_size,directory,atlases);atlases+=1
                    bpy.data.meshes.remove(mesh)
                verts=raw['verts'][raw['tri'].ravel()];normals=raw['normals'];tri=np.arange(len(verts)).reshape(-1,3);mats=raw['mats']
                groups=[]
                for mi in np.unique(mats):
                    indices=tri[mats==mi].ravel();group_uv=uv[indices];group_texture,group_orm,group_opaque,group_normal=texture,orm,opaque,normal_texture;group_tangents=tangents[indices] if tangents is not None else None;group_transmission=transmission_texture
                    source_material=materials[min(int(mi),len(materials)-1)] if materials else None
                    if raw['has_uv'] and reusable_mask_uv(source_material):
                        mesh=bpy.data.meshes.new('PT native mask geometry');mesh.from_pydata(raw['verts'].tolist(),[],raw['tri'][mats==mi].tolist());mesh.update()
                        layer=mesh.uv_layers.new(name=raw['uv_name']);layer.data.foreach_set('uv',raw['uv'][indices].ravel());mesh.polygons.foreach_set('use_smooth',np.ones(len(mesh.polygons),dtype=bool));mesh.normals_split_custom_set(raw['normals'][indices].tolist())
                        group_uv,group_texture,group_orm,group_opaque,group_normal,group_tangents,group_transmission=bake_color(mesh,[source_material],object_name,transform,bake_scene,args.atlas_size,directory,atlases,preserve_uv=True)
                        atlases+=1;native_mask_atlases+=1;bpy.data.meshes.remove(mesh)
                        message='masked UV graph baked separately with native repeated UVs; no smart-project leaf fragmentation'
                        if message not in diagnostics[source_material.name]:diagnostics[source_material.name].append(message)
                    # Engine image rows run downwards; reflect V and the normal green channel together.
                    group_uv=group_uv.copy();group_uv[:,1]=1-group_uv[:,1]
                    packed=np.column_stack((verts[indices],normals[indices],group_uv)).astype('<f4')
                    mat=dict(desc[min(int(mi),len(desc)-1)][0]);mat_id=len(result['materials'])
                    if group_texture:
                        mat['base_texture']=group_texture if mat['bake_alpha'] else group_opaque;mat['base_color']=[1,1,1]
                        if not any('emissive portal' in issue for issue in desc[min(int(mi),len(desc)-1)][2]):mat['opacity']=1
                    if group_orm:mat['orm_texture']=group_orm
                    if group_normal and any('normal/bump baked' in issue for issue in desc[min(int(mi),len(desc)-1)][2]):mat['normal_texture']=group_normal;normal_atlases.add(group_normal)
                    if mat.get('bsdf_model')=='thin_diffuse' and group_transmission:mat['transmission_texture']=group_transmission
                    result['materials'].append(mat);offset=output.tell();output.write(packed.tobytes());group=dict(material=mat_id,offset=offset,vertices=len(indices))
                    if group_tangents is not None:group['tangents_offset']=tangent_output.tell();tangent_output.write(group_tangents.astype('<f4').tobytes())
                    groups.append(group);unique_triangles+=len(indices)//3
                mesh_cache[key]=groups;print('EXPORT',object_name,len(tri),'triangles',texture,flush=True)
            for group in mesh_cache[key]:result['draws'].append(dict(group,model=packed_matrix(ROT@transform),name=object_name));triangles+=group['vertices']//3
            inst_count+=int(is_instance)
        for light_name,data,transform in light_instances:
            energy=light_color(data);matrix=ROT@transform;direction=(matrix.to_3x3()@Vector((0,0,-1))).normalized();position=list(matrix.translation)
            if data.type=='SUN':
                if 'sun' in result:raise RuntimeError('Only one finite sun is currently supported')
                result['sun']=dict(direction=list(-direction),irradiance=energy.tolist(),radius=max(1e-4,float(data.angle)*.5))
            elif data.type=='AREA':
                sx=float(data.size);sy=float(data.size_y) if data.shape in ('RECTANGLE','ELLIPSE') else sx
                circular=data.shape in ('DISK','ELLIPSE');points=[(math.cos(t*2*math.pi/32)*sx*.5,math.sin(t*2*math.pi/32)*sy*.5,0) for t in range(32)] if circular else [(-sx/2,-sy/2,0),(sx/2,-sy/2,0),(sx/2,sy/2,0),(-sx/2,sy/2,0)]
                axes=transform.to_3x3();area=sx*sy*(math.pi/4 if circular else 1)*(axes@Vector((1,0,0))).cross(axes@Vector((0,1,0))).length
                rows=[]
                for k in range(1,len(points)-1):
                    for p in (points[0],points[k+1],points[k]):rows.append([*p,0,0,-1,0,0])
                offset=output.tell();output.write(np.asarray(rows,dtype='<f4').tobytes());mi=len(result['materials']);result['materials'].append(dict(name=light_name,base_color=[0,0,0],roughness=1,metallic=0,emission=(energy/(math.pi*max(area,1e-8))).tolist()))
                result['draws'].append(dict(material=mi,offset=offset,vertices=len(rows),model=packed_matrix(matrix),name=light_name));triangles+=len(rows)//3
                warnings.append(f'{light_name}: area light energy/spread converted to uniform triangle emitter; node falloff/blackbody approximated')
            elif data.type in ('POINT','SPOT'):
                result['lights'].append(dict(type=2 if data.type=='SPOT' else 1,position=position,direction=list(direction),color=(energy/(4*math.pi)).tolist(),outer=math.cos(data.spot_size*.5) if data.type=='SPOT' else 0,inner=math.cos(data.spot_size*.5*(1-data.spot_blend)) if data.type=='SPOT' else 0))
            else:warnings.append(f'{light_name}: unsupported light type {data.type}')
    # Evaluate the original world shader with an isolated equirectangular camera.
    environment_scene=bpy.data.scenes.new('PT environment bake');environment_scene.world=original.world;environment_scene.render.engine='CYCLES';environment_scene.cycles.samples=1;environment_scene.cycles.device='CPU';environment_scene.render.threads_mode='FIXED';environment_scene.render.threads=8
    environment_scene.render.resolution_x=512;environment_scene.render.resolution_y=256;environment_scene.render.resolution_percentage=100
    cam=bpy.data.cameras.new('PT environment');cam.type='PANO';cam.panorama_type='EQUIRECTANGULAR';camera_obj=bpy.data.objects.new('PT environment',cam);environment_scene.collection.objects.link(camera_obj);camera_obj.matrix_world=ROT.inverted();environment_scene.camera=camera_obj
    bpy.context.window.scene=environment_scene;bpy.ops.render.render()
    # Render Result has no public pixel buffer in Blender 4.5 background mode.
    # Read a scene-linear EXR saved by Blender instead of applying a view transform.
    environment_scene.render.image_settings.file_format='OPEN_EXR';environment_scene.render.image_settings.color_depth='32'
    exr=directory/'environment-source.exr';bpy.data.images['Render Result'].save_render(str(exr),scene=environment_scene)
    img=bpy.data.images.load(str(exr),check_existing=False);pixels=np.empty(512*256*4,dtype=np.float32);img.pixels.foreach_get(pixels);pixels=pixels.reshape(256,512,4)[::-1,:,:3]
    pixels=np.roll(pixels,256,axis=1);(directory/'environment.bin').write_bytes(pixels.astype('<f4').tobytes());result['environment']=dict(file='environment.bin',width=512,height=256)
    warnings += ['selected shader lobes use Lambert/GGX rather than full Cycles closure graphs','object-info random color shared by instances of the same evaluated object','selected surface bump/normal baked; shader displacement and full layered normal closures are not exported','thin diffuse lobes are an energy-bounded approximation; layered glossy/Fresnel and group-internal alpha require future closure/group support','direction-dependent color/scalars are not physically reproduced by UV baking','depth of field, motion blur, compositor and Blender color management are not reproduced']
    if particle_retained<particle_total:warnings.append(f'preview particle budget retained {particle_retained}/{particle_total} instances; omitted particles may affect shadows/reflections')
    result['export']=dict(material_revision=5,tangent_basis="Blender corner MikkTSpace; V and handedness reflected together",thin_diffuse_materials=sum(m.get("bsdf_model")=="thin_diffuse" for m in result["materials"]),water_profile=args.water_profile,pool_depth=args.pool_depth if args.water_profile=='pool' else None,blender_version=bpy.app.version_string,unique_objects=len(mesh_cache),instances=inst_count,particle_instances_source=particle_total,particle_instances_retained=particle_retained,particle_limit=args.particle_limit,unique_triangles=unique_triangles,expanded_triangles=triangles,color_atlases=atlases,scalar_atlases=atlases,normal_atlases=len(normal_atlases),native_mask_atlases=native_mask_atlases,atlas_size=args.atlas_size,seconds=time.monotonic()-start,material_diagnostics=diagnostics,warnings=warnings)
    (directory/'scene.json').write_text(json.dumps(result,indent=2)+'\n');print('FINISHED',json.dumps({k:v for k,v in result['export'].items() if k!='material_diagnostics'}),flush=True)

if __name__=='__main__':main()
