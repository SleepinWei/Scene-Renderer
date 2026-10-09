from pathlib import Path
import subprocess, os
root=Path('build/pt-grooms-sunlit')
env=dict(os.environ,MTL_DEBUG_LAYER='0',MTL_SHADER_VALIDATION='0')
binary='./build/pt-hair-sunlit/runtime/pt-package-render'
for name in ['sweater']:
 for backend in ['Metal','Vulkan','CPU']:
  prefix=root/name/('check-'+backend.lower())
  command=[binary,'--backend',backend,'--scene',str(root/name/'scene.json'),'--size','128x128','--samples','64','--bounces','12','--seed','1','--threads','6','--output',str(prefix)]
  with prefix.with_suffix('.txt').open('w') as log:
   subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
  print(name,backend,'check finished',flush=True)
 prefix=root/name/'metal'
 command=[binary,'--backend','Metal','--scene',str(root/name/'scene.json'),'--size','768x768','--samples','1024','--bounces','12','--seed','1','--output',str(prefix)]
 with prefix.with_suffix('.txt').open('w') as log:
  subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 print(name,'768x768 / 1024 spp finished',flush=True)
 with (root/name/'denoise.txt').open('w') as log:
  subprocess.run(['./build/pt/Scene-Renderer','--path-trace','--pt-denoise-input',str(prefix)+'.pfm','--pt-denoise-device','cpu','--pt-exposure','0.85','--pt-output',str(root/name/'filtered')],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 print(name,'OIDN finished',flush=True)
