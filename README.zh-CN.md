# Scene Renderer

[English](README.md) · **简体中文**

一个用于学习和实验的 **C++17 图形渲染器**，起源于同济大学计算机图形学课程项目。通过经典测试场景与自然环境，展示实时光照、材质、GPU 计算和 CPU／GPU 路径追踪。

实时渲染采用统一 **RHI**，支持原生 **Metal／Vulkan**；编辑器提供相机漫游和效果参数调整。主要功能包括 PBR、CSM／PCSS、RSM、GTAO／SSAO、大气与体积云、FFT 海洋、高度／材质 Virtual Texture、植被和 TSAA。

[构建与运行](docs/getting-started.md) · [完整图集与参数](docs/rendering-gallery.md) · [系统设计](docs/system-design.md) · [技术文档与修改记录](docs/README.md)

![原生 Metal 渲染的 Sponza 中庭](img/metal/sponza.png)

## 场景与效果

以下图片除标明 Blender Cycles 参考的对照外，均由本项目实际渲染。实时效果使用原生 Metal；路径追踪图标明积分方式与降噪。复现命令、采样配置、开关对照和中间产物见[完整图集](docs/rendering-gallery.md)。

### 经典场景与 PBR

Sponza、San Miguel 与 Sibenik 用于观察建筑材质、阴影和太阳／天空 RSM 间接光照；Stanford 扫描模型及 Damaged Helmet 展示金属、非金属与纹理材质。

| San Miguel 庭院 | Sibenik Cathedral |
| --- | --- |
| ![Metal San Miguel](img/metal/san-miguel.png) | ![Metal Sibenik Cathedral](img/metal/sibenik.png) |

| Stanford Bunny：三种材质 | Damaged Helmet：PBR 纹理 |
| --- | --- |
| ![Metal Stanford Bunny](img/metal/bunny.png) | ![Metal Damaged Helmet](img/metal/helmet.png) |

### 环境遮蔽 AO

地平线积分 AO 配合几何法线重建与边缘保留滤波，减少柱脚和拱廊附近的采样条带。以下 Sponza 对照关闭 RSM 与 TSAA，保持相同光照和曝光，单独展示 AO 的效果。[算法说明与更多对照](docs/ambient-occlusion.md)。

| 原 24 点 SSAO：AO 缓冲 | 地平线 AO + 边缘保留滤波：AO 缓冲 |
| --- | --- |
| ![Sponza 原 SSAO 可见性](img/ao/sponza-ao-legacy-visibility.png) | ![Sponza 地平线积分与滤波后的 AO](img/ao/sponza-ao-gtao-visibility.png) |

| AO 关闭：最终画面 | AO 开启：最终画面 |
| --- | --- |
| ![Sponza 关闭 AO](img/ao/sponza-ao-off.png) | ![Sponza 开启地平线 AO](img/ao/sponza-ao-gtao.png) |

### 天空与太阳

大气散射与解析太阳盘共享光照参数，驱动场景、间接光照和海面。远景云层支持天气分布与风速变化。

| 白天天空 | 地平线日落 | 日落云层 |
| --- | --- | --- |
| ![Metal 白天天空](img/metal/sky-day.png) | ![Metal 日落太阳](img/metal/sky-sunset.png) | ![Metal 日落体积云](img/metal/clouds-sunset.png) |

### 可穿越三维体素云

128³ XYZ 密度场结合保守距离场、GPU 间接步进和太阳光照缓存，支持靠近及进入云体。下图采用 1080p 全分辨率，关闭云时域累积；风暴预设包含内部体积闪光。

| 三维云体 | 风暴形态与内部闪光 |
| --- | --- |
| ![Metal 三维体素云](img/metal/cloud-volume.png) | ![Metal 三维风暴云](img/metal/cloud-vortex.png) |

### FFT 海洋与透明水体

主波与短波 FFT 叠加，表现浪峰、细小波纹与泡沫；折射、RGB 吸收和近似单次散射表现浅水透射与透亮浪尖。

