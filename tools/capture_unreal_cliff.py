"""Run as a UE Python startup script after setup_unreal_cliff.py.

Slate ticks allow streaming/shaders and asynchronous screenshots to finish.
Capture the viewport only, with game view enabled (no editor UI).
"""
import json
import time
from pathlib import Path
import unreal

unreal.load_module('LevelEditor')

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'img/diagnostics/cluster-cliff/unreal'
OUT.mkdir(parents=True, exist_ok=True)
steps = [('nanite-beauty', 'r.Nanite 1', 'r.Nanite.Visualize off'),
         ('nanite-clusters', 'r.Nanite 1', 'r.Nanite.Visualize Clusters'),
         ('nanite-triangles', 'r.Nanite 1', 'r.Nanite.Visualize Triangles'),
         ('nanite-mask', 'r.Nanite 1', 'r.Nanite.Visualize Mask'),
         ('full-source-fallback', 'r.Nanite 0', 'r.Nanite.Visualize off')]
state = {'initialized': False, 'index': 0, 'task': None, 'frames': 0, 'deadline': time.monotonic() + 600}
camera = None
handle = None
report = {'captures': [], 'scope': 'UE 5.8 Metal viewport screenshots, 960x540, same layout/camera; not an engine timing comparison'}

def command(value):
    unreal.SystemLibrary.execute_console_command(None, value)

def finish():
    command('r.Nanite 1')
    command('r.Nanite.Visualize off')
    (OUT / 'capture-report.json').write_text(json.dumps(report, indent=2) + '\n')
    unreal.unregister_slate_post_tick_callback(handle)
    unreal.SystemLibrary.quit_editor()

def tick(delta):
    global camera
    try:
        if time.monotonic() > state['deadline']:
            raise RuntimeError('UE cliff screenshot timeout')
        if not state['initialized']:
            actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
            editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
            if editor is None or actors is None:
                return
            cameras = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.CameraActor)]
            if not cameras:
                return
            camera = cameras[0]
            editor.set_level_viewport_camera_info(camera.get_actor_location(), camera.get_actor_rotation())
            # TakeHighResScreenshot directly locks its viewport to this camera
            # and enables game view, without depending on LevelEditorSubsystem.
            command('r.ScreenPercentage 100')
            command('r.VSync 0')
            command('t.MaxFPS 60')
            state['initialized'] = True
        if state['task'] is not None:
            if not state['task'].is_task_done():
                return
            name = steps[state['index']][0]
            file = OUT / (name + '.png')
            if not file.exists() or file.stat().st_size < 1000:
                raise RuntimeError('Missing native UE screenshot: ' + name)
            if file.stat().st_mtime_ns < state['capture_started_ns']:
                raise RuntimeError('UE screenshot was not refreshed: ' + name)
            report['captures'].append({'name': name, 'file': file.name, 'bytes': file.stat().st_size})
            state['task'] = None
            state['index'] += 1
            state['frames'] = 0
            if state['index'] == len(steps):
                finish()
                return
        name, nanite, visualize = steps[state['index']]
        if state['frames'] == 0:
            command(nanite)
            command(visualize)
        state['frames'] += 1
        # Warm each mode; the screenshot helper also waits for shader compilation.
        if state['frames'] >= 120:
            state['capture_started_ns'] = time.time_ns()
            state['task'] = unreal.AutomationLibrary.take_high_res_screenshot(960, 540, str(OUT / name), camera=camera, force_game_view=True)
            if not state['task'].is_valid_task():
                raise RuntimeError('UE did not create a screenshot task')
    except Exception as exc:
        report['error'] = str(exc)
        unreal.log_error(str(exc))
        finish()

handle = unreal.register_slate_post_tick_callback(tick)
