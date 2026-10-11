"""Run in the full UE editor with -ExecutePythonScript=<absolute script path>.

Create a UE Nanite scene from the same CC0 LOD0 and JSON instance layout.
The project must be the generated build/unreal-cliff/CliffNanite.uproject.
"""
import json
import math
from pathlib import Path
import unreal

# Load the level editor before creating and saving the isolated map.
unreal.load_module('LevelEditor')

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / 'build/unreal-cliff'
ASSETS = ROOT / 'samples/assets/benchmarks/coastal-cliff'
LAYOUT = json.loads((ROOT / 'samples/cluster-cliff-scene.json').read_text())
if Path(unreal.Paths.project_dir()).resolve() != PROJECT.resolve():
    raise RuntimeError('This script only operates on the isolated cliff test project')

# The legacy FBX/OBJ importer with KeepXYZAxes flips Y. Pre-rotate the OBJ
# so resulting UE centimeters are (SceneRenderer x, z, y) * 100.
source = ASSETS / 'coastal_cliff_02_lod0.obj'
low, high = [float('inf')] * 3, [-float('inf')] * 3
with source.open() as stream:
    for line in stream:
        if line.startswith('v '):
            p = list(map(float, line.split()[1:4]))
            low = [min(a, b) for a, b in zip(low, p)]
            high = [max(a, b) for a, b in zip(high, p)]
origin = [(low[0] + high[0]) / 2, low[1], (low[2] + high[2]) / 2]
scale = LAYOUT['height'] / (high[1] - low[1])
prepared = PROJECT / 'coastal_cliff_02_ue.obj'
with source.open() as stream, prepared.open('w') as out:
    for line in stream:
        if line.startswith('v '):
            p = [(v - o) * scale * 100 for v, o in zip(map(float, line.split()[1:4]), origin)]
            line = 'v %.9g %.9g %.9g\n' % (p[0], -p[2], p[1])
        elif line.startswith('vn '):
            n = list(map(float, line.split()[1:4]))
            line = 'vn %.9g %.9g %.9g\n' % (n[0], -n[2], n[1])
        elif line.startswith('f '):
            # The final (x,z,y) mapping reflects handedness. Reverse corners
            # so the UE one-sided mesh exposes the same side as the source.
            corners = line.split()[1:]
            line = 'f ' + ' '.join(reversed(corners)) + '\n'
        out.write(line)

unreal.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')
task = unreal.AssetImportTask()
task.set_editor_property('filename', str(prepared))
task.set_editor_property('destination_path', '/Game/Cliff')
task.set_editor_property('destination_name', 'Cliff_LOD0')
task.set_editor_property('automated', True)
task.set_editor_property('replace_existing', True)
task.set_editor_property('save', True)
options = unreal.FbxImportUI()
options.set_editor_property('import_mesh', True)
options.set_editor_property('import_materials', False)
options.set_editor_property('import_textures', False)
options.set_editor_property('import_as_skeletal', False)
data = options.get_editor_property('static_mesh_import_data')
data.set_editor_property('combine_meshes', True)
data.set_editor_property('auto_generate_collision', False)
data.set_editor_property('generate_lightmap_u_vs', False)
data.set_editor_property('convert_scene', False)
data.set_editor_property('convert_scene_unit', False)
data.set_editor_property('normal_import_method', unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
task.set_editor_property('options', options)
task.set_editor_property('factory', unreal.FbxFactory())
tools = unreal.AssetToolsHelpers.get_asset_tools()
tools.import_asset_tasks([task])
mesh = unreal.load_asset('/Game/Cliff/Cliff_LOD0')
if mesh is None:
    raise RuntimeError('UE did not import the verified LOD0')
imported_source_count = mesh.get_num_nanite_triangles() or mesh.get_num_triangles(0)
if imported_source_count != LAYOUT['source_triangles_per_instance']:
    raise RuntimeError('UE import did not preserve the verified LOD0 triangle count')
box = mesh.get_bounding_box()
if abs(box.max.z - box.min.z - 1200) > .1:
    raise RuntimeError('UE import axis/unit conversion failed')
subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
settings = subsystem.get_nanite_settings(mesh)
settings.set_editor_property('enabled', True)
settings.set_editor_property('keep_percent_triangles', 1.0)
settings.set_editor_property('trim_relative_error', 0.0)
settings.set_editor_property('fallback_percent_triangles', 1.0)
settings.set_editor_property('fallback_relative_error', 0.0)
settings.set_editor_property('fallback_target', unreal.NaniteFallbackTarget.PERCENT_TRIANGLES)
subsystem.set_nanite_settings(mesh, settings, True)

# A direct-light material matches the same texture channels. UE recomputes its
# tangent basis; this is a geometry/layout comparison, not a shading GT.
material = unreal.load_asset('/Game/Cliff/M_Cliff') or tools.create_asset('M_Cliff', '/Game/Cliff', unreal.Material, unreal.MaterialFactoryNew())
# SceneRenderer's current geometry pipeline uses CullMode::None. Match its
# visible surfaces for this geometry comparison; shading remains engine-specific.
material.set_editor_property('two_sided', True)
editing = unreal.MaterialEditingLibrary
editing.delete_all_material_expressions(material)
for filename, prop, sampler in [
    ('coastal_cliff_02_diff_2k.jpg', unreal.MaterialProperty.MP_BASE_COLOR, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR),
    ('coastal_cliff_02_nor_gl_2k.jpg', unreal.MaterialProperty.MP_NORMAL, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL),
    ('coastal_cliff_02_rough_2k.jpg', unreal.MaterialProperty.MP_ROUGHNESS, unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR),
]:
    texture_task = unreal.AssetImportTask()
    texture_task.set_editor_property('filename', str(ASSETS / filename))
    texture_task.set_editor_property('destination_path', '/Game/Cliff')
    texture_task.set_editor_property('automated', True)
    texture_task.set_editor_property('save', True)
    texture = unreal.load_asset('/Game/Cliff/' + Path(filename).stem)
    if texture is None:
        tools.import_asset_tasks([texture_task])
        paths = texture_task.get_editor_property('imported_object_paths')
        if not paths:
            raise RuntimeError('UE texture import failed: ' + filename)
        texture = unreal.load_asset(paths[0])
    if prop == unreal.MaterialProperty.MP_NORMAL:
        texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
        texture.set_editor_property('srgb', False)
        texture.set_editor_property('flip_green_channel', True)  # GL normal -> UE tangent-space normal.
    elif prop == unreal.MaterialProperty.MP_ROUGHNESS:
        texture.set_editor_property('srgb', False)
    unreal.EditorAssetLibrary.save_loaded_asset(texture)
    node = editing.create_material_expression(material, unreal.MaterialExpressionTextureSample)
    node.set_editor_property('texture', texture)
    node.set_editor_property('sampler_type', sampler)
    editing.connect_material_property(node, 'RGB' if prop != unreal.MaterialProperty.MP_ROUGHNESS else 'R', prop)
editing.recompile_material(material)
mesh.set_material(0, material)
unreal.EditorAssetLibrary.save_loaded_asset(mesh)
unreal.EditorAssetLibrary.save_loaded_asset(material)

world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
if world is None:
    raise RuntimeError('UE did not create the isolated canyon world')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
def position(p):
    return unreal.Vector(p[0] * 100, p[2] * 100, p[1] * 100)
for index, item in enumerate(LAYOUT['instances']):
    rotation = unreal.Rotator(pitch=0, yaw=-item['yaw'], roll=0)
    actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, position(item['position']), rotation)
    actor.set_actor_label('Cliff LOD0 instance %d' % index)
    actual = actor.get_actor_rotation()
    if abs(actual.pitch) > .001 or abs(actual.roll) > .001 or abs(actual.yaw + item['yaw']) > .001:
        raise RuntimeError('UE cliff actor did not retain the requested yaw-only transform')
    actor.static_mesh_component.set_static_mesh(mesh)
    actor.static_mesh_component.set_mobility(unreal.ComponentMobility.STATIC)
floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, -6000, -4))
floor.set_actor_label('Canyon floor')
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(unreal.Vector(160, 240, .02))
light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 2500), unreal.Rotator(pitch=-60, yaw=-30, roll=0))
light.light_component.set_editor_property('intensity', 5.0)
sky = actors.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0))
camera = actors.spawn_actor_from_class(unreal.CameraActor, position(LAYOUT['camera']['position']))
camera.set_actor_label('Cliff benchmark camera')
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(position(LAYOUT['camera']['position']), position(LAYOUT['camera']['target'])), False)
vertical_fov = LAYOUT['camera']['fov']
horizontal_fov = math.degrees(2 * math.atan(math.tan(math.radians(vertical_fov / 2)) * 960 / 540))
camera.camera_component.set_editor_property('field_of_view', horizontal_fov)
camera.camera_component.set_editor_property('aspect_ratio', 960 / 540)
camera.camera_component.set_editor_property('constrain_aspect_ratio', True)
camera.camera_component.set_editor_property('override_aspect_ratio_axis_constraint', True)
camera.camera_component.set_editor_property('aspect_ratio_axis_constraint', unreal.AspectRatioAxisConstraint.ASPECT_RATIO_MAINTAIN_XFOV)
if not unreal.EditorLoadingAndSavingUtils.save_map(world, '/Game/Cliff/Canyon'):
    raise RuntimeError('UE did not save the isolated canyon map')
unreal.EditorAssetLibrary.save_directory('/Game/Cliff', only_if_is_dirty=False, recursive=True)
report = {'scene': 'Coastal Cliff canyon', 'nanite_enabled': subsystem.get_nanite_settings(mesh).enabled,
          'source_triangles_per_instance': LAYOUT['source_triangles_per_instance'], 'instances': len(LAYOUT['instances']),
          'nanite_triangles': mesh.get_num_nanite_triangles(), 'fallback_triangles': mesh.get_num_triangles(0),
          'height_cm': box.max.z - box.min.z, 'map': '/Game/Cliff/Canyon',
          'camera_vertical_fov': vertical_fov, 'camera_horizontal_fov': horizontal_fov,
          'yaw_only_transforms_verified': True,
          'ue_two_sided_material': True, 'native_geometry_cull_mode': 'None',
          'scope': 'Original CC0 LOD0; same JSON geometry transforms; UE lighting and tangent basis differ'}
if not report['nanite_enabled'] or report['nanite_triangles'] == 0:
    raise RuntimeError('UE did not build Nanite data')
if report['fallback_triangles'] != report['source_triangles_per_instance']:
    raise RuntimeError('UE full-source fallback did not retain the verified triangle count')
(PROJECT / 'setup-report.json').write_text(json.dumps(report, indent=2) + '\n')
unreal.log('CLIFF_NANITE_SETUP_SUCCESS ' + json.dumps(report))
unreal.SystemLibrary.quit_editor()