| 大浪海面 | 浅水折射与散射 |
| --- | --- |
| ![Metal FFT 海洋](img/metal/ocean.png) | ![Metal 透明水体](img/metal/ocean-clear.png) |

实时水面已加入近景密集网格、独立水下颜色／位置层，以及沿水中路径的单次散射积分。[前后对比、验证与 GPU 耗时](docs/water-realtime-upgrade.md)。

海岸预设新增可开关的 DDA 折射与地形回退、局部多次散射、浅水波、持久泡沫和湿沙；旧预设保持新增效果关闭。[开关、对照、动画与限制](docs/water-coastal-features.md)。 [第一轮性能报告：分阶段耗时、提交间隙与边界修复](docs/water-coastal-performance.md)。

![Metal 近岸浅水波与泡沫](img/metal/coastal-beach.png)

水下视角加入沿距离的吸收／散射、水出空气折射及全反射。用 `--classic coastal-underwater` 打开，Ocean 面板可切换水下视角、雾和 **Short wave ripples**。水下预设加入 0.5–2 米 FFT 短波，使近处折射与 Snell 窗口边界随波面起伏。[图像、光学路径与验证](docs/water-underwater-rendering.md)。

| 水下观察海面 | 俯视水底 |
| --- | --- |
| ![Metal 水下海面](img/diagnostics/water/ripple-demo/coastal-underwater.png) | ![Metal 水下水底](img/diagnostics/water/seabed-demo/coastal-underwater-seabed.png) |

海床示例加入 25 cm 沙纹、细微法线与穿过实时 FFT 波面的太阳焦散。用 `--classic coastal-seabed` 打开，在 Ocean 面板切换 **FFT seabed caustics** 并调整 **Caustic strength**。[海床对照、实现与 GPU 耗时](docs/water-seabed-caustics.md)。

### 海底潜水、太阳光束与水面折射

`underwater-dive` 预设从水下 6 米开始，沿礁石与海草之间的沙质通道观察。距离吸收与单次散射形成蓝绿色能见度；漂浮颗粒、覆盖地形和水下模型的 16／48／128 米级联焦散，以及随波面聚焦的太阳光束，表现海底潜水的空间与动态光照。预设使用 32 步带阴影的体积光积分。

| 海底潜水：太阳光束与海床焦散 | 抬头观察：折射天空、太阳与浮标 |
| --- | --- |
| ![Metal 海底潜水与太阳光束](img/diagnostics/water/air-refraction/underwater-dive.png) | ![水下折射的天空、太阳与浮标](img/diagnostics/water/air-refraction/underwater-dive-snell-window.png) |

```sh
./build/Scene-Renderer --classic underwater-dive --size 1280x720
```

按住鼠标右键转头，**W/A/S/D** 移动，**Q/E** 垂直移动。将 **Camera → View pitch** 设为约 **68°** 可观察天空窗口；接近水平的水下视角会出现全反射，窗口边界随波面法线移动。

Ocean 面板可独立切换 **Underwater distance fog**、**Underwater sun shafts**、**FFT seabed caustics** 和 **Wide air refraction**，并调整光束对比度、颗粒密度与体积步数。宽空气视图补充原相机视野外的水上物体；潜水预设将散射增益设为 1，保留天空与物体的对比。Metal／Vulkan 水体验证通过。宽视图仍是屏幕空间近似，当前预设优先展示画质，尚未达到 60 fps 目标。

[潜水场景与操作](docs/water-underwater-diving.md) · [焦散覆盖范围](docs/water-caustic-cascades.md) · [太阳光束实现](docs/water-sun-shafts.md) · [折射对照、验证与性能](docs/water-air-refraction.md)

### 大地形、湖泊与植被

Mountain Lake 展示 8×8 km 山湖地形：高度与材质 VT 分页、LOD、FFT 湖面、随距离变化的草地密度，以及湿沙和沙滩 PBR 材质。

![Metal Mountain Lake 山湖场景](img/metal/mountain-lake.png)

