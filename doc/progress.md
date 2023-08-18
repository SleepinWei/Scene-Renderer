# Progress & Plan

## Feature

+ PBR Material
+ Directional Light & Point Lights
+ Terrain : Tessellation & Quad Tree version
  using compute shaders.
+ HDR
+ Simple Path Tracing framework
+ Physically based sky
+ Model loading
+ gltf
+ SSAO
+ [ ] GPU Culling
+ [x] Image Based Lighting
+ [ ] Virutal Texuters ?

## restrictions

1. roughness and metallic may be independent images or in a single image file (gltf). thus, roughness is sampled on green channel and metallic on blue channel.

## issues

1. [x] improve mesh loading. currently only mesh0 is loaded, not the whole scene and a Gameobject can only have 1 mesh part.

2. 重构 async load 系统，用一个 bool 量控制是否多线程加载。现在 texture 在多线程中加载到内存，在第一帧运行时生成 OpenGL objects 并完成绑定。

3. [x] 顺着 2 的思路，必须加一个全局配置类

3. [x] 希望将 Mesh Filter 的绘制逻辑修改为使用自带的 material 指针绘制，不用从 Mesh Renderer 中再拿取。

4. 希望 shader 绑定到 material 上，而不是 mesh renderer.

5. 使用 addShape 函数初始化 MeshFilter 时，没有 material，导致无法绘制。对 灯光 等物体有用。

6. [x] light 信息每帧都重新载入 uniform buffer，考虑只在数值改变时重新载入。（添加 dirty 标志位解决）

7. IBL 仅支持预渲染的天空，且 specular 分量直接暴力采样。

8. RenderScene 必须为全局变量，因为 Ocean 中需要获得 sky，所以无法干掉全局变量。相信怎么改 Ocean

9. 如果是非阻塞式 Path Tracing，按下 R 时可能过了好多帧，会启动很多任务，需要解决。