# Scene Renderer

Scene Renderer starts as a group project of Computer Graphics Course in Tongji University.

It is a renderer based on modern opengl, focusing on natural and in-door scene rendering. It also contains a path-tracing offline renderer, but currently these two are still separated.

## Feature

### Rendering

* Physically based skies
* Volumetric cloud
* PCSS soft shadows
* PBR Material (only isotropic is supported in deferred pipeline)
  * isotropic
  * anisotropic
  * clear coat
  * approx of sss (gdc 2011: fast approximation)
* Reflective shadow maps
* Realtime Ocean
* Procedural Terrain
  * quad tree lod powered by GPU driven pipeline (GDC 2018)
* Deferred Pipeline
* SSAO
* IBL
* simple grass implementation based on gpu instancing.
* [ ] auto exposure

### Other functions

* multi thread model loading
* asset loading from json files
* gameobject-component structure
* gltf scene loading

### Modern Opengl

* improve datashader storage buffer + uniform buffer
* use compute shader to accelerate computation
  * quad tree traversal & mesh generatioin in procedural terrain
  * IFFT in ocean surface generation
  * Transmission map & sky-view lut computation in sky rendering

### Path Tracing

* basic shapes: sphere, triangle, rectangle
* simple materials : lambertian, dieletric, medium
* BVH
* importance sampling (material & light)
* multi-thread rendering on cpu
* [ ] pbr material
* [ ] gltf scene loading
* [ ] 

## Gallery

+ a random helmet
  ![helmet](./img/helmet_mine.png)
+ Sky & ocean
  ![sky_ocean](./img/sky.png)
  ![sky2](./img/sky2.png)
  ![sky3](./img/sky3.png)
+ Terrain
  ![terrain](./img/terrain.png)
  ![terrain2](./img/terrain_dynamic_lod.png)
+ House
  ![house](./img/house.png)
  ![house2](./img/house2.png)
+ Path tracing 
  cornell box (100 spp, max depth 10)
  ![path_tracing](./img/ray_tracing.png)

## Control

use `w a s d` to move, use `e q` to move up and down, hold `shift` to accelerate. 

hold `mouse right button` to look around. 

## 3rd party dependencies

+ assimp 5.0.1
+ opengl32.lib
+ glfw3
+ IrrXML
+ yaml-cpp

## Group Members

* [zyw](https://github.com/SleepinWei)
* [jyx](https://github.com/1696762169)
* [ljw](https://github.com/XiaoXKKK)
* [zzl](https://github.com/qbdl)
* [ckx](https://github.com/Moondok)
* [lkj](https://github.com/qbdl)
