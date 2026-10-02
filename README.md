# Scene Renderer

Scene Renderer 起源于同济大学计算机图形学课程小组项目，包含实时渲染器与独立的 CPU 路径追踪器。macOS 默认使用原生 **Metal** 后端；原有 OpenGL 后端仍可选择。

## Metal 构建与运行

需要 macOS、Xcode（含 Metal Toolchain）、CMake、Python 3.9+ 和 Homebrew：

```sh
brew install glfw assimp yaml-cpp glslang spirv-cross
cmake -S . -B build -DSCENERENDERER_METAL=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
./build/Scene-Renderer --demo
```

请在项目根目录运行。`--demo` 使用自动生成的材质、天空、海洋、地形和草场景；原始 `asset/` 资源包未包含在仓库中，配置场景缺失时也会自动使用此演示。

GPU 缓冲区、纹理、计算、绘制、曲面细分、ImGui 和呈现均使用 Metal，不创建 OpenGL 上下文。现有场景组件中的 GL 风格资源接口作为迁移边界保留。详见[中文迁移说明](doc/metal.md)。

## 经典场景：Metal 实际渲染

以下截图由本项目在 Apple M4 上以 **960 × 720** 离屏渲染生成，并开启 Metal API 与着色器校验。模型资源随仓库提供，下载来源、许可、修改说明及校验值见[场景资源说明](samples/README.md)。

### Cornell Box 风格室内场景

红绿侧墙、两个旋转箱体、顶灯面板，展示 PBR、点光源阴影、SSAO、RSM 近似间接光照与 HDR。几何由代码自行构建；这是实时渲染示例，不是 Cornell 原始测量基准。顶灯面板的自发光外观和实际点光源照明分别处理，并使用弱前方补光。

```sh
./build/Scene-Renderer --classic cornell
```

![Cornell Box 风格场景的 Metal 实时渲染](img/metal/cornell.png)

### Stanford Bunny

导入 Stanford 官方 PLY 网格，并展示白色非金属、金色金属与蓝色非金属三种 PBR 材质，以及阴影和大气环境光。

```sh
./build/Scene-Renderer --classic bunny
```

![Stanford Bunny 三种 PBR 材质的 Metal 渲染](img/metal/bunny.png)

### Damaged Helmet

导入 Khronos glTF 示例模型，使用原始底色、法线、金属度／粗糙度与 AO 纹理。模型归属 theblueturtle_ 与 ctxwing，包含非商业使用要求，详见[资源许可说明](samples/README.md)。

```sh
./build/Scene-Renderer --classic helmet
```

![Damaged Helmet 的 Metal 渲染](img/metal/helmet.png)

重新生成全部截图：

```sh
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 ./build/Scene-Renderer --render-gallery img/metal
```

## 功能

| 类别 | 实现 |
| --- | --- |
| 实时管线 | 延迟渲染、前向渲染、HDR 色调映射、SSAO、RSM 间接光照 |
| 材质 | PBR、各向异性、清漆层、近似 SSS、细分位移；延迟材质路径支持各向同性 PBR，其余变体走前向路径 |
| 阴影 | 级联方向光阴影、PCSS 软阴影、点光源立方体阴影 |
| 自然场景 | 物理大气与天空 LUT、IBL、FFT 海洋、GPU 四叉树 LOD 地形、实例化草 |
| 资源与界面 | Assimp 模型导入、glTF、JSON 场景、GameObject/Component 结构、ImGui |
| CPU 路径追踪 | 球／三角形／矩形、基础材质、BVH、重要性采样、多线程离线渲染 |

`Cloud` 在当前源码中只有声明，尚无体积云实现。自动曝光、CPU 路径追踪的 PBR 与 glTF 场景支持也仍待实现。Metal 初版采用单命令队列并等待每帧完成，尚未优化为多帧并行提交；当前展示不包含性能对比结论。

## 验证

```sh
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 ctest --test-dir build --output-on-failure
./build/Scene-Renderer --demo --frames 3
```

测试覆盖着色器库加载、计算结果读回、材质、曲面细分、天空、海洋、地形、草、阴影、SSAO/RSM、前向 HDR/SSS 深度，以及经典场景连续切换。检查 HDR、法线及天空等浮点输出是否有限且非空。原场景的视觉对照仍需补齐原始资产包。

旧 OpenGL 后端可独立构建：

```sh
cmake -S . -B build/opengl -DSCENERENDERER_METAL=OFF
cmake --build build/opengl -j 8
```

## 历史效果图

+ 头盔
  ![helmet](./img/helmet_mine.png)
+ 天空与海洋
  ![sky_ocean](./img/sky.png)
  ![sky2](./img/sky2.png)
  ![sky3](./img/sky3.png)
+ 地形
  ![terrain](./img/terrain.png)
  ![terrain2](./img/terrain_dynamic_lod.png)
+ 室内
  ![house](./img/house.png)
  ![house2](./img/house2.png)
+ CPU 路径追踪
  Cornell Box（100 spp，最大深度 10）
  ![path_tracing](./img/ray_tracing.png)

## 操作

`W/A/S/D` 移动，`E/Q` 上下移动，按住 `Shift` 加速；按住鼠标右键调整视角。

## 依赖

- GLFW、Assimp、yaml-cpp
- 随仓库提供的 GLM、ImGui、stb、tinygltf、glad 等头文件与源码
- Metal 构建：Xcode Metal Toolchain、glslang、SPIRV-Cross
- OpenGL 构建：平台 OpenGL 库

## 项目成员

* [zyw](https://github.com/SleepinWei)
* [jyx](https://github.com/1696762169)
* [ljw](https://github.com/XiaoXKKK)
* [zzl](https://github.com/qbdl)
* [ckx](https://github.com/Moondok)
* [lkj](https://github.com/qbdl)