| 湖岸草地 | 沙滩与湿沙 |
| --- | --- |
| ![Metal 湖岸植被](img/metal/mountain-lake-ground.png) | ![Metal 湖岸沙滩](img/metal/mountain-lake-beach.png) |

### Virtual Texture 与软阴影

高度／材质 VT 使用物理页缓存、页表、祖先回退与 GPU 深度反馈；五级 CSM 分配近远阴影精度，PCSS 估计随遮挡物距离变化的半影。

| VT 实际 GPU 物理缓存 | CSM 阴影与级联分区 |
| --- | --- |
| ![VT 高度与材质物理图集](img/diagnostics/vt-cache.png) | ![CSM 级联与过渡带](img/diagnostics/csm-cascades.png) |

![PCF、默认太阳 PCSS 与放大光源 PCSS 对照](img/diagnostics/pcss-comparison.png)

PCSS 对照依次为 PCF、默认太阳和放大光源；第三列用于展示更宽的半影。[页表、阴影图集与完整诊断](docs/render-diagnostics-gallery.md)。

### 后处理

统一的原生 HDR 到显示模块支持可选 Bloom、景深、相机运动模糊、调色、FXAA、锐化、暗角、色差与颗粒。**Post processing** 面板提供独立参数、指数／ACES fitted／Reinhard／线性四种色调映射和轻量电影预设；新增效果默认关闭。[操作、管线、对照与限制](docs/post-processing.md)。

| 默认显示 | ACES 与组合后处理 |
| --- | --- |
| ![Cornell 默认显示](img/diagnostics/post-processing/cornell-post-off.png) | ![Cornell 组合后处理](img/diagnostics/post-processing/cornell-post-combined.png) |

| HDR Bloom | 景深：前方箱体清晰 |
| --- | --- |
| ![Cornell HDR Bloom](img/diagnostics/post-processing/cornell-bloom.png) | ![Cornell 景深](img/diagnostics/post-processing/cornell-dof.png) |

```sh
./build/Scene-Renderer --classic cornell --size 1280x720
```

展开 **Post processing**，独立启用效果，或选择 **Soft cinematic preset**。Metal／Vulkan 验证通过；景深使用不透明深度，运动模糊目前支持相机运动。

### 路径追踪、焦散与次表面散射

CPU／Metal／Vulkan 路径追踪支持多次反弹、纹理材质、折射和均匀介质随机游走；CPU BDPT 用于平滑玻璃焦散。程序化地形、植被与 FFT 水面可冻结为离线场景。

