# Scene Renderer

[English](README.md) · **简体中文**

一个用于学习和实验的 **C++17 图形渲染器**，起源于同济大学计算机图形学课程项目。统一 **RHI** 支持原生 **Metal／Vulkan**，提供交互式编辑器、实时效果和 CPU／GPU 路径追踪。

![Sponza 中庭：原生 Metal，RSM 关闭](img/metal/sponza-direct.png)

[快速运行](#快速运行) · [项目能力](#项目能力) · [实时渲染](#实时渲染) · [Cluster 与动态 LOD](#cluster-与动态-lod) · [路径追踪](#路径追踪) · [开发状态](#开发状态) · [文档](#文档)

## 快速运行

macOS 需要 Xcode（含 Metal Toolchain）、CMake、Python 3.9+ 及以下依赖。从仓库根目录执行：

```sh
brew install glfw assimp yaml-cpp glslang spirv-cross
cmake -S . -B build -DSCENERENDERER_RHI_BACKEND=Metal -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Scene-Renderer -j2
./build/Scene-Renderer --memory-info
./build/Scene-Renderer --classic cornell --size 1280x720
```

Cornell、天空、海洋和潜水场景由程序生成，无需下载大型资源。Sponza／San Miguel 先执行 `python3 tools/fetch_gi_assets.py`；其他下载与 Vulkan／MoltenVK 构建见[构建与运行](docs/getting-started.md)。岩洞生成器和可视化工具需要 NumPy、Pillow。

按住鼠标右键转头，**W/A/S/D** 移动，**Q/E** 上下移动，**Shift** 加速；编辑器面板可调整光照、水体、cluster LOD 与后处理。

**默认保持内存准入保护：**进程预算取物理内存 20% 与 3 GiB 中较小值，GPU 默认占一半，编辑器最多一帧在途。16 GiB 机器对应 **进程 3 GiB／GPU 1.5 GiB**。系统压力下拒绝新增重资源工作，保留已经发布的编辑器画面。通过 `--memory-budget-mib`、`--gpu-resource-budget-mib` 和 `--memory-info` 配置或查看；这是协作式分配保护，具体范围见[内存保护](docs/memory-safety.md)。

## 项目能力

| 方向 | 实现与说明 |
| --- | --- |
| 材质与光照 | PBR、CSM／PCSS、GTAO／SSAO、材质过滤与改进 TAA；[DDGI 漫反射 GI](docs/ddgi.md)与可选 PT 探针参考。 |
| 几何与可见性 | 物体／三角形剔除、分层 meshlet、GPU 屏幕误差 LOD、间接绘制与两阶段 cluster Hi-Z。[几何管线](docs/hierarchical-cluster-geometry.md)。 |
| 自然环境 | 大气散射、体积云、高度／材质 Virtual Texture、地形 LOD、程序植被与共享天气。[环境交付规格](docs/mountain-lake-environment-spec.md)。 |
| 水体 | FFT 风浪与涌浪、反射／折射、吸收／散射、近岸波浪、泡沫、海床焦散与水下太阳光束。[水体渲染](docs/ocean-fft-and-rendering-review.md)。 |
| 后处理 | HDR Bloom、景深、相机运动模糊、色调映射／调色、FXAA、锐化、暗角、色差与颗粒。[控制与范围](docs/post-processing.md)。 |
| 路径追踪 | CPU／Metal／Vulkan 积分器、纹理材质、介电／体积输运、毛发／布料、共享 BLAS／TLAS、photon mapping 和 OIDN。[GPU PT](docs/path-tracing-gpu.md)。 |
| 运行时 | 不可变世界快照、独立渲染线程、有界准备任务、资源预算与 RenderGraph pass 剔除。[系统设计](docs/system-design.md)。 |

## 实时渲染

以下精选图片由本项目的原生 Metal 渲染器输出。[详细功能展示](docs/rendering-showcase.zh-CN.md)保留全部对照与中间结果；[完整图集](docs/rendering-gallery.md)记录采样配置和复现步骤。

### 建筑与漫反射 GI

| San Miguel：RSM 关闭 | Sponza：DDGI 开启 |
| --- | --- |
| ![原生 Metal 渲染的 San Miguel 庭院](img/metal/san-miguel-direct.png) | ![原生 DDGI 渲染的 Sponza](img/diagnostics/ddgi/metal/sponza-on.png) |

DDGI 通过辐照度探针提供局部天空可见性与漫反射染色。编辑器使用六面光栅捕获，可选离线探针参考复用 PT；当前为实时 GI 第一阶段，漏光、光照响应和材质求值与 PT 仍有差距。[探针布局与 GPU 可视化](docs/ddgi.md#原理与-sponza-探针布局) · [TAA 对照](docs/realtime-anti-aliasing.md)。RSM 默认关闭，保留为历史演示。

### 大气、地形与水体

| Mountain Lake：空气透视与山影 | 可穿越三维体积云 |
| --- | --- |
| ![山湖大气侧光](img/diagnostics/environment/aerial-side/mountain-lake-side-combined.png) | ![原生 Metal 三维体素云](img/metal/cloud-volume.png) |

地形分页、FFT 湖面和植被共享太阳、风与天气控制。[Mountain Lake](docs/mountain-lake.md) · [大气空中透视](docs/atmospheric-aerial-perspective.md) · [程序树木](docs/procedural-trees.md)。

| 海底潜水：太阳光束与焦散 | 抬头观察：折射天空、太阳与浮标 |
| --- | --- |
| ![海底潜水太阳光束](img/diagnostics/water/air-refraction/underwater-dive.png) | ![折射天空与水上浮标](img/diagnostics/water/air-refraction/underwater-dive-snell-window.png) |

使用 `--classic underwater-dive` 漫游海底。Ocean 面板可独立控制雾、光束、焦散和水出空气折射。[潜水场景与操作](docs/water-underwater-diving.md) · [折射与性能](docs/water-air-refraction.md) · [近岸波浪与泡沫](docs/water-coastal-features.md)。

### 后处理

| 默认显示 | ACES 与组合后处理 |
| --- | --- |
| ![Cornell 默认显示](img/diagnostics/post-processing/cornell-post-off.png) | ![Cornell 原生组合后处理](img/diagnostics/post-processing/cornell-post-combined.png) |

**Post processing** 面板提供独立开关与柔和电影预设。额外效果默认关闭；景深使用不透明深度，运动模糊目前覆盖相机运动。[管线与各效果对照](docs/post-processing.md)。

## Cluster 与动态 LOD

高密度石灰岩洞包含 **9 个共享网格／288 万唯一三角形**，实例化为 **76 次绘制／2,484 万逻辑三角形**。裂纹和矿物起伏直接位移真实顶点；程序资产包约 **84.8 MB**，检查 **10 GB 磁盘上限**。

![岩洞：已捕获的 PBR、真实 GPU cluster 与父 LOD 层级](img/diagnostics/cluster-cave/dynamic-lod.gif)

动画使用**内存准入保护接入前**的原生捕获。PBR、唯一 cluster ID 与父层级展示已记录的相机位置，条形图统计提交三角形。在固定 4 px 下，旧捕获相对各自视角的 0 px 参考减少 **近景 25.57% 至更远景 54.64%**，物体／遮挡剔除关闭。播放速度和三角形减少量不代表渲染帧率。[局部分块边界与三角形线框](img/diagnostics/cluster-cave/detail.png)。

最新实现提供 **11 个相机视角**，包含两个洞内大厅和同视角 **0／2／4／8 px** 对比。参考／LOD 图复用一个渲染器；诊断每批最多 **65,536 个三角形**，默认预算下预留 **24 MiB 临时内存**，大规模成员表逐行写入 JSONL。独立的 `cluster-cave-small` 保留高密度源模型。**构建与四项内存／RHI 测试通过；新 GPU 回放、内存峰值及新增视角仍待运行验证。**[步骤与验收范围](docs/cluster-cave.md) · [实现检查记录](img/diagnostics/cluster-cave/implementation-validation.json)。

<details>
<summary>独立小规格岩洞：系统内存压力正常后生成和捕获</summary>

```sh
python3 tools/prepare_cluster_cave.py --level 6
./build/Scene-Renderer --memory-info
./build/Scene-Renderer --cluster-scene-flow cluster-cave-small build/cluster-cave-bounded-small --backend Metal
python3 tools/visualize_cluster_scene_compact.py build/cluster-cave-bounded-small img/diagnostics/cluster-cave-bounded-small
```

小规格生成与受保护捕获待验收；保留现有进程／GPU 预算和系统压力检查。

</details>

其他场景的数据对应各自记录的相机与构建配置：

| 场景 | 几何规模 | 验收依据 |
| --- | --- | --- |
| Coastal Cliff | 943 万逻辑三角形，十个共享原始 LOD0 实例 | 已记录的父构建改动使同视角 4 px 减量从 3.24% 提升至 36.56%。[画质与计时](docs/cluster-parent-lod-optimization.md)。 |
| Bistro 外景 | 283 万源三角形、132 个材质网格 | 街区分块与相机路线 LOD；记录的减量较小，为 2.95–5.04%。[动画、连通性与测量](docs/rendering-showcase.zh-CN.md#bistro完整街区的-cluster-分块与-lod-演示)。 |
| Sponza 与 Stanford 扫描模型 | 建筑与高密度扫描表面 | [Meshlet 利用率](docs/cluster-partition-and-hierarchy.md)、[两阶段 Hi-Z](docs/cluster-two-stage-hiz.md)及[动态 LOD](docs/cluster-hiz-optimization-and-lod.md)。 |

这是参考 Nanite 思路的**几何原型**。分页流送、可见性缓冲着色和混合光栅仍未实现；UE Nanite 的峡谷图作为[独立参考引擎结果](docs/cluster-cliff-nanite-test.md)标注。

## 路径追踪

建筑与自然场景也提供路径追踪结果，由本项目的 CPU／Metal／Vulkan 积分器渲染。下方图片均使用 **OIDN**，表头标明积分方式。[原始图、采样与复现](docs/rendering-gallery.md#路径追踪)。

| Sponza：Metal PT，64 spp | San Miguel：Metal PT，64 spp |
| --- | --- |
| ![路径追踪 Sponza 中庭](img/path-tracing/oidn-sponza.png) | ![路径追踪 San Miguel 庭院](img/path-tracing/oidn-san-miguel.png) |

| 地形与草：Metal PT，128 spp | Mountain Lake：Metal PT，128 spp |
| --- | --- |
| ![路径追踪地形与草](img/path-tracing/pt-terrain.png) | ![路径追踪山湖](img/path-tracing/pt-mountain-lake.png) |

| 湖岸沙滩：Metal PT，1024 spp | 浅水折射：Metal PT，4096 spp |
| --- | --- |
| ![路径追踪沙滩与湿沙](img/path-tracing/pt-mountain-lake-beach.png) | ![路径追踪 FFT 浅水](img/path-tracing/pt-ocean-clear.png) |

程序地形、植被和 FFT 水面冻结在记录的模拟时刻。沙滩纹理按世界尺度采样；浅水太阳路径使用 BSDF／phase 混合提议。[捕获细节与修复](docs/path-tracing-procedural.md) · [实时／PT 海洋对齐](docs/ocean-reflection-integration.md)。

| 玻璃 Dragon：CPU BDPT | 玉石 Dragon：Metal PT，次表面散射 |
| --- | --- |
| ![CPU BDPT 玻璃龙焦散](img/path-tracing/dragon-glass.png) | ![Metal PT 玉石次表面散射](img/path-tracing/jade-polished-boundary.png) |

[Photon mapping 与泳池焦散](docs/path-tracing-photon-mapping.md)、[CPU SPPM](docs/path-tracing-sppm.md)、[各向异性 GGX](docs/path-tracing-materials.md)及[毛发／针织布料](docs/path-tracing-grooms-sunlight.md)有独立对照。当前毛发样例使用静态三角薄带，PBRT 原生曲线图片是独立参考。

| Blender Classroom：本项目 Metal PT + OIDN | Barcelona Pavilion：本项目 Metal PT + OIDN |
| --- | --- |
| ![本项目路径追踪的 Classroom](img/path-tracing/blender-classroom-materials.png) | ![完整植被与薄叶透射的 Barcelona](img/path-tracing/blender-barcelona-thin.png) |

Blender 资产通过离线导出共享实例几何。Cycles 参考图与同输入误差测量单独标注；材质转换和外观对齐尚未完整实现。[资源、转换与 Cycles 对照](docs/blender-path-tracing.md)。

## 开发状态

| 方向 | 当前范围与待完成工作 |
| --- | --- |
| 岩洞捕获 | 新的有界实现已构建并通过内存／RHI 检查；受保护 GPU 正确性、内存测量、新视角和小规格生成待验收。 |
| 漫反射 GI | 第一阶段 DDGI 可选开启；漏光控制、探针响应及与 PT 的着色一致性仍需改进。 |
| Cluster 遮挡 | 收益取决于场景复杂度；Sponza 尚未确认稳定加速。通用默认值关闭遮挡，具体场景预设可开启。 |
| 水体与天气 | 光学捕获和散射仍有近似，高开销预设需要继续优化；现有捕获不证明达到 60 fps。 |
| 参考一致性 | 光栅／PT 材质、水体输运与 Blender 闭包仍存在已记录差异；原生毛发曲线与动力学待开发。 |

图片和测量对应链接报告中的具体配置。主线程负责世界、输入与编辑器，通过不可变快照交给独立渲染线程；资源准备有界执行，底层共用 RHI。[运行时与平台范围](docs/system-design.md)。

## 文档

| 入口 | 内容 |
| --- | --- |
| [构建与运行](docs/getting-started.md) | 依赖、Metal／Vulkan 选择、资源下载、操作与捕获命令 |
| [详细功能展示](docs/rendering-showcase.zh-CN.md) | 原 README 的全部图片、算法讲解、对照与性能记录 |
| [完整图集与参数](docs/rendering-gallery.md) | 采样、效果设置、中间缓冲和复现步骤 |
| [岩洞 cluster／LOD](docs/cluster-cave.md) | 场景规格、有界捕获和待完成验收 |
| [内存保护](docs/memory-safety.md) | 预算、压力行为、原生分配检查和协作式限制 |
| [技术文档](docs/README.md) | 系统设计、实现报告、测试与后续计划 |
| [资源归属与许可](samples/README.md) | 模型／纹理来源、作者及使用条件 |

详细技术文档主要为中文。[课程成员与历史效果](docs/archive/historical-gallery.md)。
