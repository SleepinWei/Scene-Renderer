# Scene Renderer

[English](README.md) · **简体中文**

一个用于学习和实验的 **C++17 图形渲染器**，起源于同济大学计算机图形学课程项目。通过经典测试场景与自然环境，展示实时光照、材质、GPU 计算和 CPU／GPU 路径追踪。

实时渲染采用统一 **RHI**，支持原生 **Metal／Vulkan**；编辑器提供相机漫游和效果参数调整。主要功能包括 PBR、CSM／PCSS、RSM、SSAO、大气与体积云、FFT 海洋、高度／材质 Virtual Texture、植被和 TSAA。

[构建与运行](docs/getting-started.md) · [完整图集与参数](docs/rendering-gallery.md) · [系统设计](docs/system-design.md) · [技术文档与修改记录](docs/README.md)

![原生 Metal 渲染的 Sponza 中庭](img/metal/sponza.png)

## 场景与效果

以下图片由本项目实际渲染。实时效果使用原生 Metal；路径追踪图标明积分方式与降噪。复现命令、采样配置、开关对照和中间产物见[完整图集](docs/rendering-gallery.md)。

### 经典场景与 PBR

Sponza、San Miguel 与 Sibenik 用于观察建筑材质、阴影和太阳／天空 RSM 间接光照；Stanford 扫描模型及 Damaged Helmet 展示金属、非金属与纹理材质。

| San Miguel 庭院 | Sibenik Cathedral |
| --- | --- |
| ![Metal San Miguel](img/metal/san-miguel.png) | ![Metal Sibenik Cathedral](img/metal/sibenik.png) |

| Stanford Bunny：三种材质 | Damaged Helmet：PBR 纹理 |
| --- | --- |
| ![Metal Stanford Bunny](img/metal/bunny.png) | ![Metal Damaged Helmet](img/metal/helmet.png) |

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

### 路径追踪、焦散与次表面散射

CPU／Metal／Vulkan 路径追踪支持多次反弹、纹理材质、折射和均匀介质随机游走；CPU BDPT 用于平滑玻璃焦散。程序化地形、植被与 FFT 水面可冻结为离线场景。

下面展示上文建筑与自然环境场景的路径追踪结果，均使用 Open Image Denoise（OIDN）降噪。Sponza 与 San Miguel 使用 Metal PT，分辨率为 320×240、64 spp、16 次反弹；地形、山湖与海洋使用 640×480 Metal PT，将程序化动画冻结在 8 秒时刻。采样配置与未降噪原图见[路径追踪图集](docs/rendering-gallery.md#路径追踪)。

| Sponza：Metal PT＋OIDN，64 spp | San Miguel：Metal PT＋OIDN，64 spp |
| --- | --- |
| ![路径追踪 Sponza 中庭](img/path-tracing/oidn-sponza.png) | ![路径追踪 San Miguel 庭院](img/path-tracing/oidn-san-miguel.png) |

| Stanford Dragon：CPU BDPT 玻璃焦散＋OIDN | Jade Dragon：Metal PT 次表面散射＋OIDN |
| --- | --- |
| ![Stanford 透明龙与焦散](img/path-tracing/dragon-glass.png) | ![半抛光玉龙](img/path-tracing/jade-polished-boundary.png) |

| 地形与草丛：Metal PT＋OIDN，128 spp | Mountain Lake：Metal PT＋OIDN，128 spp |
| --- | --- |
| ![路径追踪地形与草丛](img/path-tracing/pt-terrain.png) | ![路径追踪山湖与倒影](img/path-tracing/pt-mountain-lake.png) |

| 湖岸沙滩：Metal PT＋OIDN，256 spp | 浅水折射：Metal PT＋OIDN，256 spp |
| --- | --- |
| ![路径追踪湖岸沙滩与湿沙](img/path-tracing/pt-mountain-lake-beach.png) | ![路径追踪 FFT 浅水](img/path-tracing/pt-ocean-clear.png) |

![路径追踪 FFT 大浪海洋：Metal PT＋OIDN，256 spp](img/path-tracing/pt-ocean.png)

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