这组头发展示采用圆形纤维散射与实时大气捕获的晴空照明。低角度天空和太阳一起旋转、增强到 10 倍源强度，并采用更窄的发丝高光，呈现暖色太阳反射；每套保留 **50,000 根发丝，Metal PT，512×512、1024 spp、12 次反弹，OIDN 展示图**。头部为中性漫反射材质。毛发来源：[Cem Yuksel](https://www.cemyuksel.com/research/hairmodels/)；头模由 **Murat Afshar** 提供。[照明、材质参数、原始图与验收](docs/path-tracing-hair-sunlight.md)。

| 强日光中的直发 | 强日光中的波浪发 |
|---|---|
| ![强日光直发](img/path-tracing/yuksel-straight-sunlit.png) | ![强日光波浪发](img/path-tracing/yuksel-wavy-sunlit.png) |

下面展示上文建筑与自然环境场景的路径追踪结果，除标明未降噪的 photon mapping 样例外，使用 Open Image Denoise（OIDN）降噪。Sponza 与 San Miguel 使用 Metal PT，分辨率为 320×240、64 spp、16 次反弹；地形、山湖与海洋使用 640×480 Metal PT，将程序化动画冻结在 8 秒时刻。采样配置与未降噪原图见[路径追踪图集](docs/rendering-gallery.md#路径追踪)。

| Sponza：Metal PT＋OIDN，64 spp | San Miguel：Metal PT＋OIDN，64 spp |
| --- | --- |
| ![路径追踪 Sponza 中庭](img/path-tracing/oidn-sponza.png) | ![路径追踪 San Miguel 庭院](img/path-tracing/oidn-san-miguel.png) |

| Stanford Dragon：CPU BDPT 玻璃焦散＋OIDN | Jade Dragon：Metal PT 次表面散射＋OIDN |
| --- | --- |
| ![Stanford 透明龙与焦散](img/path-tracing/dragon-glass.png) | ![半抛光玉龙](img/path-tracing/jade-polished-boundary.png) |

| 晴天泳池池底：Metal photon mapping＋OIDN | 水上泳池视角：Metal photon mapping＋OIDN |
| --- | --- |
| ![晴天泳池池底焦散](img/path-tracing/pool-sunlit-underwater.png) | ![晴天泳池水上焦散](img/path-tracing/pool-sunlit.png) |

短涟漪与 0.266° 太阳角半径在浅蓝瓷砖上形成清晰亮纹；400 万发射路径、64 spp、半径 0.025，水体使用冻结的解析涟漪网格。[近景原图](img/path-tracing/pool-sunlit-underwater-raw.png) · [平水面对照](img/path-tracing/pool-sunlit-flat.png) · [复现与验收](docs/path-tracing-photon-mapping.md#晴天泳池更明显的网状亮纹)。

| 池底：Metal photon mapping，100 万条光路＋32 spp，未降噪 | 水体折射焦散贡献，未降噪 |
| --- | --- |
| ![池底光子映射焦散](img/path-tracing/pool-photon.png) | ![水体折射焦散 AOV](img/path-tracing/pool-photon-caustics.png) |

池底使用固定解析波形；固定半径 photon map 为有偏估计，CPU／Metal／Vulkan 共用相同光子数据。[复现、验收与通用 PT 优化](docs/path-tracing-photon-mapping.md)。

| 地形与草丛：Metal PT＋OIDN，128 spp | Mountain Lake：Metal PT＋OIDN，128 spp |
| --- | --- |
| ![路径追踪地形与草丛](img/path-tracing/pt-terrain.png) | ![路径追踪山湖与倒影](img/path-tracing/pt-mountain-lake.png) |

| 湖岸沙滩：Metal PT＋OIDN，1024 spp | 浅水折射：Metal PT＋OIDN，4096 spp |
| --- | --- |
| ![路径追踪湖岸沙滩与湿沙](img/path-tracing/pt-mountain-lake-beach.png) | ![路径追踪 FFT 浅水](img/path-tracing/pt-ocean-clear.png) |

沙滩保留世界坐标纹理细节；水下太阳路径使用 BSDF／相位混合采样，保持原太阳与曝光。[沙滩原图](img/path-tracing/pt-mountain-lake-beach-raw.png) · [浅水原图](img/path-tracing/pt-ocean-clear-raw.png) · [修复与验证](docs/path-tracing-procedural.md#水下太阳路径采样)。

![路径追踪 FFT 大浪海洋：Metal PT＋OIDN，256 spp](img/path-tracing/pt-ocean.png)

### Blender 测试场景

Blender 官方 Classroom 与 Barcelona Pavilion 经离线导入后，由本项目 Metal PT 渲染并使用 OIDN 降噪。[资源、材质转换与验证](docs/blender-path-tracing.md)。

| Classroom | Barcelona Pavilion |
| --- | --- |
| ![Classroom Metal PT＋OIDN](img/path-tracing/blender-classroom-materials.png) | ![Barcelona Pavilion 池水 Metal PT＋OIDN](img/path-tracing/blender-barcelona-water.png) |

Barcelona 已接入顶层 Bump／Normal Map 图集、介电材质的粗糙度纹理和封闭池水的反射／折射／吸收；修复隐藏粒子发射器遮住水面及 UV 方向。池水为明确的艺术配置（IOR 1.333、深度 0.5 世界单位），可切换源参数。[原始预览](img/path-tracing/blender-barcelona-water-raw.png) · [复现与限制](docs/blender-path-tracing.md#barcelona-材质与池水) · [验收记录](img/path-tracing/blender-water-validation.json)。

CPU／Metal／Vulkan 使用 **共享几何 BLAS/TLAS**。新版 Barcelona 保留全部 **20,622 个植被粒子**，约 **5,497 万个实例三角形**。revision 5 接入 Blender corner MikkTSpace 切线，并分别烘焙薄叶的漫反射与透射；CPU 几何／加速数据约 **54.4 MiB**。以下为 640×360、256 spp 的 Metal PT＋OIDN 预览。[原始图](img/path-tracing/blender-barcelona-thin-raw.png) · [材质验收与复现](docs/blender-path-tracing.md#薄玻璃太阳反射链)。

![Barcelona 完整植被与薄叶透射：Metal PT＋OIDN](img/path-tracing/blender-barcelona-thin.png)

Blender Cycles 原场景 GT：1024 spp、原材质与完整植被，无降噪、强度截断或 glossy 模糊。旧引擎预览使用缩减粒子预算；上方完整植被图的池水与材质配置仍与源场景不同；[对照与线性参考](docs/blender-path-tracing.md#blender-cycles-gt)。

![Barcelona 原场景 GT：Blender Cycles 1024 spp](img/path-tracing/blender-barcelona-cycles-gt.png)

新增独立的 **同参数 Cycles 对照**：双方使用相同冻结几何、512 个粒子、相机、贴图、灯光、GGX 闭包、薄玻璃和封闭池水。320×180、4096 spp、深度 24，无降噪的 Metal／Cycles 总 RGB 能量差 **0.067%**，逐像素仍有反射采样噪声。这项受控基线尚不覆盖原始 Blender shader 图与法线图。[测量、独立种子噪声与复现](docs/blender-path-tracing.md#同参数-cycles-线性对照)。

分离闭包法线并增加 bump shadowing 后，小场景 Metal／Cycles 原始 RGB 相对 L1：Lambert／薄叶从 **0.838% 降至 0.290%**，GGX／薄叶从 **2.662% 降至 0.582%**（192×96、4096 spp）。太阳提议将完整 Barcelona 的 160×90、512 spp、seed 1 对照从 **4.67% → 3.53% → 2.98%**（水体、再到薄玻璃反射）；薄玻璃异常像素从 **29.06 降至 0.0656**（Cycles **0.0462**），RGB RMSE **0.156 → 0.049**。seed 2 的 L1 从 **3.07% 略升至 3.19%**，池水／薄叶稀有路径与原分层材质仍需改进。统计缓存的独立单线程 trace 微基准提升约 **1.58 倍**。[最新验收与限制](img/path-tracing/blender-thin-validation.json)。

![完整 Barcelona 法线图对照：Metal 原图、Cycles 原图、线性绝对误差](img/path-tracing/blender-barcelona-thin-matched.png)

## 系统概览

主逻辑管理世界、输入和编辑器，通过不可变场景快照向独立渲染线程发布数据。渲染线程准备 GPU 资源并调度各效果，RHI 统一资源、管线、命令与呈现；Metal／Vulkan 复用共享 GLSL 生成的着色器。详细流程、资源管理和平台边界见[系统设计](docs/system-design.md)。

## 文档

| 入口 | 内容 |
| --- | --- |
| [构建与运行](docs/getting-started.md) | 依赖、后端选择、场景下载、启动指令、操作与截图复现 |
| [完整效果图集](docs/rendering-gallery.md) | 各效果的配置、对照图、中间产物、测量与算法边界 |
| [系统设计](docs/system-design.md) | 逻辑／渲染分离、每帧流程、RHI、着色器构建与模块划分 |
| [技术文档索引](docs/README.md) | 实现说明、修改记录、验证结果与后续计划 |
| [资源归属与许可](samples/README.md) | 模型、纹理的来源、作者和使用条件 |

本项目用于学习与实验；截图、数据和功能范围对应各文档记录的配置。[课程成员与历史效果](docs/archive/historical-gallery.md)。
